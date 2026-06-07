param(
    [string]$MingwPath = "C:\Users\shashank singh\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64",
    [string]$NasmPath = (Get-Command nasm.exe).Source
)

$ReleaseDir = "release\shank-v0.1-win64"
$ToolsDir = "$ReleaseDir\tools"

Write-Host "🔥 Creating Shank Release Package..."

if (Test-Path $ReleaseDir) { Remove-Item -Recurse -Force $ReleaseDir }
New-Item -ItemType Directory -Path "$ReleaseDir" | Out-Null
New-Item -ItemType Directory -Path "$ReleaseDir\runtime" | Out-Null
New-Item -ItemType Directory -Path $ToolsDir | Out-Null

Write-Host "Building compiler..."
.\build.bat | Out-Null

Write-Host "Copying core components..."
Copy-Item "shankc.exe" -Destination "$ReleaseDir\"
Copy-Item "runtime\shank_runtime.obj" -Destination "$ReleaseDir\runtime\"

Write-Host "Copying NASM..."
Copy-Item $NasmPath -Destination "$ToolsDir\"

Write-Host "Copying MinGW (This will take a moment)..."
Copy-Item -Recurse "$MingwPath\*" -Destination "$ToolsDir\" -Exclude "share", "doc", "include"

Write-Host "Creating shank terminal command..."
Set-Content -Path "$ReleaseDir\shank.bat" -Value "@echo off`r`nset `"SHANK_HOME=%~dp0`"`r`nset `"PATH=%SHANK_HOME%\tools\bin;%PATH%`"`r`n`"%SHANK_HOME%\shankc.exe`" %*"

Write-Host "Creating install.bat for the user..."
Set-Content -Path "$ReleaseDir\install.bat" -Value "@echo off`r`necho.`r`necho 🔥 Installing Shank Compiler...`r`necho.`r`nsetx PATH `"%~dp0;%PATH%`"`r`necho.`r`necho ✅ Shank is now installed!`r`necho You can now use the 'shank' command in any new terminal.`r`npause"

Write-Host "✅ Release Package built successfully at $ReleaseDir!"
