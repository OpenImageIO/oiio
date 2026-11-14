// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include "ktx_pvt.h"
#include <limits>
#include <regex>

OIIO_PLUGIN_NAMESPACE_BEGIN

class KtxInput final : public ImageInput {
public:
    KtxInput() {}

    ~KtxInput() override { close(); }

    const char* format_name(void) const override { return "ktx"; }

    int supports(string_view feature) const override
    {
        return (
            feature == "ioproxy" ||
            // as per the KTX1/2 specs:
            // https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_keyvalue_data
            feature == "arbitrary_metadata" ||
            // KTX2 supports 2D texture arrays, 3D texture arrays, and cubemap
            // arrays. That being said, 2D texture arrays is the only one
            // supported by this OIIO plugin.
            feature == "multiimage" ||
            // KTX2 supports mipmaps. 3D texture mipmaps are treated as a
            // per-slice mipmap.
            feature == "mipmap");
    }

    bool valid_file(Filesystem::IOProxy* ioproxy) const override;

    bool open(const std::string& name, ImageSpec& newspec) override;

    bool open(const std::string& name, ImageSpec& newspec,
              const ImageSpec& config) override;

    // Calls the span-based implementation with inferred data buffer length
    bool read_native_scanline(int subimage, int miplevel, int y, int z,
                              void* data) override;

    // Calls the span-based implementation with inferred data buffer length
    bool read_native_scanlines(int subimage, int miplevel, int ybegin, int yend,
                               int z, void* data) override;

    // Actual implementation for 1d/2d textures
    bool read_native_scanlines(int subimage, int miplevel, int ybegin, int yend,
                               span<std::byte> data) override;

    // Calls the span-based implementation with inferred data buffer length
    bool read_native_tile(int subimage, int miplevel, int x, int y, int z,
                          void* data) override;

    // Calls the span-based implementation with inferred data buffer length
    bool read_native_tiles(int subimage, int miplevel, int xbegin, int xend,
                           int ybegin, int yend, int zbegin, int zend,
                           void* data) override;

    // Actual implementation for Cubemap textures
    bool read_native_tiles(int subimage, int miplevel, int xbegin, int xend,
                           int ybegin, int yend, span<std::byte> data) override;

    // Actual implementation for volume textures
    // Note: current OIIO API does not make any call to this method. This is
    // just implemented for future use and to be called within read_native_tiles
    // when the texture is a volume.
    bool read_native_volumetric_tiles(int subimage, int miplevel, int xbegin,
                                      int xend, int ybegin, int yend,
                                      int zbegin, int zend,
                                      span<std::byte> data) override;


    const std::string& filename() const { return m_filename; }

    void init() noexcept;

    bool close() override;

    int current_subimage(void) const override { return m_subimage; }

    int current_miplevel(void) const override { return m_miplevel; }

    bool seek_subimage(int subimage, int miplevel) override;

private:
    std::string m_filename {};

    // KTX2 texture.
    std::unique_ptr<ktxTexture2, decltype(ktxTexture2_Destroy)*> m_tex {
        nullptr, ktxTexture2_Destroy
    };

    int m_subimage { -1 };  // What subimage are we looking at. This is not used
                            // anywhere else except in current_subimage()

    int m_miplevel { -1 };  // What mip level are we looking at. This is not
                            // used anywhere else except in current_miplevel()

    // GPU block compression kind (only set in case of GPU-block-compressed KTX
    // textures).
    BlockCompression m_cmp { BlockCompression::NONE };

    std::unique_ptr<ImageSpec> m_config;  // Saved copy of configuration spec

    // Checks the magic
    bool ktx_magic_cmp(cspan<uint8_t> sig) const noexcept;

    std::string get_colorspace() const noexcept;

    // std::string& because string_view are still not supported by std::regex
    void parse_ktx_sc_params_metadata(const std::string& ktx_sc_params);

    bool check(int subimage, int miplevel) const noexcept;
};



// Obligatory material to make this a recognizable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int ktx_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
ktx_imageio_library_version()
{
    return "libktx v" OIIO_STRINGIZE(Ktx_VERSION_MAJOR) "." OIIO_STRINGIZE(
        Ktx_VERSION_MINOR) "." OIIO_STRINGIZE(Ktx_VERSION_PATCH);
}
OIIO_EXPORT ImageInput*
ktx_input_imageio_create()
{
    return new KtxInput;
}
OIIO_EXPORT const char* ktx_input_extensions[] = { "ktx2", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
KtxInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    // Check 'config' for any special requests
    ioproxy_retrieve_from_config(config);
    m_config.reset(new ImageSpec(config));  // save config spec
    return open(name, newspec);
}



bool
KtxInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    if (!ioproxy_use_or_open(name))
        return false;

    // If an IOProxy was passed, it had better be a File or a MemReader
    Filesystem::IOProxy* m_io = ioproxy();
    std::string proxytype     = m_io->proxytype();
    if (proxytype != "file" && proxytype != "memreader") {
        errorfmt("ktx reader can't handle proxy type {}", proxytype);
        return false;
    }

    // check if magic to insure that this is a KTX2 file
    if (!this->valid_file(m_io)) {
        errorfmt("\"{}\" is not a KTX2 file, magic number doesn't match", name);
        return false;
    }

    //
    // Load just the header (the flag KTX_TEXTURE_CREATE_NO_FLAGS means the data
    // isn't loaded). We load the actual images AFTER performing check_open()s.
    //
    // For under-the-hood details, see official libktx repo:
    // https://github.com/KhronosGroup/KTX-Software/blob/main/lib/src/texture.c
    //
    if (proxytype == "file") {
        FILE* fd = reinterpret_cast<Filesystem::IOFile*>(m_io)->handle();
        ktxTexture2* p_tex = nullptr;
        auto res
            = ktxTexture2_CreateFromStdioStream(fd, KTX_TEXTURE_CREATE_NO_FLAGS,
                                                &p_tex);
        m_tex.reset(p_tex);
        if (KTX_SUCCESS != res) {
            errorfmt("Failed to create ktx texture using "
                     "ktxTexture2_CreateFromStdioStream. Reason: {}",
                     ktxErrorString(res));
            return false;
        }
    } else /* (proxytype == "memreader") */ {
        OIIO_ASSERT(proxytype == "memreader");
        auto buff = reinterpret_cast<Filesystem::IOMemReader*>(m_io)->buffer();
        ktxTexture2* p_tex = nullptr;
        auto res = ktxTexture2_CreateFromMemory(buff.data(), buff.size(),
                                                KTX_TEXTURE_CREATE_NO_FLAGS,
                                                &p_tex);
        m_tex.reset(p_tex);
        if (KTX_SUCCESS != res) {
            errorfmt(
                "Failed to create ktx texture using ktxTexture2_CreateFromMemory. Reason: {}",
                ktxErrorString(res));
            return false;
        }
    }

    // We don't support 3D texture arrays because no graphics/compute API
    // supports these. `ktx info` on a 3D texture KTX2 file outputs the
    // following:
    //  warning-3014: File contains a 3D array texture.
    //  File contains a 3D array texture. No API supports these.
    if (m_tex->isArray && m_tex->numDimensions == 3) {
        errorfmt("3D texture arrays are not supported");
        return false;
    }

    const bool is_hdr = ktxTexture2_IsHDR(m_tex.get());
    DBG std::cout << "[ktxinput] is_hdr: " << is_hdr << std::endl;

    // Create a default spec and seek_subimage to set the dimensions based on
    // the texture type (format is set later based on the input VkFormat).
    m_spec = ImageSpec();
    if (!seek_subimage(0, 0))
        return false;  // errorfmt is set via seek_subimage

    //
    // Make sure to set the nchannels, channelnames, and format BEFORE any
    // check_open() calls.
    //
    // In case of a Basis Universal texture: there isn't a "fixed" nchannels.
    // It depends on what our transcode target is. In OIIO, we will always
    // either transcode to KTX_TTF_RGBA32 or KTX_TTF_RGB_HALF. The former is
    // LDR while the latter is HDR.
    //
    if (ktxTexture2_IsTranscodable(m_tex.get())) {
        m_spec.nchannels = is_hdr ? 3 : 4;
        m_spec.format    = is_hdr ? TypeDesc::HALF : TypeDesc::UINT8;
        m_spec.default_channel_names();
    } else /* non-basisu-encoded texture; nchannels is fixed in VkFormat */ {
        FormatInfo format_info;
        if (!get_info_from_vkformat(static_cast<VkFormat>(m_tex->vkFormat),
                                    format_info)) {
            close();
            errorfmt("Could not extract format info from provided "
                     "VkFormat: {}. This format is unsupported",
                     m_tex->vkFormat);
            return false;
        }
        m_spec.nchannels = format_info.nbrchannels;
        m_spec.format    = format_info.typedesc;
        m_spec.default_channel_names();
    }

    std::string colorspace = get_colorspace();
    m_spec.set_colorspace(colorspace);

    // Set textureformat attribute
    if (m_tex->numDimensions == 1) {
        m_spec.attribute("textureformat", "Plain Texture");
    } else if (m_tex->numDimensions == 2) {
        if (m_tex->isCubemap) {
            m_spec.attribute("textureformat", "CubeFace Environment");
            // In KTX (as per the specs) cubemap faces are always in this order
            m_spec.attribute("ktx:CubeMapSides", "+x -x +y -y +z -z");
        } else {
            m_spec.attribute("textureformat", "Plain Texture");
        }
    } else if (m_tex->numDimensions == 3) {
        m_spec.attribute("textureformat", "Volume Texture");
    } else {
        assert(false);
        return false;  // should never occur
    }

    DBG std::cout << "[ktxinput] \"textureformat\": \""
                  << m_spec.get_string_attribute("textureformat") << '"'
                  << std::endl;

    // We don't support vendor-specific supercompression schemes
    if (m_tex->supercompressionScheme > KTX_SS_END_RANGE) {
        errorfmt("Unsuppoted vendor-specific supercompression scheme: {}",
                 ktxSupercompressionSchemeString(m_tex->supercompressionScheme));
        return false;
    }

    //
    // KTX can hold layered compressions (i.e., on top of the potential GPU-
    // compatible compression like ASTC, the whole data can be furthermore
    // compressed using a super compression scheme). We call such compression
    // `supercompression` and does NOT refer to the usual compression (e.g.,
    // GPU-block compression, or Basis Universal codecs).
    //
    // Make sure to save everything that is needed to recreate this "exact same"
    // KTX2 texture from OIIO API (i.e., fields of `ktxTextureCreateInfo`).
    // Of course, the term "exact same" takes into account the quality loss when
    // reading lossy-compressed KTX2 files and saving them as KTX2 files again.
    //
    // KtxTexture fields may change after some libktx calls that take
    // KtxTexture2* argument because they may potentially modify the texture
    // (e.g., in ktxTexture2_DecodeAstc supercompressionScheme is overwritten to
    // none, ktxTexture2_TranscodeBasis overwrites texture format, etc.). So,
    // store these now and NOT after libktx calls (e.g.,
    // ktxTexture2_TranscodeBasis).
    //
    // For BasisLZ, since it is always required when using ETC1S Basis Universal
    // codec, there is no reason to save it (not needed for roundtrip).
    //
    switch (m_tex->supercompressionScheme) {
    case KTX_SS_ZSTD: m_spec.attribute("compression", "zstd"); break;
    case KTX_SS_ZLIB: m_spec.attribute("compression", "zip" /* zlib */); break;
    default: break;
    }

    // is this a 1D texture? If so, save that it is because we can't figure it
    // just based on dimensions (a texture with height 1 can be either a 1D or
    // 2D texture).
    if (m_tex->numDimensions == 1)
        m_spec.extra_attribs.attribute("ktx:1d", true);
    // Contrary to the specs' layerCount, numLayers is always >= 1
    if (m_tex->numLayers > 1)
        m_spec.attribute("oiio:subimages", static_cast<int>(m_tex->numLayers));
    // Save number of miplevels to give the option to users to avoid having to
    // figure it out by calling `seek_subimage` multiple times
    if (m_tex->numLevels > 1)
        m_spec.extra_attribs.attribute("oiio:miplevels", m_tex->numLevels);
    if (m_tex->generateMipmaps)
        m_spec.extra_attribs.attribute("ktx:generate_mipmaps", true);

    //
    // Save arbitrary metadata. KTX allows for the storage of arbitrary
    // key/value metadata pairs as per the specification here:
    //  https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_keyvalue_data
    //
    // KTX2 specs defines a predifined set of key/value metadata at
    //  https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_predefined_keyvalue_pairs
    //
    // Predifined keys we care about:
    //
    //  - KTXwriterScParams: null-terminated string
    //  - KTXcubemapIncomplete: 1 byte bitfield
    //  - KTXorientation: null-terminated string
    //
    // We don't care about the rest and we don't save them.
    //
    auto kventry = m_tex->kvDataHead;
    if (kventry)
        do {
            auto status = KTX_SUCCESS;
            unsigned int keylen { 0 };
            unsigned int vallen { 0 };
            char* key { nullptr };
            void* val { nullptr };

            if ((status = ktxHashListEntry_GetKey(kventry, &keylen, &key))
                != KTX_SUCCESS) {
                DBG std::cerr << "ktxHashListEntry_GetKey failed. Reason: "
                              << ktxErrorString(status) << std::endl;
                continue;
            }

            // Per KTX2 spec: "The key must be terminated by a NUL character"
            // We exit on any invalid key that is encountered
            if (keylen <= 1 || key[keylen - 1] != '\0') {
                errorfmt(
                    "An empty or non-null terminated metadata key string encountered. This is an invalid KTX file");
                return false;
            }

            if ((status = ktxHashListEntry_GetValue(kventry, &vallen, &val))
                != KTX_SUCCESS) {
                errorfmt("ktxHashListEntry_GetValue failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }

            // vallen checks are done below depending on the attribute name

            // keylen includes NUL termination char!
            auto attr_name              = OIIO::string_view(key, keylen - 1);
            auto ktx_prefixed_attr_name = fmt::format("ktx:{}", attr_name);

            if (attr_name == KTX_WRITER_SCPARAMS_KEY) {
                //
                // KTXwriterScParams is used to report all kinds of non-default
                // parameters used by ktx tools to write this KTX2 file.
                // This includes non default Basis Universal params (i.e., for
                // UASTC/ETC1S), non-default supercompression params,
                // non-default mipmap generation params, etc.
                // Should be NUL terminated.
                //
                auto cval = static_cast<const char*>(val);
                if (vallen <= 1 || cval[vallen - 1] != '\0') {
                    errorfmt(
                        "Invalid KTX2 file: an empty or non-null "
                        "terminated string for \"KTXwriterScParams\" metadata");
                    return false;
                }
                // vallen includes NUL char!
                parse_ktx_sc_params_metadata(std::string(cval, vallen - 1));
            } else if (attr_name == "KTXcubemapIncomplete") {
                //
                // KTX supports incomplete cubemaps (i.e., cubemaps with less
                // than 6 faces). These incomplete cubemaps are flattened into
                // 2D arrays. Per the specs:
                //  Applications wanting to store incomplete cubemaps should
                //  flatten faces into a 2D array [...]
                //
                errorfmt("Incomplete cubemaps are currently not supported");
                return false;
            } else if (attr_name == KTX_ORIENTATION_KEY) {
                // KTX may define a different orientation than the one used by OIIO. See:
                //  https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_ktxorientation
                OIIO_ASSERT(vallen <= 4);  // at most '[rl][du][oi]\0'
                auto cval = static_cast<const char*>(val);
                if (vallen <= 1 || vallen > 4 || cval[vallen - 1] != '\0') {
                    errorfmt(
                        "Invalid KTX2 file: an empty or non-null "
                        "terminated string for \"KTXorientation\" metadata");
                    return false;
                }
                OIIO::string_view val_str(cval, vallen - 1);
                const auto error_msg = fmt::format(
                    "Given KTX2 file does not comply with KTX2 specs."
                    " Metadata \"KTXorientation\" {} is invalid.",
                    val_str);
                if (val_str.length() != m_tex->numDimensions) {
                    errorfmt("{}", error_msg);
                    return false;
                }
                int orientation = 1;
                switch (m_tex->numDimensions) {
                case 1:
                    if (val_str[0] == KTX_ORIENT_X_LEFT) {
                        orientation = 2;
                    } else if (val_str[0] == KTX_ORIENT_X_RIGHT) {
                        orientation = 1;
                    } else {
                        errorfmt("{}", error_msg);
                        return false;
                    }
                    break;
                case 2:
                case 3:
                    if (val_str[0] == KTX_ORIENT_X_LEFT
                        && val_str[1] == KTX_ORIENT_Y_DOWN) {
                        orientation = 2;
                    } else if (val_str[0] == KTX_ORIENT_X_LEFT
                               && val_str[1] == KTX_ORIENT_Y_UP) {
                        orientation = 3;
                    } else if (val_str[0] == KTX_ORIENT_X_RIGHT
                               && val_str[1] == KTX_ORIENT_Y_UP) {
                        orientation = 4;
                    } else if (val_str[0] == KTX_ORIENT_X_RIGHT
                               && val_str[1] == KTX_ORIENT_Y_DOWN) {
                        orientation = 1;
                    } else {
                        errorfmt("{}", error_msg);
                        return false;
                    }
                    break;
                default:  // should never occur
                    OIIO_ASSERT(false);
                    return false;
                }

                DBG std::cout << fmt::format(
                    "[ktxinput] set OIIO orientation: {} from {}: {}\n",
                    orientation, KTX_ORIENTATION_KEY, val_str);
                if (orientation != 1)
                    m_spec.attribute("Orientation", orientation);
            }

        } while ((kventry = ktxHashList_Next(kventry)));

    //
    // BEFORE actually loading/uncompressing the data, impose some reasonable
    // limits on the dimensions.
    //
    // These ranges are set based on similar limits used in DDS plugin.
    //
    // The following calculations assume worst-case scenario with
    // VK_FORMAT_R64G64B64A64_SFLOAT input textures (i.e., 4 channels with each
    // 8 bytes). For:
    //  - 2D textures:        32768 * 32768         => 32 GBs
    //  - 2D array textures:  32768 * 32768 * 2048  => 65536 GBs
    //  - cubemaps:           16384 * 98304         => 48 GBs
    //  - cubemap textures:   16384 * 98304 * 2048  => 98304 GBs
    //  - 3D textures:        4096 * 4096 * 4096    => 2048 GBs
    //
    // For non-array textures: rely on the memory size checks inside check_open.
    // For array texture: check_open checks are insufficient because they don't
    // take into account nbr subimages (i.e., nbr layers in array)!
    //
    if (m_tex->isArray) {
        if (m_tex->numLayers > 2048) {
            errorfmt(
                "number of layers in input KTX2 texture exceeds the limit: {} > {}",
                m_tex->numLayers, 2048);
            return false;
        }
        int limit_imagesize_MB = 0;
        OIIO::getattribute("limits:imagesize_MB", TypeDesc::INT,
                           &limit_imagesize_MB);
        const auto array_size_in_bytes = m_spec.image_bytes(true)
                                         * imagesize_t(m_tex->numLayers);
        if (limit_imagesize_MB
            && array_size_in_bytes
                   > limit_imagesize_MB * imagesize_t(1024 * 1024)) {
            errorfmt(
                "input texture array is too large ({} MB) and exceeds the set memory limits of: {} MB",
                float(array_size_in_bytes) / float(1024 * 1024),
                limit_imagesize_MB);
            return false;
        }
    }
    if (m_tex->baseDepth > 1) {
        // 3D texture (3D texture arrays not supported)
        if (!check_open(m_spec, { 0, 4096, 0, 4096, 0, 4096, 0, 4 }))
            return false;
    } else if (m_tex->isCubemap) {
        // Cubemap textures or Cubemap texture arrays
        if (!check_open(m_spec, { 0, 16384, 0, 16384 * 6, 0, 1, 0, 4 }))
            return false;
    } else {
        // 1D/2D textures or 1D/2D texture arrays
        if (!check_open(m_spec, { 0, 32768, 0, 32768, 0, 1, 0, 4 }))
            return false;
    }

    //
    // Load the image(s) data (libktx owns the data buffer). This is where the
    // buffer allocation for un-supercompressed textures occurs.
    //
    // TODO: per-subimage and per-miplevel KTX2 loading
    // Implementing the per-subimage allocation approach requires some effort.
    // For the moment, let's make sure this approach is working.
    if (auto result = ktxTexture2_LoadImageData(m_tex.get(), NULL, 0);
        result != KTX_SUCCESS) {
        errorfmt("ktxTexture2_LoadImageData failed. Reason: {}",
                 ktxErrorString(result));
        return false;
    }

    // At this point: images are certainly not super-compressed but are
    // potentially basisu-compressed, GPU-block-compressed, or simply
    // uncompressed.

    //
    // Do we need to transcode this texture (i.e., is this a Basis Universal
    // texture format)?
    //
    // KTX2 provides transcoders that can directly target raw, uncompressed
    // pixels (via the KTX_TTF_RGBA32 flag).
    //
    // Important:
    // This modifies the KtxTexture2 (m_tex) therefore make sure to save
    // essential properties for proper KTX2 regeneration/roundtrip.
    //
    // Note:
    // Do not use ktxTexture2_NeedsTranscoding as it returns false for
    // UASTC_HDR_4x4. For all transcodable formats, we want to transcode them
    // here to uncompressed format (RGBA32 for LDR and RGBA_HALF for HDR).
    //
    if (ktxTexture2_IsTranscodable(m_tex.get())) {
        khr_df_model_e color_model = ktxTexture2_GetColorModel_e(m_tex.get());
        if (auto status = ktxTexture2_TranscodeBasis(
                m_tex.get(), is_hdr ? KTX_TTF_RGB_HALF : KTX_TTF_RGBA32, 0);
            status != KTX_SUCCESS) {
            errorfmt("failed to transcode KTX2 texture to raw pixels. "
                     "ktxTexture2_TranscodeBasis failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        // Save Basis Universal codec in the "ktx:codec" attribute (this
        // attribute is also used to save GPU-block-based compressions)
        m_spec.attribute("ktx:codec", basis_universal_codec_name(color_model));
    }

    // At this point: images are certainly not basisu-compressed and are either
    // GPU-block-compressed (e.g., ASTC) or simply uncompressed.

    //
    // Decode GPU-compression if any (using libktx). Supported formats:
    //
    //    ASTC: libktx provides decoders for ASTC block compression via the
    //          ktxTexture2_DecodeAstc call.
    //          This currently decodes the whole texture (all miplevels, all
    //          slices, etc.) into memory.
    //
    //    BCn:  libktx will provide decoders/encoders for BCn block compression
    //          via ktxTexture2_DecodeBCn.
    //          TODO: wait for libktx BCn PR to get merged then add BCn support.
    //
    //    ETC2: libktx provides decoders but they fall under non-open-source
    //          license. To quote KTX-Software: "The file lib/etcdec.cxx is not
    //          open source. It is made available under the terms of an Ericsson
    //          license, found in the file itself."
    //
    //    PVRTC: not planned (there are pending PRs in libktx).
    //
    if (m_tex->isCompressed /* i.e., is GPU block compressed? */) {
        FormatInfo format_info;
        if (!get_info_from_vkformat(static_cast<VkFormat>(m_tex->vkFormat),
                                    format_info)) {
            close();
            errorfmt("Could not extract format info from provided "
                     "VkFormat: {}. This format is unsupported",
                     m_tex->vkFormat);
            return false;
        }
        m_cmp = format_info.compression;
        m_spec.attribute("ktx:codec", block_compression_name(m_cmp));
        switch (m_cmp) {
#if 0  // TODO: wait for my PR in libktx to be merged
            /* BCn GPU formats */
        case BlockCompression::BC1:
        case BlockCompression::BC1A:
        case BlockCompression::BC2:
        case BlockCompression::BC3:
        case BlockCompression::BC4:
        case BlockCompression::BC5:
        case BlockCompression::BC6HU:
        case BlockCompression::BC6HS:
        case BlockCompression::BC7:
            //
            // Note:
            // ktxTexture2_DecodeBCn internally creates a new ktxTexture2 texture
            // and populates it with decoded data from the originally provided
            // texture. At the end, it moves the decoded data to m_tex and
            // destroys the temporarily created texture.
            //
            // This operation is expensive (both in memory and CPU cycles).
            // After this, m_tex->isCompressed will be false => this will only
            // be called once.
            //
            if (auto status = ktxTexture2_DecodeBCn(m_tex);
                status != KTX_SUCCESS) {
                errorfmt("failed to decode BCn-compressed texture. "
                         "ktxTexture2_DecodeBCn failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
            break;
#endif

            /* ASTC formats */
        case BlockCompression::ASTC: {
            // save the ASTC block dimensions from its VkFormat
            if (auto it = std::find_if(astc_block_dims_str_to_enum.cbegin(),
                                       astc_block_dims_str_to_enum.cend(),
                                       [&](auto const& pair) {
                                           return pair.second
                                                  == format_info.astc_block_dims;
                                       });
                it != astc_block_dims_str_to_enum.end()) {
                m_spec.attribute("ktx:astc_block_dimensions", it->first);
            } else {
                // should only occur if astc_block_dims_str_to_enum and
                // get_info_from_vkformat are not in sync
                OIIO_ASSERT(false);
                return false;
            }
            //
            // Note:
            // ktxTexture2_DecodeAstc internally creates a new ktxTexture2 texture
            // and populates it with decoded data from the originally provided
            // texture. At the end, it moves the decoded data to m_tex and
            // destroys the temporarily created texture.
            //
            // This operation is expensive (both in memory and CPU cycles).
            // After this, m_tex->isCompressed will be false => this will only
            // be called once.
            //
            if (auto status = ktxTexture2_DecodeAstc(m_tex.get());
                status != KTX_SUCCESS) {
                errorfmt("Failed to decode ASTC-compressed texture. "
                         "ktxTexture2_DecodeAstc failed. Reason: {}",
                         ktxErrorString(status));
                return false;
            }
            break;
        }

        default:
            errorfmt("{} GPU-compressed formats are not supported",
                     block_compression_name(m_cmp));
            return false;
        }
    }

    OIIO_ASSERT(!m_tex->isCompressed);

    // At this point: images are certainly uncompressed (non-basisu-encoded,
    // non-supercompressed, and non-GPU-block-compressed).

    //
    // This could mean one of the following as per the specs at:
    // https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_use_of_vk_format_undefined
    //
    //  1.  For custom formats that do not have any equivalent in GPU APIs.
    //      => Not supported.
    //
    //  2.  ETC1S/UASTC supercompression scheme: makes no sense since we
    //      transcoded it above to uncompressed format.
    //
    //  3.  For any formats from any GPU APIs that do not have Vulkan
    //      equivalents. E.g., OpenGL/Direct3D/Metal formats.
    //      In this case, one of the following metadata entries have to be
    //      present:
    //      - "KTXglFormat" for OpenGL
    //      - "KTXdxgiFormat__" for Direct3D
    //      - "KTXmetalPixelFormat" for Metal
    //      => Not supported.
    //
    //  4.  Compressed color models in Section 5.6 of [KDF14] or successors that
    //      do not have corresponding Vulkan formats.
    //      => Not supported.
    //
    // We only keep this for better error messaging.
    //
    if (m_tex->vkFormat == VK_FORMAT_UNDEFINED) {
        errorfmt(
            "VkFormat of provided KTX texture is VK_FORMAT_UNDEFINED "
            "which potentially means that a custom format with no equivalent "
            "in GPU APIs is provided. This is not supported.");
        return false;
    }

    const auto format = static_cast<VkFormat>(m_tex->vkFormat);

    //
    // Important:
    // Call this AFTER transcoding the basis universal scheme (i.e., after
    // ktxTexture2_TranscodeBasis) and AFTER detecting which GPU block
    // compression scheme is used.
    //
    FormatInfo format_info;
    if (!get_info_from_vkformat(static_cast<VkFormat>(format), format_info)) {
        errorfmt(
            "Failed to extract info (e.g., nchannels, typedesc, etc.) from VkFormat: {}",
            static_cast<uint32_t>(format));
        return false;
    }
    m_spec.set_format(format_info.typedesc);
    m_spec.nchannels = format_info.nbrchannels;
    // Save whether this is not a UNORM/SNORM VkFormat so that it can be
    // reproduced
    if (!format_info.normalized_vkformat)
        m_spec.attribute("ktx:normalized", false);

    newspec = m_spec;
    return true;
}



void
KtxInput::init() noexcept
{
    m_filename = std::string();
    m_tex.reset(nullptr);
    m_subimage = -1;
    m_miplevel = -1;
    m_cmp      = BlockCompression::NONE;
    m_config.reset(nullptr);
    ioproxy_clear();
}



bool
KtxInput::close()
{
    // Re-initialize to default so that this same instance can be re-opened
    init();
    return true;
};



bool
KtxInput::check(int subimage, int miplevel) const noexcept
{
    //
    // Before doing any calls, check if provided subimage and mip lvl are valid.
    // This is how OIIO figures out the number of subimages/miplevels.
    //
    if (subimage < 0 || miplevel < 0 || (uint32_t)subimage >= m_tex->numLayers
        || (uint32_t)miplevel >= m_tex->numLevels)
        return false;  // don't errorfmt here
    return true;
}



//
// Since we load the whole KTX texture data in open(), actual seeking makes no
// sense in this context. You can just read at any pixel by just computing an
// offset.
//
// What this seek_subimage does, is essentially parameter verification (i.e.,
// are provided subimage and miplevel sane for the current texture kind) and
// setting up dimensions and tile dimensions for cubemap or volume textures.
//
// In the context of KTX:
// - For 3D textures (i.e., depth > 1): `subimage` does NOT correspond to the
//   3D texture depth slice (unlike how libktx 'considers' 3D slices and cubemap
//   faces equivalent; from point of view of the API). `subimage` SHOULD be 0.
//   Volumes are expected to be read using `read_native_tiles` where each tile
//   is simulated as a single scanline (same way as how it is done in DDS).
// - For Cubemaps (i.e., tile_width/tile_height > 1): `subimage` also does NOT
//   correspond to the face slice (again, unlike how libktx accesses cubemap
//   faces using slices/subimages). Incomplete cubemaps
//   (i.e., number of faces < 6) are currently not supported.
// - Arrays of 2D/cubemap textures: `subimage` maps to layer in libktx (i.e.,
//   index in array). `subimage` SHOULD be [0, m_tex->numLayers[.
// - Arrays of 3D textures: currently not supported.
//
// For all kinds of textures, `miplevel` is ALWAYS interpreted as a mip level of
// the above `subimage` (e.g., `miplevel` 0 of 3D texture refers to base-level
// volume).
//
bool
KtxInput::seek_subimage(int subimage, int miplevel)
{
    if (!check(subimage, miplevel))
        return false;

    // if same subimage and miplevel as current => early out
    if (this->current_subimage() == subimage
        && this->current_miplevel() == miplevel)
        return true;

    //
    // According to official libktx source code, this is how they compute
    // dimensions of a miplevel. See:
    //  https://github.com/KhronosGroup/KTX-Software/lib/src/texture.c
    //
    const uint32_t width  = std::max(m_tex->baseWidth >> miplevel, 1u);
    const uint32_t height = std::max(m_tex->baseHeight >> miplevel, 1u);
    const uint32_t depth  = std::max(m_tex->baseDepth >> miplevel, 1u);

    // Checks against overflows
    const uint32_t int_max = static_cast<uint32_t>(
        std::numeric_limits<int>::max());
    if (width > int_max || height > int_max || depth > int_max)
        return false;

    if (m_tex->isCubemap) {
        // Overflow check
        if (height > int_max / 6)
            return false;
    }

    m_subimage = subimage;
    m_miplevel = miplevel;

    m_spec.width = m_spec.full_width = static_cast<int>(width);
    m_spec.height = m_spec.full_height = static_cast<int>(height);
    m_spec.depth = m_spec.full_depth = static_cast<int>(depth);

    //
    // For volume texture, pretend it's a tiled texture with:
    // width x 1 x 1 tiles.
    // Why? Current OIIO API does not provide safe span-based
    // read_native_scanlines with a depth parameter (i.e., `z` parameter).
    //
    if (m_tex->numDimensions == 3) {
        m_spec.tile_width  = static_cast<int>(width);
        m_spec.tile_height = 1;
        m_spec.tile_depth  = 1;
    }

    // Pretend a 1x6 layout (libktx doesn't provide access to cubemaps as whole
    // textures but rather through a per-face index API)
    if (m_tex->isCubemap) {
        m_spec.height = m_spec.full_height = static_cast<int>(height * 6);
        m_spec.tile_width                  = static_cast<int>(width);
        m_spec.tile_height                 = static_cast<int>(height);
        m_spec.tile_depth                  = 1;
    }

    return true;
}



bool
KtxInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    const int width = std::max(m_tex->baseWidth >> miplevel, 1u);
    // Use read_native_volumetric_tiles() to read scanlines from volume slices
    if (z != 0)
        return false;
    // Simply call the span-base multi-scanline method with inferred length
    return read_native_scanlines(subimage, miplevel, y, y + 1,
                                 make_span((std::byte*)data,
                                           m_spec.pixel_bytes() * width));
}



bool
KtxInput::read_native_scanlines(int subimage, int miplevel, int ybegin,
                                int yend, int z, void* data)
{
    const int width      = std::max(m_tex->baseWidth >> miplevel, 1u);
    const int height     = std::max(m_tex->baseHeight >> miplevel, 1u);
    const auto data_span = make_span((std::byte*)data, m_spec.pixel_bytes()
                                                           * width
                                                           * (yend - ybegin));
    // Use read_native_volumetric_tiles() to read scanlines from volume slices
    if (z != 0)
        return read_native_volumetric_tiles(subimage, miplevel, 0, width,
                                            ybegin, yend, z, z + 1, data_span);
    if (ybegin < 0 || ybegin >= yend || yend > height) {
        errorfmt(
            "KTX read_native_scanlines: Out of valid range scanlines range");
        return false;
    }
    // Simply call the span-base multi-scanline method with inferred length
    return read_native_scanlines(subimage, miplevel, ybegin, yend, data_span);
}



bool
KtxInput::read_native_scanlines(int subimage, int miplevel, int ybegin,
                                int yend, span<std::byte> data)
{
    if (!check(subimage, miplevel)) {
        errorfmt("KTX read_native_scanlines: invalid subimage or miplevel");
        return false;
    }

    // Use read_native_tiles() to read cubemap faces or
    // read_native_volumetric_tiles() to read 3D volume slices
    if (m_tex->isCubemap || m_tex->numDimensions == 3) {
        return false;
    }

    const int width             = std::max(m_tex->baseWidth >> miplevel, 1u);
    const int height            = std::max(m_tex->baseHeight >> miplevel, 1u);
    const size_t pitch_in_bytes = m_spec.pixel_bytes() * width;
    ktx_size_t offset;

    if (ybegin < 0 || ybegin >= yend || yend > height) {
        errorfmt(
            "KTX read_native_scanlines: Out of valid range scanlines range");
        return false;
    }

    // Can the provided span hold the requested scanlines?
    // This only accesses nchannels of the m_spec, so this is thread safe.
    if (!valid_raw_span_size(data, m_spec, 0, width, ybegin, yend))
        return false;  // errorfmt is set within valid_raw_span_size

    if (const size_t libktx_image_size = ktxTexture2_GetImageSize(m_tex.get(),
                                                                  miplevel),
        expected_image_size            = pitch_in_bytes * height;
        libktx_image_size != expected_image_size) {
        errorfmt("Expected image size of {} but libktx reported {}",
                 expected_image_size, libktx_image_size);
        return false;
    }

    // Check that our pitch (from spec) is equal to the one reported by libktx
    if (auto libktx_pitch_in_bytes
        = ktxTexture_GetRowPitch((ktxTexture*)m_tex.get(), miplevel);
        pitch_in_bytes != libktx_pitch_in_bytes) {
        errorfmt("Expected a pitch of size {} but libktx expects {}",
                 pitch_in_bytes, libktx_pitch_in_bytes);
        return false;
    }

    //
    // GetImageOffset implements internal checks depending on texture kind
    // (e.g., 3D, cubemap, etc.) and in case of invalid input,
    // KTX_INVALID_OPERATION is returned.
    //
    if (auto status = ktxTexture2_GetImageOffset(m_tex.get(), miplevel,
                                                 subimage, 0, &offset);
        status != KTX_SUCCESS) {
        errorfmt("ktxTexture2_GetImageOffset failed. Reason: {}",
                 ktxErrorString(status));
        return false;
    }

    auto src_ptr = m_tex->pData + offset + ktx_size_t(ybegin) * pitch_in_bytes;
    memcpy(data.data(), src_ptr, pitch_in_bytes * size_t(yend - ybegin));
    DBG std::cout << fmt::format(
        "[ktxinput] read_native_scanlines(subimage={},miplevel={},ybegin={},yend={})\n",
        subimage, miplevel, ybegin, yend);
    return true;
}



bool
KtxInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                           void* data)
{
    int tile_width  = std::max(m_tex->baseWidth >> miplevel, 1u);
    int tile_height = std::max(m_tex->baseHeight >> miplevel, 1u);
    int tile_depth  = std::max(m_tex->baseDepth >> miplevel, 1u);
    // Use read_native_volumetric_tiles() for volume tiles
    if (m_tex->numDimensions == 3) {
        tile_height = 1;
        tile_depth  = 1;
        return read_native_volumetric_tiles(
            subimage, miplevel, x, x + tile_width, y, y + tile_height, z,
            z + tile_depth,
            make_span(static_cast<std::byte*>(data),
                      m_spec.pixel_bytes() * tile_width));
    }
    return read_native_tiles(subimage, miplevel, x, x + tile_width, y,
                             y + tile_height,
                             make_span(static_cast<std::byte*>(data),
                                       m_spec.pixel_bytes() * tile_width
                                           * tile_height));
}



bool
KtxInput::read_native_tiles(int subimage, int miplevel, int xbegin, int xend,
                            int ybegin, int yend, int zbegin, int zend,
                            void* data)
{
    const int tile_width = std::max(m_tex->baseWidth >> miplevel, 1u);
    // Use read_native_volumetric_tiles() for volume tiles
    if (m_tex->numDimensions == 3)
        return read_native_volumetric_tiles(
            subimage, miplevel, xbegin, xend, ybegin, yend, zbegin, zend,
            make_span((std::byte*)data, m_spec.pixel_bytes() * tile_width
                                            * size_t(yend - ybegin)
                                            * size_t(zend - zbegin)));
    return read_native_tiles(subimage, miplevel, xbegin, xend, ybegin, yend,
                             make_span(static_cast<std::byte*>(data),
                                       m_spec.pixel_bytes() * tile_width
                                           * size_t(yend - ybegin)));
}



bool
KtxInput::read_native_tiles(int subimage, int miplevel, int xbegin, int xend,
                            int ybegin, int yend, span<std::byte> data)
{
    const int width             = std::max(m_tex->baseWidth >> miplevel, 1u);
    const int height            = std::max(m_tex->baseHeight >> miplevel, 1u);
    const size_t pitch_in_bytes = m_spec.pixel_bytes() * width;
    ktx_size_t offset;

    // Is this a Cubemap?
    if (!m_tex->isCubemap)
        return false;

    if (!check(subimage, miplevel)) {
        errorfmt("KTX read_native_scanlines: invalid subimage or miplevel");
        return false;
    }

    // Check provided ROI (with 1x6 layout => tiles arranged along height)
    if (xbegin != 0 || xend != width || ybegin < 0 || ybegin >= yend
        || ybegin % height || yend > (height * 6) || yend % height) {
        errorfmt(
            "KTX read_native_tiles: out of valid range tile(s) ROI. "
            "xbegin={} (should be 0), xend={} (should be {}), ybegin={} (% {} should be 0), yend={} (% {} should be 0)",
            xbegin, xend, width, ybegin, height, yend, height);
        return false;
    }

    // Can the provided span hold the requested scanlines?
    // This only accesses nchannels of the m_spec, so this is thread safe.
    if (!valid_raw_span_size(data, m_spec, xbegin, xend, ybegin, yend))
        return false;  // errorfmt is set within valid_raw_span_size

    if (const size_t libktx_image_size = ktxTexture2_GetImageSize(m_tex.get(),
                                                                  miplevel),
        expected_image_size            = pitch_in_bytes * height;
        libktx_image_size != expected_image_size) {
        errorfmt("Expected image size of {} but libktx reported {}",
                 expected_image_size, libktx_image_size);
        return false;
    }

    // Check that our pitch (from spec) is equal to the one reported by libktx
    // TODO: this cast is probably UB. Wait for my suggestion to libktx to add
    // ktxTexture2_GetRowPitch that simply takes a ktxTexture2*
    if (auto libktx_pitch_in_bytes
        = ktxTexture_GetRowPitch((ktxTexture*)m_tex.get(), miplevel);
        pitch_in_bytes != libktx_pitch_in_bytes) {
        errorfmt("Expected a pitch of size {} but libktx expects {}",
                 pitch_in_bytes, libktx_pitch_in_bytes);
        return false;
    }

    for (int start_face_idx = ybegin / height, face_idx = start_face_idx;
         face_idx < (yend / height); ++face_idx) {
        //
        // GetImageOffset implements internal checks depending on texture kind
        // (e.g., 3D, cubemap, etc.) and in case of invalid input,
        // KTX_INVALID_OPERATION is returned.
        //
        if (auto status = ktxTexture2_GetImageOffset(m_tex.get(), miplevel,
                                                     subimage, face_idx,
                                                     &offset);
            status != KTX_SUCCESS) {
            errorfmt("ktxTexture2_GetImageOffset failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        size_t tile_size_in_bytes = pitch_in_bytes * height;
        auto src_ptr              = m_tex->pData + offset;
        auto dst_ptr = data.data()
                       + size_t(face_idx - start_face_idx) * tile_size_in_bytes;
        memcpy(dst_ptr, src_ptr, tile_size_in_bytes);
    }
    DBG std::cout << fmt::format(
        "[ktxinput] read_native_tiles(subimage={},miplevel={},xbegin={},xend={},ybegin={},yend={})\n",
        subimage, miplevel, xbegin, xend, ybegin, yend);
    return true;
}



bool
KtxInput::read_native_volumetric_tiles(int subimage, int miplevel, int xbegin,
                                       int xend, int ybegin, int yend,
                                       int zbegin, int zend,
                                       span<std::byte> data)
{
    const int width             = std::max(m_tex->baseWidth >> miplevel, 1u);
    const int height            = std::max(m_tex->baseHeight >> miplevel, 1u);
    const int depth             = std::max(m_tex->baseDepth >> miplevel, 1u);
    const size_t pitch_in_bytes = m_spec.pixel_bytes() * width;
    ktx_size_t offset;

    // Is this a 3D volume texture?
    if (m_tex->numDimensions != 3)
        return false;

    if (!check(subimage, miplevel)) {
        errorfmt(
            "KTX read_native_volumetric_tiles: out of valid range tile(s) ROI");
        return false;
    }

    if (xbegin != 0 || xend != width) {
        errorfmt("KTX read_native_volumetric_tiles: for 3d volumes tiles are "
                 "reported as scanlines. xbegin must be 0 and xend must be the "
                 "width");
        return false;
    }

    if (ybegin < 0 || ybegin >= yend || yend > height || zbegin < 0
        || zbegin >= zend || zend > depth) {
        errorfmt(
            "KTX read_native_volumetric_tiles: out of valid range scanline indices");
        return false;
    }

    // Can the provided span hold the requested scanlines?
    // This only accesses nchannels of the m_spec, so this is thread safe.
    if (!valid_raw_span_size(data, m_spec, xbegin, xend, ybegin, yend, zbegin,
                             zend))
        return false;  // errorfmt is set within valid_raw_span_size

    if (const size_t libktx_image_size = ktxTexture2_GetImageSize(m_tex.get(),
                                                                  miplevel),
        expected_image_size            = pitch_in_bytes * height;
        libktx_image_size != expected_image_size) {
        errorfmt("Expected image size of {} but libktx reported {}",
                 expected_image_size, libktx_image_size);
        return false;
    }

    // Check that our pitch (from spec) is equal to the one reported by libktx
    if (auto libktx_pitch_in_bytes
        = ktxTexture_GetRowPitch((ktxTexture*)m_tex.get(), miplevel);
        pitch_in_bytes != libktx_pitch_in_bytes) {
        errorfmt("Expected a pitch of size {} but libktx expects {}",
                 pitch_in_bytes, libktx_pitch_in_bytes);
        return false;
    }

    for (int slice_idx = zbegin; slice_idx < zend; ++slice_idx) {
        if (auto status = ktxTexture2_GetImageOffset(m_tex.get(), miplevel,
                                                     subimage, slice_idx,
                                                     &offset);
            status != KTX_SUCCESS) {
            errorfmt("ktxTexture2_GetImageOffset failed. Reason: {}",
                     ktxErrorString(status));
            return false;
        }
        size_t tile_size_in_bytes = size_t(yend - ybegin) * pitch_in_bytes;
        auto src_ptr              = m_tex->pData + offset
                                    + ktx_size_t(ybegin) * pitch_in_bytes;
        auto dst_ptr = data.data()
                       + size_t(slice_idx - zbegin) * tile_size_in_bytes;
        memcpy(dst_ptr, src_ptr, tile_size_in_bytes);
    }
    DBG std::cout << fmt::format(
        "[ktxinput] read_native_volumetric_tiles(subimage={},miplevel={},"
        "xbegin={},xend={},ybegin={},yend={},zbegin={},zend={})\n",
        subimage, miplevel, xbegin, xend, ybegin, yend, zbegin, zend);
    return true;
}



bool
OpenImageIO::KtxInput::valid_file(Filesystem::IOProxy* ioproxy) const
{
    // Check magic number to assure this is a KTX2 file
    if (!ioproxy || ioproxy->mode() != Filesystem::IOProxy::Read)
        return false;

    // per KTX2 specs: the first 12 bytes of a KTX2 file are used to identify it
    uint8_t magic[12] {};
    const size_t numRead = ioproxy->pread(magic, sizeof(magic), 0);

    return (numRead == sizeof(magic))
           && this->ktx_magic_cmp(make_cspan(magic, numRead));
}



bool
KtxInput::ktx_magic_cmp(cspan<uint8_t> sig) const noexcept
{
    // this is: "«KTX 20»\r\n\x1A\n"
    const uint8_t KTX2_IDENTIFIER[12] { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                        0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    if (sig.size() < sizeof(KTX2_IDENTIFIER))
        return false;
    for (size_t i = 0; i < sizeof(KTX2_IDENTIFIER); ++i)
        if (sig[i] != KTX2_IDENTIFIER[i])
            return false;
    return true;
}


//
// Returns the OIIO colorspace identification string from given KTX2 transfer
// function and primaries.
//
// Since KTX2 textures are usually intended to be used within rendering context,
// assume scene rather than display colorspaces.
//
// See:
//  https://github.com/KhronosGroup/KTX-Software/blob/main/external/dfdutils/KHR/khr_df.h
//  https://github.com/AcademySoftwareFoundation/OpenImageIO/blob/main/src/libOpenImageIO/color_ocio.cpp
//
std::string
KtxInput::get_colorspace() const noexcept
{
    //
    // for set of, see:
    //
    const auto tf        = ktxTexture2_GetTransferFunction_e(m_tex.get());
    const auto primaries = ktxTexture2_GetPrimaries_e(m_tex.get());

    DBG std::cout << "[ktxinput] tf: " << tf << "; primaries: " << primaries
                  << std::endl;

    // First try to find first matching OIIO scene colorspace
    for (const auto& [k, v] : color_interop_id_map) {
        if (v.tf == tf && v.primaries == primaries
            && Strutil::ends_with(k, "scene")) {
            return k;
        }
    }

    // Otherwise just pick the first matching OIIO display colorspace
    for (const auto& [k, v] : color_interop_id_map) {
        if (v.tf == tf && v.primaries == primaries) {
            return k;
        }
    }

    return "unknown";
}



///
/// Parses KTXwriterScParams metadata and sets relevant attributes accordingly.
/// E.g., if Basis Universal non-default params were found, they are set.
///
/// One cannot know from the KTX2 file itself the kind of parameters that were
/// used to create/encode it (e.g., RDO params).
///
/// This is only useful when reading a KTX2 via OIIO and re-writing it again
/// (which is a bad idea since each re-write cycle worsens the quality). That
/// being said, if the user intends to use OIIO this way, the written KTX2 file
/// should be as similar as possible to the given input (assuming a read-write
/// of a KTX2 file without any change to its data).
///
/// Note:
///   std::from_chars fails on MacOS because it still apparently doesn't support
///   floating point values ...
///
void
KtxInput::parse_ktx_sc_params_metadata(const std::string& ktx_sc_params)
{
    const auto f = std::regex_constants::icase;

    // Formatting is turned off because having single-statement ifs on multiple
    // lines is really hard to follow...
    // clang-format off
    {  // BasisLZ/ETC1S params (see KTX-Software/tools/ktx/encode_utils_basis.h)
        std::regex etc1s_clevel("--clevel\\s+(\\d+)", f);
        std::regex etc1s_qlevel("--qlevel\\s+(\\d+)", f);
        std::regex etc1s_max_endpoints("--max-endpoints\\s+(\\d+)", f);
        std::regex etc1s_endpoint_rdo_threshold("--endpoint-rdo-threshold\\s+((\\d*[.])?\\d+)", f);
        std::regex etc1s_max_selectors("--max-selectors\\s+(\\d+)", f);
        std::regex etc1s_selector_rdo_threshold("--selector-rdo-threshold\\s+((\\d*[.])?\\d+)", f);

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_clevel) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_compression_level", Strutil::stoui(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_qlevel) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_quality_level", Strutil::stoui(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_max_endpoints) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_max_endpoints", Strutil::stoui(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_endpoint_rdo_threshold) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_endpoint_rdo_threshold", Strutil::stof(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_max_selectors) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_max_selectors", Strutil::stoui(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, etc1s_selector_rdo_threshold) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:etc1s_selector_rdo_threshold", Strutil::stof(m[1].str()));

        if (Strutil::icontains(ktx_sc_params, "--no-endpoint-rdo")) m_spec.extra_attribs.attribute("ktx:etc1s_no_endpoint_rdo", true);
        if (Strutil::icontains(ktx_sc_params, "--no-selector-rdo")) m_spec.extra_attribs.attribute("ktx:etc1s_no_selector_rdo", true);
    }

    {  // UASTC params (see KTX-Software/tools/ktx/encode_utils_basis.h)
        std::regex uastc_quality_re("--uastc-quality\\s+(\\d+)", f);
        std::regex uastc_rdo_l_re("--uastc-rdo-l\\s+((\\d*[.])?\\d+)", f);
        std::regex uastc_rdo_d_re("--uastc-rdo-d\\s+(\\d+)", f);
        std::regex uastc_rdo_b_re("--uastc-rdo-b\\s+((\\d*[.])?\\d+)", f);
        std::regex uastc_rdo_s_re("--uastc-rdo-s\\s+((\\d*[.])?\\d+)", f);
        std::regex uastc_hdr_lambda_re("--uastc-hdr-lambda\\s+((\\d*[.])?\\d+)", f);
        std::regex uastc_hdr_6x6i_level_re("--uastc-hdr-6x6i-level\\s+(\\d+)", f);

        // KTX Tools sets uses `--uastc-quality` to set the quality for UASTC LDR/HDR.
        // These are NOT UASTC quality flags but rather a quality setting with the valid range [0, 4]
        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_quality_re) && m.size() == 2) {
            // KTX Tools users may set `--uastc-quality` to something higher than 4 hence why we clamp it here
            // Least significant 4 bits are for the level (e.g., fastest, slowest, etc.)
            // These are the most important bits. The ramaining bits are for advanced codec quality options and cannot
            // be set by KTX Tools' `--uastc-quality` option. This is why we set the remaining bits to 0 (default).
            const auto uastc_quality_level = std::clamp<uint32_t>(Strutil::stoui(m[1].str()), 0, KTX_PACK_UASTC_MAX_LEVEL);
            m_spec.extra_attribs.attribute("ktx:uastc_flags", uastc_quality_level);
            m_spec.extra_attribs.attribute("ktx:uastc_hdr_quality", uastc_quality_level);
        }

        if (Strutil::icontains(ktx_sc_params, "--uastc-rdo")) m_spec.extra_attribs.attribute("ktx:uastc_rdo", true);

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_rdo_l_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_rdo_quality_scalar", Strutil::stof(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_rdo_d_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_rdo_dict_size", Strutil::stoui(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_rdo_b_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_rdo_max_smooth_block_error_scale", Strutil::stof(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_rdo_s_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_rdo_max_smooth_block_std_dev", Strutil::stof(m[1].str()));

        if (Strutil::icontains(ktx_sc_params, "--uastc-rdo-f")) m_spec.extra_attribs.attribute("ktx:uastc_rdo_dont_favor_simpler_modes", true);
        if (Strutil::icontains(ktx_sc_params, "--uastc-rdo-m")) m_spec.extra_attribs.attribute("ktx:uastc_rdo_no_multithreading", true);
        if (Strutil::icontains(ktx_sc_params, "--uastc-hdr-uber-mode")) m_spec.extra_attribs.attribute("ktx:uastc_hdr_uber_mode", true);
        if (Strutil::icontains(ktx_sc_params, "--uastc-hdr-ultra-quant")) m_spec.extra_attribs.attribute("ktx:uastc_hdr_ultra_quant", true);
        if (Strutil::icontains(ktx_sc_params, "--uastc-hdr-favor-astc")) m_spec.extra_attribs.attribute("ktx:uastc_hdr_favor_astc", true);

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_hdr_lambda_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_hdr_lambda", Strutil::stof(m[1].str()));

        if (std::smatch m; std::regex_search(ktx_sc_params, m, uastc_hdr_6x6i_level_re) && m.size() == 2)
            m_spec.extra_attribs.attribute("ktx:uastc_hdr_level", Strutil::stoui(m[1].str()));
    }

    {  // Common params (see KTX-Software/tools/ktx/encode_utils_common.h)
        std::regex zlib_level_re("--zlib\\s+(\\d+)", f);
        std::regex zstd_level_re("--zstd\\s+(\\d+)", f);

        // If the input texture's super-compression scheme and this metadata disagree then just ignore this metadata.
        if (std::smatch m; std::regex_search(ktx_sc_params, m, zlib_level_re) && m.size() == 2
            && Strutil::iequals(m_spec.get_string_attribute("compression"), "zip"))
            m_spec.attribute("compression", fmt::format("zip:{}", Strutil::stoui(m[1].str())));

        // If the input texture's super-compression scheme and this metadata disagree then just ignore this metadata.
        if (std::smatch m; std::regex_search(ktx_sc_params, m, zstd_level_re) && m.size() == 2
            && Strutil::iequals(m_spec.get_string_attribute("compression"), "zstd"))
            m_spec.attribute("compression", fmt::format("zstd:{}", Strutil::stoui(m[1].str())));

        if (Strutil::icontains(ktx_sc_params, "--normal-mode")) m_spec.extra_attribs.attribute("ktx:normalmap", true);
        if (Strutil::icontains(ktx_sc_params, "--no-sse")) m_spec.extra_attribs.attribute("ktx:no_sse", true);
    }
    // clang-format on

    // preSwizzle and inputSwizzle are not exposed in ktx tools
}

OIIO_PLUGIN_NAMESPACE_END
