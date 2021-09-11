/* Includes and such */

#include <stddef.h>
#include "php.h"
#include "zend_extensions.h"
#include "zend_ast.h"

/* Handle platform-specific hax */

#ifdef _WIN32
    #define ZEND_EXT_API __declspec(dllexport)
#else
    #define ZEND_EXT_API
#endif

/* Some variables/types/etc */

static void (*zend_ast_process_copy)(zend_ast*);

/* Custom AST handler */
void tco_ast_process(zend_ast *ast)
{
    FILE *fp = fopen("G:/dev/tailcall/astlog.txt", "a");

    fputs("tco_ast_process called\n", fp);

    fclose(fp);
}

/* Main startup function for the extension */
static void tco_startup(void)
{
    zend_ast_process_copy = zend_ast_process;

    zend_ast_process = tco_ast_process;
}

/* Zend extension jazz */

ZEND_EXT_API zend_extension zend_extension_entry = {
    "Tail call optimisation",
    "0.1",
    "Terence C.",
    NULL,
    NULL,
    NULL,
    NULL,
    tco_startup,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

ZEND_EXTENSION();