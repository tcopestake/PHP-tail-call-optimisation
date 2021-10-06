/*
 * The value of TCO_CALL_POOL_SIZE should be set based on the max number of
 * recursive tail calls expected in any given function. A pool of 8 will allow
 * for ~4 tail calls before new memory will start being allocated on-the-fly.
 * (Never set below 1 btw.)
 */

#define TCO_CALL_POOL_SIZE 8

/* Some variables/types/etc. */

typedef struct _tco_call_meta {
    uint16_t number;
    uint32_t *arg_mapping;
    uint32_t spare_start_index;
    uint32_t spare_last_index;
    struct _tco_call_meta *previous;
} tco_call_meta;

typedef struct _tco_context {
    bool do_compile;
    zend_op_array *op_array;
    uint32_t *t_remaps;
    uint32_t start_address;
    tco_call_meta *call_meta_tail;
    uint32_t total_extra_ops;
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