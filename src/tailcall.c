/***
 *
 * For the rundown on how this all works, check ../README.md
 *
 **/

#include <stddef.h>
#include <stdbool.h>
#include "php.h"
#include "zend_extensions.h"
#include "tailcall.h"

/*
 * Creates a new optomisation context.
 *
 * (In practice, the context is just a container for all relevant data.)
 */
tco_context *tco_new_context(zend_op_array *op_array)
{
    tco_context *context = malloc(sizeof(tco_context));

    context->op_array = op_array;
    context->t_remaps = NULL;
    context->start_address = 0;

    // (Have a guess what this does.)

    return context;
}

/*
 * Frees all memory associated with a given context - including memory
 * allocated for the context itself.
 */
void tco_free_context(tco_context *context)
{
    // Free any memory allocated for T var remaps.

    if (context->t_remaps) {
        free(context->t_remaps);
    }

    // (We need to free any Zend strings/vars here somewhere eventually.)

    // Free the memory allocated for the context itself.

    free(context);
}

/*
 * Converts a given opcode to a ZEND_NOP.
 */
inline void tco_nop_out(zend_op *op)
{
	op->opcode = ZEND_NOP;

	SET_UNUSED(op->op1);
	SET_UNUSED(op->op2);
	SET_UNUSED(op->result);
}

/*
 * Converts a given opcode to a ZEND_JMP to a given address.
 */
inline void tco_make_jmp(zend_op *op, uint32_t address)
{
	op->opcode = ZEND_JMP;
	op->extended_value = 0;
	op->op1.opline_num = address;

	SET_UNUSED(op->op1);
	SET_UNUSED(op->op2);
	SET_UNUSED(op->result);
}

/*
 * Determines whether a given init opcode is a recursive function call.
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
                        Z_STR_P(CT_CONSTANT_EX(op_array, op->op1.constant)),
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

    /*
     * If we got this far, the call either isn't a method call - or the
     * method call was within the same class/scope/whatever.
     * In either case, we now need to check the callable name.
     */

    return zend_string_equals(
        Z_STR_P(CT_CONSTANT_EX(op_array, op->op2.constant)),
        op_array->function_name
    );
}

/*
 * Returns an array for tracking T var remaps.
 */
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

/*
 * Remaps the T variable used by a given operand - if applicable.
 */
inline void tco_do_operand_remaps(zend_uchar type, znode_op operand, tco_t_remap *t_remaps)
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
 * Finds the index (0-based) of a given argument, by name.
 * In theory, there should never be any situation in which this would
 * fail to find a value.
 */
uint32_t tco_find_named_arg(zend_string *arg_name, tco_context *context)
{
    zend_op_array *op_array = context->op_array;

    for (uint32_t i = 0; i < op_array->num_args; i++) {
        if (zend_string_equals(op_array->arg_info[i].name, arg_name)) {
            return i;
        }
    }

    // (We should never be able to get this far.)

    return 0;
}

/*
 * This function will analyse the opcodes for a given ...
 */
void tco_optimise_recursive_call(
    tco_context *context,
    uint32_t start_index,
    uint32_t end_index
) {
    zend_op *op;
    zend_op *new_op;

    uint32_t next_arg_index;

    /*
     * The general idea here is:
     *
     * - Identify which T vars need to be preserved (i.e. those passed as arguments).
     * - Track how much additional space will eventually be needed for the modified opcodes.
     * - Rewrite the opcodes to remove the init/call/return/etc.
     * - Write the opcodes to assign values to variables used for arguments and
     *   jump back to the beginning of the function (after the recv opcodes).
     * - Where the recursive calls would have been, we'll instead add
     *   a jump to the aforementioned opcodes.
     *
     * (Not necessarily in that order.)
     */

    zend_op_array *op_array = context->op_array;

    // This array will be used to map arguments to their respective T vars.

    uint32_t *arg_mapping = calloc(op_array->num_args, sizeof(arg_mapping));

    // T variable mapping.
    // (op_array->T itself will be updated elsewhere.)

    tco_t_remap *t_remaps = tco_get_t_remaps(context);

    // This is used to track the next available (newly-created) T var.

    uint32_t next_free_t_var = op_array->T;

    /*
     * As we go through the opcodes, the ones we want to keep will be shuffled up
     * the op array. This variable will be used to track which index should be
     * written to next.
     * (Obviously we want to start by overwriting the first opcode.)
     */

    uint32_t destination_index = start_index;

    // This loop will skip the init at the first index & the call and return at the end.

    uint32_t index_limit = end_index - 2;

    for (
        uint32_t i = start_index + 1;
        i <= index_limit;
        i++
    ) {
        op = &op_array->opcodes[i];

        /*
         * If this opcode is trying to read a T var (in either operand)
         * we need to ensure it's reading from the remapped T var (if applicable).
         *
         * If the opcode is trying to alter a protected T var, we need to remap it.
         *
         * We should be safe to assume that no opcode will be trying to access
         * the new T vars - given that they didn't exist until we just created them.
         *
         * This part needs to be done irrespective of the opcode in question - and
         * it's important that it's done before anything else.
         */

        tco_do_operand_remaps(op->op1_type, op->op1, t_remaps);
        tco_do_operand_remaps(op->op2_type, op->op2, t_remaps);
        tco_do_operand_remaps(op->result_type, op->result, t_remaps);

        // Certain opcodes require additional processing.

        switch (op->opcode) {
            case ZEND_CHECK_UNDEF_ARGS:
                // This opcode isn't needed.

                tco_nop_out(op);

                break;

            case ZEND_SEND_VAR_EX:
            case ZEND_SEND_VAL_EX:
            case ZEND_SEND_VAR:
            case ZEND_SEND_VAL:
                // If I'm not wrong, operand 2 being a constant means it's a named argument.
                // Otherwise, operand 2 is the argument #... I think.

                if (op->op2_type == IS_CONST) {
                    // In theory, this function should never fail to find a value.

                    next_arg_index = tco_find_named_arg(
                        Z_STR_P(CT_CONSTANT_EX(op_array, op->op2.constant)),
                        context
                    );
                } else {
                    // (The indices in the operand are 1-based - so we'll have to subtract 1.)

                    next_arg_index = op->op2.num - 1;
                }

                // Map this argument to its respective (T) variable.

                arg_mapping[next_arg_index] = op->op1.var;

                // Any T variable used here needs to be protected.
                // (It needs to retain its value for the assignment later.)

                if (op->op1_type == IS_TMP_VAR) {
                    t_remaps[op->op1.var] = next_free_t_var++;
                }

                // Nop out the original opcode also - just to keep things clean.
                // (If it's nopped, it's more likely to get optimised out later.)

                tco_nop_out(op);

                break;

            default:
                // For all other opcodes, copy the opcode to its new location
                // (and increment destination_index for the next opcode).

                op_array->opcodes[destination_index++] = *op;

                break;
        }
    }

    /*
     * Here we'll iterate over every argument. If the arg is mapped to a T var, we'll
     * create an assignment of that T var, to that argument's variable.
     * Otherwise, we'll create an assignment of that argument's default value.
     *
     * e.g. If an opcode was trying to pass T4 as the first argument - and
     * the first argument is referred to using the variable $a - then
     * this will instead just assign T4 to $a.
     *
     * If the function has another argument referred to by the variable $b - and
     * no opcodes passed any values as the second argument - and $b has a default
     * value of 200 - then instead we'll create an assignment of the constant
     * value 200, to $b.
     *
     * The earlier T var remapping should have ensured that each T var will
     * still have the appropriate/original/relevant value by the time we get here.
     */

    for (uint32_t arg_index = 0; arg_index < op_array->num_args; arg_index++) {
        // Get the next available opcode (and increment destination_index).

        new_op = &op_array->opcodes[destination_index++];

        // Initialise the opcode to an assignment to this argument's variable.

        new_op->opcode = ZEND_ASSIGN;

        new_op->op1_type = IS_CV;
        new_op->op1.var = TCO_ARG_RECV_OPCODE(op_array, arg_index).result.var;

        SET_UNUSED(new_op->result);

        // Set the 2nd operand to either the appropriate T var or the default constant.

        if (arg_mapping[arg_index]) {
            new_op->op2_type = IS_TMP_VAR;
            new_op->op2.var = arg_mapping[arg_index];
        } else {
            new_op->op2_type = IS_CONST;
            new_op->op2.constant = TCO_ARG_RECV_OPCODE(op_array, arg_index).op2.constant;
        }
    }

    // The next opcode needs to be a jump back to the beginning of the function.

    tco_make_jmp(&op_array->opcodes[destination_index++], context->start_address);

    // This isn't strictly necessary, but we'll nop out any remaining unused opcodes.

    for (; destination_index <= end_index; destination_index++) {
        tco_nop_out(&op_array->opcodes[destination_index]);
    }
    
    // Free any allocated memory, etc.

    free(arg_mapping);
}

/*
 * Analyses the op array, looking for & optimising any recursive function calls.
 */
void tco_analyse(tco_context *context)
{
    zend_op *op;
    uint32_t return_index;

    zend_op_array *op_array = context->op_array;

    uint32_t search_state = TCO_STATE_SEEKING_RETURN;

    uint32_t i;

    // I think all op arrays are guaranteed to have at least one opcode, but just in case...

    if (op_array->last < 1) {
        // If there are no opcodes, there's nothing to do.

        return;
    }

    /*
     * First, we need to iterate "forwards" over the opcodes, looking for where
     * the various recv opcodes end - because we need this starting address
     * so that we know where to jump (back) to for each reiteration.
     *
     * (This could/should maybe be deferred & done elsewhere.)
     */

    bool found_start_address = false;

    for (
        i = 0;
        i < op_array->last && !found_start_address;
        i++
    ) {
        switch (op_array->opcodes[i].opcode) {
            case ZEND_RECV_INIT:
            case ZEND_RECV:
                break;

            default:
                /*
                 * If we're here, context->start_address should be pointing to
                 * the address of the first opcode of the function, sans any
                 * initialisation, etc.
                 */

                context->start_address = i;

                found_start_address = true;
        }
    }

    /*
     * Now we need to go over the rest of the opcodes, but backwards.
     */

    for (i = op_array->last; i > 0; i--) {
        op = &op_array->opcodes[i];

        switch (op->opcode) {
            case ZEND_RETURN:
                // Found a return; now we want to see if it was preceded by a call opcode.

                search_state = TCO_STATE_SEEKING_CALL;
                return_index = i;

                break;

            case ZEND_DO_UCALL:
            case ZEND_DO_FCALL:
            case ZEND_DO_FCALL_BY_NAME:
                if (search_state == TCO_STATE_SEEKING_CALL) {
                    // Found a call immediately preceding a return; now we want
                    // to find its respective init call.

                    search_state = TCO_STATE_SEEKING_INIT;
                }

                break;

         // case ZEND_INIT_NS_FCALL_BY_NAME:
            case ZEND_INIT_METHOD_CALL:
            case ZEND_INIT_STATIC_METHOD_CALL:
            case ZEND_INIT_FCALL:
            case ZEND_INIT_FCALL_BY_NAME:
                if (search_state == TCO_STATE_SEEKING_INIT) {
                    // Found a tail call; now determine whether it's a recursive call.

                    if (tco_is_call_recursive(op_array, op)) {
                        // If so, run analysis on / optimisation of the call.

                        tco_optimise_recursive_call(context, i, return_index);
                    }
                }

                // Either way, reset the search state to look for returns.

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
    };
}

/*
 * Main "entry point" for the module. Zend will call this method and pass in
 * the current op array. We'll walk over it and perform any optimisations - and
 * if applicable, update the op array with a new set of (optimised) opcodes.
 */
static void tco_op_handler(zend_op_array *op_array)
{
    // If this array has no name, we ain't interested.

    if (!op_array->function_name) {
        return;
    }

    // Create a context for this instance.

    tco_context *context = tco_new_context(op_array);

    // Run the analysis to look for recursive calls, etc.

    tco_analyse(context);

    // (We're finished here.)

    tco_free_context(context);
}

/*
 * Main startup function for the extension.
 */
static void tco_startup(void)
{
    // (Bootstrap code can go here.)
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
    NULL, // tco_startup,
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