#include "ast.h"
#include <stdio.h>
#include <string.h>

Node* node_create(Arena* arena, NodeKind kind, int line, int col) {
    Node* node = (Node*)arena_alloc(arena, sizeof(Node));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->line = line;
    node->col = col;
    node->resolved_type = NULL;
    
    return node;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

void ast_print(Node* node, int indent) {
    if (!node) return;
    
    print_indent(indent);
    
    switch (node->kind) {
        case NODE_INT_LIT:
            printf("IntLit(%lld)\n", (long long)node->int_val);
            break;
        case NODE_FLOAT_LIT:
            printf("FloatLit(%f)\n", node->float_val);
            break;
        case NODE_STR_LIT:
            printf("StrLit(\"%.*s\")\n", node->str_len, node->str_val);
            break;
        case NODE_BOOL_LIT:
            printf("BoolLit(%s)\n", node->bool_val ? "true" : "false");
            break;
        case NODE_IDENT:
            printf("Ident(%s)\n", node->name);
            break;
        case NODE_BINARY:
            printf("BinaryOp(op:%d)\n", node->binary.op);
            ast_print(node->binary.left, indent + 1);
            ast_print(node->binary.right, indent + 1);
            break;
        case NODE_UNARY:
            printf("UnaryOp(op:%d)\n", node->unary.unary_op);
            ast_print(node->unary.operand, indent + 1);
            break;
        case NODE_CALL:
            printf("Call(%s)\n", node->call.call_name);
            for (int i = 0; i < node->call.call_arg_count; i++) {
                ast_print(node->call.call_args[i], indent + 1);
            }
            break;
        case NODE_IM:
            printf("LetDecl(%s", node->let_decl.let_name);
            if (node->let_decl.let_type) {
                printf(": ");
                ast_print(node->let_decl.let_type, 0);
            }
            if (node->let_decl.let_value) {
                printf(" = ");
                ast_print(node->let_decl.let_value, 0);
            }
            printf(")\n");
            break;
        case NODE_OUT:
            printf("OutStmt(");
            ast_print(node->expr, 0);
            printf(")\n");
            break;
        case NODE_OUTT:
            printf("OuttStmt(");
            ast_print(node->expr, 0);
            printf(")\n");
            break;
        case NODE_ASSIGN:
            printf("Assign\n");
            ast_print(node->assign.assign_target, indent + 1);
            ast_print(node->assign.assign_value, indent + 1);
            break;
        case NODE_RETURN:
            printf("Return\n");
            if (node->ret_value) ast_print(node->ret_value, indent + 1);
            break;
        case NODE_IF:
            printf("If\n");
            ast_print(node->if_stmt.if_cond, indent + 1);
            for (int i = 0; i < node->if_stmt.if_body_count; i++) {
                ast_print(node->if_stmt.if_body[i], indent + 1);
            }
            break;
        case NODE_WHILE:
            printf("While\n");
            ast_print(node->while_stmt.while_cond, indent + 1);
            for (int i = 0; i < node->while_stmt.while_body_count; i++) {
                ast_print(node->while_stmt.while_body[i], indent + 1);
            }
            break;
        case NODE_FN_DEF:
            printf("FnDef(%s)\n", node->fn_def.fn_name);
            for (int i = 0; i < node->fn_def.fn_body_count; i++) {
                ast_print(node->fn_def.fn_body[i], indent + 1);
            }
            break;
        case NODE_PROGRAM:
            printf("Program\n");
            for (int i = 0; i < node->program.stmt_count; i++) {
                ast_print(node->program.stmts[i], indent + 1);
            }
            break;
        case NODE_INTERP_STR:
            printf("InterpStr\n");
            for (int i = 0; i < node->interp_str.count; i++) {
                ast_print(node->interp_str.exprs[i], indent + 1);
            }
            break;
        default:
            printf("NodeKind(%d)\n", node->kind);
            break;
    }
}
