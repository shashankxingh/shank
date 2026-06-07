#include "lexer.h"
#include <stdio.h>
#include "utils.h"

int main() {
    char* src = sk_read_file("test_new_syntax.sk");
    Lexer lexer;
    lexer_init(&lexer, src, "test_new_syntax.sk");
    
    Token t;
    do {
        t = lexer_next_token(&lexer);
        printf("Kind: %d, Line: %d, Col: %d, Text: %.*s\n", t.kind, t.line, t.col, t.length, t.start);
    } while (t.kind != TOK_EOF);
    
    return 0;
}
