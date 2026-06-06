# Shank Programming Language

Blazing fast. Dead simple.

Shank is a systems programming language designed to be as fast as C++ and as easy to write as Python.

## Features
- **Python-like Syntax**: Indentation-based, clean, minimal boilerplate.
- **Blazing Fast**: Compiles directly to x86-64 assembly via a C-based compiler pipeline.
- **Static Typing**: Optional type annotations and inference (`let x = 10` vs `let x: int = 10`).
- **No VM, No GC**: Raw performance with a minimal C runtime.

## Compiler Architecture
1. Hand-written Lexer (`src/lexer.c`) - handles `INDENT/DEDENT` seamlessly.
2. Recursive Descent Pratt Parser (`src/parser.c`) - for reliable expression evaluation and AST building.
3. Semantic Analyzer (`src/checker.c`) - robust type-checking and scope management.
4. NASM x86-64 Codegen (`src/codegen.c`) - translates typed AST directly to native machine code.

## Getting Started (Windows)
You need `gcc` (MinGW) and `nasm` installed and in your system PATH.

### Building the compiler
```powershell
.\build.bat
```
This generates `shankc.exe`.

### Running an example
```powershell
.\shankc.exe run examples\1_hello.sk
```

## Example Syntax
```python
fn fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

fn main():
    print("Fibonacci of 10 is:")
    print_int(fib(10))

main()
```
