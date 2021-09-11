/* Includes and such */

#include <stddef.h>
#include "php.h"
#include "zend_extensions.h"
#include "zend_ast.h"

/* Handle platform-specific hax */

#ifndef ZEND_EXT_API
    #ifdef _WIN32
        #define ZEND_EXT_API __declspec(dllexport)
    #else
        #define ZEND_EXT_API
    #endif
#endif

/* Some variables/types/etc */

static void (*zend_ast_process_copy)(zend_ast*);

FILE *dbg;

/* Custom AST handler */
void tco_walk_ast(zend_ast *ast)
{
    fflush(dbg);
    fputs("\n tco_walk_ast: \n", dbg);
    fprintf(dbg, "(node: %d)\n", ast->kind);
    fflush(dbg);

    switch (ast->kind) {
        // case ZEND_AST_CALL:


        /*case ZEND_AST_STMT_LIST:
            fprintf(dbg, "(ZEND_AST_STMT_LIST)\n");
            fflush(dbg);

            zend_ast_list *list = zend_ast_get_list(ast);

            for (int i = 0; i < list->children; i++) {
                tco_walk_ast(list->child[i]);
            }

            break;*/

        case ZEND_AST_FUNC_DECL:
            // (It seems there's no inline function for casting to zend_ast_decl?)

            zend_ast_decl *function = (zend_ast_decl *) ast;

            fprintf(dbg, "(ZEND_AST_FUNC_DECL)\n");
            fwrite(ZSTR_VAL(function->name), ZSTR_LEN(function->name), 1, dbg);
            fflush(dbg);

            break;

        default:
            // Generic handler for other types.

            uint32_t assumed_children = 0;
            zend_ast **child_nodes;

            fprintf(dbg, "(generic)\n");
            fflush(dbg);

            if (zend_ast_is_list(ast)) {
                fprintf(dbg, "(is a list)\n");
                fflush(dbg);

        		zend_ast_list *list = zend_ast_get_list(ast);

                assumed_children = list->children;
                child_nodes = list->child;
            } else if (ast->kind >= (1 << ZEND_AST_NUM_CHILDREN_SHIFT)) {
                fprintf(dbg, "(has children)\n");
                fflush(dbg);

                assumed_children = zend_ast_get_num_children(ast);
                child_nodes = ast->child;
            }

            // Walk through any and all child nodes.

            if (assumed_children) {
                fprintf(dbg, "(child count: %d)\n", assumed_children);

                for (uint32_t i = 0; i < assumed_children; i++) {
                    if (child_nodes[i]) {
                        tco_walk_ast(child_nodes[i]);
                    }
                }
            }
    }
}

/* Custom AST handler */
void tco_ast_process(zend_ast *ast)
{
    dbg = fopen("G:/dev/tailcall/astlog.txt", "a");

    // ...

    tco_walk_ast(ast);

    // If we hijacked another AST process, we'll call it here.

    if (zend_ast_process_copy) {
        zend_ast_process_copy(ast);
    }

    // ...

    fclose(dbg);
}

/* Main startup function for the extension */
static void tco_startup(void)
{
    // Save a copy of the original pointer & replace w/ tco_ast_process.

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