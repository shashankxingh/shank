@echo off
set SHANK_HOME=%~dp0
set PATH=%SHANK_HOME%tools\bin;%SHANK_HOME%tools;%PATH%

if "%~1"=="" (
    echo ========================================
    echo             SHANK COMPILER
    echo ========================================
    echo.
    echo Please drag and drop a .sk file onto this icon to compile it!
    echo.
    pause
    exit /b
)

echo ========================================
echo        COMPILING SHANK SCRIPT
echo ========================================
echo.
echo File: %~1
echo.

"%SHANK_HOME%shankc.exe" build "%~1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ✅ Compilation Successful! The .exe has been generated.
) else (
    echo.
    echo ❌ Compilation Failed.
)
echo.
pause
