#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "errors.h"
#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "codegen.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define access _access
#else
#include <unistd.h>
#endif

static void print_usage(void) {
    printf("🔥 Shank Compiler v0.1.0\n");
    printf("   Blazing fast. Dead simple.\n\n");
    printf("Usage: shankc <command> <file>\n");
    printf("Commands:\n");
    printf("  build <file.sk> [-o output]   Compile to executable\n");
    printf("  run <file.sk>                  Compile and run\n");
    printf("  emit <file.sk> [-o output.asm] Emit assembly\n");
    printf("  check <file.sk>                Type check only\n");
    printf("  help                           Show help\n");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    const char* command = argv[1];
    
    if (strcmp(command, "help") == 0) {
        print_usage();
        return 0;
    }
    
    if (argc < 3) {
        printf("Error: Missing file argument.\n");
        return 1;
    }
    
    const char* filename = argv[2];
    char* source = sk_read_file(filename);
    if (!source) {
        printf("Error: Could not read file '%s'.\n", filename);
        return 1;
    }
    
    Arena* arena = arena_create(1024 * 1024); // 1MB blocks
    types_init(arena);
    
    Lexer lexer;
    lexer_init(&lexer, source, filename);
    
    Parser parser;
    parser_init(&parser, &lexer, arena);
    Node* program = parser_parse_program(&parser);
    
    if (has_errors()) {
        printf("Parsing failed with %d error(s).\n", g_error_count);
        free(source);
        arena_destroy(arena);
        return 1;
    }
    
    Checker checker;
    checker_init(&checker, arena);
    checker_check(&checker, program);
    
    if (has_errors() || strcmp(command, "check") == 0) {
        if (has_errors()) {
            printf("Type checking failed with %d error(s).\n", g_error_count);
        } else {
            printf("Type check OK.\n");
        }
        free(source);
        arena_destroy(arena);
        return has_errors() ? 1 : 0;
    }
    
    char* asm_code = codegen_generate(&checker, program);
    
    const char* asm_out = "output.asm";
    const char* exe_out = "program.exe";
    
    if (strcmp(command, "emit") == 0) {
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            asm_out = argv[4];
        } else {
            // Default to filename.asm
            asm_out = "output.asm"; 
        }
        codegen_emit_to_file(asm_code, asm_out);
        printf("Assembly emitted to %s\n", asm_out);
    } else if (strcmp(command, "build") == 0 || strcmp(command, "run") == 0) {
        if (argc >= 5 && strcmp(argv[3], "-o") == 0) {
            exe_out = argv[4];
        }
        
        codegen_emit_to_file(asm_code, "output.asm");
        
        char exe_path[1024] = {0};
        char exe_dir[1024] = {0};
        #ifdef _WIN32
        GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
        char* last_slash = strrchr(exe_path, '\\');
        if (last_slash) {
            *last_slash = '\0';
            strcpy(exe_dir, exe_path);
        } else {
            strcpy(exe_dir, ".");
        }
        #else
        strcpy(exe_dir, ".");
        #endif

        char nasm_cmd[1024];
        char bundled_nasm[1024];
        snprintf(bundled_nasm, sizeof(bundled_nasm), "%s\\tools\\nasm.exe", exe_dir);
        if (access(bundled_nasm, 0) == 0) {
            snprintf(nasm_cmd, sizeof(nasm_cmd), "\"%s\" -f win64 output.asm -o output.obj", bundled_nasm);
        } else {
            snprintf(nasm_cmd, sizeof(nasm_cmd), "nasm -f win64 output.asm -o output.obj");
        }

        // Call NASM and GCC
        int res1 = system(nasm_cmd);
        if (res1 != 0) {
            printf("Error: NASM assembly failed. Is nasm in your PATH or tools/ folder?\n");
        } else {
            char gcc_cmd[1024];
            char bundled_gcc[1024];
            char runtime_obj[1024];
            snprintf(bundled_gcc, sizeof(bundled_gcc), "%s\\tools\\gcc.exe", exe_dir);
            snprintf(runtime_obj, sizeof(runtime_obj), "%s\\runtime\\shank_runtime.obj", exe_dir);
            
            if (access(runtime_obj, 0) != 0) {
                strcpy(runtime_obj, "runtime\\shank_runtime.obj");
            }

            if (access(bundled_gcc, 0) == 0) {
                snprintf(gcc_cmd, sizeof(gcc_cmd), "\"%s\" output.obj \"%s\" -o %s", bundled_gcc, runtime_obj, exe_out);
            } else {
                snprintf(gcc_cmd, sizeof(gcc_cmd), "gcc output.obj \"%s\" -o %s", runtime_obj, exe_out);
            }

            int res2 = system(gcc_cmd);
            if (res2 != 0) {
                printf("Error: GCC linking failed. Is gcc in your PATH or tools/ folder?\n");
            } else {
                printf("Build successful: %s\n", exe_out);
                
                if (strcmp(command, "run") == 0) {
                    char run_cmd[1024];
                    snprintf(run_cmd, sizeof(run_cmd), ".\\%s", exe_out);
                    system(run_cmd);
                }
                
                // Cleanup intermediate files
                remove("output.asm");
                remove("output.obj");
                
                // If it was just a run command, cleanup the executable too!
                if (strcmp(command, "run") == 0) {
                    remove(exe_out);
                }
            }
        }
    } else {
        printf("Unknown command: %s\n", command);
    }
    
    free(asm_code);
    free(source);
    arena_destroy(arena);
    return 0;
}
