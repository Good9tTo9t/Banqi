# Banqi (Chess Project)

## How to Build and Run

### Option 1: Using the provided script (Windows)
Simply double-click or run the `run.bat` file in the project root. It will automatically generate the CMake build files, compile the project, and run the executable.

### Option 2: Using manual commands
1. Generate the build files:
   ```powershell
   cmake -S . -B build
   ```
2. Compile the project:
   ```powershell
   cmake --build build
   ```
3. Run the executable (from the `src` folder so assets load correctly):
   ```powershell
   cd src
   ..\build\Debug\MyRaylibGame.exe   # If using MSVC
   # OR
   ..\build\MyRaylibGame.exe         # If using MinGW
   ```

## Clean Build
If you need to clean the build files, you can use:
```powershell
cmake --build build --target clean
```