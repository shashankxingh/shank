#include "utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// --- Arena Allocator ---
Arena* arena_create(size_t block_size) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    
    arena->default_block_size = block_size > 0 ? block_size : 4096;
    arena->current = NULL;
    return arena;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena) return NULL;
    
    // Ensure 8-byte alignment
    size = (size + 7) & ~7;
    
    if (!arena->current || arena->current->used + size > arena->current->size) {
        size_t new_size = arena->default_block_size;
        if (size > new_size) new_size = size;
        
        ArenaBlock* new_block = (ArenaBlock*)malloc(sizeof(ArenaBlock) + new_size);
        if (!new_block) return NULL;
        
        new_block->size = new_size;
        new_block->used = 0;
        new_block->next = arena->current;
        arena->current = new_block;
    }
    
    void* ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    return ptr;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;
    
    ArenaBlock* curr = arena->current;
    while (curr) {
        ArenaBlock* next = curr->next;
        free(curr);
        curr = next;
    }
    free(arena);
}

// --- String Builder ---
StringBuilder* sb_init(void) {
    StringBuilder* sb = (StringBuilder*)malloc(sizeof(StringBuilder));
    if (!sb) return NULL;
    
    sb->cap = 64;
    sb->len = 0;
    sb->data = (char*)malloc(sb->cap);
    if (!sb->data) {
        free(sb);
        return NULL;
    }
    
    sb->data[0] = '\0';
    return sb;
}

void sb_append(StringBuilder* sb, const char* str) {
    if (!sb || !str) return;
    
    size_t len = strlen(str);
    if (sb->len + len + 1 > sb->cap) {
        while (sb->len + len + 1 > sb->cap) {
            sb->cap *= 2;
        }
        sb->data = (char*)realloc(sb->data, sb->cap);
    }
    
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
}

void sb_appendf(StringBuilder* sb, const char* fmt, ...) {
    if (!sb || !fmt) return;
    
    va_list args;
    va_start(args, fmt);
    
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);
    
    if (len < 0) {
        va_end(args);
        return;
    }
    
    if (sb->len + len + 1 > sb->cap) {
        while (sb->len + len + 1 > sb->cap) {
            sb->cap *= 2;
        }
        sb->data = (char*)realloc(sb->data, sb->cap);
    }
    
    vsnprintf(sb->data + sb->len, len + 1, fmt, args);
    sb->len += len;
    
    va_end(args);
}

char* sb_to_string(StringBuilder* sb) {
    if (!sb) return NULL;
    char* result = sk_strdup(sb->data);
    return result;
}

void sb_free(StringBuilder* sb) {
    if (!sb) return;
    free(sb->data);
    free(sb);
}

// --- Helpers ---
char* sk_strdup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* dup = (char*)malloc(len + 1);
    if (dup) {
        memcpy(dup, str, len + 1);
    }
    return dup;
}

char* sk_read_file(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    
    char* content = (char*)malloc(size + 1);
    if (!content) {
        fclose(file);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, size, file);
    content[read_size] = '\0';
    
    fclose(file);
    return content;
}
