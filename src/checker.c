#include "checker.h"
#include "errors.h"
#include <string.h>
#include <stdlib.h>

void checker_init(Checker* checker, Arena* arena) {
    checker->arena = arena;
    checker->symbols = symbol_table_create(arena);
    checker->current_function_return_type = NULL;
    checker->had_error = 0;
    
    // Add builtin functions to top level scope
    Type* print_int_t = type_create(arena, TYPE_FUNCTION);
    print_int_t->func.param_count = 1;
    print_int_t->func.param_types = (Type**)arena_alloc(arena, sizeof(Type*));
    print_int_t->func.param_types[0] = type_int;
    print_int_t->func.return_type = type_none;
    symbol_define(checker->symbols, "print_int", print_int_t, 0);
    
    Type* print_float_t = type_create(arena, TYPE_FUNCTION);
    print_float_t->func.param_count = 1;
    print_float_t->func.param_types = (Type**)arena_alloc(arena, sizeof(Type*));
    print_float_t->func.param_types[0] = type_float;
    print_float_t->func.return_type = type_none;
    symbol_define(checker->symbols, "print_float", print_float_t, 0);

    // Simplification for v0.1: just 'print' that we'll lower in codegen depending on type
    Type* print_t = type_create(arena, TYPE_FUNCTION);
    print_t->func.param_count = 1;
    print_t->func.param_types = (Type**)arena_alloc(arena, sizeof(Type*));
    print_t->func.param_types[0] = type_unknown; // polymorphic
    print_t->func.return_type = type_none;
    symbol_define(checker->symbols, "print", print_t, 0);
    
    Type* range_t = type_create(arena, TYPE_FUNCTION);
    range_t->func.param_count = 1;
    range_t->func.param_types = (Type**)arena_alloc(arena, sizeof(Type*));
    range_t->func.param_types[0] = type_int; // stop
    // Return type is array for now
    Type* arr_t = type_create(arena, TYPE_ARRAY);
    arr_t->array.elem_type = type_int;
    range_t->func.return_type = arr_t;
    symbol_define(checker->symbols, "range", range_t, 0);
}

static Type* check_expr(Checker* checker, Node* expr);
static void check_stmt(Checker* checker, Node* stmt);

static Type* lookup_type_by_name(Checker* checker, const char* name) {
    if (strcmp(name, "int") == 0) return type_int;
    if (strcmp(name, "float") == 0) return type_float;
    if (strcmp(name, "bool") == 0) return type_bool;
    if (strcmp(name, "str") == 0) return type_str;
    if (strcmp(name, "none") == 0) return type_none;
    
    Symbol* sym = symbol_lookup(checker->symbols, name);
    if (sym && sym->type->kind == TYPE_STRUCT) return sym->type;
    
    return type_unknown;
}

static Type* check_expr(Checker* checker, Node* expr) {
    if (!expr) return type_unknown;
    
    Type* type = type_unknown;
    
    switch (expr->kind) {
        case NODE_INT_LIT: type = type_int; break;
        case NODE_FLOAT_LIT: type = type_float; break;
        case NODE_STR_LIT: type = type_str; break;
        case NODE_INTERP_STR: {
            for (int i = 0; i < expr->interp_str.count; i++) {
                check_expr(checker, expr->interp_str.exprs[i]);
            }
            type = type_str;
            break;
        }
        case NODE_BOOL_LIT: type = type_bool; break;
        
        case NODE_IDENT: {
            Symbol* sym = symbol_lookup(checker->symbols, expr->name);
            if (!sym) {
                sk_error(NULL, expr->line, expr->col, "Undefined variable '%s'.", expr->name);
                checker->had_error = 1;
            } else {
                type = sym->type;
            }
            break;
        }
        
        case NODE_BINARY: {
            Type* left_t = check_expr(checker, expr->binary.left);
            Type* right_t = check_expr(checker, expr->binary.right);
            
            if (left_t == type_unknown || right_t == type_unknown) break;
            
            TokenKind op = expr->binary.op;
            
            if (op == TOK_EQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT || op == TOK_LEQ || op == TOK_GEQ) {
                if (!type_equals(left_t, right_t)) {
                    char* lt = type_to_string(left_t);
                    char* rt = type_to_string(right_t);
                    sk_error(NULL, expr->line, expr->col, "Type mismatch in comparison: '%s' and '%s'.", lt, rt);
                    free(lt); free(rt);
                    checker->had_error = 1;
                }
                type = type_bool;
            } else if (op == TOK_AND || op == TOK_OR) {
                if (left_t != type_bool || right_t != type_bool) {
                    sk_error(NULL, expr->line, expr->col, "Logical operators require bool operands.");
                    checker->had_error = 1;
                }
                type = type_bool;
            } else {
                // Arithmetic
                if (left_t == type_int && right_t == type_int) type = type_int;
                else if (left_t == type_float && right_t == type_float) type = type_float;
                else if (left_t == type_int && right_t == type_float) type = type_float;
                else if (left_t == type_float && right_t == type_int) type = type_float;
                else if (left_t == type_str && right_t == type_str && op == TOK_PLUS) type = type_str;
                else {
                    char* lt = type_to_string(left_t);
                    char* rt = type_to_string(right_t);
                    sk_error(NULL, expr->line, expr->col, "Invalid operands to binary operator: '%s' and '%s'.", lt, rt);
                    free(lt); free(rt);
                    checker->had_error = 1;
                }
            }
            break;
        }
        
        case NODE_UNARY: {
            Type* operand_t = check_expr(checker, expr->unary.operand);
            if (operand_t == type_unknown) break;
            
            if (expr->unary.unary_op == TOK_NOT) {
                if (operand_t != type_bool) {
                    sk_error(NULL, expr->line, expr->col, "Operand to 'not' must be bool.");
                    checker->had_error = 1;
                }
                type = type_bool;
            } else if (expr->unary.unary_op == TOK_MINUS) {
                if (operand_t != type_int && operand_t != type_float) {
                    sk_error(NULL, expr->line, expr->col, "Operand to '-' must be numeric.");
                    checker->had_error = 1;
                }
                type = operand_t;
            }
            break;
        }
        
        case NODE_CALL: {
            Symbol* sym = symbol_lookup(checker->symbols, expr->call.call_name);
            if (!sym) {
                sk_error(NULL, expr->line, expr->col, "Undefined function '%s'.", expr->call.call_name);
                checker->had_error = 1;
                break;
            }
            
            if (sym->type->kind != TYPE_FUNCTION) {
                sk_error(NULL, expr->line, expr->col, "'%s' is not callable.", expr->call.call_name);
                checker->had_error = 1;
                break;
            }
            
            Type* func_t = sym->type;
            
            if (strcmp(expr->call.call_name, "print") == 0) {
                // Special polymorphic print
                for (int i = 0; i < expr->call.call_arg_count; i++) {
                    check_expr(checker, expr->call.call_args[i]);
                }
                type = type_none;
            } else {
                if (expr->call.call_arg_count != func_t->func.param_count) {
                    sk_error(NULL, expr->line, expr->col, "Expected %d arguments to '%s', got %d.", 
                             func_t->func.param_count, expr->call.call_name, expr->call.call_arg_count);
                    checker->had_error = 1;
                } else {
                    for (int i = 0; i < expr->call.call_arg_count; i++) {
                        Type* arg_t = check_expr(checker, expr->call.call_args[i]);
                        Type* param_t = func_t->func.param_types[i];
                        if (arg_t != type_unknown && !type_equals(arg_t, param_t)) {
                            char* at = type_to_string(arg_t);
                            char* pt = type_to_string(param_t);
                            sk_error(NULL, expr->call.call_args[i]->line, expr->call.call_args[i]->col, 
                                     "Argument %d type mismatch: expected '%s', got '%s'.", i+1, pt, at);
                            free(at); free(pt);
                            checker->had_error = 1;
                        }
                    }
                }
                type = func_t->func.return_type;
            }
            break;
        }
        
        case NODE_ARRAY_LIT: {
            if (expr->array_lit.count == 0) {
                type = type_create(checker->arena, TYPE_ARRAY);
                type->array.elem_type = type_unknown; // Can't infer
            } else {
                Type* first_type = check_expr(checker, expr->array_lit.elements[0]);
                for (int i = 1; i < expr->array_lit.count; i++) {
                    Type* elem_t = check_expr(checker, expr->array_lit.elements[i]);
                    if (!type_equals(first_type, elem_t)) {
                        sk_error(NULL, expr->line, expr->col, "Array elements must have consistent types.");
                        checker->had_error = 1;
                    }
                }
                type = type_create(checker->arena, TYPE_ARRAY);
                type->array.elem_type = first_type;
            }
            break;
        }
        
        case NODE_INDEX: {
            Type* obj_t = check_expr(checker, expr->index_access.object);
            Type* idx_t = check_expr(checker, expr->index_access.index);
            
            if (idx_t != type_unknown && idx_t != type_int) {
                sk_error(NULL, expr->line, expr->col, "Array index must be int.");
                checker->had_error = 1;
            }
            
            if (obj_t != type_unknown) {
                if (obj_t->kind != TYPE_ARRAY) {
                    sk_error(NULL, expr->line, expr->col, "Can only index arrays.");
                    checker->had_error = 1;
                } else {
                    type = obj_t->array.elem_type;
                }
            }
            break;
        }
        
        case NODE_FIELD_ACCESS: {
            Type* obj_t = check_expr(checker, expr->field_access.object);
            if (obj_t != type_unknown) {
                if (obj_t->kind != TYPE_STRUCT) {
                    sk_error(NULL, expr->line, expr->col, "Can only access fields on structs.");
                    checker->had_error = 1;
                } else {
                    int found = 0;
                    for (int i = 0; i < obj_t->struct_type.field_count; i++) {
                        if (strcmp(obj_t->struct_type.fields[i].name, expr->field_access.field) == 0) {
                            type = obj_t->struct_type.fields[i].type;
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        sk_error(NULL, expr->line, expr->col, "Struct '%s' has no field '%s'.", 
                                 obj_t->struct_type.struct_name, expr->field_access.field);
                        checker->had_error = 1;
                    }
                }
            }
            break;
        }
        
        default:
            break;
    }
    
    expr->resolved_type = type;
    return type;
}

static void check_stmt(Checker* checker, Node* stmt) {
    if (!stmt) return;
    
    switch (stmt->kind) {
        case NODE_EXPR_STMT:
            check_expr(checker, stmt->expr);
            break;
            
        case NODE_IM: {
            Type* val_t = type_unknown;
            if (stmt->let_decl.let_value) {
                val_t = check_expr(checker, stmt->let_decl.let_value);
            }
            
            Type* ann_t = type_unknown;
            if (stmt->let_decl.let_type) {
                ann_t = lookup_type_by_name(checker, stmt->let_decl.let_type->name);
                if (ann_t == type_unknown) {
                    sk_error(NULL, stmt->line, stmt->col, "Unknown type '%s'.", stmt->let_decl.let_type->name);
                    checker->had_error = 1;
                }
            }
            
            Type* final_t = type_unknown;
            if (ann_t != type_unknown && val_t != type_unknown) {
                if (!type_equals(ann_t, val_t)) {
                    char* a = type_to_string(ann_t);
                    char* v = type_to_string(val_t);
                    sk_error(NULL, stmt->line, stmt->col, "Type mismatch: expected '%s', got '%s'.", a, v);
                    free(a); free(v);
                    checker->had_error = 1;
                }
                final_t = ann_t;
            } else if (ann_t != type_unknown) {
                final_t = ann_t;
            } else if (val_t != type_unknown) {
                final_t = val_t;
            } else {
                sk_error(NULL, stmt->line, stmt->col, "Cannot infer type for '%s'.", stmt->let_decl.let_name);
                checker->had_error = 1;
            }
            
            if (symbol_define(checker->symbols, stmt->let_decl.let_name, final_t, stmt->let_decl.let_mutable) == NULL) {
                sk_error(NULL, stmt->line, stmt->col, "Variable '%s' already defined in this scope.", stmt->let_decl.let_name);
                checker->had_error = 1;
            }
            
            stmt->resolved_type = final_t;
            break;
        }
        
        case NODE_OUT: {
            check_expr(checker, stmt->expr);
            break;
        }

        
        case NODE_ASSIGN: {
            Type* target_t = check_expr(checker, stmt->assign.assign_target);
            Type* val_t = check_expr(checker, stmt->assign.assign_value);
            
            if (stmt->assign.assign_target->kind == NODE_IDENT) {
                Symbol* sym = symbol_lookup(checker->symbols, stmt->assign.assign_target->name);
                if (sym && !sym->is_mutable) {
                    sk_error(NULL, stmt->line, stmt->col, "Cannot assign to immutable variable '%s'.", sym->name);
                    checker->had_error = 1;
                }
            }
            
            if (target_t != type_unknown && val_t != type_unknown && !type_equals(target_t, val_t)) {
                char* t = type_to_string(target_t);
                char* v = type_to_string(val_t);
                sk_error(NULL, stmt->line, stmt->col, "Type mismatch in assignment: expected '%s', got '%s'.", t, v);
                free(t); free(v);
                checker->had_error = 1;
            }
            break;
        }
        
        case NODE_IF: {
            Type* cond_t = check_expr(checker, stmt->if_stmt.if_cond);
            if (cond_t != type_unknown && cond_t != type_bool) {
                sk_error(NULL, stmt->if_stmt.if_cond->line, stmt->if_stmt.if_cond->col, "If condition must be bool.");
                checker->had_error = 1;
            }
            
            scope_push(checker->symbols);
            for (int i = 0; i < stmt->if_stmt.if_body_count; i++) {
                check_stmt(checker, stmt->if_stmt.if_body[i]);
            }
            scope_pop(checker->symbols);
            
            for (int i = 0; i < stmt->if_stmt.elif_count; i++) {
                Node* elif = stmt->if_stmt.elif_clauses[i];
                Type* elif_cond = check_expr(checker, elif->elif_stmt.elif_cond);
                if (elif_cond != type_unknown && elif_cond != type_bool) {
                    sk_error(NULL, elif->elif_stmt.elif_cond->line, elif->elif_stmt.elif_cond->col, "Elif condition must be bool.");
                    checker->had_error = 1;
                }
                
                scope_push(checker->symbols);
                for (int j = 0; j < elif->elif_stmt.elif_body_count; j++) {
                    check_stmt(checker, elif->elif_stmt.elif_body[j]);
                }
                scope_pop(checker->symbols);
            }
            
            if (stmt->if_stmt.else_body) {
                scope_push(checker->symbols);
                for (int i = 0; i < stmt->if_stmt.else_count; i++) {
                    check_stmt(checker, stmt->if_stmt.else_body[i]);
                }
                scope_pop(checker->symbols);
            }
            break;
        }
        
        case NODE_WHILE: {
            Type* cond_t = check_expr(checker, stmt->while_stmt.while_cond);
            if (cond_t != type_unknown && cond_t != type_bool) {
                sk_error(NULL, stmt->while_stmt.while_cond->line, stmt->while_stmt.while_cond->col, "While condition must be bool.");
                checker->had_error = 1;
            }
            
            scope_push(checker->symbols);
            for (int i = 0; i < stmt->while_stmt.while_body_count; i++) {
                check_stmt(checker, stmt->while_stmt.while_body[i]);
            }
            scope_pop(checker->symbols);
            break;
        }
        
        case NODE_FOR: {
            Type* iter_t = check_expr(checker, stmt->for_stmt.for_iter);
            Type* elem_t = type_unknown;
            
            if (iter_t != type_unknown) {
                if (iter_t->kind == TYPE_ARRAY) {
                    elem_t = iter_t->array.elem_type;
                } else {
                    sk_error(NULL, stmt->for_stmt.for_iter->line, stmt->for_stmt.for_iter->col, "Can only iterate over arrays.");
                    checker->had_error = 1;
                }
            }
            
            scope_push(checker->symbols);
            symbol_define(checker->symbols, stmt->for_stmt.for_var, elem_t, 0); // immutable iter var
            
            for (int i = 0; i < stmt->for_stmt.for_body_count; i++) {
                check_stmt(checker, stmt->for_stmt.for_body[i]);
            }
            scope_pop(checker->symbols);
            break;
        }
        
        case NODE_RETURN: {
            Type* ret_t = type_none;
            if (stmt->ret_value) {
                ret_t = check_expr(checker, stmt->ret_value);
            }
            
            if (checker->current_function_return_type) {
                if (ret_t != type_unknown && !type_equals(ret_t, checker->current_function_return_type)) {
                    char* a = type_to_string(ret_t);
                    char* e = type_to_string(checker->current_function_return_type);
                    sk_error(NULL, stmt->line, stmt->col, "Return type mismatch: expected '%s', got '%s'.", e, a);
                    free(a); free(e);
                    checker->had_error = 1;
                }
            }
            break;
        }
        
        case NODE_STRUCT_DEF: {
            Type* st_type = type_create(checker->arena, TYPE_STRUCT);
            st_type->name = stmt->struct_def.struct_name;
            st_type->struct_type.struct_name = stmt->struct_def.struct_name;
            st_type->struct_type.field_count = stmt->struct_def.struct_field_count;
            st_type->struct_type.fields = (TypeField*)arena_alloc(checker->arena, sizeof(TypeField) * stmt->struct_def.struct_field_count);
            
            for (int i = 0; i < stmt->struct_def.struct_field_count; i++) {
                st_type->struct_type.fields[i].name = stmt->struct_def.struct_fields[i].name;
                st_type->struct_type.fields[i].type = lookup_type_by_name(checker, stmt->struct_def.struct_fields[i].type_name);
            }
            
            symbol_define(checker->symbols, stmt->struct_def.struct_name, st_type, 0);
            stmt->resolved_type = st_type;
            break;
        }
        
        case NODE_FN_DEF: {
            Type* fn_t = type_create(checker->arena, TYPE_FUNCTION);
            fn_t->func.param_count = stmt->fn_def.fn_param_count;
            fn_t->func.param_types = (Type**)arena_alloc(checker->arena, sizeof(Type*) * fn_t->func.param_count);
            
            for (int i = 0; i < fn_t->func.param_count; i++) {
                fn_t->func.param_types[i] = lookup_type_by_name(checker, stmt->fn_def.fn_params[i].type_name);
            }
            
            fn_t->func.return_type = lookup_type_by_name(checker, stmt->fn_def.fn_return_type);
            
            if (symbol_define(checker->symbols, stmt->fn_def.fn_name, fn_t, 0) == NULL) {
                sk_error(NULL, stmt->line, stmt->col, "Function '%s' already defined.", stmt->fn_def.fn_name);
                checker->had_error = 1;
            }
            
            Type* prev_return_type = checker->current_function_return_type;
            checker->current_function_return_type = fn_t->func.return_type;
            
            scope_push(checker->symbols);
            
            for (int i = 0; i < fn_t->func.param_count; i++) {
                symbol_define(checker->symbols, stmt->fn_def.fn_params[i].name, fn_t->func.param_types[i], 0);
            }
            
            for (int i = 0; i < stmt->fn_def.fn_body_count; i++) {
                check_stmt(checker, stmt->fn_def.fn_body[i]);
            }
            
            scope_pop(checker->symbols);
            
            checker->current_function_return_type = prev_return_type;
            stmt->resolved_type = fn_t;
            break;
        }
        
        default:
            break;
    }
}

int checker_check(Checker* checker, Node* program) {
    if (!program || program->kind != NODE_PROGRAM) return 0;
    
    // First pass: register structs and functions
    for (int i = 0; i < program->program.stmt_count; i++) {
        Node* stmt = program->program.stmts[i];
        if (stmt->kind == NODE_STRUCT_DEF) {
            check_stmt(checker, stmt); // Register struct
        }
    }
    
    for (int i = 0; i < program->program.stmt_count; i++) {
        Node* stmt = program->program.stmts[i];
        if (stmt->kind == NODE_FN_DEF) {
            // Register functions in global scope but don't check body yet
            Type* fn_t = type_create(checker->arena, TYPE_FUNCTION);
            fn_t->func.param_count = stmt->fn_def.fn_param_count;
            fn_t->func.param_types = (Type**)arena_alloc(checker->arena, sizeof(Type*) * fn_t->func.param_count);
            for (int j = 0; j < fn_t->func.param_count; j++) {
                fn_t->func.param_types[j] = lookup_type_by_name(checker, stmt->fn_def.fn_params[j].type_name);
            }
            fn_t->func.return_type = lookup_type_by_name(checker, stmt->fn_def.fn_return_type);
            symbol_define(checker->symbols, stmt->fn_def.fn_name, fn_t, 0);
        }
    }
    
    // Second pass: full check
    for (int i = 0; i < program->program.stmt_count; i++) {
        Node* stmt = program->program.stmts[i];
        if (stmt->kind == NODE_STRUCT_DEF) continue; // Already done
        
        if (stmt->kind == NODE_FN_DEF) {
            // Check body
            Symbol* sym = symbol_lookup(checker->symbols, stmt->fn_def.fn_name);
            Type* prev_return_type = checker->current_function_return_type;
            checker->current_function_return_type = sym->type->func.return_type;
            
            scope_push(checker->symbols);
            for (int j = 0; j < sym->type->func.param_count; j++) {
                symbol_define(checker->symbols, stmt->fn_def.fn_params[j].name, sym->type->func.param_types[j], 0);
            }
            for (int j = 0; j < stmt->fn_def.fn_body_count; j++) {
                check_stmt(checker, stmt->fn_def.fn_body[j]);
            }
            scope_pop(checker->symbols);
            
            checker->current_function_return_type = prev_return_type;
        } else {
            check_stmt(checker, stmt);
        }
    }
    
    return checker->had_error;
}
