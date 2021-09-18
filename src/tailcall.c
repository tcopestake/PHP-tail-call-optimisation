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
 * The value of OP_BLOCK_POOL_SIZE should be set based on
 * how many returns are expected in a given function.
 * A pool of 8 will allow for ~4 returns before new blocks will start
 * being allocated on-the-fly.
 */

#define OP_BLOCK_POOL_SIZE 8

typedef uint32_t tco_t_remap;

typedef struct _tco_op_block {
    uint16_t number;
    uint32_t op_array_start_index;
    uint32_t op_array_end_index;
    struct _tco_op_block *previous;
} tco_op_block;

typedef struct _tco_context {
    bool do_optimise;
    uint32_t required_size;
    tco_op_block *op_block_pool;
    uint16_t op_block_next_index;
    tco_op_block *op_block_tail;
    zend_op_array *op_array;
    tco_t_remap *t_remaps;
} tco_context;

enum {
    TCO_STATE_SEEKING_RETURN,
    TCO_STATE_SEEKING_CALL,
    TCO_STATE_SEEKING_INIT,
};

/* ... */
tco_context *tco_new_context(zend_op_array *op_array)
{
    // Create the context.

    tco_context *context = malloc(sizeof(tco_context));

    context->op_array = op_array;
    context->do_optimise = false;
    context->op_block_tail = NULL;
    context->op_block_next_index = 0;
    context->t_remaps = NULL;

    // Set the starting size (which is subject to change.)

    context->required_size = op_array->last;

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

    // Stage 3: Free any memory allocated for T var remaps.

    if (context->t_remaps) {
        free(context->t_remaps);
    }

    // Lastly: Free the memory allocated for the context itself.

    free(context);
}

void tco_assemble_blocks(tco_context *context)
{
    zend_op_array *op_array = context->op_array;

    tco_op_block *op_block = context->op_block_tail;

    // Allocate new memoir for the reassembled opcodes.
    // (The required total size should be stored in context->required_size.)

    zend_op *new_ops = emalloc(sizeof(zend_op) * context->required_size);

    // Iterate over each block, either a) copying their opcodes from the old array, to the new
    // or b) writing the new (modified) opcodes for recursive calls.

    uint32_t destination_index = 0;
    uint32_t source_length;

    while (op_block) {
        fprintf(dbg, "Block #%d (start: %d - end: %d)\n", op_block->number, op_block->op_array_start_index, op_block->op_array_end_index);
        fflush(dbg);

        // Copy opcodes.

        source_length = 1 + (op_block->op_array_end_index - op_block->op_array_start_index);

        fprintf(dbg, "Length: %d\n", source_length);
        fflush(dbg);

        memcpy(
            new_ops + destination_index,
            op_array->opcodes + op_block->op_array_start_index,
            sizeof(zend_op) * source_length
        );

        fprintf(dbg, "(Memory copied)\n");
        fflush(dbg);

        // Update the destination_index (to point to where the next block of opcodes should be copied to).

        destination_index += source_length;

        // Point op_block to the next (technically previous) block.

        op_block = op_block->previous;
    }

    // Free the memory for the old ops; it's not needed anymore.
    // & then point the op array to the new opcodes.

	efree(op_array->opcodes);

    op_array->opcodes = new_ops;
}

/*
 *
 */
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


/* ... */
tco_t_remap *tco_get_t_remaps(tco_context *context)
{
    // Each existing T variable will potentially need its own remap.

    size_t bytes_required = sizeof(tco_t_remap) * context->op_array->T;

    // If remaps haven't already been allocated, we need to allocate 'em.

    if (!context->t_remaps) {
        context->t_remaps = (tco_t_remap *) malloc(bytes_required);

        // While we're here, we should probably update T to reflect the new (expected) number.
        // (This may make more sense done elsewhere, but it's here for now at least.)

        context->op_array->T += context->op_array->T;
    }

    // (We also need to intialise everything to zero.)

    memset(context->t_remaps, 0x00, bytes_required);

    return context->t_remaps;
}

/* ... */
inline void tco_check_operand_remaps(zend_uchar type, znode_op operand, tco_t_remap *t_remaps)
{
    if (type == IS_TMP_VAR) {
        // If the given T var has been remapped, we need to remap this operand.

        uint32_t remap_to = t_remaps[operand.var];

        if (remap_to > 0) {
            operand.var = remap_to;
        }
    }
}

/*
 *
 */
void tco_analyse_call(tco_op_block *op_block, tco_context *context)
{
    zend_op *op;

    zend_op_array *op_array = context->op_array;

    // (We need to protect T vars which have been used)

    /*
     * What we essentially need to do here is determine:
     * - Which values need to be reset.
     * - How much additional space will need to be allocated for the new opcodes.
     */

    // First, mark for optimisation.
    // (If we got this far, optimisation is going to happen.)

    context->do_optimise = true;

    // T variable mapping.
    // (op_array->T itself will be updated elsewhere.)

    tco_t_remap *t_remaps = tco_get_t_remaps(context);

    uint32_t next_free_t_var = op_array->T;

    // I won't yet do any shenanigans to optimise this part;
    // I'll just assume all arguments need to be set.
    // (I also won't yet account for default arguments or named arguments.)

    op_array->opcodes[op_block->op_array_start_index].opcode = ZEND_NOP;
    op_array->opcodes[op_block->op_array_end_index].opcode = ZEND_NOP;
    op_array->opcodes[op_block->op_array_end_index - 1].opcode = ZEND_NOP;

    uint32_t arguments_passed = 0;
    uint32_t destination_index = op_block->op_array_start_index;

    // This loop will skip the init at the first index & the call and return at the end.

    uint32_t end_index = (op_block->op_array_end_index - 2);

    for (
        uint32_t i = (op_block->op_array_start_index + 1);
        i <= end_index;
        i++
    ) {
        op = &op_array->opcodes[i];

        fprintf(dbg, "&op_array->opcodes[i]: %d\n", op->opcode);
        fflush(dbg);

        switch (op->opcode) {
            case ZEND_SEND_VAR:
            case ZEND_SEND_VAR_EX:
            case ZEND_SEND_VAL:
            case ZEND_SEND_VAL_EX:
                // This opcode isn't needed - so we'll nop it out.
                // This should get optimised out by later passes elsewhere anyway.
                // (Alternatively, it would be possible to just shuffle the next opcode up, etc.)

                op->opcode = ZEND_NOP;

                // Any T variable used here needs to be protected.
                // (It's going to be needed later.)

                if (op->op1_type == IS_TMP_VAR) {
                    t_remaps[op->op1.var] = next_free_t_var++;
                }

                // Increment the argument counter.

                ++arguments_passed;

                break;

            default:
                /*
                 * If this opcode is trying to read a T var (in either operand)
                 * we need to ensure it's reading from the remapped T var (if applicable).
                 *
                 * If the opcode is trying to alter a protected T var, we need to remap it.
                 *
                 * We should be safe to assume that no opcode will be trying to access
                 * the new T vars - given that they didn't exist until we just created them.
                 */

                tco_check_operand_remaps(op->op1_type, op->op1, t_remaps);
                tco_check_operand_remaps(op->op2_type, op->op2, t_remaps);

                tco_check_operand_remaps(op->result_type, op->result, t_remaps);

                // ...

                break;
        }

        ++destination_index;
    }
}

/*
 * The general idea here is:
 *
 * - Start from the end of the opcode array.
 * - Whenever a return opcode is encountered, be on red alert.
 * - If the next (technically previous) opcode is a function call...
 * - Start gathering useful intel.
 * -
 */
void tco_explore_op_array(tco_context *context)
{
    tco_op_block *current_op_block;
    zend_op *op;
    uint32_t return_index;

    zend_op_array *op_array = context->op_array;

    uint32_t search_state = TCO_STATE_SEEKING_RETURN;

    uint32_t i = op_array->last;

    // I think all op arrays are guaranteed to have at least one opcode, but just in case...

    if (i < 1) {
        // If there are no opcodes, there's nothing to do.

        return;
    }

    // Create a starting op block.

    current_op_block = tco_new_op_block(context);

    // For now, assume that the starting block ends at the end of the op array.
    // (We can also assume that the start is at the beginning.)

    current_op_block->op_array_end_index = i - 1;
    current_op_block->op_array_start_index = 0;

    // Now iterate over the op array.

    do {
        --i;

        op = &op_array->opcodes[i];

        switch (op->opcode) {
            case ZEND_RETURN:
                search_state = TCO_STATE_SEEKING_CALL;
                return_index = i;

                break;

            case ZEND_DO_UCALL:
            case ZEND_DO_FCALL:
            case ZEND_DO_FCALL_BY_NAME:
                if (search_state == TCO_STATE_SEEKING_CALL) {
                    search_state = TCO_STATE_SEEKING_INIT;
                }

                break;

         // case ZEND_INIT_NS_FCALL_BY_NAME:
            case ZEND_INIT_METHOD_CALL:
            case ZEND_INIT_STATIC_METHOD_CALL:
            case ZEND_INIT_FCALL:
            case ZEND_INIT_FCALL_BY_NAME:
                // Determine whether this is a recursive call.

                fprintf(dbg, "Function call in: %s\n", op_array->function_name->val);
                fflush(dbg);

                if (tco_is_call_recursive(op_array, op)) {
                    /*
                     * If we got this far, every opcode between here & return_index
                     * needs to be placed in its own block.
                     */

                    /* If the end index of the current block is the same as return_index,
                     * we can reuse the current block (as nothing else is in it).
                     * Otherwise, we'll have to create a new one.
                     */

                    if (current_op_block->op_array_end_index != return_index) {
                        // Set the start index of the current block to whatever comes after return_index.

                        current_op_block->op_array_start_index = return_index + 1;

                        // Create a new block for the recursive call.

                        current_op_block = tco_new_op_block(context);
                    }

                    // Point the current block (whether it be new or old) to the recursive call opcodes.

                    current_op_block->op_array_end_index = return_index;
                    current_op_block->op_array_start_index = i;

                    // Here we need to run some analysis on the call - so we know in advance
                    // what will need to be rewritten later (in tco_assemble_blocks).

                    tco_analyse_call(current_op_block, context);

                    // Create a new block for whatever comes next.
                    // (Which we also only need to do if we know there's still more opcodes to process.)

                    if (i > 0) {
                        current_op_block = tco_new_op_block(context);

                        current_op_block->op_array_end_index = i - 1;
                        current_op_block->op_array_start_index = 0;
                    }

                    fprintf(dbg, "(Call is recursive)\n");
                    fflush(dbg);
                }

                // Either way, reset the search state.

                search_state = TCO_STATE_SEEKING_RETURN;

                break;

            default:
                /*
                 * If we're here, the current opcode is neither a return,
                 * a call or an init.
                 *
                 * If the current search state is TCO_STATE_SEEKING_CALL, we need
                 * to reset to TCO_STATE_SEEKING_RETURN (because this means we found
                 * a return, but no immediate call i.e. this cannot be a tail call).
                 */

                if (search_state == TCO_STATE_SEEKING_CALL) {
                    search_state = TCO_STATE_SEEKING_RETURN;
                }
        }
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

    fprintf(dbg, "Processing: %s\n", op_array->function_name->val);
    fflush(dbg);

    tco_context *context = tco_new_context(op_array);

    tco_explore_op_array(context);

    if (context->do_optimise) {
        tco_assemble_blocks(context);

        fprintf(dbg, "Assembled blocks\n");
        fflush(dbg);
    }

    tco_free_context(context);

    fprintf(dbg, "Freed stuff.\n");
    fflush(dbg);
}

/*
 */
void tco_cleanup()
{

}

/* Main startup function for the extension. */
static void tco_startup(void)
{
    dbg = fopen("G:/dev/tailcall/astlog.txt", "w");
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