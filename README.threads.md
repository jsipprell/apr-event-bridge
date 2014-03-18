Apache Portable Runtime Libevent Bridge Thread Support
======================================================

Full native threading is available on supported platforms (typically via
pthreads, but there is some rudimentary support for windows threading). At a
minimum, *BOTH* the APR and libevent *MUST* be configured for thread support
in order for AEB to compile correctly and be usable in a multithreaded
application.

AEB will attempt to enable thread support by default when configure is first
run but will emit a warning and disable thread support if the above conditions
are not met or something else goes wrong. This can be forced by passing
``--enable-threads`` or ``--disable-threads`` to configure.

##Using AEB Threads##

* All threading primitives should generally be accessed through the APR,
  although certain basic operations will work correctly as long as pthreads is
  underlying all libraries.

* Generally speaking, event-based I/O is vastly more efficient than
  multithreaded or multiprocess I/O as its quite easy to induce significant
  amounts of unintentional contention between threads (and processes come with
  a lot of overhead). That being said, there are cases when a small number of
  threads can be beneficial such as when multi-core or multi-cpu systems are
  dedicated to a single network intensive task. Additionally, it may make
  sense to have a single I/O thread processing events and hand actually cpu
  intensive work off to a pool of worker threads (such as via APR's
  ``apr_thread_pool_t``). In either case, you should attempt to minimize the
  interaction between different threads and different events as much as
  possible -- the best models will always be those where the events created by
  a thread are never handled by any other thread. (Of course, in case you
  really don't want to do that, keep reading ...)

* A private memory pool is created for each thread when ``aeb_event_create()``
  or ``aeb_event_loop()`` is first called. This pool is destroyed when the
  thread or application exits. During its lifetime this pool is used for only
  one task: to allocate a single ``event_base`` which is the coordinating data
  structure used by libevent during the various I/O loops. This has a number
  of critical side effects:

 1. Calling ``aeb_event_add()``, ``aeb_event_remove()``, ``aeb_event_loop()``
    or similar *ONLY* effects the current thread. When an event is initially
    allocated it is indirectly associated with the currently running thread;
    this means that if you hand off an event to a different thread such as ...

````  C
    static void *other_thread(apr_thread_t *t, void *data)
    {
       aeb_event_t *event = (aeb_event_t*)data;

       aeb_event_add(event);
       aeb_event_loop(NULL);
       ...
    }
````

   Then the event will **NEVER** be processed and no callbacks will occur
   (unless the thread that created it also calls ``aeb_event_add()`` and
   ``aeb_event_loop()``).

2. You can move an event to a different thread after creation. Once this is
   done calls to ``aeb_event_add()`` will effect the new thread's event handling
   rather than the thread that created it.

   Keep in mind that this *ONLY* switches the ``event_base``, not the memory
   pool the event was allocated from. Fortunately however, should the
   allocating pool be destroyed the event will automatically be removed from
   the base it is assigned to regardless of which thread "owns" what.

   Example of moving an event to our thread:

````  C
    static void *io_thread(apr_thread_t *t, void *data)
    {
       aeb_event_t *event = (aeb_event_t*)data;

       aeb_event_thread_assign(event,t);
       aeb_event_add(event);
       aeb_event_loop(NULL);
       ...
    }
````

    Note that it is not necessary to call ``aeb_event_remove()`` before moving
    it to a different thread and in some cases it is also not necessary to call
    ``aeb_event_add()`` although it's generally safest to do so (it's a noop if
    already added). ``aeb_event_thread_assign()`` will automatically remove it
    from the previous thread's ``event_base`` and add it to the new one (it's
    never automatically added to the new one unless it had been previously
    added to the old).

 3. There is currently no way to determine who "owns" an event, although you
    can call ``aeb_event_thread_assign()`` with ``NULL`` as the second
    argument as shorthard for assigning it to the current running thread.
    Keep in mind that this "ownership" has nothing to do with memory
    allocation or creation but only about which thread created the
    ``event_base`` currently assigned to the event.

 4. It's possible to use events as a form of inter-thread RPC, although there
    are much better ways of accomplishing this sort of thing. Consider:

  a. One dispatch thread running:

````  C
     /* globally available dispatch thread */
     static apr_thread_t *dispatch_thread = NULL;

     static void *dispatch_thread_start(apr_thread_t *t, void *data)
     {
       dispatch_thread = t;
       aeb_event_loop(NULL);
     }
````

  b. To execute a particular callback function inside the dispatch thread:

````  C
     static void call_dispatch_thread(apr_pool_t *p,
                                   aeb_event_callback_fn func,
                                   int misc)
     {
       aeb_event_t *ev;

       aeb_event_create(p,func,&ev);
       aeb_event_thread_assign(e,dispatch_thread);
       aeb_event_add(ev);
       aeb_event_trigger(ev,misc);
     }
````

 c. This **won't** necessarily _immediately_ execute the callback function,
    but it will be scheduled for handling whenever the dispatch thread is both
    running ane examining all pending events from its event loop.

 d. When performing a trick like this it's important to watch out for memory
    leaks or allocation conflicts. In the above example the event should be
    considered "owned" by the dispatch thread *and* the memory pool must not
    be destroyed until it can be assured that the dispatch thread won't be in
    the middle of handling the event While it would be fine to destroy the
    pool if the event had been added but wasn't pending (calling
    aeb_event_trigger() always makes it immediately pending), there are
    potential race conditions that need to be considered so it's generally
    best to handle allocation in such a way that the "receiving" thread can
    take care of it however it sees fit.
