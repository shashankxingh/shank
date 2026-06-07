#include "lexer.h"
#include "errors.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void lexer_init(Lexer* lexer, const char* source, const char* filename) {
    lexer->source = source;
    lexer->filename = filename;
    lexer->current = source;
    lexer->line_start = source;
    lexer->line = 1;
    lexer->indent_stack[0] = 0;
    lexer->indent_depth = 1;
    lexer->pending_dedents = 0;
    lexer->is_at_line_start = 1;
    lexer->in_fstr = 0;
    lexer->brace_depth = 0;
}

static int is_at_end(Lexer* lexer) {
    return *lexer->current == '\0';
}

static char advance(Lexer* lexer) {
    lexer->current++;
    return lexer->current[-1];
}

static char peek(Lexer* lexer) {
    return *lexer->current;
}

static char peek_next(Lexer* lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->current[1];
}

static int match(Lexer* lexer, char expected) {
    if (is_at_end(lexer)) return 0;
    if (*lexer->current != expected) return 0;
    lexer->current++;
    return 1;
}

static Token make_token(Lexer* lexer, TokenKind kind, const char* start) {
    Token token;
    token.kind = kind;
    token.start = start;
    token.length = (int)(lexer->current - start);
    token.line = lexer->line;
    token.col = (int)(start - lexer->line_start) + 1;
    return token;
}

static Token error_token(Lexer* lexer, const char* message, const char* start) {
    Token token = make_token(lexer, TOK_ERROR, start);
    sk_error(lexer->filename, token.line, token.col, "%s", message);
    return token;
}

static void skip_whitespace(Lexer* lexer) {
    for (;;) {
        char c = peek(lexer);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lexer);
                break;
            case '#':
                while (peek(lexer) != '\n' && !is_at_end(lexer)) advance(lexer);
                break;
            default:
                return;
        }
    }
}

static int check_keyword(Lexer* lexer, const char* start, int length, const char* rest, TokenKind kind) {
    if (lexer->current - start == length && memcmp(start, rest, length) == 0) {
        return kind;
    }
    return TOK_IDENT;
}

static TokenKind identifier_type(Lexer* lexer, const char* start) {
    int len = (int)(lexer->current - start);
    
    #define MATCH(str, kind) if (len == strlen(str) && memcmp(start, str, len) == 0) return kind;
    
    MATCH("im", TOK_IM)
    MATCH("mut", TOK_MUT)
    MATCH("out", TOK_OUT)
    MATCH("fn", TOK_FN)
    MATCH("struct", TOK_STRUCT)
    MATCH("enum", TOK_ENUM)
    MATCH("if", TOK_IF)
    MATCH("elif", TOK_ELIF)
    MATCH("else", TOK_ELSE)
    MATCH("for", TOK_FOR)
    MATCH("while", TOK_WHILE)
    MATCH("in", TOK_IN)
    MATCH("match", TOK_MATCH)
    MATCH("case", TOK_CASE)
    MATCH("return", TOK_RETURN)
    MATCH("break", TOK_BREAK)
    MATCH("continue", TOK_CONTINUE)
    MATCH("import", TOK_IMPORT)
    MATCH("true", TOK_TRUE)
    MATCH("false", TOK_FALSE)
    MATCH("none", TOK_NONE)
    MATCH("and", TOK_AND)
    MATCH("or", TOK_OR)
    MATCH("not", TOK_NOT)
    
    #undef MATCH
    
    return TOK_IDENT;
}

static Token identifier(Lexer* lexer, const char* start) {
    while (isalnum(peek(lexer)) || peek(lexer) == '_') {
        advance(lexer);
    }
    return make_token(lexer, identifier_type(lexer, start), start);
}

static Token number(Lexer* lexer, const char* start) {
    int is_float = 0;
    while (isdigit(peek(lexer))) advance(lexer);

    if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
        is_float = 1;
        advance(lexer); // consume .
        while (isdigit(peek(lexer))) advance(lexer);
    }

    Token token = make_token(lexer, is_float ? TOK_FLOAT_LIT : TOK_INT_LIT, start);
    
    // Quick parse for basic numbers, would need strtoll/strtod for production
    if (is_float) {
        token.float_val = atof(start);
    } else {
        token.int_val = atoll(start);
    }
    
    return token;
}

static Token string(Lexer* lexer, const char* start, int is_continuation) {
    while (peek(lexer) != '"' && peek(lexer) != '{' && !is_at_end(lexer)) {
        if (peek(lexer) == '\n') {
            lexer->line++;
            lexer->line_start = lexer->current + 1;
        }
        advance(lexer);
    }

    if (is_at_end(lexer)) return error_token(lexer, "Unterminated string", start);

    if (peek(lexer) == '"') {
        advance(lexer); // closing quote
        if (is_continuation) {
            lexer->in_fstr = 0;
            return make_token(lexer, TOK_FSTR_END, start);
        } else {
            return make_token(lexer, TOK_STR_LIT, start);
        }
    } else if (peek(lexer) == '{') {
        advance(lexer); // consume {
        lexer->in_fstr = 1;
        lexer->brace_depth = 1;
        return make_token(lexer, is_continuation ? TOK_FSTR_MID : TOK_FSTR_START, start);
    }
    
    return error_token(lexer, "Unexpected string state", start);
}

Token lexer_next_token(Lexer* lexer) {
    if (lexer->pending_dedents > 0) {
        lexer->pending_dedents--;
        return make_token(lexer, TOK_DEDENT, lexer->current);
    }

    if (lexer->is_at_line_start) {
        int spaces = 0;
        const char* indent_start = lexer->current;
        
        while (peek(lexer) == ' ' || peek(lexer) == '\t') {
            if (advance(lexer) == '\t') spaces += 4;
            else spaces += 1;
        }
        
        if (peek(lexer) == '\n' || peek(lexer) == '\r' || peek(lexer) == '#') {
            // Empty line, ignore indentation
            skip_whitespace(lexer);
            if (is_at_end(lexer)) return make_token(lexer, TOK_EOF, lexer->current);
        } else {
            lexer->is_at_line_start = 0;
            
            int current_indent = lexer->indent_stack[lexer->indent_depth - 1];
            if (spaces > current_indent) {
                lexer->indent_stack[lexer->indent_depth++] = spaces;
                return make_token(lexer, TOK_INDENT, indent_start);
            } else if (spaces < current_indent) {
                while (lexer->indent_depth > 0 && lexer->indent_stack[lexer->indent_depth - 1] > spaces) {
                    lexer->indent_depth--;
                    lexer->pending_dedents++;
                }
                
                if (lexer->indent_stack[lexer->indent_depth - 1] != spaces) {
                    sk_error(lexer->filename, lexer->line, spaces, "Inconsistent indentation");
                }
                
                if (lexer->pending_dedents > 0) {
                    lexer->pending_dedents--;
                    return make_token(lexer, TOK_DEDENT, indent_start);
                }
            }
        }
    }

    skip_whitespace(lexer);
    const char* start = lexer->current;

    if (is_at_end(lexer)) {
        if (lexer->indent_depth > 1) {
            lexer->indent_depth--;
            lexer->pending_dedents = lexer->indent_depth - 1;
            return make_token(lexer, TOK_DEDENT, start);
        }
        return make_token(lexer, TOK_EOF, start);
    }

    char c = advance(lexer);

    if (c == '\n') {
        lexer->line++;
        lexer->line_start = lexer->current;
        lexer->is_at_line_start = 1;
        return make_token(lexer, TOK_NEWLINE, start);
    }

    if (isalpha(c) || c == '_') return identifier(lexer, start);
    if (isdigit(c)) return number(lexer, start);

    switch (c) {
        case '(': return make_token(lexer, TOK_LPAREN, start);
        case ')': return make_token(lexer, TOK_RPAREN, start);
        case '[': return make_token(lexer, TOK_LBRACKET, start);
        case ']': return make_token(lexer, TOK_RBRACKET, start);
        case '{': 
            if (lexer->in_fstr) lexer->brace_depth++;
            return make_token(lexer, TOK_LBRACE, start);
        case '}':
            if (lexer->in_fstr) {
                lexer->brace_depth--;
                if (lexer->brace_depth == 0) {
                    return string(lexer, start, 1);
                }
            }
            return make_token(lexer, TOK_RBRACE, start);
        case ':': return make_token(lexer, TOK_COLON, start);
        case ',': return make_token(lexer, TOK_COMMA, start);
        case '"': return string(lexer, start, 0);
        
        case '.': 
            if (match(lexer, '.')) return make_token(lexer, TOK_DOTDOT, start);
            return make_token(lexer, TOK_DOT, start);
            
        case '-': 
            if (match(lexer, '>')) return make_token(lexer, TOK_ARROW, start);
            if (match(lexer, '=')) return make_token(lexer, TOK_MINUS_ASSIGN, start);
            return make_token(lexer, TOK_MINUS, start);
            
        case '+':
            if (match(lexer, '=')) return make_token(lexer, TOK_PLUS_ASSIGN, start);
            return make_token(lexer, TOK_PLUS, start);
            
        case '*':
            if (match(lexer, '*')) return make_token(lexer, TOK_POWER, start);
            if (match(lexer, '=')) return make_token(lexer, TOK_STAR_ASSIGN, start);
            return make_token(lexer, TOK_STAR, start);
            
        case '/':
            if (match(lexer, '/')) {
                // Integer division, but for now just skip to end of line if it's a comment
                // Wait, python // is intdiv, shank # is comment.
                return make_token(lexer, TOK_SLASH, start); // simplified for now
            }
            if (match(lexer, '=')) return make_token(lexer, TOK_SLASH_ASSIGN, start);
            return make_token(lexer, TOK_SLASH, start);
            
        case '%': return make_token(lexer, TOK_PERCENT, start);
        case '&': return make_token(lexer, TOK_AND, start); // simplification
        case '=':
            if (match(lexer, '=')) return make_token(lexer, TOK_EQ, start);
            return make_token(lexer, TOK_ASSIGN, start);
            
        case '!':
            if (match(lexer, '=')) return make_token(lexer, TOK_NEQ, start);
            return make_token(lexer, TOK_NOT, start);
            
        case '<':
            if (match(lexer, '=')) return make_token(lexer, TOK_LEQ, start);
            return make_token(lexer, TOK_LT, start);
            
        case '>':
            if (match(lexer, '=')) return make_token(lexer, TOK_GEQ, start);
            return make_token(lexer, TOK_GT, start);
            
        case '|':
            if (match(lexer, '|')) return make_token(lexer, TOK_OR, start);
            return error_token(lexer, "Unexpected character", start);
            
        case ' ':
        case '\r':
        case '\t':
            return error_token(lexer, "Unexpected character", start);
    }

    return error_token(lexer, "Unexpected character", start);
}
