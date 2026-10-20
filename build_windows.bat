@echo off
REM Builds Seedling.vst3 on Windows. Needs: Visual Studio 2022 (Desktop C++ workload) and CMake. Run from this folder.
cmake -B build -DCMAKE_BUILD_TYPE=Release || goto :err
cmake --build build --config Release --target Seedling_VST3 || goto :err
echo.
echo Done: build\Seedling_artefacts\Release\VST3\Seedling.vst3
echo Copy that folder to C:\Program Files\Common Files\VST3\
goto :eof
:err
echo Build failed.
exit /b 1
