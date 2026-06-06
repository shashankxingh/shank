#ifndef SHANK_PARSER_H
#define SHANK_PARSER_H

#include "lexer.h"
#include "ast.h"

typedef struct {
    Lexer* lexer;
    Arena* arena;
    Token current;
    Token previous;
    int had_error;
    int panic_mode;
} Parser;

void parser_init(Parser* parser, Lexer* lexer, Arena* arena);
Node* parser_parse_program(Parser* parser);

#endif // SHANK_PARSER_H
