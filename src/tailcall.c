/* Includes and such */

#include <stddef.h>
#include <stdbool.h>
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

typedef struct _tco_callable_returns_link {
	zend_ast **siblings_and_self;
	uint32_t self_index;
    zend_ast *self_node;
    struct _tco_callable_returns_link *previous;
} tco_callable_returns_link;

typedef struct _tco_callable_context {
	zend_ast_decl *callable_decl;
    tco_callable_returns_link *returns_tail;
} tco_callable_context;

typedef struct _tco_context {
	zend_ast_decl *class_decl;
	tco_callable_context *callable_context;
} tco_context;

tco_context tco_global_context;

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
tco_callable_context *tco_create_callable_context(zend_ast_decl *callable_decl)
{
    tco_callable_context *callable_context = malloc(sizeof(tco_callable_context));

    callable_context->callable_decl = callable_decl;
    callable_context->returns_tail = NULL;

    return callable_context;
}

/*
 * Allocate memory for a new node in the returns list.
 */
tco_callable_returns_link *tco_alloc_callable_returns_link()
{
    return malloc(sizeof(tco_callable_returns_link));
}

/*
 * Frees all memory allocated for (and associated with) a given context.
 * (This includes the return nodes btw.)
 */
void tco_free_callable_context(tco_callable_context *callable_context)
{
    // Free memory allocated for returns.

    tco_callable_returns_link *current_return = callable_context->returns_tail;
    tco_callable_returns_link *tmp;

    while (current_return) {
        fprintf(dbg, "(freeing current_return)\n");
        fflush(dbg);

        // Save the previous pointer before we free the memory it's stored in.

        tmp = current_return->previous;

        free(current_return);

        // Update pointer to previous (technically next) return node.

        current_return = tmp;
    }

    // Now free the context itself.

    free(callable_context);
}

/*
 * Adds a new return node/link in the given context.
 */
void tco_callable_context_add_return(
    tco_callable_context *callable_context,
    zend_ast **siblings_and_self,
    uint32_t self_index
) {
    tco_callable_returns_link *new_returns_link = tco_alloc_callable_returns_link();

    // Set return link node values.

    new_returns_link->siblings_and_self = siblings_and_self;
    new_returns_link->self_index = self_index;
    new_returns_link->self_node = siblings_and_self[self_index];

    // Point new node to the current tail & update tail pointer.

    new_returns_link->previous = callable_context->returns_tail;

    callable_context->returns_tail = new_returns_link;
}

/*
 * ...
 */
void tco_patch()
{
    zval callable_name_zval;

    zend_ast_decl *class_decl = tco_global_context.class_decl;
    tco_callable_context *callable_context = tco_global_context.callable_context;

    if (class_decl) {
        fprintf(dbg, "Class: ");
        fwrite(ZSTR_VAL(class_decl->name), ZSTR_LEN(class_decl->name), 1, dbg);
        fprintf(dbg, "\n");
        fflush(dbg);
    } else {
        fprintf(dbg, "(No class context)\n");
        fflush(dbg);
    }

    // ...

	zval output;

	ZVAL_STRING(&output, "(Test)");

	zend_ast_zval *arg = emalloc(sizeof(zend_ast_zval));
	arg->kind = ZEND_AST_ZVAL;
	arg->attr = 0;
    arg->val = output;

    zend_ast *echo = zend_ast_create(ZEND_AST_ECHO, (zend_ast *)arg);

    // ...





    // We need the declaration name as a zval for the comparison functions (I think).

    ZVAL_STR(&callable_name_zval, callable_context->callable_decl->name);

    // Explore returns (if applicable).

    tco_callable_returns_link *current_return = callable_context->returns_tail;

    for (; current_return; current_return = current_return->previous) {
        zend_ast *return_node = current_return->self_node;
        zend_ast *return_value = return_node->child[0];

        // As it stands, we're only interested in returns which call functions/methods.
        // We also need to determine whether the function/other is calling itself.

        switch (return_value->kind) {
            case ZEND_AST_CALL:
                if (
                    string_case_compare_function(
                        &callable_name_zval,
                        &((zend_ast_zval *) return_value->child[0])->val
                    ) != 0
                ) {
                    // Not recursive; ignore it.

                    continue;
                }

                // If we're here, this is a recursive function call.



                break;

            case ZEND_AST_METHOD_CALL:
            case ZEND_AST_STATIC_CALL:
                break;
        }

        continue;

        fprintf(dbg, "(return statement)\n");
        fflush(dbg);

        fprintf(dbg, "Child index: %d\n", current_return->self_index);
        fflush(dbg);

        current_return->siblings_and_self[current_return->self_index] = echo;

        //zend_ast *return_node = current_return->parent_ast[current_return->return_child_index];

        //fprintf(dbg, "Child index: %d\n", current_return->return_child_index);
        //fflush(dbg);
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
    uint32_t self_index
) {
    // This may get used if global contextual values are changed.
    // (and subsequently need to be restored)

    tco_context context_backup;
    bool restore_class_decl = false;

    // Part 1: Identify current node; set any values of interest;
    // & configure values for exploring this node's children later.

    zend_ast_decl *callable_decl = NULL;
    uint32_t assumed_children = 0;
    zend_ast **child_nodes;

    switch (ast->kind) {
        case ZEND_AST_CLASS:
            // We're about to switch to a new class declaration - so we'll back up the current declaration.

            context_backup.class_decl = tco_global_context.class_decl;

            restore_class_decl = true;

            // Switch to new class declaration.

            tco_global_context.class_decl = (zend_ast_decl *) ast;

            // Configure child node values, etc.

            assumed_children = 4;
            child_nodes = tco_global_context.class_decl->child;

            break;

        case ZEND_AST_RETURN:
            fprintf(dbg, "(ast->kind == ZEND_AST_RETURN)\n");
            fflush(dbg);

            // While we're here, we'll collect this return in the current context.
            // We're only interested in saving return values within a declaration.

            if (tco_global_context.callable_context) {
                tco_callable_context_add_return(
                    tco_global_context.callable_context,
                    siblings_and_self,
                    self_index
                );
            } else {
                fprintf(dbg, "(Skipping rando return)\n");
                fflush(dbg);
            }

            // Set values for exploring child nodes, etc.

            assumed_children = 1;
            child_nodes = ast->child;

            break;

    	case ZEND_AST_FUNC_DECL:
    	case ZEND_AST_METHOD:
            // We're about to switch to a new callable context - so we'll back up the current context.

            context_backup.callable_context = tco_global_context.callable_context;

            // Create a new callable context for this... callable.

            callable_decl = (zend_ast_decl *) ast;

            tco_global_context.callable_context = tco_create_callable_context(callable_decl);

            // Fall-through here is intentional.

    	case ZEND_AST_CLOSURE:
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

    // Part 2: Explore any and all child nodes.

    if (assumed_children) {
        for (uint32_t i = 0; i < assumed_children; i++) {
            if (child_nodes[i]) {
                tco_walk_ast(child_nodes[i], child_nodes, i);
            }
        }
    }

    // Part 3: Extra magic if this current node is a function/method declaration.

    if (callable_decl) {
        // Patch it.

        tco_patch();

        // Free the current declaration context; we don't need it anymore.

        tco_free_callable_context(tco_global_context.callable_context);

        // Restore the original callable context.

        tco_global_context.callable_context = context_backup.callable_context;
    }

    // If context was changed, restore it.

    if (restore_class_decl) {
        tco_global_context.class_decl = context_backup.class_decl;
    }
}

/*
 * Custom AST handler.
 */
void tco_ast_process(zend_ast *ast)
{
    dbg = fopen("G:/dev/tailcall/astlog.txt", "w");

    // Init the global context.

    tco_global_context.class_decl = NULL;
    tco_global_context.callable_context = NULL;

    // (Have a guess what this does.)

    tco_walk_ast(ast, NULL, 0);

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