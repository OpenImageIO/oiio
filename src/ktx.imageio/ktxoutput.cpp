// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include "OpenImageIO/color.h"
#include "ktx_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

// TODO:
//  - Should we add support to 2D texture arrays (i.e., "multiimage")?

class KtxOutput final : public ImageOutput {
public:
    KtxOutput()
    {
#if defined(KHRONOS_STATIC)
        // This has to be set if libktx is statically built
        DBG std::cout << "KHRONOS_STATIC set to 1" << std::endl;
#endif
    }

    ~KtxOutput() override { close(); }

    const char* format_name(void) const override { return "ktx"; }

    int supports(string_view feature) const override
    {
        // Note: make sure "origin" is NOT supported! All write_* methods assume
        // 0s for x,y,z origins!
        return (
            feature == "alpha" || feature == "ioproxy" ||
            // TODO: properly implement arbitrary_metadata
            // as per the KTX2 specs:
            // registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_keyvalue_data
            // feature == "arbitrary_metadata" ||
            // TODO: KTX2 supports 2D texture arrays, but this plugin doesn't
            // implement it yet.
            // feature == "multiimage" ||
            // KTX2 supports mipmaps
            feature == "mipmap" ||
            // KTX2 supports 3D textures
            feature == "volumes" ||
            // Can write in any order whatsoever
            feature == "random_access" ||
            // Can write Cubemaps and volume textures (these are simulated as
            // tiles even though libktx actually treats volumes slices as
            // subimages)
            feature == "tiles");
    }

    bool open(const std::string& name, const ImageSpec& spec,
              OpenMode mode = Create) override;

    // Calls the span-based single-scanline implementation
    bool write_scanline(int y, int z, TypeDesc format, const void* data,
                        stride_t xstride) override;

    // Calls the span-based multi-scanline implementation
    bool write_scanline(int y, TypeDesc format,
                        const image_span<const std::byte>& data) override;

    // Calls the span-based multi-scanline implementation
    bool write_scanlines(int ybegin, int yend, int z, TypeDesc format,
                         const void* data, stride_t xstride = AutoStride,
                         stride_t ystride = AutoStride) override;

    // Actual implementation
    bool write_scanlines(int ybegin, int yend, TypeDesc format,
                         const image_span<const std::byte>& data) override;

    // Calls the span-based single-tile implementation
    bool write_tile(int x, int y, int z, TypeDesc format, const void* data,
                    stride_t xstride = AutoStride,
                    stride_t ystride = AutoStride,
                    stride_t zstride = AutoStride) override;

    // Calls the span-based multi-tile implementation
    bool write_tile(int x, int y, int z, TypeDesc format,
                    const image_span<const std::byte>& data) override;

    // Actual implementation
    bool write_tiles(int xbegin, int xend, int ybegin, int yend, int zbegin,
                     int zend, TypeDesc format,
                     const image_span<const std::byte>& data) override;

    bool close() override;

private:
    std::string m_filename;

    bool m_initialized { false };  // Has open() with mode == Create was

    // Uncompressed Vulkan format to create the initial texture with.
    VkFormat m_vkformat { VK_FORMAT_UNDEFINED };

    int m_miplevel_idx { -1 };  // Current MIP level

    int m_max_nmiplevels { 1 };  // Max number allowable MIP levels

    uint32_t m_basewidth { 0 };  // MIP level 0 width

    uint32_t m_baseheight { 0 };  // MIP level 0 height

    uint32_t m_face_baseheight { 0 };  // MIP level 0 height of a mipmap face

    uint32_t m_basedepth { 0 };  // MIP level 0 depth

    bool m_is1D { false };  // is this a 1D texture?

    bool m_is3D { false };  // is this a 3D texture?

    bool m_is_cubemap { false };  // is this a cubemap texture?

    ktxSupercmpScheme m_superCmp { KTX_SS_NONE };

    // Whether to generate MIP maps when loading texture to graphics API. This
    // will be passed to ktxTextureCreateInfo's generateMipmaps param.
    bool m_generate_mipmaps { false };

    // Should the target VkFormat be UNORM/SNORM (if possible, given other
    // ImageSpec inforamtion and depending on the ImageSpec::format)?
    bool m_normalized { true };

    BlockCompression m_cmp { BlockCompression::NONE };

    bool m_use_basis_universal { false };

    ktxBasisParams m_basis_params { 0 };

    ktxAstcParams m_astc_params { 0 };  // Params for ASTC GPU block compression

    uint32_t m_zlib_level { 9 };  // Only for Zlib supercompression. Defaults to
                                  // highest compression level.

    uint32_t m_zstd_level { 22 };  // Only for ZSTD supercompression. Defaults
                                   // to highest compression level.

    khr_df_transfer_e m_tf { KHR_DF_TRANSFER_UNSPECIFIED };

    khr_df_primaries_e m_primaries { KHR_DF_PRIMARIES_UNSPECIFIED };

    int m_nthreads { 1 };

    //
    // Container for raw (i.e., uncompressed) texture data structured as:
    //   mip level -> pixels (all faces/slices in same contiguous array)
    //
    // To access underlying data: m_imgs[miplevel_idx] + face_or_slice_offset
    //
    // NOTES:
    //  - The number of slices/faces is known before hand (i.e., appending
    //    slices/faces is NOT supported).
    //  - libktx only supports writing whole images (i.e., miplevel + layer +
    //    slice/face hence why we keep a large std::vector at all times. This is
    //    also needed because we apply compression upon file closure and not
    //    each time on write_scanline(s). The latter isn't even supported by
    //    libktx (as far as I know).
    //
    std::vector<std::vector<uint8_t>> m_imgs;

    bool validate_cubemap_userspec(const ImageSpec& userspec) const;

    bool validate_volume_userspec(const ImageSpec& userspec) const;

    bool validate_2d_texture_userspec(const ImageSpec& userspec) const;

    bool append_mipmaps_vector(imagesize_t image_bytes) noexcept;

    void init() noexcept;

    bool write_ktx2();

    bool construct_basis_params(ktxBasisParams& params,
                                std::string_view codec) const;

    bool construct_astc_params(ktxAstcParams& params) const;

    bool try_parse_astc_block_dimensions(
        string_view block_dims_str,
        ktx_pack_astc_block_dimension_e& block_dims) const noexcept;

    bool try_parse_primaries(string_view primaries_str,
                             khr_df_primaries_e& primaries) const noexcept;

    bool try_parse_tf(string_view tf_str, khr_df_transfer_e& tf) const noexcept;

    // Copied as is from imageoutput.cpp because it is currently
    // translation-unit static and not usable from here
    bool check_span_size(TypeDesc datatype, imagesize_t npixels,
                         const image_span<const std::byte>& data) const;

    bool get_tf_and_primaries(string_view oiio_colorspace,
                              khr_df_transfer_e& tf,
                              khr_df_primaries_e& primaries) const;
};



OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageOutput*
ktx_output_imageio_create()
{
    return new KtxOutput;
}
OIIO_EXPORT const char* ktx_output_extensions[] = { "ktx2", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
KtxOutput::open(const std::string& name, const ImageSpec& userspec,
                OpenMode mode)
{
    if (mode == Create) {
        if (userspec.x != 0 || userspec.y != 0 || userspec.z != 0) {
            close();
            errorfmt("Non-zero origins are not supported by KTX2 OIIO plugin");
            return false;
        }

        //
        // "textureformat" attribute is used to unambiguously determine the kind
        // of texture to write. For cubemaps, user MUST set this to
        // "CubeFace Environment" for the output to be a cubmap texture (this is
        // done instead of just checking tile_width to avoid ambiguity).
        // For volumes, user MUST either set depth > 1 or set the attribute to
        // "Volume Texture"
        //
        const auto texformat = userspec.get_string_attribute("textureformat");

        //
        // Before anything, do some basic checks on the given dimensions
        // depending on the given texture kind (2D, 3D, Cubemap, etc.).
        //
        // The check_open() calls below set the m_spec from the given userspec
        //
        // Note:
        //  userspec.tile_width > 1 is NOT a reliable way to check for cubemap
        //  outputs (e.g., it could simply be that provided userspec corresponds
        //  to a tiled image; exr for instance).
        //
        if (userspec.depth > 1
            || Strutil::iequals(texformat, "Volume Texture")) {
            if (!validate_volume_userspec(userspec)
                || !check_open(mode, userspec,
                               { 0, 4096, 0, 4096, 0, 4096, 0, 4 })) {
                close();
                return false;
            }
            m_is3D = true;
        } else if (Strutil::iequals(texformat, "CubeFace Environment")) {
            if (!validate_cubemap_userspec(userspec)
                || !check_open(mode, userspec,
                               { 0, 16384, 0, 16384 * 6, 0, 1, 0, 4 })) {
                close();
                return false;
            }
            m_is_cubemap      = true;
            m_face_baseheight = m_spec.tile_height;
        } else /* Plain Texture (default) */ {
            if (!validate_2d_texture_userspec(userspec)
                || !check_open(mode, userspec,
                               { 0, 32768, 0, 32768, 0, 1, 0, 4 })) {
                close();
                return false;
            }
        }

        // Save base dimensions (useful to compute MIP-level dims)
        m_basewidth  = m_spec.width;
        m_baseheight = m_spec.height;
        m_basedepth  = m_spec.depth;

        // Save name for later use
        m_filename = name;

        m_nthreads = std::max(1, OIIO::get_int_attribute("threads", 0));

        // no need to check on the format (TypeDesc) here; if it is not
        // supported `get_vkformat_from_info` below will fail.

        const auto colorspace = m_spec.get_string_attribute("oiio:ColorSpace",
                                                            "unknown");
        m_spec.set_colorspace(colorspace);

        // Try to get tf + primaries from OIIO colorspace.
        get_tf_and_primaries(colorspace, m_tf, m_primaries);

        // If user wants to override (not convert!) the tf and/or primaries;
        // give them priority over the currently set colorspace (i.e., these
        // are similar to --assign-primaries and --assign-tf in KTX Tools CLI).

        //
        // Since ktxTexture2_Create always sets primaries to BT709 (because it
        // just can't know what primaries the input is using), allow the user to
        // override the primaries using "ktx:primaries". This is especially
        // useful when targeting glTF which requires non-color KTX2's primaries
        // to be set to UNSPECIFIED.
        //
        // You almost always want to set these for any non-sRGB VkFormats (since
        // most of the time these are non-color data such as normal maps, height
        // maps, etc.).
        //
        if (auto Q = m_spec.find_attribute("ktx:primaries", TypeDesc::STRING)) {
            if (!try_parse_primaries(Q->get_string(), m_primaries)) {
                close();
                errorfmt("Provided unsupported primaries: {}", Q->get_string());
                return false;
            }
        }

        if (auto Q = m_spec.find_attribute("ktx:tf", TypeDesc::STRING)) {
            if (!try_parse_tf(Q->get_string(), m_tf)) {
                close();
                errorfmt("Provided unsupported transfer function: {}",
                         Q->get_string());
                return false;
            }
        }

        // TODO: erroring on (m_tf == KHR_DF_TRANSFER_UNSPECIFIED) causes
        //       sourceprovenance.nanobind and other tests to fail. Just keep
        //       colorspace as "unknown"?

        bool is_srgb = m_tf == KHR_DF_TRANSFER_SRGB;

        if (auto Q = m_spec.find_attribute("ktx:1d", TypeDesc::INT)) {
            auto ndims = Q->get_int();
            m_is1D     = ndims == 1;
        }

        // For 1D textures, we expect the height to either be 0 or 1. Code
        // handles "0" heights fine (because of std::max(height, 1))
        if (m_is1D && m_baseheight > 1) {
            close();
            errorfmt(
                "1D textures expect a height of 0 or 1 but supplied height is {}",
                m_baseheight);
            return false;
        }

        m_use_basis_universal = false;
        if (auto Q = m_spec.find_attribute("ktx:codec", TypeDesc::STRING)) {
            const auto& codec = Q->get_string();
            // Check if this is a GPU block-compressed format (e.g., ASTC, BCn)
            // otherwise check if it is a Basis Universal codec.
            if (Strutil::iequals(codec, "astc")) {
                m_cmp = BlockCompression::ASTC;
#if 0 /* for when BCn gets added */
            } else if (Strutil::iequals(compression, "bc1")) {
                m_cmp = BlockCompression::BC1;
            } else if (Strutil::iequals(compression, "bc1a")) {
                m_cmp = BlockCompression::BC1A;
            } else if (Strutil::iequals(compression, "bc2")) {
                m_cmp = BlockCompression::BC2;
            } else if (Strutil::iequals(compression, "bc3")) {
                m_cmp = BlockCompression::BC3;
            } else if (Strutil::iequals(compression, "bc4")) {
                m_cmp = BlockCompression::BC4;
            } else if (Strutil::iequals(compression, "bc5")) {
                m_cmp = BlockCompression::BC5;
            } else if (Strutil::iequals(compression, "bc6hu")) {
                m_cmp = BlockCompression::BC6HU;
            } else if (Strutil::iequals(compression, "bc6hs")) {
                m_cmp = BlockCompression::BC6HS;
            } else if (Strutil::iequals(compression, "bc7")) {
                m_cmp = BlockCompression::BC7;
#endif
            } else if (!codec.empty()
                       && !construct_basis_params(m_basis_params, codec)) {
                close();
                // construct_basis_params calls errorfmt
                return false;
            }

            m_use_basis_universal = m_basis_params.codec
                                    != KTX_BASIS_CODEC_NONE;
            DBG std::cout
                << "[ktxoutput] GPU block compression or basis universal codec"
                   " (from \"ktx:codec\"): "
                << codec << std::endl;
        }

        //
        // Use sensible defaults in case the input data did not originate from a
        // KTX2 file and the user did not provide a supercompression scheme.
        // KTX2 usually uses BasisLZ supercompression scheme to benefit from
        // both: smaller disk filesizes and on-the-fly transcoding to a
        // supported native GPU format.
        //
        const auto cmp_lvl = m_spec.decode_compression_metadata();
        // BasisLZ is automatically applied when input is ETC1S. It makes no
        // sense to apply it manually.
        if (Strutil::iequals(cmp_lvl.first, "zstd")) {
            m_superCmp = ktxSupercmpScheme::KTX_SS_ZSTD;
            if (cmp_lvl.second != -1)
                m_zstd_level = std::clamp<uint32_t>(cmp_lvl.second, 1u, 22u);
        } else if (Strutil::iequals(cmp_lvl.first, "zip")
                   || Strutil::iequals(cmp_lvl.first, "deflate")
                   || Strutil::iequals(cmp_lvl.first, "zlib")) {
            m_superCmp = ktxSupercmpScheme::KTX_SS_ZLIB;
            if (cmp_lvl.second != -1)
                m_zlib_level = std::clamp<uint32_t>(cmp_lvl.second, 1u, 9u);
        } else if (cmp_lvl.first.empty()) {
            m_superCmp = ktxSupercmpScheme::KTX_SS_NONE;
        } else {
            // If anything else is specified just default to ZSTD because it is
            // lossless and usually produces the highest compression.
            m_superCmp = ktxSupercmpScheme::KTX_SS_ZSTD;
        }

        if (auto Q = m_spec.find_attribute("ktx:generate_mipmaps",
                                           TypeDesc::INT)) {
            m_generate_mipmaps = static_cast<bool>(Q->get_int());
            DBG std::cout << "[ktxoutput] generate mipmaps: " << std::boolalpha
                          << m_generate_mipmaps << std::endl;
        }

        if (auto Q = m_spec.find_attribute("ktx:normalized", TypeDesc::INT)) {
            m_normalized = static_cast<bool>(Q->get_int());
            DBG std::cout << "[ktxoutput] normalized: " << std::boolalpha
                          << m_normalized << std::endl;
        }

        //
        // If we intend to compress to BasisLZ/ETC1S or UASTC then we need to
        // figure the VkFormat so that ktxTexture_SetImageFromMemory does not
        // segfault. (makes sense, since we are creating a KTX texture and
        // telling it to allocate storage, how would it know the size of a given
        // subimage if we provide it with VK_FORMAT_UNDEFINED?)
        //
        // The approach is to simply determine the VkFormat from the ImageSpec.
        //
        m_vkformat = get_vkformat_from_info(m_spec, is_srgb, m_normalized);
        if (m_vkformat == VK_FORMAT_UNDEFINED) {
            close();
            errorfmt("Failed to determine VkFormat from nchannels={} format={} "
                     "colorspace={} normalize={}",
                     m_spec.nchannels, m_spec.format, colorspace, m_normalized);
            return false;
        }

        // max(dims) and not min because mip levels don't just stop at first
        // dimension that reaches 1 (all dimensions must reach 1).
        // Max nbr miplevels for cubemaps MUST be computed from dimensions of
        // individual faces not combined faces (i.e., 1x6)!
        m_max_nmiplevels = calculate_nbr_mip_levels(
            m_is3D ? std::max({ m_spec.width, m_spec.height, m_spec.depth })
            : m_is_cubemap ? std::max(m_spec.tile_width, m_spec.tile_height)
                           : std::max(m_spec.width, m_spec.height));

        DBG std::cout << "[ktxoutput] max number mip levels allowed: "
                      << m_max_nmiplevels << std::endl;

        // Initialize slices/faces container
        OIIO_ASSERT(m_imgs.empty()
                    && "Expected mip levels container to be empty");
        OIIO_ASSERT(m_miplevel_idx == -1);

        // Reserve space for base-level mipmap (level 0)
        if (!append_mipmaps_vector(m_spec.image_bytes())) {
            // if we can't even allocate base-level mipmap => close()
            close();
            errorfmt(
                "Failed to create internal buffer to hold the base mip level "
                "image. This is probably due to insufficient memory and/or "
                "large image dimensions.");
            return false;
        }

        ioproxy_retrieve_from_config(m_spec);
        if (!ioproxy_use_or_open(m_filename)) {
            close();
            return false;
        }

        m_initialized = true;
        return true;
    }  // mode == Create

    if (mode == AppendMIPLevel) {
        // Don't call close() here (use may want to access previously appended
        // data in case of error)
        if (!m_initialized) {
            errorfmt("Cannot append a MIP level if no file has been opened. "
                     "Make sure to call open() with mode=\"Create\" first.");
            return false;
        }

        if (m_generate_mipmaps) {
            errorfmt(
                "Cannot append MIP levels if Graphics API mipmap generation is "
                "requested (i.e., when \"ktx:generate_mipmaps\" is true).");
            return false;
        }

        if ((m_miplevel_idx + 1) >= m_max_nmiplevels) {
            errorfmt("Maximum number of mip levels {} is reached",
                     m_max_nmiplevels);
            return false;
        }

        const uint32_t miplevel_width
            = std::max(1u, m_basewidth >> (m_miplevel_idx + 1));
        const uint32_t miplevel_height
            = m_is_cubemap
                  ? std::max(1u, m_face_baseheight >> (m_miplevel_idx + 1)) * 6
                  : std::max(1u, m_baseheight >> (m_miplevel_idx + 1));
        const uint32_t miplevel_depth
            = std::max(1u, m_basedepth >> (m_miplevel_idx + 1));

        if (userspec.width < 1 || userspec.height < 1 || userspec.depth < 1) {
            errorfmt("MIP level dimensions must be positive");
            return false;
        }

        // Check if provided userspec has same nbrchannels, channelnames, etc.
        // as spec used in Open mode.
        if (userspec.x != m_spec.x || userspec.y != m_spec.y
            || userspec.z != m_spec.z || userspec.nchannels != m_spec.nchannels
            || userspec.channelformats != m_spec.channelformats
            || userspec.format != m_spec.format
            || userspec.alpha_channel != m_spec.alpha_channel
            || userspec.z_channel != m_spec.z_channel
            || userspec.deep != m_spec.deep) {
            errorfmt(
                "Provided userspec for MIP level {} does not match the original"
                " spec used in Open mode",
                m_miplevel_idx + 1);
            return false;
        }

        if ((uint32_t)userspec.width != miplevel_width
            || (uint32_t)userspec.height != miplevel_height
            || (uint32_t)userspec.depth != miplevel_depth) {
            errorfmt(
                "Expected (width,height,depth): ({},{},{}) but got: ({},{},{})",
                miplevel_width, miplevel_height, miplevel_depth, userspec.width,
                userspec.height, userspec.depth);
            return false;
        }

        // Using validate_cubemap_userspec/validate_volume_userspec checks here
        // on the userspec fail because oiiotool doesn't update the
        // tile_width/tile_height for mip levels > 0
        if (!m_is_cubemap && !m_is3D) {
            if (!validate_2d_texture_userspec(userspec))
                return false;
        }

        // Reserve memory for this mip level and increment MIP-level index
        if (!append_mipmaps_vector(userspec.image_bytes())) {
            // Don't close as the caller may need to access the previously
            // written data!
            // close();
            errorfmt(
                "Failed to create internal buffer to hold data for the mip "
                "level {}. This is probably due to insufficient memory and/or "
                "large image dimensions.",
                m_miplevel_idx + 1);
            return false;
        }

        // Only update current spec after passing all checks
        m_spec.width = m_spec.full_width = userspec.width;
        m_spec.height = m_spec.full_height = userspec.height;
        m_spec.depth = m_spec.full_depth = userspec.depth;

        if (m_is3D) {
            // If this is a volume texture, also update tile_width
            m_spec.tile_width  = userspec.width;
            m_spec.tile_height = 1;
            m_spec.tile_depth  = 1;
        } else if (m_is_cubemap) {
            // If this is a cubemap texture, update tile dims
            m_spec.tile_width  = userspec.width;
            m_spec.tile_height = userspec.height / 6;
            m_spec.tile_depth  = 1;
        }

        return true;
    }  // mode == AppendMIPLevel

    // (mode == AppendSubimage) is NOT supported. Pre-declaring the number of
    // subimages is easier to implement (random access to array entries is
    // already supported).

    return false;
}



bool
KtxOutput::write_scanline(int y, int z, TypeDesc format, const void* data,
                          stride_t xstride)
{
    return write_scanlines(y, y + 1, z, format, data, xstride);
}



bool
KtxOutput::write_scanline(int y, TypeDesc format,
                          const image_span<const std::byte>& data)
{
    return write_scanlines(y, y + 1, format, data);
}



bool
KtxOutput::write_scanlines(int ybegin, int yend, int /* z */, TypeDesc format,
                           const void* data, stride_t xstride, stride_t ystride)
{
    const image_span data_img_span(static_cast<const std::byte*>(data),
                                   m_spec.nchannels, m_spec.width,
                                   yend - ybegin, 1, AutoStride, xstride,
                                   ystride, AutoStride,
                                   format.is_unknown() ? m_spec.format.size()
                                                       : format.size());
    return write_scanlines(ybegin, yend, format, data_img_span);
}



bool
KtxOutput::write_scanlines(int ybegin, int yend, TypeDesc format,
                           const image_span<const std::byte>& data)
{
    // For cubemaps and volumes use write_tiles()
    if (m_is_cubemap || m_is3D)
        return false;

    // No need to care about x,y,z origins; they are not supported and always 0
    if (ybegin < 0 || yend > m_spec.height || ybegin >= yend) {
        errorfmt("write_scanlines: Invalid scanline range: [{},{})", ybegin,
                 yend);
        return false;
    }

    if (!check_span_size(format, m_spec.width * size_t(yend - ybegin), data))
        return false;

    if (m_miplevel_idx < 0 || size_t(m_miplevel_idx) >= m_imgs.size()) {
        errorfmt("write_tiles: no valid MIP level is open");
        return false;
    }

    return copy_to_image_buffer(0, m_spec.width, ybegin, yend, 0, 1, format,
                                data.data(), data.xstride(), data.ystride(),
                                data.zstride(), m_imgs[m_miplevel_idx].data());
}



bool
KtxOutput::write_tile(int x, int y, int z, TypeDesc format, const void* data,
                      stride_t xstride, stride_t ystride, stride_t zstride)
{
    const image_span data_img_span(static_cast<const std::byte*>(data),
                                   m_spec.nchannels, m_spec.tile_width,
                                   m_spec.tile_height, m_spec.tile_depth,
                                   AutoStride, xstride, ystride, zstride,
                                   format.is_unknown() ? m_spec.format.size()
                                                       : format.size());
    return write_tile(x, y, z, format, data_img_span);
}



bool
KtxOutput::write_tile(int x, int y, int z, TypeDesc format,
                      const image_span<const std::byte>& data)
{
    return write_tiles(x, x + m_spec.tile_width, y, y + m_spec.tile_height, z,
                       z + m_spec.tile_depth, format, data);
}



bool
KtxOutput::write_tiles(int xbegin, int xend, int ybegin, int yend, int zbegin,
                       int zend, TypeDesc format,
                       const image_span<const std::byte>& data)
{
    // Basis checks on provided ranges
    if (!m_spec.valid_tile_range(xbegin, xend, ybegin, yend, zbegin, zend)) {
        errorfmt("write_tiles: invalid tile range [{},{})x[{},{})x[{},{})",
                 xbegin, xend, ybegin, yend, zbegin, zend);
        return false;
    }

    if (!check_span_size(format,
                         size_t(int64_t(xend) - xbegin)
                             * size_t(int64_t(yend) - ybegin)
                             * size_t(int64_t(zend) - zbegin),
                         data))
        return false;  // check_span_size sets errorfmt

    if (m_miplevel_idx < 0 || size_t(m_miplevel_idx) >= m_imgs.size()) {
        errorfmt("write_tiles: no valid MIP level is open");
        return false;
    }

    // copy_tile_to_image_buffer is a wrapper around this and does checks that
    // we already have done + it copies the whole ImageSpec so just use this
    // directly.
    return copy_to_image_buffer(xbegin, xend, ybegin, yend, zbegin, zend,
                                format, data.data(), data.xstride(),
                                data.ystride(), data.zstride(),
                                m_imgs[m_miplevel_idx].data());
}



bool
KtxOutput::close()
{
    // closing an un-opened ImageOutput instance is fine
    if (!m_initialized) {
        // we could error before setting `m_initialized = true` and we could
        // call open() again. Make sure to re-initialize.
        init();
        return true;
    }
    // Check if already closed => if so, then the KTX2 file is already saved
    if (!ioproxy_opened()) {
        init();
        return true;
    }
    if (!write_ktx2()) {
        // Do not init()
        return false;
    }
    init();
    return true;
}



bool
KtxOutput::validate_cubemap_userspec(const ImageSpec& userspec) const
{
    // In KTX2, cubemaps are 'simulated' as 1x6 layout (tiles along the
    // height). KTX2 requires that cubemap faces are squared.
    if (userspec.depth > 1 || userspec.width != userspec.tile_width
        || userspec.height != userspec.tile_height * 6
        || userspec.tile_depth != 1
        || userspec.tile_width != userspec.tile_height) {
        errorfmt("For cubemap textures: depth must be <= 1 (provided {});"
                 " tile width must be: {} (provided {});"
                 " tile height must be 1/6th the height: {} (provided {});"
                 " tile depth must be: 1 (provided {}) ",
                 userspec.depth, userspec.width, userspec.tile_width,
                 userspec.height, userspec.tile_height, userspec.tile_depth);
        return false;
    }
    return true;
}



bool
KtxOutput::validate_volume_userspec(const ImageSpec& userspec) const
{
    if (userspec.depth < 1 || userspec.tile_width != userspec.width
        || userspec.tile_height != 1 || userspec.tile_depth != 1) {
        errorfmt("For volume textures: depth must be >= 1 (provided {});"
                 " tile width must be: {} (provided {});"
                 " tile height must be: 1 (provided {});"
                 " tile depth must be: 1 (provided {})",
                 userspec.depth, userspec.width, userspec.tile_width,
                 userspec.tile_height, userspec.tile_depth);
        return false;
    }
    return true;
}



bool
KtxOutput::validate_2d_texture_userspec(const ImageSpec& userspec) const
{
    if (userspec.depth > 1) {
        errorfmt("Provided depth is {} > 1 but \"textureformat\" attribute "
                 "is not set to \"Volume Texture\"",
                 userspec.depth);
        return false;
    }
    if (userspec.tile_width != 0 || userspec.tile_height != 0
        || userspec.tile_depth > 1 /* tile_depth is by default 1 */) {
        errorfmt("Provided a tiled texture but \"textureformat\" attribute "
                 "is not set to \"CubeFace Environment\"");
        return false;
    }
    return true;
}



bool
KtxOutput::append_mipmaps_vector(imagesize_t image_bytes) noexcept
{
    try {
        m_imgs.emplace_back(image_bytes, uint8_t(0));
        ++m_miplevel_idx;
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
}



void
KtxOutput::init() noexcept
{
    m_filename            = std::string();
    m_initialized         = false;
    m_vkformat            = VK_FORMAT_UNDEFINED;
    m_miplevel_idx        = -1;
    m_max_nmiplevels      = 1;
    m_basewidth           = 0;
    m_baseheight          = 0;
    m_face_baseheight     = 0;
    m_basedepth           = 0;
    m_is1D                = false;
    m_is3D                = false;
    m_is_cubemap          = false;
    m_superCmp            = KTX_SS_NONE;
    m_generate_mipmaps    = false;
    m_normalized          = true;
    m_cmp                 = BlockCompression::NONE;
    m_use_basis_universal = false;
    m_basis_params        = { 0 };
    m_astc_params         = { 0 };
    m_zlib_level          = 9;
    m_zstd_level          = 22;
    m_tf                  = KHR_DF_TRANSFER_UNSPECIFIED;
    m_primaries           = KHR_DF_PRIMARIES_UNSPECIFIED;
    // clear & release the capacity even though m_imgs' capacity itself is
    // negligeable (it just holds nbr miplevels elements)
    // swap is noexcept and constructor an empty vector doesn't allocate
    // anything => no worries about exceptions
    std::vector<std::vector<uint8_t>>().swap(m_imgs);
    ioproxy_clear();
}



//
// Construct ktxBasisParams struct from the Basis Universal attributes in
// current ImageSpec.
//
// Returns false if provided `codec` does not correspond to a valid Basis
// Universal codec.
//
bool
KtxOutput::construct_basis_params(ktxBasisParams& params,
                                  std::string_view codec) const
{
    // Set defaults
    params             = { 0 };
    params.structSize  = sizeof(ktxBasisParams);
    params.threadCount = m_nthreads;

    params.etc1sCompressionLevel = KTX_ETC1S_DEFAULT_COMPRESSION_LEVEL;

    params.uastcFlags = KTX_PACK_UASTC_LEVEL_DEFAULT;

    if (Strutil::iequals(codec, "uastc") || Strutil::iequals(codec, "uastc-ldr")
        || Strutil::iequals(codec, "uastc-ldr-4x4")) {
        params.codec = KTX_BASIS_CODEC_UASTC_LDR_4x4;
    } else if (Strutil::iequals(codec, "uastc-hdr")
               || Strutil::iequals(codec, "uastc-hdr-4x4")) {
        params.codec = KTX_BASIS_CODEC_UASTC_HDR_4x4;
    } else if (Strutil::iequals(codec, "etc1s")) {
        params.codec = KTX_BASIS_CODEC_ETC1S;
    } else if (Strutil::iequals(codec, "uastc-hdr-6x6")) {
        params.codec = KTX_BASIS_CODEC_UASTC_HDR_6x6_INTERMEDIATE;
    } else {
        errorfmt(
            "Provided Basis Universal codec \"{}\" is invalid. Supported "
            "values: \"uastc\" \"etc1s\" \"uastc-hdr-4x4\" \"uastc-hdr-6x6\"",
            codec);
        return false;
    }

    // Formatting is turned off because having single-statement ifs on multiple
    // lines is really hard to follow...
    // clang-format off
    if (params.codec == KTX_BASIS_CODEC_ETC1S) {
        // Params that only apply to ETC1S
        if (auto Q = m_spec.find_attribute("ktx:etc1s_compression_level", TypeDesc::UINT32)) params.etc1sCompressionLevel = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:etc1s_quality_level", TypeDesc::UINT32)) params.qualityLevel = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:etc1s_max_endpoints", TypeDesc::UINT32)) params.maxEndpoints = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:etc1s_endpoint_rdo_threshold", TypeDesc::FLOAT)) params.endpointRDOThreshold = Q->get_float();
        if (auto Q = m_spec.find_attribute("ktx:etc1s_max_selectors", TypeDesc::UINT32)) params.maxSelectors = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:etc1s_selector_rdo_threshold", TypeDesc::FLOAT)) params.selectorRDOThreshold = Q->get_float();
        if (auto Q = m_spec.find_attribute("ktx:etc1s_no_endpoint_rdo")) params.noEndpointRDO = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:etc1s_no_selector_rdo", TypeDesc::INT)) params.noSelectorRDO = static_cast<bool>(Q->get_int());
    } else if (params.codec == KTX_BASIS_CODEC_UASTC_LDR_4x4) {
        // Params that only apply to UASTC LDR + RDO
        if (auto Q = m_spec.find_attribute("ktx:uastc_flags", TypeDesc::UINT32)) params.uastcFlags = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo", TypeDesc::INT)) params.uastcRDO = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_quality_scalar", TypeDesc::FLOAT)) params.uastcRDOQualityScalar = Q->get_float();
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_dict_size", TypeDesc::UINT32)) params.uastcRDODictSize = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_max_smooth_block_error_scale", TypeDesc::FLOAT)) params.uastcRDOMaxSmoothBlockErrorScale = Q->get_float();
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_max_smooth_block_std_dev", TypeDesc::FLOAT)) params.uastcRDOMaxSmoothBlockStdDev = Q->get_float();
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_dont_favor_simpler_modes", TypeDesc::INT)) params.uastcRDODontFavorSimplerModes = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:uastc_rdo_no_multithreading", TypeDesc::INT)) params.uastcRDONoMultithreading = static_cast<bool>(Q->get_int());
    } else if (params.codec == KTX_BASIS_CODEC_UASTC_HDR_4x4 || params.codec == KTX_BASIS_CODEC_UASTC_HDR_6x6_INTERMEDIATE) {
        // Params that only apply to UASTC HDR
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_level", TypeDesc::UINT32)) params.uastcHDRLevel = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_quality", TypeDesc::UINT32)) params.uastcHDRQuality = *static_cast<const uint32_t*>(Q->data());
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_uber_mode", TypeDesc::INT)) params.uastcHDRUberMode = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_ultra_quant", TypeDesc::INT)) params.uastcHDRUltraQuant = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_favor_astc", TypeDesc::INT)) params.uastcHDRFavorAstc = static_cast<bool>(Q->get_int());
        if (auto Q = m_spec.find_attribute("ktx:uastc_hdr_lambda", TypeDesc::FLOAT)) params.uastcHDRLambda = Q->get_float();
    }

    // Params that apply to both ETC1S and UASTC
    if (auto Q = m_spec.find_attribute("ktx:no_sse", TypeDesc::INT)) params.noSSE = static_cast<bool>(Q->get_int());
    if (auto Q = m_spec.find_attribute("ktx:normalmap", TypeDesc::INT)) params.normalMap = static_cast<bool>(Q->get_int());
    if (auto Q = m_spec.find_attribute("ktx:input_swizzle", TypeDesc(TypeDesc::CHAR, 4))) memcpy(params.inputSwizzle, Q->data(), 4);
    if (auto Q = m_spec.find_attribute("ktx:pre_swizzle", TypeDesc::INT)) params.preSwizzle = static_cast<bool>(Q->get_int());
    // clang-format on

    return true;
}



//
// Construct ktxAstcParams struct from the ASTC GPU block compression attributes
// in current ImageSpec.
//
// Unlike `construct_basis_params`, this always succeeds.
//
bool
KtxOutput::construct_astc_params(ktxAstcParams& params) const
{
    // set sensible defaults
    params             = { 0 };
    params.structSize  = sizeof(ktxAstcParams);
    params.verbose     = false;
    params.threadCount = m_nthreads;
    params.blockDimension
        = ktx_pack_astc_block_dimension_e::KTX_PACK_ASTC_BLOCK_DIMENSION_4x4;
    // KTX Tools does not expose this parameter so leave it as default
    params.mode         = KTX_PACK_ASTC_ENCODER_MODE_DEFAULT;
    params.qualityLevel = KTX_PACK_ASTC_QUALITY_LEVEL_THOROUGH;
    params.normalMap    = false;
    params.perceptual   = false;
    // assigning '\0' to first element means no swizzle
    params.inputSwizzle[0] = '\0';
    // clang-format off
    if (auto Q = m_spec.find_attribute("ktx:astc_block_dimensions", TypeDesc::STRING)) {
        ktx_pack_astc_block_dimension_e block_dims;
        // we don't care if this fails; just default to 4x4
        if (try_parse_astc_block_dimensions(Q->get_string(), block_dims)) {
            params.blockDimension = block_dims;
        } else {
            errorfmt("Failed to parse ASTC block dimensions from "
                "\"ktx:astc_block_dimensions\": {}", Q->get_string());
            return false;
        }
    }
    if (auto Q = m_spec.find_attribute("ktx:astc_quality_level", TypeDesc::UINT32)) params.qualityLevel = *static_cast<const uint32_t*>(Q->data());
    if (auto Q = m_spec.find_attribute("ktx:normalmap", TypeDesc::INT)) params.normalMap = static_cast<bool>(Q->get_int());
    if (auto Q = m_spec.find_attribute("ktx:perceptual", TypeDesc::INT)) params.perceptual = static_cast<bool>(Q->get_int());
    if (auto Q = m_spec.find_attribute("ktx:input_swizzle", TypeDesc(TypeDesc::CHAR, 4))) memcpy(params.inputSwizzle, Q->data(), 4);
    // clang-format on
    return true;
}



bool
KtxOutput::try_parse_astc_block_dimensions(
    string_view block_dims_str,
    ktx_pack_astc_block_dimension_e& block_dims) const noexcept
{
    // Small lookup tables; just loop through and avoid string allocations
    for (const auto& [k, v] : astc_block_dims_str_to_enum) {
        if (Strutil::iequals(k, block_dims_str)) {
            block_dims = v;
            return true;
        }
    }
    return false;
}



bool
KtxOutput::try_parse_primaries(string_view primaries_str,
                               khr_df_primaries_e& primaries) const noexcept
{
    // Small lookup tables; just loop through and avoid string allocations
    for (const auto& [k, v] : primaries_str_to_enum) {
        if (Strutil::iequals(k, primaries_str)) {
            primaries = v;
            return true;
        }
    }
    return false;
}



bool
KtxOutput::try_parse_tf(string_view tf_str,
                        khr_df_transfer_e& tf) const noexcept
{
    // Small lookup tables; just loop through and avoid string allocations
    for (const auto& [k, v] : tf_str_to_enum) {
        if (Strutil::iequals(k, tf_str)) {
            tf = v;
            return true;
        }
    }
    return false;
}



// Copied as is from imageoutput.cpp because it is translation-unit static
// and not accessible from ktxoutput.cpp
bool
KtxOutput::check_span_size(TypeDesc datatype, imagesize_t npixels,
                           const image_span<const std::byte>& data) const
{
    if (datatype.is_unknown()) {
        size_t sz = npixels * m_spec.pixel_bytes(true);
        if (sz != data.size_bytes()) {
            errorfmt("image_span size is incorrect ({} bytes vs {} needed)",
                     data.size_bytes(), sz);
            return false;
        }
    } else {
        size_t nvals = npixels * size_t(m_spec.nchannels);
        if (nvals != data.nvalues()) {
            errorfmt("image_span size is incorrect ({} values vs {} needed)",
                     data.nvalues(), nvals);
            return false;
        }
    }
    return true;
}



bool
KtxOutput::get_tf_and_primaries(string_view oiio_colorspace,
                                khr_df_transfer_e& tf,
                                khr_df_primaries_e& primaries) const
{
    const auto& colorconfig = ColorConfig::default_colorconfig();
    for (const auto& [k, v] : color_interop_id_map) {
        if (colorconfig.equivalent(oiio_colorspace, k)) {
            tf        = v.tf;
            primaries = v.primaries;
            return true;
        }
    }
    return false;
}



bool
KtxOutput::write_ktx2()
{
    //
    // KTX2 texture RAII'fied via a unique_ptr.
    // Q. Why not create this in open()?
    // A. open() can be called with AppendMIPLevel mode which means we don't
    //    actually know the number of miplevels to create this texture with
    //    until a call to close(). libktx doesn't support appending mip levels.
    //    It would be really nice for libktx to support appending mip levels
    //    (at least to uncompressed textures) so that we can save significant
    //    memory allocations and memcpy's.
    //
    std::unique_ptr<ktxTexture2, decltype(ktxTexture2_Destroy)*> tex {
        nullptr, ktxTexture2_Destroy
    };

    OIIO_ASSERT(
        m_vkformat != VK_FORMAT_UNDEFINED
        && "VkFormat should never be VK_FORMAT_UNDEFINED when creating a KTX2 texture");
    // TODO: arrays are currently not supported
    ktxTextureCreateInfo create_info;
    create_info.glInternalformat = 0;  // Ignored as this is not a KTX1 texture
    create_info.vkFormat         = m_vkformat;
    create_info.pDfd             = nullptr;
    create_info.baseWidth        = m_basewidth;
    create_info.baseHeight    = m_is_cubemap ? m_face_baseheight : m_baseheight;
    create_info.baseDepth     = m_basedepth;
    create_info.numDimensions = m_is1D ? 1u : m_is3D ? 3u : 2u;
    create_info.numLevels     = m_miplevel_idx + 1;
    create_info.numLayers     = 1;
    create_info.numFaces      = m_is_cubemap ? 6 : 1;
    create_info.isArray       = KTX_FALSE;
    create_info.generateMipmaps = m_generate_mipmaps;

    DBG std::cout << "[ktxoutput] calling ktxTexture2_Create with: "
                  << "vkFormat=" << create_info.vkFormat << "; "
                  << "baseWidth=" << create_info.baseWidth << "; "
                  << "baseHeight=" << create_info.baseHeight << "; "
                  << "baseDepth=" << create_info.baseDepth << "; "
                  << "numDimensions=" << create_info.numDimensions << "; "
                  << "numLevels=" << create_info.numLevels << "; "
                  << "numLayers=" << create_info.numLayers << "; "
                  << "numFaces=" << create_info.numFaces << "; "
                  << "isArray=" << create_info.isArray << "; "
                  << "generateMipmaps=" << create_info.generateMipmaps << "; "
                  << std::endl;

    ktxTexture2* p_tex = nullptr;
    auto result = ktxTexture2_Create(&create_info,
                                     KTX_TEXTURE_CREATE_ALLOC_STORAGE, &p_tex);
    tex.reset(p_tex);

    if (result != KTX_SUCCESS) {
        errorfmt("ktxTexture2_Create failed. Reason: {}",
                 ktxErrorString(result));
        return false;
    }

    if (m_is_cubemap)
        OIIO_ASSERT(p_tex->numFaces == 6);

    // Set tf and primaries
    result = ktxTexture2_SetPrimaries(tex.get(), m_primaries);
    if (result != KTX_SUCCESS) {
        errorfmt("ktxTexture2_SetPrimaries failed. Reason: {}",
                 ktxErrorString(result));
        return false;
    }

    result = ktxTexture2_SetTransferFunction(tex.get(), m_tf);
    if (result != KTX_SUCCESS) {
        errorfmt("ktxTexture2_SetTransferFunction failed. Reason: {}",
                 ktxErrorString(result));
        return false;
    }

    //
    // At first, set uncompressed data for all miplevels, layers, slices, etc.
    // The loop over slices and the loop over faces are mutually exclusive
    // (i.e., if tex->numLayers > 1 then tex->numFaces == 1, and vice versa).
    //
    // Note 1:
    //  You may notice the `generateMipmaps` flag in the ktxTexture struct, it
    //  is just used to instruct Vulkan or OpenGL to generate mipmaps for the
    //  texture to be uploaded NOT for mipmap generation on the CPU.
    //
    // Important:
    //  If the VkFormat related to the KTX texture creation is wrongly set, this
    //  will cause a segfault!
    //
    for (uint32_t level_idx = 0; level_idx < tex->numLevels; ++level_idx) {
        const size_t miplvl_width  = std::max(tex->baseWidth >> level_idx, 1u);
        const size_t miplvl_height = std::max(tex->baseHeight >> level_idx, 1u);
        const size_t miplvl_depth  = std::max(tex->baseDepth >> level_idx, 1u);
        // Since array layers are not supported this loop will only execute once
        for (ktx_uint32_t layer_idx = 0; layer_idx < tex->numLayers;
             ++layer_idx) {
            for (uint32_t face_idx = 0; face_idx < tex->numFaces; ++face_idx) {
                for (uint32_t slice_idx = 0; slice_idx < miplvl_depth;
                     ++slice_idx) {
                    const size_t slice_or_face_size = miplvl_width
                                                      * miplvl_height
                                                      * m_spec.pixel_bytes();
                    // Faces and Slices are mutually exclusive, addition is fine
                    const size_t slice_or_face_offset = (face_idx + slice_idx)
                                                        * slice_or_face_size;
                    auto data_ptr = m_imgs[level_idx].data();

                    // Before anything, be absolutely certain that what we are about
                    // to write is of the exact same size (in bytes) of what libktx
                    // expects us to write for this mip level.
                    const size_t expected_size
                        = ktxTexture2_GetImageSize(tex.get(), level_idx);
                    if (slice_or_face_size != expected_size) {
                        errorfmt(
                            "libktx expects {} bytes to be written for this mip"
                            " level {} but {} bytes are instead attempted to "
                            "be written",
                            expected_size, level_idx, slice_or_face_size);
                        return false;
                    }

                    // TODO: this cast to (ktxTexture*) may be UB (lifetime
                    // of ktxTexture hasn't properly started, etc.).
                    // libktx doesn't expose: ktxTexture2_SetImageFromStdioStream
                    // so for the moment use this until this issue here gets
                    // solved: https://github.com/KhronosGroup/KTX-Software/issues/1273
                    auto status = ktxTexture_SetImageFromMemory(
                        (ktxTexture*)tex.get(), level_idx, 0,
                        face_idx + slice_idx, data_ptr + slice_or_face_offset,
                        slice_or_face_size);
                    if (status != KTX_SUCCESS) {
                        errorfmt(
                            "ktxTexture_SetImageFromMemory failed. Reason: {}",
                            ktxErrorString(status));
                        return false;
                    }
                    DBG std::cout << fmt::format(
                        "[ktxoutput] ktxTexture_SetImageFromMemory for "
                        "slice_idx={} face_idx={} level_idx={} wrote {} bytes",
                        slice_idx, face_idx, level_idx, slice_or_face_size)
                                  << std::endl;
                }  // slices
            }  // faces
        }  // layers
    }  // mip levels

    // TODO: At this point we pay the memory price for at least 2X the size of
    // the whole uncompressed image (with all its mipmaps, layers, faces/slices,
    // etc.). It would be nice to just have a per-slice/face compression
    // somehow implemented here.

    //
    // After having written all necessary uncompressed data, check if the
    // texture is expected to be compressed (i.e., data should be compressed
    // using some specified GPU-block-compression format). If so, compress
    // said data using libktx.
    //
    if (m_cmp != BlockCompression::NONE) {
        switch (m_cmp) {
            // ASTC
        case BlockCompression::ASTC: {
            if (!construct_astc_params(m_astc_params))
                return false;
            if (auto status = ktxTexture2_CompressAstcEx(tex.get(),
                                                         &m_astc_params);
                status != KTX_SUCCESS) {
                errorfmt("ktxTexture2_CompressAstc failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
            break;
        }
#if 0
           // BCn
        case BlockCompression::BC1:
        case BlockCompression::BC3:
        case BlockCompression::BC4:
        case BlockCompression::BC5:
        case BlockCompression::BC7:
            // TODO: expose BCn compression quality parameter as spec attribute
            if (auto status = ktxTexture2_CompressBCn(tex, nullptr);
                status != KTX_SUCCESS) {
                errorfmt("ktxTexture2_CompressBCn failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
#endif
        default:
            errorfmt("Writing/Encoding {} compression is not supported",
                     block_compression_name(m_cmp));
            return false;
        }
    }

    //
    // If a Basis Universal compression is requested (i.e., to BasisLZ/ETC1S
    // or UASTC), compress the texture before writing. This is mutually
    // exclusive with the isCompressed check above (i.e., you can't have a
    // texture that is compressed using some GPU-block-format that also uses
    // some Basis Universal latent format).
    //
    if (m_use_basis_universal) {
        if (auto status = ktxTexture2_CompressBasisEx(tex.get(),
                                                      &m_basis_params);
            status != KTX_SUCCESS) {
            errorfmt("ktxTexture2_CompressBasisEx failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
    }

    //
    // Finally, apply the supercompression scheme (if any and only if the
    // texture does NOT already have a supercompression applied).
    //
    // Supercompression can be applied (especially if RDO is used with BCn/ASTC)
    // to significantly reduce disk file size at the expense of additional CPU
    // data load time (i.e., data now has to be inflated before being uploaded
    // to the GPU).
    //
    if (tex->supercompressionScheme == KTX_SS_NONE) {
        if (m_superCmp == KTX_SS_ZLIB) {
            if (auto status = ktxTexture2_DeflateZLIB(tex.get(), m_zlib_level);
                status != KTX_SUCCESS) {
                errorfmt("ktxTexture2_DeflateZLIB failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
        } else if (m_superCmp == KTX_SS_ZSTD) {
            if (auto status = ktxTexture2_DeflateZstd(tex.get(), m_zstd_level);
                status != KTX_SUCCESS) {
                errorfmt("ktxTexture2_DeflateZstd failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
        }
    }

    //
    // Now write key/value data (KVD). Ideally, we should follow the KTX2 spec
    // about which KVD we are encouraged to write and also follow same process
    // as KTX-Software (e.g., write used mipmap filter in `KTXScWriterParams`
    // entry, write `KTXWriter` fields, etc.)
    //

    // Add/overwrite the KTXwriter metadata entry. The specs encourages us to do
    // so.
    {
        char writer[100];
        snprintf(writer, sizeof(writer), "oiio version %d - plugin version %d",
                 OPENIMAGEIO_VERSION, OIIO_PLUGIN_VERSION);
        if (auto status
            = ktxHashList_AddKVPair(&tex->kvDataHead, KTX_WRITER_KEY,
                                    (ktx_uint32_t)strlen(writer) + 1, writer);
            status != KTX_SUCCESS) {
            errorfmt("ktxHashList_AddKVPair failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        DBG std::cout << "[ktxoutput] KTXwrite: " << writer << '\n';
    }

    //
    // Write KTXorientation metadata if textures are not oriented as
    // right-downwards (default orientation in OIIO)
    // see: https://openimageio.readthedocs.io/en/latest/stdmetadata.html
    //
    if (auto Q = m_spec.find_attribute("Orientation", TypeDesc::INT);
        Q != nullptr && Q->get_int() != 1 /* normal orientation */) {
        auto orientation        = Q->get_int();
        char orientation_str[4] = { KTX_ORIENT_X_RIGHT, KTX_ORIENT_Y_DOWN,
                                    KTX_ORIENT_Z_IN, '\0' };  // "rdi"
        switch (orientation) {
        case 2 /* flipped horizontally (top to bottom, right to left) */:
            snprintf(orientation_str, sizeof(orientation_str), "%c%c%c",
                     KTX_ORIENT_X_LEFT, KTX_ORIENT_Y_DOWN, KTX_ORIENT_Z_OUT);
            break;
        case 3 /* rotated (bottom to top, right to left) */:
            snprintf(orientation_str, sizeof(orientation_str), "%c%c%c",
                     KTX_ORIENT_X_LEFT, KTX_ORIENT_Y_UP, KTX_ORIENT_Z_IN);
            break;
        case 4 /* flipped vertically (bottom to top, left to right) */:
            snprintf(orientation_str, sizeof(orientation_str), "%c%c%c",
                     KTX_ORIENT_X_RIGHT, KTX_ORIENT_Y_UP, KTX_ORIENT_Z_OUT);
            break;
            // Transposed images are not supported by KTX
        case 5:
        case 6:
        case 7:
        case 8:
            errorfmt(
                "Transposed orientation settings are not supported by KTX");
            return false;
        default:  // should never occur
            OIIO_ASSERT(false);
            errorfmt("Unsupported \"Orientation\" attribute: {}", orientation);
            return false;
        }
        if (tex->numDimensions == 1)
            // For 1D textures, we just need to write [rl]
            orientation_str[1] = '\0';
        else if (tex->numDimensions == 2)
            // For 2D textures, we just need to write [rl][du]
            orientation_str[2] = '\0';
        if (auto status
            = ktxHashList_AddKVPair(&tex->kvDataHead, KTX_ORIENTATION_KEY,
                                    (ktx_uint32_t)strlen(orientation_str) + 1,
                                    orientation_str);
            status != KTX_SUCCESS) {
            errorfmt("ktxHashList_AddKVPair failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
    }

    Filesystem::IOProxy* m_io = ioproxy();
    if (!strcmp(m_io->proxytype(), "file")) {
        auto fd = reinterpret_cast<Filesystem::IOFile*>(m_io)->handle();
        if (auto status = ktxTexture2_WriteToStdioStream(tex.get(), fd);
            status != KTX_SUCCESS) {
            errorfmt("ktxTexture2_WriteToStdioStream failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        return true;
    }

    if (!strcmp(m_io->proxytype(), "vecoutput")) {
        auto proxy = reinterpret_cast<Filesystem::IOVecOutput*>(m_io);
        // TODO: use ktxTexture2_WriteToStream instead
        ktx_uint8_t* buff;
        ktx_size_t buff_size;
        if (auto status = ktxTexture2_WriteToMemory(tex.get(), &buff,
                                                    &buff_size);
            status != KTX_SUCCESS) {
            errorfmt("ktxTexture2_WriteToMemory failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        // RAII-based cleanup
        auto _ = std::unique_ptr<ktx_uint8_t, decltype(std::free)*>(buff,
                                                                    std::free);
        const auto nbr_written_bytes = proxy->write(buff, buff_size);
        if (nbr_written_bytes != buff_size) {
            errorfmt(
                "vecoutput proxy failed to write {} bytes but just wrote instead {}",
                buff_size, nbr_written_bytes);
            return false;
        }
        return true;
    }

    // TODO: memreader proxy

    // OIIO should guarantee that this never happens
    errorfmt("unexpected IOProxy type: {}", m_io->proxytype());
    return false;
}

OIIO_PLUGIN_NAMESPACE_END
