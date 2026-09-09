#include "qrun_vm.h"
#include "qrun_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * VM Creation & Initialization
 * ========================================================================= */

qrun_vm_t* qrun_vm_create(void)
{
    qrun_vm_t* vm = malloc(sizeof(*vm));
    if (!vm) return NULL;
    
    memset(vm, 0, sizeof(*vm));
    
    /* Allocate stack */
    vm->stack = malloc(4096 * sizeof(qrun_stackval_t));
    vm->stack_size = 4096;
    vm->sp = 0;
    
    /* Allocate globals */
    vm->globals = malloc(1024 * sizeof(qrun_value_t));
    vm->globals_size = 1024;
    
    /* Allocate named globals (for __ptrsize, etc.) */
    vm->named_globals = malloc(64 * sizeof(vm->named_globals[0]));
    vm->nglobals = 0;
    vm->nglobals_capacity = 64;
    
    /* Allocate call frames */
    vm->frames = malloc(256 * sizeof(vm->frames[0]));
    vm->frame_size = 256;
    vm->nfuncs = 0;
    vm->funcs = malloc(256 * sizeof(vm->funcs[0]));
    memset(vm->funcs, 0, 256 * sizeof(vm->funcs[0]));
    vm->fp = 0;
    
    /* Initialize frames (clear arrays pointers) */
    for (int i = 0; i < 256; i++) {
        vm->frames[i].arrays = NULL;
        vm->frames[i].narrays = 0;
    }
    
    /* Allocate labels */
    vm->labels = malloc(512 * sizeof(vm->labels[0]));
    vm->nlabels = 0;
    vm->labels_capacity = 512;
    
    /* Allocate pointer heap */
    vm->heap = malloc(256 * sizeof(qrun_value_t));
    vm->heap_size = 0;
    vm->heap_capacity = 256;
    
    /* Allocate global arrays */
    vm->garrays = malloc(64 * sizeof(vm->garrays[0]));
    vm->ngarrays = 0;
    vm->garrays_capacity = 64;
    
    vm->string_pool = NULL;  /* Will be set by qrun_vm_load_ir */
    
    vm->halted = 0;
    vm->exit_code = 0;
    
    return vm;
}

void qrun_vm_destroy(qrun_vm_t* vm)
{
    if (!vm) return;
    
    free(vm->code);
    free(vm->stack);
    free(vm->globals);
    free(vm->named_globals);
    free(vm->heap);
    
    /* Destroy frame local arrays */
    for (size_t i = 0; i < vm->fp; i++) {
        free(vm->frames[i].locals);
        if (vm->frames[i].arrays) {
            for (size_t j = 0; j < vm->frames[i].narrays; j++) {
                free(vm->frames[i].arrays[j].data);
            }
            free(vm->frames[i].arrays);
        }
    }
    free(vm->frames);
    free(vm->funcs);
    free(vm->labels);
    
    /* Destroy global arrays */
    for (size_t i = 0; i < vm->ngarrays; i++) {
        free(vm->garrays[i].data);
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
    if (!vm || !filename) return -1;
    
    if (qrun_ir_parse(filename, &vm->code, &vm->code_size, &vm->string_pool) != 0) {
        return -1;
    }
    
    /* Build function, label, and global array lookup tables */
    for (size_t i = 0; i < vm->code_size; i++) {
        if (vm->code[i].op == OP_FUNC) {
            if (vm->nfuncs >= 256) {
                fprintf(stderr, "Too many functions\n");
                return -1;
            }
            vm->funcs[vm->nfuncs].name = vm->code[i].arg.func.name;
            vm->funcs[vm->nfuncs].addr = i;
            vm->funcs[vm->nfuncs].nargs = vm->code[i].arg.func.nargs;
            vm->funcs[vm->nfuncs].nlocals = vm->code[i].arg.func.nlocals;
            vm->nfuncs++;
        } else if (vm->code[i].op == OP_LABEL) {
            if (vm->nlabels >= vm->labels_capacity) {
                fprintf(stderr, "Too many labels\n");
                return -1;
            }
            vm->labels[vm->nlabels].name = vm->code[i].arg.s;
            vm->labels[vm->nlabels].addr = i;
            vm->nlabels++;
        } else if (vm->code[i].op == OP_GARRAY) {
            /* Allocate global arrays at load time */
            const char* name = vm->code[i].arg.array.name;
            const char* type = vm->code[i].arg.array.type;
            size_t size = vm->code[i].arg.array.size;
            int tsize = qrun_type_size(type);
            
            if (vm->ngarrays >= vm->garrays_capacity) {
                fprintf(stderr, "Too many global arrays\n");
                return -1;
            }
            
            vm->garrays[vm->ngarrays].name = (char*)name;
            vm->garrays[vm->ngarrays].data = calloc(size, sizeof(qrun_value_t));
            vm->garrays[vm->ngarrays].size = size;
            vm->garrays[vm->ngarrays].type_size = tsize;
            vm->ngarrays++;
        }
    }
    
    return 0;
}

/* =========================================================================
 * VM Execution - Stack & Memory
 * ========================================================================= */

static void qrun_push_value(qrun_vm_t* vm, qrun_value_t v)
{
    if (vm->sp >= vm->stack_size) {
        fprintf(stderr, "Stack overflow\n");
        vm->halted = 1;
        return;
    }
    vm->stack[vm->sp].val = v;
    vm->sp++;
}

static qrun_value_t qrun_pop_value(qrun_vm_t* vm)
{
    if (vm->sp == 0) {
        fprintf(stderr, "Stack underflow\n");
        vm->halted = 1;
        return 0;
    }
    vm->sp--;
    return vm->stack[vm->sp].val;
}

static qrun_value_t qrun_peek_value(qrun_vm_t* vm)
{
    if (vm->sp == 0) return 0;
    return vm->stack[vm->sp - 1].val;
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
    if (vm->fp == 0) {
        fprintf(stderr, "No active frame\n");
        return 0;
    }
    
    size_t frame_idx = vm->fp - 1;
    if (slot < 0 || slot >= (int)vm->frames[frame_idx].nlocals_allocated) {
        fprintf(stderr, "Local slot out of bounds: %d\n", slot);
        return 0;
    }
    
    return vm->frames[frame_idx].locals[slot];
}

static void qrun_store_local(qrun_vm_t* vm, int slot, qrun_value_t val)
{
    if (vm->fp == 0) {
        fprintf(stderr, "No active frame\n");
        return;
    }
    
    size_t frame_idx = vm->fp - 1;
    if (slot < 0 || slot >= (int)vm->frames[frame_idx].nlocals_allocated) {
        fprintf(stderr, "Local slot out of bounds: %d\n", slot);
        return;
    }
    
    vm->frames[frame_idx].locals[slot] = val;
}

/* =========================================================================
 * VM Execution - Main Loop
 * ========================================================================= */

int qrun_vm_run(qrun_vm_t* vm)
{
    if (!vm || !vm->code) {
        fprintf(stderr, "VM not initialized\n");
        return -1;
    }
    
    /* Find main() function */
    size_t main_addr = 0;
    int found_main = 0;
    
    for (size_t i = 0; i < vm->nfuncs; i++) {
        if (vm->funcs[i].name && strcmp(vm->funcs[i].name, "main") == 0) {
            main_addr = vm->funcs[i].addr;
            found_main = 1;
            break;
        }
    }
    
    if (!found_main) {
        fprintf(stderr, "Error: main() not found\n");
        return -1;
    }
    
    vm->pc = main_addr;
    vm->halted = 0;
    
    /* Push initial frame for main() */
    vm->frames[0].code_addr = vm->code_size;  /* Return address (end of program) */
    vm->frames[0].nlocals_allocated = 256;
    vm->frames[0].locals = calloc(256, sizeof(qrun_value_t));
    vm->fp = 1;
    
    while (!vm->halted && vm->pc < vm->code_size) {
        qrun_instruction_t* instr = &vm->code[vm->pc];
        qrun_opcode_t op = instr->op;
        
        /* Decode & Execute */
        int should_increment = 1;  /* Most opcodes auto-increment, jumps set to 0 */
        
        switch (op) {
        case OP_PUSH:
            qrun_push_value(vm, instr->arg.i);
            break;
        
        case OP_DUP: {
            qrun_value_t v = qrun_peek_value(vm);
            qrun_push_value(vm, v);
            break;
        }
        
        case OP_SWAP: {
            if (vm->sp < 2) {
                fprintf(stderr, "Stack underflow for SWAP\n");
                vm->halted = 1;
                break;
            }
            qrun_value_t a = qrun_pop_value(vm);
            qrun_value_t b = qrun_pop_value(vm);
            qrun_push_value(vm, a);
            qrun_push_value(vm, b);
            break;
        }
        
        case OP_LOADL: {
            qrun_value_t v = qrun_load_local(vm, instr->arg.i);
            qrun_push_value(vm, v);
            break;
        }
        
        case OP_STOREL: {
            qrun_value_t v = qrun_pop_value(vm);
            qrun_store_local(vm, instr->arg.i, v);
            break;
        }
        
        case OP_LOADG: {
            const char* name = instr->arg.s;
            qrun_value_t value = 0;
            
            /* Search named globals first (__ptrsize, etc.) */
            for (size_t i = 0; i < vm->nglobals; i++) {
                if (vm->named_globals[i].name && strcmp(vm->named_globals[i].name, name) == 0) {
                    value = vm->named_globals[i].value;
                    qrun_push_value(vm, value);
                    break;
                }
            }
            
            fprintf(stderr, "LOADG: '%s' not found in named globals\n", name);
            vm->halted = 1;
            break;
        }
        
        case OP_STOREG: {
            const char* name = instr->arg.s;
            qrun_value_t v = qrun_pop_value(vm);
            
            /* Search named globals */
            for (size_t i = 0; i < vm->nglobals; i++) {
                if (vm->named_globals[i].name && strcmp(vm->named_globals[i].name, name) == 0) {
                    vm->named_globals[i].value = v;
                    break;
                }
            }
            
            fprintf(stderr, "STOREG: '%s' not found in named globals\n", name);
            vm->halted = 1;
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
                vm->halted = 1;
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
                vm->halted = 1;
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
            vm->halted = 1;
            break;
        
        case OP_PRINT: {
            qrun_value_t v = qrun_pop_value(vm);
            printf("%d\n", v);
            break;
        }
        
        case OP_PRINTC: {
            qrun_value_t v = qrun_pop_value(vm);
            printf("%c", (char)(v & 0xFF));
            break;
        }
        
        case OP_RET: {
            qrun_value_t ret_val = qrun_pop_value(vm);
            
            if (vm->fp == 0) {
                /* Exiting main */
                vm->exit_code = ret_val;
                vm->halted = 1;
            } else {
                /* Return from function */
                vm->fp--;
                size_t ret_addr = vm->frames[vm->fp].code_addr;
                free(vm->frames[vm->fp].locals);
                
                vm->pc = ret_addr;
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
            const char* fname = instr->arg.call.name;
            int nargs = instr->arg.call.nargs;
            
            size_t func_idx = 0;
            int found = 0;
            
            for (size_t i = 0; i < vm->nfuncs; i++) {
                if (vm->funcs[i].name && strcmp(vm->funcs[i].name, fname) == 0) {
                    func_idx = i;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                fprintf(stderr, "Error: function '%s' not found\n", fname);
                vm->halted = 1;
                break;
            }
            
            /* Pop arguments from stack */
            qrun_value_t* args = malloc(nargs * sizeof(qrun_value_t));
            for (int i = nargs - 1; i >= 0; i--) {
                args[i] = qrun_pop_value(vm);
            }
            
            /* Push new frame */
            if (vm->fp >= vm->frame_size) {
                fprintf(stderr, "Call stack overflow\n");
                vm->halted = 1;
                free(args);
                break;
            }
            
            vm->frames[vm->fp].code_addr = vm->pc + 1;  /* +1 because PC is already incremented in the loop */
            vm->frames[vm->fp].nlocals_allocated = 256;  /* Allocate max slots */
            vm->frames[vm->fp].locals = calloc(256, sizeof(qrun_value_t));
            
            /* Copy arguments to local slots 0..nargs-1 */
            for (int i = 0; i < nargs; i++) {
                vm->frames[vm->fp].locals[i] = args[i];
            }
            
            vm->fp++;
            free(args);
            
            /* Jump to function */
            vm->pc = vm->funcs[func_idx].addr + 1;  /* Skip FUNC opcode */
            should_increment = 0;  /* PC already set */
            break;
        }
        
        case OP_LABEL:
            /* Labels are no-ops at runtime (used for lookup) */
            break;
        
        case OP_JMP: {
            const char* label_name = instr->arg.s;
            for (size_t i = 0; i < vm->nlabels; i++) {
                if (vm->labels[i].name && strcmp(vm->labels[i].name, label_name) == 0) {
                    vm->pc = vm->labels[i].addr;
                    should_increment = 0;
                    break;
                }
            }
            if (should_increment) {  /* Label not found */
                fprintf(stderr, "Error: label '%s' not found\n", label_name);
                vm->halted = 1;
            }
            break;
        }
        
        case OP_BEQ: {
            qrun_value_t val = qrun_pop_value(vm);
            if (val == 0) {
                const char* label_name = instr->arg.s;
                for (size_t i = 0; i < vm->nlabels; i++) {
                    if (vm->labels[i].name && strcmp(vm->labels[i].name, label_name) == 0) {
                        vm->pc = vm->labels[i].addr;
                        should_increment = 0;
                        break;
                    }
                }
                if (should_increment) {  /* Label not found */
                    fprintf(stderr, "Error: label '%s' not found\n", label_name);
                    vm->halted = 1;
                }
            }
            break;
        }
        
        case OP_BNE: {
            qrun_value_t val = qrun_pop_value(vm);
            if (val != 0) {
                const char* label_name = instr->arg.s;
                for (size_t i = 0; i < vm->nlabels; i++) {
                    if (vm->labels[i].name && strcmp(vm->labels[i].name, label_name) == 0) {
                        vm->pc = vm->labels[i].addr;
                        should_increment = 0;
                        break;
                    }
                }
                if (should_increment) {  /* Label not found */
                    fprintf(stderr, "Error: label '%s' not found\n", label_name);
                    vm->halted = 1;
                }
            }
            break;
        }
        
        case OP_LARRAY: {
            int slot = instr->arg.array.slot;
            const char* type = instr->arg.array.type;
            size_t size = instr->arg.array.size;
            int tsize = qrun_type_size(type);
            
            if (vm->fp == 0) {
                fprintf(stderr, "LARRAY: no active frame\n");
                vm->halted = 1;
                break;
            }
            
            size_t frame_idx = vm->fp - 1;
            struct {
                qrun_value_t* data;
                size_t size;
                int type_size;
            }* arrays = vm->frames[frame_idx].arrays;
            
            /* Expand arrays if needed */
            if (slot >= (int)vm->frames[frame_idx].narrays) {
                size_t new_size = slot + 1;
                arrays = realloc(arrays, new_size * sizeof(arrays[0]));
                for (size_t i = vm->frames[frame_idx].narrays; i < new_size; i++) {
                    arrays[i].data = NULL;
                    arrays[i].size = 0;
                    arrays[i].type_size = 0;
                }
                vm->frames[frame_idx].arrays = arrays;
                vm->frames[frame_idx].narrays = new_size;
            }
            
            /* Allocate array */
            arrays[slot].data = calloc(size, sizeof(qrun_value_t));
            arrays[slot].size = size;
            arrays[slot].type_size = tsize;
            break;
        }
        
        case OP_GARRAY:
            /* Global arrays are allocated during IR load - skip at runtime */
            break;
        
        case OP_LOADIDX: {
            qrun_value_t index = qrun_pop_value(vm);
            int is_local = (instr->arg.array.size != 0);  /* Repurposed: size=1 means local */
            
            if (is_local) {
                int slot = instr->arg.array.slot;
                if (vm->fp == 0) {
                    fprintf(stderr, "LOADIDX: no active frame\n");
                    vm->halted = 1;
                    break;
                }
                
                size_t frame_idx = vm->fp - 1;
                if (slot < 0 || slot >= (int)vm->frames[frame_idx].narrays) {
                    fprintf(stderr, "LOADIDX: array slot %d out of range\n", slot);
                    vm->halted = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)vm->frames[frame_idx].arrays[slot].size) {
                    fprintf(stderr, "LOADIDX: array index out of bounds\n");
                    vm->halted = 1;
                    break;
                }
                
                qrun_push_value(vm, vm->frames[frame_idx].arrays[slot].data[index]);
            } else {
                /* Global array */
                const char* name = instr->arg.array.name;
                int found = -1;
                for (size_t i = 0; i < vm->ngarrays; i++) {
                    if (vm->garrays[i].name && strcmp(vm->garrays[i].name, name) == 0) {
                        found = i;
                        break;
                    }
                }
                
                if (found < 0) {
                    fprintf(stderr, "LOADIDX: global array '%s' not found\n", name);
                    vm->halted = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)vm->garrays[found].size) {
                    fprintf(stderr, "LOADIDX: array index out of bounds\n");
                    vm->halted = 1;
                    break;
                }
                
                qrun_push_value(vm, vm->garrays[found].data[index]);
            }
            break;
        }
        
        case OP_STOREIDX: {
            qrun_value_t value = qrun_pop_value(vm);
            qrun_value_t index = qrun_pop_value(vm);
            int is_local = (instr->arg.array.size != 0);
            
            if (is_local) {
                int slot = instr->arg.array.slot;
                
                if (vm->fp == 0) {
                    fprintf(stderr, "STOREIDX: no active frame\n");
                    vm->halted = 1;
                    break;
                }
                
                size_t frame_idx = vm->fp - 1;
                if (slot < 0 || slot >= (int)vm->frames[frame_idx].narrays) {
                    fprintf(stderr, "STOREIDX: array slot %d out of range\n", slot);
                    vm->halted = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)vm->frames[frame_idx].arrays[slot].size) {
                    fprintf(stderr, "STOREIDX: array index out of bounds\n");
                    vm->halted = 1;
                    break;
                }
                
                vm->frames[frame_idx].arrays[slot].data[index] = value;
            } else {
                /* Global array */
                const char* name = instr->arg.array.name;
                int found = -1;
                
                for (size_t i = 0; i < vm->ngarrays; i++) {
                    if (vm->garrays[i].name && strcmp(vm->garrays[i].name, name) == 0) {
                        found = i;
                        break;
                    }
                }
                
                if (found < 0) {
                    fprintf(stderr, "STOREIDX: global array '%s' not found\n", name);
                    vm->halted = 1;
                    break;
                }
                
                if (index < 0 || index >= (int)vm->garrays[found].size) {
                    fprintf(stderr, "STOREIDX: array index out of bounds\n");
                    vm->halted = 1;
                    break;
                }
                
                vm->garrays[found].data[index] = value;
            }
            break;
        }
        
        case OP_ADDRL: {
            /* Allocate heap space, push negative address */
            int slot = instr->arg.i;
            if (vm->fp == 0) {
                fprintf(stderr, "ADDRL: no active frame\n");
                vm->halted = 1;
                break;
            }
            
            /* Load local value, put on heap */
            qrun_value_t val = qrun_load_local(vm, slot);
            if (vm->heap_size >= vm->heap_capacity) {
                fprintf(stderr, "Heap overflow\n");
                vm->halted = 1;
                break;
            }
            
            vm->heap[vm->heap_size] = val;
            qrun_value_t addr = -(vm->heap_size + 1000);  /* Negative address */
            vm->heap_size++;
            
            qrun_push_value(vm, addr);
            break;
        }
        
        case OP_ADDRG: {
            /* Push address of global named variable */
            const char* name = instr->arg.s;
            
            /* Search named globals */
            for (size_t i = 0; i < vm->nglobals; i++) {
                if (vm->named_globals[i].name && strcmp(vm->named_globals[i].name, name) == 0) {
                    qrun_value_t addr = -(2000 + i);
                    qrun_push_value(vm, addr);
                    return 0;
                }
            }
            
            fprintf(stderr, "ADDRG: global '%s' not found\n", name);
            vm->halted = 1;
            break;
        }
        
        case OP_LOADP: {
            /* Pop pointer-slot index, load pointer value from that local slot */
            int slot = instr->arg.i;
            if (vm->fp == 0) {
                fprintf(stderr, "LOADP: no active frame\n");
                vm->halted = 1;
                break;
            }
            
            qrun_value_t ptr = qrun_load_local(vm, slot);
            qrun_push_value(vm, ptr);
            break;
        }
        
        case OP_STOREP: {
            /* Pop pointer value, store in local slot */
            qrun_value_t ptr = qrun_pop_value(vm);
            int slot = instr->arg.i;
            if (vm->fp == 0) {
                fprintf(stderr, "STOREP: no active frame\n");
                vm->halted = 1;
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
                vm->halted = 1;
                break;
            }
            
            /* Decode pointer: addr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -addr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            if (block_id == 0) {
                /* Local array access - offset is into heap */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->heap_size) {
                    fprintf(stderr, "LOADIND: heap pointer out of bounds (idx=%d)\n", heap_idx);
                    vm->halted = 1;
                    break;
                }
                qrun_push_value(vm, vm->heap[heap_idx]);
            } else if (block_id == 1) {
                /* Global array access */
                int garr_idx = offset;
                if (garr_idx < 0 || garr_idx >= (int)vm->ngarrays) {
                    fprintf(stderr, "LOADIND: global array index out of range\n");
                    vm->halted = 1;
                    break;
                }
                if (vm->garrays[garr_idx].size > 0) {
                    qrun_push_value(vm, vm->garrays[garr_idx].data[0]);
                } else {
                    qrun_push_value(vm, 0);
                }
            }
            break;
        }
        
        case OP_STOREIND: {
            /* Pop value, pop address, store value to heap at address */
            qrun_value_t value = qrun_pop_value(vm);
            qrun_value_t addr_val = qrun_pop_value(vm);
            
            if (addr_val >= 0) {
                fprintf(stderr, "STOREIND: invalid pointer (not negative)\n");
                vm->halted = 1;
                break;
            }
            
            /* Decode pointer: addr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -addr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            if (block_id == 0) {
                /* Local array access - offset is into heap */
                int heap_idx = offset;
                if (heap_idx < 0 || heap_idx >= (int)vm->heap_size) {
                    fprintf(stderr, "STOREIND: heap pointer out of bounds (idx=%d)\n", heap_idx);
                    vm->halted = 1;
                    break;
                }
                vm->heap[heap_idx] = value;
            } else if (block_id == 1) {
                /* Global array access */
                int garr_idx = offset;
                if (garr_idx < 0 || garr_idx >= (int)vm->ngarrays) {
                    fprintf(stderr, "STOREIND: global array index out of range\n");
                    vm->halted = 1;
                    break;
                }
                /* offset into global array is stored in heap... but we don't have secondary offset */
                /* For now, assume offset=0 for global arrays */
                if (vm->garrays[garr_idx].size > 0) {
                    vm->garrays[garr_idx].data[0] = value;
                }
            }
            break;
        }
        
        case OP_IPADD: {
            /* Pop pointer, pop integer, push (ptr + int*size) */
            qrun_value_t ptr_val = qrun_pop_value(vm);
            qrun_value_t int_val = qrun_pop_value(vm);
            
            fprintf(stderr, "DEBUG IPADD: ptr_val=%d, int_val=%d\n", ptr_val, int_val);
            
            if (ptr_val >= 0) {
                fprintf(stderr, "IPADD: pointer operand required\n");
                vm->halted = 1;
                break;
            }
            
            /* Decode pointer: ptr_val = -(block_id*100000 + offset + 1000) */
            qrun_value_t encoded = -ptr_val - 1000;
            int block_id = encoded / 100000;
            int offset = encoded % 100000;
            
            /* Get type size from instruction argument */
            int tsize = qrun_type_size(instr->arg.array.type);
            offset += (int_val * tsize);
            
            /* Re-encode with new offset */
            qrun_value_t result = -(block_id * 100000 + offset + 1000);
            qrun_push_value(vm, result);
            break;
        }
        
        case OP_PADD: {
            /* Pop two pointers, push sum (for offset arithmetic) */
            qrun_value_t ptr2 = qrun_pop_value(vm);
            qrun_value_t ptr1 = qrun_pop_value(vm);
            
            if (ptr1 >= 0 || ptr2 >= 0) {
                fprintf(stderr, "PADD: pointer operands required\n");
                vm->halted = 1;
                break;
            }
            
            qrun_value_t result = ptr1 + ptr2;
            qrun_push_value(vm, result);
            break;
        }
        
        case OP_PCMPNE: {
            /* Pop two pointers, compare for inequality */
            qrun_value_t ptr2 = qrun_pop_value(vm);
            qrun_value_t ptr1 = qrun_pop_value(vm);
            
            if (ptr1 >= 0 || ptr2 >= 0) {
                fprintf(stderr, "PCMPNE: pointer operands required\n");
                vm->halted = 1;
                break;
            }
            
            qrun_push_value(vm, (ptr1 != ptr2) ? 1 : 0);
            break;
        }
        
        case OP_PUSHADDR: {
            /* PUSHADDR <scope> <name/slot>: push address of local or global
               Encoding: -(block_id*100000 + offset + 1000)
               Local arrays: block_id=0, offset=0 initially
               Global arrays: block_id=1, offset=global_idx
            */
            int is_local = (instr->arg.array.size == 1);  /* size=1 means local, 0 means global */
            
            if (is_local) {
                int slot = instr->arg.array.slot;
                if (slot < 0 || slot >= 256) {
                    fprintf(stderr, "PUSHADDR: invalid local slot %d\n", slot);
                    vm->halted = 1;
                    break;
                }
                qrun_value_t addr = -(0 * 100000 + 0 + 1000);  /* block_id=0, offset=0 */
                qrun_push_value(vm, addr);
            } else {
                /* Address of global array */
                const char* name = instr->arg.array.name;
                if (!name) {
                    fprintf(stderr, "PUSHADDR: missing global name\n");
                    vm->halted = 1;
                    break;
                }
                
                /* Look up in global arrays */
                int global_idx = -1;
                for (size_t i = 0; i < vm->ngarrays; i++) {
                    if (vm->garrays[i].name && strcmp(vm->garrays[i].name, name) == 0) {
                        global_idx = i;
                        break;
                    }
                }
                
                if (global_idx == -1) {
                    fprintf(stderr, "PUSHADDR: global '%s' not found\n", name);
                    vm->halted = 1;
                    break;
                }
                
                /* Push with block_id=1 to indicate global array */
                qrun_value_t addr = -(1 * 100000 + global_idx + 1000);
                qrun_push_value(vm, addr);
            }
            break;
        }
        
        case OP_HALT:
            vm->halted = 1;
            break;
        
        default:
            fprintf(stderr, "Unknown opcode: %d\n", op);
            vm->halted = 1;
            break;
        }
        
        /* Increment PC unless a jump set it explicitly */
        if (should_increment) {
            vm->pc++;
        }
    }
    
    return vm->exit_code;
}
