#ifndef SHANK_CHECKER_H
#define SHANK_CHECKER_H

#include "ast.h"
#include "symbols.h"
#include "types.h"

typedef struct {
    Arena* arena;
    SymbolTable* symbols;
    Type* current_function_return_type;
    int had_error;
} Checker;

void checker_init(Checker* checker, Arena* arena);
int checker_check(Checker* checker, Node* program);

#endif // SHANK_CHECKER_H
