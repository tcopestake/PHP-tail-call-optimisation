/* Includes and such */

#include <stddef.h>
#include "php.h"
#include "zend_extensions.h"

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

/*
 * ...
 *
 */
void tco_patch_declaration(zend_ast_decl *declaration)
{
    fprintf(dbg, "tco_patch_declaration: ");
    fwrite(ZSTR_VAL(declaration->name), ZSTR_LEN(declaration->name), 1, dbg);
    fprintf(dbg, "\n");
    fflush(dbg);
}

/*
 * The general idea here is:
 *
 * 1. Recursively explore all child nodes.
 * 2. Once child nodes have been explored...
 * 3. Check whether current node is a function/method declaration.
 * 4. If so, run it through tco_patch_function()
 *
 */
void tco_walk_ast(zend_ast *ast)
{
    // Part 1: Explore child nodes.

    uint32_t assumed_children = 0;
    zend_ast **child_nodes;
    zend_ast_decl *declaration = NULL;

    // Get children/counts for declarations, lists and types w/ 1+ children.

    switch (ast->kind) {
    	case ZEND_AST_FUNC_DECL:
    	case ZEND_AST_METHOD:
            declaration = (zend_ast_decl *) ast;

            // Fall-through here is intentional.

    	case ZEND_AST_CLOSURE:
    	case ZEND_AST_CLASS:
            assumed_children = 4;
            child_nodes = ((zend_ast_decl *) ast)->child;

            break;

        default:
            if (zend_ast_is_list(ast)) {
                zend_ast_list *list = zend_ast_get_list(ast);

                assumed_children = list->children;
                child_nodes = list->child;
            } else if (ast->kind >= (1 << ZEND_AST_NUM_CHILDREN_SHIFT)) {
                assumed_children = zend_ast_get_num_children(ast);
                child_nodes = ast->child;
            }
    }

    // Walk through any and all child nodes.

    if (assumed_children) {
        for (uint32_t i = 0; i < assumed_children; i++) {
            if (child_nodes[i]) {
                tco_walk_ast(child_nodes[i]);
            }
        }
    }

    // Part 2: Extra magic for function/method declarations.

    if (declaration) {
        tco_patch_declaration(declaration);
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