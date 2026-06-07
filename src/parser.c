#include "parser.h"
#include "errors.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void advance(Parser* parser) {
    parser->previous = parser->current;
    
    for (;;) {
        parser->current = lexer_next_token(parser->lexer);
        if (parser->current.kind != TOK_ERROR) break;
    }
}

static void error_at(Parser* parser, Token* token, const char* message) {
    if (parser->panic_mode) return;
    parser->panic_mode = 1;
    parser->had_error = 1;
    
    sk_error(parser->lexer->filename, token->line, token->col, "%s", message);
}

static void error(Parser* parser, const char* message) {
    error_at(parser, &parser->previous, message);
}

static void error_at_current(Parser* parser, const char* message) {
    error_at(parser, &parser->current, message);
}

static void consume(Parser* parser, TokenKind kind, const char* message) {
    if (parser->current.kind == kind) {
        advance(parser);
        return;
    }
    error_at_current(parser, message);
}

static int check(Parser* parser, TokenKind kind) {
    return parser->current.kind == kind;
}

static int match(Parser* parser, TokenKind kind) {
    if (!check(parser, kind)) return 0;
    advance(parser);
    return 1;
}

static void synchronize(Parser* parser) {
    parser->panic_mode = 0;
    
    while (parser->current.kind != TOK_EOF) {
        if (parser->previous.kind == TOK_NEWLINE) return;
        
        switch (parser->current.kind) {
            case TOK_IM:
            case TOK_MUT:
            case TOK_OUT:
            case TOK_OUTT:
            case TOK_OUTC:
            case TOK_FN:
            case TOK_STRUCT:
            case TOK_IF:
            case TOK_WHILE:
            case TOK_FOR:
            case TOK_RETURN:
                return;
            default:
                ; // Do nothing
        }
        advance(parser);
    }
}

// Forward declarations
static Node* parse_expression(Parser* parser);
static Node* parse_statement(Parser* parser);
static Node* parse_block(Parser* parser, int* count_out);
static Node* parse_type_annotation(Parser* parser);

// --- Pratt Parser for Expressions ---

typedef enum {
    PREC_NONE,
    PREC_ASSIGN,  // = += -=
    PREC_OR,      // or
    PREC_AND,     // and
    PREC_EQUALITY,// == !=
    PREC_COMPAR,  // < > <= >=
    PREC_TERM,    // + -
    PREC_FACTOR,  // * / %
    PREC_POWER,   // **
    PREC_CAST,    // ->
    PREC_UNARY,   // - not
    PREC_CALL,    // . () []
    PREC_PRIMARY
} Precedence;

typedef Node* (*ParsePrefixFn)(Parser*);
typedef Node* (*ParseInfixFn)(Parser*, Node*);

typedef struct {
    ParsePrefixFn prefix;
    ParseInfixFn infix;
    Precedence precedence;
} ParseRule;

static ParseRule* get_rule(TokenKind kind);
static Node* parse_precedence(Parser* parser, Precedence precedence);

static Node* parse_number(Parser* parser) {
    if (parser->previous.kind == TOK_FLOAT_LIT) {
        Node* node = node_create(parser->arena, NODE_FLOAT_LIT, parser->previous.line, parser->previous.col);
        node->float_val = parser->previous.float_val;
        return node;
    } else {
        Node* node = node_create(parser->arena, NODE_INT_LIT, parser->previous.line, parser->previous.col);
        node->int_val = parser->previous.int_val;
        return node;
    }
}

static Node* parse_string(Parser* parser) {
    Node* node = node_create(parser->arena, NODE_STR_LIT, parser->previous.line, parser->previous.col);
    node->str_val = sk_strdup(parser->previous.start + 1); // skip quote
    node->str_len = parser->previous.length - 2;
    node->str_val[node->str_len] = '\0'; // ensure null termination for C compat
    return node;
}

static Node* parse_fstr(Parser* parser) {
    Node* node = node_create(parser->arena, NODE_INTERP_STR, parser->previous.line, parser->previous.col);
    
    Node** exprs = NULL;
    int count = 0;
    int cap = 4;
    exprs = (Node**)malloc(sizeof(Node*) * cap);
    
    // Add the prefix string (from " to { )
    Node* str_node = node_create(parser->arena, NODE_STR_LIT, parser->previous.line, parser->previous.col);
    str_node->str_val = sk_strdup(parser->previous.start + 1);
    str_node->str_len = parser->previous.length - 2; // " to {
    str_node->str_val[str_node->str_len] = '\0';
    exprs[count++] = str_node;
    
    while (1) {
        if (count >= cap) { cap *= 2; exprs = (Node**)realloc(exprs, sizeof(Node*) * cap); }
        exprs[count++] = parse_expression(parser);
        
        if (match(parser, TOK_FSTR_MID)) {
            Node* mid_node = node_create(parser->arena, NODE_STR_LIT, parser->previous.line, parser->previous.col);
            mid_node->str_val = sk_strdup(parser->previous.start + 1); // skip }
            mid_node->str_len = parser->previous.length - 2; // } to {
            mid_node->str_val[mid_node->str_len] = '\0';
            
            if (count >= cap) { cap *= 2; exprs = (Node**)realloc(exprs, sizeof(Node*) * cap); }
            exprs[count++] = mid_node;
        } else if (match(parser, TOK_FSTR_END)) {
            Node* end_node = node_create(parser->arena, NODE_STR_LIT, parser->previous.line, parser->previous.col);
            end_node->str_val = sk_strdup(parser->previous.start + 1); // skip }
            end_node->str_len = parser->previous.length - 2; // } to "
            end_node->str_val[end_node->str_len] = '\0';
            
            if (count >= cap) { cap *= 2; exprs = (Node**)realloc(exprs, sizeof(Node*) * cap); }
            exprs[count++] = end_node;
            break;
        } else {
            error(parser, "Expected interpolated string continuation or end.");
            break;
        }
    }
    
    node->interp_str.exprs = (Node**)arena_alloc(parser->arena, sizeof(Node*) * count);
    memcpy(node->interp_str.exprs, exprs, sizeof(Node*) * count);
    node->interp_str.count = count;
    free(exprs);
    return node;
}

static Node* parse_literal(Parser* parser) {
    Node* node;
    switch (parser->previous.kind) {
        case TOK_FALSE:
            node = node_create(parser->arena, NODE_BOOL_LIT, parser->previous.line, parser->previous.col);
            node->bool_val = 0;
            return node;
        case TOK_TRUE:
            node = node_create(parser->arena, NODE_BOOL_LIT, parser->previous.line, parser->previous.col);
            node->bool_val = 1;
            return node;
        case TOK_NONE:
            // Could represent as an indentifier or special node. Using IDENT for now to simplify
            node = node_create(parser->arena, NODE_IDENT, parser->previous.line, parser->previous.col);
            node->name = sk_strdup("none");
            return node;
        default:
            return NULL; // Unreachable
    }
}

static Node* parse_variable(Parser* parser) {
    Node* node = node_create(parser->arena, NODE_IDENT, parser->previous.line, parser->previous.col);
    char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';
    node->name = name;
    return node;
}

static Node* parse_grouping(Parser* parser) {
    Node* expr = parse_expression(parser);
    consume(parser, TOK_RPAREN, "Expect ')' after expression.");
    return expr;
}

static Node* parse_array_literal(Parser* parser) {
    Node* node = node_create(parser->arena, NODE_ARRAY_LIT, parser->previous.line, parser->previous.col);
    
    Node** elements = NULL;
    int count = 0;
    int cap = 4;
    elements = (Node**)malloc(sizeof(Node*) * cap);
    
    if (!check(parser, TOK_RBRACKET)) {
        do {
            if (count >= cap) {
                cap *= 2;
                elements = (Node**)realloc(elements, sizeof(Node*) * cap);
            }
            elements[count++] = parse_expression(parser);
        } while (match(parser, TOK_COMMA));
    }
    
    consume(parser, TOK_RBRACKET, "Expect ']' after array elements.");
    
    node->array_lit.elements = (Node**)arena_alloc(parser->arena, sizeof(Node*) * count);
    memcpy(node->array_lit.elements, elements, sizeof(Node*) * count);
    node->array_lit.count = count;
    free(elements);
    
    return node;
}
static Node* parse_put_expr(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    int has_paren = match(parser, TOK_LPAREN);
    Node* prompt = parse_expression(parser);
    if (has_paren) {
        consume(parser, TOK_RPAREN, "Expect ')' after put prompt.");
    }
    
    Node* node = node_create(parser->arena, NODE_PUT, line, col);
    node->put_expr.prompt = prompt;
    node->resolved_type = NULL; // will be resolved in checker
    return node;
}

static Node* parse_unary(Parser* parser) {
    TokenKind operator_type = parser->previous.kind;
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* operand = parse_precedence(parser, PREC_UNARY);
    
    Node* node = node_create(parser->arena, NODE_UNARY, line, col);
    node->unary.unary_op = operator_type;
    node->unary.operand = operand;
    return node;
}

static Node* parse_binary(Parser* parser, Node* left) {
    TokenKind operator_type = parser->previous.kind;
    ParseRule* rule = get_rule(operator_type);
    
    // Left-associative by default: pass prec + 1
    // Right-associative for power: pass prec
    Precedence prec = rule->precedence;
    if (operator_type != TOK_POWER) {
        prec = (Precedence)(prec + 1);
    }
    
    Node* right = parse_precedence(parser, prec);
    
    Node* node = node_create(parser->arena, NODE_BINARY, left->line, left->col);
    node->binary.left = left;
    node->binary.op = operator_type;
    node->binary.right = right;
    return node;
}

static Node* parse_call(Parser* parser, Node* left) {
    if (left->kind != NODE_IDENT && left->kind != NODE_FIELD_ACCESS) {
        error(parser, "Invalid call target.");
        return left;
    }
    
    Node* node = node_create(parser->arena, NODE_CALL, left->line, left->col);
    
    if (left->kind == NODE_IDENT) {
        node->call.call_name = left->name;
    } else {
        // Method call -> desugar or store differently, for now just store string representation
        StringBuilder* sb = sb_init();
        if (left->field_access.object->kind == NODE_IDENT) {
            sb_appendf(sb, "%s.%s", left->field_access.object->name, left->field_access.field);
        } else {
            sb_appendf(sb, "<expr>.%s", left->field_access.field);
        }
        node->call.call_name = sb_to_string(sb);
        sb_free(sb);
    }
    
    Node** args = NULL;
    int arg_count = 0;
    int cap = 4;
    args = (Node**)malloc(sizeof(Node*) * cap);
    
    if (!check(parser, TOK_RPAREN)) {
        do {
            if (arg_count >= cap) {
                cap *= 2;
                args = (Node**)realloc(args, sizeof(Node*) * cap);
            }
            args[arg_count++] = parse_expression(parser);
        } while (match(parser, TOK_COMMA));
    }
    
    consume(parser, TOK_RPAREN, "Expect ')' after arguments.");
    
    node->call.call_args = (Node**)arena_alloc(parser->arena, sizeof(Node*) * arg_count);
    memcpy(node->call.call_args, args, sizeof(Node*) * arg_count);
    node->call.call_arg_count = arg_count;
    free(args);
    
    return node;
}

static Node* parse_dot(Parser* parser, Node* left) {
    consume(parser, TOK_IDENT, "Expect property name after '.'.");
    
    char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';
    
    Node* node = node_create(parser->arena, NODE_FIELD_ACCESS, left->line, left->col);
    node->field_access.object = left;
    node->field_access.field = name;
    return node;
}

static Node* parse_index(Parser* parser, Node* left) {
    Node* index = parse_expression(parser);
    consume(parser, TOK_RBRACKET, "Expect ']' after index.");
    
    Node* node = node_create(parser->arena, NODE_INDEX, left->line, left->col);
    node->index_access.object = left;
    node->index_access.index = index;
    return node;
}

static Node* parse_cast(Parser* parser, Node* left) {
    consume(parser, TOK_IDENT, "Expect type name after '->'.");
    char* target_type = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(target_type, parser->previous.start, parser->previous.length);
    target_type[parser->previous.length] = '\0';
    
    Node* node = node_create(parser->arena, NODE_CAST, left->line, left->col);
    node->cast_expr.expr = left;
    node->cast_expr.target_type = target_type;
    return node;
}

static ParseRule rules[] = {
    [TOK_INT_LIT]     = {parse_number,   NULL,         PREC_NONE},
    [TOK_FLOAT_LIT]   = {parse_number,   NULL,         PREC_NONE},
    [TOK_STR_LIT]     = {parse_string,   NULL,         PREC_NONE},
    [TOK_FSTR_START]  = {parse_fstr,     NULL,         PREC_NONE},
    [TOK_BOOL_LIT]    = {parse_literal,  NULL,         PREC_NONE}, // Doesn't exist, using TRUE/FALSE
    [TOK_IDENT]       = {parse_variable, NULL,         PREC_NONE},
    
    [TOK_LPAREN]      = {parse_grouping, parse_call,   PREC_CALL},
    [TOK_RPAREN]      = {NULL,           NULL,         PREC_NONE},
    [TOK_LBRACKET]    = {parse_array_literal, parse_index,  PREC_CALL},
    [TOK_RBRACKET]    = {NULL,           NULL,         PREC_NONE},
    [TOK_DOT]         = {NULL,           parse_dot,    PREC_CALL},
    
    [TOK_PLUS]        = {NULL,           parse_binary, PREC_TERM},
    [TOK_MINUS]       = {parse_unary,    parse_binary, PREC_TERM},
    [TOK_STAR]        = {NULL,           parse_binary, PREC_FACTOR},
    [TOK_SLASH]       = {NULL,           parse_binary, PREC_FACTOR},
    [TOK_PERCENT]     = {NULL,           parse_binary, PREC_FACTOR},
    [TOK_POWER]       = {NULL,           parse_binary, PREC_POWER},
    
    [TOK_EQ]          = {NULL,           parse_binary, PREC_EQUALITY},
    [TOK_NEQ]         = {NULL,           parse_binary, PREC_EQUALITY},
    [TOK_LT]          = {NULL,           parse_binary, PREC_COMPAR},
    [TOK_GT]          = {NULL,           parse_binary, PREC_COMPAR},
    [TOK_LEQ]         = {NULL,           parse_binary, PREC_COMPAR},
    [TOK_GEQ]         = {NULL,           parse_binary, PREC_COMPAR},
    
    [TOK_AND]         = {NULL,           parse_binary, PREC_AND},
    [TOK_OR]          = {NULL,           parse_binary, PREC_OR},
    [TOK_NOT]         = {parse_unary,    NULL,         PREC_NONE},
    
    [TOK_ARROW]       = {NULL,           parse_cast,   PREC_CAST},
    
    [TOK_TRUE]        = {parse_literal,  NULL,         PREC_NONE},
    [TOK_FALSE]       = {parse_literal,  NULL,         PREC_NONE},
    [TOK_NONE]        = {parse_literal,  NULL,         PREC_NONE},
    [TOK_PUT]         = {parse_put_expr, NULL,         PREC_NONE},
};

static ParseRule* get_rule(TokenKind kind) {
    if (kind >= sizeof(rules) / sizeof(rules[0])) {
        static ParseRule empty = {NULL, NULL, PREC_NONE};
        return &empty;
    }
    return &rules[kind];
}

static Node* parse_precedence(Parser* parser, Precedence precedence) {
    advance(parser);
    ParsePrefixFn prefix_rule = get_rule(parser->previous.kind)->prefix;
    if (prefix_rule == NULL) {
        error(parser, "Expect expression.");
        return NULL;
    }
    
    Node* left = prefix_rule(parser);
    
    while (precedence <= get_rule(parser->current.kind)->precedence) {
        advance(parser);
        ParseInfixFn infix_rule = get_rule(parser->previous.kind)->infix;
        left = infix_rule(parser, left);
    }
    
    return left;
}

static Node* parse_expression(Parser* parser) {
    return parse_precedence(parser, PREC_ASSIGN);
}

// --- Statements ---

static Node* parse_type_annotation(Parser* parser) {
    // Just a simple ident or array type for now
    if (match(parser, TOK_IDENT)) {
        Node* node = node_create(parser->arena, NODE_IDENT, parser->previous.line, parser->previous.col);
        char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
        memcpy(name, parser->previous.start, parser->previous.length);
        name[parser->previous.length] = '\0';
        node->name = name;
        return node;
    } else {
        error_at_current(parser, "Expect type name.");
        return NULL;
    }
}

static Node* parse_im_decl(Parser* parser, int is_mutable) {
    Node* type_node = parse_type_annotation(parser);
    
    consume(parser, TOK_IDENT, "Expect variable name.");
    char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';
    
    consume(parser, TOK_ASSIGN, "Expect '=' after variable declaration.");
    Node* value = parse_expression(parser);
    
    consume(parser, TOK_NEWLINE, "Expect newline after declaration.");
    
    Node* node = node_create(parser->arena, NODE_IM, parser->previous.line, parser->previous.col);
    node->let_decl.let_name = name;
    node->let_decl.let_type = type_node;
    node->let_decl.let_value = value;
    node->let_decl.let_mutable = is_mutable;
    return node;
}

static Node* parse_out_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* expr = parse_expression(parser);
    consume(parser, TOK_NEWLINE, "Expect newline after out statement.");
    
    Node* node = node_create(parser->arena, NODE_OUT, line, col);
    node->expr = expr;
    return node;
}

static Node* parse_outt_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* expr = parse_expression(parser);
    consume(parser, TOK_NEWLINE, "Expect newline after outt statement.");
    
    Node* node = node_create(parser->arena, NODE_OUTT, line, col);
    node->expr = expr;
    return node;
}

static Node* parse_outc_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* expr = parse_expression(parser);
    consume(parser, TOK_NEWLINE, "Expect newline after outc statement.");
    
    Node* node = node_create(parser->arena, NODE_OUTC, line, col);
    node->expr = expr;
    return node;
}

static Node* parse_block(Parser* parser, int* count_out) {
    consume(parser, TOK_NEWLINE, "Expect newline before block.");
    while (match(parser, TOK_NEWLINE)) {} // Skip empty lines before the indent
    consume(parser, TOK_INDENT, "Expect indentation before block.");
    
    Node** stmts = NULL;
    int count = 0;
    int cap = 8;
    stmts = (Node**)malloc(sizeof(Node*) * cap);
    
    while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
        if (check(parser, TOK_NEWLINE)) {
            advance(parser);
            continue;
        }
        
        if (count >= cap) {
            cap *= 2;
            stmts = (Node**)realloc(stmts, sizeof(Node*) * cap);
        }
        stmts[count++] = parse_statement(parser);
    }
    
    consume(parser, TOK_DEDENT, "Expect dedent after block.");
    
    Node** arena_stmts = (Node**)arena_alloc(parser->arena, sizeof(Node*) * count);
    memcpy(arena_stmts, stmts, sizeof(Node*) * count);
    free(stmts);
    
    *count_out = count;
    return (Node*)arena_stmts;
}

static Node* parse_if_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* cond = parse_expression(parser);
    consume(parser, TOK_COLON, "Expect ':' after condition.");
    
    int body_count = 0;
    Node** body = (Node**)parse_block(parser, &body_count);
    
    Node** elifs = NULL;
    int elif_count = 0;
    int elif_cap = 2;
    elifs = (Node**)malloc(sizeof(Node*) * elif_cap);
    
    while (match(parser, TOK_ELIF)) {
        int e_line = parser->previous.line;
        int e_col = parser->previous.col;
        Node* e_cond = parse_expression(parser);
        consume(parser, TOK_COLON, "Expect ':' after elif condition.");
        int e_body_count = 0;
        Node** e_body = (Node**)parse_block(parser, &e_body_count);
        
        Node* e_node = node_create(parser->arena, NODE_ELIF, e_line, e_col);
        e_node->elif_stmt.elif_cond = e_cond;
        e_node->elif_stmt.elif_body = e_body;
        e_node->elif_stmt.elif_body_count = e_body_count;
        
        if (elif_count >= elif_cap) {
            elif_cap *= 2;
            elifs = (Node**)realloc(elifs, sizeof(Node*) * elif_cap);
        }
        elifs[elif_count++] = e_node;
    }
    
    Node** else_body = NULL;
    int else_count = 0;
    if (match(parser, TOK_ELSE)) {
        consume(parser, TOK_COLON, "Expect ':' after else.");
        else_body = (Node**)parse_block(parser, &else_count);
    }
    
    Node* node = node_create(parser->arena, NODE_IF, line, col);
    node->if_stmt.if_cond = cond;
    node->if_stmt.if_body = body;
    node->if_stmt.if_body_count = body_count;
    
    node->if_stmt.elif_clauses = (Node**)arena_alloc(parser->arena, sizeof(Node*) * elif_count);
    memcpy(node->if_stmt.elif_clauses, elifs, sizeof(Node*) * elif_count);
    node->if_stmt.elif_count = elif_count;
    free(elifs);
    
    node->if_stmt.else_body = else_body;
    node->if_stmt.else_count = else_count;
    
    return node;
}

static Node* parse_while_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* cond = parse_expression(parser);
    consume(parser, TOK_COLON, "Expect ':' after condition.");
    
    int count = 0;
    Node** body = (Node**)parse_block(parser, &count);
    
    Node* node = node_create(parser->arena, NODE_WHILE, line, col);
    node->while_stmt.while_cond = cond;
    node->while_stmt.while_body = body;
    node->while_stmt.while_body_count = count;
    return node;
}

static Node* parse_for_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    consume(parser, TOK_IDENT, "Expect variable name after 'for'.");
    char* var_name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(var_name, parser->previous.start, parser->previous.length);
    var_name[parser->previous.length] = '\0';
    
    consume(parser, TOK_IN, "Expect 'in' after variable name.");
    
    Node* iter = parse_expression(parser);
    consume(parser, TOK_COLON, "Expect ':' after iterable.");
    
    int count = 0;
    Node** body = (Node**)parse_block(parser, &count);
    
    Node* node = node_create(parser->arena, NODE_FOR, line, col);
    node->for_stmt.for_var = var_name;
    node->for_stmt.for_iter = iter;
    node->for_stmt.for_body = body;
    node->for_stmt.for_body_count = count;
    return node;
}

static Node* parse_when_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* cond = parse_expression(parser);
    consume(parser, TOK_COLON, "Expect ':' after condition.");
    
    int body_count = 0;
    Node** body = (Node**)parse_block(parser, &body_count);
    
    Node** otherwise_body = NULL;
    int otherwise_count = 0;
    if (match(parser, TOK_OTHERWISE)) {
        consume(parser, TOK_COLON, "Expect ':' after otherwise.");
        otherwise_body = (Node**)parse_block(parser, &otherwise_count);
    }
    
    Node* node = node_create(parser->arena, NODE_WHEN, line, col);
    node->when_stmt.condition = cond;
    node->when_stmt.body = body;
    node->when_stmt.body_count = body_count;
    node->when_stmt.otherwise_body = otherwise_body;
    node->when_stmt.otherwise_count = otherwise_count;
    return node;
}

static Node* parse_repeat_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* count_expr = parse_expression(parser);
    
    consume(parser, TOK_AS, "Expect 'as' after repeat count.");
    consume(parser, TOK_IDENT, "Expect loop variable name.");
    
    char* var_name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(var_name, parser->previous.start, parser->previous.length);
    var_name[parser->previous.length] = '\0';
    
    consume(parser, TOK_COLON, "Expect ':' after repeat variable.");
    
    int body_count = 0;
    Node** body = (Node**)parse_block(parser, &body_count);
    
    Node* node = node_create(parser->arena, NODE_REPEAT, line, col);
    node->repeat_stmt.count_expr = count_expr;
    node->repeat_stmt.loop_var = var_name;
    node->repeat_stmt.body = body;
    node->repeat_stmt.body_count = body_count;
    return node;
}

static Node* parse_return_stmt(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    Node* value = NULL;
    if (!check(parser, TOK_NEWLINE)) {
        value = parse_expression(parser);
    }
    consume(parser, TOK_NEWLINE, "Expect newline after return value.");
    
    Node* node = node_create(parser->arena, NODE_RETURN, line, col);
    node->ret_value = value;
    return node;
}

static Node* parse_fn_def(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    consume(parser, TOK_IDENT, "Expect function name.");
    char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';
    
    consume(parser, TOK_LPAREN, "Expect '(' after function name.");
    
    Param* params = NULL;
    int param_count = 0;
    int cap = 4;
    params = (Param*)malloc(sizeof(Param) * cap);
    
    if (!check(parser, TOK_RPAREN)) {
        do {
            // Handle people accidentally writing 'im' or 'mut' in parameters
            if (match(parser, TOK_IM) || match(parser, TOK_MUT)) {
                // just ignore them for now to be forgiving
            }
            
            // To support C-style `int a` or Python-style `a: int` or just `a`,
            // we will just look for an identifier.
            consume(parser, TOK_IDENT, "Expect parameter name or type.");
            char* p_name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
            memcpy(p_name, parser->previous.start, parser->previous.length);
            p_name[parser->previous.length] = '\0';
            
            char* p_type = NULL;
            
            // If they wrote `int a` (C-style)
            if (check(parser, TOK_IDENT) && !check(parser, TOK_COLON)) {
                p_type = p_name; // the first ident was the type
                consume(parser, TOK_IDENT, "Expect parameter name.");
                p_name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
                memcpy(p_name, parser->previous.start, parser->previous.length);
                p_name[parser->previous.length] = '\0';
            } else if (match(parser, TOK_COLON)) {
                // Python style: `a: int`
                Node* type_node = parse_type_annotation(parser);
                p_type = type_node ? sk_strdup(type_node->name) : sk_strdup("unknown");
            } else {
                // No type specified: default to int
                p_type = sk_strdup("int");
            }
            
            if (param_count >= cap) {
                cap *= 2;
                params = (Param*)realloc(params, sizeof(Param) * cap);
            }
            params[param_count].name = p_name;
            params[param_count].type_name = p_type;
            param_count++;
            
        } while (match(parser, TOK_COMMA));
    }
    
    consume(parser, TOK_RPAREN, "Expect ')' after parameters.");
    
    char* ret_type = sk_strdup("unknown"); // Will be inferred or defaulted to none if unknown
    if (match(parser, TOK_ARROW)) {
        Node* rt_node = parse_type_annotation(parser);
        free(ret_type);
        ret_type = rt_node ? sk_strdup(rt_node->name) : sk_strdup("unknown");
    }
    
    consume(parser, TOK_COLON, "Expect ':' before function body.");
    
    int body_count = 0;
    Node** body = (Node**)parse_block(parser, &body_count);
    
    Node* node = node_create(parser->arena, NODE_FN_DEF, line, col);
    node->fn_def.fn_name = name;
    
    node->fn_def.fn_params = (Param*)arena_alloc(parser->arena, sizeof(Param) * param_count);
    memcpy(node->fn_def.fn_params, params, sizeof(Param) * param_count);
    node->fn_def.fn_param_count = param_count;
    free(params);
    
    node->fn_def.fn_return_type = ret_type; // Memory leak, should be arena allocated ideally, but ok for now
    node->fn_def.fn_body = body;
    node->fn_def.fn_body_count = body_count;
    
    return node;
}

static Node* parse_struct_def(Parser* parser) {
    int line = parser->previous.line;
    int col = parser->previous.col;
    
    consume(parser, TOK_IDENT, "Expect struct name.");
    char* name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
    memcpy(name, parser->previous.start, parser->previous.length);
    name[parser->previous.length] = '\0';
    
    consume(parser, TOK_COLON, "Expect ':' after struct name.");
    consume(parser, TOK_NEWLINE, "Expect newline before struct fields.");
    consume(parser, TOK_INDENT, "Expect indentation before struct fields.");
    
    Field* fields = NULL;
    int field_count = 0;
    int cap = 4;
    fields = (Field*)malloc(sizeof(Field) * cap);
    
    while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
        if (check(parser, TOK_NEWLINE)) {
            advance(parser);
            continue;
        }
        
        consume(parser, TOK_IDENT, "Expect field name.");
        char* f_name = (char*)arena_alloc(parser->arena, parser->previous.length + 1);
        memcpy(f_name, parser->previous.start, parser->previous.length);
        f_name[parser->previous.length] = '\0';
        
        consume(parser, TOK_COLON, "Expect ':' after field name.");
        Node* type_node = parse_type_annotation(parser);
        char* f_type = type_node ? sk_strdup(type_node->name) : sk_strdup("unknown");
        
        consume(parser, TOK_NEWLINE, "Expect newline after field declaration.");
        
        if (field_count >= cap) {
            cap *= 2;
            fields = (Field*)realloc(fields, sizeof(Field) * cap);
        }
        fields[field_count].name = f_name;
        fields[field_count].type_name = f_type;
        field_count++;
    }
    
    consume(parser, TOK_DEDENT, "Expect dedent after struct fields.");
    
    Node* node = node_create(parser->arena, NODE_STRUCT_DEF, line, col);
    node->struct_def.struct_name = name;
    
    node->struct_def.struct_fields = (Field*)arena_alloc(parser->arena, sizeof(Field) * field_count);
    memcpy(node->struct_def.struct_fields, fields, sizeof(Field) * field_count);
    node->struct_def.struct_field_count = field_count;
    free(fields);
    
    return node;
}

static Node* parse_expression_stmt(Parser* parser) {
    Node* expr = parse_expression(parser);
    
    if (match(parser, TOK_ASSIGN)) {
        Node* value = parse_expression(parser);
        consume(parser, TOK_NEWLINE, "Expect newline after assignment.");
        Node* node = node_create(parser->arena, NODE_ASSIGN, expr->line, expr->col);
        node->assign.assign_target = expr;
        node->assign.assign_value = value;
        return node;
    }
    
    consume(parser, TOK_NEWLINE, "Expect newline after expression.");
    Node* node = node_create(parser->arena, NODE_EXPR_STMT, expr->line, expr->col);
    node->expr = expr;
    return node;
}

static Node* parse_statement(Parser* parser) {
    if (match(parser, TOK_IM)) return parse_im_decl(parser, 0);
    if (match(parser, TOK_MUT)) return parse_im_decl(parser, 1);
    if (match(parser, TOK_OUT)) return parse_out_stmt(parser);
    if (match(parser, TOK_OUTT)) return parse_outt_stmt(parser);
    if (match(parser, TOK_OUTC)) return parse_outc_stmt(parser);
    if (match(parser, TOK_IF)) return parse_if_stmt(parser);
    if (match(parser, TOK_WHILE)) return parse_while_stmt(parser);
    if (match(parser, TOK_FOR)) return parse_for_stmt(parser);
    if (match(parser, TOK_WHEN)) return parse_when_stmt(parser);
    if (match(parser, TOK_REPEAT)) return parse_repeat_stmt(parser);
    if (match(parser, TOK_RETURN)) return parse_return_stmt(parser);
    if (match(parser, TOK_FN)) return parse_fn_def(parser);
    if (match(parser, TOK_STRUCT)) return parse_struct_def(parser);
    
    if (match(parser, TOK_BREAK)) {
        Node* n = node_create(parser->arena, NODE_BREAK, parser->previous.line, parser->previous.col);
        consume(parser, TOK_NEWLINE, "Expect newline.");
        return n;
    }
    if (match(parser, TOK_CONTINUE)) {
        Node* n = node_create(parser->arena, NODE_CONTINUE, parser->previous.line, parser->previous.col);
        consume(parser, TOK_NEWLINE, "Expect newline.");
        return n;
    }
    
    return parse_expression_stmt(parser);
}

void parser_init(Parser* parser, Lexer* lexer, Arena* arena) {
    parser->lexer = lexer;
    parser->arena = arena;
    parser->had_error = 0;
    parser->panic_mode = 0;
}

Node* parser_parse_program(Parser* parser) {
    advance(parser); // prime the pump
    
    Node** stmts = NULL;
    int count = 0;
    int cap = 16;
    stmts = (Node**)malloc(sizeof(Node*) * cap);
    
    while (!match(parser, TOK_EOF)) {
        if (check(parser, TOK_NEWLINE)) {
            advance(parser);
            continue;
        }
        
        if (count >= cap) {
            cap *= 2;
            stmts = (Node**)realloc(stmts, sizeof(Node*) * cap);
        }
        
        stmts[count++] = parse_statement(parser);
        
        if (parser->panic_mode) synchronize(parser);
    }
    
    Node* node = node_create(parser->arena, NODE_PROGRAM, 1, 1);
    node->program.stmts = (Node**)arena_alloc(parser->arena, sizeof(Node*) * count);
    memcpy(node->program.stmts, stmts, sizeof(Node*) * count);
    node->program.stmt_count = count;
    free(stmts);
    
    return node;
}
