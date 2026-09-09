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
    
    /* Allocate call frames */
    vm->frames = malloc(256 * sizeof(vm->frames[0]));
    vm->frame_size = 256;
    vm->nfuncs = 0;
    vm->funcs = malloc(256 * sizeof(vm->funcs[0]));
    memset(vm->funcs, 0, 256 * sizeof(vm->funcs[0]));
    vm->fp = 0;
    
    /* Allocate labels */
    vm->labels = malloc(512 * sizeof(vm->labels[0]));
    vm->nlabels = 0;
    vm->labels_capacity = 512;
    
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
    
    for (size_t i = 0; i < vm->fp; i++) {
        free(vm->frames[i].locals);
    }
    free(vm->frames);
    free(vm->funcs);
    free(vm->labels);
    
    /* Destroy string pool */
    if (vm->string_pool) {
        qrun_string_pool_destroy((qrun_string_pool_t*)vm->string_pool);
    }
    
    free(vm);
}

int qrun_vm_load_ir(qrun_vm_t* vm, const char* filename)
{
    if (!vm || !filename) return -1;
    
    if (qrun_ir_parse(filename, &vm->code, &vm->code_size, &vm->string_pool) != 0) {
        return -1;
    }
    
    /* Build function and label lookup tables */
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
