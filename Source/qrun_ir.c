#include "qrun_ir.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * String Pool - for efficient name storage
 * ========================================================================= */

qrun_string_pool_t* qrun_string_pool_create(void)
{
    qrun_string_pool_t* pool = malloc(sizeof(*pool));
    if (!pool) return NULL;
    
    pool->names = malloc(256 * sizeof(char*));
    pool->capacity = 256;
    pool->count = 0;
    
    return pool;
}

void qrun_string_pool_destroy(qrun_string_pool_t* pool)
{
    if (!pool) return;
    
    for (size_t i = 0; i < pool->count; i++) {
        free(pool->names[i]);
    }
    free(pool->names);
    free(pool);
}

char* qrun_string_pool_intern(qrun_string_pool_t* pool, const char* s)
{
    if (!pool || !s) return NULL;
    
    /* Check if already interned */
    for (size_t i = 0; i < pool->count; i++) {
        if (strcmp(pool->names[i], s) == 0) {
            return pool->names[i];
        }
    }
    
    /* Expand if needed */
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        char** new_names = realloc(pool->names, pool->capacity * sizeof(char*));
        if (!new_names) return NULL;
        pool->names = new_names;
    }
    
    /* Add new string */
    char* dup = malloc(strlen(s) + 1);
    if (!dup) return NULL;
    strcpy(dup, s);
    
    pool->names[pool->count] = dup;
    return pool->names[pool->count++];
}

/* =========================================================================
 * Lexer/Parser - Text IR to Instructions
 * ========================================================================= */

typedef struct {
    FILE* f;
    char line[256];
    char* tok;
    qrun_string_pool_t* pool;
} qrun_lexer_t;

static qrun_lexer_t* qrun_lexer_create(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", filename);
        return NULL;
    }
    
    qrun_lexer_t* lex = malloc(sizeof(*lex));
    lex->f = f;
    lex->tok = NULL;
    lex->pool = qrun_string_pool_create();
    
    return lex;
}

static void qrun_lexer_destroy(qrun_lexer_t* lex)
{
    if (!lex) return;
    fclose(lex->f);
    /* Don't destroy pool here - it will be destroyed by VM */
    free(lex);
}

static int qrun_next_line(qrun_lexer_t* lex)
{
    do {
        if (!fgets(lex->line, sizeof(lex->line), lex->f)) {
            return 0;
        }
        /* Skip comments and empty lines */
        char* p = lex->line;
        while (isspace(*p)) p++;
        if (*p == ';' || *p == '#' || *p == '\0' || *p == '\n') {
            continue;
        }
        lex->tok = p;
        return 1;
    } while (1);
}

static char* qrun_next_token(qrun_lexer_t* lex)
{
    if (!lex->tok) return NULL;
    
    /* Skip whitespace */
    while (isspace(*lex->tok)) {
        lex->tok++;
    }
    
    if (*lex->tok == '\0' || *lex->tok == '\n' || *lex->tok == ';') {
        return NULL;
    }
    
    static char buf[256];
    int i = 0;
    while (i < 255 && !isspace(*lex->tok) && *lex->tok != '\n' && *lex->tok != ';') {
        buf[i++] = *lex->tok++;
    }
    buf[i] = '\0';
    
    return buf;
}

static qrun_opcode_t qrun_opcode_from_string(const char* s)
{
    if (strcmp(s, "FUNC") == 0)      return OP_FUNC;
    if (strcmp(s, "ENDFUNC") == 0)   return OP_ENDFUNC;
    if (strcmp(s, "RET") == 0)       return OP_RET;
    if (strcmp(s, "LABEL") == 0)     return OP_LABEL;
    if (strcmp(s, "JMP") == 0)       return OP_JMP;
    if (strcmp(s, "PUSH") == 0)      return OP_PUSH;
    if (strcmp(s, "DUP") == 0)       return OP_DUP;
    if (strcmp(s, "SWAP") == 0)      return OP_SWAP;
    if (strcmp(s, "LOADL") == 0)     return OP_LOADL;
    if (strcmp(s, "STOREL") == 0)    return OP_STOREL;
    if (strcmp(s, "LOADG") == 0)     return OP_LOADG;
    if (strcmp(s, "STOREG") == 0)    return OP_STOREG;
    if (strcmp(s, "ADD") == 0)       return OP_ADD;
    if (strcmp(s, "SUB") == 0)       return OP_SUB;
    if (strcmp(s, "MUL") == 0)       return OP_MUL;
    if (strcmp(s, "DIV") == 0)       return OP_DIV;
    if (strcmp(s, "MOD") == 0)       return OP_MOD;
    if (strcmp(s, "NEG") == 0)       return OP_NEG;
    if (strcmp(s, "EQ") == 0)        return OP_EQ;
    if (strcmp(s, "NE") == 0)        return OP_NE;
    if (strcmp(s, "LT") == 0)        return OP_LT;
    if (strcmp(s, "LE") == 0)        return OP_LE;
    if (strcmp(s, "GT") == 0)        return OP_GT;
    if (strcmp(s, "GE") == 0)        return OP_GE;
    if (strcmp(s, "BEQ") == 0)       return OP_BEQ;
    if (strcmp(s, "BNE") == 0)       return OP_BNE;
    if (strcmp(s, "CALL") == 0)      return OP_CALL;
    if (strcmp(s, "PRINT") == 0)     return OP_PRINT;
    if (strcmp(s, "PRINTC") == 0)    return OP_PRINTC;
    if (strcmp(s, "GLOBAL") == 0)    return OP_GLOBAL;
    if (strcmp(s, "OK") == 0)        return OP_HALT;
    
    return -1;
}

int qrun_ir_parse(const char* filename, 
                  qrun_instruction_t** out_code, 
                  size_t* out_size,
                  void** out_pool)
{
    if (!filename || !out_code || !out_size) {
        return -1;
    }
    
    qrun_lexer_t* lex = qrun_lexer_create(filename);
    if (!lex) return -1;
    
    qrun_instruction_t* code = malloc(1024 * sizeof(*code));
    size_t code_idx = 0;
    
    while (qrun_next_line(lex)) {
        char* op_str = qrun_next_token(lex);
        if (!op_str) continue;
        
        qrun_opcode_t op = qrun_opcode_from_string(op_str);
        if (op == (qrun_opcode_t)(-1)) {
            fprintf(stderr, "Error: unknown opcode '%s'\n", op_str);
            goto error;
        }
        
        code[code_idx].op = op;
        
        /* Parse arguments based on opcode */
        char* arg_str = qrun_next_token(lex);
        
        switch (op) {
        case OP_PUSH:
        case OP_LOADL:
        case OP_STOREL:
            if (arg_str) {
                code[code_idx].arg.i = atoi(arg_str);
            }
            break;
        
        case OP_FUNC: {
            if (arg_str) {
                code[code_idx].arg.func.name = qrun_string_pool_intern(lex->pool, arg_str);
                char* nargs_str = qrun_next_token(lex);
                char* nlocals_str = qrun_next_token(lex);
                code[code_idx].arg.func.nargs = nargs_str ? atoi(nargs_str) : 0;
                code[code_idx].arg.func.nlocals = nlocals_str ? atoi(nlocals_str) : 0;
            }
            break;
        }
        
        case OP_CALL: {
            if (arg_str) {
                code[code_idx].arg.call.name = qrun_string_pool_intern(lex->pool, arg_str);
                char* nargs_str = qrun_next_token(lex);
                code[code_idx].arg.call.nargs = nargs_str ? atoi(nargs_str) : 0;
            }
            break;
        }
        
        case OP_LOADG:
        case OP_STOREG:
        case OP_LABEL:
        case OP_JMP:
            if (arg_str) {
                code[code_idx].arg.s = qrun_string_pool_intern(lex->pool, arg_str);
            }
            break;
        
        default:
            break;
        }
        
        code_idx++;
    }
    
    qrun_lexer_destroy(lex);
    
    *out_code = code;
    *out_size = code_idx;
    if (out_pool) *out_pool = lex->pool;  /* Would be NULL after destroy */
    return 0;

error:
    qrun_lexer_destroy(lex);
    free(code);
    return -1;
}
