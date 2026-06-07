#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    StringBuilder* out;
    StringBuilder* data_section;
    int label_count;
    int string_count;
    int stack_offset;
    Checker* checker;
    SymbolTable* symbols;
} Codegen;

static void gen_stmt(Codegen* cg, Node* stmt);
static void gen_expr(Codegen* cg, Node* expr);

void codegen_init(void) {
    // any global init
}

static int next_label(Codegen* cg) {
    return cg->label_count++;
}

static void emit(Codegen* cg, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // Add indentation unless it's a label
    if (strchr(fmt, ':') == NULL && fmt[0] != ';') {
        sb_append(cg->out, "    ");
    }
    
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (len >= 0) {
        char* buf = (char*)malloc(len + 1);
        vsnprintf(buf, len + 1, fmt, args);
        sb_append(cg->out, buf);
        free(buf);
    }
    sb_append(cg->out, "\n");
    va_end(args);
}

static void emit_data(Codegen* cg, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    if (strchr(fmt, ':') == NULL) {
        sb_append(cg->data_section, "    ");
    }
    
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (len >= 0) {
        char* buf = (char*)malloc(len + 1);
        vsnprintf(buf, len + 1, fmt, args);
        sb_append(cg->data_section, buf);
        free(buf);
    }
    sb_append(cg->data_section, "\n");
    va_end(args);
}

static void gen_expr(Codegen* cg, Node* expr) {
    if (!expr) return;
    
    switch (expr->kind) {
        case NODE_INT_LIT:
            emit(cg, "mov rax, %lld", (long long)expr->int_val);
            break;
            
        case NODE_FLOAT_LIT: {
            int lbl = next_label(cg);
            emit_data(cg, "flt_%d dq %f", lbl, expr->float_val);
            emit(cg, "movsd xmm0, [rel flt_%d]", lbl);
            break;
        }
        
        case NODE_STR_LIT: {
            int lbl = cg->string_count++;
            emit_data(cg, "str_%d db \"%s\", 0", lbl, expr->str_val);
            
            // Fat pointer structure on stack? No, for expressions, we can pass pointer in rcx and len in rdx
            // Or return fat pointer in rax (ptr) and rdx (len).
            // For now, simplify to just returning pointer in rax, and we'll handle length dynamically or statically
            emit(cg, "lea rax, [rel str_%d]", lbl);
            emit(cg, "mov r10, %d", expr->str_len); // r10 holds len for now
            break;
        }
        
        case NODE_BOOL_LIT:
            emit(cg, "mov rax, %d", expr->bool_val);
            break;
            
        case NODE_IDENT: {
            Symbol* sym = symbol_lookup(cg->symbols, expr->name);
            if (sym) {
                if (sym->type == type_float) {
                    emit(cg, "movsd xmm0, [rbp - %d]", sym->stack_offset);
                } else {
                    emit(cg, "mov rax, [rbp - %d]", sym->stack_offset);
                }
            }
            break;
        }
        
        case NODE_BINARY: {
            // Very simplified: evaluate right, push, evaluate left, pop right, operate
            // Doesn't handle float vs int properly in this simple version
            gen_expr(cg, expr->binary.right);
            emit(cg, "push rax");
            gen_expr(cg, expr->binary.left);
            emit(cg, "pop rcx"); // right is in rcx, left is in rax
            
            switch (expr->binary.op) {
                case TOK_PLUS: emit(cg, "add rax, rcx"); break;
                case TOK_MINUS: emit(cg, "sub rax, rcx"); break;
                case TOK_STAR: emit(cg, "imul rax, rcx"); break;
                case TOK_SLASH: 
                    emit(cg, "cqo");
                    emit(cg, "idiv rcx"); // result in rax
                    break;
                case TOK_EQ:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "sete al");
                    emit(cg, "movzx rax, al");
                    break;
                case TOK_NEQ:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "setne al");
                    emit(cg, "movzx rax, al");
                    break;
                case TOK_LT:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "setl al");
                    emit(cg, "movzx rax, al");
                    break;
                case TOK_GT:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "setg al");
                    emit(cg, "movzx rax, al");
                    break;
                case TOK_LEQ:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "setle al");
                    emit(cg, "movzx rax, al");
                    break;
                case TOK_GEQ:
                    emit(cg, "cmp rax, rcx");
                    emit(cg, "setge al");
                    emit(cg, "movzx rax, al");
                    break;
                default: break;
            }
            break;
        }
        
        case NODE_CALL: {
            if (strcmp(expr->call.call_name, "print") == 0 || strcmp(expr->call.call_name, "print_int") == 0) {
                if (expr->call.call_arg_count > 0) {
                    Node* arg = expr->call.call_args[0];
                    gen_expr(cg, arg);
                    
                    if (arg->resolved_type == type_str) {
                        emit(cg, "mov rcx, rax"); // pointer
                        emit(cg, "mov rdx, r10"); // length
                        emit(cg, "sub rsp, 32"); // shadow space
                        emit(cg, "call sk_print_str");
                        emit(cg, "add rsp, 32");
                    } else if (arg->resolved_type == type_float) {
                        emit(cg, "movq rcx, xmm0"); // pass float via xmm0 -> rcx for our print_float stub
                        emit(cg, "sub rsp, 32");
                        emit(cg, "call sk_print_float");
                        emit(cg, "add rsp, 32");
                    } else if (arg->resolved_type == type_bool) {
                        emit(cg, "mov rcx, rax");
                        emit(cg, "sub rsp, 32");
                        emit(cg, "call sk_print_bool");
                        emit(cg, "add rsp, 32");
                    } else {
                        // default int
                        emit(cg, "mov rcx, rax");
                        emit(cg, "sub rsp, 32");
                        emit(cg, "call sk_print_int");
                        emit(cg, "add rsp, 32");
                    }
                    emit(cg, "sub rsp, 32");
                    emit(cg, "call sk_print_newline");
                    emit(cg, "add rsp, 32");
                }
            } else {
                // Regular function call (Win64 ABI)
                // Evaluate args and put in rcx, rdx, r8, r9 (max 4 args for now to keep simple)
                for (int i = 0; i < expr->call.call_arg_count; i++) {
                    gen_expr(cg, expr->call.call_args[i]);
                    // Push to stack temporarily
                    emit(cg, "push rax");
                }
                
                // Pop into correct registers in reverse
                for (int i = expr->call.call_arg_count - 1; i >= 0; i--) {
                    if (i == 3) emit(cg, "pop r9");
                    else if (i == 2) emit(cg, "pop r8");
                    else if (i == 1) emit(cg, "pop rdx");
                    else if (i == 0) emit(cg, "pop rcx");
                }
                
                emit(cg, "sub rsp, 32"); // shadow space
                emit(cg, "call sk_%s", expr->call.call_name);
                emit(cg, "add rsp, 32");
            }
            break;
        }
        
        default:
            emit(cg, "; TODO expr kind %d", expr->kind);
            break;
    }
}

static void gen_stmt(Codegen* cg, Node* stmt) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case NODE_EXPR_STMT:
            gen_expr(cg, stmt->expr);
            break;
            
        case NODE_IM: {
            Symbol* sym = symbol_define(cg->symbols, stmt->let_decl.let_name, stmt->resolved_type, stmt->let_decl.let_mutable);
            if (sym) {
                cg->stack_offset += 8; // allocate 8 bytes for variable
                sym->stack_offset = cg->stack_offset;
                
                if (stmt->let_decl.let_value) {
                    gen_expr(cg, stmt->let_decl.let_value);
                    if (sym->type == type_float) {
                        emit(cg, "movsd [rbp - %d], xmm0", sym->stack_offset);
                    } else {
                        emit(cg, "mov [rbp - %d], rax", sym->stack_offset);
                    }
                }
            }
            break;
        }
        
        case NODE_OUT: {
            gen_expr(cg, stmt->expr);
            if (stmt->expr->resolved_type == type_str) {
                emit(cg, "mov rcx, rax"); // pointer
                emit(cg, "mov rdx, r10"); // length
                emit(cg, "sub rsp, 32"); // shadow space
                emit(cg, "call sk_print_str");
                emit(cg, "add rsp, 32");
            } else if (stmt->expr->resolved_type == type_float) {
                emit(cg, "movq rcx, xmm0");
                emit(cg, "sub rsp, 32");
                emit(cg, "call sk_print_float");
                emit(cg, "add rsp, 32");
            } else if (stmt->expr->resolved_type == type_bool) {
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32");
                emit(cg, "call sk_print_bool");
                emit(cg, "add rsp, 32");
            } else {
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32");
                emit(cg, "call sk_print_int");
                emit(cg, "add rsp, 32");
            }
            emit(cg, "sub rsp, 32");
            emit(cg, "call sk_print_newline");
            emit(cg, "add rsp, 32");
            break;
        }
        
        case NODE_ASSIGN: {
            if (stmt->assign.assign_target->kind == NODE_IDENT) {
                Symbol* sym = symbol_lookup(cg->symbols, stmt->assign.assign_target->name);
                if (sym) {
                    gen_expr(cg, stmt->assign.assign_value);
                    if (sym->type == type_float) {
                        emit(cg, "movsd [rbp - %d], xmm0", sym->stack_offset);
                    } else {
                        emit(cg, "mov [rbp - %d], rax", sym->stack_offset);
                    }
                }
            }
            break;
        }
        
        case NODE_IF: {
            int lbl_else = next_label(cg);
            int lbl_end = next_label(cg);
            
            gen_expr(cg, stmt->if_stmt.if_cond);
            emit(cg, "cmp rax, 0");
            emit(cg, "je .L_else_%d", lbl_else);
            
            scope_push(cg->symbols);
            for (int i = 0; i < stmt->if_stmt.if_body_count; i++) {
                gen_stmt(cg, stmt->if_stmt.if_body[i]);
            }
            scope_pop(cg->symbols);
            emit(cg, "jmp .L_end_%d", lbl_end);
            
            emit(cg, ".L_else_%d:", lbl_else);
            // Elifs omitted for brevity
            if (stmt->if_stmt.else_body) {
                scope_push(cg->symbols);
                for (int i = 0; i < stmt->if_stmt.else_count; i++) {
                    gen_stmt(cg, stmt->if_stmt.else_body[i]);
                }
                scope_pop(cg->symbols);
            }
            
            emit(cg, ".L_end_%d:", lbl_end);
            break;
        }
        
        case NODE_WHILE: {
            int lbl_start = next_label(cg);
            int lbl_end = next_label(cg);
            
            emit(cg, ".L_while_start_%d:", lbl_start);
            gen_expr(cg, stmt->while_stmt.while_cond);
            emit(cg, "cmp rax, 0");
            emit(cg, "je .L_while_end_%d", lbl_end);
            
            scope_push(cg->symbols);
            for (int i = 0; i < stmt->while_stmt.while_body_count; i++) {
                gen_stmt(cg, stmt->while_stmt.while_body[i]);
            }
            scope_pop(cg->symbols);
            
            emit(cg, "jmp .L_while_start_%d", lbl_start);
            emit(cg, ".L_while_end_%d:", lbl_end);
            break;
        }
        
        case NODE_RETURN: {
            if (stmt->ret_value) {
                gen_expr(cg, stmt->ret_value);
            }
            emit(cg, "mov rsp, rbp");
            emit(cg, "pop rbp");
            emit(cg, "ret");
            break;
        }
        
        case NODE_FN_DEF: {
            emit(cg, "");
            emit(cg, "global sk_%s", stmt->fn_def.fn_name);
            emit(cg, "sk_%s:", stmt->fn_def.fn_name);
            emit(cg, "push rbp");
            emit(cg, "mov rbp, rsp");
            
            // Local variable allocation space
            emit(cg, "sub rsp, 256"); // Preallocate for v0.1
            
            int saved_offset = cg->stack_offset;
            cg->stack_offset = 0;
            
            // Move args from registers to stack
            scope_push(cg->symbols);
            for (int i = 0; i < stmt->fn_def.fn_param_count; i++) {
                cg->stack_offset += 8;
                Symbol* sym = symbol_define(cg->symbols, stmt->fn_def.fn_params[i].name, type_int, 0);
                if (sym) sym->stack_offset = cg->stack_offset;
                
                if (i == 0) emit(cg, "mov [rbp - %d], rcx", cg->stack_offset);
                else if (i == 1) emit(cg, "mov [rbp - %d], rdx", cg->stack_offset);
                else if (i == 2) emit(cg, "mov [rbp - %d], r8", cg->stack_offset);
                else if (i == 3) emit(cg, "mov [rbp - %d], r9", cg->stack_offset);
            }
            
            for (int i = 0; i < stmt->fn_def.fn_body_count; i++) {
                gen_stmt(cg, stmt->fn_def.fn_body[i]);
            }
            scope_pop(cg->symbols);
            
            // Default return if no explicit return
            emit(cg, "xor eax, eax");
            emit(cg, "mov rsp, rbp");
            emit(cg, "pop rbp");
            emit(cg, "ret");
            
            cg->stack_offset = saved_offset;
            break;
        }
        
        default:
            break;
    }
}

char* codegen_generate(Checker* checker, Node* program) {
    if (!program || program->kind != NODE_PROGRAM) return NULL;
    
    Codegen cg = {0};
    cg.out = sb_init();
    cg.data_section = sb_init();
    cg.checker = checker;
    cg.symbols = symbol_table_create(checker->arena);
    cg.stack_offset = 0;
    
    emit(&cg, "bits 64");
    emit(&cg, "default rel\n");
    
    // External functions from our runtime
    emit(&cg, "extern sk_print_int");
    emit(&cg, "extern sk_print_float");
    emit(&cg, "extern sk_print_str");
    emit(&cg, "extern sk_print_bool");
    emit(&cg, "extern sk_print_newline");
    emit(&cg, "extern sk_alloc");
    emit(&cg, "extern sk_free");
    emit(&cg, "extern ExitProcess\n");
    
    emit(&cg, "section .text\n");
    
    // Generate code for all functions first
    for (int i = 0; i < program->program.stmt_count; i++) {
        if (program->program.stmts[i]->kind == NODE_FN_DEF) {
            gen_stmt(&cg, program->program.stmts[i]);
        }
    }
    
    // Generate main function
    emit(&cg, "\nglobal shank_main");
    emit(&cg, "shank_main:");
    emit(&cg, "push rbp");
    emit(&cg, "mov rbp, rsp");
    emit(&cg, "sub rsp, 256"); // space for locals
    
    for (int i = 0; i < program->program.stmt_count; i++) {
        if (program->program.stmts[i]->kind != NODE_FN_DEF && program->program.stmts[i]->kind != NODE_STRUCT_DEF) {
            gen_stmt(&cg, program->program.stmts[i]);
        }
    }
    
    // Exit
    emit(&cg, "mov rcx, 0");
    emit(&cg, "call ExitProcess");
    emit(&cg, "mov rsp, rbp");
    emit(&cg, "pop rbp");
    emit(&cg, "ret\n");
    
    StringBuilder* final_asm = sb_init();
    sb_append(final_asm, sb_to_string(cg.out));
    
    if (cg.data_section->len > 0) {
        sb_append(final_asm, "\nsection .data\n");
        sb_append(final_asm, sb_to_string(cg.data_section));
    }
    
    char* result = sb_to_string(final_asm);
    
    sb_free(cg.out);
    sb_free(cg.data_section);
    sb_free(final_asm);
    
    return result;
}

int codegen_emit_to_file(const char* asm_string, const char* filename) {
    if (!asm_string || !filename) return 0;
    
    FILE* file = fopen(filename, "w");
    if (!file) return 0;
    
    fprintf(file, "%s", asm_string);
    fclose(file);
    return 1;
}
