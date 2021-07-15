// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


//
// General information about PSD:
// https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
//


#include <csetjmp>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/tiffutils.h>

#include "jpeg_memory_src.h"
#include "psd_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace psd_pvt;


class PSDInput final : public ImageInput {
public:
    PSDInput();
    virtual ~PSDInput() { close(); }
    virtual const char* format_name(void) const override { return "psd"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "exif" || feature == "iptc"
                || feature == "thumbnail");
    }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual int current_subimage() const override { return m_subimage; }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool get_thumbnail(ImageBuf& thumb, int subimage) override
    {
        thumb = m_thumbnail;
        return m_thumbnail.initialized();
    }

private:
    enum ColorMode {
        ColorMode_Bitmap       = 0,
        ColorMode_Grayscale    = 1,
        ColorMode_Indexed      = 2,
        ColorMode_RGB          = 3,
        ColorMode_CMYK         = 4,
        ColorMode_Multichannel = 7,
        ColorMode_Duotone      = 8,
        ColorMode_Lab          = 9
    };

    enum Compression {
        Compression_Raw         = 0,
        Compression_RLE         = 1,
        Compression_ZIP         = 2,
        Compression_ZIP_Predict = 3
    };

    enum ChannelID {
        ChannelID_Transparency = -1,
        ChannelID_LayerMask    = -2,
        ChannelID_UserMask     = -3
    };

    // Image resource loaders to handle loading certain image resources
    // into ImageSpec
    struct ResourceLoader {
        uint16_t resource_id;
        std::function<bool(PSDInput*, uint32_t)> load;
    };

    // Map image resource ID to image resource block
    typedef std::map<uint16_t, ImageResourceBlock> ImageResourceMap;

    struct ResolutionInfo {
        float hRes;
        int16_t hResUnit;
        int16_t widthUnit;
        float vRes;
        int16_t vResUnit;
        int16_t heightUnit;

        enum ResolutionUnit { PixelsPerInch = 1, PixelsPerCentimeter = 2 };

        enum Unit {
            Inches      = 1,
            Centimeters = 2,
            Points      = 3,
            Picas       = 4,
            Columns     = 5
        };
    };

    struct LayerMaskInfo {
        uint64_t length;
        std::streampos begin;
        std::streampos end;

        struct LayerInfo {
            uint64_t length;
            int16_t layer_count;
            std::streampos begin;
            std::streampos end;
        };

        LayerInfo layer_info;
    };

    struct ChannelInfo {
        uint32_t row_length;
        int16_t channel_id;
        uint64_t data_length;
        std::streampos data_pos;
        uint16_t compression;
        std::vector<uint32_t> rle_lengths;
        std::vector<std::streampos> row_pos;
    };

    struct Layer {
        uint32_t top, left, bottom, right;
        uint32_t width, height;
        uint16_t channel_count;

        std::vector<ChannelInfo> channel_info;
        std::map<int16_t, ChannelInfo*> channel_id_map;

        char bm_key[4];
        uint8_t opacity;
        uint8_t clipping;
        uint8_t flags;
        uint32_t extra_length;

        struct MaskData {
            uint32_t top, left, bottom, right;
            uint8_t default_color;
            uint8_t flags;
        };

        MaskData mask_data;

        //TODO: layer blending ranges?

        std::string name;

        struct AdditionalInfo {
            char key[4];
            uint64_t length;
            std::streampos pos;
        };

        std::vector<AdditionalInfo> additional_info;
    };

    struct GlobalMaskInfo {
        uint16_t overlay_color_space;
        uint16_t color_components[4];
        uint16_t opacity;
        int8_t kind;
    };

    struct ImageDataSection {
        std::vector<ChannelInfo> channel_info;
        //When the layer count is negative, this is true and indicates that
        //the first alpha channel should be used as transparency (for the
        //merged image)
        bool transparency;
    };

    std::string m_filename;
    OIIO::ifstream m_file;
    //Current subimage
    int m_subimage;
    //Subimage count (1 + layer count)
    int m_subimage_count;
    std::vector<ImageSpec> m_specs;
    static const ResourceLoader resource_loaders[];
    //This holds the attributes for the merged image (subimage 0)
    ImageSpec m_composite_attribs;
    //This holds common attributes that apply to all subimages
    ImageSpec m_common_attribs;
    //psd:RawData config option, indicates that the user wants the raw,
    //unconverted channel data
    bool m_WantRaw;
    TypeDesc m_type_desc;
    //This holds all the ChannelInfos for all subimages
    //Example: m_channels[subimg][channel]
    std::vector<std::vector<ChannelInfo*>> m_channels;
    //Alpha Channel Names, not currently used
    std::vector<std::string> m_alpha_names;
    //Buffers for channel data
    std::vector<std::string> m_channel_buffers;
    //Buffer for RLE conversion
    std::string m_rle_buffer;
    //Index of the transparent color, if any (for Indexed color mode only)
    int16_t m_transparency_index;
    //Background color
    double m_background_color[4];
    ///< Do not convert unassociated alpha
    bool m_keep_unassociated_alpha;


    FileHeader m_header;
    ColorModeData m_color_data;
    LayerMaskInfo m_layer_mask_info;
    std::vector<Layer> m_layers;
    GlobalMaskInfo m_global_mask_info;
    ImageDataSection m_image_data;
    ImageBuf m_thumbnail;

    //Reset to initial state
    void init();

    //File Header
    bool load_header();
    bool read_header();
    bool validate_header();

    //Color Mode Data
    bool load_color_data();
    bool validate_color_data();

    //Image Resources
    bool load_resources();
    bool read_resource(ImageResourceBlock& block);
    bool validate_resource(ImageResourceBlock& block);
    //Call the resource_loaders to load the resources into an ImageSpec
    //m_specs should be resized to m_subimage_count first
    bool handle_resources(ImageResourceMap& resources);
    //ResolutionInfo
    bool load_resource_1005(uint32_t length);
    //Alpha Channel Names
    bool load_resource_1006(uint32_t length);
    //Background Color
    bool load_resource_1010(uint32_t length);
    //JPEG thumbnail (Photoshop 4.0)
    bool load_resource_1033(uint32_t length);
    //JPEG thumbnail (Photoshop 5.0)
    bool load_resource_1036(uint32_t length);
    // ICC Profile (Photoshop 5.0)
    bool load_resource_1039(uint32_t length);
    //Transparency index (Indexed color mode)
    bool load_resource_1047(uint32_t length);
    //Exif data 1
    bool load_resource_1058(uint32_t length);
    //Exif data 3
    bool load_resource_1059(uint32_t length);
    //XMP metadata
    bool load_resource_1060(uint32_t length);
    //Pixel Aspect Ratio
    bool load_resource_1064(uint32_t length);

    //Load thumbnail resource, used for resources 1033 and 1036
    bool load_resource_thumbnail(uint32_t length, bool isBGR);
    //For thumbnail loading
    struct thumbnail_error_mgr {
        jpeg_error_mgr pub;
        jmp_buf setjmp_buffer;
    };
    METHODDEF(void)
    thumbnail_error_exit(j_common_ptr cinfo);

    //Layers
    bool load_layers();
    bool load_layer(Layer& layer);
    bool load_layer_channels(Layer& layer);
    bool load_layer_channel(Layer& layer, ChannelInfo& channel_info);
    bool read_rle_lengths(uint32_t height, std::vector<uint32_t>& rle_lengths);

    //Global Mask Info
    bool load_global_mask_info();

    //Global Additional Layer Info
    bool load_global_additional();

    //Image Data Section
    bool load_image_data();

    void set_type_desc();
    //Setup m_specs and m_channels
    void setup();
    void fill_channel_names(ImageSpec& spec, bool transparency);

    //Read a row of channel data
    bool read_channel_row(const ChannelInfo& channel_info, uint32_t row,
                          char* data);

    // Interleave channels (RRRGGGBBB -> RGBRGBRGB) while copying from
    // m_channel_buffers[0..nchans-1] to dst.
    template<typename T> void interleave_row(T* dst, size_t nchans);

    //Convert the channel data to RGB
    bool indexed_to_rgb(char* dst);
    bool bitmap_to_rgb(char* dst);

    // Convert from photoshop native alpha to
    // associated/premultiplied
    template<class T>
    void removeBackground(T* data, int size, int nchannels, int alpha_channel,
                          double* background)
    {
        // RGB = CompRGB - (1 - alpha) * Background;
        double scale = std::numeric_limits<T>::is_integer
                           ? 1.0 / double(std::numeric_limits<T>::max())
                           : 1.0;

        for (; size; --size, data += nchannels)
            for (int c = 0; c < nchannels; c++)
                if (c != alpha_channel) {
                    double alpha = data[alpha_channel] * scale;
                    double f     = data[c];

                    data[c] = T(f - (((1.0 - alpha) * background[c]) / scale));
                }
    }

    template<class T>
    void unassociateAlpha(T* data, int size, int nchannels, int alpha_channel,
                          double* background)
    {
        // RGB = (CompRGB - (1 - alpha) * Background) / alpha
        double scale = std::numeric_limits<T>::is_integer
                           ? 1.0 / double(std::numeric_limits<T>::max())
                           : 1.0;

        for (; size; --size, data += nchannels)
            for (int c = 0; c < nchannels; c++)
                if (c != alpha_channel) {
                    double alpha = data[alpha_channel] * scale;
                    double f     = data[c];

                    if (alpha > 0.0)
                        data[c] = T(
                            (f - (((1.0 - alpha) * background[c]) / scale))
                            / alpha);
                    else
                        data[c] = 0;
                }
    }

    template<class T>
    void associateAlpha(T* data, int size, int nchannels, int alpha_channel)
    {
        double scale = std::numeric_limits<T>::is_integer
                           ? 1.0 / double(std::numeric_limits<T>::max())
                           : 1.0;
        for (; size; --size, data += nchannels)
            for (int c = 0; c < nchannels; c++)
                if (c != alpha_channel) {
                    double f = data[c];
                    data[c]  = T(f * (data[alpha_channel] * scale));
                }
    }

    void background_to_assocalpha(int n, void* data);
    void background_to_unassalpha(int n, void* data);
    void unassalpha_to_assocalpha(int n, void* data);

    template<typename T>
    void cmyk_to_rgb(int n, const T* cmyk, size_t cmyk_stride, T* rgb,
                     size_t rgb_stride)
    {
        for (; n; --n, cmyk += cmyk_stride, rgb += rgb_stride) {
            float C = convert_type<T, float>(cmyk[0]);
            float M = convert_type<T, float>(cmyk[1]);
            float Y = convert_type<T, float>(cmyk[2]);
            float K = convert_type<T, float>(cmyk[3]);
#if 0
            // WHY doesn't this work if it's cmyk?
            float R = (1.0f - C) * (1.0f - K);
            float G = (1.0f - M) * (1.0f - K);
            float B = (1.0f - Y) * (1.0f - K);
#else
            // But this gives the right results????? WTF?
            // Is it because it's subtractive and PhotoShop records it
            // as MAX-val?
            float R = C * (K);
            float G = M * (K);
            float B = Y * (K);
#endif
            rgb[0] = convert_type<float, T>(R);
            rgb[1] = convert_type<float, T>(G);
            rgb[2] = convert_type<float, T>(B);
        }
    }

    //Check if m_file is good. If not, set error message and return false.
    bool check_io();

    //This may be a bit inefficient but I think it's worth the convenience.
    //This takes care of things like reading a 32-bit BE into a 64-bit LE.
    template<typename TStorage, typename TVariable>
    bool read_bige(TVariable& value)
    {
        TStorage buffer;
        m_file.read((char*)&buffer, sizeof(buffer));
        if (!bigendian())
            swap_endian(&buffer);
        value = buffer;
        return m_file.good();
    }

    int read_pascal_string(std::string& s, uint16_t mod_padding);

    bool decompress_packbits(const char* src, char* dst, uint16_t packed_length,
                             uint16_t unpacked_length);

    // These are AdditionalInfo entries that, for PSBs, have an 8-byte length
    static const char* additional_info_psb[];
    static const unsigned int additional_info_psb_count;
    bool is_additional_info_psb(const char* key);

    // Channel names and counts for each color mode
    static const char* mode_channel_names[][4];
    static const unsigned int mode_channel_count[];

    // Some attributes may apply to only the merged composite.
    // Others may apply to all subimages.
    // These functions are intended to be used by image resource loaders.
    //
    // Add an attribute to the composite image spec
    template<typename T>
    void composite_attribute(const std::string& name, const T& value)
    {
        m_composite_attribs.attribute(name, value);
    }

    // Add an attribute to the composite image spec
    template<typename T>
    void composite_attribute(const std::string& name, const TypeDesc& type,
                             const T& value)
    {
        m_composite_attribs.attribute(name, type, value);
    }

    // Add an attribute to the composite image spec and common image spec
    template<typename T>
    void common_attribute(const std::string& name, const T& value)
    {
        m_composite_attribs.attribute(name, value);
        m_common_attribs.attribute(name, value);
    }

    // Add an attribute to the composite image spec and common image spec
    template<typename T>
    void common_attribute(const std::string& name, const TypeDesc& type,
                          const T& value)
    {
        m_composite_attribs.attribute(name, type, value);
        m_common_attribs.attribute(name, type, value);
    }
};



// Image resource loaders
// To add an image resource loader, do the following:
// 1) Add ADD_LOADER(<ResourceID>) below
// 2) Add a method in PSDInput:
//    bool load_resource_<ResourceID> (uint32_t length);
#define ADD_LOADER(id)                                                      \
    {                                                                       \
        id, std::bind(&PSDInput::load_resource_##id, std::placeholders::_1, \
                      std::placeholders::_2)                                \
    }
const PSDInput::ResourceLoader PSDInput::resource_loaders[]
    = { ADD_LOADER(1005), ADD_LOADER(1006), ADD_LOADER(1010), ADD_LOADER(1033),
        ADD_LOADER(1036), ADD_LOADER(1039), ADD_LOADER(1047), ADD_LOADER(1058),
        ADD_LOADER(1059), ADD_LOADER(1060), ADD_LOADER(1064) };
#undef ADD_LOADER



const char* PSDInput::additional_info_psb[]
    = { "LMsk", "Lr16", "Lr32", "Layr", "Mt16", "Mt32", "Mtrn",
        "Alph", "FMsk", "Ink2", "FEid", "FXid", "PxSD", "cinf" };

const unsigned int PSDInput::additional_info_psb_count
    = sizeof(additional_info_psb) / sizeof(additional_info_psb[0]);

const char* PSDInput::mode_channel_names[][4] = {
    { "A" }, { "I" }, { "I" }, { "R", "G", "B" }, { "C", "M", "Y", "K" }, {},
    {},      {},      {},      { "L", "a", "b" }
};

const unsigned int PSDInput::mode_channel_count[] = { 1, 1, 1, 3, 4,
                                                      0, 0, 0, 0, 3 };



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
psd_input_imageio_create()
{
    return new PSDInput;
}

OIIO_EXPORT int psd_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
psd_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT const char* psd_input_extensions[] = { "psd", "pdd", "psb",
                                                   nullptr };

OIIO_PLUGIN_EXPORTS_END



PSDInput::PSDInput() { init(); }



bool
PSDInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    Filesystem::open(m_file, name, std::ios::binary);

    if (!m_file) {
        errorf("\"%s\": failed to open file", name);
        return false;
    }

    // File Header
    if (!load_header()) {
        errorf("failed to open \"%s\": failed load_header", name);
        return false;
    }

    // Color Mode Data
    if (!load_color_data()) {
        errorf("failed to open \"%s\": failed load_color_data", name);
        return false;
    }

    // Image Resources
    if (!load_resources()) {
        errorf("failed to open \"%s\": failed load_resources", name);
        return false;
    }

    // Layers
    if (!load_layers()) {
        errorf("failed to open \"%s\": failed load_layers", name);
        return false;
    }

    // Global Mask Info
    if (!load_global_mask_info()) {
        errorf("failed to open \"%s\": failed load_global_mask_info", name);
        return false;
    }

    // Global Additional Layer Info
    if (!load_global_additional()) {
        errorf("failed to open \"%s\": failed load_global_additional", name);
        return false;
    }

    // Image Data
    if (!load_image_data()) {
        errorf("failed to open \"%s\": failed load_image_data", name);
        return false;
    }

    // Layer count + 1 for merged composite (Image Data Section)
    m_subimage_count = m_layers.size() + 1;
    // Set m_type_desc to the appropriate TypeDesc
    set_type_desc();
    // Setup ImageSpecs and m_channels
    setup();

    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
PSDInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    m_WantRaw = config.get_int_attribute("psd:RawData")
                || config.get_int_attribute("oiio:RawColor");

    if (config.get_int_attribute("oiio:UnassociatedAlpha", 0) == 1)
        m_keep_unassociated_alpha = true;

    return open(name, newspec);
}



bool
PSDInput::close()
{
    init();
    return true;
}



bool
PSDInput::seek_subimage(int subimage, int miplevel)
{
    if (miplevel != 0)
        return false;
    if (subimage == m_subimage)
        return true;  // Early return when not changing subimages
    if (subimage < 0 || subimage >= m_subimage_count)
        return false;

    m_subimage = subimage;
    m_spec     = m_specs[subimage];
    return true;
}



void
PSDInput::background_to_assocalpha(int n, void* data)
{
    switch (m_spec.format.basetype) {
    case TypeDesc::UINT8:
        removeBackground((unsigned char*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::UINT16:
        removeBackground((unsigned short*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::UINT32:
        removeBackground((unsigned long*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::FLOAT:
        removeBackground((float*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    default: break;
    }
}



void
PSDInput::background_to_unassalpha(int n, void* data)
{
    switch (m_spec.format.basetype) {
    case TypeDesc::UINT8:
        unassociateAlpha((unsigned char*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::UINT16:
        unassociateAlpha((unsigned short*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::UINT32:
        unassociateAlpha((unsigned long*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    case TypeDesc::FLOAT:
        unassociateAlpha((float*)data, n, m_spec.nchannels,
                         m_spec.alpha_channel, m_background_color);
        break;
    default: break;
    }
}



void
PSDInput::unassalpha_to_assocalpha(int n, void* data)
{
    switch (m_spec.format.basetype) {
    case TypeDesc::UINT8:
        associateAlpha((unsigned char*)data, n, m_spec.nchannels,
                       m_spec.alpha_channel);
        break;
    case TypeDesc::UINT16:
        associateAlpha((unsigned short*)data, n, m_spec.nchannels,
                       m_spec.alpha_channel);
        break;
    case TypeDesc::UINT32:
        associateAlpha((unsigned long*)data, n, m_spec.nchannels,
                       m_spec.alpha_channel);
        break;
    case TypeDesc::FLOAT:
        associateAlpha((float*)data, n, m_spec.nchannels, m_spec.alpha_channel);
        break;
    default: break;
    }
}



bool
PSDInput::read_native_scanline(int subimage, int miplevel, int y, int /*z*/,
                               void* data)
{
    lock_guard lock(*this);
    if (!seek_subimage(subimage, miplevel))
        return false;

    y -= m_spec.y;
    if (y < 0 || y > m_spec.height)
        return false;

    if (m_channel_buffers.size() < m_channels[m_subimage].size())
        m_channel_buffers.resize(m_channels[m_subimage].size());

    int bps = (m_header.depth + 7) / 8;  // bytes per sample
    OIIO_DASSERT(bps == 1 || bps == 2 || bps == 4);
    std::vector<ChannelInfo*>& channels = m_channels[m_subimage];
    int channel_count                   = (int)channels.size();
    for (int c = 0; c < channel_count; ++c) {
        std::string& buffer       = m_channel_buffers[c];
        ChannelInfo& channel_info = *channels[c];
        if (buffer.size() < channel_info.row_length)
            buffer.resize(channel_info.row_length);

        if (!read_channel_row(channel_info, y, &buffer[0]))
            return false;
    }
    char* dst = (char*)data;
    if (m_WantRaw || m_header.color_mode == ColorMode_RGB
        || m_header.color_mode == ColorMode_Multichannel
        || m_header.color_mode == ColorMode_Grayscale) {
        switch (bps) {
        case 4:
            interleave_row((float*)dst, m_channels[m_subimage].size());
            break;
        case 2:
            interleave_row((unsigned short*)dst, m_channels[m_subimage].size());
            break;
        default:
            interleave_row((unsigned char*)dst, m_channels[m_subimage].size());
            break;
        }
    } else if (m_header.color_mode == ColorMode_CMYK) {
        switch (bps) {
        case 4: {
            std::unique_ptr<float[]> cmyk(new float[4 * m_spec.width]);
            interleave_row(cmyk.get(), 4);
            cmyk_to_rgb(m_spec.width, cmyk.get(), 4, (float*)dst,
                        m_spec.nchannels);
            break;
        }
        case 2: {
            std::unique_ptr<unsigned short[]> cmyk(
                new unsigned short[4 * m_spec.width]);
            interleave_row(cmyk.get(), 4);
            cmyk_to_rgb(m_spec.width, cmyk.get(), 4, (unsigned short*)dst,
                        m_spec.nchannels);
            break;
        }
        default: {
            std::unique_ptr<unsigned char[]> cmyk(
                new unsigned char[4 * m_spec.width]);
            interleave_row(cmyk.get(), 4);
            cmyk_to_rgb(m_spec.width, cmyk.get(), 4, (unsigned char*)dst,
                        m_spec.nchannels);
            break;
        }
        }
    } else if (m_header.color_mode == ColorMode_Indexed) {
        if (!indexed_to_rgb(dst))
            return false;
    } else if (m_header.color_mode == ColorMode_Bitmap) {
        if (!bitmap_to_rgb(dst))
            return false;
    } else {
        OIIO_ASSERT(0 && "unknown color mode");
        return false;
    }

    // PSD specifically dictates unassociated (un-"premultiplied") alpha.
    // Convert to associated unless we were requested not to do so.
    //
    // Composite layer (subimage 0) is mixed with background, which
    // affects the alpha (aka white borders if background not removed).
    //
    // Composite:
    // m_keep_unassociated_alpha true: remove background and convert to unassociated
    // m_keep_unassociated_alpha false: remove background only
    //
    // Other Layers:
    // m_keep_unassociated_alpha true: do nothing
    // m_keep_unassociated_alpha false: convert to associated
    //
    //
    if (m_spec.alpha_channel != -1) {
        if (m_subimage == 0) {
            if (m_keep_unassociated_alpha) {
                background_to_unassalpha(m_spec.width, data);
            } else {
                background_to_assocalpha(m_spec.width, data);
            }
        } else {
            if (m_keep_unassociated_alpha) {
                // do nothing - leave as it is
            } else {
                unassalpha_to_assocalpha(m_spec.width, data);
            }
        }
    }

    return true;
#undef DEB
}



void
PSDInput::init()
{
    m_filename.clear();
    m_file.close();
    m_subimage       = -1;
    m_subimage_count = 0;
    m_specs.clear();
    m_WantRaw = false;
    m_layers.clear();
    m_image_data.channel_info.clear();
    m_image_data.transparency = false;
    m_channels.clear();
    m_alpha_names.clear();
    m_channel_buffers.clear();
    m_rle_buffer.clear();
    m_transparency_index      = -1;
    m_keep_unassociated_alpha = false;
    m_background_color[0]     = 1.0;
    m_background_color[1]     = 1.0;
    m_background_color[2]     = 1.0;
    m_background_color[3]     = 1.0;
    m_thumbnail.clear();
}



bool
PSDInput::load_header()
{
    if (!read_header() || !validate_header())
        return false;

    return true;
}



bool
PSDInput::read_header()
{
    m_file.read(m_header.signature, 4);
    read_bige<uint16_t>(m_header.version);
    m_file.seekg(6, std::ios::cur);
    read_bige<uint16_t>(m_header.channel_count);
    read_bige<uint32_t>(m_header.height);
    read_bige<uint32_t>(m_header.width);
    read_bige<uint16_t>(m_header.depth);
    read_bige<uint16_t>(m_header.color_mode);
    return check_io();
}



bool
PSDInput::validate_header()
{
    if (std::memcmp(m_header.signature, "8BPS", 4) != 0) {
        errorf("[Header] invalid signature");
        return false;
    }
    if (m_header.version != 1 && m_header.version != 2) {
        errorf("[Header] invalid version");
        return false;
    }
    if (m_header.channel_count < 1 || m_header.channel_count > 56) {
        errorf("[Header] invalid channel count");
        return false;
    }
    switch (m_header.version) {
    case 1:
        // PSD
        // width/height range: [1,30000]
        if (m_header.height < 1 || m_header.height > 30000) {
            errorf("[Header] invalid image height");
            return false;
        }
        if (m_header.width < 1 || m_header.width > 30000) {
            errorf("[Header] invalid image width");
            return false;
        }
        break;
    case 2:
        // PSB (Large Document Format)
        // width/height range: [1,300000]
        if (m_header.height < 1 || m_header.height > 300000) {
            errorf("[Header] invalid image height");
            return false;
        }
        if (m_header.width < 1 || m_header.width > 300000) {
            errorf("[Header] invalid image width");
            return false;
        }
        break;
    }
    // Valid depths are 1,8,16,32
    if (m_header.depth != 1 && m_header.depth != 8 && m_header.depth != 16
        && m_header.depth != 32) {
        errorf("[Header] invalid depth");
        return false;
    }
    if (m_WantRaw)
        return true;

    //There are other (undocumented) color modes not listed here
    switch (m_header.color_mode) {
    case ColorMode_Bitmap:
    case ColorMode_Indexed:
    case ColorMode_RGB:
    case ColorMode_Grayscale:
    case ColorMode_CMYK:
    case ColorMode_Multichannel: break;
    case ColorMode_Duotone:
    case ColorMode_Lab: errorf("[Header] unsupported color mode"); return false;
    default: errorf("[Header] unrecognized color mode"); return false;
    }
    return true;
}



bool
PSDInput::load_color_data()
{
    read_bige<uint32_t>(m_color_data.length);
    if (!check_io())
        return false;

    if (!validate_color_data())
        return false;

    if (m_color_data.length) {
        m_color_data.data.resize(m_color_data.length);
        m_file.read(&m_color_data.data[0], m_color_data.length);
    }
    return check_io();
}



bool
PSDInput::validate_color_data()
{
    if (m_header.color_mode == ColorMode_Duotone && m_color_data.length == 0) {
        errorf(
            "[Color Mode Data] color mode data should be present for duotone image");
        return false;
    }
    if (m_header.color_mode == ColorMode_Indexed
        && m_color_data.length != 768) {
        errorf("[Color Mode Data] length should be 768 for indexed color mode");
        return false;
    }
    return true;
}



bool
PSDInput::load_resources()
{
    uint32_t length;
    read_bige<uint32_t>(length);

    if (!check_io())
        return false;

    ImageResourceBlock block;
    ImageResourceMap resources;
    std::streampos begin = m_file.tellg();
    std::streampos end   = begin + (std::streampos)length;
    while (m_file && m_file.tellg() < end) {
        if (!read_resource(block) || !validate_resource(block))
            return false;

        resources.insert(std::make_pair(block.id, block));
    }
    if (!check_io())
        return false;

    if (!handle_resources(resources))
        return false;

    m_file.seekg(end);
    return check_io();
}



bool
PSDInput::read_resource(ImageResourceBlock& block)
{
    m_file.read(block.signature, 4);
    read_bige<uint16_t>(block.id);
    read_pascal_string(block.name, 2);
    read_bige<uint32_t>(block.length);
    // Save the file position of the image resource data
    block.pos = m_file.tellg();
    // Skip the image resource data
    m_file.seekg(block.length, std::ios::cur);
    // Image resource blocks are supposed to be padded to an even size.
    // I'm not sure if the padding is included in the length field
    if (block.length % 2 != 0)
        m_file.seekg(1, std::ios::cur);

    return check_io();
}



bool
PSDInput::validate_resource(ImageResourceBlock& block)
{
    if (std::memcmp(block.signature, "8BIM", 4) != 0) {
        errorf("[Image Resource] invalid signature");
        return false;
    }
    return true;
}



bool
PSDInput::handle_resources(ImageResourceMap& resources)
{
    // Loop through each of our resource loaders
    const ImageResourceMap::const_iterator end(resources.end());
    for (const ResourceLoader& loader : resource_loaders) {
        ImageResourceMap::const_iterator it(resources.find(loader.resource_id));
        // If a resource with that ID exists in the file, call the loader
        if (it != end) {
            m_file.seekg(it->second.pos);
            if (!check_io())
                return false;

            loader.load(this, it->second.length);
            if (!check_io())
                return false;
        }
    }
    return true;
}

bool PSDInput::load_resource_1005(uint32_t /*length*/)
{
    ResolutionInfo resinfo;
    // Fixed 16.16
    read_bige<uint32_t>(resinfo.hRes);
    resinfo.hRes /= 65536.0f;
    read_bige<int16_t>(resinfo.hResUnit);
    read_bige<int16_t>(resinfo.widthUnit);
    // Fixed 16.16
    read_bige<uint32_t>(resinfo.vRes);
    resinfo.vRes /= 65536.0f;
    read_bige<int16_t>(resinfo.vResUnit);
    read_bige<int16_t>(resinfo.heightUnit);
    if (!m_file)
        return false;

    // Make sure the same unit is used both horizontally and vertically
    // FIXME(dewyatt): I don't know for sure that the unit can differ. However,
    // if it can, perhaps we should be using ResolutionUnitH/ResolutionUnitV or
    // something similar.
    if (resinfo.hResUnit != resinfo.vResUnit) {
        errorf(
            "[Image Resource] [ResolutionInfo] Resolutions must have the same unit");
        return false;
    }
    // Make sure the unit is supported
    // Note: This relies on the above check that the units are the same.
    if (resinfo.hResUnit != ResolutionInfo::PixelsPerInch
        && resinfo.hResUnit != ResolutionInfo::PixelsPerCentimeter) {
        errorf(
            "[Image Resource] [ResolutionInfo] Unrecognized resolution unit");
        return false;
    }
    common_attribute("XResolution", resinfo.hRes);
    common_attribute("XResolution", resinfo.hRes);
    common_attribute("YResolution", resinfo.vRes);
    switch (resinfo.hResUnit) {
    case ResolutionInfo::PixelsPerInch:
        common_attribute("ResolutionUnit", "in");
        break;
    case ResolutionInfo::PixelsPerCentimeter:
        common_attribute("ResolutionUnit", "cm");
        break;
    };
    return true;
}



bool
PSDInput::load_resource_1006(uint32_t length)
{
    int32_t bytes_remaining = length;
    std::string name;
    while (m_file && bytes_remaining >= 2) {
        bytes_remaining -= read_pascal_string(name, 1);
        m_alpha_names.push_back(name);
    }
    return check_io();
}



bool PSDInput::load_resource_1010(uint32_t /*length*/)
{
    const double int8_to_dbl = 1.0 / 0xFF;
    int8_t color_id;
    int32_t color;

    read_bige<int8_t>(color_id);
    read_bige<int32_t>(color);

    m_background_color[0] = ((color)&0xFF) * int8_to_dbl;
    m_background_color[1] = ((color >> 8) & 0xFF) * int8_to_dbl;
    m_background_color[2] = ((color >> 16) & 0xFF) * int8_to_dbl;
    m_background_color[3] = ((color >> 24) & 0xFF) * int8_to_dbl;

    return true;
}



bool
PSDInput::load_resource_1033(uint32_t length)
{
    return load_resource_thumbnail(length, true);
}



bool
PSDInput::load_resource_1036(uint32_t length)
{
    return load_resource_thumbnail(length, false);
}

bool
PSDInput::load_resource_1039(uint32_t length)
{
    std::unique_ptr<char[]> icc_buf(new char[length]);
    if (!m_file.read(icc_buf.get(), length))
        return false;

    TypeDesc type(TypeDesc::UINT8, length);
    common_attribute("ICCProfile", type, icc_buf.get());
    return true;
}

bool PSDInput::load_resource_1047(uint32_t /*length*/)
{
    read_bige<int16_t>(m_transparency_index);
    if (m_transparency_index < 0 || m_transparency_index >= 768) {
        errorf("[Image Resource] [Transparency Index] index is out of range");
        return false;
    }
    return true;
}



bool
PSDInput::load_resource_1058(uint32_t length)
{
    std::string data(length, 0);
    if (!m_file.read(&data[0], length))
        return false;

    if (!decode_exif(data, m_composite_attribs)
        || !decode_exif(data, m_common_attribs)) {
        errorf("Failed to decode Exif data");
        return false;
    }
    return true;
}



bool
PSDInput::load_resource_1059(uint32_t length)
{
    //FIXME(dewyatt): untested, I don't have any images with this resource
    return load_resource_1058(length);
}



bool
PSDInput::load_resource_1060(uint32_t length)
{
    std::string data(length, 0);
    if (!m_file.read(&data[0], length))
        return false;

    // Store the XMP data for the composite and all other subimages
    if (!decode_xmp(data, m_composite_attribs)
        || !decode_xmp(data, m_common_attribs)) {
        errorf("Failed to decode XMP data");
        return false;
    }
    return true;
}



bool PSDInput::load_resource_1064(uint32_t /*length*/)
{
    uint32_t version;
    if (!read_bige<uint32_t>(version))
        return false;

    if (version != 1 && version != 2) {
        errorf("[Image Resource] [Pixel Aspect Ratio] Unrecognized version");
        return false;
    }
    double aspect_ratio;
    if (!read_bige<double>(aspect_ratio))
        return false;

    // FIXME(dewyatt): loss of precision?
    common_attribute("PixelAspectRatio", (float)aspect_ratio);
    return true;
}



bool
PSDInput::load_resource_thumbnail(uint32_t length, bool isBGR)
{
    enum ThumbnailFormat { kJpegRGB = 1, kRawRGB = 0 };

    uint32_t format;
    uint32_t width, height;
    uint32_t widthbytes;
    uint32_t total_size;
    uint32_t compressed_size;
    uint16_t bpp;
    uint16_t planes;
    int stride;
    jpeg_decompress_struct cinfo;
    thumbnail_error_mgr jerr;
    uint32_t jpeg_length = length - 28;

    read_bige<uint32_t>(format);
    read_bige<uint32_t>(width);
    read_bige<uint32_t>(height);
    read_bige<uint32_t>(widthbytes);
    read_bige<uint32_t>(total_size);
    read_bige<uint32_t>(compressed_size);
    read_bige<uint16_t>(bpp);
    read_bige<uint16_t>(planes);
    if (!m_file)
        return false;

    // We only support kJpegRGB since I don't have any test images with
    // kRawRGB
    if (format != kJpegRGB || bpp != 24 || planes != 1) {
        errorf(
            "[Image Resource] [JPEG Thumbnail] invalid or unsupported format");
        return false;
    }

    cinfo.err           = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = thumbnail_error_exit;
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        errorf("[Image Resource] [JPEG Thumbnail] libjpeg error");
        return false;
    }
    std::string jpeg_data(jpeg_length, '\0');
    if (!m_file.read(&jpeg_data[0], jpeg_length))
        return false;

    jpeg_create_decompress(&cinfo);
    jpeg_memory_src(&cinfo, (unsigned char*)&jpeg_data[0], jpeg_length);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    stride = cinfo.output_width * cinfo.output_components;
    ImageSpec thumbspec(cinfo.output_width, cinfo.output_height, 3, TypeUInt8);
    m_thumbnail.reset(thumbspec);
    // jpeg_destroy_decompress will deallocate this
    JSAMPLE** buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo,
                                                  JPOOL_IMAGE, stride, 1);
    while (cinfo.output_scanline < cinfo.output_height) {
        if (jpeg_read_scanlines(&cinfo, buffer, 1) != 1) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            errorf("[Image Resource] [JPEG Thumbnail] libjpeg error");
            return false;
        }
        m_thumbnail.get_pixels(ROI(0, width, cinfo.output_scanline,
                                   cinfo.output_scanline + 1, 0, 1, 0, 3),
                               TypeUInt8, buffer[0]);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    // Set these attributes for the merged composite only (subimage 0)
    composite_attribute("thumbnail_width", (int)width);
    composite_attribute("thumbnail_height", (int)height);
    composite_attribute("thumbnail_nchannels", 3);
    if (isBGR)
        m_thumbnail = ImageBufAlgo::channels(m_thumbnail, 3, { 2, 1, 0 });
    return true;
}



void
PSDInput::thumbnail_error_exit(j_common_ptr cinfo)
{
    thumbnail_error_mgr* mgr = (thumbnail_error_mgr*)cinfo->err;
    longjmp(mgr->setjmp_buffer, 1);
}



bool
PSDInput::load_layers()
{
    if (m_header.version == 1)
        read_bige<uint32_t>(m_layer_mask_info.length);
    else
        read_bige<uint64_t>(m_layer_mask_info.length);

    m_layer_mask_info.begin = m_file.tellg();
    m_layer_mask_info.end   = m_layer_mask_info.begin
                            + (std::streampos)m_layer_mask_info.length;
    if (!check_io())
        return false;

    if (!m_layer_mask_info.length)
        return true;

    LayerMaskInfo::LayerInfo& layer_info = m_layer_mask_info.layer_info;
    if (m_header.version == 1)
        read_bige<uint32_t>(layer_info.length);
    else
        read_bige<uint64_t>(layer_info.length);

    layer_info.begin = m_file.tellg();
    layer_info.end   = layer_info.begin + (std::streampos)layer_info.length;
    if (!check_io())
        return false;

    if (!layer_info.length)
        return true;

    read_bige<int16_t>(layer_info.layer_count);
    if (layer_info.layer_count < 0) {
        m_image_data.transparency = true;
        layer_info.layer_count    = -layer_info.layer_count;
    }
    m_layers.resize(layer_info.layer_count);
    for (int16_t layer_nbr = 0; layer_nbr < layer_info.layer_count;
         ++layer_nbr) {
        Layer& layer = m_layers[layer_nbr];
        if (!load_layer(layer))
            return false;
    }
    for (int16_t layer_nbr = 0; layer_nbr < layer_info.layer_count;
         ++layer_nbr) {
        Layer& layer = m_layers[layer_nbr];
        if (!load_layer_channels(layer))
            return false;
    }
    return true;
}



bool
PSDInput::load_layer(Layer& layer)
{
    read_bige<uint32_t>(layer.top);
    read_bige<uint32_t>(layer.left);
    read_bige<uint32_t>(layer.bottom);
    read_bige<uint32_t>(layer.right);
    read_bige<uint16_t>(layer.channel_count);
    if (!check_io())
        return false;

    layer.width  = std::abs((int)layer.right - (int)layer.left);
    layer.height = std::abs((int)layer.bottom - (int)layer.top);
    layer.channel_info.resize(layer.channel_count);
    for (uint16_t channel = 0; channel < layer.channel_count; channel++) {
        ChannelInfo& channel_info = layer.channel_info[channel];
        read_bige<int16_t>(channel_info.channel_id);
        if (m_header.version == 1)
            read_bige<uint32_t>(channel_info.data_length);
        else
            read_bige<uint64_t>(channel_info.data_length);

        layer.channel_id_map[channel_info.channel_id] = &channel_info;
    }
    char bm_signature[4];
    m_file.read(bm_signature, 4);
    if (!check_io())
        return false;

    if (std::memcmp(bm_signature, "8BIM", 4) != 0) {
        errorf("[Layer Record] Invalid blend mode signature");
        return false;
    }
    m_file.read(layer.bm_key, 4);
    read_bige<uint8_t>(layer.opacity);
    read_bige<uint8_t>(layer.clipping);
    read_bige<uint8_t>(layer.flags);
    // skip filler
    m_file.seekg(1, std::ios::cur);
    read_bige<uint32_t>(layer.extra_length);
    uint32_t extra_remaining = layer.extra_length;
    // layer mask data length
    uint32_t lmd_length;
    read_bige<uint32_t>(lmd_length);
    if (!check_io())
        return false;

    if (lmd_length > 0) {
        std::streampos lmd_start = m_file.tellg();
        std::streampos lmd_end   = lmd_start + (std::streampos)lmd_length;

        if (lmd_length >= 4 * 4 + 1 * 2) {
            read_bige<uint32_t>(layer.mask_data.top);
            read_bige<uint32_t>(layer.mask_data.left);
            read_bige<uint32_t>(layer.mask_data.bottom);
            read_bige<uint32_t>(layer.mask_data.right);
            read_bige<uint8_t>(layer.mask_data.default_color);
            read_bige<uint8_t>(layer.mask_data.flags);
        }

        // skip mask parameters
        // skip "real" fields

        m_file.seekg(lmd_end);
        if (!check_io())
            return false;
    }
    extra_remaining -= (lmd_length + 4);

    // layer blending ranges length
    uint32_t lbr_length;
    read_bige<uint32_t>(lbr_length);
    // skip block
    m_file.seekg(lbr_length, std::ios::cur);
    extra_remaining -= (lbr_length + 4);
    if (!check_io())
        return false;

    extra_remaining -= read_pascal_string(layer.name, 4);
    while (m_file && extra_remaining >= 12) {
        layer.additional_info.emplace_back();
        Layer::AdditionalInfo& info = layer.additional_info.back();

        char signature[4];
        m_file.read(signature, 4);
        m_file.read(info.key, 4);
        if (std::memcmp(signature, "8BIM", 4) != 0
            && std::memcmp(signature, "8B64", 4) != 0) {
            errorf("[Additional Layer Info] invalid signature");
            return false;
        }
        extra_remaining -= 8;
        if (m_header.version == 2 && is_additional_info_psb(info.key)) {
            read_bige<uint64_t>(info.length);
            extra_remaining -= 8;
        } else {
            read_bige<uint32_t>(info.length);
            extra_remaining -= 4;
        }
        m_file.seekg(info.length, std::ios::cur);
        extra_remaining -= info.length;
    }
    return check_io();
}



bool
PSDInput::load_layer_channels(Layer& layer)
{
    for (uint16_t channel = 0; channel < layer.channel_count; ++channel) {
        ChannelInfo& channel_info = layer.channel_info[channel];
        if (!load_layer_channel(layer, channel_info))
            return false;
    }
    return true;
}



bool
PSDInput::load_layer_channel(Layer& layer, ChannelInfo& channel_info)
{
    std::streampos start_pos = m_file.tellg();
    if (channel_info.data_length >= 2) {
        read_bige<uint16_t>(channel_info.compression);
        if (!check_io())
            return false;
    }
    // No data at all or just compression
    if (channel_info.data_length <= 2)
        return true;

    // Use mask_data size when channel_id is -2
    uint32_t width, height;
    if (channel_info.channel_id == ChannelID_LayerMask) {
        width  = (uint32_t)std::abs((int)layer.mask_data.right
                                   - (int)layer.mask_data.left);
        height = (uint32_t)std::abs((int)layer.mask_data.bottom
                                    - (int)layer.mask_data.top);
    } else {
        width  = layer.width;
        height = layer.height;
    }

    channel_info.data_pos = m_file.tellg();
    channel_info.row_pos.resize(height);
    channel_info.row_length = (width * m_header.depth + 7) / 8;
    switch (channel_info.compression) {
    case Compression_Raw:
        if (height) {
            channel_info.row_pos[0] = channel_info.data_pos;
            for (uint32_t i = 1; i < height; ++i)
                channel_info.row_pos[i]
                    = channel_info.row_pos[i - 1]
                      + (std::streampos)channel_info.row_length;
        }
        channel_info.data_length = channel_info.row_length * height;
        break;
    case Compression_RLE:
        // RLE lengths are stored before the channel data
        if (!read_rle_lengths(height, channel_info.rle_lengths))
            return false;

        // channel data is located after the RLE lengths
        channel_info.data_pos = m_file.tellg();
        // subtract the RLE lengths read above
        channel_info.data_length = channel_info.data_length
                                   - (channel_info.data_pos - start_pos);
        if (height) {
            channel_info.row_pos[0] = channel_info.data_pos;
            for (uint32_t i = 1; i < height; ++i)
                channel_info.row_pos[i]
                    = channel_info.row_pos[i - 1]
                      + (std::streampos)channel_info.rle_lengths[i - 1];
        }
        break;
    // These two aren't currently supported. They would likely
    // require large changes in the code as they probably don't
    // support random access like the other modes. I doubt these are
    // used much and I haven't found any test images.
    case Compression_ZIP:
    case Compression_ZIP_Predict:
    default:
        errorf("[Layer Channel] unsupported compression");
        return false;
        ;
    }
    m_file.seekg(channel_info.data_length, std::ios::cur);
    return check_io();
}



bool
PSDInput::read_rle_lengths(uint32_t height, std::vector<uint32_t>& rle_lengths)
{
    rle_lengths.resize(height);
    for (uint32_t row = 0; row < height && m_file; ++row) {
        if (m_header.version == 1)
            read_bige<uint16_t>(rle_lengths[row]);
        else
            read_bige<uint32_t>(rle_lengths[row]);
    }
    return check_io();
}



bool
PSDInput::load_global_mask_info()
{
    if (!m_layer_mask_info.length)
        return true;

    m_file.seekg(m_layer_mask_info.layer_info.end);
    uint64_t remaining = m_layer_mask_info.end - m_file.tellg();
    uint32_t length;

    // This section should be at least 17 bytes, but some files lack
    // global mask info and additional layer info, not covered in the spec
    if (remaining < 17) {
        m_file.seekg(m_layer_mask_info.end);
        return true;
    }

    read_bige<uint32_t>(length);
    std::streampos start = m_file.tellg();
    std::streampos end   = start + (std::streampos)length;
    if (!check_io())
        return false;

    // this can be empty
    if (!length)
        return true;

    read_bige<uint16_t>(m_global_mask_info.overlay_color_space);
    for (int i = 0; i < 4; ++i)
        read_bige<uint16_t>(m_global_mask_info.color_components[i]);

    read_bige<uint16_t>(m_global_mask_info.opacity);
    read_bige<int16_t>(m_global_mask_info.kind);
    m_file.seekg(end);
    return check_io();
}



bool
PSDInput::load_global_additional()
{
    if (!m_layer_mask_info.length)
        return true;

    char signature[4];
    char key[4];
    uint64_t length;
    uint64_t remaining = m_layer_mask_info.length
                         - (m_file.tellg() - m_layer_mask_info.begin);
    while (m_file && remaining >= 12) {
        m_file.read(signature, 4);
        if (!check_io())
            return false;

        // the spec supports 8BIM, and 8B64 (presumably for psb support)
        if (std::memcmp(signature, "8BIM", 4) != 0
            && std::memcmp(signature, "8B64", 4) != 0) {
            errorf("[Global Additional Layer Info] invalid signature");
            return false;
        }
        m_file.read(key, 4);
        if (!check_io())
            return false;

        remaining -= 8;
        if (m_header.version == 2 && is_additional_info_psb(key)) {
            read_bige<uint64_t>(length);
            remaining -= 8;
        } else {
            read_bige<uint32_t>(length);
            remaining -= 4;
        }
        // Long story short these are aligned to 4 bytes but that is not
        // included in the stored length and the specs do not mention it.

        // round up to multiple of 4
        length = (length + 3) & ~3;
        remaining -= length;
        // skip it for now
        m_file.seekg(length, std::ios::cur);
    }
    // finished with the layer and mask information section, seek to the end
    m_file.seekg(m_layer_mask_info.end);
    return check_io();
}



bool
PSDInput::load_image_data()
{
    uint16_t compression;
    uint32_t row_length = (m_header.width * m_header.depth + 7) / 8;
    int16_t id          = 0;
    read_bige<uint16_t>(compression);
    if (!check_io())
        return false;

    if (compression != Compression_Raw && compression != Compression_RLE) {
        errorf("[Image Data Section] unsupported compression");
        return false;
    }
    m_image_data.channel_info.resize(m_header.channel_count);
    // setup some generic properties and read any RLE lengths
    // Image Data Section has RLE lengths for all channels stored first
    for (ChannelInfo& channel_info : m_image_data.channel_info) {
        channel_info.compression = compression;
        channel_info.channel_id  = id++;
        channel_info.data_length = row_length * m_header.height;
        if (compression == Compression_RLE) {
            if (!read_rle_lengths(m_header.height, channel_info.rle_lengths))
                return false;
        }
    }
    for (ChannelInfo& channel_info : m_image_data.channel_info) {
        channel_info.row_pos.resize(m_header.height);
        channel_info.data_pos   = m_file.tellg();
        channel_info.row_length = (m_header.width * m_header.depth + 7) / 8;
        switch (compression) {
        case Compression_Raw:
            channel_info.row_pos[0] = channel_info.data_pos;
            for (uint32_t i = 1; i < m_header.height; ++i)
                channel_info.row_pos[i] = channel_info.row_pos[i - 1]
                                          + (std::streampos)row_length;

            m_file.seekg(channel_info.row_pos.back()
                         + (std::streampos)row_length);
            break;
        case Compression_RLE:
            channel_info.row_pos[0] = channel_info.data_pos;
            for (uint32_t i = 1; i < m_header.height; ++i)
                channel_info.row_pos[i]
                    = channel_info.row_pos[i - 1]
                      + (std::streampos)channel_info.rle_lengths[i - 1];

            m_file.seekg(channel_info.row_pos.back()
                         + (std::streampos)channel_info.rle_lengths.back());
            break;
        }
    }
    return check_io();
}



void
PSDInput::setup()
{
    // raw_channel_count is the number of channels in the file
    // spec_channel_count is what we will report to OIIO client
    int raw_channel_count, spec_channel_count;
    if (m_header.color_mode == ColorMode_Multichannel) {
        spec_channel_count = raw_channel_count = m_header.channel_count;
    } else {
        raw_channel_count = mode_channel_count[m_header.color_mode];
        spec_channel_count
            = m_WantRaw ? raw_channel_count
                        : (m_header.color_mode == ColorMode_Grayscale ? 1 : 3);
        if (m_image_data.transparency) {
            spec_channel_count++;
            raw_channel_count++;
        } else if (m_header.color_mode == ColorMode_Indexed
                   && m_transparency_index) {
            spec_channel_count++;
        }
    }

    // Composite spec
    m_specs.emplace_back(m_header.width, m_header.height, spec_channel_count,
                         m_type_desc);
    m_specs.back().extra_attribs = m_composite_attribs.extra_attribs;
    if (m_WantRaw)
        fill_channel_names(m_specs.back(), m_image_data.transparency);

    // Composite channels
    m_channels.reserve(m_subimage_count);
    m_channels.resize(1);
    m_channels[0].reserve(raw_channel_count);
    for (int i = 0; i < raw_channel_count; ++i)
        m_channels[0].push_back(&m_image_data.channel_info[i]);

    for (Layer& layer : m_layers) {
        spec_channel_count = m_WantRaw ? mode_channel_count[m_header.color_mode]
                                       : 3;
        raw_channel_count  = mode_channel_count[m_header.color_mode];
        bool transparency  = (bool)layer.channel_id_map.count(
            ChannelID_Transparency);
        if (transparency) {
            spec_channel_count++;
            raw_channel_count++;
        }
        m_specs.emplace_back(layer.width, layer.height, spec_channel_count,
                             m_type_desc);
        ImageSpec& spec    = m_specs.back();
        spec.x             = layer.left;
        spec.y             = layer.top;
        spec.extra_attribs = m_common_attribs.extra_attribs;
        if (m_WantRaw)
            fill_channel_names(spec, transparency);

        m_channels.resize(m_channels.size() + 1);
        std::vector<ChannelInfo*>& channels = m_channels.back();
        channels.reserve(raw_channel_count);
        for (unsigned int i = 0; i < mode_channel_count[m_header.color_mode];
             ++i)
            channels.push_back(layer.channel_id_map[i]);

        if (transparency)
            channels.push_back(layer.channel_id_map[ChannelID_Transparency]);
        if (layer.name.size())
            spec.attribute("oiio:subimagename", layer.name);
    }

    if (m_specs.back().alpha_channel != -1)
        if (m_keep_unassociated_alpha)
            m_specs.back().attribute("oiio:UnassociatedAlpha", 1);
}



void
PSDInput::fill_channel_names(ImageSpec& spec, bool transparency)
{
    spec.channelnames.clear();
    if (m_header.color_mode == ColorMode_Multichannel) {
        spec.default_channel_names();
    } else {
        for (unsigned int i = 0; i < mode_channel_count[m_header.color_mode];
             ++i)
            spec.channelnames.emplace_back(
                mode_channel_names[m_header.color_mode][i]);
        if (transparency)
            spec.channelnames.emplace_back("A");
    }
}



bool
PSDInput::read_channel_row(const ChannelInfo& channel_info, uint32_t row,
                           char* data)
{
    if (row >= channel_info.row_pos.size())
        return false;

    uint32_t rle_length;
    channel_info.row_pos[row];
    m_file.seekg(channel_info.row_pos[row]);
    switch (channel_info.compression) {
    case Compression_Raw: m_file.read(data, channel_info.row_length); break;
    case Compression_RLE:
        rle_length = channel_info.rle_lengths[row];
        if (m_rle_buffer.size() < rle_length)
            m_rle_buffer.resize(rle_length);

        m_file.read(&m_rle_buffer[0], rle_length);
        if (!check_io())
            return false;

        if (!decompress_packbits(&m_rle_buffer[0], data, rle_length,
                                 channel_info.row_length))
            return false;

        break;
    }
    if (!check_io())
        return false;

    if (!bigendian()) {
        switch (m_header.depth) {
        case 16: swap_endian((uint16_t*)data, m_spec.width); break;
        case 32:
            swap_endian((uint32_t*)data, m_spec.width);
            // if (row == 131)
            //     printf ("%x %x %x %x\n",
            //             ((uint32_t*)data)[0], ((uint32_t*)data)[1],
            //             ((uint32_t*)data)[2], ((uint32_t*)data)[3]);
            // convert_type<float,uint32_t> ((float *)&data[0],
            //                               (uint32_t*)&data[1], m_spec.width);
            break;
        }
    }
    return true;
}



template<typename T>
void
PSDInput::interleave_row(T* dst, size_t nchans)
{
    OIIO_DASSERT(nchans <= m_channels[m_subimage].size());
    for (size_t c = 0; c < nchans; ++c) {
        const T* cbuf = (const T*)&(m_channel_buffers[c][0]);
        for (int x = 0; x < m_spec.width; ++x)
            dst[nchans * x + c] = cbuf[x];
    }
}



bool
PSDInput::indexed_to_rgb(char* dst)
{
    char* src = &m_channel_buffers[m_subimage][0];
    // The color table is 768 bytes which is 256 * 3 channels (always RGB)
    char* table = &m_color_data.data[0];
    if (m_transparency_index >= 0) {
        for (int i = 0; i < m_spec.width; ++i) {
            unsigned char index = *src++;
            if (index == m_transparency_index) {
                std::memset(dst, 0, 4);
                dst += 4;
                continue;
            }
            *dst++ = table[index];        // R
            *dst++ = table[index + 256];  // G
            *dst++ = table[index + 512];  // B
            *dst++ = 0xff;                // A
        }
    } else {
        for (int i = 0; i < m_spec.width; ++i) {
            unsigned char index = *src++;
            *dst++              = table[index];        // R
            *dst++              = table[index + 256];  // G
            *dst++              = table[index + 512];  // B
        }
    }
    return true;
}



bool
PSDInput::bitmap_to_rgb(char* dst)
{
    for (int i = 0; i < m_spec.width; ++i) {
        int byte = i / 8;
        int bit  = 7 - i % 8;
        char result;
        char* src = &m_channel_buffers[m_subimage][byte];
        if (*src & (1 << bit))
            result = 0;
        else
            result = 0xff;

        std::memset(dst, result, 3);
        dst += 3;
    }
    return true;
}



void
PSDInput::set_type_desc()
{
    switch (m_header.depth) {
    case 1:
    case 8: m_type_desc = TypeDesc::UINT8; break;
    case 16: m_type_desc = TypeDesc::UINT16; break;
    case 32: m_type_desc = TypeDesc::FLOAT; break;
    };
}



bool
PSDInput::check_io()
{
    if (!m_file) {
        errorf("\"%s\": I/O error", m_filename);
        return false;
    }
    return true;
}



int
PSDInput::read_pascal_string(std::string& s, uint16_t mod_padding)
{
    s.clear();
    uint8_t length;
    int bytes = 0;
    if (m_file.read((char*)&length, 1)) {
        bytes = 1;
        if (length == 0) {
            if (m_file.seekg(mod_padding - 1, std::ios::cur))
                bytes += mod_padding - 1;
        } else {
            s.resize(length);
            if (m_file.read(&s[0], length)) {
                bytes += length;
                if (mod_padding > 0) {
                    for (int padded_length = length + 1;
                         padded_length % mod_padding != 0; padded_length++) {
                        if (!m_file.seekg(1, std::ios::cur))
                            break;

                        bytes++;
                    }
                }
            }
        }
    }
    return bytes;
}



bool
PSDInput::decompress_packbits(const char* src, char* dst,
                              uint16_t packed_length, uint16_t unpacked_length)
{
    int32_t src_remaining = packed_length;
    int32_t dst_remaining = unpacked_length;
    int16_t header;
    int length;

    while (src_remaining > 0 && dst_remaining > 0) {
        header = *src++;
        src_remaining--;

        if (header == 128)
            continue;
        else if (header >= 0) {
            // (1 + n) literal bytes
            length = 1 + header;
            src_remaining -= length;
            dst_remaining -= length;
            if (src_remaining < 0 || dst_remaining < 0)
                return false;

            std::memcpy(dst, src, length);
            src += length;
            dst += length;
        } else {
            // repeat byte (1 - n) times
            length = 1 - header;
            src_remaining--;
            dst_remaining -= length;
            if (src_remaining < 0 || dst_remaining < 0)
                return false;

            std::memset(dst, *src, length);
            src++;
            dst += length;
        }
    }
    return true;
}



bool
PSDInput::is_additional_info_psb(const char* key)
{
    for (unsigned int i = 0; i < additional_info_psb_count; ++i)
        if (std::memcmp(additional_info_psb[i], key, 4) == 0)
            return true;

    return false;
}

OIIO_PLUGIN_NAMESPACE_END
