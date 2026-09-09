#include "qrun_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ir_file>\n", argv[0]);
        return 1;
    }
    
    qrun_vm_t* vm = qrun_vm_create();
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        return 1;
    }
    
    if (qrun_vm_load_ir(vm, argv[1]) != 0) {
        fprintf(stderr, "Failed to load IR from %s\n", argv[1]);
        qrun_vm_destroy(vm);
        return 1;
    }
    
    int exit_code = qrun_vm_run(vm);
    
    qrun_vm_destroy(vm);
    return exit_code;
}
