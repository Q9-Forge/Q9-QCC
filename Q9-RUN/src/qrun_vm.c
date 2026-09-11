#include "qrun_vm.h"
#include "qrun_ir.h"
#ifndef QRUN_OS9
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/* =========================================================================
 * VM Creation & Initialization
 * ========================================================================= */

qrun_vm_t* qrun_vm_create(void)
{
    qrun_vm_t* vm = malloc(sizeof(qrun_vm_t));
    if (!vm) return NULL;
    
    memset(vm, 0, sizeof(qrun_vm_t));
    
    /* Allocate stack */
    vm->stack = malloc(4096 * sizeof(qrun_stackval_t));
    vm->state[QRUN_STACK_SIZE] = 4096;
    vm->state[QRUN_SP] = 0;
    
    /* Allocate globals */
    vm->globals = malloc(1024 * sizeof(qrun_value_t));
    vm->state[QRUN_GLOBALS_SIZE] = 1024;
    
    /* Allocate named globals (for __ptrsize, etc.) */
    vm->named_globals = malloc(64 * sizeof(qrun_global_t));
    vm->state[QRUN_NGLOBALS] = 0;
    vm->state[QRUN_NGLOBALS_CAPACITY] = 64;
    
    /* Allocate call frames */
    vm->frames = malloc(256 * sizeof(qrun_frame_t));
    vm->state[QRUN_FRAME_SIZE] = 256;
    vm->state[QRUN_NFUNCS] = 0;
    vm->funcs = malloc(256 * sizeof(qrun_function_t));
    memset(vm->funcs, 0, 256 * sizeof(qrun_function_t));
    vm->state[QRUN_FP] = 0;
    
    qrun_frame_t* frames = vm->frames;
    /* Initialize frames (clear arrays pointers) */
    { int i;
        for (i = 0; i < 256; i++) {
        frames[i].arrays = NULL;
        frames[i].narrays = 0;
    }
        }
    
    /* Allocate labels */
    vm->labels = malloc(512 * sizeof(qrun_label_t));
    vm->state[QRUN_NLABELS] = 0;
    vm->state[QRUN_LABELS_CAPACITY] = 512;
    
    /* Allocate pointer heap */
    /* QCC-generated programs use sizeable local character buffers.  Keep
       enough room for those buffers in the reference VM; pointers to local
       arrays are represented as encoded offsets, so a fixed backing store
       also avoids invalidating the array views held by call frames. */
    vm->heap = malloc(1024 * 1024 * sizeof(qrun_value_t));
    vm->state[QRUN_HEAP_SIZE] = 0;
    vm->state[QRUN_HEAP_CAPACITY] = 1024 * 1024;
    
    /* Allocate global array handles */
    vm->garray_handles = malloc(64 * QRUN_PTR_BYTES);
    vm->garray_handle_indices = malloc(64 * sizeof(int));
    vm->state[QRUN_NGARRAY_HANDLES] = 0;
    vm->state[QRUN_GARRAY_HANDLES_CAPACITY] = 64;
    
    /* Allocate global arrays */
    vm->garrays = malloc(64 * sizeof(qrun_array_t));
    vm->state[QRUN_NGARRAYS] = 0;
    vm->state[QRUN_GARRAYS_CAPACITY] = 64;
    
    vm->string_pool = NULL;  /* Will be set by qrun_vm_load_ir */
    
    vm->state[QRUN_HALTED] = 0;
    vm->state[QRUN_EXIT_CODE] = 0;
    
    return vm;
}

void qrun_vm_destroy(qrun_vm_t* vm)
{
    qrun_array_t* garrays = vm->garrays;
    qrun_frame_t* frames = vm->frames;
    if (!vm) return;
    
    free(vm->code);
    free(vm->stack);
    free(vm->globals);
    free(vm->named_globals);
    free(vm->heap);
    free(vm->garray_handles);
    free(vm->garray_handle_indices);
    
    /* Destroy frame local arrays */
    { size_t i;
        for (i = 0; i < vm->state[QRUN_FP]; i++) {
        free(frames[i].locals);
        if (frames[i].arrays) {
            { size_t j;
        for (j = 0; j < frames[i].narrays; j++) {
                /* Array views belong to vm->heap. */
            }
        }
            free(frames[i].arrays);
        }
    }
        }
    free(vm->frames);
    free(vm->funcs);
    free(vm->labels);
    
    /* Destroy global arrays */
    { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
        free(garrays[i].data);
    }
        }
    free(vm->garrays);
    
    /* Destroy string pool */
    if (vm->string_pool) {
        qrun_string_pool_destroy((qrun_string_pool_t*)vm->string_pool);
    }
    
    free(vm);
}

/* Forward declaration */
static int qrun_type_size(const char* type);

int qrun_vm_load_ir(qrun_vm_t* vm, const char* filename)
{
    qrun_function_t* funcs = vm->funcs;
    qrun_label_t* labels = vm->labels;
    qrun_global_t* named_globals = vm->named_globals;
    qrun_array_t* garrays = vm->garrays;
    if (!vm || !filename) return -1;
    
    qrun_instruction_t* code = NULL;
    size_t code_size = 0;
    void* pool = NULL;
    if (qrun_ir_parse(filename, &code, &code_size, &pool) != 0) {
        return -1;
    }
    vm->code = code;
    vm->state[QRUN_CODE_SIZE] = code_size;
    vm->string_pool = pool;
    /* Build function, label, and global array lookup tables */
    { size_t i;
        for (i = 0; i < vm->state[QRUN_CODE_SIZE]; i++) {
        if (code[i].op == OP_FUNC) {
            if (vm->state[QRUN_NFUNCS] >= 256) {
                fprintf(stderr, "Too many functions\n");
                return -1;
            }
            funcs[vm->state[QRUN_NFUNCS]].name = code[i].arg_s;
            funcs[vm->state[QRUN_NFUNCS]].addr = i;
            funcs[vm->state[QRUN_NFUNCS]].nargs = code[i].arg_nargs;
            funcs[vm->state[QRUN_NFUNCS]].nlocals = code[i].arg_func_nlocals;
            vm->state[QRUN_NFUNCS]++;
        } else if (code[i].op == OP_LABEL) {
            if (vm->state[QRUN_NLABELS] >= vm->state[QRUN_LABELS_CAPACITY]) {
                fprintf(stderr, "Too many labels\n");
                return -1;
            }
            labels[vm->state[QRUN_NLABELS]].name = code[i].arg_s;
            labels[vm->state[QRUN_NLABELS]].addr = i;
            vm->state[QRUN_NLABELS]++;
        } else if (code[i].op == OP_GARRAY) {
            /* Allocate global arrays at load time */
            const char* name = code[i].arg_s;
            const char* type = code[i].arg_type;
            size_t size = code[i].arg_array_size;
            int tsize = qrun_type_size(type);
            
            if (vm->state[QRUN_NGARRAYS] >= vm->state[QRUN_GARRAYS_CAPACITY]) {
                fprintf(stderr, "Too many global arrays\n");
                return -1;
            }
            
            /* Keep one VM value per logical array element.  Pointer offsets
               remain byte offsets, but this representation matches LOADIDX /
               STOREIDX and the qccvm oracle for char/short/int arrays. */
            size_t nvalues = size;

            garrays[vm->state[QRUN_NGARRAYS]].name = (char*)name;
            garrays[vm->state[QRUN_NGARRAYS]].data = calloc(nvalues, sizeof(qrun_value_t));
            garrays[vm->state[QRUN_NGARRAYS]].size = size;  /* Keep size as element count */
            garrays[vm->state[QRUN_NGARRAYS]].type_size = tsize;
            vm->state[QRUN_NGARRAYS]++;
        } else if (code[i].op == OP_GLOBAL) {
            /* Register global variable at load time */
            const char* name = code[i].arg_s;
            int32_t init_val = code[i].arg_global_init;
            
            if (vm->state[QRUN_NGLOBALS] >= 64) {
                fprintf(stderr, "Too many global variables\n");
                return -1;
            }
            
            named_globals[vm->state[QRUN_NGLOBALS]].name = name;
            named_globals[vm->state[QRUN_NGLOBALS]].value = init_val;
            vm->state[QRUN_NGLOBALS]++;
        }
    }
        }
    
    return 0;
}

/* =========================================================================
 * VM Execution - Stack & Memory
 * ========================================================================= */

static void qrun_push_value(qrun_vm_t* vm, qrun_value_t v)
{
    qrun_stackval_t* stack = vm->stack;
    if (vm->state[QRUN_SP] >= vm->state[QRUN_STACK_SIZE]) {
        fprintf(stderr, "Stack overflow\n");
        vm->state[QRUN_HALTED] = 1;
        return;
    }
    stack[vm->state[QRUN_SP]].val = v;
    vm->state[QRUN_SP]++;
}

static qrun_value_t qrun_pop_value(qrun_vm_t* vm)
{
    qrun_stackval_t* stack = vm->stack;
    if (vm->state[QRUN_SP] == 0) {
        fprintf(stderr, "Stack underflow\n");
        vm->state[QRUN_HALTED] = 1;
        return 0;
    }
    vm->state[QRUN_SP]--;
    return stack[vm->state[QRUN_SP]].val;
}

static qrun_value_t qrun_peek_value(qrun_vm_t* vm)
{
    qrun_stackval_t* stack = vm->stack;
    if (vm->state[QRUN_SP] == 0) return 0;
    return stack[vm->state[QRUN_SP] - 1].val;
}

static int qrun_type_size(const char* type)
{
    if (!type) return 4;
    if (type[0] == 'c' || type[0] == 'b') return 1;  /* char/byte */
    if (type[0] == 'h') return 2;  /* short */
    if (type[0] == 'p') return 8;  /* pointer */
    return 4;  /* default int */
}

static qrun_value_t qrun_load_local(qrun_vm_t* vm, int slot)
{
    qrun_frame_t* frames = vm->frames;
    if (vm->state[QRUN_FP] == 0) {
        fprintf(stderr, "No active frame\n");
        return 0;
    }
    
    size_t frame_idx = vm->state[QRUN_FP] - 1;
    if (slot < 0 || slot >= (int)frames[frame_idx].nlocals_allocated) {
        fprintf(stderr, "Local slot out of bounds: %d\n", slot);
        return 0;
    }
    
    return frames[frame_idx].locals[slot];
}

static void qrun_store_local(qrun_vm_t* vm, int slot, qrun_value_t val)
{
    qrun_frame_t* frames = vm->frames;
    if (vm->state[QRUN_FP] == 0) {
        fprintf(stderr, "No active frame\n");
        return;
    }
    
    size_t frame_idx = vm->state[QRUN_FP] - 1;
    if (slot < 0 || slot >= (int)frames[frame_idx].nlocals_allocated) {
        fprintf(stderr, "Local slot out of bounds: %d\n", slot);
        return;
    }
    
    frames[frame_idx].locals[slot] = val;
}

/* =========================================================================
 * VM Execution - Main Loop
 * ========================================================================= */

int qrun_vm_run(qrun_vm_t* vm)
{
    qrun_handle_t* handles = vm->garray_handles;
    qrun_function_t* funcs = vm->funcs;
    qrun_label_t* labels = vm->labels;
    qrun_global_t* named_globals = vm->named_globals;
    qrun_array_t* garrays = vm->garrays;
    qrun_frame_t* frames = vm->frames;
    if (!vm || !vm->code) {
        fprintf(stderr, "VM not initialized\n");
        return -1;
    }
    
    /* Find main() function */
    size_t main_addr = 0;
    int found_main = 0;
    
    { size_t i;
        for (i = 0; i < vm->state[QRUN_NFUNCS]; i++) {
        if (funcs[i].name && strcmp(funcs[i].name, "main") == 0) {
            main_addr = funcs[i].addr;
            found_main = 1;
            break;
        }
    }
        }
    
    if (!found_main) {
        fprintf(stderr, "Error: main() not found\n");
        return -1;
    }
    
    vm->state[QRUN_PC] = main_addr;
    vm->state[QRUN_HALTED] = 0;
    
    /* Push initial frame for main() */
    frames[0].code_addr = vm->state[QRUN_CODE_SIZE];  /* Return address (end of program) */
    frames[0].nlocals_allocated = 256;
    frames[0].locals = calloc(256, sizeof(qrun_value_t));
    vm->state[QRUN_FP] = 1;
    
    while (!vm->state[QRUN_HALTED] && vm->state[QRUN_PC] < vm->state[QRUN_CODE_SIZE]) {
        qrun_instruction_t* instr = vm->code + vm->state[QRUN_PC];
        qrun_opcode_t op = instr->op;
        
        /* Decode & Execute */
        int should_increment = 1;  /* Most opcodes auto-increment, jumps set to 0 */
        
        switch (op) {
        case OP_PUSH:
            qrun_push_value(vm, instr->arg_i);
            break;
        
        case OP_DUP: {
            qrun_value_t v = qrun_peek_value(vm);
            qrun_push_value(vm, v);
            break;
        }
        
        case OP_SWAP: {
            if (vm->state[QRUN_SP] < 2) {
                fprintf(stderr, "Stack underflow for SWAP\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            qrun_value_t a = qrun_pop_value(vm);
            qrun_value_t b = qrun_pop_value(vm);
            qrun_push_value(vm, a);
            qrun_push_value(vm, b);
            break;
        }

        case OP_DROP:
            (void)qrun_pop_value(vm);
            break;
        
        case OP_LOADL: {
            qrun_value_t v = qrun_load_local(vm, instr->arg_i);
            qrun_push_value(vm, v);
            break;
        }
        
        case OP_STOREL: {
            qrun_value_t v = qrun_pop_value(vm);
            qrun_store_local(vm, instr->arg_i, v);
            break;
        }
        
        case OP_LOADG: {
            const char* name = instr->arg_s;
            qrun_value_t value = 0;
            int found = 0;
            
            /* Search named globals */
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGLOBALS]; i++) {
                if (named_globals[i].name && strcmp(named_globals[i].name, name) == 0) {
                    value = named_globals[i].value;
                    qrun_push_value(vm, value);
                    found = 1;
                    break;
                }
            }
        }
            
            if (!found) {
                fprintf(stderr, "LOADG: '%s' not found in named globals\n", name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_STOREG: {
            const char* name = instr->arg_s;
            qrun_value_t v = qrun_pop_value(vm);
            int found = 0;
            
            /* Search named globals */
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGLOBALS]; i++) {
                if (named_globals[i].name && strcmp(named_globals[i].name, name) == 0) {
                    named_globals[i].value = v;
                    found = 1;
                    break;
                }
            }
        }
            
            if (!found) {
                fprintf(stderr, "STOREG: '%s' not found in named globals\n", name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_LOADGP: {
            /* Load global pointer - same as LOADG (values are just ints) */
            const char* name = instr->arg_s;
            qrun_value_t value = 0;
            int found = 0;
            
            /* Search named globals first */
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGLOBALS]; i++) {
                if (named_globals[i].name && strcmp(named_globals[i].name, name) == 0) {
                    value = named_globals[i].value;
                    qrun_push_value(vm, value);
                    found = 1;
                    break;
                }
            }
        }
            
            if (!found) {
                fprintf(stderr, "LOADGP: '%s' not found in named globals\n", name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_STOREGP: {
            /* Store global pointer - same as STOREG (values are just ints) */
            const char* name = instr->arg_s;
            qrun_value_t v = qrun_pop_value(vm);
            int found = 0;
            
            /* Search named globals */
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGLOBALS]; i++) {
                if (named_globals[i].name && strcmp(named_globals[i].name, name) == 0) {
                    named_globals[i].value = v;
                    found = 1;
                    break;
                }
            }
        }
            
            if (!found) {
                fprintf(stderr, "STOREGP: '%s' not found in named globals\n", name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_ADD: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, a + b);
            break;
        }
        
        case OP_SUB: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, a - b);
            break;
        }
        
        case OP_MUL: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, a * b);
            break;
        }
        
        case OP_DIV: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            if (b == 0) {
                fprintf(stderr, "Division by zero\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            qrun_push_value(vm, a / b);
            break;
        }
        
        case OP_MOD: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            if (b == 0) {
                fprintf(stderr, "Modulo by zero\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            qrun_push_value(vm, a % b);
            break;
        }
        
        case OP_NEG: {
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, -a);
            break;
        }

        case OP_BAND:
        case OP_BOR:
        case OP_BXOR:
        case OP_SHL:
        case OP_SHR:
        case OP_USHR: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_value_t result;
            if (op == OP_BAND) result = a & b;
            else if (op == OP_BOR) result = a | b;
            else if (op == OP_BXOR) result = a ^ b;
            else if (op == OP_SHL) result = a << b;
            else if (op == OP_SHR) result = a >> b;
            else result = (qrun_value_t)((uint32_t)a >> b);
            qrun_push_value(vm, result);
            break;
        }

        case OP_NOTBIT: {
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, ~a);
            break;
        }
        
        case OP_CMPEQ: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a == b) ? 1 : 0);
            break;
        }
        
        case OP_CMPNE: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a != b) ? 1 : 0);
            break;
        }
        
        case OP_CMPLT: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a < b) ? 1 : 0);
            break;
        }
        
        case OP_CMPLE: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a <= b) ? 1 : 0);
            break;
        }
        
        case OP_CMPGT: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a > b) ? 1 : 0);
            break;
        }
        
        case OP_CMPGE: {
            qrun_value_t b = qrun_pop_value(vm);
            qrun_value_t a = qrun_pop_value(vm);
            qrun_push_value(vm, (a >= b) ? 1 : 0);
            break;
        }
        
        case OP_CMPULT:
        case OP_CMPUGE:
        case OP_CMPULE:
        case OP_CMPUGT:
            /* Unsigned comparisons - treat as signed for now */
            fprintf(stderr, "Unsigned comparisons not yet implemented\n");
            vm->state[QRUN_HALTED] = 1;
            break;
        
        case OP_PRINT: {
            qrun_value_t v = qrun_pop_value(vm);
#ifdef QRUN_OS9
            putint(v);
            putchar(10);
#else
            printf("%d\n", v);
#endif
            break;
        }
        
        case OP_PRINTC: {
            qrun_value_t v = qrun_pop_value(vm);
#ifdef QRUN_OS9
            putchar((char)(v & 0xFF));
#else
            printf("%c", (char)(v & 0xFF));
#endif
            break;
        }
        
        case OP_RET: {
            qrun_value_t ret_val = qrun_pop_value(vm);
            
            if (vm->state[QRUN_FP] == 0) {
                /* Exiting main */
                vm->state[QRUN_EXIT_CODE] = ret_val;
                vm->state[QRUN_HALTED] = 1;
            } else {
                /* Return from function */
                vm->state[QRUN_FP]--;
                size_t ret_addr = frames[vm->state[QRUN_FP]].code_addr;
                free(frames[vm->state[QRUN_FP]].locals);
                
                vm->state[QRUN_PC] = ret_addr;
                qrun_push_value(vm, ret_val);
                should_increment = 0;  /* PC already set */
            }
            break;
        }
        
        case OP_FUNC: {
            /* Skip - just used as marker */
            break;
        }
        
        case OP_ENDFUNC: {
            /* Skip - just used as marker */
            break;
        }
        
        case OP_CALL: {
            /* Find function */
            const char* fname = instr->arg_s;
            int nargs = instr->arg_nargs;
            
            size_t func_idx = 0;
            int found = 0;
            
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NFUNCS]; i++) {
                if (funcs[i].name && strcmp(funcs[i].name, fname) == 0) {
                    func_idx = i;
                    found = 1;
                    break;
                }
            }
        }
            
            if (!found) {
                fprintf(stderr, "Error: function '%s' not found\n", fname);
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Pop arguments from stack */
            qrun_value_t* args = malloc(nargs * sizeof(qrun_value_t));
            { int i;
        for (i = nargs - 1; i >= 0; i--) {
                args[i] = qrun_pop_value(vm);
            }
        }
            
            /* Push new frame */
            if (vm->state[QRUN_FP] >= vm->state[QRUN_FRAME_SIZE]) {
                fprintf(stderr, "Call stack overflow\n");
                vm->state[QRUN_HALTED] = 1;
                free(args);
                break;
            }
            
            frames[vm->state[QRUN_FP]].code_addr = vm->state[QRUN_PC] + 1;  /* +1 because PC is already incremented in the loop */
            frames[vm->state[QRUN_FP]].nlocals_allocated = 256;  /* Allocate max slots */
            frames[vm->state[QRUN_FP]].locals = calloc(256, sizeof(qrun_value_t));
            
            /* Copy arguments to local slots 0..nargs-1 */
            { int i;
        for (i = 0; i < nargs; i++) {
                frames[vm->state[QRUN_FP]].locals[i] = args[i];
            }
        }
            
            vm->state[QRUN_FP]++;
            free(args);
            
            /* Jump to function */
            vm->state[QRUN_PC] = funcs[func_idx].addr + 1;  /* Skip FUNC opcode */
            should_increment = 0;  /* PC already set */
            break;
        }
        
        case OP_LABEL:
            /* Labels are no-ops at runtime (used for lookup) */
            break;
        
        case OP_JMP: {
            const char* label_name = instr->arg_s;
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NLABELS]; i++) {
                if (labels[i].name && strcmp(labels[i].name, label_name) == 0) {
                    vm->state[QRUN_PC] = labels[i].addr;
                    should_increment = 0;
                    break;
                }
            }
        }
            if (should_increment) {  /* Label not found */
                fprintf(stderr, "Error: label '%s' not found\n", label_name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_BEQ: {
            qrun_value_t val = qrun_pop_value(vm);
            if (val == 0) {
                const char* label_name = instr->arg_s;
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NLABELS]; i++) {
                    if (labels[i].name && strcmp(labels[i].name, label_name) == 0) {
                        vm->state[QRUN_PC] = labels[i].addr;
                        should_increment = 0;
                        break;
                    }
                }
        }
                if (should_increment) {  /* Label not found */
                    fprintf(stderr, "Error: label '%s' not found\n", label_name);
                    vm->state[QRUN_HALTED] = 1;
                }
            }
            break;
        }
        
        case OP_BNE: {
            qrun_value_t val = qrun_pop_value(vm);
            if (val != 0) {
                const char* label_name = instr->arg_s;
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NLABELS]; i++) {
                    if (labels[i].name && strcmp(labels[i].name, label_name) == 0) {
                        vm->state[QRUN_PC] = labels[i].addr;
                        should_increment = 0;
                        break;
                    }
                }
        }
                if (should_increment) {  /* Label not found */
                    fprintf(stderr, "Error: label '%s' not found\n", label_name);
                    vm->state[QRUN_HALTED] = 1;
                }
            }
            break;
        }
        
        case OP_LARRAY: {
            int slot = instr->arg_array_slot;
            const char* type = instr->arg_type;
            size_t size = instr->arg_array_size;
            int tsize = qrun_type_size(type);
            
            if (vm->state[QRUN_FP] == 0) {
                fprintf(stderr, "LARRAY: no active frame\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            size_t frame_idx = vm->state[QRUN_FP] - 1;
            qrun_array_t* arrays = frames[frame_idx].arrays;
            
            /* Expand arrays if needed */
            if (slot >= (int)frames[frame_idx].narrays) {
                size_t new_size = slot + 1;
                arrays = realloc(arrays, new_size * sizeof(qrun_array_t));
                { size_t i;
        for (i = frames[frame_idx].narrays; i < new_size; i++) {
                    arrays[i].data = NULL;
                    arrays[i].size = 0;
                    arrays[i].type_size = 0;
                }
        }
                frames[frame_idx].arrays = arrays;
                frames[frame_idx].narrays = new_size;
            }
            
            /* Allocate array on heap instead of via calloc
               This allows PUSHADDR L slot to return a valid heap pointer
               and STOREIND/LOADIND to work with local arrays */
            if (vm->state[QRUN_HEAP_SIZE] + size > vm->state[QRUN_HEAP_CAPACITY]) {
                fprintf(stderr, "LARRAY: heap overflow (need %d, have %d)\n", 
                        vm->state[QRUN_HEAP_SIZE] + size, vm->state[QRUN_HEAP_CAPACITY]);
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Allocate space on heap and zero-initialize */
            size_t heap_start = vm->state[QRUN_HEAP_SIZE];
            { size_t i;
        for (i = 0; i < size; i++) {
                vm->heap[vm->state[QRUN_HEAP_SIZE]++] = 0;
            }
        }
            
            /* Point array.data to this heap space */
            arrays[slot].data = vm->heap + heap_start;
            arrays[slot].size = size;
            arrays[slot].type_size = tsize;
            break;
        }
        
        case OP_GARRAY:
            /* Global arrays are allocated during IR load - skip at runtime */
            break;
        
        case OP_LOADIDX: {
            qrun_value_t index = qrun_pop_value(vm);
            int is_local = (instr->arg_array_size != 0);  /* Repurposed: size=1 means local */
            
            if (is_local) {
                int slot = instr->arg_array_slot;
                if (vm->state[QRUN_FP] == 0) {
                    fprintf(stderr, "LOADIDX: no active frame\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                size_t frame_idx = vm->state[QRUN_FP] - 1;
    qrun_array_t* local_arrays = frames[frame_idx].arrays;
                if (slot < 0 || slot >= (int)frames[frame_idx].narrays) {
                    fprintf(stderr, "LOADIDX: array slot %d out of range\n", slot);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)local_arrays[slot].size) {
                    fprintf(stderr, "LOADIDX: array index out of bounds\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                qrun_push_value(vm, local_arrays[slot].data[index]);
            } else {
                /* Global array */
                const char* name = instr->arg_s;
                int found = -1;
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
                    if (garrays[i].name && strcmp(garrays[i].name, name) == 0) {
                        found = i;
                        break;
                    }
                }
        }
                
                if (found < 0) {
                    fprintf(stderr, "LOADIDX: global array '%s' not found\n", name);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)garrays[found].size) {
                    fprintf(stderr, "LOADIDX: array index out of bounds\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                qrun_push_value(vm, garrays[found].data[index]);
            }
            break;
        }
        
        case OP_STOREIDX: {
            qrun_value_t value = qrun_pop_value(vm);
            qrun_value_t index = qrun_pop_value(vm);
            int is_local = (instr->arg_array_size != 0);
            
            if (is_local) {
                int slot = instr->arg_array_slot;
                
                if (vm->state[QRUN_FP] == 0) {
                    fprintf(stderr, "STOREIDX: no active frame\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                size_t frame_idx = vm->state[QRUN_FP] - 1;
    qrun_array_t* local_arrays = frames[frame_idx].arrays;
                if (slot < 0 || slot >= (int)frames[frame_idx].narrays) {
                    fprintf(stderr, "STOREIDX: array slot %d out of range\n", slot);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)local_arrays[slot].size) {
                    fprintf(stderr, "STOREIDX: array index out of bounds\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                local_arrays[slot].data[index] = value;
            } else {
                /* Global array */
                const char* name = instr->arg_s;
                int found = -1;
                
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
                    if (garrays[i].name && strcmp(garrays[i].name, name) == 0) {
                        found = i;
                        break;
                    }
                }
        }
                
                if (found < 0) {
                    fprintf(stderr, "STOREIDX: global array '%s' not found\n", name);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)garrays[found].size) {
                    fprintf(stderr, "STOREIDX: array index out of bounds\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                garrays[found].data[index] = value;
            }
            break;
        }
        
        case OP_ADDRL: {
            /* Allocate heap space for taking address of local, push address
               Encoding: block_id=2 (pointer-to-locals)
            */
            int slot = instr->arg_i;
            if (vm->state[QRUN_FP] == 0) {
                fprintf(stderr, "ADDRL: no active frame\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Load local value, put on heap */
            qrun_value_t val = qrun_load_local(vm, slot);
            if (vm->state[QRUN_HEAP_SIZE] >= vm->state[QRUN_HEAP_CAPACITY]) {
                fprintf(stderr, "Heap overflow\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            size_t heap_idx = vm->state[QRUN_HEAP_SIZE];
            vm->heap[vm->state[QRUN_HEAP_SIZE]] = val;
            vm->state[QRUN_HEAP_SIZE]++;
            
            /* block_id=2 for ADDRL pointers */
            qrun_value_t addr = -(2 * 100000 + heap_idx + 1000);
            qrun_push_value(vm, addr);
            break;
        }
        
        case OP_ADDRG: {
            /* Push address of global named variable (searches both named_globals and garrays) */
            const char* name = instr->arg_s;
            int found = 0;
            
            /* Search global arrays first */
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
                if (garrays[i].name && strcmp(garrays[i].name, name) == 0) {
                    /* Create handle pointer for global array */
                    if (vm->state[QRUN_NGARRAY_HANDLES] >= vm->state[QRUN_GARRAY_HANDLES_CAPACITY]) {
                        size_t new_capacity = vm->state[QRUN_GARRAY_HANDLES_CAPACITY] * 2;
                        int* new_indices = realloc(
                            vm->garray_handle_indices, new_capacity * sizeof(int));
                        if (!new_indices) {
                            fprintf(stderr, "ADDRG: cannot grow global array handles\n");
                            vm->state[QRUN_HALTED] = 1;
                            found = 1;
                            break;
                        }
                        vm->garray_handle_indices = new_indices;
                        qrun_value_t** new_handles = realloc(
                            vm->garray_handles, new_capacity * QRUN_PTR_BYTES);
                        if (!new_handles) {
                            /* The index array may have grown, but its old
                               capacity remains a valid bound for both. */
                            fprintf(stderr, "ADDRG: cannot grow global array handles\n");
                            vm->state[QRUN_HALTED] = 1;
                            found = 1;
                            break;
                        }
                        vm->garray_handles = new_handles;
                        handles = new_handles;
                        vm->garray_handle_indices = new_indices;
                        vm->state[QRUN_GARRAY_HANDLES_CAPACITY] = new_capacity;
                    }
                    handles[vm->state[QRUN_NGARRAY_HANDLES]] = garrays[i].data;
                    vm->garray_handle_indices[vm->state[QRUN_NGARRAY_HANDLES]] = (int)i;  /* Store garray_idx */
                    int handle_idx = (int)vm->state[QRUN_NGARRAY_HANDLES];
                    vm->state[QRUN_NGARRAY_HANDLES]++;
                    
                    /* Encode as: block_id = 256 + handle_idx, offset = 0 */
                    qrun_value_t addr = -((256 + handle_idx) * 100000 + 1000);
                    qrun_push_value(vm, addr);
                    found = 1;
                    break;
                }
            }
        }
            
            /* If not found in garrays, search named globals */
            if (!found) {
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NGLOBALS]; i++) {
                    if (named_globals[i].name && strcmp(named_globals[i].name, name) == 0) {
                        qrun_value_t addr = -(2000 + i);
                        qrun_push_value(vm, addr);
                        found = 1;
                        break;
                    }
                }
        }
            }
            
            if (!found) {
                fprintf(stderr, "ADDRG: global '%s' not found\n", name);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_LOADP: {
            /* Pop pointer-slot index, load pointer value from that local slot */
            int slot = instr->arg_i;
            if (vm->state[QRUN_FP] == 0) {
                fprintf(stderr, "LOADP: no active frame\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            qrun_value_t ptr = qrun_load_local(vm, slot);
            qrun_push_value(vm, ptr);
            break;
        }
        
        case OP_STOREP: {
            /* Pop pointer value, store in local slot */
            qrun_value_t ptr = qrun_pop_value(vm);
            int slot = instr->arg_i;
            if (vm->state[QRUN_FP] == 0) {
                fprintf(stderr, "STOREP: no active frame\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            qrun_store_local(vm, slot, ptr);
            break;
        }
        
        case OP_LOADIND: {
            /* Pop address, load value from heap, push value */
            qrun_value_t addr_val = qrun_pop_value(vm);
            
            if (addr_val >= 0) {
                fprintf(stderr, "LOADIND: invalid pointer (not negative)\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Decode pointer: addr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -addr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            if (block_id == 0) {
                /* Local array access - offset is into heap (from LARRAY) */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->state[QRUN_HEAP_SIZE]) {
                    fprintf(stderr, "LOADIND: heap pointer out of bounds (idx=%d, heap_size=%d)\n", 
                            heap_idx, vm->state[QRUN_HEAP_SIZE]);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                qrun_push_value(vm, vm->heap[heap_idx]);
            } else if (block_id >= 256) {
                /* Global array access via handle pointer
                   block_id = 256 + handle_idx, offset may be non-zero from PADD */
                int handle_idx = block_id - 256;
                if (handle_idx < 0 || handle_idx >= (int)vm->state[QRUN_NGARRAY_HANDLES]) {
                    fprintf(stderr, "LOADIND: global array handle out of range (handle_idx=%d)\n", handle_idx);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                qrun_value_t* base_ptr = handles[handle_idx];
                if (!base_ptr) {
                    fprintf(stderr, "LOADIND: global array handle is NULL\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                int elem_size = qrun_type_size(instr->arg_type);
                qrun_value_t value = base_ptr[offset / elem_size];
                qrun_push_value(vm, value);
            } else if (block_id == 2) {
                /* Pointer to local (from ADDRL) - offset is heap index */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->state[QRUN_HEAP_SIZE]) {
                    fprintf(stderr, "LOADIND: ADDRL pointer out of bounds (idx=%d)\n", heap_idx);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                qrun_push_value(vm, vm->heap[heap_idx]);
            } else {
                fprintf(stderr, "LOADIND: unknown block_id %d\n", block_id);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_STOREIND: {
            /* Pop value, pop address, store value to heap at address */
            qrun_value_t value = qrun_pop_value(vm);
            qrun_value_t addr_val = qrun_pop_value(vm);
            
            if (addr_val >= 0) {
                fprintf(stderr, "STOREIND: invalid pointer (not negative)\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Decode pointer: addr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -addr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            if (block_id == 0) {
                /* Local array access - offset is into heap (from LARRAY) */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->state[QRUN_HEAP_SIZE]) {
                    fprintf(stderr, "STOREIND: heap pointer out of bounds (idx=%d)\n", heap_idx);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                vm->heap[heap_idx] = value;
            } else if (block_id >= 256) {
                /* Global array access via handle pointer
                   block_id = 256 + handle_idx, offset may be non-zero from PADD */
                int handle_idx = block_id - 256;
                if (handle_idx < 0 || handle_idx >= (int)vm->state[QRUN_NGARRAY_HANDLES]) {
                    fprintf(stderr, "STOREIND: global array handle out of range (handle_idx=%d)\n", handle_idx);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                qrun_value_t* base_ptr = handles[handle_idx];
                if (!base_ptr) {
                    fprintf(stderr, "STOREIND: global array handle is NULL\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                int elem_size = qrun_type_size(instr->arg_type);
                base_ptr[offset / elem_size] = value;
            } else if (block_id == 2) {
                /* Pointer to local (from ADDRL) - offset is heap index */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->state[QRUN_HEAP_SIZE]) {
                    fprintf(stderr, "STOREIND: ADDRL pointer out of bounds (idx=%d)\n", heap_idx);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                vm->heap[heap_idx] = value;
            } else {
                fprintf(stderr, "STOREIND: unknown block_id %d\n", block_id);
                vm->state[QRUN_HALTED] = 1;
            }
            break;
        }
        
        case OP_IPADD:
        case OP_PTRINDEX: {
            /* Pop pointer, pop integer, push (ptr + int*size) */
            qrun_value_t ptr_val = qrun_pop_value(vm);
            qrun_value_t int_val = qrun_pop_value(vm);
            
            if (ptr_val >= 0) {
                fprintf(stderr, "IPADD: pointer operand required\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Decode pointer: ptr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -ptr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            /* Get type size from instruction argument */
            int tsize = qrun_type_size(instr->arg_type);
            offset += (int_val * tsize);
            
            /* Re-encode with new offset */
            qrun_value_t result = -(block_id * 100000 + offset + 1000);
            qrun_push_value(vm, result);
            break;
        }
        
        case OP_IPADDN: {
            /* Pop pointer, pop integer, push (ptr + int*runtime_size)
               IPADDN <size>: size is provided as instruction argument (struct byte size) */
            qrun_value_t ptr_val = qrun_pop_value(vm);
            qrun_value_t int_val = qrun_pop_value(vm);
            int runtime_size = instr->arg_i;
            
            if (ptr_val >= 0) {
                fprintf(stderr, "IPADDN: pointer operand required\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Decode pointer: ptr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -ptr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            /* Add runtime size scaled by count */
            offset += (int_val * runtime_size);
            
            /* Re-encode with new offset */
            qrun_value_t result = -(block_id * 100000 + offset + 1000);
            qrun_push_value(vm, result);
            break;
        }
        
        case OP_PADD: {
            /* Pop integer, pop pointer, push (ptr + int*size)
               Stack order: [..., ptr, int] -> pop int, pop ptr
               Similar to IPADD but for pointer+pointer (encoded as ptr+int offset) */
            qrun_value_t int_val = qrun_pop_value(vm);
            qrun_value_t ptr_val = qrun_pop_value(vm);
            
            if (ptr_val >= 0) {
                fprintf(stderr, "PADD: pointer operand required\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Decode pointer: ptr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -ptr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            /* Get type size from instruction argument */
            int tsize = qrun_type_size(instr->arg_type);
            offset += (int_val * tsize);
            
            /* Re-encode with new offset */
            qrun_value_t result = -(block_id * 100000 + offset + 1000);
            qrun_push_value(vm, result);
            break;
        }
        
        case OP_PCMPNE: {
            /* Pop two pointers, compare for inequality */
            qrun_value_t ptr2 = qrun_pop_value(vm);
            qrun_value_t ptr1 = qrun_pop_value(vm);
            
            if (ptr1 >= 0 || ptr2 >= 0) {
                fprintf(stderr, "PCMPNE: pointer operands required\n");
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            qrun_push_value(vm, (ptr1 != ptr2) ? 1 : 0);
            break;
        }
        
        case OP_PUSHADDR: {
            /* PUSHADDR <scope> <name/slot>: push address of local or global
               Encoding: -(block_id*100000 + offset + 1000)
               Local arrays: block_id=0, offset=heap_start (index into heap)
               Global arrays: block_id=1, offset=global_idx
            */
            int is_local = (instr->arg_array_size == 1);  /* size=1 means local, 0 means global */
            
            if (is_local) {
                int slot = instr->arg_array_slot;
                if (slot < 0 || slot >= 256) {
                    fprintf(stderr, "PUSHADDR: invalid local slot %d\n", slot);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                if (vm->state[QRUN_FP] == 0) {
                    fprintf(stderr, "PUSHADDR: no active frame\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                size_t frame_idx = vm->state[QRUN_FP] - 1;
    qrun_array_t* local_arrays = frames[frame_idx].arrays;
                if (slot >= (int)frames[frame_idx].narrays) {
                    fprintf(stderr, "PUSHADDR: local slot %d not allocated\n", slot);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                /* Get the heap index from array.data pointer
                   heap_start = arrays[slot].data - vm->heap */
                qrun_value_t* array_ptr = local_arrays[slot].data;
                if (array_ptr == NULL) {
                    fprintf(stderr, "PUSHADDR: local array at slot %d is NULL\n", slot);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                int heap_start = array_ptr - vm->heap;
                if (heap_start < 0 || heap_start >= (int)vm->state[QRUN_HEAP_SIZE]) {
                    fprintf(stderr, "PUSHADDR: local array pointer out of heap range\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                qrun_value_t addr = -(0 * 100000 + heap_start + 1000);  /* block_id=0, offset=heap_start */
                qrun_push_value(vm, addr);
            } else {
                /* Address of global array */
                const char* name = instr->arg_s;
                if (!name) {
                    fprintf(stderr, "PUSHADDR: missing global name\n");
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                /* Look up in global arrays */
                int global_idx = -1;
                { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
                    if (garrays[i].name && strcmp(garrays[i].name, name) == 0) {
                        global_idx = i;
                        break;
                    }
                }
        }
                
                if (global_idx == -1) {
                    fprintf(stderr, "PUSHADDR: global '%s' not found\n", name);
                    vm->state[QRUN_HALTED] = 1;
                    break;
                }
                
                /* Create a handle for this global array's data pointer
                   This allows pointer arithmetic to work correctly */
                if (vm->state[QRUN_NGARRAY_HANDLES] >= vm->state[QRUN_GARRAY_HANDLES_CAPACITY]) {
                    vm->state[QRUN_GARRAY_HANDLES_CAPACITY] *= 2;
                    vm->garray_handles = realloc(vm->garray_handles, 
                                                  vm->state[QRUN_GARRAY_HANDLES_CAPACITY] * QRUN_PTR_BYTES);
                    handles = vm->garray_handles;
                }
                
                size_t handle_idx = vm->state[QRUN_NGARRAY_HANDLES];
                handles[handle_idx] = garrays[global_idx].data;
                vm->state[QRUN_NGARRAY_HANDLES]++;
                
                /* Encoding: use block_id = 256 + handle_idx to encode handle in block_id
                   This way PADD can modify offset and we still know which array it is */
                int block_id = 256 + (int)handle_idx;
                qrun_value_t addr = -(block_id * 100000 + 0 + 1000);  /* offset=0 initially */
                qrun_push_value(vm, addr);
            }
            break;
        }
        
        case OP_GLOBAL:
            /* GLOBAL <name> [init] - Global variable declaration (no-op at runtime) */
            break;
        
        case OP_GINIT: {
            /* GINIT <name> <index> <value> - Initialize global array element */
            const char* garray_name = instr->arg_s;
            int index = instr->arg_ginit_index;
            int32_t value = instr->arg_ginit_value;
            
            /* Find global array by name */
            int garray_idx = -1;
            { size_t i;
        for (i = 0; i < vm->state[QRUN_NGARRAYS]; i++) {
                if (garrays[i].name && strcmp(garrays[i].name, garray_name) == 0) {
                    garray_idx = (int)i;
                    break;
                }
            }
        }
            
            if (garray_idx < 0) {
                fprintf(stderr, "GINIT: global array '%s' not found\n", garray_name);
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            if (index < 0 || index >= (int)garrays[garray_idx].size) {
                fprintf(stderr, "GINIT: array index %d out of bounds for '%s' (size %zu)\n", 
                        index, garray_name, garrays[garray_idx].size);
                vm->state[QRUN_HALTED] = 1;
                break;
            }
            
            /* Store value in array at byte offset
               index is element number, convert to byte offset
               Cast to char* for byte-level access */
            int tsize = garrays[garray_idx].type_size;
            char* byte_ptr = (char*)garrays[garray_idx].data;
            byte_ptr[index * tsize] = (unsigned char)value;
            break;
        }
        
        case OP_GLOBALDECL:
            /* GLOBALDECL - external global declaration (no-op at runtime) */
            /* Used for multi-file linking declarations */
            break;
        
        case OP_FUNCDECL:
            /* FUNCDECL - external function declaration (no-op at runtime) */
            /* Used for multi-file linking declarations */
            break;
        
        case OP_HALT:
            vm->state[QRUN_HALTED] = 1;
            break;
        
        default:
            fprintf(stderr, "Unknown opcode: %d\n", op);
            vm->state[QRUN_HALTED] = 1;
            break;
        }
        
        /* Increment PC unless a jump set it explicitly */
        if (should_increment) {
            vm->state[QRUN_PC]++;
        }
    }
    
    return vm->state[QRUN_EXIT_CODE];
}
