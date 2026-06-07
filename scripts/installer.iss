[Setup]
AppName=Shank Compiler
AppVersion=0.1.0
AppPublisher=Shank Language
DefaultDirName={autopf}\Shank
DefaultGroupName=Shank Compiler
UninstallDisplayIcon={app}\shankc.exe
Compression=lzma2
SolidCompression=yes
OutputDir=..\release
OutputBaseFilename=Shank_Setup_v0.1.0_win64
ChangesEnvironment=yes
PrivilegesRequired=lowest

[Files]
Source: "..\release\shank-v0.1-win64\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Shank Compiler"; Filename: "{app}\drag_and_drop.bat"
Name: "{autodesktop}\Shank Compiler"; Filename: "{app}\drag_and_drop.bat"

[Tasks]
Name: "envPath"; Description: "Add Shank to system PATH (Recommended)"; GroupDescription: "Additional icons:"
Name: "desktopicon"; Description: "Create a desktop icon"; GroupDescription: "Additional icons:"

[Run]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -WindowStyle Hidden -Command ""$userPath = [Environment]::GetEnvironmentVariable('Path', 'User'); if ($userPath -notlike '*{app}*') {{ [Environment]::SetEnvironmentVariable('Path', $userPath + ';{app}', 'User') }"""; Flags: runhidden; Tasks: envPath

[UninstallRun]
Filename: "powershell.exe"; Parameters: "-ExecutionPolicy Bypass -WindowStyle Hidden -Command ""$userPath = [Environment]::GetEnvironmentVariable('Path', 'User'); $newPath = ($userPath -split ';' | Where-Object {{ $_ -ne '{app}' }}) -join ';'; [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')"""; Flags: runhidden
