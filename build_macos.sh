#!/bin/sh
# Builds Seedling.vst3 (and AU) on macOS. Needs: Xcode command line tools and CMake. Run from this folder.
set -e
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target Seedling_VST3 Seedling_AU
echo
echo "Done: build/Seedling_artefacts/Release/VST3/Seedling.vst3"
echo "Copy it to ~/Library/Audio/Plug-Ins/VST3/ (the .component to ~/Library/Audio/Plug-Ins/Components/)"
