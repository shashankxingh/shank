#include "types.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Type* type_int;
Type* type_float;
Type* type_bool;
Type* type_str;
Type* type_none;
Type* type_unknown;

void types_init(Arena* arena) {
    type_int = type_create(arena, TYPE_INT);
    type_int->name = "int";
    
    type_float = type_create(arena, TYPE_FLOAT);
    type_float->name = "float";
    
    type_bool = type_create(arena, TYPE_BOOL);
    type_bool->name = "bool";
    
    type_str = type_create(arena, TYPE_STR);
    type_str->name = "str";
    
    type_none = type_create(arena, TYPE_NONE);
    type_none->name = "none";
    
    type_unknown = type_create(arena, TYPE_UNKNOWN);
    type_unknown->name = "unknown";
}

Type* type_create(Arena* arena, TypeKind kind) {
    Type* t = (Type*)arena_alloc(arena, sizeof(Type));
    memset(t, 0, sizeof(Type));
    t->kind = kind;
    return t;
}

int type_equals(Type* a, Type* b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    
    switch (a->kind) {
        case TYPE_INT:
        case TYPE_FLOAT:
        case TYPE_BOOL:
        case TYPE_STR:
        case TYPE_NONE:
        case TYPE_UNKNOWN:
            return 1; // Singletons
            
        case TYPE_ARRAY:
            return type_equals(a->array.elem_type, b->array.elem_type);
            
        case TYPE_STRUCT:
            return strcmp(a->struct_type.struct_name, b->struct_type.struct_name) == 0;
            
        case TYPE_FUNCTION:
            if (a->func.param_count != b->func.param_count) return 0;
            if (!type_equals(a->func.return_type, b->func.return_type)) return 0;
            for (int i = 0; i < a->func.param_count; i++) {
                if (!type_equals(a->func.param_types[i], b->func.param_types[i])) return 0;
            }
            return 1;
    }
    return 0;
}

char* type_to_string(Type* type) {
    if (!type) return sk_strdup("null_type");
    
    switch (type->kind) {
        case TYPE_INT: return sk_strdup("int");
        case TYPE_FLOAT: return sk_strdup("float");
        case TYPE_BOOL: return sk_strdup("bool");
        case TYPE_STR: return sk_strdup("str");
        case TYPE_NONE: return sk_strdup("none");
        case TYPE_UNKNOWN: return sk_strdup("unknown");
        
        case TYPE_ARRAY: {
            char* elem = type_to_string(type->array.elem_type);
            StringBuilder* sb = sb_init();
            sb_appendf(sb, "[%s]", elem);
            char* result = sb_to_string(sb);
            sb_free(sb);
            free(elem); // Need to use standard free here since sk_strdup uses malloc
            return result;
        }
        
        case TYPE_STRUCT:
            return sk_strdup(type->struct_type.struct_name);
            
        case TYPE_FUNCTION: {
            StringBuilder* sb = sb_init();
            sb_append(sb, "fn(");
            for (int i = 0; i < type->func.param_count; i++) {
                if (i > 0) sb_append(sb, ", ");
                char* ptype = type_to_string(type->func.param_types[i]);
                sb_append(sb, ptype);
                free(ptype);
            }
            sb_append(sb, ") -> ");
            char* rtype = type_to_string(type->func.return_type);
            sb_append(sb, rtype);
            free(rtype);
            char* result = sb_to_string(sb);
            sb_free(sb);
            return result;
        }
    }
    return sk_strdup("invalid_type");
}
