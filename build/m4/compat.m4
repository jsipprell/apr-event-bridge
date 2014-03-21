m4_ifndef([AS_VAR_COPY],
[m4_define([AS_VAR_COPY],
[AS_LITERAL_IF([$1[]$2], [$1=$$2], [eval $1=\$$2])])])dnl
m4_pattern_forbid([AEB][_IF][_AUTOCONF][_VERSION],[m4 failure])dnl
m4_ifndef([AEB_IF_AUTOCONF_VERSION],
[m4_define([AEB_IF_AUTOCONF_VERSION],
[m4_cond(m4_version_compare(m4_defn([AC_AUTOCONF_VERSION]),[$1]),[-1],[$3],[$2])])])dnl
