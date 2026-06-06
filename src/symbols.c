#include "symbols.h"
#include <string.h>
#include <stdlib.h>

static uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash;
}

SymbolTable* symbol_table_create(Arena* arena) {
    SymbolTable* table = (SymbolTable*)arena_alloc(arena, sizeof(SymbolTable));
    memset(table, 0, sizeof(SymbolTable));
    table->arena = arena;
    table->current_depth = 0;
    return table;
}

void scope_push(SymbolTable* table) {
    table->current_depth++;
}

void scope_pop(SymbolTable* table) {
    // Remove all symbols at the current depth
    for (int i = 0; i < SYMBOL_TABLE_SIZE; i++) {
        Symbol* curr = table->buckets[i];
        Symbol* prev = NULL;
        
        while (curr) {
            if (curr->scope_depth == table->current_depth) {
                // Remove curr
                if (prev) {
                    prev->next = curr->next;
                } else {
                    table->buckets[i] = curr->next;
                }
                curr = curr->next;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    }
    table->current_depth--;
}

Symbol* symbol_define(SymbolTable* table, const char* name, Type* type, int is_mutable) {
    // Check if it already exists in the CURRENT scope
    uint32_t idx = hash_string(name) % SYMBOL_TABLE_SIZE;
    
    Symbol* curr = table->buckets[idx];
    while (curr) {
        if (strcmp(curr->name, name) == 0 && curr->scope_depth == table->current_depth) {
            return NULL; // Already defined in this scope
        }
        curr = curr->next;
    }
    
    Symbol* sym = (Symbol*)arena_alloc(table->arena, sizeof(Symbol));
    sym->name = sk_strdup(name); // Duplicate to be safe
    sym->type = type;
    sym->is_mutable = is_mutable;
    sym->scope_depth = table->current_depth;
    sym->stack_offset = 0;
    
    // Prepend to bucket
    sym->next = table->buckets[idx];
    table->buckets[idx] = sym;
    
    return sym;
}

Symbol* symbol_lookup(SymbolTable* table, const char* name) {
    uint32_t idx = hash_string(name) % SYMBOL_TABLE_SIZE;
    
    Symbol* best_match = NULL;
    int best_depth = -1;
    
    Symbol* curr = table->buckets[idx];
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            if (curr->scope_depth > best_depth) {
                best_match = curr;
                best_depth = curr->scope_depth;
            }
        }
        curr = curr->next;
    }
    
    return best_match;
}
