#include "qrun_vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Parse architecture JSON file and set globals */
static int qrun_load_arch_file(qrun_vm_t* vm, const char* arch_file)
{
    FILE* f = fopen(arch_file, "r");
    if (!f) {
        fprintf(stderr, "Failed to open architecture file: %s\n", arch_file);
        return -1;
    }
    
    /* Simple JSON parser for { "__ptrsize": N } format */
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        /* Look for "__ptrsize": N pattern */
        const char* ptr = strstr(line, "__ptrsize");
        if (!ptr) continue;
        
        ptr = strchr(ptr, ':');
        if (!ptr) continue;
        
        int value = atoi(ptr + 1);
        
        /* Add global __ptrsize */
        if (vm->nglobals < vm->nglobals_capacity) {
            vm->named_globals[vm->nglobals].name = "__ptrsize";
            vm->named_globals[vm->nglobals].value = value;
            vm->nglobals++;
            fprintf(stderr, "Architecture: __ptrsize = %d\n", value);
        }
    }
    
    fclose(f);
    return 0;
}

int main(int argc, char* argv[])
{
    const char* ir_file = NULL;
    const char* arch_file = NULL;
    
    /* Parse arguments: [--arch=FILE] IR_FILE */
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--arch=", 7) == 0) {
            arch_file = argv[i] + 7;
        } else {
            ir_file = argv[i];
        }
    }
    
    if (!ir_file) {
        fprintf(stderr, "Usage: %s [--arch=ARCH_FILE] <ir_file>\n", argv[0]);
        return 1;
    }
    
    qrun_vm_t* vm = qrun_vm_create();
    if (!vm) {
        fprintf(stderr, "Failed to create VM\n");
        return 1;
    }
    
    /* Load architecture globals before IR */
    if (arch_file) {
        if (qrun_load_arch_file(vm, arch_file) != 0) {
            qrun_vm_destroy(vm);
            return 1;
        }
    }
    
    if (qrun_vm_load_ir(vm, ir_file) != 0) {
        fprintf(stderr, "Failed to load IR from %s\n", ir_file);
        qrun_vm_destroy(vm);
        return 1;
    }
    
    int exit_code = qrun_vm_run(vm);
    
    qrun_vm_destroy(vm);
    return exit_code;
}
