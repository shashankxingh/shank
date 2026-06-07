#ifndef SHANK_RUNTIME_H
#define SHANK_RUNTIME_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// String type
typedef struct {
    const char* data;
    int64_t len;
} SkString;

// Dynamic array
typedef struct {
    void* data;
    int64_t len;
    int64_t cap;
    int64_t elem_size;
} SkArray;

// Print functions (called from generated assembly)
void sk_print_int(int64_t value);
void sk_print_float(double value);
void sk_print_str(const char* data, int64_t len);
void sk_print_bool(int8_t value);
void sk_print_newline(void);

// String operations
SkString sk_str_new(const char* data, int64_t len);
SkString sk_str_concat(SkString a, SkString b);
int64_t sk_str_len(SkString s);
int sk_str_eq(SkString a, SkString b);
SkString sk_int_to_str(int64_t value);
SkString sk_float_to_str(double value);
SkString sk_bool_to_str(int8_t value);

// Array operations
SkArray sk_array_new(int64_t elem_size, int64_t initial_cap);
void sk_array_push(SkArray* arr, const void* elem);
void* sk_array_get(SkArray* arr, int64_t index);
int64_t sk_array_len(SkArray* arr);
void sk_array_free(SkArray* arr);

// Memory
void* sk_alloc(int64_t size);
void sk_free(void* ptr);

// Error handling
void sk_panic(const char* msg, int64_t len);

// Math helpers
int64_t sk_pow_int(int64_t base, int64_t exp);
double sk_pow_float(double base, double exp);

// Range iterator
typedef struct { int64_t start; int64_t end; int64_t step; } SkRange;
SkRange sk_range(int64_t start, int64_t end);
SkRange sk_range_step(int64_t start, int64_t end, int64_t step);

#endif
