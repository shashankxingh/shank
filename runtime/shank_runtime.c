#include "shank_runtime.h"

// Print functions
void sk_print_int(int64_t value) {
    printf("%lld", (long long)value);
}

void sk_print_float(double value) {
    printf("%f", value);
}

void sk_print_str(const char* data, int64_t len) {
    if (data) {
        printf("%s", data);
    }
}

void sk_print_bool(int8_t value) {
    printf("%s", value ? "true" : "false");
}

void sk_print_newline(void) {
    printf("\n");
}

// String operations
SkString sk_str_new(const char* data, int64_t len) {
    SkString str;
    str.data = data;
    str.len = len;
    return str;
}

SkString sk_str_concat(SkString a, SkString b) {
    int64_t new_len = a.len + b.len;
    char* new_data = (char*)malloc(new_len + 1);
    
    if (a.len > 0) memcpy(new_data, a.data, a.len);
    if (b.len > 0) memcpy(new_data + a.len, b.data, b.len);
    new_data[new_len] = '\0';
    
    SkString str;
    str.data = new_data;
    str.len = new_len;
    return str;
}

int64_t sk_str_len(SkString s) {
    return s.len;
}

int sk_str_eq(SkString a, SkString b) {
    if (a.len != b.len) return 0;
    return memcmp(a.data, b.data, a.len) == 0;
}

// Array operations
SkArray sk_array_new(int64_t elem_size, int64_t initial_cap) {
    if (initial_cap <= 0) initial_cap = 4;
    SkArray arr;
    arr.data = malloc(elem_size * initial_cap);
    arr.len = 0;
    arr.cap = initial_cap;
    arr.elem_size = elem_size;
    return arr;
}

void sk_array_push(SkArray* arr, const void* elem) {
    if (arr->len >= arr->cap) {
        arr->cap *= 2;
        arr->data = realloc(arr->data, arr->elem_size * arr->cap);
    }
    memcpy((char*)arr->data + (arr->len * arr->elem_size), elem, arr->elem_size);
    arr->len++;
}

void* sk_array_get(SkArray* arr, int64_t index) {
    if (index < 0 || index >= arr->len) {
        sk_panic("Array index out of bounds", 25);
    }
    return (char*)arr->data + (index * arr->elem_size);
}

int64_t sk_array_len(SkArray* arr) {
    return arr->len;
}

void sk_array_free(SkArray* arr) {
    free(arr->data);
    arr->data = NULL;
    arr->len = 0;
    arr->cap = 0;
}

// Memory
void* sk_alloc(int64_t size) {
    return calloc(1, size);
}

void sk_free(void* ptr) {
    free(ptr);
}

// Error handling
void sk_panic(const char* msg, int64_t len) {
    fprintf(stderr, "panic: %.*s\n", (int)len, msg);
    exit(1);
}

// Math helpers
int64_t sk_pow_int(int64_t base, int64_t exp) {
    int64_t res = 1;
    while (exp > 0) {
        if (exp % 2 == 1) res *= base;
        base *= base;
        exp /= 2;
    }
    return res;
}

double sk_pow_float(double base, double exp) {
    // For now, simple loop for integer exponents or just rely on libm
    // Need <math.h> for pow(), avoiding extra link dependencies if possible.
    // We'll leave it simple.
    double res = 1.0;
    int e = (int)exp;
    for (int i = 0; i < e; i++) res *= base;
    return res;
}

// Range iterator
SkRange sk_range(int64_t start, int64_t end) {
    SkRange r = {start, end, 1};
    return r;
}

SkRange sk_range_step(int64_t start, int64_t end, int64_t step) {
    SkRange r = {start, end, step};
    return r;
}

SkString sk_int_to_str(int64_t value) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    return sk_str_new(buf, len);
}

SkString sk_float_to_str(double value) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", value);
    return sk_str_new(buf, len);
}

SkString sk_bool_to_str(int8_t value) {
    if (value) {
        return sk_str_new("True", 4);
    } else {
        return sk_str_new("False", 5);
    }
}

char* sk_interp_concat(char* a, char* b) {
    int len_a = a ? strlen(a) : 0;
    int len_b = b ? strlen(b) : 0;
    char* res = (char*)malloc(len_a + len_b + 1);
    if (len_a) strcpy(res, a); else res[0] = '\0';
    if (len_b) strcat(res, b);
    return res;
}

char* sk_int_to_cstr(int64_t val) {
    char* buf = (char*)malloc(32);
    snprintf(buf, 32, "%lld", (long long)val);
    return buf;
}

char* sk_float_to_cstr(double val) {
    char* buf = (char*)malloc(64);
    snprintf(buf, 64, "%g", val);
    return buf;
}

char* sk_bool_to_cstr(int8_t val) {
    char* buf = (char*)malloc(8);
    strcpy(buf, val ? "True" : "False");
    return buf;
}


// C Entry Point
extern void shank_main(void);

int main(int argc, char** argv) {
    // Initialize any runtime systems if needed
    shank_main();
    return 0;
}
