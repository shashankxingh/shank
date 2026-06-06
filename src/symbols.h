#ifndef SHANK_SYMBOLS_H
#define SHANK_SYMBOLS_H

#include "types.h"
#include "utils.h"

typedef struct Symbol Symbol;
struct Symbol {
    char* name;
    Type* type;
    int is_mutable;
    int scope_depth;
    
    // Memory/codegen info
    int stack_offset; // offset from RBP
    
    Symbol* next;
};

#define SYMBOL_TABLE_SIZE 1024

typedef struct {
    Arena* arena;
    Symbol* buckets[SYMBOL_TABLE_SIZE];
    int current_depth;
} SymbolTable;

SymbolTable* symbol_table_create(Arena* arena);
void scope_push(SymbolTable* table);
void scope_pop(SymbolTable* table);

Symbol* symbol_define(SymbolTable* table, const char* name, Type* type, int is_mutable);
Symbol* symbol_lookup(SymbolTable* table, const char* name);

#endif // SHANK_SYMBOLS_H
