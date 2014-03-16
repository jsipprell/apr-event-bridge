AC_DEFUN([_AEB_CHECK_GCC_VISIBILITY],
  [if test x"$_aeb_vis_checked" = x""; then
    _aeb_vis_checked=1
    cat >conftest.c <<EOF
      int foo __attribute__ ((visibility ("hidden"))) = 1;
      int bar __attribute__ ((visibility ("default"))) = 1;
EOF
    _aeb_vis=no
    _aeb_vis_hidden=
    _aeb_vis_export=
    if ${CC-cc} -Werror -S conftest.c -o conftest.s >/dev/null 2>&1; then
      if grep '\.hidden.*foo' conftest.s >/dev/null; then
        _aeb_vis_hidden="__attribute__ ((visibility (\"hidden\")))"
      fi
      if grep '\.globl.*bar' conftest.s >/dev/null; then
        _aeb_vis_export="__attribute__ ((visibility (\"default\")))"
      fi
      _aeb_vis=yes
    fi
      
    ac_cv_have_visibility_attribute="${ac_cv_have_visibility_attribute:-$_aeb_vis}"
    ac_cv_gcc_visibility_hidden="${ac_cv_gcc_visibility_hidden:-${_aeb_vis_hidden}}"
    ac_cv_gcc_visibility_export="${ac_cv_gcc_visibility_export:-${_aeb_vis_export}}"
  fi
  ])dnl

AC_DEFUN([AEB_CHECK_GCC_VISIBILITY],
  [AC_CACHE_CHECK([whether __attribute__((visibility())) is supported],
      [ac_cv_have_visibility_attribute], _AEB_CHECK_GCC_VISIBILITY)
    if test x"$ac_cv_have_visibility_attribute" = x"yes"; then
      AC_CACHE_CHECK([whether symbol hidding attribute works],
                    [ac_cv_gcc_visibility_hidden], _AEB_CHECK_GCC_VISIBILITY)
      AC_CACHE_CHECK([whether symbol exporting attribute works],
                    [ac_cv_gcc_visibility_export], _AEB_CHECK_GCC_VISIBILITY)
    else
      ac_cv_gcc_visibility_hidden=
      ac_cv_gcc_visibility_export=
    fi

    if test x"$ac_cv_have_visibility_attribute" = x"yes"; then
      AC_DEFINE_UNQUOTED([HAVE_VISIBILITY_ATTRIBUTE],[1],
        [Define to 1 if your C compilter supports the visibility __attribute__.])
      if test x"$ac_cv_gcc_visibility_hidden" != x""; then
        AC_DEFINE_UNQUOTED([VISIBILITY_HIDDEN],[$ac_cv_gcc_visibility_hidden],
          [Define to your C compiler's attribute for preventing an exported symbol being dynamically linkable.])
      fi
      if test x"$ac_cv_gcc_visibility_export" != x""; then
        AC_DEFINE_UNQUOTED([VISIBILITY_EXPORT],[$ac_cv_gcc_visibility_export],
          [Define to your C compiler's attribute for exporting symbols for dynamic linking.])
      fi
    fi
  ])dnl

