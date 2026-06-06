#ifndef SHANK_TYPES_H
#define SHANK_TYPES_H

#include "utils.h"

typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOL,
    TYPE_STR,
    TYPE_NONE,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_FUNCTION,
    TYPE_UNKNOWN
} TypeKind;

typedef struct Type Type;
typedef struct {
    char* name;
    Type* type;
} TypeField;

struct Type {
    TypeKind kind;
    char* name; // Custom name for structs, or NULL for primitives
    
    union {
        struct { Type* elem_type; } array;
        struct { 
            char* struct_name; 
            TypeField* fields; 
            int field_count; 
        } struct_type;
        struct { 
            Type** param_types; 
            int param_count; 
            Type* return_type; 
        } func;
    };
};

extern Type* type_int;
extern Type* type_float;
extern Type* type_bool;
extern Type* type_str;
extern Type* type_none;
extern Type* type_unknown;

void types_init(Arena* arena);
Type* type_create(Arena* arena, TypeKind kind);
int type_equals(Type* a, Type* b);
char* type_to_string(Type* type);

#endif // SHANK_TYPES_H
