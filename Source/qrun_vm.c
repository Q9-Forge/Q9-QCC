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
    vm->fp = 0;
    
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
    
    free(vm);
}

int qrun_vm_load_ir(qrun_vm_t* vm, const char* filename)
{
    if (!vm || !filename) return -1;
    
    return qrun_ir_parse(filename, &vm->code, &vm->code_size);
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
    if (slot < 0 || slot >= (int)vm->frames[frame_idx].nlocals) {
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
    if (slot < 0 || slot >= (int)vm->frames[frame_idx].nlocals) {
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
    
    for (size_t i = 0; i < vm->code_size; i++) {
        if (vm->code[i].op == OP_FUNC && vm->code[i].arg.s && 
            strcmp(vm->code[i].arg.s, "main") == 0) {
            main_addr = i;
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
    
    while (!vm->halted && vm->pc < vm->code_size) {
        qrun_instruction_t* instr = &vm->code[vm->pc];
        qrun_opcode_t op = instr->op;
        
        /* Fetch next instruction */
        vm->pc++;
        
        /* Decode & Execute */
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
            /* TODO: Phase 2 */
            fprintf(stderr, "CALL not yet implemented\n");
            vm->halted = 1;
            break;
        }
        
        case OP_LABEL:
        case OP_JMP:
        case OP_BEQ:
        case OP_BNE:
            /* TODO: Phase 3 (Control flow) */
            break;
        
        case OP_HALT:
            vm->halted = 1;
            break;
        
        default:
            fprintf(stderr, "Unknown opcode: %d\n", op);
            vm->halted = 1;
            break;
        }
    }
    
    return vm->exit_code;
}
