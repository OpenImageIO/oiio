// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <algorithm>
#include <ctime> /* time_t, struct tm, gmtime */
#include <iostream>
#include <memory>

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/platform.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/tiffutils.h>

#if OIIO_GNUC_VERSION || OIIO_CLANG_VERSION >= 50000
// fix warnings in libraw headers: use of auto_ptr
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#if OIIO_CPLUSPLUS_VERSION >= 17                                               \
    && (OIIO_CLANG_VERSION || OIIO_APPLE_CLANG_VERSION)
// libraw uses auto_ptr, which is not in C++17 at all for clang, though
// it does seem to be for gcc. So for clang, alias it to unique_ptr.
namespace std {
template<class T> using auto_ptr = unique_ptr<T>;
}
#endif

#include <libraw/libraw.h>
#include <libraw/libraw_version.h>


// This plugin utilises LibRaw:
//   http://www.libraw.org/
// Documentation:
//   http://www.libraw.org/docs
// Example raw images from many camera models:
//   https://www.rawsamples.ch


OIIO_PLUGIN_NAMESPACE_BEGIN

class RawInput final : public ImageInput {
public:
    RawInput() {}
    virtual ~RawInput() { close(); }
    virtual const char* format_name(void) const override { return "raw"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "exif"
                /* not yet? || feature == "iptc"*/);
    }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    bool process();
    bool m_process  = true;
    bool m_unpacked = false;
    std::unique_ptr<LibRaw> m_processor;
    libraw_processed_image_t* m_image = nullptr;
    std::string m_filename;
    ImageSpec m_config;  // save config requests
    std::string m_make;

    bool do_unpack();

    // Do the actual open. It expects m_filename and m_config to be set.
    bool open_raw(bool unpack, const std::string& name,
                  const ImageSpec& config);
    void get_makernotes();
    void get_makernotes_canon();
    void get_makernotes_nikon();
    void get_makernotes_olympus();
    void get_makernotes_fuji();
    void get_makernotes_kodak();
    void get_makernotes_pentax();
    void get_makernotes_panasonic();
    void get_makernotes_sony();
    void get_lensinfo();
    void get_shootinginfo();

    template<typename T> static bool allval(cspan<T> d, T v = T(0))
    {
        return std::all_of(d.begin(), d.end(),
                           [&](const T& a) { return a == v; });
    }

    static std::string prefixedname(string_view prefix, std::string& name)
    {
        return prefix.size() ? (std::string(prefix) + ':' + name) : name;
    }

    void add(string_view prefix, std::string name, int data, bool force = true,
             int ignval = 0)
    {
        if (force || data != ignval)
            m_spec.attribute(prefixedname(prefix, name), data);
    }
    void add(string_view prefix, std::string name, float data,
             bool force = true, float ignval = 0)
    {
        if (force || data != ignval)
            m_spec.attribute(prefixedname(prefix, name), data);
    }
    void add(string_view prefix, std::string name, string_view data,
             bool force = true, int ignval = 0)
    {
        if (force || (data.size() && data[0]))
            m_spec.attribute(prefixedname(prefix, name), data);
    }
    void add(string_view prefix, std::string name, unsigned long long data,
             bool force = true, unsigned long long ignval = 0)
    {
        if (force || data != ignval)
            m_spec.attribute(prefixedname(prefix, name), TypeDesc::UINT64,
                             &data);
    }
    void add(string_view prefix, std::string name, unsigned int data,
             bool force = true, int ignval = 0)
    {
        add(prefix, name, (int)data, force, ignval);
    }
    void add(string_view prefix, std::string name, unsigned short data,
             bool force = true, int ignval = 0)
    {
        add(prefix, name, (int)data, force, ignval);
    }
    void add(string_view prefix, std::string name, unsigned char data,
             bool force = true, int ignval = 0)
    {
        add(prefix, name, (int)data, force, ignval);
    }
    void add(string_view prefix, std::string name, double data,
             bool force = true, float ignval = 0)
    {
        add(prefix, name, float(data), force, ignval);
    }

    void add(string_view prefix, std::string name, cspan<int> data,
             bool force = true, int ignval = 0)
    {
        if (force || !allval(data, ignval)) {
            int size = data.size() > 1 ? data.size() : 0;
            m_spec.attribute(prefixedname(prefix, name),
                             TypeDesc(TypeDesc::INT, size), data.data());
        }
    }
    void add(string_view prefix, std::string name, cspan<short> data,
             bool force = true, short ignval = 0)
    {
        if (force || !allval(data, ignval)) {
            int size = data.size() > 1 ? data.size() : 0;
            m_spec.attribute(prefixedname(prefix, name),
                             TypeDesc(TypeDesc::INT16, size), data.data());
        }
    }
    void add(string_view prefix, std::string name, cspan<unsigned short> data,
             bool force = true, unsigned short ignval = 0)
    {
        if (force || !allval(data, ignval)) {
            int size = data.size() > 1 ? data.size() : 0;
            m_spec.attribute(prefixedname(prefix, name),
                             TypeDesc(TypeDesc::UINT16, size), data.data());
        }
    }
    void add(string_view prefix, std::string name, cspan<unsigned char> data,
             bool force = true, unsigned char ignval = 0)
    {
        if (force || !allval(data, ignval)) {
            int size = data.size() > 1 ? data.size() : 0;
            m_spec.attribute(prefixedname(prefix, name),
                             TypeDesc(TypeDesc::UINT8, size), data.data());
        }
    }
    void add(string_view prefix, std::string name, cspan<float> data,
             bool force = true, float ignval = 0)
    {
        if (force || !allval(data, ignval)) {
            int size = data.size() > 1 ? data.size() : 0;
            m_spec.attribute(prefixedname(prefix, name),
                             TypeDesc(TypeDesc::FLOAT, size), data.data());
        }
    }
    void add(string_view prefix, std::string name, cspan<double> data,
             bool force = true, float ignval = 0)
    {
        float* d = OIIO_ALLOCA(float, data.size());
        for (auto i = 0; i < data.size(); ++i)
            d[i] = data[i];
        add(prefix, name, cspan<float>(d, data.size()), force, ignval);
    }
};



// Export version number and create function symbols
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int raw_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
raw_imageio_library_version()
{
    return ustring::sprintf("libraw %s", libraw_version()).c_str();
}

OIIO_EXPORT ImageInput*
raw_input_imageio_create()
{
    return new RawInput;
}

OIIO_EXPORT const char* raw_input_extensions[]
    = { "bay", "bmq",  "cr2", "crw", "cs1", "dc2", "dcr", "dng", "erf",
        "fff", "hdr",  "k25", "kdc", "mdc", "mos", "mrw", "nef", "orf",
        "pef", "pxn",  "raf", "raw", "rdc", "sr2", "srf", "x3f", "arw",
        "3fr", "cine", "ia",  "kc2", "mef", "nrw", "qtk", "rw2", "sti",
        "rwl", "srw",  "drf", "dsc", "ptx", "cap", "iiq", "rwz", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
RawInput::open(const std::string& name, ImageSpec& newspec)
{
    // If user doesn't want to provide any config, just use an empty spec.
    ImageSpec config;
    return open(name, newspec, config);
}



#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 17, 0)
static void
exif_parser_cb(ImageSpec* spec, int tag, int tifftype, int len,
               unsigned int byteorder, LibRaw_abstract_datastream* ifp)
{
    // Oy, the data offsets are all going to be relative to the start of the
    // stream, not relative to our current position and data block. So we
    // need to remember that offset and pass its negative as the
    // offset_adjustment to the handler.
    size_t streampos = ifp->tell();
    // std::cerr << "Stream position " << streampos << "\n";

    TypeDesc type          = tiff_datatype_to_typedesc(TIFFDataType(tifftype),
                                              size_t(len));
    const TagInfo* taginfo = tag_lookup("Exif", tag);
    if (!taginfo) {
        // Strutil::fprintf (std::cerr, "NO TAGINFO FOR CALLBACK tag=%d (0x%x): tifftype=%d,len=%d (%s), byteorder=0x%x\n",
        //                   tag, tag, tifftype, len, type, byteorder);
        return;
    }
    if (type.size() >= (1 << 20))
        return;  // sanity check -- too much memory
    size_t size = tiff_data_size(TIFFDataType(tifftype)) * len;
    std::vector<unsigned char> buf(size);
    ifp->read(buf.data(), size, 1);

    // debug scaffolding
    // Strutil::fprintf (std::cerr, "CALLBACK tag=%s: tifftype=%d,len=%d (%s), byteorder=0x%x\n",
    //                   taginfo->name, tifftype, len, type, byteorder);
    // for (int i = 0; i < std::min(16UL,size); ++i) {
    //     if (buf[i] >= ' ' && buf[i] < 128)
    //         std::cerr << char(buf[i]);
    //     Strutil::fprintf (std::cerr, "(%d) ", int(buf[i]));
    // }
    // std::cerr << "\n";

    bool swab = (littleendian() != (byteorder == 0x4949));
    if (swab) {
        if (type.basetype == TypeDesc::UINT16)
            swap_endian((uint16_t*)buf.data(), len);
        if (type.basetype == TypeDesc::UINT32)
            swap_endian((uint32_t*)buf.data(), len);
    }

    if (taginfo->handler) {
        TIFFDirEntry dir;
        dir.tdir_tag    = uint16_t(tag);
        dir.tdir_type   = uint16_t(tifftype);
        dir.tdir_count  = uint32_t(len);
        dir.tdir_offset = 0;
        taginfo->handler(*taginfo, dir, buf, *spec, swab, -int(streampos));
        // std::cerr << "HANDLED " << taginfo->name << "\n";
        return;
    }
    if (taginfo->tifftype == TIFF_NOTYPE)
        return;  // skip
    if (tifftype == TIFF_RATIONAL || tifftype == TIFF_SRATIONAL) {
        spec->attribute(taginfo->name, type, buf.data());
        return;
    }
    if (type.basetype == TypeDesc::UINT16) {
        spec->attribute(taginfo->name, type, buf.data());
        return;
    }
    if (type.basetype == TypeDesc::UINT32) {
        spec->attribute(taginfo->name, type, buf.data());
        return;
    }
    if (type == TypeString) {
        spec->attribute(taginfo->name, string_view((char*)buf.data(), size));
        return;
    }
    // Strutil::fprintf (std::cerr, "RAW metadata NOT HANDLED: tag=%s: tifftype=%d,len=%d (%s), byteorder=0x%x\n",
    //                   taginfo->name, tifftype, len, type, byteorder);
}
#endif



bool
RawInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    m_filename = name;
    m_config   = config;

    // For a fresh open, we are concerned with just reading all the
    // meatadata quickly, because maybe that's all that will be needed. So
    // call open_raw passing unpack=false. This will not read the pixels! We
    // will need to close and re-open with unpack=true if and when we need
    // the actual pixel values.
    bool ok = open_raw(false, m_filename, m_config);
    if (ok)
        newspec = m_spec;
    else
        close();
    return ok;
}



bool
RawInput::open_raw(bool unpack, const std::string& name,
                   const ImageSpec& config)
{
    // std::cout << "open_raw " << name << " unpack=" << unpack << "\n";
    m_processor.reset(new LibRaw);

    // Temp spec for exif parser callback to dump into
    ImageSpec exifspec;
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 17, 0)
    m_processor->set_exifparser_handler((exif_parser_callback)exif_parser_cb,
                                        &exifspec);
#endif

    int ret;
    if ((ret = m_processor->open_file(name.c_str())) != LIBRAW_SUCCESS) {
        errorf("Could not open file \"%s\", %s", m_filename,
               libraw_strerror(ret));
        return false;
    }

    ASSERT(!m_unpacked);
    if (unpack) {
        if ((ret = m_processor->unpack()) != LIBRAW_SUCCESS) {
            errorf("Could not unpack \"%s\", %s", m_filename,
                   libraw_strerror(ret));
            return false;
        }
    }
    m_processor->adjust_sizes_info_only();

    // Process image at half size if "raw:half_size" is not 0
    m_processor->imgdata.params.half_size
        = config.get_int_attribute("raw:half_size", 0);
    int div = m_processor->imgdata.params.half_size == 0 ? 1 : 2;

    // Set file information
    m_spec = ImageSpec(m_processor->imgdata.sizes.iwidth / div,
                       m_processor->imgdata.sizes.iheight / div,
                       3,  // LibRaw should only give us 3 channels
                       TypeDesc::UINT16);
    // Move the exif attribs we already read into the spec we care about
    m_spec.extra_attribs.swap(exifspec.extra_attribs);

    // Output 16 bit images
    m_processor->imgdata.params.output_bps = 16;

    // Disable exposure correction (unless config "raw:auto_bright" == 1)
    m_processor->imgdata.params.no_auto_bright
        = !config.get_int_attribute("raw:auto_bright", 0);
    // Use camera white balance if "raw:use_camera_wb" is not 0
    m_processor->imgdata.params.use_camera_wb
        = config.get_int_attribute("raw:use_camera_wb", 1);
    // Turn off maximum threshold value (unless set to non-zero)
    m_processor->imgdata.params.adjust_maximum_thr
        = config.get_float_attribute("raw:adjust_maximum_thr", 0.0f);
    // Set camera maximum value if "raw:user_sat" is not 0
    m_processor->imgdata.params.user_sat
        = config.get_int_attribute("raw:user_sat", 0);
    {
        auto p = config.find_attribute("raw:aber");
        if (p && p->type() == TypeDesc(TypeDesc::FLOAT, 2)) {
            m_processor->imgdata.params.aber[0] = p->get<float>(0);
            m_processor->imgdata.params.aber[2] = p->get<float>(1);
        }
        if (p && p->type() == TypeDesc(TypeDesc::DOUBLE, 2)) {
            m_processor->imgdata.params.aber[0] = p->get<double>(0);
            m_processor->imgdata.params.aber[2] = p->get<double>(1);
        }
    }
    // Set user white balance coefficients.
    // Only has effect if "raw:use_camera_wb" is equal to 0,
    // i.e. we are not using the camera white balance
    {
        auto p = config.find_attribute("raw:user_mul");
        if (p && p->type() == TypeDesc(TypeDesc::FLOAT, 4)) {
            m_processor->imgdata.params.user_mul[0] = p->get<float>(0);
            m_processor->imgdata.params.user_mul[1] = p->get<float>(1);
            m_processor->imgdata.params.user_mul[2] = p->get<float>(2);
            m_processor->imgdata.params.user_mul[3] = p->get<float>(3);
        }
        if (p && p->type() == TypeDesc(TypeDesc::DOUBLE, 4)) {
            m_processor->imgdata.params.user_mul[0] = p->get<double>(0);
            m_processor->imgdata.params.user_mul[1] = p->get<double>(1);
            m_processor->imgdata.params.user_mul[2] = p->get<double>(2);
            m_processor->imgdata.params.user_mul[3] = p->get<double>(3);
        }
    }

    // Use embedded color profile. Values mean:
    // 0: do not use embedded color profile
    // 1 (default): use embedded color profile (if present) for DNG files
    //    (always), for other files only if use_camera_wb is set.
    // 3: use embedded color data (if present) regardless of white
    //    balance setting.
    m_processor->imgdata.params.use_camera_matrix
        = config.get_int_attribute("raw:use_camera_matrix", 1);

    // Check to see if the user has explicitly requested output colorspace
    // primaries via a configuration hinnt "raw:ColorSpace". The default if
    // there is no such hint is convert to sRGB, so that if somebody just
    // naively reads a raw image and slaps it into a framebuffer for
    // display, it will work just like a jpeg. More sophisticated users
    // might request a particular color space, like "ACES". Note that a
    // request for "linear" will give you sRGB primaries with a linear
    // response.
    std::string cs = config.get_string_attribute("raw:ColorSpace", "sRGB");
    if (Strutil::iequals(cs, "raw")) {
        // Values straight from the chip
        m_processor->imgdata.params.output_color = 0;
        m_processor->imgdata.params.gamm[0]      = 1.0;
        m_processor->imgdata.params.gamm[1]      = 1.0;
    } else if (Strutil::iequals(cs, "sRGB")) {
        // Request explicit sRGB, including usual sRGB response
        m_processor->imgdata.params.output_color = 1;
        m_processor->imgdata.params.gamm[0]      = 1.0 / 2.4;
        m_processor->imgdata.params.gamm[1]      = 12.92;
    } else if (Strutil::iequals(cs, "Adobe")) {
        // Request Adobe color space with 2.2 gamma (no linear toe)
        m_processor->imgdata.params.output_color = 2;
        m_processor->imgdata.params.gamm[0]      = 1.0 / 2.2;
        m_processor->imgdata.params.gamm[1]      = 0.0;
    } else if (Strutil::iequals(cs, "Wide")) {
        m_processor->imgdata.params.output_color = 3;
        m_processor->imgdata.params.gamm[0]      = 1.0;
        m_processor->imgdata.params.gamm[1]      = 1.0;
    } else if (Strutil::iequals(cs, "ProPhoto")) {
        // ProPhoto by convention has gamma 1.8
        m_processor->imgdata.params.output_color = 4;
        m_processor->imgdata.params.gamm[0]      = 1.8;
        m_processor->imgdata.params.gamm[1]      = 0.0;
    } else if (Strutil::iequals(cs, "XYZ")) {
        // XYZ linear
        m_processor->imgdata.params.output_color = 5;
        m_processor->imgdata.params.gamm[0]      = 1.0;
        m_processor->imgdata.params.gamm[1]      = 1.0;
    } else if (Strutil::iequals(cs, "ACES")) {
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
        // ACES linear
        m_processor->imgdata.params.output_color = 6;
        m_processor->imgdata.params.gamm[0]      = 1.0;
        m_processor->imgdata.params.gamm[1]      = 1.0;
#else
        errorf(
            "raw:ColorSpace value of \"ACES\" is not supported by libRaw %d.%d.%d",
            LIBRAW_MAJOR_VERSION, LIBRAW_MINOR_VERSION, LIBRAW_PATCH_VERSION);
        return false;
#endif
    } else if (Strutil::iequals(cs, "linear")) {
        // Request "sRGB" primaries, linear reponse
        m_processor->imgdata.params.output_color = 1;
        m_processor->imgdata.params.gamm[0]      = 1.0;
        m_processor->imgdata.params.gamm[1]      = 1.0;
    } else {
        errorf("raw:ColorSpace set to unknown value \"%s\"", cs);
        return false;
    }
    m_spec.attribute("oiio:ColorSpace", cs);

    // Exposure adjustment
    float exposure = config.get_float_attribute("raw:Exposure", -1.0f);
    if (exposure >= 0.0f) {
        if (exposure < 0.25f || exposure > 8.0f) {
            errorf("raw:Exposure invalid value. range 0.25f - 8.0f");
            return false;
        }
        m_processor->imgdata.params.exp_correc
            = 1;  // enable exposure correction
        m_processor->imgdata.params.exp_shift
            = exposure;  // set exposure correction
        // Set the attribute in the output spec
        m_spec.attribute("raw:Exposure", exposure);
    }

    // Highlight adjustment
    int highlight_mode = config.get_int_attribute("raw:HighlightMode", 0);
    if (highlight_mode != 0) {
        if (highlight_mode < 0 || highlight_mode > 9) {
            errorf("raw:HighlightMode invalid value. range 0-9");
            return false;
        }
        m_processor->imgdata.params.highlight = highlight_mode;
        m_spec.attribute("raw:HighlightMode", highlight_mode);
    }

    // Interpolation quality
    // note: LibRaw must be compiled with demosaic pack GPL2 to use demosaic
    // algorithms 5-9. It must be compiled with demosaic pack GPL3 for
    // algorithm 10 (AMAzE). If either of these packs are not included, it
    // will silently use option 3 - AHD.
    std::string demosaic = config.get_string_attribute("raw:Demosaic");
    if (demosaic.size()) {
        static const char* demosaic_algs[]
            = { "linear",
                "VNG",
                "PPG",
                "AHD",
                "DCB",
                "AHD-Mod",
                "AFD",
                "VCD",
                "Mixed",
                "LMMSE",
                "AMaZE",
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 16, 0)
                "DHT",
                "AAHD",
#endif
                // Future demosaicing algorithms should go here
                NULL };
        size_t d;
        for (d = 0; demosaic_algs[d]; d++)
            if (Strutil::iequals(demosaic, demosaic_algs[d]))
                break;
        if (demosaic_algs[d])
            m_processor->imgdata.params.user_qual = d;
        else if (Strutil::iequals(demosaic, "none")) {
#ifdef LIBRAW_DECODER_FLATFIELD
            // See if we can access the Bayer patterned data for this raw file
            libraw_decoder_info_t decoder_info;
            m_processor->get_decoder_info(&decoder_info);
            if (!(decoder_info.decoder_flags & LIBRAW_DECODER_FLATFIELD)) {
                errorf("Unable to extract unbayered data from file \"%s\"",
                       name);
                return false;
            }

#endif
            // User has selected no demosaicing, so no processing needs to be done
            m_process = false;

            // This will read back a single, bayered channel
            m_spec.nchannels = 1;
            m_spec.channelnames.clear();
            m_spec.channelnames.emplace_back("Y");

            // Also, any previously set demosaicing options are void, so remove them
            m_spec.erase_attribute("oiio:Colorspace");
            m_spec.erase_attribute("raw:Colorspace");
            m_spec.erase_attribute("raw:Exposure");
        } else {
            errorf("raw:Demosaic set to unknown value");
            return false;
        }
        // Set the attribute in the output spec
        m_spec.attribute("raw:Demosaic", demosaic);
    } else {
        m_processor->imgdata.params.user_qual = 3;
        m_spec.attribute("raw:Demosaic", "AHD");
    }

    // Metadata

    const libraw_image_sizes_t& sizes(m_processor->imgdata.sizes);
    m_spec.attribute("PixelAspectRatio", (float)sizes.pixel_aspect);
    // FIXME: sizes. top_margin, left_margin, raw_pitch, mask?

    const libraw_iparams_t& idata(m_processor->imgdata.idata);
    const libraw_colordata_t& color(m_processor->imgdata.color);

    if (idata.make[0]) {
        m_make = std::string(idata.make);
        m_spec.attribute("Make", idata.make);
    } else {
        m_make.clear();
    }
    if (idata.model[0])
        m_spec.attribute("Model", idata.model);
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 17, 0)
    if (idata.software[0])
        m_spec.attribute("Software", idata.software);
    else
#endif
        if (color.model2[0])
        m_spec.attribute("Software", color.model2);

    // FIXME: idata. dng_version, is_foveon, colors, filters, cdesc

    m_spec.attribute("Exif:Flash", (int)color.flash_used);

    // FIXME -- all sorts of things in this struct

    const libraw_imgother_t& other(m_processor->imgdata.other);
    m_spec.attribute("Exif:ISOSpeedRatings", (int)other.iso_speed);
    m_spec.attribute("ExposureTime", other.shutter);
    m_spec.attribute("Exif:ShutterSpeedValue", -std::log2(other.shutter));
    m_spec.attribute("FNumber", other.aperture);
    m_spec.attribute("Exif:ApertureValue", 2.0f * std::log2(other.aperture));
    m_spec.attribute("Exif:FocalLength", other.focal_len);
    struct tm m_tm;
    Sysutil::get_local_time(&m_processor->imgdata.other.timestamp, &m_tm);
    char datetime[20];
    strftime(datetime, 20, "%Y-%m-%d %H:%M:%S", &m_tm);
    m_spec.attribute("DateTime", datetime);
    add("raw", "ShotOrder", other.shot_order, false);
    // FIXME: other.shot_order
    if (other.desc[0])
        m_spec.attribute("ImageDescription", other.desc);
    if (other.artist[0])
        m_spec.attribute("Artist", other.artist);
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 17, 0)
    if (other.parsed_gps.gpsparsed) {
        add("GPS", "Latitude", other.parsed_gps.latitude, false, 0.0f);
        add("GPS", "Longitude", other.parsed_gps.longtitude, false,
            0.0f);  // N.B. wrong spelling!
        add("GPS", "TimeStamp", other.parsed_gps.gpstimestamp, false, 0.0f);
        add("GPS", "Altitude", other.parsed_gps.altitude, false, 0.0f);
        add("GPS", "LatitudeRef", string_view(&other.parsed_gps.latref, 1),
            false);
        add("GPS", "LongitudeRef", string_view(&other.parsed_gps.longref, 1),
            false);
        add("GPS", "AltitudeRef", string_view(&other.parsed_gps.altref, 1),
            false);
        add("GPS", "Status", string_view(&other.parsed_gps.gpsstatus, 1),
            false);
    }
#endif
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    // float FlashEC;
    // float FlashGN;
    // float CameraTemperature;
    // float SensorTemperature;
    // float SensorTemperature2;
    // float LensTemperature;
    // float AmbientTemperature;
    // float BatteryTemperature;
    // float exifAmbientTemperature;
    add("Exif", "Humidity", other.exifHumidity, false, 0.0f);
    add("Exif", "Pressure", other.exifPressure, false, 0.0f);
    add("Exif", "WaterDepth", other.exifWaterDepth, false, 0.0f);
    add("Exif", "Acceleration", other.exifAcceleration, false, 0.0f);
    add("Exif", "CameraElevactionAngle", other.exifCameraElevationAngle, false,
        0.0f);
    // float real_ISO;
#endif

    // libraw reoriented the image for us, so squash any orientation
    // metadata we may have found in the Exif. Preserve the original as
    // "raw:Orientation".
    int ori = m_spec.get_int_attribute("Orientation", 1);
    if (ori > 1)
        m_spec.attribute("raw:Orientation", ori);
    m_spec.attribute("Orientation", 1);

    // FIXME -- thumbnail possibly in m_processor->imgdata.thumbnail

    get_lensinfo();
    get_shootinginfo();
    get_makernotes();

    return true;
}



// Helper macro: add metadata with the same name as mn.name, but don't
// force it if it's the `ignore` value.
#define MAKER(name, ignore) add(m_make, #name, mn.name, false, ignore)

// Helper: add metadata, no matter what the value.
#define MAKERF(name) add(m_make, #name, mn.name, true)



void
RawInput::get_makernotes()
{
    if (Strutil::istarts_with(m_make, "Canon"))
        get_makernotes_canon();
    else if (Strutil::istarts_with(m_make, "Nikon"))
        get_makernotes_nikon();
    else if (Strutil::istarts_with(m_make, "Olympus"))
        get_makernotes_olympus();
    else if (Strutil::istarts_with(m_make, "Fuji"))
        get_makernotes_fuji();
    else if (Strutil::istarts_with(m_make, "Kodak"))
        get_makernotes_kodak();
    else if (Strutil::istarts_with(m_make, "Panasonic"))
        get_makernotes_panasonic();
    else if (Strutil::istarts_with(m_make, "Pentax"))
        get_makernotes_pentax();
    else if (Strutil::istarts_with(m_make, "Sony"))
        get_makernotes_sony();
}



void
RawInput::get_makernotes_canon()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    auto const& mn(m_processor->imgdata.makernotes.canon);
    // MAKER (CanonColorDataVer, 0);
    // MAKER (CanonColorDataSubVer, 0);
    MAKERF(SpecularWhiteLevel);
    MAKERF(ChannelBlackLevel);
    MAKERF(AverageBlackLevel);
    MAKERF(MeteringMode);
    MAKERF(SpotMeteringMode);
    MAKERF(FlashMeteringMode);
    MAKERF(FlashExposureLock);
    MAKERF(ExposureMode);
    MAKERF(AESetting);
    MAKERF(HighlightTonePriority);
    MAKERF(ImageStabilization);
    MAKERF(FocusMode);
    MAKER(AFPoint, 0);
    MAKERF(FocusContinuous);
    //  short        AFPointsInFocus30D;
    //  uchar        AFPointsInFocus1D[8];
    //  ushort       AFPointsInFocus5D;        /* bytes in reverse*/
    MAKERF(AFAreaMode);
    if (mn.AFAreaMode) {
        MAKERF(NumAFPoints);
        MAKERF(ValidAFPoints);
        MAKERF(AFImageWidth);
        MAKERF(AFImageHeight);
        //  short        AFAreaWidths[61];
        //  short        AFAreaHeights[61];
        //  short        AFAreaXPositions[61];
        //  short        AFAreaYPositions[61];
        //  short        AFPointsInFocus[4]
        //  short        AFPointsSelected[4];
        //  ushort       PrimaryAFPoint;
    }
    MAKERF(FlashMode);
    MAKERF(FlashActivity);
    MAKER(FlashBits, 0);
    MAKER(ManualFlashOutput, 0);
    MAKER(FlashOutput, 0);
    MAKER(FlashGuideNumber, 0);
    MAKERF(ContinuousDrive);
    MAKER(SensorWidth, 0);
    MAKER(SensorHeight, 0);
    MAKER(SensorLeftBorder, 0);
    MAKER(SensorTopBorder, 0);
    MAKER(SensorRightBorder, 0);
    MAKER(SensorBottomBorder, 0);
    MAKER(BlackMaskLeftBorder, 0);
    MAKER(BlackMaskTopBorder, 0);
    MAKER(BlackMaskRightBorder, 0);
    MAKER(BlackMaskBottomBorder, 0);
#endif
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    // Extra added with libraw 0.19:
    // unsigned int mn.multishot[4]
    MAKER(AFMicroAdjMode, 0);
    MAKER(AFMicroAdjValue, 0.0f);
#endif
}



void
RawInput::get_makernotes_nikon()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    auto const& mn(m_processor->imgdata.makernotes.nikon);
    MAKER(ExposureBracketValue, 0.0f);
    MAKERF(ActiveDLighting);
    MAKERF(ShootingMode);
    MAKERF(ImageStabilization);
    MAKER(VibrationReduction, 0);
    MAKERF(VRMode);
    MAKER(FocusMode, 0);
    MAKERF(AFPoint);
    MAKER(AFPointsInFocus, 0);
    MAKERF(ContrastDetectAF);
    MAKERF(AFAreaMode);
    MAKERF(PhaseDetectAF);
    if (mn.PhaseDetectAF) {
        MAKER(PrimaryAFPoint, 0);
        // uchar AFPointsUsed[29];
    }
    if (mn.ContrastDetectAF) {
        MAKER(AFImageWidth, 0);
        MAKER(AFImageHeight, 0);
        MAKER(AFAreaXPposition, 0);
        MAKER(AFAreaYPosition, 0);
        MAKER(AFAreaWidth, 0);
        MAKER(AFAreaHeight, 0);
        MAKER(ContrastDetectAFInFocus, 0);
    }
    MAKER(FlashSetting, 0);
    MAKER(FlashType, 0);
    MAKERF(FlashExposureCompensation);
    MAKERF(ExternalFlashExposureComp);
    MAKERF(FlashExposureBracketValue);
    MAKERF(FlashMode);
    // signed char FlashExposureCompensation2;
    // signed char FlashExposureCompensation3;
    // signed char FlashExposureCompensation4;
    MAKERF(FlashSource);
    MAKERF(FlashFirmware);
    MAKERF(ExternalFlashFlags);
    MAKERF(FlashControlCommanderMode);
    MAKER(FlashOutputAndCompensation, 0);
    MAKER(FlashFocalLength, 0);
    MAKER(FlashGNDistance, 0);
    MAKERF(FlashGroupControlMode);
    MAKERF(FlashGroupOutputAndCompensation);
    MAKER(FlashColorFilter, 0);

    MAKER(NEFCompression, 0);
    MAKER(ExposureMode, -1);
    MAKER(nMEshots, 0);
    MAKER(MEgainOn, 0);
    MAKERF(ME_WB);
    MAKERF(AFFineTune);
    MAKERF(AFFineTuneIndex);
    MAKERF(AFFineTuneAdj);
#endif
}



void
RawInput::get_makernotes_olympus()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    auto const& mn(m_processor->imgdata.makernotes.olympus);
    MAKERF(OlympusCropID);
    MAKERF(OlympusFrame); /* upper left XY, lower right XY */
    MAKERF(OlympusSensorCalibration);
    MAKERF(FocusMode);
    MAKERF(AutoFocus);
    MAKERF(AFPoint);
    // unsigned     AFAreas[64];
    MAKERF(AFPointSelected);
    MAKERF(AFResult);
    // MAKERF(ImageStabilization);  Removed after 0.19
    MAKERF(ColorSpace);
#endif
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    MAKERF(AFFineTune);
    if (mn.AFFineTune)
        MAKERF(AFFineTuneAdj);
#endif
}



void
RawInput::get_makernotes_panasonic()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    auto const& mn(m_processor->imgdata.makernotes.panasonic);
    MAKERF(Compression);
    MAKER(BlackLevelDim, 0);
    MAKERF(BlackLevel);
#endif
}



void
RawInput::get_makernotes_pentax()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    auto const& mn(m_processor->imgdata.makernotes.pentax);
    MAKERF(FocusMode);
    MAKERF(AFPointsInFocus);
    MAKERF(DriveMode);
    MAKERF(AFPointSelected);
    MAKERF(FocusPosition);
    MAKERF(AFAdjustment);
#endif
}



void
RawInput::get_makernotes_kodak()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    auto const& mn(m_processor->imgdata.makernotes.kodak);
    MAKERF(BlackLevelTop);
    MAKERF(BlackLevelBottom);
    MAKERF(offset_left);
    MAKERF(offset_top);
    MAKERF(clipBlack);
    MAKERF(clipWhite);
    // float romm_camDaylight[3][3];
    // float romm_camTungsten[3][3];
    // float romm_camFluorescent[3][3];
    // float romm_camFlash[3][3];
    // float romm_camCustom[3][3];
    // float romm_camAuto[3][3];
#endif
}



void
RawInput::get_makernotes_fuji()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    auto const& mn(m_processor->imgdata.makernotes.fuji);
    add(m_make, "ExpoMidPointShift", mn.FujiExpoMidPointShift);
    add(m_make, "DynamicRange", mn.FujiDynamicRange);
    add(m_make, "FilmMode", mn.FujiFilmMode);
    add(m_make, "DynamicRangeSetting", mn.FujiDynamicRangeSetting);
    add(m_make, "DevelopmentDynamicRange", mn.FujiDevelopmentDynamicRange);
    add(m_make, "AutoDynamicRange", mn.FujiAutoDynamicRange);
    MAKERF(FocusMode);
    MAKERF(AFMode);
    MAKERF(FocusPixel);
    MAKERF(ImageStabilization);
    MAKERF(FlashMode);
    MAKERF(WB_Preset);
    MAKERF(ShutterType);
    MAKERF(ExrMode);
    MAKERF(Macro);
    MAKERF(Rating);
    MAKERF(FrameRate);
    MAKERF(FrameWidth);
    MAKERF(FrameHeight);
#endif
}



void
RawInput::get_makernotes_sony()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    auto const& mn(m_processor->imgdata.makernotes.sony);
    MAKERF(SonyCameraType);
#endif
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 19, 0)
    // uchar Sony0x9400_version; /* 0 if not found/deciphered, 0xa, 0xb, 0xc following exiftool convention */
    // uchar Sony0x9400_ReleaseMode2;
    // unsigned Sony0x9400_SequenceImageNumber;
    // uchar Sony0x9400_SequenceLength1;
    // unsigned Sony0x9400_SequenceFileNumber;
    // uchar Sony0x9400_SequenceLength2;
    if (mn.raw_crop.cwidth || mn.raw_crop.cheight) {
        add(m_make, "cropleft", mn.raw_crop.cleft, true);
        add(m_make, "croptop", mn.raw_crop.ctop, true);
        add(m_make, "cropwidth", mn.raw_crop.cwidth, true);
        add(m_make, "cropheight", mn.raw_crop.cheight, true);
    }
    MAKERF(AFMicroAdjValue);
    MAKERF(AFMicroAdjOn);
    MAKER(AFMicroAdjRegisteredLenses, 0);
    MAKERF(group2010);
    if (mn.real_iso_offset != 0xffff)
        MAKERF(real_iso_offset);
    MAKERF(firmware);
    MAKERF(ImageCount3_offset);
    MAKER(ImageCount3, 0);
    if (mn.ElectronicFrontCurtainShutter == 0
        || mn.ElectronicFrontCurtainShutter == 1)
        MAKERF(ElectronicFrontCurtainShutter);
    MAKER(MeteringMode2, 0);
    add(m_make, "DateTime", mn.SonyDateTime);
    // MAKERF(TimeStamp);  Removed after 0.19, is in 'other'
    MAKER(ShotNumberSincePowerUp, 0);
#endif
}



void
RawInput::get_lensinfo()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    {
        auto const& mn(m_processor->imgdata.lens);
        MAKER(MinFocal, 0.0f);
        MAKER(MaxFocal, 0.0f);
        MAKER(MaxAp4MinFocal, 0.0f);
        MAKER(MaxAp4MaxFocal, 0.0f);
        MAKER(EXIF_MaxAp, 0.0f);
        MAKER(LensMake, 0);
        MAKER(Lens, 0);
        MAKER(LensSerial, 0);
        MAKER(InternalLensSerial, 0);
        MAKER(FocalLengthIn35mmFormat, 0);
    }
    {
        auto const& mn(m_processor->imgdata.lens.makernotes);
        MAKER(LensID, 0ULL);
        MAKER(Lens, 0);
        MAKER(LensFormat, 0);
        MAKER(LensMount, 0);
        MAKER(CamID, 0ULL);
        MAKER(CameraFormat, 0);
        MAKER(CameraMount, 0);
        MAKER(body, 0);
        MAKER(FocalType, 0);
        MAKER(LensFeatures_pre, 0);
        MAKER(LensFeatures_suf, 0);
        MAKER(MinFocal, 0.0f);
        MAKER(MaxFocal, 0.0f);
        MAKER(MaxAp4MinFocal, 0.0f);
        MAKER(MaxAp4MaxFocal, 0.0f);
        MAKER(MinAp4MinFocal, 0.0f);
        MAKER(MinAp4MaxFocal, 0.0f);
        MAKER(MaxAp, 0.0f);
        MAKER(MinAp, 0.0f);
        MAKER(CurFocal, 0.0f);
        MAKER(CurAp, 0.0f);
        MAKER(MaxAp4CurFocal, 0.0f);
        MAKER(MinAp4CurFocal, 0.0f);
        MAKER(MinFocusDistance, 0.0f);
        MAKER(FocusRangeIndex, 0.0f);
        MAKER(LensFStops, 0.0f);
        MAKER(TeleconverterID, 0ULL);
        MAKER(Teleconverter, 0);
        MAKER(AdapterID, 0ULL);
        MAKER(Adapter, 0);
        MAKER(AttachmentID, 0ULL);
        MAKER(Attachment, 0);
        MAKER(CanonFocalUnits, 0);
        MAKER(FocalLengthIn35mmFormat, 0.0f);
    }

    if (Strutil::iequals(m_make, "Nikon")) {
        auto const& mn(m_processor->imgdata.lens.nikon);
        add(m_make, "EffectiveMaxAp", mn.NikonEffectiveMaxAp);
        add(m_make, "LensIDNumber", mn.NikonLensIDNumber);
        add(m_make, "LensFStops", mn.NikonLensFStops);
        add(m_make, "MCUVersion", mn.NikonMCUVersion);
        add(m_make, "LensType", mn.NikonLensType);
    }
    if (Strutil::iequals(m_make, "DNG")) {
        auto const& mn(m_processor->imgdata.lens.dng);
        MAKER(MaxAp4MaxFocal, 0.0f);
        MAKER(MaxAp4MinFocal, 0.0f);
        MAKER(MaxFocal, 0.0f);
        MAKER(MinFocal, 0.0f);
    }
#endif
}



void
RawInput::get_shootinginfo()
{
#if LIBRAW_VERSION >= LIBRAW_MAKE_VERSION(0, 18, 0)
    auto const& mn(m_processor->imgdata.shootinginfo);
    MAKER(DriveMode, -1);
    MAKER(FocusMode, -1);
    MAKER(MeteringMode, -1);
    MAKERF(AFPoint);
    MAKER(ExposureMode, -1);
    MAKERF(ImageStabilization);
    MAKER(BodySerial, 0);
    MAKER(InternalBodySerial, 0);
#endif
}



bool
RawInput::close()
{
    if (m_image) {
        LibRaw::dcraw_clear_mem(m_image);
        m_image = nullptr;
    }
    m_processor.reset();
    m_unpacked = false;
    m_process  = true;
    return true;
}



bool
RawInput::do_unpack()
{
    if (m_unpacked)
        return true;

    // We need to unpack but we didn't when we opened the file. Close and
    // re-open with unpack.
    close();
    bool ok    = open_raw(true, m_filename, m_config);
    m_unpacked = true;
    return ok;
}



bool
RawInput::process()
{
    if (!m_image) {
        int ret = m_processor->dcraw_process();
        if (ret != LIBRAW_SUCCESS) {
            errorf("Processing image failed, %s", libraw_strerror(ret));
            return false;
        }

        m_image = m_processor->dcraw_make_mem_image(&ret);
        if (!m_image) {
            errorf("LibRaw failed to create in memory image");
            return false;
        }

        if (m_image->type != LIBRAW_IMAGE_BITMAP) {
            errorf("LibRaw did not return expected image type");
            return false;
        }

        if (m_image->colors != 3) {
            errorf("LibRaw did not return 3 channel image");
            return false;
        }
    }
    return true;
}



bool
RawInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (y < 0 || y >= m_spec.height)  // out of range scanline
        return false;

    if (!m_unpacked)
        do_unpack();

    if (!m_process) {
        // The user has selected not to apply any debayering.
        // We take the raw data directly
        unsigned short* scanline = &(
            (m_processor->imgdata.rawdata.raw_image)[m_spec.width * y]);
        memcpy(data, scanline, m_spec.scanline_bytes(true));
        return true;
    }

    // Check the state of the internal RAW reader.
    // Have to load the entire image at once, so only do this once
    if (!m_image) {
        if (!process()) {
            return false;
        }
    }

    int length = m_spec.width * m_image->colors;  // Should always be 3 colors

    // Because we are reading UINT16's, we need to cast m_image->data
    unsigned short* scanline = &(((unsigned short*)m_image->data)[length * y]);
    memcpy(data, scanline, m_spec.scanline_bytes(true));

    return true;
}

OIIO_PLUGIN_NAMESPACE_END
