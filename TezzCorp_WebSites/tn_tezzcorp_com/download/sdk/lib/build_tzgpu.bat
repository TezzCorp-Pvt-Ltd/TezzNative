@echo off
REM Run in the supported MSVC developer prompt; pass --cuda-root --arch --out.
python "%~dp0build_tzgpu.py" %*
exit /b %errorlevel%
