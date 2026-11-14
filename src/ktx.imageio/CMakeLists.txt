# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

if (Ktx_FOUND)
    # Get MAJOR.MINOR.PATCH from set Ktx version
    string(REPLACE "." ";" version_list "${Ktx_VERSION}")
    list(GET version_list 0 Ktx_VERSION_MAJOR)
    list(GET version_list 1 Ktx_VERSION_MINOR)
    list(GET version_list 2 Ktx_VERSION_PATCH)

    add_oiio_plugin (ktxinput.cpp ktxoutput.cpp
      LINK_LIBRARIES KTX::ktx
      DEFINITIONS
        "-DKtx_VERSION_MAJOR=${Ktx_VERSION_MAJOR}"
        "-DKtx_VERSION_MINOR=${Ktx_VERSION_MINOR}"
        "-DKtx_VERSION_PATCH=${Ktx_VERSION_PATCH}"
        )
else ()
  message (WARNING "KTX plugin will not be built, no libktx")
  set (format_plugin_definitions ${format_plugin_definitions} DISABLE_KTX=1 PARENT_SCOPE)
endif ()
