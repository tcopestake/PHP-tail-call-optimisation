PHP_ARG_ENABLE(tailcall, enable recursive tail call optimisation, no)

if test "$PHP_TAILCALL" != "no"; then
    PHP_NEW_EXTENSION(tailcall, tailcall.c, $ext_shared)
fi