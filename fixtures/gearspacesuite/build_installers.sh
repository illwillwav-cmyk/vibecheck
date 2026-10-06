#!/bin/bash
set -e

BUILD_DIR="build"
INSTALLER_DIR="installers"

echo "Building Gearspace Suite Plugins..."

mkdir -p $BUILD_DIR
cd $BUILD_DIR
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel 4

cd ..
mkdir -p $INSTALLER_DIR

for plugin in AntiLoudnessLimiter ZeroDistortionEQ PhasePerfectAligner ScalableCompressor NoDongleReverb FreeDelay GainMatchedSat CPUFriendlySynth RealTimeAnalyzer UpdateProofUtility; do
    echo "Creating installer for $plugin..."
    
    # We create a temporary package root
    pkgroot=$(mktemp -d)
    
    # AU
    if [ -d "$BUILD_DIR/$plugin/${plugin}_artefacts/Release/AU/$plugin.component" ]; then
        mkdir -p "$pkgroot/Library/Audio/Plug-Ins/Components"
        cp -r "$BUILD_DIR/$plugin/${plugin}_artefacts/Release/AU/$plugin.component" "$pkgroot/Library/Audio/Plug-Ins/Components/"
    fi
    
    # VST3
    if [ -d "$BUILD_DIR/$plugin/${plugin}_artefacts/Release/VST3/$plugin.vst3" ]; then
        mkdir -p "$pkgroot/Library/Audio/Plug-Ins/VST3"
        cp -r "$BUILD_DIR/$plugin/${plugin}_artefacts/Release/VST3/$plugin.vst3" "$pkgroot/Library/Audio/Plug-Ins/VST3/"
    fi
    
    pkgbuild --root "$pkgroot" --identifier "com.gearspace.$plugin" --version "1.0.0" "$INSTALLER_DIR/$plugin-Mac.pkg"
    
    rm -rf "$pkgroot"
done

echo "All installers created in $INSTALLER_DIR/"
