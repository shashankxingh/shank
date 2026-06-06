#include "errors.h"
#include <stdio.h>
#include <stdarg.h>

int g_error_count = 0;

// Simple ANSI colors for Windows 10+ / Linux
#define COLOR_RED "\x1b[31m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_BLUE "\x1b[34m"
#define COLOR_RESET "\x1b[0m"

void sk_error(const char* filename, int line, int col, const char* fmt, ...) {
    g_error_count++;
    
    fprintf(stderr, "%serror%s: ", COLOR_RED, COLOR_RESET);
    if (filename) {
        fprintf(stderr, "%s:%d:%d: ", filename, line, col);
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void sk_warning(const char* filename, int line, int col, const char* fmt, ...) {
    fprintf(stderr, "%swarning%s: ", COLOR_YELLOW, COLOR_RESET);
    if (filename) {
        fprintf(stderr, "%s:%d:%d: ", filename, line, col);
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

void sk_note(const char* fmt, ...) {
    fprintf(stderr, "%snote%s: ", COLOR_BLUE, COLOR_RESET);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
}

int has_errors(void) {
    return g_error_count > 0;
}
