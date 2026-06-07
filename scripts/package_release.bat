@echo off
set RELEASE_DIR=release\shank-v0.1-win64
set TOOLS_DIR=%RELEASE_DIR%\tools

echo 🔥 Creating Shank Release Package...

if exist "%RELEASE_DIR%" rmdir /s /q "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%"
mkdir "%RELEASE_DIR%\runtime"
mkdir "%TOOLS_DIR%"

echo Building compiler...
call build.bat

echo Copying core components...
copy shankc.exe "%RELEASE_DIR%\"
copy runtime\shank_runtime.obj "%RELEASE_DIR%\runtime\"
copy scripts\drag_and_drop.bat "%RELEASE_DIR%\"

echo Copying NASM...
for /f "delims=" %%i in ('where nasm.exe') do copy "%%i" "%TOOLS_DIR%\"

echo Copying MinGW (This will take a moment)...
xcopy "C:\Users\shashank singh\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64" "%TOOLS_DIR%\" /E /I /Y /EXCLUDE:exclude.txt

echo Creating shank.bat...
(
echo @echo off
echo set SHANK_HOME=%%~dp0
echo set PATH=%%SHANK_HOME%%tools\bin;%%SHANK_HOME%%tools;%%PATH%%
echo "%%SHANK_HOME%%shankc.exe" %%*
) > "%RELEASE_DIR%\shank.bat"

echo Creating install.bat...
(
echo @echo off
echo echo.
echo echo 🔥 Installing Shank Compiler...
echo echo.
echo setx PATH "%%~dp0;%%PATH%%"
echo echo.
echo echo ✅ Shank is now installed!
echo echo You can now use the 'shank' command in any new terminal.
echo pause
) > "%RELEASE_DIR%\install.bat"

echo ✅ Release Package built successfully at %RELEASE_DIR%!
