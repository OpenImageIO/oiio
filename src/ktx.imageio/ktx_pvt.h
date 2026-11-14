// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#pragma once

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <ktx.h>

#define Ktx_VERSION \
    OIIO_MAKE_VERSION(Ktx_VERSION_MAJOR, Ktx_VERSION_MINOR, Ktx_VERSION_PATCH)

OIIO_PLUGIN_NAMESPACE_BEGIN

#define DBG if (0)

enum class BlockCompression : uint8_t {
    NONE        = 0u,
    BC1         = 1u,  // aka DXT1
    BC1A        = 2u,
    BC2         = 3u,  // aka DXT3
    BC3         = 4u,  // aka DXT5
    BC4         = 5u,
    BC5         = 6u,
    BC6HU       = 7u,
    BC6HS       = 8u,
    BC7         = 9u,
    ETC1        = 10u,
    ETC2_RGB    = 11u,
    ETC2_RGB_A1 = 12u,
    ETC2_RGBA   = 13u,
    EAC_R11     = 14u,
    EAC_RG11    = 15u,
    ASTC        = 16u,
};



// clang-format off
static std::unordered_map<std::string, ktx_pack_astc_block_dimension_e> astc_block_dims_str_to_enum{
  { "4x4",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 },
  { "5x4",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_5x4 },
  { "5x5",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_5x5 },
  { "6x5",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_6x5 },
  { "6x6",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 },
  { "8x5",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_8x5 },
  { "8x6",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_8x6 },
  { "10x5",  ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_10x5 },
  { "10x6",  ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_10x6 },
  { "8x8",   ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_8x8 },
  { "10x8",  ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_10x8 },
  { "10x10", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_10x10 },
  { "12x10", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_12x10 },
  { "12x12", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_12x12 },
  { "3x3x3", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_3x3x3 },
  { "4x3x3", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x3x3 },
  { "4x4x3", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x4x3 },
  { "4x4x4", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x4x4 },
  { "5x4x4", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_5x4x4 },
  { "5x5x4", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_5x5x4 },
  { "5x5x5", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_5x5x5 },
  { "6x5x5", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_6x5x5 },
  { "6x6x5", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_6x6x5 },
  { "6x6x6", ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_6x6x6 },
};
// clang-format on



// clang-format off
static std::unordered_map<std::string, khr_df_primaries_e> primaries_str_to_enum{
  { "none",         khr_df_primaries_e::KHR_DF_PRIMARIES_UNSPECIFIED },
  { "bt709",        khr_df_primaries_e::KHR_DF_PRIMARIES_BT709 },
  { "srgb",         khr_df_primaries_e::KHR_DF_PRIMARIES_SRGB },
  { "bt601_ebu",    khr_df_primaries_e::KHR_DF_PRIMARIES_BT601_EBU },
  { "bt601_smpte",  khr_df_primaries_e::KHR_DF_PRIMARIES_BT601_SMPTE },
  { "bt2020",       khr_df_primaries_e::KHR_DF_PRIMARIES_BT2020 },
  { "ciexyz",       khr_df_primaries_e::KHR_DF_PRIMARIES_CIEXYZ },
  { "aces",         khr_df_primaries_e::KHR_DF_PRIMARIES_ACES },
  { "acescc",       khr_df_primaries_e::KHR_DF_PRIMARIES_ACESCC },
  { "ntsc1953",     khr_df_primaries_e::KHR_DF_PRIMARIES_NTSC1953 },
  { "pal525",       khr_df_primaries_e::KHR_DF_PRIMARIES_PAL525 },
  { "displayp3",    khr_df_primaries_e::KHR_DF_PRIMARIES_DISPLAYP3 },
  { "adobergb",     khr_df_primaries_e::KHR_DF_PRIMARIES_ADOBERGB },
};
// clang-format on



// clang-format off
static std::unordered_map<std::string, khr_df_transfer_e> tf_str_to_enum{
  { "none",           khr_df_transfer_e::KHR_DF_TRANSFER_UNSPECIFIED },
  { "linear",         khr_df_transfer_e::KHR_DF_TRANSFER_LINEAR },
  { "srgb",           khr_df_transfer_e::KHR_DF_TRANSFER_SRGB },
  { "srgb_eotf",      khr_df_transfer_e::KHR_DF_TRANSFER_SRGB_EOTF },
  { "scrgb",          khr_df_transfer_e::KHR_DF_TRANSFER_SCRGB },
  { "scrgb_eotf",     khr_df_transfer_e::KHR_DF_TRANSFER_SCRGB_EOTF },
  { "itu",            khr_df_transfer_e::KHR_DF_TRANSFER_ITU },
  { "itu_oetf",       khr_df_transfer_e::KHR_DF_TRANSFER_ITU_OETF },
  { "bt601",          khr_df_transfer_e::KHR_DF_TRANSFER_BT601 },
  { "bt601_oetf",     khr_df_transfer_e::KHR_DF_TRANSFER_BT601_OETF },
  { "bt709",          khr_df_transfer_e::KHR_DF_TRANSFER_BT709 },
  { "bt709_oetf",     khr_df_transfer_e::KHR_DF_TRANSFER_BT709_OETF },
  { "bt2020",         khr_df_transfer_e::KHR_DF_TRANSFER_BT2020 },
  { "bt2020_oetf",    khr_df_transfer_e::KHR_DF_TRANSFER_BT2020_OETF },
  { "smpte170m",      khr_df_transfer_e::KHR_DF_TRANSFER_SMTPE170M },
  { "smpte170m_oetf", khr_df_transfer_e::KHR_DF_TRANSFER_SMTPE170M_OETF },
  { "smpte170m_eotf", khr_df_transfer_e::KHR_DF_TRANSFER_SMTPE170M_EOTF },
  { "ntsc",           khr_df_transfer_e::KHR_DF_TRANSFER_NTSC },
  { "ntsc_eotf",      khr_df_transfer_e::KHR_DF_TRANSFER_NTSC_EOTF },
  { "slog",           khr_df_transfer_e::KHR_DF_TRANSFER_SLOG },
  { "slog_oetf",      khr_df_transfer_e::KHR_DF_TRANSFER_SLOG_OETF },
  { "slog2",          khr_df_transfer_e::KHR_DF_TRANSFER_SLOG2 },
  { "slog2_oetf",     khr_df_transfer_e::KHR_DF_TRANSFER_SLOG2_OETF },
  { "bt1886",         khr_df_transfer_e::KHR_DF_TRANSFER_BT1886 },
  { "bt1886_eotf",    khr_df_transfer_e::KHR_DF_TRANSFER_BT1886_EOTF },
  { "hlg_oetf",       khr_df_transfer_e::KHR_DF_TRANSFER_HLG_OETF },
  { "hlg_eotf",       khr_df_transfer_e::KHR_DF_TRANSFER_HLG_EOTF },
  { "pq_oetf",        khr_df_transfer_e::KHR_DF_TRANSFER_PQ_OETF},
  { "pg_eotf",        khr_df_transfer_e::KHR_DF_TRANSFER_PQ_EOTF },
  { "dcip3",          khr_df_transfer_e::KHR_DF_TRANSFER_DCIP3 },
  { "dcip3_eotf",     khr_df_transfer_e::KHR_DF_TRANSFER_DCIP3_EOTF },
  { "pal_oetf",       khr_df_transfer_e::KHR_DF_TRANSFER_PAL_OETF },
  { "pal625_eotf",    khr_df_transfer_e::KHR_DF_TRANSFER_PAL625_EOTF },
  { "st240",          khr_df_transfer_e::KHR_DF_TRANSFER_ST240 },
  { "st240_oetf",     khr_df_transfer_e::KHR_DF_TRANSFER_ST240_OETF },
  { "st240_eotf",     khr_df_transfer_e::KHR_DF_TRANSFER_ST240_EOTF },
  { "acescc",         khr_df_transfer_e::KHR_DF_TRANSFER_ACESCC },
  { "acescc_oetf",    khr_df_transfer_e::KHR_DF_TRANSFER_ACESCC_OETF },
  { "acescct",        khr_df_transfer_e::KHR_DF_TRANSFER_ACESCCT },
  { "acescct_oetf",   khr_df_transfer_e::KHR_DF_TRANSFER_ACESCCT_OETF },
  { "abobergb",       khr_df_transfer_e::KHR_DF_TRANSFER_ADOBERGB },
  { "adobergb_eotf",  khr_df_transfer_e::KHR_DF_TRANSFER_ADOBERGB_EOTF },
};
// clang-format on



struct KtxColorID {
    khr_df_transfer_e tf;
    khr_df_primaries_e primaries;
};



// clang-format off
///
/// Only color spaces supported by glTF are: sRGB and data (non-color).
/// Essentially: "srgb_rec709_display", "srgb_rec709_scene",
///              "srgbe_rec709_display", and "data". All the others are
///              supported by KTX but are not supported by glTF.
///
/// Note:
/// If colorspace is `data` then primaries MUST be set to UNSPECIFIED and
/// tf MUST be set to LINEAR. These are part of the requirements for
/// integrating KTX2 within glTF as can be read from here:
///
/// https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_texture_basisu/README.md#additional-requirements
///
static std::unordered_map<std::string, KtxColorID> color_interop_id_map{
  { "lin_ciexyzd65_display",  { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_CIEXYZ } },
  { "lin_ciexyzd65_scene",    { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_CIEXYZ } },
  { "lin_rec709_display",     { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_BT709 } },
  { "lin_rec709_scene",       { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_BT709 } },
  { "lin_rec2020_display",    { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_BT2020 } },
  { "lin_rec2020_scene",      { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_BT2020 } },
  { "lin_p3d65_display",      { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "lin_p3d65_scene",        { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "lin_adobergb_display",   { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_ADOBERGB } },
  { "lin_adobergb_scene",     { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_ADOBERGB } },
  { "lin_ap0_scene",          { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_ACES } },
  { "lin_ap1_scene",          { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_ACESCC } },
  { "srgb_ap1_scene",         { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_ACESCC } },
  { "g22_rec709_display",     { KHR_DF_TRANSFER_NTSC,         KHR_DF_PRIMARIES_BT709 } },
  { "g22_rec709_scene",       { KHR_DF_TRANSFER_NTSC,         KHR_DF_PRIMARIES_BT709 } },
  { "g22_p3d65_display",      { KHR_DF_TRANSFER_NTSC,         KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "g22_p3d65_scene",        { KHR_DF_TRANSFER_NTSC,         KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "g22_adobergb_display",   { KHR_DF_TRANSFER_ADOBERGB,     KHR_DF_PRIMARIES_ADOBERGB } },
  { "g22_adobergb_scene",     { KHR_DF_TRANSFER_ADOBERGB,     KHR_DF_PRIMARIES_ADOBERGB } },
  { "g24_rec709_display",     { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT709 } },
  { "g24_rec709_scene",       { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT709 } },
  { "g24_p3d65_display",      { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "g24_p3d65_scene",        { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "g24_rec2020_display",    { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT2020 } },
  { "g24_rec2020_scene",      { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT2020 } },
  { "g24_rec601_display",     { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT601_SMPTE } },
  { "g24_rec601pal_display",  { KHR_DF_TRANSFER_BT1886,       KHR_DF_PRIMARIES_BT601_EBU } },
  { "srgb_rec709_display",    { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_BT709 } },
  { "srgb_rec709_scene",      { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_BT709 } },
  { "srgbe_rec709_display",   { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_BT709 } },
  { "srgb_p3d65_display",     { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "srgb_p3d65_scene",       { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "srgbe_p3d65_display",    { KHR_DF_TRANSFER_SRGB,         KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "g26_xyzd65_display",     { KHR_DF_TRANSFER_DCIP3,        KHR_DF_PRIMARIES_CIEXYZ } },
  { "itu709_rec709_scene",    { KHR_DF_TRANSFER_ITU,          KHR_DF_PRIMARIES_BT709 } },
  { "pq_rec709_display",      { KHR_DF_TRANSFER_PQ_EOTF,      KHR_DF_PRIMARIES_BT709 } },
  { "pq_p3d65_display",       { KHR_DF_TRANSFER_PQ_EOTF,      KHR_DF_PRIMARIES_DISPLAYP3 } },
  { "pq_rec2020_display",     { KHR_DF_TRANSFER_PQ_EOTF,      KHR_DF_PRIMARIES_BT2020 } },
  { "hlg_rec2020_display",    { KHR_DF_TRANSFER_HLG_EOTF,     KHR_DF_PRIMARIES_BT2020 } },
  { "data",                   { KHR_DF_TRANSFER_LINEAR,       KHR_DF_PRIMARIES_UNSPECIFIED } },
  { "unknown",                { KHR_DF_TRANSFER_UNSPECIFIED,  KHR_DF_PRIMARIES_UNSPECIFIED } },
};
// clang-format on



static inline std::string
block_compression_name(BlockCompression cmp)
{
    switch (cmp) {
    case BlockCompression::NONE: return "none";
    case BlockCompression::BC1: return "bc1";
    case BlockCompression::BC1A: return "bc1a";
    case BlockCompression::BC2: return "bc2";
    case BlockCompression::BC3: return "bc3";
    case BlockCompression::BC4: return "bc4";
    case BlockCompression::BC5: return "bc5";
    case BlockCompression::BC6HU: return "bc6hu";
    case BlockCompression::BC6HS: return "bc6hs";
    case BlockCompression::BC7: return "bc7";
    case BlockCompression::ETC1: return "etc1";
    case BlockCompression::ETC2_RGB: return "etc2_rgb";
    case BlockCompression::ETC2_RGB_A1: return "etc2_rgb_a1";
    case BlockCompression::ETC2_RGBA: return "etc2_rgba";
    case BlockCompression::EAC_R11: return "eac_r11";
    case BlockCompression::EAC_RG11: return "eac_rg11";
    case BlockCompression::ASTC: return "astc";
    default: break;
    }
    return std::to_string(static_cast<uint8_t>(cmp));
}



static inline std::string
basis_universal_codec_name(khr_df_model_e color_model)
{
    switch (color_model) {
    // UASTC codecs
    case khr_df_model_e::KHR_DF_MODEL_UASTC_LDR_4x4: return "uastc-ldr-4x4";
    case khr_df_model_e::KHR_DF_MODEL_UASTC_HDR_4x4: return "uastc-hdr-4x4";
    case khr_df_model_e::KHR_DF_MODEL_UASTC_HDR_6x6: return "uastc-hdr-6x6";
    // ETC1S codecs
    case khr_df_model_e::KHR_DF_MODEL_ETC1S: return "etc1s";
    default: break;
    }
    return std::to_string(static_cast<uint32_t>(color_model));
}



// clang-format off
//
// Copied from KTX-Software/lib/src/vkformat_enum.h
// KTX-Software commit: b9bcfeb1df982f38b11d460cdd9878aa748ebafb
// Autogenerating this proved to be a bigger headache that simply copying and
// pasting the autogenerated enum from libktx.
//
enum VkFormat : uint32_t {
    VK_FORMAT_UNDEFINED                                  = 0,
    VK_FORMAT_R4G4_UNORM_PACK8                           = 1,  // <-- NOT SUPPORTED
    VK_FORMAT_R4G4B4A4_UNORM_PACK16                      = 2,  // <-- NOT SUPPORTED
    VK_FORMAT_B4G4R4A4_UNORM_PACK16                      = 3,  // <-- NOT SUPPORTED
    VK_FORMAT_R5G6B5_UNORM_PACK16                        = 4,  // <-- NOT SUPPORTED
    VK_FORMAT_B5G6R5_UNORM_PACK16                        = 5,  // <-- NOT SUPPORTED
    VK_FORMAT_R5G5B5A1_UNORM_PACK16                      = 6,  // <-- NOT SUPPORTED
    VK_FORMAT_B5G5R5A1_UNORM_PACK16                      = 7,  // <-- NOT SUPPORTED
    VK_FORMAT_A1R5G5B5_UNORM_PACK16                      = 8,  // <-- NOT SUPPORTED
    VK_FORMAT_R8_UNORM                                   = 9,
    VK_FORMAT_R8_SNORM                                   = 10,
    VK_FORMAT_R8_USCALED                                 = 11,  // <-- NOT SUPPORTED
    VK_FORMAT_R8_SSCALED                                 = 12,  // <-- NOT SUPPORTED
    VK_FORMAT_R8_UINT                                    = 13,
    VK_FORMAT_R8_SINT                                    = 14,
    VK_FORMAT_R8_SRGB                                    = 15,
    VK_FORMAT_R8G8_UNORM                                 = 16,
    VK_FORMAT_R8G8_SNORM                                 = 17,
    VK_FORMAT_R8G8_USCALED                               = 18,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8_SSCALED                               = 19,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8_UINT                                  = 20,
    VK_FORMAT_R8G8_SINT                                  = 21,
    VK_FORMAT_R8G8_SRGB                                  = 22,
    VK_FORMAT_R8G8B8_UNORM                               = 23,
    VK_FORMAT_R8G8B8_SNORM                               = 24,
    VK_FORMAT_R8G8B8_USCALED                             = 25,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8B8_SSCALED                             = 26,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8B8_UINT                                = 27,
    VK_FORMAT_R8G8B8_SINT                                = 28,
    VK_FORMAT_R8G8B8_SRGB                                = 29,
    VK_FORMAT_B8G8R8_UNORM                               = 30,
    VK_FORMAT_B8G8R8_SNORM                               = 31,
    VK_FORMAT_B8G8R8_USCALED                             = 32,  // <-- NOT SUPPORTED
    VK_FORMAT_B8G8R8_SSCALED                             = 33,  // <-- NOT SUPPORTED
    VK_FORMAT_B8G8R8_UINT                                = 34,
    VK_FORMAT_B8G8R8_SINT                                = 35,
    VK_FORMAT_B8G8R8_SRGB                                = 36,
    VK_FORMAT_R8G8B8A8_UNORM                             = 37,
    VK_FORMAT_R8G8B8A8_SNORM                             = 38,
    VK_FORMAT_R8G8B8A8_USCALED                           = 39,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8B8A8_SSCALED                           = 40,  // <-- NOT SUPPORTED
    VK_FORMAT_R8G8B8A8_UINT                              = 41,
    VK_FORMAT_R8G8B8A8_SINT                              = 42,
    VK_FORMAT_R8G8B8A8_SRGB                              = 43,
    VK_FORMAT_B8G8R8A8_UNORM                             = 44,
    VK_FORMAT_B8G8R8A8_SNORM                             = 45,
    VK_FORMAT_B8G8R8A8_USCALED                           = 46,  // <-- NOT SUPPORTED
    VK_FORMAT_B8G8R8A8_SSCALED                           = 47,  // <-- NOT SUPPORTED
    VK_FORMAT_B8G8R8A8_UINT                              = 48,
    VK_FORMAT_B8G8R8A8_SINT                              = 49,
    VK_FORMAT_B8G8R8A8_SRGB                              = 50,
    VK_FORMAT_A8B8G8R8_UNORM_PACK32                      = 51,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_SNORM_PACK32                      = 52,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_USCALED_PACK32                    = 53,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_SSCALED_PACK32                    = 54,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_UINT_PACK32                       = 55,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_SINT_PACK32                       = 56,  // <-- NOT SUPPORTED
    VK_FORMAT_A8B8G8R8_SRGB_PACK32                       = 57,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_UNORM_PACK32                   = 58,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_SNORM_PACK32                   = 59,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_USCALED_PACK32                 = 60,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_SSCALED_PACK32                 = 61,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_UINT_PACK32                    = 62,  // <-- NOT SUPPORTED
    VK_FORMAT_A2R10G10B10_SINT_PACK32                    = 63,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_UNORM_PACK32                   = 64,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_SNORM_PACK32                   = 65,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_USCALED_PACK32                 = 66,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_SSCALED_PACK32                 = 67,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_UINT_PACK32                    = 68,  // <-- NOT SUPPORTED
    VK_FORMAT_A2B10G10R10_SINT_PACK32                    = 69,  // <-- NOT SUPPORTED
    VK_FORMAT_R16_UNORM                                  = 70,
    VK_FORMAT_R16_SNORM                                  = 71,
    VK_FORMAT_R16_USCALED                                = 72,  // <-- NOT SUPPORTED
    VK_FORMAT_R16_SSCALED                                = 73,  // <-- NOT SUPPORTED
    VK_FORMAT_R16_UINT                                   = 74,
    VK_FORMAT_R16_SINT                                   = 75,
    VK_FORMAT_R16_SFLOAT                                 = 76,
    VK_FORMAT_R16G16_UNORM                               = 77,
    VK_FORMAT_R16G16_SNORM                               = 78,
    VK_FORMAT_R16G16_USCALED                             = 79,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16_SSCALED                             = 80,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16_UINT                                = 81,
    VK_FORMAT_R16G16_SINT                                = 82,
    VK_FORMAT_R16G16_SFLOAT                              = 83,
    VK_FORMAT_R16G16B16_UNORM                            = 84,
    VK_FORMAT_R16G16B16_SNORM                            = 85,
    VK_FORMAT_R16G16B16_USCALED                          = 86,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16B16_SSCALED                          = 87,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16B16_UINT                             = 88,
    VK_FORMAT_R16G16B16_SINT                             = 89,
    VK_FORMAT_R16G16B16_SFLOAT                           = 90,
    VK_FORMAT_R16G16B16A16_UNORM                         = 91,
    VK_FORMAT_R16G16B16A16_SNORM                         = 92,
    VK_FORMAT_R16G16B16A16_USCALED                       = 93,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16B16A16_SSCALED                       = 94,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16B16A16_UINT                          = 95,
    VK_FORMAT_R16G16B16A16_SINT                          = 96,
    VK_FORMAT_R16G16B16A16_SFLOAT                        = 97,
    VK_FORMAT_R32_UINT                                   = 98,
    VK_FORMAT_R32_SINT                                   = 99,
    VK_FORMAT_R32_SFLOAT                                 = 100,
    VK_FORMAT_R32G32_UINT                                = 101,
    VK_FORMAT_R32G32_SINT                                = 102,
    VK_FORMAT_R32G32_SFLOAT                              = 103,
    VK_FORMAT_R32G32B32_UINT                             = 104,
    VK_FORMAT_R32G32B32_SINT                             = 105,
    VK_FORMAT_R32G32B32_SFLOAT                           = 106,
    VK_FORMAT_R32G32B32A32_UINT                          = 107,
    VK_FORMAT_R32G32B32A32_SINT                          = 108,
    VK_FORMAT_R32G32B32A32_SFLOAT                        = 109,
    VK_FORMAT_R64_UINT                                   = 110,
    VK_FORMAT_R64_SINT                                   = 111,
    VK_FORMAT_R64_SFLOAT                                 = 112,
    VK_FORMAT_R64G64_UINT                                = 113,
    VK_FORMAT_R64G64_SINT                                = 114,
    VK_FORMAT_R64G64_SFLOAT                              = 115,
    VK_FORMAT_R64G64B64_UINT                             = 116,
    VK_FORMAT_R64G64B64_SINT                             = 117,
    VK_FORMAT_R64G64B64_SFLOAT                           = 118,
    VK_FORMAT_R64G64B64A64_UINT                          = 119,
    VK_FORMAT_R64G64B64A64_SINT                          = 120,
    VK_FORMAT_R64G64B64A64_SFLOAT                        = 121,
    VK_FORMAT_B10G11R11_UFLOAT_PACK32                    = 122,  // <-- NOT SUPPORTED
    VK_FORMAT_E5B9G9R9_UFLOAT_PACK32                     = 123,  // <-- NOT SUPPORTED
    VK_FORMAT_D16_UNORM                                  = 124,  // <-- NOT SUPPORTED
    VK_FORMAT_X8_D24_UNORM_PACK32                        = 125,  // <-- NOT SUPPORTED
    VK_FORMAT_D32_SFLOAT                                 = 126,  // <-- NOT SUPPORTED
    VK_FORMAT_S8_UINT                                    = 127,  // <-- NOT SUPPORTED
    VK_FORMAT_D16_UNORM_S8_UINT                          = 128,  // <-- NOT SUPPORTED
    VK_FORMAT_D24_UNORM_S8_UINT                          = 129,  // <-- NOT SUPPORTED
    VK_FORMAT_D32_SFLOAT_S8_UINT                         = 130,  // <-- NOT SUPPORTED
    VK_FORMAT_BC1_RGB_UNORM_BLOCK                        = 131,  // TODO: BCn formats will be supported soon
    VK_FORMAT_BC1_RGB_SRGB_BLOCK                         = 132,
    VK_FORMAT_BC1_RGBA_UNORM_BLOCK                       = 133,
    VK_FORMAT_BC1_RGBA_SRGB_BLOCK                        = 134,
    VK_FORMAT_BC2_UNORM_BLOCK                            = 135,
    VK_FORMAT_BC2_SRGB_BLOCK                             = 136,
    VK_FORMAT_BC3_UNORM_BLOCK                            = 137,
    VK_FORMAT_BC3_SRGB_BLOCK                             = 138,
    VK_FORMAT_BC4_UNORM_BLOCK                            = 139,
    VK_FORMAT_BC4_SNORM_BLOCK                            = 140,
    VK_FORMAT_BC5_UNORM_BLOCK                            = 141,
    VK_FORMAT_BC5_SNORM_BLOCK                            = 142,
    VK_FORMAT_BC6H_UFLOAT_BLOCK                          = 143,
    VK_FORMAT_BC6H_SFLOAT_BLOCK                          = 144,
    VK_FORMAT_BC7_UNORM_BLOCK                            = 145,
    VK_FORMAT_BC7_SRGB_BLOCK                             = 146,
    VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK                    = 147,  // <-- NOT SUPPORTED
    VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK                     = 148,  // <-- NOT SUPPORTED
    VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK                  = 149,  // <-- NOT SUPPORTED
    VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK                   = 150,  // <-- NOT SUPPORTED
    VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK                  = 151,  // <-- NOT SUPPORTED
    VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK                   = 152,  // <-- NOT SUPPORTED
    VK_FORMAT_EAC_R11_UNORM_BLOCK                        = 153,  // <-- NOT SUPPORTED
    VK_FORMAT_EAC_R11_SNORM_BLOCK                        = 154,  // <-- NOT SUPPORTED
    VK_FORMAT_EAC_R11G11_UNORM_BLOCK                     = 155,  // <-- NOT SUPPORTED
    VK_FORMAT_EAC_R11G11_SNORM_BLOCK                     = 156,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4_UNORM_BLOCK                       = 157,
    VK_FORMAT_ASTC_4x4_SRGB_BLOCK                        = 158,
    VK_FORMAT_ASTC_5x4_UNORM_BLOCK                       = 159,
    VK_FORMAT_ASTC_5x4_SRGB_BLOCK                        = 160,
    VK_FORMAT_ASTC_5x5_UNORM_BLOCK                       = 161,
    VK_FORMAT_ASTC_5x5_SRGB_BLOCK                        = 162,
    VK_FORMAT_ASTC_6x5_UNORM_BLOCK                       = 163,
    VK_FORMAT_ASTC_6x5_SRGB_BLOCK                        = 164,
    VK_FORMAT_ASTC_6x6_UNORM_BLOCK                       = 165,
    VK_FORMAT_ASTC_6x6_SRGB_BLOCK                        = 166,
    VK_FORMAT_ASTC_8x5_UNORM_BLOCK                       = 167,
    VK_FORMAT_ASTC_8x5_SRGB_BLOCK                        = 168,
    VK_FORMAT_ASTC_8x6_UNORM_BLOCK                       = 169,
    VK_FORMAT_ASTC_8x6_SRGB_BLOCK                        = 170,
    VK_FORMAT_ASTC_8x8_UNORM_BLOCK                       = 171,
    VK_FORMAT_ASTC_8x8_SRGB_BLOCK                        = 172,
    VK_FORMAT_ASTC_10x5_UNORM_BLOCK                      = 173,
    VK_FORMAT_ASTC_10x5_SRGB_BLOCK                       = 174,
    VK_FORMAT_ASTC_10x6_UNORM_BLOCK                      = 175,
    VK_FORMAT_ASTC_10x6_SRGB_BLOCK                       = 176,
    VK_FORMAT_ASTC_10x8_UNORM_BLOCK                      = 177,
    VK_FORMAT_ASTC_10x8_SRGB_BLOCK                       = 178,
    VK_FORMAT_ASTC_10x10_UNORM_BLOCK                     = 179,
    VK_FORMAT_ASTC_10x10_SRGB_BLOCK                      = 180,
    VK_FORMAT_ASTC_12x10_UNORM_BLOCK                     = 181,
    VK_FORMAT_ASTC_12x10_SRGB_BLOCK                      = 182,
    VK_FORMAT_ASTC_12x12_UNORM_BLOCK                     = 183,
    VK_FORMAT_ASTC_12x12_SRGB_BLOCK                      = 184,
    VK_FORMAT_G8B8G8R8_422_UNORM                         = 1000156000,  // <-- NOT SUPPORTED
    VK_FORMAT_B8G8R8G8_422_UNORM                         = 1000156001,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM                  = 1000156002,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8R8_2PLANE_420_UNORM                   = 1000156003,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM                  = 1000156004,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8R8_2PLANE_422_UNORM                   = 1000156005,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM                  = 1000156006,  // <-- NOT SUPPORTED
    VK_FORMAT_R10X6_UNORM_PACK16                         = 1000156007,  // <-- NOT SUPPORTED
    VK_FORMAT_R10X6G10X6_UNORM_2PACK16                   = 1000156008,  // <-- NOT SUPPORTED
    VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16         = 1000156009,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16     = 1000156010,  // <-- NOT SUPPORTED
    VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16     = 1000156011,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16 = 1000156012,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16  = 1000156013,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16 = 1000156014,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16  = 1000156015,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16 = 1000156016,  // <-- NOT SUPPORTED
    VK_FORMAT_R12X4_UNORM_PACK16                         = 1000156017,  // <-- NOT SUPPORTED
    VK_FORMAT_R12X4G12X4_UNORM_2PACK16                   = 1000156018,  // <-- NOT SUPPORTED
    VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16         = 1000156019,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16     = 1000156020,  // <-- NOT SUPPORTED
    VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16     = 1000156021,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16 = 1000156022,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16  = 1000156023,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16 = 1000156024,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16  = 1000156025,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16 = 1000156026,  // <-- NOT SUPPORTED
    VK_FORMAT_G16B16G16R16_422_UNORM                     = 1000156027,  // <-- NOT SUPPORTED
    VK_FORMAT_B16G16R16G16_422_UNORM                     = 1000156028,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM               = 1000156029,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16R16_2PLANE_420_UNORM                = 1000156030,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM               = 1000156031,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16R16_2PLANE_422_UNORM                = 1000156032,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM               = 1000156033,  // <-- NOT SUPPORTED
    VK_FORMAT_G8_B8R8_2PLANE_444_UNORM                   = 1000330000,  // <-- NOT SUPPORTED
    VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16  = 1000330001,  // <-- NOT SUPPORTED
    VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16  = 1000330002,  // <-- NOT SUPPORTED
    VK_FORMAT_G16_B16R16_2PLANE_444_UNORM                = 1000330003,  // <-- NOT SUPPORTED
    VK_FORMAT_A4R4G4B4_UNORM_PACK16                      = 1000340000,  // <-- NOT SUPPORTED
    VK_FORMAT_A4B4G4R4_UNORM_PACK16                      = 1000340001,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK                      = 1000066000,
    VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK                      = 1000066001,
    VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK                      = 1000066002,
    VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK                      = 1000066003,
    VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK                      = 1000066004,
    VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK                      = 1000066005,
    VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK                      = 1000066006,
    VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK                      = 1000066007,
    VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK                     = 1000066008,
    VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK                     = 1000066009,
    VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK                     = 1000066010,
    VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK                    = 1000066011,
    VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK                    = 1000066012,
    VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK                    = 1000066013,
    VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG                = 1000054000,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG                = 1000054001,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG                = 1000054002,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG                = 1000054003,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG                 = 1000054004,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG                 = 1000054005,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG                 = 1000054006,  // <-- NOT SUPPORTED
    VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG                 = 1000054007,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_3x3x3_UNORM_BLOCK_EXT                 = 1000288000,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_3x3x3_SRGB_BLOCK_EXT                  = 1000288001,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_3x3x3_SFLOAT_BLOCK_EXT                = 1000288002,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x3x3_UNORM_BLOCK_EXT                 = 1000288003,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x3x3_SRGB_BLOCK_EXT                  = 1000288004,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x3x3_SFLOAT_BLOCK_EXT                = 1000288005,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x3_UNORM_BLOCK_EXT                 = 1000288006,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x3_SRGB_BLOCK_EXT                  = 1000288007,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x3_SFLOAT_BLOCK_EXT                = 1000288008,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x4_UNORM_BLOCK_EXT                 = 1000288009,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x4_SRGB_BLOCK_EXT                  = 1000288010,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_4x4x4_SFLOAT_BLOCK_EXT                = 1000288011,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x4x4_UNORM_BLOCK_EXT                 = 1000288012,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x4x4_SRGB_BLOCK_EXT                  = 1000288013,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x4x4_SFLOAT_BLOCK_EXT                = 1000288014,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x4_UNORM_BLOCK_EXT                 = 1000288015,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x4_SRGB_BLOCK_EXT                  = 1000288016,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x4_SFLOAT_BLOCK_EXT                = 1000288017,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x5_UNORM_BLOCK_EXT                 = 1000288018,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x5_SRGB_BLOCK_EXT                  = 1000288019,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_5x5x5_SFLOAT_BLOCK_EXT                = 1000288020,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x5x5_UNORM_BLOCK_EXT                 = 1000288021,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x5x5_SRGB_BLOCK_EXT                  = 1000288022,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x5x5_SFLOAT_BLOCK_EXT                = 1000288023,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x5_UNORM_BLOCK_EXT                 = 1000288024,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x5_SRGB_BLOCK_EXT                  = 1000288025,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x5_SFLOAT_BLOCK_EXT                = 1000288026,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x6_UNORM_BLOCK_EXT                 = 1000288027,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x6_SRGB_BLOCK_EXT                  = 1000288028,  // <-- NOT SUPPORTED
    VK_FORMAT_ASTC_6x6x6_SFLOAT_BLOCK_EXT                = 1000288029,  // <-- NOT SUPPORTED
    VK_FORMAT_R16G16_SFIXED5_NV                          = 1000464000,  // <-- NOT SUPPORTED
    VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR                  = 1000470000,  // <-- NOT SUPPORTED
    VK_FORMAT_A8_UNORM_KHR                               = 1000470001,  // <-- NOT SUPPORTED
    VK_FORMAT_MAX_ENUM                                   = 0x7FFFFFFF
};
// clang-format on



struct FormatInfo {
    int nbrchannels;
    TypeDesc typedesc;
    BlockCompression compression { BlockCompression::NONE };
    VkFormat uncompressed_format { VK_FORMAT_UNDEFINED };
    /* Does the VkFormat contain a UNORM or SNORM specifier? */
    bool normalized_vkformat { true };
    /* ASTC block dimensions. Only valid if compression is `ASTC` */
    ktx_pack_astc_block_dimension_e astc_block_dims {
        ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x4
    };
};



//
// Get ImageSpec-related information, block compression type (if any), and
// uncompressed VkFormat (for block-compressed formats) for the given VkFormat.
//
// Notes:
// Colorspace detection should be done via the color model and transfer function
// and not via VkFormat. See:
// https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_data_format_descriptor
//
// TODO: there are a lot of formats to test so currently only "widely" used
// formats are supported (every format included here has to have a corresponding
// test file with the same VkFormat).
//
inline bool
get_info_from_vkformat(VkFormat vkformat, FormatInfo& formatinfo)
{
    // clang-format off
    switch (vkformat) {
        /* ------------------------------------------------------------------ */
        /* ------------------ 8-bit uncompressed formats -------------------- */
        /* ------------------------------------------------------------------ */

        // 8-bit R[G[B[A]]] uncompressed UNORM formats
    case       VK_FORMAT_R8_UNORM: formatinfo = { 1, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case     VK_FORMAT_R8G8_UNORM: formatinfo = { 2, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case   VK_FORMAT_R8G8B8_UNORM: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case VK_FORMAT_R8G8B8A8_UNORM: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::NONE }; return true;

        // 8-bit R[G[B[A]]] uncompressed SNORM formats
    case       VK_FORMAT_R8_SNORM: formatinfo = { 1, TypeDesc::INT8, BlockCompression::NONE }; return true;
    case     VK_FORMAT_R8G8_SNORM: formatinfo = { 2, TypeDesc::INT8, BlockCompression::NONE }; return true;
    case   VK_FORMAT_R8G8B8_SNORM: formatinfo = { 3, TypeDesc::INT8, BlockCompression::NONE }; return true;
    case VK_FORMAT_R8G8B8A8_SNORM: formatinfo = { 4, TypeDesc::INT8, BlockCompression::NONE }; return true;

        // 8-bit R[G[B[A]]] uncompressed SRGB formats
    case       VK_FORMAT_R8_SRGB: formatinfo = { 1, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case     VK_FORMAT_R8G8_SRGB: formatinfo = { 2, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case   VK_FORMAT_R8G8B8_SRGB: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::NONE }; return true;
    case VK_FORMAT_R8G8B8A8_SRGB: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::NONE }; return true;

        // 8-bit R[G[B[A]]] uncompressed UINT formats (not normalized)
    case       VK_FORMAT_R8_UINT: formatinfo = { 1, TypeDesc::UINT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case     VK_FORMAT_R8G8_UINT: formatinfo = { 2, TypeDesc::UINT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case   VK_FORMAT_R8G8B8_UINT: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R8G8B8A8_UINT: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 8-bit R[G[B[A]]] uncompressed SINT formats (not normalized)
    case       VK_FORMAT_R8_SINT: formatinfo = { 1, TypeDesc::INT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case     VK_FORMAT_R8G8_SINT: formatinfo = { 2, TypeDesc::INT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case   VK_FORMAT_R8G8B8_SINT: formatinfo = { 3, TypeDesc::INT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R8G8B8A8_SINT: formatinfo = { 4, TypeDesc::INT8, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // TODO: BGRA formats (need to add std::array<char, 4> channelnames to FormatInfo)


        /* ------------------------------------------------------------------ */
        /* ------------------ 16-bit uncompressed formats ------------------- */
        /* ------------------------------------------------------------------ */

        // 16-bit uncompressed UNORM formats
    case          VK_FORMAT_R16_UNORM: formatinfo = { 1, TypeDesc::UINT16, BlockCompression::NONE }; return true;
    case       VK_FORMAT_R16G16_UNORM: formatinfo = { 2, TypeDesc::UINT16, BlockCompression::NONE }; return true;
    case    VK_FORMAT_R16G16B16_UNORM: formatinfo = { 3, TypeDesc::UINT16, BlockCompression::NONE }; return true;
    case VK_FORMAT_R16G16B16A16_UNORM: formatinfo = { 4, TypeDesc::UINT16, BlockCompression::NONE }; return true;

        // 16-bit uncompressed SNORM formats
    case          VK_FORMAT_R16_SNORM: formatinfo = { 1, TypeDesc::INT16, BlockCompression::NONE }; return true;
    case       VK_FORMAT_R16G16_SNORM: formatinfo = { 2, TypeDesc::INT16, BlockCompression::NONE }; return true;
    case    VK_FORMAT_R16G16B16_SNORM: formatinfo = { 3, TypeDesc::INT16, BlockCompression::NONE }; return true;
    case VK_FORMAT_R16G16B16A16_SNORM: formatinfo = { 4, TypeDesc::INT16, BlockCompression::NONE }; return true;

        // 16-bit uncompressed SFLOAT formats
    case          VK_FORMAT_R16_SFLOAT: formatinfo = { 1, TypeDesc::HALF, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R16G16_SFLOAT: formatinfo = { 2, TypeDesc::HALF, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R16G16B16_SFLOAT: formatinfo = { 3, TypeDesc::HALF, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R16G16B16A16_SFLOAT: formatinfo = { 4, TypeDesc::HALF, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 16-bit uncompressed UINT formats
    case          VK_FORMAT_R16_UINT: formatinfo = { 1, TypeDesc::UINT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R16G16_UINT: formatinfo = { 2, TypeDesc::UINT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R16G16B16_UINT: formatinfo = { 3, TypeDesc::UINT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R16G16B16A16_UINT: formatinfo = { 4, TypeDesc::UINT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 16-bit uncompressed SINT formats
    case          VK_FORMAT_R16_SINT: formatinfo = { 1, TypeDesc::INT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R16G16_SINT: formatinfo = { 2, TypeDesc::INT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R16G16B16_SINT: formatinfo = { 3, TypeDesc::INT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R16G16B16A16_SINT: formatinfo = { 4, TypeDesc::INT16, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;


        /* ------------------------------------------------------------------ */
        /* ------------------ 32-bit uncompressed formats ------------------- */
        /* ------------------------------------------------------------------ */

        // 32-bit uncompressed SFLOAT formats
    case          VK_FORMAT_R32_SFLOAT: formatinfo = { 1, TypeDesc::FLOAT, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R32G32_SFLOAT: formatinfo = { 2, TypeDesc::FLOAT, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R32G32B32_SFLOAT: formatinfo = { 3, TypeDesc::FLOAT, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R32G32B32A32_SFLOAT: formatinfo = { 4, TypeDesc::FLOAT, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 32-bit uncompressed UINT formats
    case          VK_FORMAT_R32_UINT: formatinfo = { 1, TypeDesc::UINT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R32G32_UINT: formatinfo = { 2, TypeDesc::UINT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R32G32B32_UINT: formatinfo = { 3, TypeDesc::UINT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R32G32B32A32_UINT: formatinfo = { 4, TypeDesc::UINT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 32-bit uncompressed SINT formats
    case          VK_FORMAT_R32_SINT: formatinfo = { 1, TypeDesc::INT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R32G32_SINT: formatinfo = { 2, TypeDesc::INT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R32G32B32_SINT: formatinfo = { 3, TypeDesc::INT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R32G32B32A32_SINT: formatinfo = { 4, TypeDesc::INT32, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;


        /* ------------------------------------------------------------------ */
        /* ------------------ 64-bit uncompressed formats ------------------- */
        /* ------------------------------------------------------------------ */

        // 64-bit uncompressed SFLOAT formats
    case          VK_FORMAT_R64_SFLOAT: formatinfo = { 1, TypeDesc::DOUBLE, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R64G64_SFLOAT: formatinfo = { 2, TypeDesc::DOUBLE, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R64G64B64_SFLOAT: formatinfo = { 3, TypeDesc::DOUBLE, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R64G64B64A64_SFLOAT: formatinfo = { 4, TypeDesc::DOUBLE, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 64-bit uncompressed UINT formats
    case          VK_FORMAT_R64_UINT: formatinfo = { 1, TypeDesc::UINT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R64G64_UINT: formatinfo = { 2, TypeDesc::UINT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R64G64B64_UINT: formatinfo = { 3, TypeDesc::UINT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R64G64B64A64_UINT: formatinfo = { 4, TypeDesc::UINT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;

        // 64-bit uncompressed SINT formats
    case          VK_FORMAT_R64_SINT: formatinfo = { 1, TypeDesc::INT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case       VK_FORMAT_R64G64_SINT: formatinfo = { 2, TypeDesc::INT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case    VK_FORMAT_R64G64B64_SINT: formatinfo = { 3, TypeDesc::INT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;
    case VK_FORMAT_R64G64B64A64_SINT: formatinfo = { 4, TypeDesc::INT64, BlockCompression::NONE, VK_FORMAT_UNDEFINED, false }; return true;


        /* ------------------------------------------------------------------ */
        /* ---------------------- compressed formats ------------------------ */
        /* ------------------------------------------------------------------ */

        // ETC2 block-compressed formats (not supported)
    case    VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::ETC2_RGB, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case   VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::ETC2_RGB, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case  VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ETC2_RGB_A1, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case  VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ETC2_RGBA, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ETC2_RGB_A1, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ETC2_RGBA, VK_FORMAT_R8G8B8A8_UNORM }; return true;

        // BCn formats
    case   VK_FORMAT_BC1_RGB_SRGB_BLOCK: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::BC1, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case  VK_FORMAT_BC1_RGB_UNORM_BLOCK: formatinfo = { 3, TypeDesc::UINT8, BlockCompression::BC1, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case  VK_FORMAT_BC1_RGBA_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC1A, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC1A, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case       VK_FORMAT_BC2_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC2, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case      VK_FORMAT_BC2_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC2, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case       VK_FORMAT_BC3_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC3, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case      VK_FORMAT_BC3_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC3, VK_FORMAT_R8G8B8A8_UNORM }; return true;
    case      VK_FORMAT_BC4_UNORM_BLOCK: formatinfo = { 1, TypeDesc::UINT8, BlockCompression::BC4, VK_FORMAT_R8_UNORM }; return true;
    case      VK_FORMAT_BC4_SNORM_BLOCK: formatinfo = { 1, TypeDesc::UINT8, BlockCompression::BC4, VK_FORMAT_R8_SNORM }; return true;
    case      VK_FORMAT_BC5_UNORM_BLOCK: formatinfo = { 2, TypeDesc::UINT8, BlockCompression::BC5, VK_FORMAT_R8G8_UNORM }; return true;
    case      VK_FORMAT_BC5_SNORM_BLOCK: formatinfo = { 2, TypeDesc::UINT8, BlockCompression::BC5, VK_FORMAT_R8G8_SNORM }; return true;
    case    VK_FORMAT_BC6H_UFLOAT_BLOCK: formatinfo = { 3, TypeDesc::HALF, BlockCompression::BC6HU, VK_FORMAT_R16G16B16_SFLOAT, false }; return true;
    case    VK_FORMAT_BC6H_SFLOAT_BLOCK: formatinfo = { 3, TypeDesc::HALF, BlockCompression::BC6HS, VK_FORMAT_R16G16B16_SFLOAT, false }; return true;
    case       VK_FORMAT_BC7_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC7, VK_FORMAT_R8G8B8A8_SRGB }; return true;
    case      VK_FORMAT_BC7_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::BC7, VK_FORMAT_R8G8B8A8_UNORM }; return true;

        // LDR ASTC linear formats (2D blocks)
    case   VK_FORMAT_ASTC_4x4_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 }; return true;
    case   VK_FORMAT_ASTC_5x4_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_5x4 }; return true;
    case   VK_FORMAT_ASTC_5x5_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_5x5 }; return true;
    case   VK_FORMAT_ASTC_6x5_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_6x5 }; return true;
    case   VK_FORMAT_ASTC_6x6_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 }; return true;
    case   VK_FORMAT_ASTC_8x5_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x5 }; return true;
    case   VK_FORMAT_ASTC_8x6_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x6 }; return true;
    case   VK_FORMAT_ASTC_8x8_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x8 }; return true;
    case  VK_FORMAT_ASTC_10x5_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x5 }; return true;
    case  VK_FORMAT_ASTC_10x6_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x6 }; return true;
    case  VK_FORMAT_ASTC_10x8_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x8 }; return true;
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x10 }; return true;
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_12x10 }; return true;
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_UNORM, true, KTX_PACK_ASTC_BLOCK_DIMENSION_12x12 }; return true;

        // LDR ASTC sRGB formats (2D blocks)
    case   VK_FORMAT_ASTC_4x4_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 }; return true;
    case   VK_FORMAT_ASTC_5x4_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_5x4 }; return true;
    case   VK_FORMAT_ASTC_5x5_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_5x5 }; return true;
    case   VK_FORMAT_ASTC_6x5_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_6x5 }; return true;
    case   VK_FORMAT_ASTC_6x6_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 }; return true;
    case   VK_FORMAT_ASTC_8x5_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x5 }; return true;
    case   VK_FORMAT_ASTC_8x6_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x6 }; return true;
    case   VK_FORMAT_ASTC_8x8_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_8x8 }; return true;
    case  VK_FORMAT_ASTC_10x5_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x5 }; return true;
    case  VK_FORMAT_ASTC_10x6_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x6 }; return true;
    case  VK_FORMAT_ASTC_10x8_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x8 }; return true;
    case VK_FORMAT_ASTC_10x10_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_10x10 }; return true;
    case VK_FORMAT_ASTC_12x10_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_12x10 }; return true;
    case VK_FORMAT_ASTC_12x12_SRGB_BLOCK: formatinfo = { 4, TypeDesc::UINT8, BlockCompression::ASTC, VK_FORMAT_R8G8B8A8_SRGB, true, KTX_PACK_ASTC_BLOCK_DIMENSION_12x12 }; return true;
        
        // HDR ASTC formats (2D blocks)
    case   VK_FORMAT_ASTC_4x4_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_4x4 }; return true;
    case   VK_FORMAT_ASTC_5x4_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_5x4 }; return true;
    case   VK_FORMAT_ASTC_5x5_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_5x5 }; return true;
    case   VK_FORMAT_ASTC_6x5_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_6x5 }; return true;
    case   VK_FORMAT_ASTC_6x6_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_6x6 }; return true;
    case   VK_FORMAT_ASTC_8x5_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_8x5 }; return true;
    case   VK_FORMAT_ASTC_8x6_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_8x6 }; return true;
    case   VK_FORMAT_ASTC_8x8_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_8x8 }; return true;
    case  VK_FORMAT_ASTC_10x5_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_10x5 }; return true;
    case  VK_FORMAT_ASTC_10x6_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_10x6 }; return true;
    case  VK_FORMAT_ASTC_10x8_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_10x8 }; return true;
    case VK_FORMAT_ASTC_10x10_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_10x10 }; return true;
    case VK_FORMAT_ASTC_12x10_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_12x10 }; return true;
    case VK_FORMAT_ASTC_12x12_SFLOAT_BLOCK: formatinfo = { 4, TypeDesc::HALF, BlockCompression::ASTC, VK_FORMAT_R16G16B16A16_SFLOAT, false, KTX_PACK_ASTC_BLOCK_DIMENSION_12x12 }; return true;
    default: break;
    }
    // clang-format on
    return false;
}



//
// Gets the VkFormat from provided info (supplied from `ktxoutput->spec()`)
//
// Number of channels determines the number of channels in the returned
// VkFormat (e.g., 3 => RGB). This is assuming the default channel names: RGBA.
//
// The type is determined based on the provided format (which usually
// comes from `ktxoutput->spec()`) and also whether normalized is set. Just
// using the type alone is insufficient to determine the VkFormat because we
// can't, for instance, distinguish between VK_FORMAT_R8_UNORM and
// VK_FORMAT_R8_UINT. This table showcases the VkFormat and its upload data
// type:
//
// Vulkan format                        | Upload data type (per channel)
// ------------------------------------ | --------------------------------------
// VK_FORMAT_R8[G8[B8[A8]]]_UNORM       | UINT8
// VK_FORMAT_R8[G8[B8[A8]]]_SNORM       | INT8
// VK_FORMAT_R8[G8[B8[A8]]]_UINT        | UINT8
// VK_FORMAT_R8[G8[B8[A8]]]_SINT        | INT8
// VK_FORMAT_.+_SRGB                    | UINT8
// VK_FORMAT_R16[G16[B16[A16]]]_SFLOAT  | HALF
// VK_FORMAT_R16[G16[B16[A16]]]_UNORM   | UINT16
// VK_FORMAT_R16[G16[B16[A16]]]_SNORM   | INT16
// VK_FORMAT_R16[G16[B16[A16]]]_UINT    | UINT16
// VK_FORMAT_R16[G16[B16[A16]]]_SINT    | INT16
// VK_FORMAT_R32[G32[B32[A32]]]_SFLOAT  | FLOAT
// VK_FORMAT_R32[G32[B32[A32]]]_UINT    | UINT32
// VK_FORMAT_R32[G32[B32[A32]]]_SINT    | INT32
// VK_FORMAT_R64[G64[B64[A64]]]_SFLOAT  | DOUBLE
// VK_FORMAT_R64[G64[B64[A64]]]_UINT    | UINT64
// VK_FORMAT_R64[G64[B64[A64]]]_SINT    | INT64
// VK_FORMAT_B8G8R8[A8]_UNORM           | UINT8
// VK_FORMAT_B8G8R8[A8]_SNORM           | INT8
// VK_FORMAT_B8G8R8[A8]_UINT            | UINT8
// VK_FORMAT_B8G8R8[A8]_SINT            | INT8
// VK_FORMAT_B8G8R8[A8]_SRGB            | UINT8
//
// As per the KTX2 specs, the _SRGB variants of VkFormat are determined based
// on the supplied colorspace (if sRGB => VkFormat MUST be SRGB; unlike DDS).
//
// Swizzling isn't supported yet (e.g., VK_FORMAT_B8G8R8A8_SRGB) but it will be
// soon (after robust testing on these formats is done).
//
// SRGB colorspace is prioritized over normalization (i.e., if `srg_colorspace`
// is `true` and `normalized` is also `true`, `normalized` is ignored if an SRGB
// output VkFormat is possible).
//
inline VkFormat
get_vkformat_from_info(ImageSpec const& spec, bool srgb_colorspace,
                       bool normalized) noexcept
{
    const auto& channelnames = spec.channelnames;
    bool bgr_channels        = false;

    // clang-format off
    // For 1 channel, we support R (or Y which is treated as R) channel names
    // The Y channel name is expected because default_channel_names sets it in case nbrchannels == 1
    if (spec.nchannels == 1 && channelnames[0] != "R" && channelnames[0] != "Y")
        return VK_FORMAT_UNDEFINED;

    // For 2 channels, we only support RG (or YA which is treated as RG) channel names
    if (spec.nchannels == 2 && !(channelnames[0] == "R" && channelnames[1] == "G")
                            && !(channelnames[0] == "Y" && channelnames[1] == "A"))
        return VK_FORMAT_UNDEFINED;

    // For 3 channels, we support either RGB or BGR
    if (spec.nchannels == 3) {
        if ((   channelnames[0] == "B"
             && channelnames[1] == "G"
             && channelnames[2] == "R"))
            bgr_channels = true;
        else if (!(channelnames[0] == "R"
                && channelnames[1] == "G"
                && channelnames[2] == "B"))
            return VK_FORMAT_UNDEFINED;
    }

    // For 4 channels, we support RGBA or BGRA
    if (spec.nchannels == 4) {
        if (   channelnames[0] == "B"
            && channelnames[1] == "G"
            && channelnames[2] == "R"
            && channelnames[3] == "A")
            bgr_channels = true;
        else if (!(channelnames[0] == "R"
                && channelnames[1] == "G"
                && channelnames[2] == "B"
                && channelnames[3] == "A"))
            return VK_FORMAT_UNDEFINED;
    }

    if (bgr_channels) {
      switch(static_cast<TypeDesc::BASETYPE>(spec.format.basetype)) {
      case TypeDesc::UINT8: {
        switch (spec.nchannels) {
          case 3:  return srgb_colorspace ? VK_FORMAT_B8G8R8_SRGB : normalized ? VK_FORMAT_B8G8R8_UNORM : VK_FORMAT_B8G8R8_UINT;
          case 4:  return srgb_colorspace ? VK_FORMAT_B8G8R8A8_SRGB : normalized ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_B8G8R8A8_UINT;
          default: return VK_FORMAT_UNDEFINED;
        }
      }
      case TypeDesc::INT8: {
        switch (spec.nchannels) {
          case 3:  return normalized ? VK_FORMAT_B8G8R8_SNORM : VK_FORMAT_B8G8R8_SINT;
          case 4:  return normalized ? VK_FORMAT_B8G8R8A8_SNORM : VK_FORMAT_B8G8R8A8_SINT;
          default: return VK_FORMAT_UNDEFINED;
        }
      }
      default: return VK_FORMAT_UNDEFINED;
      }
    }

    // else: R[G[B[A]]] channel names

    switch (static_cast<TypeDesc::BASETYPE>(spec.format.basetype)) {
    case TypeDesc::UINT8: {
      switch (spec.nchannels) {
        case 1:  return srgb_colorspace ? VK_FORMAT_R8_SRGB : normalized ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT;
        case 2:  return srgb_colorspace ? VK_FORMAT_R8G8_SRGB : normalized ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT;
        case 3:  return srgb_colorspace ? VK_FORMAT_R8G8B8_SRGB : normalized ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT;
        case 4:  return srgb_colorspace ? VK_FORMAT_R8G8B8A8_SRGB : normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::INT8: {
      switch (spec.nchannels) {
        case 1:  return normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT;
        case 2:  return normalized ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_SINT;
        case 3:  return normalized ? VK_FORMAT_R8G8B8_SNORM : VK_FORMAT_R8G8B8_SINT;
        case 4:  return normalized ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_SINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::HALF: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R16_SFLOAT;
        case 2:  return VK_FORMAT_R16G16_SFLOAT;
        case 3:  return VK_FORMAT_R16G16B16_SFLOAT;
        case 4:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::UINT16: {
      switch (spec.nchannels) {
        case 1:  return normalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT;
        case 2:  return normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT;
        case 3:  return normalized ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R16G16B16_UINT;
        case 4:  return normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::INT16: {
      switch (spec.nchannels) {
        case 1:  return normalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT;
        case 2:  return normalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT;
        case 3:  return normalized ? VK_FORMAT_R16G16B16_SNORM : VK_FORMAT_R16G16B16_SINT;
        case 4:  return normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::FLOAT: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R32_SFLOAT;
        case 2:  return VK_FORMAT_R32G32_SFLOAT;
        case 3:  return VK_FORMAT_R32G32B32_SFLOAT;
        case 4:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::UINT32: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R32_UINT;
        case 2:  return VK_FORMAT_R32G32_UINT;
        case 3:  return VK_FORMAT_R32G32B32_UINT;
        case 4:  return VK_FORMAT_R32G32B32A32_UINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::INT32: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R32_SINT;
        case 2:  return VK_FORMAT_R32G32_SINT;
        case 3:  return VK_FORMAT_R32G32B32_SINT;
        case 4:  return VK_FORMAT_R32G32B32A32_SINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::DOUBLE: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R64_SFLOAT;
        case 2:  return VK_FORMAT_R64G64_SFLOAT;
        case 3:  return VK_FORMAT_R64G64B64_SFLOAT;
        case 4:  return VK_FORMAT_R64G64B64A64_SFLOAT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::UINT64: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R64_UINT;
        case 2:  return VK_FORMAT_R64G64_UINT;
        case 3:  return VK_FORMAT_R64G64B64_UINT;
        case 4:  return VK_FORMAT_R64G64B64A64_UINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }
    case TypeDesc::INT64: {
      switch (spec.nchannels) {
        case 1:  return VK_FORMAT_R64_SINT;
        case 2:  return VK_FORMAT_R64G64_SINT;
        case 3:  return VK_FORMAT_R64G64B64_SINT;
        case 4:  return VK_FORMAT_R64G64B64A64_SINT;
        default: return VK_FORMAT_UNDEFINED;
      }
    }

    default: return VK_FORMAT_UNDEFINED;
    }
    // clang-format on
}



inline uint32_t
calculate_nbr_mip_levels(uint32_t dimension) noexcept
{
    uint32_t nmips = 0;
    while (dimension > 0) {
        dimension >>= 1;
        ++nmips;
    }
    return nmips;
}

OIIO_PLUGIN_NAMESPACE_END
