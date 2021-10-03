/* Some variables/types/etc. */

typedef uint32_t tco_t_remap;

typedef struct _tco_context {
    zend_op_array *op_array;
    tco_t_remap *t_remaps;
    uint32_t start_address;
} tco_context;

enum {
    TCO_STATE_SEEKING_RETURN,
    TCO_STATE_SEEKING_CALL,
    TCO_STATE_SEEKING_INIT,
};

/* This macro just helps look up the recv opcode from a given argument # */

#define TCO_ARG_RECV_OPCODE(op_array, arg_index) op_array->opcodes[arg_index]

/* Handle platform-specific hax */

#ifndef ZEND_EXT_API
    #ifdef _WIN32
        #define ZEND_EXT_API __declspec(dllexport)
    #else
        #define ZEND_EXT_API
    #endif
#endif