#!/usr/bin/env bash

if [[ ! -e build/$PLATFORM ]] ; then
    mkdir -p build/$PLATFORM
fi
if [[ ! -e dist/$PLATFORM ]] ; then
    mkdir -p dist/$PLATFORM
fi

# DEP_DIR="$PWD/ext/dist"
DEP_DIR="$PWD/dist/$PLATFORM"
mkdir -p "$DEP_DIR"
INT_DIR="build/$PLATFORM"
VCPKG_INSTALLATION_ROOT=/c/vcpkg

export CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH:=.}
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$DEP_DIR"
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$BOOST_ROOT"
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$VCPKG_INSTALLATION_ROOT/installed/x64-windows"
export PATH="$PATH:$DEP_DIR/bin:$DEP_DIR/lib:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin:/bin"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DEP_DIR/bin:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DEP_DIR/lib:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/lib"

# export MY_CMAKE_FLAGS="$MY_CMAKE_FLAGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
# export EXR_CMAKE_FLAGS="$EXR_CMAKE_FLAGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"

ls -l "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise/VC/Tools/MSVC"

mkdir ext

# ZLib
pushd ext
git clone -b v1.2.11 https://github.com/madler/zlib.git
cd zlib
mkdir -p $INT_DIR
cd $INT_DIR
cmake ../.. -G "$CMAKE_GENERATOR" -DCMAKE_CONFIGURATION_TYPES="$CMAKE_BUILD_TYPE" -DCMAKE_PREFIX_PATH="$DEP_DIR" -DCMAKE_INSTALL_PREFIX="$DEP_DIR"
cmake --build . --config $CMAKE_BUILD_TYPE --target install
popd
export MY_CMAKE_FLAGS="$MY_CMAKE_FLAGS -DZLIB_LIBRARY=$DEP_DIR/lib/zlib.lib"
export EXR_CMAKE_FLAGS="$EXR_CMAKE_FLAGS -DZLIB_LIBRARY=$DEP_DIR/lib/zlib.lib"

echo "DEP_DIR $DEP_DIR :"
ls -R -l "$DEP_DIR"

EXRCXXFLAGS=" /W1 /EHsc /DWIN32=1 "
EXR_BUILD_TYPE=$CMAKE_BUILD_TYPE
EXRINSTALLDIR=$DEP_DIR
EXRBRANCH=v2.4.0
source src/build-scripts/build_openexr.bash
#export OpenEXR_ROOT=$OPENEXR_ROOT
#export OpenEXR_ROOT=$OPENEXR_ROOT
export PATH="$EXRINSTALLDIR/bin:$EXRINSTALLDIR/lib:$PATH"
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PATH
# the above line is admittedly sketchy

cp $DEP_DIR/lib/*.lib $DEP_DIR/bin
cp $DEP_DIR/bin/*.dll $DEP_DIR/lib
echo "DEP_DIR $DEP_DIR :"
ls -R -l "$DEP_DIR"


vcpkg list
vcpkg update

# vcpkg install zlib:x64-windows
vcpkg install tiff:x64-windows
vcpkg install libpng:x64-windows
# vcpkg install openexr:x64-windows
# vcpkg install libjpeg-turbo:x64-windows
# vcpkg install giflib:x64-windows

# webp openjpeg
#time vcpkg install freetype libraw

echo "$VCPKG_INSTALLATION_ROOT"
ls "$VCPKG_INSTALLATION_ROOT"
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows"
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows/lib"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows/lib"
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"


echo "All VCPkg installs:"
vcpkg list

echo "CMAKE_PREFIX_PATH = $CMAKE_PREFIX_PATH"


# export PATH="$PATH:$DEP_DIR/bin:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/bin"
export PATH="$DEP_DIR/lib:$DEP_DIR/bin:$PATH:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/lib"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$VCPKG_INSTALLATION_ROOT/installed/x64-windows/lib:$DEP_DIR/lib:$DEP_DIR/bin"

src/build-scripts/install_test_images.bash

ls /c/hostedtoolcache/windows/Python
echo "/c/hostedtoolcache/windows/Python/3.6.8/x64"
ls /c/hostedtoolcache/windows/Python/3.6.8/x64


# source src/build-scripts/build_openexr.bash
# export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$ILMBASE_ROOT;$OPENEXR_ROOT"
# source src/build-scripts/build_ocio.bash


if [[ "$PYTHON_VERSION" == "3.6" ]] ; then
    export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;/c/hostedtoolcache/windows/Python/3.6.8/x64"
fi
