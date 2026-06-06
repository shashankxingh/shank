@echo off
setlocal enabledelayedexpansion

echo Building Shank Compiler...
gcc -Wall -Wextra -O2 src\utils.c src\errors.c src\ast.c src\lexer.c src\symbols.c src\types.c src\parser.c src\checker.c src\codegen.c src\main.c -o shankc.exe
if %ERRORLEVEL% NEQ 0 (
    echo Compiler build failed!
    exit /b %ERRORLEVEL%
)

echo Building Shank Runtime...
gcc -c -O2 runtime\shank_runtime.c -o runtime\shank_runtime.obj
if %ERRORLEVEL% NEQ 0 (
    echo Runtime build failed!
    exit /b %ERRORLEVEL%
)

echo Build complete!
shankc.exe help
