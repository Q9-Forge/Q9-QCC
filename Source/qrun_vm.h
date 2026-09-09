#ifndef QRUN_VM_H
#define QRUN_VM_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * QCC Stack IR Interpreter - Virtual Machine
 * ========================================================================= */

/* Value: 32-bit integer (can represent ints, bools, etc.) */
typedef int32_t qrun_value_t;

/* Pointer: (block_id, offset) pair for pointer semantics */
typedef struct {
    int32_t block;      /* 0 = globals, >0 = local frame */
    int32_t offset;     /* byte offset within block */
} qrun_pointer_t;

/* Stack entry: can hold value OR pointer */
typedef union {
    qrun_value_t   val;
    qrun_pointer_t ptr;
} qrun_stackval_t;

/* Opcode enum - Phase 1 subset */
typedef enum {
    /* Control flow */
    OP_FUNC,        /* FUNC <name> <nargs> <nlocals> */
    OP_ENDFUNC,     /* ENDFUNC */
    OP_RET,         /* RET */
    OP_LABEL,       /* LABEL <name> */
    OP_JMP,         /* JMP <label> */
    
    /* Stack ops */
    OP_PUSH,        /* PUSH <int32> */
    OP_DUP,         /* DUP */
    OP_SWAP,        /* SWAP */
    
    /* Load/Store local */
    OP_LOADL,       /* LOADL <slot> */
    OP_STOREL,      /* STOREL <slot> */
    
    /* Load/Store global */
    OP_LOADG,       /* LOADG <name> */
    OP_STOREG,      /* STOREG <name> */
    OP_LOADGP,      /* LOADGP <name> - load pointer global */
    OP_STOREGP,     /* STOREGP <name> - store pointer global */
    
    /* Arithmetic */
    OP_ADD,         /* ADD */
    OP_SUB,         /* SUB */
    OP_MUL,         /* MUL */
    OP_DIV,         /* DIV */
    OP_MOD,         /* MOD */
    OP_NEG,         /* NEG */
    
    /* Comparisons (signed) */
    OP_CMPEQ,       /* CMPEQ */
    OP_CMPNE,       /* CMPNE */
    OP_CMPLT,       /* CMPLT */
    OP_CMPLE,       /* CMPLE */
    OP_CMPGT,       /* CMPGT */
    OP_CMPGE,       /* CMPGE */
    
    /* Comparisons (unsigned) */
    OP_CMPULT,      /* CMPULT */
    OP_CMPUGE,      /* CMPUGE */
    OP_CMPULE,      /* CMPULE */
    OP_CMPUGT,      /* CMPUGT */
    
    /* Conditional branches */
    OP_BEQ,         /* BEQ <label> - branch if top == 0 */
    OP_BNE,         /* BNE <label> - branch if top != 0 */
    
    /* Function calls */
    OP_CALL,        /* CALL <name> <nargs> */
    
    /* Arrays & Locals */
    OP_LARRAY,      /* LARRAY <slot> <type> <size> */
    OP_GARRAY,      /* GARRAY <name> <type> <size> <init> */
    OP_LOADIDX,     /* LOADIDX <scope> <name> <type> */
    OP_STOREIDX,    /* STOREIDX <scope> <name> <type> */
    
    /* Pointers */
    OP_ADDRL,       /* ADDRL <slot> - address of local */
    OP_ADDRG,       /* ADDRG <name> - address of global */
    OP_LOADP,       /* LOADP <slot> - load pointer from local */
    OP_STOREP,      /* STOREP <slot> - store pointer to local */
    OP_LOADIND,     /* LOADIND <type> - dereference and load */
    OP_STOREIND,    /* STOREIND <type> - dereference and store */
    OP_IPADD,       /* IPADD <type> - pointer += int * sizeof(type) */
    OP_PADD,        /* PADD <type> - pointer + pointer (offset add) */
    OP_PCMPNE,      /* PCMPNE - compare pointers for inequality */
    OP_PUSHADDR,    /* PUSHADDR <scope> <slot/name> <offset> */
    OP_IPADDN,      /* IPADDN <size> - pointer += int * size (runtime size) */
    
    /* IO/Debug */
    OP_PRINT,       /* PRINT - pop and print top value */
    OP_PRINTC,      /* PRINTC - pop and print as char */
    
    /* Declarations */
    OP_GLOBAL,      /* GLOBAL <name> [init] */
    OP_GINIT,       /* GINIT <name> <index> <value> - initialize global array element */
    
    /* Sentinel */
    OP_HALT
} qrun_opcode_t;

/* Instruction: opcode + arguments */
typedef struct {
    qrun_opcode_t op;
    
    union {
        int32_t   i;                    /* integer argument */
        char*     s;                    /* string argument (name, label) */
        struct {
            char* name;
            int nargs;
            int nlocals;
        } func;                         /* FUNC arguments */
        struct {
            char* name;
            int nargs;
        } call;                         /* CALL arguments */
        struct {
            int slot;                   /* local slot or scope for LOADIDX/STOREIDX */
            char* type;                 /* type tag (i, c, h, etc.) */
            int size;                   /* array size (for LARRAY) */
            char* name;                 /* name (for GARRAY, LOADIDX, STOREIDX) */
        } array;                        /* Array operations */
        struct {
            char* name;                 /* global array name */
            int index;                  /* array index */
            int32_t value;              /* value to initialize */
        } ginit;                        /* GINIT - global array initialization */
    } arg;
} qrun_instruction_t;

/* VM State */
typedef struct {
    /* Instruction stream */
    qrun_instruction_t* code;
    size_t code_size;
    size_t pc;              /* Program counter */
    
    /* Function lookup table */
    struct {
        char* name;
        size_t addr;        /* instruction index */
        int nargs;
        int nlocals;
    }* funcs;
    size_t nfuncs;
    
    /* Label lookup table */
    struct {
        char* name;
        size_t addr;        /* instruction index */
    }* labels;
    size_t nlabels;
    size_t labels_capacity;
    
    /* Value stack */
    qrun_stackval_t* stack;
    size_t stack_size;
    size_t sp;              /* Stack pointer (next free) */
    
    /* String pool (for IR strings, kept alive) */
    void* string_pool;      /* qrun_string_pool_t* */
    
    /* Globals storage (flat memory) */
    qrun_value_t* globals;
    size_t globals_size;
    
    /* Named globals (for __ptrsize and similar) */
    struct {
        const char* name;
        qrun_value_t value;
    }* named_globals;
    size_t nglobals;
    size_t nglobals_capacity;
    
    /* Pointer heap - simple memory model for pointers */
    qrun_value_t* heap;
    size_t heap_size;
    size_t heap_capacity;
    
    /* Global array handle pointers (for PUSHADDR to reference with offset arithmetic)
       Maps handle index to actual data pointer in garrays */
    qrun_value_t** garray_handles;
    size_t ngarray_handles;
    size_t garray_handles_capacity;
    
    /* Global arrays */
    struct {
        char* name;
        qrun_value_t* data;
        size_t size;
        int type_size;
    }* garrays;
    size_t ngarrays;
    size_t garrays_capacity;
    
    /* Call stack / local frames */
    struct {
        size_t code_addr;   /* return address (instruction index) */
        qrun_value_t* locals;  /* local variables for this frame (sparse, up to 256) */
        size_t nlocals_allocated;  /* size of locals array */
        
        /* Local arrays indexed by slot */
        struct {
            qrun_value_t* data;
            size_t size;
            int type_size;  /* 1, 2, 4, or 8 bytes */
        }* arrays;
        size_t narrays;  /* number of arrays allocated */
    }* frames;
    size_t frame_size;
    size_t fp;              /* Frame pointer (next free) */
    
    /* Halt flag */
    int halted;
    int32_t exit_code;
} qrun_vm_t;

/* VM API */
qrun_vm_t* qrun_vm_create(void);
void       qrun_vm_destroy(qrun_vm_t* vm);
int        qrun_vm_load_ir(qrun_vm_t* vm, const char* filename);
int        qrun_vm_run(qrun_vm_t* vm);

#endif /* QRUN_VM_H */
