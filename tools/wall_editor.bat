@echo off
rem Launch the GLtron wall art editor. Uses python from PATH (needs Pillow).
python "%~dp0wall_editor.py" %*
if errorlevel 1 pause
