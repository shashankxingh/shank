#ifndef SHANK_ERRORS_H
#define SHANK_ERRORS_H

extern int g_error_count;

void sk_error(const char* filename, int line, int col, const char* fmt, ...);
void sk_warning(const char* filename, int line, int col, const char* fmt, ...);
void sk_note(const char* fmt, ...);
int has_errors(void);

#endif // SHANK_ERRORS_H
