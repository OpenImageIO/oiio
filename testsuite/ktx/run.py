#!/usr/bin/env python

# Copyright Contributors to the OpenImageIO project.
# SPDX-License-Identifier: BSD-3-Clause and Apache-2.0
# https://github.com/AcademySoftwareFoundation/OpenImageIO

# All of the test files here are copied, as is, from KTX-Software repo and fall
# under the Apache-2.0 license of KTX-Software:
#
#   Copyright 2013-2020 Mark Callow SPDX-License-Identifier: Apache-2.0

#
# KTX-Software has two sets of ktx2 test files:
#   - a relatively small set for libktx:
#     https://github.com/KhronosGroup/KTX-Software/tree/e2f948066c108b56b8d0052b460b2ac7d34886aa/tests/resources/ktx2
#   - a very larget tests set for ktx tools:
#     https://github.com/KhronosGroup/KTX-Software-CTS/tree/6d23ae9e52cce2ebc6495c4692ec89f632ff70d4
#
# commit hashes:
#   - libktx test files: 6c474d8627999de8acf07d819c196f83d025cd44
#   - ktx tools test files (CTS): 6d23ae9e52cce2ebc6495c4692ec89f632ff70d4
# 
# Since OIIO KTX2 plugin simply forwards all operations to libktx, there is no
# need to do extensive testing on encoding/decoding functionalities. libktx
# already does very extensive testing on thousands of ktx2 inputs. What we do
# instead is that we test that we call libktx correctly and that parameters
# (which are numerous) are passed correctly.
#
# Testing via C++ OpenImageIO API is also done separately in this repo:
#   https://github.com/walcht/ktx-oiio-examples
#

# save the error output
redirect = ' >> out.txt 2>&1 '
files = [

    # raw (uncompressed + non-supercompressed) formats
    "r8g8b8a8_srgb.ktx2",
    "r8g8b8a8_srgb_mip.ktx2",
    "r8g8b8_srgb_mip.ktx2",
    "r8g8b8a8_srgb_3d_7.ktx2",
    "r8g8b8a8_srgb_array_7_mip.ktx2",

    # ETC-compressed formats (not supported)
    # "r8g8b8a8_srgb_mip_etc2.ktx2",
    # "etc2_unorm_array_7.ktx2",

    # raw (uncompressed) and supercompressed formats
    "color_grid_zstd_5.ktx2",
    # "skybox_zstd_22.ktx2", # VK_FORMAT_B10G11R11_UFLOAT_PACK32 not yet supported

    # ASTC-compressed formats
    "r8g8b8a8_srgb_mip_astc.ktx2",
    "ktx_app_astc_8x8.ktx2",
    # "astc_8x8_unorm_array_7.ktx2", # VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK

    # BCn-compressed formats (to be supported)
    # "bc3_unorm_array_7.ktx2",
    # "pattern_02_bc2.ktx2",

    # UASTC formats (widely used within KTX2 container format)
    "color_grid_uastc_zstd_5.ktx2",
    "Iron_Bars_001_normal_uastc_zstd_10.ktx2",
    "ktx_document_uastc_rdo_4_zstd_5.ktx2",
    "cubemap_goldengate_uastc_rdo_4_zstd_5.ktx2",

    # HDR formats
    "Desk_small_zstd_15.ktx2", # VK_FORMAT_R16G16B16_SFLOAT
    "Desk_uastc_hdr4x4_zstd_15.ktx2",
    "Desk_uastc_hdr6x6i.ktx2",
    "Desk_astc_hdr6x6.ktx2", # VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK 

    # Basis LZ/ETC1S formats (widely used within KTX2 container format)
    "kodim17_blze.ktx2",
    "r8g8b8a8_srgb_mip_blze.ktx2",
    "color_grid_blze.ktx2",
    "alpha_simple_blze.ktx2",
    "cubemap_yokohama_blze.ktx2",
    "FlightHelmet_baseColor_blze.ktx2",
    "Iron_Bars_001_normal_blze.ktx2",
    "ktx_document_blze.ktx2",

    # Misc (orientation flags, alpha configurations, etc.)
    "alpha_complex_straight.ktx2",
    "orient_down_metadata.ktx2",
    "orient_up_metadata.ktx2",
]

# Most basic testing; just test `oiiotool --info` on libktx main test files
for f in files:
    command += info_command (OIIO_TESTSUITE_IMAGEDIR + "/" + f)

# Create a simple checker pattern RGBA PNG (has to be RGBA because Basis
# Universal codecs cannot transcode to opaque uncompressed formats)
command += (oiio_app("oiiotool") 
            + " --pattern checker 64x64 4 -d uint8 -o checker_original.png >> out.txt ;\n")

# Default write (with nothing specified) should default to a lossless format
# + supercompression scheme and should match exactly with original input
command += oiiotool ("checker_original.png -o checker_default.ktx2")
command += diff_command ("checker_original.png", "checker_default.ktx2")
command += info_command ("checker_default.ktx2")

# UASTC write test: check generation of an UASTC-based KTX2 file
command += oiiotool ("checker_original.png --attrib ktx:codec uastc " +
                     "--compression zstd --attrib ktx:uastc_rdo 1 -o checker_uastc.ktx2")
command += diff_command ("checker_original.png", "checker_uastc.ktx2")
command += info_command ("checker_uastc.ktx2")

# ETC1S write test: ETC1S-based + ZSTD lvl 20 KTX2 file
command += oiiotool ("checker_original.png --attrib ktx:codec etc1s " +
                     "--compression zstd:20 -o checker_etc1s.ktx2")
command += diff_command ("checker_original.png", "checker_etc1s.ktx2")
command += info_command ("checker_etc1s.ktx2")

# ASTC 6x6 block write test:ASTC-block-compressed + ZLIB lvl 5 KTX2 file
command += oiiotool ("checker_original.png --attrib ktx:codec astc " +
                     "--attrib ktx:astc_block_dimensions 6x6 " +
                     "--compression zip:5 -o checker_astc_6x6.ktx2")
command += diff_command ("checker_original.png", "checker_astc_6x6.ktx2")
command += info_command ("checker_astc_6x6.ktx2")

# Test that re-orientation metadata is handled correctly.
# orient_[up|down]_metadata are essentially the same image with one flipped
# vertically (orient_up_metadata)
command += (oiio_app("oiiotool") + (OIIO_TESTSUITE_IMAGEDIR + "/" + "orient_up_metadata.ktx2")
            + " --reorient -o orient_up_metadata.png >> out.txt ;\n")
command += diff_command (OIIO_TESTSUITE_IMAGEDIR + "/" + "orient_down_metadata.ktx2", "orient_up_metadata.png")

# Roundtrip tests for small compressed/uncompressed files (8x8) of supported Vulkan formats.
vkformat_files = [
    # 8-bit formats
    "valid_R8_SRGB_2D.ktx2",
    "valid_R8_UINT_2D.ktx2",
    "valid_R8_SINT_2D.ktx2",
    "valid_R8_UNORM_2D.ktx2",
    "valid_R8G8_SRGB_2D.ktx2",
    "valid_R8G8_UINT_2D.ktx2",
    "valid_R8G8_SINT_2D.ktx2",
    "valid_R8G8_UNORM_2D.ktx2",
    "valid_R8G8B8_SRGB_2D.ktx2",
    "valid_R8G8B8_UINT_2D.ktx2",
    "valid_R8G8B8_SINT_2D.ktx2",
    "valid_R8G8B8_UNORM_2D.ktx2",
    "valid_R8G8B8A8_SRGB_2D.ktx2",
    "valid_R8G8B8A8_UINT_2D.ktx2",
    "valid_R8G8B8A8_SINT_2D.ktx2",
    "valid_R8G8B8A8_UNORM_2D.ktx2",
    "valid_R8G8B8A8_SRGB_CUBE.ktx2",
    "valid_R8G8B8A8_SRGB_CUBE_MIP.ktx2",
    # "valid_R8G8B8A8_SRGB_CUBE_ARRAY.ktx2",
    # "valid_R8G8B8A8_SRGB_CUBE_ARRAY_MIP.ktx2",
    "valid_R8G8B8A8_SRGB_3D.ktx2",
    "valid_R8G8B8A8_SRGB_3D_MIP.ktx2",

    # 16-bit formats
    "valid_R16_SFLOAT_2D.ktx2",
    "valid_R16_UINT_2D.ktx2",
    "valid_R16_SINT_2D.ktx2",
    "valid_R16_UNORM_2D.ktx2",
    "valid_R16G16_SFLOAT_2D.ktx2",
    "valid_R16G16_UINT_2D.ktx2",
    "valid_R16G16_SINT_2D.ktx2",
    "valid_R16G16_UNORM_2D.ktx2",
    "valid_R16G16B16_SFLOAT_2D.ktx2",
    "valid_R16G16B16_UINT_2D.ktx2",
    "valid_R16G16B16_SINT_2D.ktx2",
    "valid_R16G16B16_UNORM_2D.ktx2",
    "valid_R16G16B16A16_SFLOAT_2D.ktx2",
    "valid_R16G16B16A16_UINT_2D.ktx2",
    "valid_R16G16B16A16_SINT_2D.ktx2",
    "valid_R16G16B16A16_UNORM_2D.ktx2",
    "valid_R16G16B16A16_SFLOAT_CUBE.ktx2",
    "valid_R16G16B16A16_SFLOAT_CUBE_MIP.ktx2",
    "valid_R16G16B16A16_SFLOAT_3D.ktx2",
    "valid_R16G16B16A16_SFLOAT_3D_MIP.ktx2",

    # 32-bit formats
    "valid_R32_SFLOAT_2D.ktx2",
    "valid_R32_UINT_2D.ktx2",
    "valid_R32G32_SFLOAT_2D.ktx2",
    "valid_R32G32_UINT_2D.ktx2",
    "valid_R32G32B32_SFLOAT_2D.ktx2",
    "valid_R32G32B32_UINT_2D.ktx2",
    "valid_R32G32B32A32_SFLOAT_2D.ktx2",
    "valid_R32G32B32A32_UINT_2D.ktx2",
    "valid_R32G32B32A32_SFLOAT_CUBE.ktx2",
    "valid_R32G32B32A32_SFLOAT_CUBE_MIP.ktx2",
    "valid_R32G32B32A32_SFLOAT_3D.ktx2",
    "valid_R32G32B32A32_SFLOAT_3D_MIP.ktx2",

    # 1D textures (these are rarely used)
    "valid_R8G8B8A8_SRGB_1D.ktx2",

    # UASTC + ZSTD/ZLIB levels (test super-compression level roundtrip)
    "valid_R8G8B8_UNORM_2D_UASTC_ZLIB_1.ktx2",
    "valid_R8G8B8_UNORM_2D_UASTC_ZLIB_9.ktx2",
    "valid_R8G8B8_UNORM_2D_UASTC_ZSTD_1.ktx2",
    "valid_R8G8B8_UNORM_2D_UASTC_ZSTD_9.ktx2",

    # ETC1S/BasisLZ roundtrip
    "valid_R8G8B8_UNORM_2D_BLZE.ktx2",
]

for filename in vkformat_files:
    fp = OIIO_TESTSUITE_IMAGEDIR + "/" + filename
    regenerated_filename = f"generated_{filename}";
    command += info_command (fp)
    # Use `--native` to prevent oiiotool from converting input types to float.
    # Without this, the exact same bytes may not be reproduced for, for
    # instance, UINT32 types (which in turn results in the ktxdiff command below
    # to fail).
    command += oiiotool (f"{fp} --native -o {regenerated_filename}")
    command += diff_command (fp, regenerated_filename)

    # For more robust testing; if `ktxdiff` is installed (part of KTX Tools)
    # then you can compare the generated vs. original KTX2 files (headers and
    # texture data) to verify that the VK_FORMAT is the same, primaries and tf
    # are the same, etc. If you see any diff in out.txt then something failed.
    # ktxdiff_path = "<path-to-ktxdiff>"
    # command += f"{ktxdiff_path} {fp} {regenerated_filename} --tolerance 0 --skip-kvd >> out.txt 2>&1;\n"
