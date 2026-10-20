#!/bin/sh
# Builds Seedling.vst3 on Linux. Needs: cmake, g++, libx11-dev, libxrandr-dev, libxinerama-dev, libxcursor-dev, libfreetype-dev, libasound2-dev, libgl-dev
set -e
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Seedling_VST3 -j"$(nproc)"
echo "Done: build/Seedling_artefacts/Release/VST3/Seedling.vst3"
