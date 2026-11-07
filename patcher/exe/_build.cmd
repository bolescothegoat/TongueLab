@echo off
rem Builds TongueLabSlideBonePatcher.exe (one static file, no runtime needed).
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
python gen_header.py ..\patch_table.json patch_data.h || exit /b 1
cl /nologo /EHsc /O2 /MT /std:c++20 /W3 /DUNICODE /D_UNICODE patcher.cpp /Fe:TongueLabSlideBonePatcher.exe /link /SUBSYSTEM:CONSOLE bcrypt.lib ole32.lib shell32.lib user32.lib uuid.lib || exit /b 1
echo BUILD-OK
