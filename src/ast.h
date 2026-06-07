#ifndef SHANK_AST_H
#define SHANK_AST_H

#include "utils.h"
#include <stdint.h>

// --- Token Kinds ---
typedef enum {
    TOK_INT_LIT, TOK_FLOAT_LIT, TOK_STR_LIT, TOK_BOOL_LIT,
    TOK_IDENT,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT, TOK_POWER,
    TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LEQ, TOK_GEQ,
    TOK_ASSIGN, TOK_PLUS_ASSIGN, TOK_MINUS_ASSIGN, TOK_STAR_ASSIGN, TOK_SLASH_ASSIGN,
    TOK_ARROW,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COLON, TOK_COMMA, TOK_DOT, TOK_DOTDOT,
    TOK_IM, TOK_MUT, TOK_FN, TOK_STRUCT, TOK_ENUM,
    TOK_IN,
    TOK_MATCH, TOK_CASE,
    TOK_RETURN, TOK_BREAK, TOK_CONTINUE,
    TOK_IMPORT, TOK_TRUE, TOK_FALSE, TOK_NONE, TOK_OUT, TOK_OUTT, TOK_OUTC, TOK_PUT,
    TOK_WHEN, TOK_ELSEWHEN, TOK_OTHERWISE, TOK_REPEAT, TOK_AS,
    TOK_FSTR_START, TOK_FSTR_MID, TOK_FSTR_END, TOK_LBRACE, TOK_RBRACE,
    TOK_INDENT, TOK_DEDENT, TOK_NEWLINE, TOK_EOF, TOK_ERROR
} TokenKind;

// --- Node Kinds ---
typedef enum {
    NODE_INT_LIT, NODE_FLOAT_LIT, NODE_STR_LIT, NODE_BOOL_LIT,
    NODE_IDENT, NODE_BINARY, NODE_UNARY, NODE_CALL,
    NODE_FIELD_ACCESS, NODE_INDEX, NODE_ARRAY_LIT,
    NODE_IM, NODE_ASSIGN, NODE_RETURN, NODE_OUT, NODE_OUTT, NODE_OUTC, NODE_PUT,
    NODE_MATCH, NODE_CASE,
    NODE_EXPR_STMT, NODE_BREAK, NODE_CONTINUE,
    NODE_FN_DEF, NODE_STRUCT_DEF, NODE_ENUM_DEF,
    NODE_IMPORT, NODE_PROGRAM, NODE_INTERP_STR,
    NODE_WHEN, NODE_ELSEWHEN, NODE_OTHERWISE, NODE_REPEAT, NODE_CAST
} NodeKind;

typedef struct Node Node;

typedef struct {
    char* name;
    char* type_name;
} Param;

typedef struct {
    char* name;
    char* type_name;
} Field;

// --- AST Node ---
struct Node {
    NodeKind kind;
    int line;
    int col;
    
    // For type checker (populated in semantic phase)
    void* resolved_type;
    
    union {
        int64_t int_val;
        double float_val;
        struct { char* str_val; int str_len; };
        int bool_val;
        char* name;
        
        struct { Node* left; TokenKind op; Node* right; } binary;
        struct { TokenKind unary_op; Node* operand; } unary;
        struct { char* call_name; Node** call_args; int call_arg_count; } call;
        
        struct { Node* object; char* field; } field_access;
        struct { Node* object; Node* index; } index_access;
        struct { Node** elements; int count; } array_lit;
        struct { Node** exprs; int count; } interp_str;
        
        struct { char* let_name; Node* let_type; Node* let_value; int let_mutable; } let_decl;
        struct { Node* assign_target; Node* assign_value; } assign;
        Node* ret_value;
        
        struct { Node* cond; Node** body; int body_count; } elsewhen_stmt;
        
        Node* expr;
        
        struct { 
            char* fn_name; 
            Param* fn_params; int fn_param_count; 
            char* fn_return_type; 
            Node** fn_body; int fn_body_count; 
        } fn_def;
        
        struct { 
            char* struct_name; 
            Field* struct_fields; int struct_field_count; 
            Node** struct_methods; int struct_method_count; 
        } struct_def;
        
        struct { Node* prompt; } put_expr;
        
        struct {
            Node* expr;
            char* target_type;
        } cast_expr;
        
        struct {
            Node* condition;
            Node** body;
            int body_count;
            Node** elsewhen_clauses;
            int elsewhen_count;
            Node** otherwise_body;
            int otherwise_count;
        } when_stmt;
        
        struct {
            Node* count_expr;
            char* loop_var;
            Node** body;
            int body_count;
        } repeat_stmt;
        
        struct { Node** stmts; int stmt_count; } program;
    };
};

Node* node_create(Arena* arena, NodeKind kind, int line, int col);
void ast_print(Node* node, int indent);

#endif // SHANK_AST_H
