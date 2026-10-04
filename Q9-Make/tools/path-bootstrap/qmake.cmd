@echo off
rem qmake PATH-fallback bootstrap for Windows -- not the real qmake, a stand-in.
rem Install this directory LAST in PATH, after %Q9SDK%\Windows\CMDS. While the
rem real qmake.exe exists there, PATH finds it first and never reaches this
rem script. After "qmake clean" removed it, the next plain "qmake" lands here:
rem   1. restore the cached binary (cache\qmake-Windows-x86_64.exe), or
rem   2. only if that is missing, rebuild it with clang from Q9-Make\src.
rem Either way the real binary is installed and started with the original
rem arguments. Same two stages as the POSIX script "qmake" next to this file.
setlocal EnableExtensions EnableDelayedExpansion
if "%Q9SDK%"=="" (
  echo Set Q9SDK to the SDK root before calling qmake 1>&2
  exit /b 1
)
set "REAL=%Q9SDK%\Windows\CMDS\qmake.exe"
set "SELF=%~dp0"
set "CACHE=%SELF%cache\qmake-Windows-x86_64.exe"
set "SRCDIR=%SELF%..\.."
if not exist "%REAL%" (
  if not exist "%Q9SDK%\Windows\CMDS" mkdir "%Q9SDK%\Windows\CMDS"
  if exist "%CACHE%" (
    echo qmake: not found at %REAL%, restoring cached binary ... 1>&2
    copy /Y "%CACHE%" "%REAL%" >nul
  ) else (
    set "BUILD=%SRCDIR%\build\host-Windows"
    echo qmake: no cached binary, bootstrapping with clang into !BUILD! ... 1>&2
    if not exist "!BUILD!" mkdir "!BUILD!"
    clang -std=c89 -D_CRT_SECURE_NO_WARNINGS "-I%SRCDIR%\src" -o "!BUILD!\qmake.exe" "%SRCDIR%\src\qmake.c" "%SRCDIR%\src\platform_windows.c"
    if errorlevel 1 exit /b 1
    if not exist "%SELF%cache" mkdir "%SELF%cache"
    copy /Y "!BUILD!\qmake.exe" "%REAL%" >nul
    copy /Y "!BUILD!\qmake.exe" "%CACHE%" >nul
  )
  echo qmake: installed at %REAL% 1>&2
)
"%REAL%" %*
exit /b %errorlevel%
