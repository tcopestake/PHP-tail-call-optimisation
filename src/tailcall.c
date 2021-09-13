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

FILE *dbg;

/*
 * The general idea here is:
 * - While searching the op array...
 * - Irrelevant ops are saved in the current block.
 * - When a return of interest is detected (i.e. a recursive tail call)...
 * - A new block is created - where rewritten opcodes will be added.
 * - At the end, a new op array of the required size will be allocated...
 * - ... and all blocks will be iterated over and written to the new op array.
 *
 */

#define OP_BLOCK_POOL_SIZE 8

typedef struct _tco_op_block {
    uint16_t number;
    uint32_t op_array_start_index;
    uint32_t op_array_end_index;
    struct _tco_op_block *previous;
} tco_op_block;

typedef struct _tco_context {
    tco_op_block *op_block_pool;
    uint16_t op_block_next_index;
    tco_op_block *op_block_tail;
} tco_context;

tco_context *tco_new_context()
{
    // Create the context.

    tco_context *context = malloc(sizeof(tco_context));

    context->op_block_tail = NULL;
    context->op_block_next_index = 0;

    // Allocate enough memory for the op block pool.

    context->op_block_pool = malloc(sizeof(tco_op_block) * OP_BLOCK_POOL_SIZE);

    // (Have a guess what this does.)

    return context;
}

tco_op_block *tco_new_op_block(tco_context *context)
{
    // If there's a free block in the pool, we'll use it.
    // Otherwise, we'll have to allocate a new one.
    // (In the future, maybe allocating a new pool would be better?)

    tco_op_block *op_block;

    if (context->op_block_next_index < OP_BLOCK_POOL_SIZE) {
        op_block = context->op_block_pool + context->op_block_next_index;
    } else {
        op_block = malloc(sizeof(tco_op_block));
    }

    // This isn't necessarily the best way to handle this, but it'll work for now.

    op_block->number = context->op_block_next_index++;

    // Map tails, etc.

    op_block->previous = context->op_block_tail;

    context->op_block_tail = op_block;

    // Set initial indices... ?

    op_block->op_array_start_index = 0;
    op_block->op_array_end_index = 0;

    // (Some additional processing will go here eventually)

    // Return t'block.

    return op_block;
}

void tco_free_context(tco_context *context)
{
    // Stage 1: Free any additional memory allocated for blocks outside of the pool.

    while (
        context->op_block_tail
        && (context->op_block_tail->number >= OP_BLOCK_POOL_SIZE)
    ) {
        // Free the memory.

        free(context->op_block_tail);

        // Point to the next (technically previous) block.

        context->op_block_tail = context->op_block_tail->previous;
    }

    // Stage 2: Free the memory allocated for the pool.

    free(context->op_block_pool);

    // Stage 3: Free the memory allocated for the context itself.

    free(context);
}

void tco_explore_blocks(tco_context *context)
{
    tco_op_block *op_block = context->op_block_tail;

    fprintf(dbg, "(Exploring blocks)\n");
    fflush(dbg);

    while (op_block) {
        fprintf(dbg, "Block #%d (start: %d - end: %d)\n", op_block->number, op_block->op_array_start_index, op_block->op_array_end_index);
        fflush(dbg);

        // Point to the next (technically previous) block.

        op_block = op_block->previous;
    }
}

bool tco_is_call_recursive(zend_op_array *op_array, zend_op *op)
{
    // Make sure operand 2 is a constant.

	if (op->op2_type != IS_CONST) {
        return false;
    }

    // For methods, we also need to check that the class/"scope" matches.

    switch (op->opcode) {
        case ZEND_INIT_STATIC_METHOD_CALL:
            // (We may not need to verify op_array->scope here, but I will just in caaase.)

    		if (!op_array->scope || !op_array->scope->name) {
                return false;
            }

            // If operand 1 is a constant/string, compare it to the current class/scope name.

            if (op->op1_type == IS_CONST) {
                if (
                    !zend_string_equals(
                        Z_STR_P(op_array->literals + op->op1.constant),
                        op_array->scope->name
                    )
                ) {
                    return false;
                }
            } else if (op->op1_type != IS_UNUSED) {
                // I'm not 100% on this, but I think this will cover both self:: and static::
                // If we're here, the call isn't recursive.

                return false;
            }

            break;

        case ZEND_INIT_METHOD_CALL:
            // I'm pretty sure operand 1 being "unused" means $this, but I could be wrong.

            if (op->op1_type != IS_UNUSED) {
                // operand 1 isn't $this; can't be recursive.

                return false;
            }

            break;
    }

    // If we got this far, the call either isn't a method call
    // or the method call was within the same class/scope/whatever.
    // In all cases, we now need to check the callable name.

    return zend_string_equals(
        Z_STR_P(op_array->literals + op->op2.constant),
        op_array->function_name
    );
}

/*
 * ...
 *
 */
void tco_cleanup()
{

}

/* Main startup function for the extension. */
static void tco_startup(void)
{
    dbg = fopen("G:/dev/tailcall/astlog.txt", "w");
}

enum {
    TCO_STATE_SEARCHING = 1,
    TCO_STATE_FOUND_RETURN,
    TCO_STATE_FOUND_UCALL,
};

/*
 * The general idea here is:
 *
 * - Start from the end of the opcode array.
 * - Whenever a return opcode is encountered, be on red alert.
 * - If the next (technically previous) opcode is a function call...
 * - Start gathering useful intel.
 * -
 */
void tco_explore_op_array(zend_op_array *op_array, tco_context *context)
{
    tco_op_block *current_op_block;
    zend_op *op;
    uint32_t return_index;
    uint32_t ucall_index;
    uint32_t init_index;
    uint32_t search_state = TCO_STATE_SEARCHING;

    uint32_t i = op_array->last;

    // I think all op arrays are guaranteed to have at least one opcode, but just in case...

    if (i < 1) {
        // If there are no opcodes, there's nothing to do.

        return;
    }

    // Create a starting op block.

    current_op_block = tco_new_op_block(context);

    // For now, assume that the starting block ends at the end of the op array.

    current_op_block->op_array_end_index = i - 1;

    // Now iterate over the op array.

    do {
        --i;

        op = &op_array->opcodes[i];

        switch (op->opcode) {
            case ZEND_RETURN:
                search_state = TCO_STATE_FOUND_RETURN;
                return_index = i;

                break;

            case ZEND_DO_UCALL:
                if (search_state == TCO_STATE_FOUND_RETURN) {
                    search_state = TCO_STATE_FOUND_UCALL;
                    ucall_index = i;
                }

                break;

            case ZEND_INIT_METHOD_CALL:
            case ZEND_INIT_STATIC_METHOD_CALL:
            case ZEND_INIT_FCALL:
			case ZEND_INIT_FCALL_BY_NAME:
			// case ZEND_INIT_NS_FCALL_BY_NAME:
                // Determine whether this is a recursive call.

                fprintf(dbg, "Function call in: %s\n", op_array->function_name->val);
                fflush(dbg);

                if (tco_is_call_recursive(op_array, op)) {
                    fprintf(dbg, "(Call is recursive)\n");
                    fflush(dbg);
                }

                if (search_state == TCO_STATE_FOUND_RETURN) {
                    init_index = i;
                }

                break;

            default:
                // God will never forgive me for this.

                switch (search_state) {
                    case TCO_STATE_FOUND_UCALL:
                        /*
                         * If we're here, that means we've found a return followed by a ucall
                         * i.e. this could potentially be a recursive call.
                         * These opcodes will be handled later and should be ignored here.
                         */

                        break;

                    case TCO_STATE_FOUND_RETURN:
                        /*
                         * If we're here, that means the previous opcode was a return, but
                         * this current opcode is not a ucall i.e. this cannot (for our purposes)
                         * be counted as a potential recursive call.
                         * So we'll just reset the state & save the opcode to the current block.
                         */

                        search_state = TCO_STATE_SEARCHING;

                        // (Fall-through here is intentional.)

                    default:
                        // For everything else, just save the opcode to the current block.

                        current_op_block->op_array_start_index = i;
                }
        }

        /* fprintf(dbg, "op: %d: ", i);
        fprintf(dbg, "%s\n", zend_get_opcode_name(op->opcode));
        fflush(dbg); */
    } while (i);

	fprintf(dbg, "(done)\n");
}

/* ... */
static void tco_op_handler(zend_op_array *op_array)
{
    // If there's no function name, we ain't interested.

	if (!op_array->function_name) {
        return;
    }

	fprintf(dbg, "(Doing something)\n");
    fflush(dbg);

    tco_context *context = tco_new_context();

    tco_explore_op_array(op_array, context);

    tco_explore_blocks(context);

    tco_free_context(context);
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
    tco_op_handler,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

ZEND_EXTENSION();