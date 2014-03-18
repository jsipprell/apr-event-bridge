dnl NOTE: These *must* be included using builtin(include, thisfile.m4)
dnl       and *cannot* be in the same directory (although a subdir is fine)
dnl       as m4 files pulled in automagically via aclocal.
dnl
dnl Restricted form of AC_ARG_ENABLE that limits user options
dnl
dnl $1 = option name
dnl $2 = help-string
dnl $3 = default value  (auto).  "--" means do not set it by default
dnl $4 = allowed values (auto yes no)
dnl $5 = overridden default
AC_DEFUN([AEB_ARG_ENABLE], [# AEB begin --enable-$1
  pushdef([aeb_DefVal],ifelse($3,,auto,$3))
  pushdef([aeb_VarName],translit([$1],[-],[_]))
  AC_ARG_ENABLE($1,ifelse($4,,[$2],[$2 (]translit([$4],[ ],[|])[)]) ifelse($3,--,,@<:@[default=]aeb_DefVal@:>@),[

  aeb_arg=invalid
  for aeb_val in ifelse($4,,[auto yes no],[$4]) ; do
    if test x"$enableval" = x"$aeb_val" ; then
      aeb_arg="$aeb_val"
    fi
  done
  if test x"$aeb_arg" = x"invalid"; then
    AC_MSG_ERROR(bad value [']$enableval['] for --enable-$1)
  fi
  [aeb_enable_]aeb_VarName="$aeb_arg"
]ifelse($3,--,,[,
[ [aeb_enable_]aeb_VarName=ifelse($5,,aeb_DefVal,[${]$5[:-]aeb_DefVal[}])]]))dnl
dnl AC_MSG_RESULT([AEB --enable-$1 $aeb_enable_$1])
  popdef([aeb_DefVal])
  popdef([aeb_VarName])
# AEB end --enable-$1
])dnl
dnl
dnl --------------------------------------------------------------------
dnl Restricted form of AC_ARG_WITH that limits user options
dnl
dnl $1 = option name
dnl $2 = help-string
dnl $3 = default value (no)
dnl $4 = allowed values (yes or no)
AC_DEFUN([AEB_ARG_WITH], [# AEB begin --with-$1
  pushdef([aeb_VarName],translit([$1],[-],[_]))
  AC_ARG_WITH($1,[$2 @<:@default=]ifelse($3,,yes,$3)@:>@,[
    aeb_arg=invalid
    for aeb_val in ifelse($4,,[yes no],[$4]) ; do
      if test x"$withval" = x"$aeb_val"; then
        aeb_arg="$aeb_val"
      fi
    done
    if test x"$aeb_arg" = x"invalid"; then
      AC_MSG_ERROR(bad value [']$withval['] for --with-$1)
    fi
    [aeb_with_]aeb_VarName="$aeb_arg"
],
[   [aeb_with_]aeb_VarName=ifelse($3,,"no","$3")])dnl
dnl AC_MSG_RESULT([AEB --with-$1 $aeb_with_$1])
  popdef([aeb_VarName])
# AEB end --with-$1
])dnl