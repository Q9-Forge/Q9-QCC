@echo off
rem build_xcc_win.cmd NAME OUTDIR SOURCE "XCC-FLAGS" [SOURCE2]
rem Windows counterpart of build_xcc_sdk.sh: builds one OS-9/68k module with
rem Microware XCC. MWOS_ROOT may be set in the environment (default below).
rem The flags are passed as ONE quoted argument because cmd splits "a=b".
setlocal EnableExtensions
if "%MWOS_ROOT%"=="" set "MWOS_ROOT=B:\projects\MWOS"
if not exist "%MWOS_ROOT%\DOS\BIN\xcc.exe" (
  echo Missing XCC: %MWOS_ROOT%\DOS\BIN\xcc.exe 1>&2
  exit /b 2
)
set "NAME=%~1"
set "OUT=%~2"
set "FLAGS=%~4"
set SRCS="%~3"
if not "%~5"=="" set SRCS=%SRCS% "%~5"
set "STAGE=%TEMP%\q9xcc-%NAME%"
if not exist "%STAGE%" mkdir "%STAGE%"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%STAGE%\%NAME%" del "%STAGE%\%NAME%"
set "MWOS=%MWOS_ROOT%"
set "PATH=%MWOS_ROOT%\DOS\BIN;%PATH%"
cd /d "%STAGE%"
"%MWOS_ROOT%\DOS\BIN\xcc.exe" -mw=%MWOS_ROOT% -tp=68030,ld %FLAGS% "-f=%STAGE%\%NAME%" %SRCS%
if errorlevel 1 exit /b 1
if not exist "%STAGE%\%NAME%" (
  echo XCC failed to build %NAME% 1>&2
  exit /b 1
)
copy /Y "%STAGE%\%NAME%" "%OUT%\%NAME%" >nul
echo XCC %NAME%: built
exit /b 0
