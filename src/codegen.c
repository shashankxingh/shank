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
        
        case NODE_INTERP_STR: {
            if (expr->interp_str.count == 0) {
                int lbl = cg->string_count++;
                emit_data(cg, "str_%d db 0", lbl);
                emit(cg, "lea rax, [rel str_%d]", lbl);
                break;
            }
            
            gen_expr(cg, expr->interp_str.exprs[0]);
            Type* t = (Type*)expr->interp_str.exprs[0]->resolved_type;
            if (t == type_int) {
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_int_to_cstr"); emit(cg, "add rsp, 32");
            } else if (t == type_float) {
                emit(cg, "movq rcx, xmm0");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_float_to_cstr"); emit(cg, "add rsp, 32");
            } else if (t == type_bool) {
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_bool_to_cstr"); emit(cg, "add rsp, 32");
            }
            
            emit(cg, "push r12");
            emit(cg, "sub rsp, 8"); // Align stack to 16 bytes
            emit(cg, "mov r12, rax");
            
            for (int i = 1; i < expr->interp_str.count; i++) {
                gen_expr(cg, expr->interp_str.exprs[i]);
                Type* sub_t = (Type*)expr->interp_str.exprs[i]->resolved_type;
                if (sub_t == type_int) {
                    emit(cg, "mov rcx, rax");
                    emit(cg, "sub rsp, 32"); emit(cg, "call sk_int_to_cstr"); emit(cg, "add rsp, 32");
                } else if (sub_t == type_float) {
                    emit(cg, "movq rcx, xmm0");
                    emit(cg, "sub rsp, 32"); emit(cg, "call sk_float_to_cstr"); emit(cg, "add rsp, 32");
                } else if (sub_t == type_bool) {
                    emit(cg, "mov rcx, rax");
                    emit(cg, "sub rsp, 32"); emit(cg, "call sk_bool_to_cstr"); emit(cg, "add rsp, 32");
                }
                
                emit(cg, "mov rcx, r12");
                emit(cg, "mov rdx, rax");
                
                emit(cg, "sub rsp, 32");
                emit(cg, "call sk_interp_concat");
                emit(cg, "add rsp, 32");
                
                emit(cg, "mov r12, rax");
            }
            
            emit(cg, "mov rax, r12");
            emit(cg, "add rsp, 8");
            emit(cg, "pop r12");
            break;
        }
        
        case NODE_PUT: {
            // First evaluate the prompt string (which puts string ptr in rax, length in r10)
            gen_expr(cg, expr->put_expr.prompt);
            
            // Pass string ptr to rcx and length to rdx (Windows x64 ABI)
            emit(cg, "mov rcx, rax");
            emit(cg, "mov rdx, r10");
            
            // Call the correct runtime function based on resolved type
            // Stack MUST be 16-byte aligned before call
            emit(cg, "push r12");
            emit(cg, "mov r12, rsp");
            emit(cg, "and rsp, -16"); // Align to 16 bytes
            emit(cg, "sub rsp, 32");  // Shadow space
            
            if (expr->resolved_type == type_int) {
                emit(cg, "call sk_input_int");
            } else if (expr->resolved_type == type_float) {
                emit(cg, "call sk_input_float");
                // The result is already in xmm0
            } else if (expr->resolved_type == type_bool) {
                emit(cg, "call sk_input_bool");
            } else {
                // Default to string
                emit(cg, "call sk_input_str");
                
                // sk_input_str returns char* in rax. We need length in r10.
                emit(cg, "push r13"); // preserve r13 (misaligns by 8)
                emit(cg, "sub rsp, 8"); // re-align to 16
                emit(cg, "mov r13, rax"); // save pointer
                emit(cg, "mov rcx, rax"); // pass to sk_cstr_len
                emit(cg, "sub rsp, 32");
                emit(cg, "call sk_cstr_len");
                emit(cg, "add rsp, 32");
                emit(cg, "mov r10, rax"); // length to r10
                emit(cg, "mov rax, r13"); // restore pointer
                emit(cg, "add rsp, 8");
                emit(cg, "pop r13");
            }
            
            emit(cg, "mov rsp, r12"); // Restore original unaligned stack
            emit(cg, "pop r12");
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
                case TOK_PERCENT:
                    emit(cg, "cqo");
                    emit(cg, "idiv rcx"); 
                    emit(cg, "mov rax, rdx"); // remainder in rdx
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
                case TOK_AND:
                    emit(cg, "and rax, rcx");
                    break;
                case TOK_OR:
                    emit(cg, "or rax, rcx");
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
        
        case NODE_CAST: {
            gen_expr(cg, expr->cast_expr.expr);
            Type* src_t = expr->cast_expr.expr->resolved_type;
            Type* target_t = expr->resolved_type;
            
            if (src_t == type_float && target_t == type_int) {
                emit(cg, "cvttsd2si rax, xmm0");
            } else if (src_t == type_int && target_t == type_float) {
                emit(cg, "cvtsi2sd xmm0, rax");
            } else if ((src_t == type_int || src_t == type_bool) && target_t == type_str) {
                // Simplified, doesn't really work perfectly without memory management
                // Just use the runtime functions
                if (src_t == type_int) {
                    emit(cg, "mov rcx, rax");
                    emit(cg, "sub rsp, 32"); emit(cg, "call sk_int_to_cstr"); emit(cg, "add rsp, 32");
                } else {
                    emit(cg, "mov rcx, rax");
                    emit(cg, "sub rsp, 32"); emit(cg, "call sk_bool_to_cstr"); emit(cg, "add rsp, 32");
                }
                // String result is in rax. Needs length in r10
                emit(cg, "push rax");
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_cstr_len"); emit(cg, "add rsp, 32");
                emit(cg, "mov r10, rax");
                emit(cg, "pop rax");
            } else if (src_t == type_float && target_t == type_str) {
                emit(cg, "movq rcx, xmm0");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_float_to_cstr"); emit(cg, "add rsp, 32");
                
                emit(cg, "push rax");
                emit(cg, "mov rcx, rax");
                emit(cg, "sub rsp, 32"); emit(cg, "call sk_cstr_len"); emit(cg, "add rsp, 32");
                emit(cg, "mov r10, rax");
                emit(cg, "pop rax");
            }
            // Add string parsing to int/float later
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

        case NODE_OUTT: {
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
            break;
        }
        
        case NODE_OUTC: {
            gen_expr(cg, stmt->expr);
            emit(cg, "mov rcx, rax");
            emit(cg, "sub rsp, 32");
            emit(cg, "call sk_print_char");
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

        case NODE_WHEN: {
            int lbl_end = next_label(cg);
            
            // Generate labels for elsewhens
            int* elsewhen_labels = (int*)malloc(sizeof(int) * stmt->when_stmt.elsewhen_count);
            for (int i = 0; i < stmt->when_stmt.elsewhen_count; i++) {
                elsewhen_labels[i] = next_label(cg);
            }
            
            int lbl_otherwise = next_label(cg);
            
            // First condition
            gen_expr(cg, stmt->when_stmt.condition);
            emit(cg, "cmp rax, 0");
            if (stmt->when_stmt.elsewhen_count > 0) {
                emit(cg, "je .L_elsewhen_%d", elsewhen_labels[0]);
            } else {
                emit(cg, "je .L_otherwise_%d", lbl_otherwise);
            }
            
            scope_push(cg->symbols);
            for (int i = 0; i < stmt->when_stmt.body_count; i++) {
                gen_stmt(cg, stmt->when_stmt.body[i]);
            }
            scope_pop(cg->symbols);
            emit(cg, "jmp .L_end_%d", lbl_end);
            
            for (int i = 0; i < stmt->when_stmt.elsewhen_count; i++) {
                emit(cg, ".L_elsewhen_%d:", elsewhen_labels[i]);
                Node* e_node = stmt->when_stmt.elsewhen_clauses[i];
                
                gen_expr(cg, e_node->elsewhen_stmt.cond);
                emit(cg, "cmp rax, 0");
                
                if (i + 1 < stmt->when_stmt.elsewhen_count) {
                    emit(cg, "je .L_elsewhen_%d", elsewhen_labels[i+1]);
                } else {
                    emit(cg, "je .L_otherwise_%d", lbl_otherwise);
                }
                
                scope_push(cg->symbols);
                for (int j = 0; j < e_node->elsewhen_stmt.body_count; j++) {
                    gen_stmt(cg, e_node->elsewhen_stmt.body[j]);
                }
                scope_pop(cg->symbols);
                emit(cg, "jmp .L_end_%d", lbl_end);
            }
            
            free(elsewhen_labels);
            
            emit(cg, ".L_otherwise_%d:", lbl_otherwise);
            if (stmt->when_stmt.otherwise_body) {
                scope_push(cg->symbols);
                for (int i = 0; i < stmt->when_stmt.otherwise_count; i++) {
                    gen_stmt(cg, stmt->when_stmt.otherwise_body[i]);
                }
                scope_pop(cg->symbols);
            }
            
            emit(cg, ".L_end_%d:", lbl_end);
            break;
        }
        
        case NODE_REPEAT: {
            int lbl_start = next_label(cg);
            int lbl_end = next_label(cg);
            
            // Evaluate count expression
            gen_expr(cg, stmt->repeat_stmt.count_expr);
            
            // Store count in loop variable
            Symbol* sym = symbol_lookup(cg->symbols, stmt->repeat_stmt.loop_var);
            if (!sym) {
                // Just in case it wasn't registered in scope correctly, register it
                sym = symbol_define(cg->symbols, stmt->repeat_stmt.loop_var, type_int, 0);
            }
            cg->stack_offset += 8;
            sym->stack_offset = cg->stack_offset;
            
            // Initialize counter to 0
            emit(cg, "mov qword [rbp - %d], 0", sym->stack_offset);
            
            // Save the max count in another stack location
            cg->stack_offset += 8;
            int limit_offset = cg->stack_offset;
            emit(cg, "mov [rbp - %d], rax", limit_offset);
            
            emit(cg, ".L_repeat_start_%d:", lbl_start);
            
            // Check condition: i < limit
            emit(cg, "mov rax, [rbp - %d]", sym->stack_offset);
            emit(cg, "cmp rax, [rbp - %d]", limit_offset);
            emit(cg, "jge .L_repeat_end_%d", lbl_end);
            
            scope_push(cg->symbols);
            for (int i = 0; i < stmt->repeat_stmt.body_count; i++) {
                gen_stmt(cg, stmt->repeat_stmt.body[i]);
            }
            scope_pop(cg->symbols);
            
            // Increment counter
            emit(cg, "mov rax, [rbp - %d]", sym->stack_offset);
            emit(cg, "inc rax");
            emit(cg, "mov [rbp - %d], rax", sym->stack_offset);
            
            emit(cg, "jmp .L_repeat_start_%d", lbl_start);
            emit(cg, ".L_repeat_end_%d:", lbl_end);
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
            int fn_rsp_patch = cg->out->len;
            emit(cg, "sub rsp, 000000"); // 6 digits for patching
            
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
            
            int alloc_size = cg->stack_offset;
            if (alloc_size % 16 != 0) alloc_size += 16 - (alloc_size % 16);
            char buf[16];
            snprintf(buf, sizeof(buf), "%06d", alloc_size);
            memcpy(cg->out->data + fn_rsp_patch + 13, buf, 6);
            
            cg->stack_offset = saved_offset;
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
    emit(&cg, "extern sk_print_char");
    emit(&cg, "extern sk_print_float");
    emit(&cg, "extern sk_print_str");
    emit(&cg, "extern sk_print_bool");
    emit(&cg, "extern sk_print_newline");
    emit(&cg, "extern sk_str_concat");
    emit(&cg, "extern sk_int_to_cstr");
    emit(&cg, "extern sk_float_to_cstr");
    emit(&cg, "extern sk_bool_to_cstr");
    emit(&cg, "extern sk_interp_concat");
    emit(&cg, "extern sk_alloc");
    emit(&cg, "extern sk_free");
    emit(&cg, "extern sk_input_int");
    emit(&cg, "extern sk_input_float");
    emit(&cg, "extern sk_input_str");
    emit(&cg, "extern sk_input_bool");
    emit(&cg, "extern sk_cstr_len");
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
    int main_rsp_patch = cg.out->len;
    emit(&cg, "sub rsp, 000000"); // space for locals
    
    for (int i = 0; i < program->program.stmt_count; i++) {
        if (program->program.stmts[i]->kind != NODE_FN_DEF && program->program.stmts[i]->kind != NODE_STRUCT_DEF) {
            gen_stmt(&cg, program->program.stmts[i]);
        }
    }
    
    int main_alloc_size = cg.stack_offset;
    if (main_alloc_size % 16 != 0) main_alloc_size += 16 - (main_alloc_size % 16);
    char buf2[16];
    snprintf(buf2, sizeof(buf2), "%06d", main_alloc_size);
    memcpy(cg.out->data + main_rsp_patch + 13, buf2, 6);
    
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
