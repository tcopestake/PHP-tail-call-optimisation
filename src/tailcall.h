/*
 * The value of TCO_CALL_POOL_SIZE should be set based on the max number of
 * recursive tail calls expected in any given function. A pool of 8 will allow
 * for ~4 tail calls before new memory will start being allocated on-the-fly.
 * (Never set below 1 btw.)
 */

#define TCO_CALL_POOL_SIZE 8

/* Some variables/types/etc. */

FILE *dbg;

typedef uint32_t tco_t_remap;

typedef struct _tco_call_info {
    uint16_t number;
    zend_op *pseudo_ops;
    uint32_t next_pseudo_op;
    struct _tco_call_info *previous;
} tco_call_info;

typedef struct _tco_context {
    bool do_optimise;
    uint32_t required_opcode_count;
    tco_call_info *call_info_tail;
    zend_op_array *op_array;
    tco_t_remap *t_remaps;
    uint32_t appendix_offset;
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