@echo off
set SHANK_HOME=%~dp0
set PATH=%SHANK_HOME%tools\bin;%SHANK_HOME%tools;%PATH%
"%SHANK_HOME%shankc.exe" %*
