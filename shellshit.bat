@echo off
REM

set SRC=assignment.cpp
set OUT=assignment.exe

echo Compiling %SRC%...
g++ -std=c++17 -O2 -Wall %SRC% -o %OUT%

if %errorlevel%==0 (
    echo Compilation successful!
    echo Running program...
    %OUT%
) else (
    echo  Compilation failed.
)
pause