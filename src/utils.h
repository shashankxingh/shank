#ifndef SHANK_UTILS_H
#define SHANK_UTILS_H

#include <stddef.h>
#include <stdint.h>

// --- Arena Allocator ---
typedef struct ArenaBlock ArenaBlock;
struct ArenaBlock {
    ArenaBlock* next;
    size_t size;
    size_t used;
    uint8_t data[];
};

typedef struct {
    ArenaBlock* current;
    size_t default_block_size;
} Arena;

Arena* arena_create(size_t block_size);
void* arena_alloc(Arena* arena, size_t size);
void arena_destroy(Arena* arena);

// --- String Builder ---
typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

StringBuilder* sb_init(void);
void sb_append(StringBuilder* sb, const char* str);
void sb_appendf(StringBuilder* sb, const char* fmt, ...);
char* sb_to_string(StringBuilder* sb);
void sb_free(StringBuilder* sb);

// --- Helpers ---
char* sk_strdup(const char* str);
char* sk_read_file(const char* path);

#endif // SHANK_UTILS_H
