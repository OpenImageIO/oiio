# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

###########################################################################
# Find external dependencies
###########################################################################

if (NOT VERBOSE)
    set (PkgConfig_FIND_QUIETLY true)
    set (Threads_FIND_QUIETLY true)
endif ()

message (STATUS "${ColorBoldWhite}")
message (STATUS "* Checking for dependencies...")
message (STATUS "*   - Missing a dependency 'Package'?")
message (STATUS "*     Try cmake -DPackage_ROOT=path or set environment var Package_ROOT=path")
message (STATUS "*     For many dependencies, we supply src/build-scripts/build_Package.bash")
message (STATUS "*   - To exclude an optional dependency (even if found),")
message (STATUS "*     -DUSE_Package=OFF or set environment var USE_Package=OFF ")
message (STATUS "${ColorReset}")


set (OIIO_LOCAL_DEPS_PATH "${CMAKE_SOURCE_DIR}/ext/dist" CACHE STRING
     "Local area for dependencies added to CMAKE_PREFIX_PATH")
list (APPEND CMAKE_PREFIX_PATH ${OIIO_LOCAL_DEPS_PATH})

# Tell CMake that find_package should try to find the highest matching version
# of a package, rather than the first one it finds.
set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)


include (FindThreads)


###########################################################################
# Dependencies for required formats and features. These are so critical
# that we will not complete the build if they are not found.

checked_find_package (ZLIB REQUIRED)  # Needed by several packages

# Help set up this target for libtiff config file when using static libtiff
if (NOT TARGET CMath::CMath)
    find_library (MATH_LIBRARY m)
    if (NOT MATH_LIBRARY-NOTFOUND)
        add_library (CMath::CMath UNKNOWN IMPORTED)
        set_property (TARGET CMath::CMath
                      APPEND PROPERTY IMPORTED_LOCATION  ${MATH_LIBRARY})
    endif ()
endif ()

# IlmBase & OpenEXR
checked_find_package (Imath REQUIRED
    VERSION_MIN 3.1
)

checked_find_package (OpenEXR REQUIRED CONFIG
    VERSION_MIN 3.1
    NO_FP_RANGE_CHECK
    )

# Set variables for two things that imported targets can't express: the global
# ordering below, and the PUBLIC/PRIVATE choice for Imath in libOpenImageIO.
# Everything else uses the targets.
get_target_property (IMATH_INCLUDES Imath::Imath
                     INTERFACE_INCLUDE_DIRECTORIES)
get_target_property (OPENEXR_INCLUDES OpenEXR::OpenEXR
                     INTERFACE_INCLUDE_DIRECTORIES)

# Force Imath includes to be before everything else to ensure that we have
# the right Imath/OpenEXR version, not some older version in the system
# library.
include_directories(BEFORE ${IMATH_INCLUDES} ${OPENEXR_INCLUDES})
set (OPENIMAGEIO_IMATH_DEPENDENCY_VISIBILITY "PRIVATE" CACHE STRING
     "Should we expose Imath library dependency as PUBLIC or PRIVATE")
set (OPENIMAGEIO_CONFIG_DO_NOT_FIND_IMATH OFF CACHE BOOL
     "Exclude find_dependency(Imath) from the exported OpenImageIOConfig.cmake")

# JPEG -- prefer JPEG-Turbo to regular libjpeg
checked_find_package (libjpeg-turbo
                      VERSION_MIN 2.1
                      DEFINITIONS USE_JPEG_TURBO=1)
if (TARGET libjpeg-turbo::jpeg) # Try to find the non-turbo version
    # Doctor it so libjpeg-turbo is aliased as JPEG::JPEG
    alias_library_if_not_exists (JPEG::JPEG libjpeg-turbo::jpeg)
    set (JPEG_FOUND TRUE)
else ()
    # Try to find the non-turbo version
    checked_find_package (JPEG REQUIRED
                          VERSION_MIN 9.0)
endif ()


# Ultra HDR
checked_find_package (libuhdr
                      VERSION_MIN 1.3)

# Static libtiff configs may reference Deflate::Deflate and ZSTD::ZSTD without
# importing them (https://gitlab.com/libtiff/libtiff/-/work_items/871), so
# libdeflate and zstd must be located before TIFF discovery. In particular, a
# previously auto-built static TIFF rediscovered from the local deps cache
# needs this; the packages found during build_TIFF.cmake do not carry over to
# later reconfigures.
if (NOT TARGET Deflate::Deflate)
    checked_find_package (libdeflate
                          VERSION_MIN 1.18)
    alias_library_if_not_exists (Deflate::Deflate libdeflate::libdeflate_static)
    alias_library_if_not_exists (Deflate::Deflate libdeflate::libdeflate_shared)
endif ()
if (NOT TARGET ZSTD::ZSTD)
    checked_find_package (zstd VERSION_MIN 1.4)
    alias_library_if_not_exists (ZSTD::ZSTD zstd::libzstd_static)
    alias_library_if_not_exists (ZSTD::ZSTD zstd::libzstd_shared)
endif ()

# WebP must be found before TIFF, so that an auto-built libtiff can use it.
checked_find_package (WebP VERSION_MIN 1.1)

checked_find_package (TIFF REQUIRED
                      VERSION_MIN 4.1
                      RECOMMEND_MIN 4.5
                      RECOMMEND_MIN_REASON "4.2+ for GPS, 4.5+ various security fixes")
alias_library_if_not_exists (TIFF::TIFF TIFF::tiff)

# JPEG XL
option (USE_JXL "Enable JPEG XL support" ON)
checked_find_package (JXL
                      VERSION_MIN 0.10.1
                      DEFINITIONS USE_JXL=1)

# Pugixml setup.  Normally we just use the version bundled with oiio, but
# some linux distros are quite particular about having separate packages so we
# allow this to be overridden to use the distro-provided package if desired.
option (USE_EXTERNAL_PUGIXML "Use an externally built shared library version of the pugixml library" OFF)
if (USE_EXTERNAL_PUGIXML)
    checked_find_package (pugixml CONFIG REQUIRED
                          VERSION_MIN 1.11
                          DEFINITIONS USE_EXTERNAL_PUGIXML=1)
else ()
    message (STATUS "Using internal PugiXML")
endif()

# From pythonutils.cmake
set_option (USE_PYTHON "Enable support of Python bindings" ON)
if (USE_PYTHON)
    find_python()
endif ()
if (USE_PYTHON AND OIIO_BUILD_PYTHON_PYBIND11)
    checked_find_package (pybind11 REQUIRED VERSION_MIN 2.7)
endif ()
if (USE_PYTHON AND OIIO_BUILD_PYTHON_NANOBIND)
    discover_nanobind_cmake_dir()
    checked_find_package (nanobind CONFIG REQUIRED
                          VERSION_MIN 2.8.0 VERSION_MAX 3.9
                          NO_FP_RANGE_CHECK
                          BUILD_LOCAL missing)
endif ()


###########################################################################
# Dependencies for optional formats and features. If these are not found,
# we will continue building, but the related functionality will be disabled.

checked_find_package (PNG VERSION_MIN 1.6.0)
if (TARGET PNG::png_static)
    set (PNG_TARGET PNG::png_static)
elseif (TARGET PNG::PNG)
    set (PNG_TARGET PNG::PNG)
endif ()

checked_find_package (Freetype
                      VERSION_MIN 2.10.0
                      DEFINITIONS USE_FREETYPE=1 )

checked_find_package (OpenColorIO REQUIRED
                      VERSION_MIN 2.3
                      VERSION_MAX 2.9
                     )
if (NOT OPENCOLORIO_INCLUDES)
    get_target_property(OPENCOLORIO_INCLUDES OpenColorIO::OpenColorIO INTERFACE_INCLUDE_DIRECTORIES)
endif ()
include_directories(BEFORE ${OPENCOLORIO_INCLUDES})

checked_find_package (OpenCV VERSION_MIN 4.0
                      PREFER_CONFIG
                      DEFINITIONS USE_OPENCV=1)

# Intel TBB
set (TBB_USE_DEBUG_BUILD OFF)
checked_find_package (TBB 2017
                      SETVARIABLES OIIO_TBB
                      PREFER_CONFIG)

# DCMTK is used to read DICOM images
checked_find_package (DCMTK CONFIG VERSION_MIN 3.6.2)

checked_find_package (FFmpeg VERSION_MIN 4.0)

checked_find_package (GIF VERSION_MIN 5.0)

# For HEIF/HEIC/AVIF formats
checked_find_package (Libheif VERSION_MIN 1.11
                      PREFER_CONFIG
                      RECOMMEND_MIN 1.16
                      RECOMMEND_MIN_REASON "1.16 for orientation support, 1.17 for monochrome support")

checked_find_package (LibRaw
                      VERSION_MIN 0.21.0
                      RECOMMEND_MIN 0.21.3
                      RECOMMEND_MIN_REASON "0.21.3 for many security fixes"
                      PRINT LibRaw_r_LIBRARIES)

checked_find_package (OpenJPEG VERSION_MIN 2.0
                      RECOMMEND_MIN 2.2
                      RECOMMEND_MIN_REASON "for multithreading support"
                      PREFER_CONFIG)
# Note: Recent OpenJPEG versions have exported cmake configs, but we don't
# find them reliable at all, so we stick to our FindOpenJPEG.cmake module.

checked_find_package (openjph VERSION_MIN 0.21.2
                      NO_FP_RANGE_CHECK)
# Note: we need NO_FP_RANGE_CHECK because openjph's exported config uses
# SameMinorVersion compatibility, so passing our minimum to find_package would
# reject any newer minor release.

checked_find_package (OpenVDB
                      VERSION_MIN  9.0
                      DEPS         TBB
                      DEFINITIONS  USE_OPENVDB=1)

checked_find_package (Ptex PREFER_CONFIG)
if (NOT Ptex_FOUND OR NOT Ptex_VERSION)
    # Fallback for inadequate Ptex exported configs. This will eventually
    # disappear when we can 100% trust Ptex's exports.
    unset (Ptex_FOUND)
    checked_find_package (Ptex)
endif ()

option (USE_R3DSDK "Enable R3DSDK (RED camera) support" OFF)
checked_find_package (R3DSDK NO_RECORD_NOTFOUND)  # RED camera

set (NUKE_VERSION "7.0" CACHE STRING "Nuke version to target")
checked_find_package (Nuke NO_RECORD_NOTFOUND)

if ((FFmpeg_FOUND OR FREETYPE_FOUND OR TARGET Freetype::Freetype)
    AND NOT TARGET BZip2::BZip2)
    checked_find_package (BZip2)   # Used by ffmpeg and freetype
endif()


# Qt -- used for iv
option (USE_QT "Use Qt if found" ON)
if (USE_QT)
    checked_find_package (OpenGL)   # used for iv
endif ()
if (USE_QT AND OPENGL_FOUND)
    checked_find_package (Qt6 COMPONENTS Core Gui Widgets OpenGLWidgets)
    if (NOT Qt6_FOUND)
        checked_find_package (Qt5 COMPONENTS Core Gui Widgets OpenGL)
    endif ()
    if (NOT Qt5_FOUND AND NOT Qt6_FOUND AND APPLE)
        message (STATUS "  If you think you installed qt with Homebrew and it still doesn't work,")
        message (STATUS "  try:   export PATH=/usr/local/opt/qt/bin:$PATH")
    endif ()
endif ()


# Google Highway for SIMD (optional optimization)
if (OIIO_USE_HWY)
    checked_find_package (hwy)
endif ()

# Tessil/robin-map. Use its own exported CMake config (target tsl::robin_map)
# rather than a bespoke Find module. This also means that when the nanobind
# Python backend is enabled, nanobind's own CMake code will detect and reuse
# this exact target/version instead of compiling against its private vendored
# copy (see nanobind_build_library()'s `NOT TARGET tsl::robin_map` check).
checked_find_package (Robinmap CONFIG REQUIRED
                      NAMES tsl-robin-map
                      VERSION_MIN 1.3.0
                      NO_FP_RANGE_CHECK
                      BUILD_LOCAL missing
                     )

# fmtlib
set_option (OIIO_INTERNALIZE_FMT "Copy fmt headers into <install>/include/OpenImageIO/detail/fmt" ON)
set_option (OIIO_USE_COMPILED_FMT "Link against compiled fmt::fmt instead of header-only fmt" OFF)
if (OIIO_USE_COMPILED_FMT)
    set (OIIO_USE_COMPILED_FMT_VALUE 1)
else ()
    set (OIIO_USE_COMPILED_FMT_VALUE 0)
endif ()
checked_find_package (fmt REQUIRED
                      VERSION_MIN 9.0
                      BUILD_LOCAL missing
                     )
if (OIIO_USE_COMPILED_FMT)
    get_target_property(FMT_INCLUDE_DIR fmt::fmt INTERFACE_INCLUDE_DIRECTORIES)
else ()
    get_target_property(FMT_INCLUDE_DIR fmt::fmt-header-only INTERFACE_INCLUDE_DIRECTORIES)
endif ()

# Note for Ktx plugin developers:
#   set VERSION_MIN to 0.0.0 if you have a locally-built libktx tracking
#   main branch of KTX-Software because building non-tagged libktx sets
#   the version to 0.0.0.
checked_find_package (Ktx
    # VERSION_MIN 0.0.0 # If you target main branch of KTX-Software (versionless)
    VERSION_MIN 5.0.0
    BUILD_LOCAL missing
)

###########################################################################

list (SORT CFP_ALL_BUILD_DEPS_FOUND COMPARE STRING CASE INSENSITIVE)
message (STATUS "All build dependencies: ${CFP_ALL_BUILD_DEPS_FOUND}")
