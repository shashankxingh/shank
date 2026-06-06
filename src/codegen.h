#ifndef SHANK_CODEGEN_H
#define SHANK_CODEGEN_H

#include "ast.h"
#include "checker.h"
#include "utils.h"

void codegen_init(void);
char* codegen_generate(Checker* checker, Node* program);
int codegen_emit_to_file(const char* asm_string, const char* filename);

#endif // SHANK_CODEGEN_H
