#ifndef QRUN_VM_H
#define QRUN_VM_H

#ifdef QRUN_OS9
#include "qrun_os9.h"
#define QRUN_PTR_BYTES 8
#else
#include <stdint.h>
#include <stddef.h>
#define QRUN_PTR_BYTES sizeof(void*)
#endif

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
typedef struct {
    qrun_value_t val;
} qrun_stackval_t;

/* Opcode enum - Phase 1 subset */
typedef int qrun_opcode_t;
#define OP_FUNC 0
#define OP_ENDFUNC 1
#define OP_RET 2
#define OP_LABEL 3
#define OP_JMP 4
#define OP_PUSH 5
#define OP_DUP 6
#define OP_SWAP 7
#define OP_DROP 8
#define OP_LOADL 9
#define OP_STOREL 10
#define OP_LOADG 11
#define OP_STOREG 12
#define OP_LOADGP 13
#define OP_STOREGP 14
#define OP_ADD 15
#define OP_SUB 16
#define OP_MUL 17
#define OP_DIV 18
#define OP_MOD 19
#define OP_NEG 20
#define OP_BAND 21
#define OP_BOR 22
#define OP_BXOR 23
#define OP_NOTBIT 24
#define OP_SHL 25
#define OP_SHR 26
#define OP_USHR 27
#define OP_CMPEQ 28
#define OP_CMPNE 29
#define OP_CMPLT 30
#define OP_CMPLE 31
#define OP_CMPGT 32
#define OP_CMPGE 33
#define OP_CMPULT 34
#define OP_CMPUGE 35
#define OP_CMPULE 36
#define OP_CMPUGT 37
#define OP_BEQ 38
#define OP_BNE 39
#define OP_CALL 40
#define OP_LARRAY 41
#define OP_GARRAY 42
#define OP_LOADIDX 43
#define OP_STOREIDX 44
#define OP_ADDRL 45
#define OP_ADDRG 46
#define OP_LOADP 47
#define OP_STOREP 48
#define OP_LOADIND 49
#define OP_STOREIND 50
#define OP_IPADD 51
#define OP_PTRINDEX 52
#define OP_PADD 53
#define OP_PCMPNE 54
#define OP_PUSHADDR 55
#define OP_IPADDN 56
#define OP_PRINT 57
#define OP_PRINTC 58
#define OP_GLOBAL 59
#define OP_GINIT 60
#define OP_GLOBALDECL 61
#define OP_FUNCDECL 62
#define OP_HALT 63

/* Flat records keep the layout expressible by the QCC frontend. */
typedef struct {
    qrun_opcode_t op;
    int32_t arg_i;
    char* arg_s;
    int arg_nargs;
    int arg_func_nlocals;
    int arg_array_slot;
    char* arg_type;
    int arg_array_size;
    int arg_ginit_index;
    int32_t arg_ginit_value;
    int arg_global_scope;
    int32_t arg_global_init;
} qrun_instruction_t;
typedef struct {
    char* name;
    size_t addr;
    int nargs;
    int nlocals;
} qrun_function_t;
typedef struct {
    char* name;
    size_t addr;
} qrun_label_t;
typedef struct {
    const char* name;
    qrun_value_t value;
} qrun_global_t;
typedef struct {
    char* name;
    qrun_value_t* data;
    size_t size;
    int type_size;
} qrun_array_t;
typedef struct {
    size_t code_addr;
    qrun_value_t* locals;
    size_t nlocals_allocated;
    qrun_array_t* arrays;
    size_t narrays;
} qrun_frame_t;
typedef qrun_value_t* qrun_handle_t;
typedef struct {
    qrun_instruction_t* code;
    qrun_function_t* funcs;
    qrun_label_t* labels;
    qrun_stackval_t* stack;
    void* string_pool;
    qrun_value_t* globals;
    qrun_global_t* named_globals;
    qrun_value_t* heap;
    qrun_handle_t* garray_handles;
    int* garray_handle_indices;
    qrun_array_t* garrays;
    qrun_frame_t* frames;
    int state[24];
} qrun_vm_t;
#define QRUN_CODE_SIZE 0
#define QRUN_PC 1
#define QRUN_NFUNCS 2
#define QRUN_NLABELS 3
#define QRUN_LABELS_CAPACITY 4
#define QRUN_STACK_SIZE 5
#define QRUN_SP 6
#define QRUN_GLOBALS_SIZE 7
#define QRUN_NGLOBALS 8
#define QRUN_NGLOBALS_CAPACITY 9
#define QRUN_HEAP_SIZE 10
#define QRUN_HEAP_CAPACITY 11
#define QRUN_NGARRAY_HANDLES 12
#define QRUN_GARRAY_HANDLES_CAPACITY 13
#define QRUN_NGARRAYS 14
#define QRUN_GARRAYS_CAPACITY 15
#define QRUN_FRAME_SIZE 16
#define QRUN_FP 17
#define QRUN_HALTED 18
#define QRUN_EXIT_CODE 19

/* VM API */
qrun_vm_t* qrun_vm_create(void);
void       qrun_vm_destroy(qrun_vm_t* vm);
int        qrun_vm_load_ir(qrun_vm_t* vm, const char* filename);
int        qrun_vm_run(qrun_vm_t* vm);

#endif /* QRUN_VM_H */
