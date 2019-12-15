// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include <libheif/heif_cxx.h>


// This plugin utilises libheif:
//   https://github.com/strukturag/libheif
//
// General information about HEIF/HEIC:
//
// Sources of sample images:
//     https://github.com/nokiatech/heif/tree/gh-pages/content


OIIO_PLUGIN_NAMESPACE_BEGIN

class HeifInput final : public ImageInput {
public:
    HeifInput() {}
    virtual ~HeifInput() { close(); }
    virtual const char* format_name(void) const override { return "heif"; }
    virtual int supports(string_view feature) const override
    {
        return feature == "exif";
    }
    // virtual bool valid_file(const std::string& filename) const override;
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    std::string m_filename;
    int m_subimage      = -1;
    int m_num_subimages = 0;
    int m_has_alpha     = false;
    std::unique_ptr<heif::Context> m_ctx;
    heif_item_id m_primary_id;             // id of primary image
    std::vector<heif_item_id> m_item_ids;  // ids of all other images
    heif::ImageHandle m_ihandle;
    heif::Image m_himage;
};



// Export version number and create function symbols
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int heif_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
heif_imageio_library_version()
{
    return "libheif " LIBHEIF_VERSION;
}

OIIO_EXPORT ImageInput*
heif_input_imageio_create()
{
    return new HeifInput;
}

OIIO_EXPORT const char* heif_input_extensions[] = { "heic", "heif", "heics",
                                                    nullptr };

OIIO_PLUGIN_EXPORTS_END


#if 0
bool
HeifInput::valid_file(const std::string& filename) const
{
    const size_t magic_size = 12;
    uint8_t magic[magic_size];
    FILE *file = Filesystem::fopen(filename, "rb");
    fread (magic, magic_size, 1, file);
    fclose (file);
    heif_filetype_result filetype_check = heif_check_filetype(magic,12);
    return filetype_check != heif_filetype_no
            && filetype_check != heif_filetype_yes_unsupported
    // This is what the libheif example said to do, but I can't find
    // the filetype constants declared anywhere. Are they obsolete?
}
#endif



bool
HeifInput::open(const std::string& name, ImageSpec& newspec)
{
    // If user doesn't want to provide any config, just use an empty spec.
    ImageSpec config;
    return open(name, newspec, config);
}



bool
HeifInput::open(const std::string& name, ImageSpec& newspec,
                const ImageSpec& config)
{
    m_filename = name;
    m_subimage = -1;

    m_ctx.reset(new heif::Context);
    m_himage  = heif::Image();
    m_ihandle = heif::ImageHandle();

    try {
        m_ctx->read_from_file(name);
        // FIXME: should someday be read_from_reader to give full flexibility

        m_item_ids   = m_ctx->get_list_of_top_level_image_IDs();
        m_primary_id = m_ctx->get_primary_image_ID();
        for (size_t i = 0; i < m_item_ids.size(); ++i)
            if (m_item_ids[i] == m_primary_id) {
                m_item_ids.erase(m_item_ids.begin() + i);
                break;
            }
        // std::cout << " primary id: " << m_primary_id << "\n";
        // std::cout << " item ids: " << Strutil::join(m_item_ids, ", ") << "\n";
        m_num_subimages = 1 + int(m_item_ids.size());

    } catch (const heif::Error& err) {
        std::string e = err.get_message();
        errorf("%s", e.empty() ? "unknown exception" : e.c_str());
        return false;
    } catch (const std::exception& err) {
        std::string e = err.what();
        errorf("%s", e.empty() ? "unknown exception" : e.c_str());
        return false;
    }

    bool ok = seek_subimage(0, 0);
    newspec = spec();
    return ok;
}



bool
HeifInput::close()
{
    m_himage  = heif::Image();
    m_ihandle = heif::ImageHandle();
    m_ctx.reset();
    m_subimage      = -1;
    m_num_subimages = 0;
    return true;
}



bool
HeifInput::seek_subimage(int subimage, int miplevel)
{
    if (miplevel != 0)
        return false;

    if (subimage == m_subimage) {
        return true;  // already there
    }

    if (subimage >= m_num_subimages) {
        return false;
    }

    try {
        auto id     = (subimage == 0) ? m_primary_id : m_item_ids[subimage - 1];
        m_ihandle   = m_ctx->get_image_handle(id);
        m_has_alpha = m_ihandle.has_alpha_channel();
        auto chroma = m_has_alpha ? heif_chroma_interleaved_RGBA
                                  : heif_chroma_interleaved_RGB;
        m_himage = m_ihandle.decode_image(heif_colorspace_RGB, chroma);

    } catch (const heif::Error& err) {
        std::string e = err.get_message();
        errorf("%s", e.empty() ? "unknown exception" : e.c_str());
        close();
        return false;
    } catch (const std::exception& err) {
        std::string e = err.what();
        errorf("%s", e.empty() ? "unknown exception" : e.c_str());
        close();
        return false;
    }

    int bits = m_himage.get_bits_per_pixel(heif_channel_interleaved);
    m_spec = ImageSpec(m_ihandle.get_width(), m_ihandle.get_height(), bits / 8,
                       TypeUInt8);

    m_spec.attribute("oiio:ColorSpace", "sRGB");

    auto meta_ids = m_ihandle.get_list_of_metadata_block_IDs();
    // std::cout << "nmeta? " << meta_ids.size() << "\n";
    for (auto m : meta_ids) {
        auto metacontents = m_ihandle.get_metadata(m);
        if (Strutil::iequals(m_ihandle.get_metadata_type(m), "Exif")
            && metacontents.size() >= 10) {
            cspan<uint8_t> s(&metacontents[10], metacontents.size() - 10);
            decode_exif(s, m_spec);
        } else if (0  // For now, skip this, I haven't seen anything useful
                   && Strutil::iequals(m_ihandle.get_metadata_type(m), "mime")
                   && Strutil::iequals(m_ihandle.get_metadata_content_type(m),
                                       "application/rdf+xml")) {
            decode_xmp(metacontents, m_spec);
        } else {
#ifdef DEBUG
            std::cout << "Don't know how to decode meta " << m
                      << " type=" << m_ihandle.get_metadata_type(m)
                      << " contenttype='"
                      << m_ihandle.get_metadata_content_type(m) << "'\n";
            std::cout << "---\n"
                      << string_view((const char*)&metacontents[0],
                                     metacontents.size())
                      << "\n---\n";
#endif
        }
    }

    // Erase the orientation metadata becaue libheif appears to be doing
    // the rotation-to-canonical-direction for us.
    m_spec.erase_attribute("Orientation");

    m_subimage = subimage;
    return true;
}



bool
HeifInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (y < 0 || y >= m_spec.height)  // out of range scanline
        return false;

    int ystride          = 0;
    const uint8_t* hdata = m_himage.get_plane(heif_channel_interleaved,
                                              &ystride);
    if (!hdata) {
        errorf("Unknown read error");
        return false;
    }
    hdata += (y - m_spec.y) * ystride;
    memcpy(data, hdata, m_spec.width * m_spec.pixel_bytes());
    return true;
}


OIIO_PLUGIN_NAMESPACE_END
