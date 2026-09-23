@echo off
rem Build mode comes from build.sh: "debug" (default) or "release"
set MODE=%1
if "%MODE%"=="" set MODE=debug
set OptFlag=-Od
if /i "%MODE%"=="release" set OptFlag=-O2

set VCVARSALL=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat
set CLANGCL=C:\ProgramDirIMade\clang+llvm-21.1.4-x86_64-pc-windows-msvc\clang+llvm-21.1.4-x86_64-pc-windows-msvc\bin\clang-cl.exe
set CODE=C:\Users\ams56\work\tufty\code
set BUILD=C:\Users\ams56\work\tufty\build
rem Optimization flag deliberately NOT in here; it's added per build as %OptFlag%
set CommonCompilerFlags=-MTd -nologo -GR- -EHa- -Oi -WX -W4 -wd4201 -wd4100 -Wno-unused-function -Wno-writable-strings -wd4189 -Wno-unused-variable -Wno-sign-compare -Wno-missing-field-initializers -Wno-missing-braces -Wno-unused-but-set-variable -DTUFTY_INTERNAL=1 -DTUFTY_SLOW=1 -DTUFTY_WIN32=1 -FC -Z7
set CommonLinkerFlags=-opt:ref -incremental:no /DEBUG


call "%VCVARSALL%" x64 > NUL 2>&1
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

if not exist %BUILD% mkdir %BUILD%

rem Leave info behind for build.sh, which writes compile_commands.json for clangd.
rem INCLUDE was just set by vcvarsall.bat: the Windows SDK + MSVC header folders.
> "%BUILD%\clangd_flags.txt" echo %CommonCompilerFlags%
> "%BUILD%\clangd_include.txt" echo %INCLUDE%

pushd %BUILD%
echo build mode: %MODE%
if exist tufty.pdb del tufty.pdb
if exist win32_tufty_handmade.pdb del win32_tufty_handmade.pdb
"%CLANGCL%" %CommonCompilerFlags% %OptFlag% %CODE%\tufty.cpp -LD /link /MAP:tufty.map %CommonLinkerFlags% /EXPORT:GameUpdateAndRender /EXPORT:GameGetSoundSamples
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
echo tufty.dll: OK
"%CLANGCL%" %CommonCompilerFlags% %OptFlag% %CODE%\win32_tufty_handmade.cpp user32.lib gdi32.lib winmm.lib comdlg32.lib /link /MAP:win32_tufty_handmade.map %CommonLinkerFlags%
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
echo win32_tufty_handmade.exe: OK
popd
