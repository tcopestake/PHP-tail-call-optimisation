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

/* Some variables/types/etc. */

typedef struct _tco_decl_returns_link {
	zend_ast **siblings_and_self;
	uint32_t self_index;
    struct _tco_decl_returns_link *previous;
} tco_decl_returns_link;

typedef struct _tco_decl_context {
	zend_ast_decl *declaration;
    tco_decl_returns_link *returns_tail;
} tco_decl_context;

static void (*zend_ast_process_copy)(zend_ast*);

FILE *dbg;

/*
 * ...
 *
 */
void tco_cleanup()
{

}

/*
 * Allocate memory for a new declaration context, etc.
 */
tco_decl_context *tco_create_decl_context(zend_ast_decl *declaration)
{
    tco_decl_context *decl_context = malloc(sizeof(tco_decl_context));

    decl_context->declaration = declaration;
    decl_context->returns_tail = NULL;

    return decl_context;
}

/*
 * Allocate memory for a new node in the returns list.
 */
tco_decl_returns_link *tco_alloc_decl_returns_link()
{
    return malloc(sizeof(tco_decl_returns_link));
}

/*
 * Frees all memory allocated for (and associated with) a given context.
 * (This includes the return nodes btw.)
 */
void tco_free_decl_context(tco_decl_context *decl_context)
{
    // Free memory allocated for returns.

    tco_decl_returns_link *current_return = decl_context->returns_tail;
    tco_decl_returns_link *tmp;

    while (current_return) {
        fprintf(dbg, "(freeing current_return)\n");
        fflush(dbg);

        // Save the pointer before we free the memory it's stored in.

        tmp = current_return->previous;

        free(current_return);

        // Update pointer to next (technically previous) return node.

        current_return = tmp;
    }

    // Now free the context itself.

    free(decl_context);
}

/*
 * Adds a new return node/link in the given context.
 */
void tco_decl_context_add_return(
    tco_decl_context *decl_context,
    zend_ast **siblings_and_self,
    uint32_t self_index
) {
    tco_decl_returns_link *new_returns_link = tco_alloc_decl_returns_link();

    // Set return link node values.

    new_returns_link->siblings_and_self = siblings_and_self;
    new_returns_link->self_index = self_index;

    // Point new node to the current tail & update tail pointer.

    new_returns_link->previous = decl_context->returns_tail;

    decl_context->returns_tail = new_returns_link;
}

/*
 * ...
 */
void tco_patch_declaration(tco_decl_context *decl_context)
{
	zval output;

	ZVAL_STRING(&output, "(Test)");

	zend_ast_zval *arg = emalloc(sizeof(zend_ast_zval));
	arg->kind = ZEND_AST_ZVAL;
	arg->attr = 0;
    arg->val = output;

    zend_ast *echo = zend_ast_create(ZEND_AST_ECHO, (zend_ast *)arg);










    zend_ast_decl *declaration = decl_context->declaration;

    fprintf(dbg, "tco_patch_declaration: ");
    fwrite(ZSTR_VAL(declaration->name), ZSTR_LEN(declaration->name), 1, dbg);
    fprintf(dbg, "\n");
    fflush(dbg);

    // Explore returns (if applicable).

    tco_decl_returns_link *current_return = decl_context->returns_tail;

    while (current_return) {
        fprintf(dbg, "(return statement)\n");
        fflush(dbg);

        fprintf(dbg, "Child index: %d\n", current_return->self_index);
        fflush(dbg);

        current_return->siblings_and_self[current_return->self_index] = echo;

        //zend_ast *return_node = current_return->parent_ast[current_return->return_child_index];

        //fprintf(dbg, "Child index: %d\n", current_return->return_child_index);
        //fflush(dbg);

        // Update pointer to next (technically previous) return node.

        current_return = current_return->previous;
    }
}

/*
 * The general idea here is:
 *
 * - Recursively explore all child nodes.
 * - If current node is a function/method declaration...
 * - Collect return statements relevant to that scope/context.
 * - Then run the declaration (and its respective returns) through tco_patch_declaration()
 *
 */
void tco_walk_ast(
    zend_ast *ast,
    zend_ast **siblings_and_self,
    uint32_t self_index,
    tco_decl_context *decl_context
) {
    // Part 1: Explore child nodes.

    uint32_t assumed_children = 0;
    zend_ast **child_nodes;
    zend_ast_decl *declaration = NULL;

    // Get children/counts for declarations, lists and types w/ 1+ children.

    switch (ast->kind) {
        case ZEND_AST_RETURN:
            fprintf(dbg, "(ast->kind == ZEND_AST_RETURN)\n");
            fflush(dbg);

            // While we're here, we'll collect this return in the current context.

            tco_decl_context_add_return(decl_context, siblings_and_self, self_index);

            // Set values for exploring child nodes, etc.

            assumed_children = 1;
            child_nodes = ast->child;

            break;

    	case ZEND_AST_FUNC_DECL:
    	case ZEND_AST_METHOD:
            declaration = (zend_ast_decl *) ast;

            // Create a new declaration context for this... declaration.

            decl_context = tco_create_decl_context(declaration);

            // Fall-through here is intentional.

    	case ZEND_AST_CLOSURE:
    	case ZEND_AST_CLASS:
            assumed_children = 4; // (Should we be calculating these values from the node/constant?)
            child_nodes = ((zend_ast_decl *) ast)->child;

            break;

        default:
            // Generic code for lists/other.

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
                tco_walk_ast(child_nodes[i], child_nodes, i, decl_context);
            }
        }
    }

    // Part 2: Extra magic for function/method declarations.

    if (declaration) {
        // Patch it.

        tco_patch_declaration(decl_context);

        // Free the current declaration context; we don't need it anymore.

        tco_free_decl_context(decl_context);
    }
}

/*
 * Custom AST handler.
 */
void tco_ast_process(zend_ast *ast)
{
    dbg = fopen("G:/dev/tailcall/astlog.txt", "a");

    // (Have a guess what this does.)

    tco_walk_ast(ast, NULL, 0, NULL);

    // ...

    tco_cleanup();

    fclose(dbg);

    // If we hijacked another AST process, we'll call it here.

    if (zend_ast_process_copy) {
        zend_ast_process_copy(ast);
    }
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