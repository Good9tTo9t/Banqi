@echo off
echo ==== Building Project ====
cmake -S . -B build
cmake --build build

echo.
echo ==== Running Project ====
cd src
if exist "..\build\MyRaylibGame.exe" (
    "..\build\MyRaylibGame.exe"
) else if exist "..\build\Debug\MyRaylibGame.exe" (
    "..\build\Debug\MyRaylibGame.exe"
) else (
    echo Executable not found! Please check if the build was successful.
)
cd ..

echo.
pause
