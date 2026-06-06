#ifndef SHANK_LEXER_H
#define SHANK_LEXER_H

#include "ast.h"
#include <stdint.h>

typedef struct {
    TokenKind kind;
    const char* start;
    int length;
    int line;
    int col;
    
    union {
        int64_t int_val;
        double float_val;
    };
} Token;

typedef struct {
    const char* source;
    const char* filename;
    const char* current;
    const char* line_start;
    int line;
    
    // For Python-like indentation
    int indent_stack[128];
    int indent_depth;
    int pending_dedents;
    int is_at_line_start;
} Lexer;

void lexer_init(Lexer* lexer, const char* source, const char* filename);
Token lexer_next_token(Lexer* lexer);

#endif // SHANK_LEXER_H
