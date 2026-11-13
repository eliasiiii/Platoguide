#!/bin/sh
# Needs macOS with Xcode command line tools. No Theos required.
set -e
xcrun -sdk iphoneos clang++ -arch arm64 -miphoneos-version-min=15.0 -std=c++20 -fobjc-arc \
  -dynamiclib -framework Foundation -framework UIKit -framework JavaScriptCore \
  -install_name @rpath/PlatoGuide.dylib -o PlatoGuide.dylib PlatoGuide.mm
file PlatoGuide.dylib
