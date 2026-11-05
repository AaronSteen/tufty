@echo off
rem Build mode comes from build.sh: "debug" (default) or "release"
set MODE=%1
if "%MODE%"=="" set MODE=debug
set OptFlag=-Od
if /i "%MODE%"=="release" set OptFlag=-O2
rem Optional second argument from build.sh: "analyze" also runs the static
rem analyzer after a successful compile. Empty means compile only.
rem (%~2 strips the quotes build.sh puts around it, so an empty one becomes truly empty.)
set ANALYZE=%~2

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
echo WAITING FOR PDB > lock.tmp
"%CLANGCL%" %CommonCompilerFlags% %OptFlag% %CODE%\tufty.cpp -LD /link /MAP:tufty.map %CommonLinkerFlags% /EXPORT:GameUpdateAndRender /EXPORT:GameGetSoundSamples /NOIMPLIB /NOEXP
if %ERRORLEVEL% NEQ 0 (del lock.tmp & exit /b %ERRORLEVEL%)
echo tufty.dll: compiled OK
del lock.tmp
"%CLANGCL%" %CommonCompilerFlags% %OptFlag% %CODE%\win32_tufty_handmade.cpp user32.lib gdi32.lib winmm.lib comdlg32.lib /link /MAP:win32_tufty_handmade.map %CommonLinkerFlags%
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
echo win32_tufty_handmade.exe: compiled OK

rem STATIC ANALYSIS (set up Nov 2026)
rem Skipped unless build.sh passed "analyze", because it adds a lot to build time.
rem "goto" jumps to the line labeled :done near the bottom of this file, so
rem everything between here and there is skipped.
if /i not "%ANALYZE%"=="analyze" goto done
rem Runs only when both compiles above succeeded (a failed compile exits before
rem reaching here), so compiler errors always come first in the quickfix list and
rem are never repeated by the analyzer.
rem --analyze runs Clang's static analyzer instead of compiling: it simulates every
rem path through each function looking for bugs like returning a pointer to a local
rem variable. No optimization or linker flags, since nothing is built.
rem Its findings print as "file(line,col): warning: ...", the same shape as compiler
rem output, so build.sh's sed and Neovim's errorformat handle them unchanged.
rem They do NOT fail the build, even with -WX. Each run also leaves a .plist report
rem file here in build\ (for graphical tools; safe to ignore).
rem
rem The analyzer's exit code is always 0, so it can't tell us whether it found
rem anything. Instead, each run's output is saved to a .txt file here in build\,
rem printed with "type" (so it still reaches the quickfix list), and then searched
rem with findstr for a real finding. /c:"..." makes findstr search for that exact
rem text, spaces included; > NUL hides the matching lines it would print. findstr
rem sets ERRORLEVEL to 0 if it found a match. "): warning:" only matches findings
rem that point at a file location, like "tufty.h(125,5): warning: ...", and not
rem the analyzer's location-less "warning: Path diagnostic report ..." notes.
"%CLANGCL%" %CommonCompilerFlags% --analyze %CODE%\tufty.cpp > analysis_tufty.txt 2>&1
type analysis_tufty.txt
findstr /c:"): warning:" analysis_tufty.txt > NUL
if %ERRORLEVEL% EQU 0 (echo tufty.dll: static analysis found issues) else (echo tufty.dll: static analysis OK)

"%CLANGCL%" %CommonCompilerFlags% --analyze %CODE%\win32_tufty_handmade.cpp > analysis_win32.txt 2>&1
type analysis_win32.txt
findstr /c:"): warning:" analysis_win32.txt > NUL
if %ERRORLEVEL% EQU 0 (echo win32_tufty_handmade.exe: static analysis found issues) else (echo win32_tufty_handmade.exe: static analysis OK)

:done
popd
exit /b 0
