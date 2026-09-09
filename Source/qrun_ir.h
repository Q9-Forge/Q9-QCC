#ifndef QRUN_IR_H
#define QRUN_IR_H

#include "qrun_vm.h"

/* =========================================================================
 * IR Parser - Text format to Instruction stream
 * ========================================================================= */

typedef struct {
    char** names;
    size_t count;
    size_t capacity;
} qrun_string_pool_t;

/* Parse IR file into instruction stream */
int qrun_ir_parse(const char* filename, 
                  qrun_instruction_t** out_code, 
                  size_t* out_size,
                  void** out_pool);

/* String pool for name interning */
qrun_string_pool_t* qrun_string_pool_create(void);
void                qrun_string_pool_destroy(qrun_string_pool_t* pool);
char*               qrun_string_pool_intern(qrun_string_pool_t* pool, const char* s);

#endif /* QRUN_IR_H */
