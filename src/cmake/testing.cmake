# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO/

include (CTest)

# Make a build/platform/testsuite directory, and copy the master runtest.py
# there. The rest is up to the tests themselves.
file (MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/testsuite")
file (COPY "${CMAKE_CURRENT_SOURCE_DIR}/testsuite/common"
      DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/testsuite")
add_custom_command (OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/testsuite/runtest.py"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${CMAKE_CURRENT_SOURCE_DIR}/testsuite/runtest.py"
                        "${CMAKE_CURRENT_BINARY_DIR}/testsuite/runtest.py"
                    MAIN_DEPENDENCY "${CMAKE_CURRENT_SOURCE_DIR}/testsuite/runtest.py")
add_custom_target ( CopyFiles ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/testsuite/runtest.py" )

# set(OIIO_TESTSUITE_IMAGEDIR "${PROJECT_SOURCE_DIR}/.." CACHE PATH
set(OIIO_TESTSUITE_IMAGEDIR "${PROJECT_BINARY_DIR}/testsuite" CACHE PATH
    "Location of oiio-images, openexr-images, libtiffpic, etc.." )


# Build a single ENVIRONMENT list entry "PYTHONPATH=..." for CTest.
# On Windows, keep this deterministic and do not append inherited PYTHONPATH:
# semicolon-separated values can be split by CMake list processing when used
# as test ENVIRONMENT entries.
function (oiio_tests_pythonpath_env_entry out_var prefix_dir)
    if (WIN32)
        set (_pythonpath "${prefix_dir}")
    else ()
        if (DEFINED ENV{PYTHONPATH} AND NOT "$ENV{PYTHONPATH}" STREQUAL "")
            string (CONCAT _pythonpath "${prefix_dir}" ":" "$ENV{PYTHONPATH}")
        else ()
            set (_pythonpath "${prefix_dir}")
        endif ()
    endif ()
    set (${out_var} "PYTHONPATH=${_pythonpath}" PARENT_SCOPE)
endfunction ()


# oiio_add_tests() - add a set of test cases.
#
# Usage:
#   oiio_add_tests ( test1 [ test2 ... ]
#                    [ IMAGEDIR name_of_reference_image_directory ]
#                    [ URL http://find.reference.cases.here.com ]
#                    [ FOUNDVAR variable_name ... ]
#                    [ ENABLEVAR variable_name ... ]
#                    [ DISABLEVAR variable_name ... ]
#                    [ SUFFIX suffix ]
#                    [ ENVIRONMENT "VAR=value" ... ]
#                  )
#
# The optional argument IMAGEDIR is used to check whether external test images
# (not supplied with OIIO) are present, and to disable the test cases if
# they're not.  If IMAGEDIR is present, URL should also be included to tell
# the user where to find such tests.
#
# The optional FOUNDVAR introduces variables (typically Foo_FOUND) that if
# not existing and true, will skip the test.
#
# The optional ENABLEVAR introduces variables (typically ENABLE_Foo) that
# if existing and yet false/off/zero, will skip the test. (Not existing
# does NOT disable the test.)
#
# The optional DISABLEVAR introduces variables that, if existing and
# true/on/nonzero, will skip the test.
#
# The optional SUFFIX is appended to the test name.
#
# The optional ENVIRONMENT is a list of environment variables to set for the
# test.
#
macro (oiio_add_tests)
    cmake_parse_arguments (_ats "" "SUFFIX;TESTNAME" "URL;IMAGEDIR;LABEL;FOUNDVAR;ENABLEVAR;DISABLEVAR;ENVIRONMENT" ${ARGN})
       # Arguments: <prefix> <options> <one_value_keywords> <multi_value_keywords> args...
    set (_ats_testdir "${OIIO_TESTSUITE_IMAGEDIR}/${_ats_IMAGEDIR}")
    # If there was a FOUNDVAR param specified and that variable name is
    # not defined, mark the test as broken.
    set (_test_disabled FALSE)
    set (_test_notfound FALSE)
    foreach (_var ${_ats_FOUNDVAR})
        # FOUNDVAR entires had better exist and be true
        if (NOT ${_var})
            set (_ats_LABEL "broken")
            set (_test_notfound TRUE)
        endif ()
    endforeach ()
    set (_test_disabled 0)
    foreach (_var ${_ats_ENABLEVAR})
        # ENABLEVAR, *if* it exists, must be true. But not existing is fine.
        if ((NOT "${${_var}}" STREQUAL "" AND NOT "${${_var}}") OR
            (NOT "$ENV{${_var}}" STREQUAL "" AND NOT "$ENV{${_var}}"))
            set (_ats_LABEL "broken")
            set (_test_disabled TRUE)
        endif ()
    endforeach ()
    foreach (_var ${_ats_DISABLEVAR})
        # DISABLEVAR, if true, disable the test. Not existing is fine.
        if (${_var})
            set (_ats_LABEL "broken")
            set (_test_disabled TRUE)
        endif ()
    endforeach ()
    # Default PYTHONPATH (see oiio_add_all_tests), placed first so that a
    # PYTHONPATH in the caller's ENVIRONMENT overrides it.
    if (NOT "${_pybind_tests_pythonpath}" STREQUAL "")
        list (PREPEND _ats_ENVIRONMENT "${_pybind_tests_pythonpath}")
    endif ()
    # Things we add to the environment for tests:
    # - For OCIO 2.2+, have the testsuite use the default built-in config.
    # - Some tests (e.g. cmake-consumer) configure their own child cmake
    #   project that does find_package(OpenImageIO). Point them at this
    #   build's install location so that works out of the box, without users
    #   needing to set OpenImageIO_ROOT in their environment themselves.
    list (APPEND _ats_ENVIRONMENT "OCIO=ocio://default"
                                  "OIIO_TESTSUITE_OCIOCONFIG=ocio://default"
                                  "OpenImageIO_ROOT=${CMAKE_INSTALL_PREFIX}")
    if (_test_disabled)
        message (STATUS "Skipping test(s) ${_ats_UNPARSED_ARGUMENTS} because of disabled ${_ats_ENABLEVAR}")
    elseif (_test_notfound)
        message (STATUS "Skipping test(s) ${_ats_UNPARSED_ARGUMENTS} because of missing dependency from ${_ats_FOUNDVAR}")
    elseif (_ats_IMAGEDIR AND NOT EXISTS ${_ats_testdir})
        # If the directory containing reference data (images) for the test
        # isn't found, point the user at the URL.
        message (STATUS "\n\nDid not find ${_ats_testdir}")
        message (STATUS "  -> Will not run tests ${_ats_UNPARSED_ARGUMENTS}")
        message (STATUS "  -> You can find it at ${_ats_URL}\n")
    else ()
        # Add the tests if all is well.
        set (_has_generator_expr TRUE)
        set (_testsuite "${CMAKE_SOURCE_DIR}/testsuite")
        foreach (_testname ${_ats_UNPARSED_ARGUMENTS})
            set (_testsrcdir "${_testsuite}/${_testname}")
            set (_testdir "${CMAKE_BINARY_DIR}/testsuite/${_testname}${_ats_SUFFIX}")
            if (_ats_TESTNAME)
                set (_testname "${_ats_TESTNAME}")
            endif ()
            if (_ats_SUFFIX)
                set (_testname "${_testname}${_ats_SUFFIX}")
            endif ()
            if (_ats_LABEL MATCHES "broken")
                set (_testname "${_testname}-broken")
            endif ()

            set (_runtest ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/testsuite/runtest.py" ${_testdir})
            if (MSVC_IDE)
                set (_runtest ${_runtest} --devenv-config $<CONFIGURATION>
                                          --solution-path "${CMAKE_BINARY_DIR}" )
            endif ()

            file (MAKE_DIRECTORY "${_testdir}")

            add_test ( NAME ${_testname} COMMAND ${_runtest} )
            set_property(TEST ${_testname} APPEND PROPERTY ENVIRONMENT
                             "OIIO_TESTSUITE_ROOT=${_testsuite}"
                             "OIIO_TESTSUITE_SRC=${_testsrcdir}"
                             "OIIO_TESTSUITE_CUR=${_testdir}"
                             "Python_EXECUTABLE=${Python3_EXECUTABLE}"
                             ${_ats_ENVIRONMENT})
            if (NOT ${_ats_testdir} STREQUAL "")
                set_property(TEST ${_testname} APPEND PROPERTY ENVIRONMENT
                             "OIIO_TESTSUITE_IMAGEDIR=${_ats_testdir}")
            endif()

        endforeach ()
        message (VERBOSE "TESTS: ${_ats_UNPARSED_ARGUMENTS}")
    endif ()
endmacro ()



# The tests are organized into a macro so it can be called after all the
# directories with plugins are included.
#
macro (oiio_add_all_tests)

    # PYTHONPATH entries pointing at this build's Python bindings.
    # oiio_add_tests gives every test the pybind11 one by default, so any
    # test that runs Python uses this build rather than whatever is in the
    # ambient environment. Backend-specific tests override it with their
    # own ENVIRONMENT entry. Empty where the Python tests can't run (see
    # below for why sanitizer builds are excluded).
    set (_pybind_tests_pythonpath "")
    set (_nanobind_tests_pythonpath "")
    if (USE_PYTHON AND NOT BUILD_OIIOUTIL_ONLY AND NOT SANITIZE)
        if (WIN32)
            # On Windows CI we run the install target before tests. Use the
            # installed package path to avoid multi-config output layout quirks.
            set (_installed_python_site_packages
                "${CMAKE_INSTALL_PREFIX}/${PYTHON_SITE_ROOT_DIR}")
            oiio_tests_pythonpath_env_entry (_pybind_tests_pythonpath
                "${_installed_python_site_packages}")
        else ()
            oiio_tests_pythonpath_env_entry (_pybind_tests_pythonpath
                "${CMAKE_BINARY_DIR}/lib/python/site-packages")
        endif ()
        if (OIIO_PYTHON_BINDINGS_BACKEND STREQUAL "both")
            # In "both" mode, the nanobind module is kept isolated under its
            # own build-tree location so it doesn't clobber the pybind11
            # module that also lives in lib/python/site-packages.
            oiio_tests_pythonpath_env_entry (_nanobind_tests_pythonpath
                "${CMAKE_BINARY_DIR}/lib/python/nanobind")
        else ()
            # nanobind-only builds install the module to the same location
            # pybind11 would have used (see setup_python_module_nanobind()
            # in pythonutils.cmake).
            set (_nanobind_tests_pythonpath "${_pybind_tests_pythonpath}")
        endif ()
    endif ()

    # Freestanding tests:
    oiio_add_tests (
                    cmake-consumer
                    cryptomatte
                    docs-examples-cpp
                    texture-device
                    iinfo igrep
                    nonwhole-tiles
                    oiiotool
                    oiiotool-composite
                    oiiotool-control
                    oiiotool-copy
                    oiiotool-decorrstretch
                    oiiotool-demosaic
                    oiiotool-fixnan
                    oiiotool-layers
                    oiiotool-pattern
                    oiiotool-readerror
                    oiiotool-subimage
                    oiiotool-text
                    oiiotool-thumbnail-get
                    oiiotool-xform
                    diff
                    flip
                    dither dup-channels
                    jpeg jpeg-corrupt jpeg-metadata
                    maketx oiiotool-maketx
                    misnamed-file
                    missingcolor
                    null
                    rational
                   )

    set (all_texture_tests
                    texture-derivs texture-fill
                    texture-flipt texture-gettexels texture-gray
                    texture-interp-bicubic
                    texture-blurtube
                    texture-crop texture-cropover
                    texture-half texture-uint16
                    texture-icwrite
                    texture-interp-bilinear
                    texture-interp-closest
                    texture-levels-stochaniso
                    texture-levels-stochmip
                    texture-mip-nomip texture-mip-onelevel
                    texture-mip-trilinear
                    texture-mip-stochastictrilinear
                    texture-mip-stochasticaniso
                    texture-missing
                    texture-overscan
                    texture-pointsample
                    texture-udim texture-udim2
                    texture-uint8
                    texture-width0blur
                    texture-wrapfill
                    texture-blurfix
                    texture-fat texture-skinny
                    texture-stats
                    texture-threadtimes
                    texture-env
                    texture-colorspace
                   )
    oiio_add_tests (${all_texture_tests})
    # Duplicate texture tests with batch mode
    oiio_add_tests (${all_texture_tests}
                    SUFFIX ".batch"
                    ENVIRONMENT TESTTEX_BATCH=1)

    # Tests that require oiio-images:
    oiio_add_tests (gpsread
                    jpeg-thumbnail
                    oiiotool-attribs
                    texture-filtersize
                    texture-filtersize-stochastic
                    texture-res texture-maxres
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images"
                   )

    # Add tests that require the Python bindings.
    #
    # We also exclude these tests if this is a sanitizer build, because the
    # Python interpreter itself won't be linked with the right asan
    # libraries to run correctly.
    if (USE_PYTHON AND NOT BUILD_OIIOUTIL_ONLY AND NOT SANITIZE)
        # Keep in sync with the pybind11 python-* tests below as dual-backend
        # coverage expands. imageinput/imagebufalgo also need oiio-images.
        set (nanobind_python_tests
             docs-examples-python
             filters
             python-colorconfig
             python-deep
             python-imagebuf
             python-imagecache
             python-imageoutput
             python-imagespec
             python-oiio
             python-paramlist
             python-roi
             python-texturesys
             python-typedesc
             sourceprovenance)
        set (nanobind_python_tests_imagedir
             python-imageinput
             python-imageinput-colorconfig
             python-imagebufalgo)
        set (nanobind_python_test_suffix ".nanobind")
        if (OIIO_BUILD_PYTHON_PYBIND11)
            oiio_add_tests (
                docs-examples-python
                python-colorconfig
                python-deep
                python-imagebuf
                python-imagecache
                python-imageoutput
                python-imagespec
                python-oiio
                python-paramlist
                python-roi
                python-texturesys
                python-typedesc
                sourceprovenance
                filters
                )
            # These Python tests also need access to oiio-images
            oiio_add_tests (
                python-imageinput python-imageinput-colorconfig
                python-imagebufalgo
                IMAGEDIR oiio-images
                )
        else ()
            set (nanobind_python_test_suffix "")
        endif ()

        if (OIIO_BUILD_PYTHON_NANOBIND)
            oiio_add_tests (
                ${nanobind_python_tests}
                SUFFIX ${nanobind_python_test_suffix}
                ENVIRONMENT "${_nanobind_tests_pythonpath}"
                )
            oiio_add_tests (
                ${nanobind_python_tests_imagedir}
                SUFFIX ${nanobind_python_test_suffix}
                IMAGEDIR oiio-images
                ENVIRONMENT "${_nanobind_tests_pythonpath}"
                )
        endif ()
    endif ()

    oiio_add_tests (oiiotool-color
                    FOUNDVAR OpenColorIO_FOUND)

    # Tests to run with HWY enabled.
    # Remember to add tests here as hwy enabled IBA functions are added
    oiio_add_tests ( oiiotool
                     oiiotool-composite
                     oiiotool-xform
                     docs-examples-cpp
        FOUNDVAR hwy_FOUND
        ENABLEVAR OIIO_USE_HWY
        SUFFIX ".hwy"
        ENVIRONMENT "OPENIMAGEIO_ENABLE_HWY=1"
        )

    oiio_add_tests ( python-imagebufalgo
        FOUNDVAR hwy_FOUND
        ENABLEVAR OIIO_USE_HWY  USE_PYTHON OIIO_BUILD_PYTHON_PYBIND11
        DISABLEVAR BUILD_OIIOUTIL_ONLY SANITIZE
        SUFFIX ".hwy"
        ENVIRONMENT "OPENIMAGEIO_ENABLE_HWY=1"
        IMAGEDIR oiio-images
        )

    # Same test, run against the nanobind bindings (mirrors the pybind11
    # variant above so hwy coverage isn't pybind11-only).
    oiio_add_tests ( python-imagebufalgo
        FOUNDVAR hwy_FOUND
        ENABLEVAR OIIO_USE_HWY  USE_PYTHON OIIO_BUILD_PYTHON_NANOBIND
        DISABLEVAR BUILD_OIIOUTIL_ONLY SANITIZE
        SUFFIX "${nanobind_python_test_suffix}.hwy"
        ENVIRONMENT "OPENIMAGEIO_ENABLE_HWY=1" "${_nanobind_tests_pythonpath}"
        IMAGEDIR oiio-images
        )

    # List testsuites for specific formats or features which might be not found
    # or be intentionally disabled, or which need special external reference
    # images from the web that if not found, should skip the tests:
    oiio_add_tests (bmp
                    ENABLEVAR ENABLE_BMP
                    IMAGEDIR oiio-images/bmpsuite)
    oiio_add_tests (cineon
                    ENABLEVAR ENABLE_CINEON
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (dicom
                    FOUNDVAR DCMTK_FOUND ENABLEVAR ENABLE_DCMTK)
    oiio_add_tests (dpx
                    ENABLEVAR ENABLE_DPX
                    IMAGEDIR oiio-images/dpx URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (ktx
                    FOUNDVAR Ktx_FOUND ENABLEVAR ENABLE_KTX
                    IMAGEDIR oiio-images/ktx2)
    oiio_add_tests (dds
                    ENABLEVAR ENABLE_DDS
                    IMAGEDIR oiio-images/dds URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (fits
                    ENABLEVAR ENABLE_FITS
                    IMAGEDIR fits-images
                    URL http://www.cv.nrao.edu/fits/data/tests/)
    oiio_add_tests (ffmpeg
                    ENABLEVAR ENABLE_FFMPEG
                    FOUNDVAR FFmpeg_FOUND)
    oiio_add_tests (gif
                    FOUNDVAR GIF_FOUND ENABLEVAR ENABLE_GIF
                    IMAGEDIR oiio-images/gif URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (hdr
                    ENABLEVAR ENABLE_HDR
                    IMAGEDIR openexr-images
                    URL http://github.com/AcademySoftwareFoundation/openexr-images)
    oiio_add_tests (heif
                    FOUNDVAR Libheif_FOUND
                    ENABLEVAR ENABLE_Libheif
                    IMAGEDIR oiio-images/heif
                    URL http://github.com/AcademySoftwareFoundation/openexr-images)
    oiio_add_tests (ico
                    ENABLEVAR ENABLE_ICO
                    IMAGEDIR oiio-images/ico URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (iff
                    ENABLEVAR ENABLE_IFF
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (jpeg-ultrahdr
                    FOUNDVAR libuhdr_FOUND
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (jpeg2000
                    FOUNDVAR OPENJPEG_FOUND
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (htj2k
                    FOUNDVAR openjph_FOUND
                    IMAGEDIR oiio-images URL "Recent checkout of OpenImageIO-images")
    oiio_add_tests (jpeg2000-j2kp4files
                    FOUNDVAR OPENJPEG_FOUND
                    IMAGEDIR j2kp4files_v1_5
                    URL http://www.itu.int/net/ITU-T/sigdb/speimage/ImageForm-s.aspx?val=10100803)
    oiio_add_tests (jxl
                    FOUNDVAR JXL_FOUND)
    oiio_add_tests (imagebufalgo-opencv
                    FOUNDVAR OpenCV_FOUND)
    set (all_openexr_tests
         openexr-suite openexr-multires openexr-chroma openexr-decreasingy
         openexr-v2 openexr-window perchannel oiiotool-deep openexr-thumbnail)
    if (USE_PYTHON AND NOT SANITIZE)
        list (APPEND all_openexr_tests openexr-copy)
    endif ()
    if (OpenEXR_VERSION VERSION_GREATER_EQUAL 3.1.10)
        # OpenEXR 3.1.10 is the first release where the exr core library
        # properly supported all compression types (DWA in particular).
        list (APPEND all_openexr_tests openexr-compression)
        # openexr-scanlines reads a DWA-compressed file, so it needs the same
        # minimum as openexr-compression for the core-library test variant.
        if (USE_PYTHON AND NOT SANITIZE)
            list (APPEND all_openexr_tests openexr-scanlines)
        endif ()
    endif ()
    if (OpenEXR_VERSION VERSION_GREATER_EQUAL 3.3)
        # OpenEXR 3.3 is when IDManifest was introduced
        list (APPEND all_openexr_tests openexr-idmanifest)
    endif ()
    # Run all OpenEXR tests without core library
    oiio_add_tests (${all_openexr_tests} openexr-luminance-chroma
                    ENVIRONMENT OPENIMAGEIO_OPTIONS=openexr:core=0
                    IMAGEDIR openexr-images
                    URL http://github.com/AcademySoftwareFoundation/openexr-images)
    # For OpenEXR >= 3.1, be sure to test with the core option on
    if (OpenEXR_VERSION VERSION_GREATER_EQUAL 3.1)
        oiio_add_tests (${all_openexr_tests}
                        SUFFIX ".core"
                        ENVIRONMENT OPENIMAGEIO_OPTIONS=openexr:core=1
                        IMAGEDIR openexr-images
                        URL http://github.com/AcademySoftwareFoundation/openexr-images)
    endif ()

    # HTJ2K was introduced in OpenEXR v.3.4.0
    if (OpenEXR_VERSION VERSION_GREATER_EQUAL 3.4.0)
        if (TEST openexr-compression)
            set_property (TEST openexr-compression APPEND PROPERTY ENVIRONMENT
                          "OIIO_OPENEXR_HTJ2K_SUPPORT=1")
        endif()
        if (TEST openexr-compression.core)
            set_property (TEST openexr-compression.core APPEND PROPERTY
                          ENVIRONMENT "OIIO_OPENEXR_HTJ2K_SUPPORT=1")
        endif()
    endif()
    # New compression tests of zstd and lj2k require OpenEXR main branch,
    # or latest release or at least V3.5.0
    if (OpenEXR_VERSION VERSION_GREATER_EQUAL 3.5.0)
        if (TEST openexr-compression)
            set_property (TEST openexr-compression APPEND PROPERTY ENVIRONMENT
                          "OIIO_OPENEXR_LJ2K_ZSTD_SUPPORT=1")
        endif ()
        if (TEST openexr-compression.core)
            set_property (TEST openexr-compression.core APPEND PROPERTY 
                          ENVIRONMENT "OIIO_OPENEXR_LJ2K_ZSTD_SUPPORT=1")
        endif ()
    endif ()
    
    # Regression test (compiles its own helper and generates its own image)
    # for a partial edge-tile heap overflow in the OpenEXR readers.
    oiio_add_tests (openexr-partialtile)
    # Self-contained decompression-bomb regression (ships its own tiny fixture);
    # exercises both the C++ and C-API readers via the openexr:core attribute.
    oiio_add_tests (openexr-bomb)
    # Self-contained multi-part colorInteropID inheritance test (ships its own
    # tiny fixture); exercises both the C++ and C-API readers.
    oiio_add_tests (openexr-multipart-colorspace)
    # if (NOT DEFINED ENV{${PROJECT_NAME}_CI})
    #     oiio_add_tests (openexr-damaged
    #                     IMAGEDIR openexr-images
    #                     URL http://github.com/AcademySoftwareFoundation/openexr-images)
    # endif ()
    oiio_add_tests (openvdb texture-texture3d
                    FOUNDVAR OpenVDB_FOUND ENABLEVAR ENABLE_OpenVDB)
    oiio_add_tests (openvdb texture-texture3d
                    SUFFIX ".batch"
                    ENVIRONMENT TESTTEX_BATCH=1
                    FOUNDVAR OpenVDB_FOUND ENABLEVAR ENABLE_OpenVDB)
    # Self-contained malformed-input regression (ships its own tiny fixtures).
    oiio_add_tests (openvdb-damaged
                    FOUNDVAR OpenVDB_FOUND ENABLEVAR ENABLE_OpenVDB)
    oiio_add_tests (png png-damaged
                    ENABLEVAR ENABLE_PNG
                    IMAGEDIR oiio-images/png)
    # Self-contained decompression-bomb regression (ships its own tiny fixture).
    oiio_add_tests (png-bomb ENABLEVAR ENABLE_PNG)
    oiio_add_tests (pnm
                    ENABLEVAR ENABLE_PNM
                    IMAGEDIR oiio-images)
    oiio_add_tests (psd psd-colormodes
                    ENABLEVAR ENABLE_PSD
                    IMAGEDIR oiio-images/psd)
    oiio_add_tests (psd-empty-layers ENABLEVAR ENABLE_PSD)
    oiio_add_tests (ptex
                    FOUNDVAR Ptex_FOUND ENABLEVAR ENABLE_PTEX)
    oiio_add_tests (raw raw-thumbnail
                    FOUNDVAR LIBRAW_FOUND ENABLEVAR ENABLE_LIBRAW
                    IMAGEDIR oiio-images/raw)
    oiio_add_tests (rla
                    ENABLEVAR ENABLE_RLA
                    IMAGEDIR oiio-images/rla)
    oiio_add_tests (sgi
                    ENABLEVAR ENABLE_SGI
                    IMAGEDIR oiio-images)
    oiio_add_tests (softimage
                    ENABLEVAR ENABLE_SOFTIMAGE
                    IMAGEDIR oiio-images/softimage)
    oiio_add_tests (targa
                    ENABLEVAR ENABLE_TARGA
                    IMAGEDIR oiio-images)
    if (USE_PYTHON AND NOT SANITIZE)
        oiio_add_tests (targa-thumbnail
                    ENABLEVAR ENABLE_TARGA
                    IMAGEDIR oiio-images)
    endif()
    oiio_add_tests (oiiotool-thumbnail-set
                    ENABLEVAR ENABLE_TARGA)
    if (WIN32)
        if (OIIO_BUILD_TOOLS)
            # Add test for long path handling if support is enabled at the system level.
            execute_process (
                COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/testsuite/windows-long-paths/check_registry.py"
                RESULT_VARIABLE _reg_check_status
                OUTPUT_QUIET ERROR_QUIET
            )
            if (_reg_check_status EQUAL 0)
                add_test (NAME windows-long-paths
                          COMMAND
                          ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/testsuite/windows-long-paths/test.py"
                          --source-root "${CMAKE_SOURCE_DIR}"
                          --oiiotool-path $<TARGET_FILE:oiiotool>)
            else ()
                message (STATUS "Skipping Windows long path test: System-level support is not enabled")
            endif ()
        endif ()
    else ()
        oiio_add_tests (term
                        ENABLEVAR ENABLE_TERM)
        # I just could not get this test to work on Windows CI. The test fails
        # when comparing the output, but the saved artifacts compare just fine
        # on my system. Maybe someone will come back to this.
    endif ()
    oiio_add_tests (tiff-suite tiff-depths tiff-misc
                    IMAGEDIR oiio-images/libtiffpic)
    oiio_add_tests (tiff-subimage-seek
                    ENABLEVAR USE_PYTHON)
    # Regression test (compiles its own helper and generates its own image)
    # for a partial edge-tile heap overflow in the TIFF reader.
    oiio_add_tests (tiff-partialtile)
    oiio_add_tests (webp
                    FOUNDVAR WebP_FOUND ENABLEVAR ENABLE_WebP
                    IMAGEDIR oiio-images/webp)
    oiio_add_tests (zfile ENABLEVAR ENABLE_ZFILE
                    IMAGEDIR oiio-images)

    if (SPI_TESTS)
        oiio_add_tests (oiiotool-spi
                        FOUNDVAR SPI_TESTS
                        IMAGEDIR spi-oiio-tests
                        URL "noplace -- it's SPI specific tests")
    endif ()

endmacro()


set (OIIO_LOCAL_TESTDATA_ROOT "${CMAKE_SOURCE_DIR}/.." CACHE PATH
     "Directory to check for local copies of testsuite data")
option (OIIO_DOWNLOAD_MISSING_TESTDATA "Try to download any missing test data" OFF)

function (oiio_get_test_data name)
    cmake_parse_arguments (_ogtd "" "REPO;BRANCH" "" ${ARGN})
       # Arguments: <prefix> <options> <one_value_keywords> <multi_value_keywords> args...
    if (IS_DIRECTORY "${OIIO_LOCAL_TESTDATA_ROOT}/${name}"
        AND NOT EXISTS "${CMAKE_BINARY_DIR}/testsuite/${name}")
        set (_ogtd_LINK_RESULT "")
        message (STATUS "Linking ${name} from ${OIIO_LOCAL_TESTDATA_ROOT}/${name}")
        file (CREATE_LINK "${OIIO_LOCAL_TESTDATA_ROOT}/${name}"
                          "${CMAKE_BINARY_DIR}/testsuite/${name}"
                          SYMBOLIC RESULT _ogtd_LINK_RESULT)
        # Note: Using 'COPY_ON_ERROR' in the above command should have prevented the need to
        # have the manual fall-back below. However, there's been at least one case where a user
        # noticed that copying did not happen if creating the link failed (CMake 3.24). We can
        # adjust this in the future if CMake behavior improves.
        message (VERBOSE "Link result ${_ogtd_LINK_RESULT}")
        if (NOT _ogtd_LINK_RESULT EQUAL 0)
            # Older cmake or failure to link -- copy
            message (STATUS "Copying ${name} from ${OIIO_LOCAL_TESTDATA_ROOT}/${name}")
            file (COPY "${OIIO_LOCAL_TESTDATA_ROOT}/${name}"
                  DESTINATION "${CMAKE_BINARY_DIR}/testsuite")
        endif ()
    elseif (IS_DIRECTORY "${CMAKE_BINARY_DIR}/testsuite/${name}")
        message (STATUS "Test data for ${name} already present in testsuite")
    elseif (OIIO_DOWNLOAD_MISSING_TESTDATA AND _ogtd_REPO)
        # Test data directory didn't exist -- fetch it
        message (STATUS "Cloning ${name} from ${_ogtd_REPO}")
        if (NOT _ogtd_BRANCH)
            set (_ogtd_BRANCH main)
        endif ()
        find_package (Git)
        if (Git_FOUND AND GIT_EXECUTABLE)
            git_clone_with_retry (${name} ${_ogtd_REPO}
                                  ${CMAKE_BINARY_DIR}/testsuite/${name}
                GIT_ARGS --depth 1 -b ${_ogtd_BRANCH})
        else ()
            message (WARNING "${ColorRed}Could not find Git executable, could not download test data from ${_ogtd_REPO}${ColorReset}")
        endif ()
    else ()
        message (STATUS "${ColorRed}Missing test data ${name}${ColorReset}")
    endif ()
endfunction()

function (oiio_setup_test_data)
    # TODO: revert this to target dev branch before merging
    oiio_get_test_data (oiio-images
                        REPO https://github.com/walcht/OpenImageIO-images.git
                        BRANCH ktx2-add-uncompressed-vkformat-files)
                        # BRANCH dev-${OpenImageIO_VERSION_MAJOR}.${OpenImageIO_VERSION_MINOR})
    oiio_get_test_data (openexr-images
                        REPO https://github.com/AcademySoftwareFoundation/openexr-images.git
                        BRANCH main)
    oiio_get_test_data (fits-images)
    oiio_get_test_data (j2kp4files_v1_5)
endfunction ()
