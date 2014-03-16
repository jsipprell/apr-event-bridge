AC_DEFUN([AEB_CHECK_ALIAS_ATTRIBUTE],
  [AC_CACHE_CHECK([[whether __attribute__((__may_alias__)) is supported]],
      [ac_cv_have_alias_attribute],
      [aeb_save_CFLAGS="$CFLAGS"
      CFLAGS="-fstrict-aliasing -Werror -Wstrict-aliasing"
      AC_COMPILE_IFELSE([
        AC_LANG_PROGRAM([[
            extern void apr_pool_userdata_get(void **, const char*, unsigned);
            typedef struct { int x; const char *y; } aeb_context_t;
          ]],[[
            __attribute__((__may_alias__)) aeb_context_t *ctx = 0;
            apr_pool_userdata_get((void**)&ctx,0,0);
          ]])],
        [ac_cv_have_alias_attribute=yes],
        [ac_cv_have_alias_attribute=no])
      CFLAGS="$aeb_save_CFLAGS"])
    
  AS_IF([test x"$ac_cv_have_alias_attribute" = x"yes"],
      [AC_DEFINE_UNQUOTED([HAVE_RELAXED_ALIAS_ATTRIBUTE],[1],
        [Define to 1 if your C compiler supports relaxing strict aliasing for automatic variables.])
      AC_DEFINE_UNQUOTED([RELAX_STRICT_ALIASING_ATTRIBUTE],[[__attribute__((__may_alias__))]],
        [Define to your C compiler's attribute for relaxing strict aliasing])
      ])
  ])dnl

