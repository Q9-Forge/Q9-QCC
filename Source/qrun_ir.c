#include "qrun_ir.h"
#ifndef QRUN_OS9
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

/* =========================================================================
 * String Pool - for efficient name storage
 * ========================================================================= */

qrun_string_pool_t* qrun_string_pool_create(void)
{
    qrun_string_pool_t* pool = malloc(sizeof(qrun_string_pool_t));
    if (!pool) return NULL;
    
    pool->names = malloc(256 * QRUN_PTR_BYTES);
    pool->capacity = 256;
    pool->count = 0;
    
    return pool;
}

void qrun_string_pool_destroy(qrun_string_pool_t* pool)
{
    if (!pool) return;
    
    { size_t i;
        for (i = 0; i < pool->count; i++) {
        free(pool->names[i]);
    }
        }
    free(pool->names);
    free(pool);
}

char* qrun_string_pool_intern(qrun_string_pool_t* pool, const char* s)
{
    if (!pool || !s) return NULL;
    
    /* Check if already interned */
    { size_t i;
        for (i = 0; i < pool->count; i++) {
        if (strcmp(pool->names[i], s) == 0) {
            return pool->names[i];
        }
    }
        }
    
    /* Expand if needed */
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        char** new_names = realloc(pool->names, pool->capacity * QRUN_PTR_BYTES);
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
    char* token_buffer;
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
    
    qrun_lexer_t* lex = malloc(sizeof(qrun_lexer_t));
    lex->f = f;
    lex->token_buffer = malloc(256);
    if (!lex->token_buffer) {
        fclose(f);
        free(lex);
        return NULL;
    }
    lex->tok = NULL;
    lex->pool = qrun_string_pool_create();
    
    return lex;
}

static void qrun_lexer_destroy(qrun_lexer_t* lex)
{
    if (!lex) return;
    fclose(lex->f);
    free(lex->token_buffer);
    /* Don't destroy pool here - it will be destroyed by VM */
    free(lex);
}

static int qrun_next_line(qrun_lexer_t* lex)
{
    do {
        if (!fgets(lex->line, 256, lex->f)) {
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
        lex->tok = lex->tok + 1;
    }
    
    if (*lex->tok == '\0' || *lex->tok == '\n' || *lex->tok == ';') {
        return NULL;
    }
    
    char* buf = lex->token_buffer;
    int i = 0;
    while (i < 255 && !isspace(*lex->tok) && *lex->tok != '\n' && *lex->tok != ';') {
        buf[i] = *lex->tok;
        i++;
        lex->tok = lex->tok + 1;
    }
    buf[i] = '\0';
    
    return buf;
}

static qrun_opcode_t qrun_opcode_from_string(const char* s)
{
    /* The IR producer terminates a complete stream with the bare token OK.
       Keep this sentinel independent of the target C library's string ABI;
       the OS-9 build must be able to recognize it before any library call. */
    if (s) {
        if (s[0] == 'O') {
            if (s[1] == 'K') {
                if (s[2] == '\0') return OP_HALT;
            }
        }
    }
    if (strcmp(s, "FUNC") == 0)      return OP_FUNC;
    if (strcmp(s, "ENDFUNC") == 0)   return OP_ENDFUNC;
    if (strcmp(s, "RET") == 0)       return OP_RET;
    if (strcmp(s, "RETP") == 0)      return OP_RET;  /* Pointer version, same semantics */
    if (strcmp(s, "LABEL") == 0)     return OP_LABEL;
    if (strcmp(s, "JMP") == 0)       return OP_JMP;
    if (strcmp(s, "PUSH") == 0)      return OP_PUSH;
    if (strcmp(s, "DUP") == 0)       return OP_DUP;
    if (strcmp(s, "SWAP") == 0)      return OP_SWAP;
    if (strcmp(s, "DROP") == 0)      return OP_DROP;
    if (strcmp(s, "LOADL") == 0)     return OP_LOADL;
    if (strcmp(s, "STOREL") == 0)    return OP_STOREL;
    if (strcmp(s, "LOADG") == 0)     return OP_LOADG;
    if (strcmp(s, "STOREG") == 0)    return OP_STOREG;
    if (strcmp(s, "LOADGP") == 0)    return OP_LOADGP;
    if (strcmp(s, "STOREGP") == 0)   return OP_STOREGP;
    if (strcmp(s, "ADD") == 0)       return OP_ADD;
    if (strcmp(s, "SUB") == 0)       return OP_SUB;
    if (strcmp(s, "MUL") == 0)       return OP_MUL;
    if (strcmp(s, "DIV") == 0)       return OP_DIV;
    if (strcmp(s, "MOD") == 0)       return OP_MOD;
    if (strcmp(s, "NEG") == 0)       return OP_NEG;
    if (strcmp(s, "BAND") == 0)      return OP_BAND;
    if (strcmp(s, "BOR") == 0)       return OP_BOR;
    if (strcmp(s, "BXOR") == 0)      return OP_BXOR;
    if (strcmp(s, "NOTBIT") == 0)    return OP_NOTBIT;
    if (strcmp(s, "SHL") == 0)       return OP_SHL;
    if (strcmp(s, "SHR") == 0)       return OP_SHR;
    if (strcmp(s, "USHR") == 0)      return OP_USHR;
    if (strcmp(s, "CMPEQ") == 0)     return OP_CMPEQ;
    if (strcmp(s, "CMPNE") == 0)     return OP_CMPNE;
    if (strcmp(s, "CMPLT") == 0)     return OP_CMPLT;
    if (strcmp(s, "CMPLE") == 0)     return OP_CMPLE;
    if (strcmp(s, "CMPGT") == 0)     return OP_CMPGT;
    if (strcmp(s, "CMPGE") == 0)     return OP_CMPGE;
    if (strcmp(s, "CMPULT") == 0)    return OP_CMPULT;
    if (strcmp(s, "CMPUGE") == 0)    return OP_CMPUGE;
    if (strcmp(s, "CMPULE") == 0)    return OP_CMPULE;
    if (strcmp(s, "CMPUGT") == 0)    return OP_CMPUGT;
    /* Legacy aliases for old tests */
    if (strcmp(s, "EQ") == 0)        return OP_CMPEQ;
    if (strcmp(s, "NE") == 0)        return OP_CMPNE;
    if (strcmp(s, "LT") == 0)        return OP_CMPLT;
    if (strcmp(s, "LE") == 0)        return OP_CMPLE;
    if (strcmp(s, "GT") == 0)        return OP_CMPGT;
    if (strcmp(s, "GE") == 0)        return OP_CMPGE;
    if (strcmp(s, "BEQ") == 0)       return OP_BEQ;
    if (strcmp(s, "BNE") == 0)       return OP_BNE;
    /* Legacy alias */
    if (strcmp(s, "JZ") == 0)        return OP_BEQ;  /* Jump if Zero = BEQ */
    if (strcmp(s, "CALL") == 0)      return OP_CALL;
    if (strcmp(s, "CALLP") == 0)     return OP_CALL;  /* Pointer version, same semantics */
    if (strcmp(s, "LARRAY") == 0)    return OP_LARRAY;
    if (strcmp(s, "GARRAY") == 0)    return OP_GARRAY;
    if (strcmp(s, "LOADIDX") == 0)   return OP_LOADIDX;
    if (strcmp(s, "STOREIDX") == 0)  return OP_STOREIDX;
    if (strcmp(s, "ADDRL") == 0)     return OP_ADDRL;
    if (strcmp(s, "ADDRG") == 0)     return OP_ADDRG;
    if (strcmp(s, "LOADP") == 0)     return OP_LOADP;
    if (strcmp(s, "STOREP") == 0)    return OP_STOREP;
    if (strcmp(s, "LOADIND") == 0)   return OP_LOADIND;
    if (strcmp(s, "STOREIND") == 0)  return OP_STOREIND;
    if (strcmp(s, "IPADD") == 0)     return OP_IPADD;
    if (strcmp(s, "PTRINDEX") == 0)  return OP_PTRINDEX;
    if (strcmp(s, "IPADDN") == 0)    return OP_IPADDN;
    if (strcmp(s, "PADD") == 0)      return OP_PADD;
    if (strcmp(s, "PCMPNE") == 0)    return OP_PCMPNE;
    if (strcmp(s, "PUSHADDR") == 0)  return OP_PUSHADDR;
    if (strcmp(s, "PRINT") == 0)     return OP_PRINT;
    if (strcmp(s, "PRINTC") == 0)    return OP_PRINTC;
    if (strcmp(s, "GLOBAL") == 0)    return OP_GLOBAL;
    if (strcmp(s, "GINIT") == 0)     return OP_GINIT;
    if (strcmp(s, "GLOBALDECL") == 0) return OP_GLOBALDECL;
    if (strcmp(s, "FUNCDECL") == 0)  return OP_FUNCDECL;
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
    
    qrun_instruction_t* code = malloc(1024 * sizeof(qrun_instruction_t));
    size_t capacity = 1024;
    size_t code_idx = 0;
    
    while (qrun_next_line(lex)) {
        char* op_str = qrun_next_token(lex);
        if (!op_str) continue;
        
        qrun_opcode_t op = qrun_opcode_from_string(op_str);
        if (op == (qrun_opcode_t)(-1)) {
            fprintf(stderr, "Error: unknown opcode '%s'\n", op_str);
            goto error;
        }
        
        if (code_idx == capacity) {
            qrun_instruction_t* grown = realloc(code, capacity * 2 * sizeof(qrun_instruction_t));
            if (!grown) goto error;
            code = grown;
            capacity *= 2;
        }
        memset(&code[code_idx], 0, sizeof(qrun_instruction_t));
        code[code_idx].op = op;
        
        /* Parse arguments based on opcode */
        char* arg_str = qrun_next_token(lex);
        
        switch (op) {
        case OP_PUSH:
        case OP_LOADL:
        case OP_STOREL:
        case OP_LOADP:
        case OP_STOREP:
        case OP_ADDRL:
            if (arg_str) {
                code[code_idx].arg_i = atoi(arg_str);
            }
            break;
        
        case OP_FUNC: {
            if (arg_str) {
                code[code_idx].arg_s = qrun_string_pool_intern(lex->pool, arg_str);
                char* nargs_str = qrun_next_token(lex);
                code[code_idx].arg_nargs = nargs_str ? atoi(nargs_str) : 0;
                char* nlocals_str = qrun_next_token(lex);
                code[code_idx].arg_func_nlocals = nlocals_str ? atoi(nlocals_str) : 0;
            }
            break;
        }
        
        case OP_CALL: {
            if (arg_str) {
                code[code_idx].arg_s = qrun_string_pool_intern(lex->pool, arg_str);
                char* nargs_str = qrun_next_token(lex);
                code[code_idx].arg_nargs = nargs_str ? atoi(nargs_str) : 0;
            }
            break;
        }
        
        case OP_LARRAY: {
            /* LARRAY <slot> <type> <size> */
            if (arg_str) {
                int slot_val = atoi(arg_str);
                char* type_str = qrun_next_token(lex);
                char* type_str_intern = type_str ? qrun_string_pool_intern(lex->pool, type_str) : NULL;
                char* size_str = qrun_next_token(lex);
                int size_val = size_str ? atoi(size_str) : 0;
                
                code[code_idx].arg_array_slot = slot_val;
                code[code_idx].arg_type = type_str_intern;
                code[code_idx].arg_array_size = size_val;
            }
            break;
        }
        
        case OP_GARRAY: {
            /* GARRAY <name> <type> <size> <init> */
            if (arg_str) {
                char* name = qrun_string_pool_intern(lex->pool, arg_str);
                char* type_str = qrun_next_token(lex);
                if (type_str) type_str = qrun_string_pool_intern(lex->pool, type_str);
                char* size_str = qrun_next_token(lex);
                int size_val = size_str ? atoi(size_str) : 0;
                char* init_str = qrun_next_token(lex);
                
                code[code_idx].arg_s = name;
                code[code_idx].arg_type = type_str;
                code[code_idx].arg_array_size = size_val;
                /* init is ignored for now */
            }
            break;
        }
        
        case OP_LOADIDX:
        case OP_STOREIDX: {
            /* LOADIDX <scope> <name/slot> <type> */
            if (arg_str) {
                /* scope is L/G, parse it */
                int is_local = (arg_str[0] == 'L');
                char* name_or_slot = qrun_next_token(lex);
                
                if (is_local) {
                    code[code_idx].arg_array_slot = name_or_slot ? atoi(name_or_slot) : 0;
                } else {
                    code[code_idx].arg_s = name_or_slot ? qrun_string_pool_intern(lex->pool, name_or_slot) : NULL;
                }
                
                char* type_str = qrun_next_token(lex);
                code[code_idx].arg_type = type_str ? qrun_string_pool_intern(lex->pool, type_str) : NULL;
                code[code_idx].arg_array_size = is_local ? 1 : 0;  /* Repurpose: size=0 means global */
            }
            break;
        }

        case OP_LOADIND:
        case OP_STOREIND:
            /* LOADIND/STOREIND carry the pointee type, e.g. "p" for a
               function/data pointer. */
            if (arg_str) {
                code[code_idx].arg_type =
                    qrun_string_pool_intern(lex->pool, arg_str);
            }
            break;
        
        case OP_PUSHADDR: {
            /* PUSHADDR <scope> <name/slot> */
            if (arg_str) {
                int is_local = (arg_str[0] == 'L');
                char* name_or_slot = qrun_next_token(lex);
                
                if (is_local) {
                    code[code_idx].arg_array_slot = name_or_slot ? atoi(name_or_slot) : 0;
                } else {
                    code[code_idx].arg_s = name_or_slot ? qrun_string_pool_intern(lex->pool, name_or_slot) : NULL;
                }
                code[code_idx].arg_array_size = is_local ? 1 : 0;  /* Repurpose: size=0 means global */
            }
            break;
        }
        
        case OP_IPADD:
        case OP_PTRINDEX:
        case OP_PADD: {
            /* IPADD <type> or PADD <type>: type for size calculation */
            if (arg_str) {
                code[code_idx].arg_type = qrun_string_pool_intern(lex->pool, arg_str);
            }
            break;
        }
        
        case OP_IPADDN: {
            /* IPADDN <size>: runtime size for offset calculation */
            if (arg_str) {
                code[code_idx].arg_i = atoi(arg_str);
            }
            break;
        }
        
        case OP_PCMPNE: {
            /* PCMPNE has no args */
            break;
        }
        
        case OP_GINIT: {
            /* GINIT <name> <index> <value> */
            if (arg_str) {
                char* name = qrun_string_pool_intern(lex->pool, arg_str);
                char* index_str = qrun_next_token(lex);
                int index = index_str ? atoi(index_str) : 0;
                char* value_str = qrun_next_token(lex);
                int32_t value = value_str ? (int32_t)atoi(value_str) : 0;
                
                code[code_idx].arg_s = name;
                code[code_idx].arg_ginit_index = index;
                code[code_idx].arg_ginit_value = value;
            }
            break;
        }
        
        case OP_GLOBAL: {
            /* GLOBAL <name> <scope> <type> <init> */
            if (arg_str) {
                char* name = qrun_string_pool_intern(lex->pool, arg_str);
                char* scope_str = qrun_next_token(lex);
                int scope = scope_str ? atoi(scope_str) : 0;
                char* type_str = qrun_next_token(lex);
                if (type_str) type_str = qrun_string_pool_intern(lex->pool, type_str);
                char* init_str = qrun_next_token(lex);
                int32_t init_val = init_str ? (int32_t)atoi(init_str) : 0;
                
                code[code_idx].arg_s = name;
                code[code_idx].arg_global_scope = scope;
                code[code_idx].arg_type = type_str;
                code[code_idx].arg_global_init = init_val;
            }
            break;
        }
        
        case OP_GLOBALDECL: {
            /* GLOBALDECL <name> <type> - external global declaration (no-op) */
            /* Just parse and discard - these are for multi-file linking */
            if (arg_str) {
                char* type_str = qrun_next_token(lex);
                /* Type is discarded - we just ignore external declarations */
            }
            break;
        }
        
        case OP_FUNCDECL: {
            /* FUNCDECL <name> <nargs> - external function declaration (no-op) */
            /* Just parse and discard - these are for multi-file linking */
            if (arg_str) {
                char* nargs_str = qrun_next_token(lex);
                /* nargs is discarded - we just ignore external declarations */
            }
            break;
        }
        
        case OP_LOADG:
        case OP_STOREG:
        case OP_LOADGP:
        case OP_STOREGP:
        case OP_ADDRG:
        case OP_LABEL:
        case OP_JMP:
        case OP_BEQ:
        case OP_BNE:
            if (arg_str) {
                code[code_idx].arg_s = qrun_string_pool_intern(lex->pool, arg_str);
            }
            break;
        
        default:
            break;
        }
        
        code_idx++;
    }
    
    *out_code = code;
    *out_size = code_idx;
    if (out_pool) *out_pool = lex->pool;
    qrun_lexer_destroy(lex);
    return 0;

error:
    qrun_lexer_destroy(lex);
    free(code);
    return -1;
}
