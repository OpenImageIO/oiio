// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <boost/thread/tss.hpp>

#include <tiffio.h>
#include <zlib.h>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/parallel.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/tiffutils.h>
#include <OpenImageIO/typedesc.h>

#include "imageio_pvt.h"


OIIO_PLUGIN_NAMESPACE_BEGIN


// General TIFF information:
// TIFF 6.0 spec:
//     http://partners.adobe.com/public/developer/en/tiff/TIFF6.pdf
// Other Adobe TIFF docs:
//     http://partners.adobe.com/public/developer/tiff/index.html
// Adobe extensions to allow 16 (and 24) bit float in TIFF (ugh, not on
// their developer page, only on Chris Cox's web site?):
//     http://chriscox.org/TIFFTN3d1.pdf
// Libtiff:
//     http://remotesensing.org/libtiff/



// Helper struct for constructing tables of TIFF tags
struct TIFF_tag_info {
    int tifftag;            // TIFF tag used for this info
    const char* name;       // Attribute name we use, or NULL to ignore the tag
    TIFFDataType tifftype;  // Data type that TIFF wants
};



// Note about MIP-maps versus subimages:
//
// TIFF files support subimages, but do not explicitly support
// multiresolution/MIP maps.  So we have always used subimages to store
// MIP levels.
//
// At present, TIFF is the only format people use for multires textures
// that don't explicitly support it, so rather than make the
// TextureSystem have to handle both cases, we choose instead to emulate
// MIP with subimage in a way that's purely within the TIFFInput class.
// To the outside world, it really does look MIP-mapped.  This only
// kicks in for TIFF files that have the "textureformat" metadata set.
//
// The internal m_subimage really does contain the subimage, but for the
// MIP emulation case, we report the subimage as the MIP level, and 0 as
// the subimage.  It is indeed a tangled web of deceit we weave.



class TIFFInput final : public ImageInput {
public:
    TIFFInput();
    virtual ~TIFFInput();
    virtual const char* format_name(void) const override { return "tiff"; }
    virtual bool valid_file(const std::string& filename) const override;
    virtual int supports(string_view feature) const override
    {
        return (feature == "exif" || feature == "iptc");
        // N.B. No support for arbitrary metadata.
    }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override
    {
        // If m_emulate_mipmap is true, pretend subimages are mipmap levels
        lock_guard lock(m_mutex);
        return m_emulate_mipmap ? 0 : m_subimage;
    }
    virtual int current_miplevel(void) const override
    {
        // If m_emulate_mipmap is true, pretend subimages are mipmap levels
        lock_guard lock(m_mutex);
        return m_emulate_mipmap ? m_subimage : 0;
    }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual ImageSpec spec(int subimage, int miplevel) override;
    virtual ImageSpec spec_dimensions(int subimage, int miplevel) override;
    const ImageSpec& spec(void) const override { return m_spec; }
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool read_native_scanlines(int subimage, int miplevel, int ybegin,
                                       int yend, int z, void* data) override;
    virtual bool read_native_tile(int subimage, int miplevel, int x, int y,
                                  int z, void* data) override;
    virtual bool read_native_tiles(int subimage, int miplevel, int xbegin,
                                   int xend, int ybegin, int yend, int zbegin,
                                   int zend, void* data) override;
    virtual bool read_scanline(int y, int z, TypeDesc format, void* data,
                               stride_t xstride) override;
    virtual bool read_scanlines(int subimage, int miplevel, int ybegin,
                                int yend, int z, int chbegin, int chend,
                                TypeDesc format, void* data, stride_t xstride,
                                stride_t ystride) override;
    virtual bool read_tile(int x, int y, int z, TypeDesc format, void* data,
                           stride_t xstride, stride_t ystride,
                           stride_t zstride) override;
    virtual bool read_tiles(int subimage, int miplevel, int xbegin, int xend,
                            int ybegin, int yend, int zbegin, int zend,
                            int chbegin, int chend, TypeDesc format, void* data,
                            stride_t xstride, stride_t ystride,
                            stride_t zstride) override;

private:
    TIFF* m_tif;                            ///< libtiff handle
    std::string m_filename;                 ///< Stash the filename
    std::vector<unsigned char> m_scratch;   ///< Scratch space for us to use
    std::vector<unsigned char> m_scratch2;  ///< More scratch
    int m_subimage;                  ///< What subimage are we looking at?
    int m_next_scanline;             ///< Next scanline we'll read
    bool m_no_random_access;         ///< Should we avoid random access?
    bool m_emulate_mipmap;           ///< Should we emulate mip with subimage?
    bool m_keep_unassociated_alpha;  ///< If the image is unassociated, please
                                     ///<   try to keep it that way!
    bool m_raw_color;                ///< If the image is not RGB, don't
                                     ///<   transform the color.
    bool m_convert_alpha;            ///< Do we need to associate alpha?
    bool m_separate;                 ///< Separate planarconfig?
    bool m_testopenconfig;           ///< Debug aid to test open-with-config
    bool m_use_rgba_interface;       ///< Sometimes we punt
    bool m_is_byte_swapped;          ///< Is the file opposite our endian?
    int m_rowsperstrip;              ///< For scanline imgs, rows per strip
    unsigned short m_planarconfig;   ///< Planar config of the file
    unsigned short m_bitspersample;  ///< Of the *file*, not the client's view
    unsigned short m_photometric;    ///< Of the *file*, not the client's view
    unsigned short m_compression;    ///< TIFF compression tag
    unsigned short m_predictor;      ///< TIFF compression predictor tag
    unsigned short m_inputchannels;  ///< Channels in the file (careful with CMYK)
    std::vector<unsigned short> m_colormap;   ///< Color map for palette images
    std::vector<uint32_t> m_rgbadata;         ///< Sometimes we punt
    std::vector<ImageSpec> m_subimage_specs;  ///< Cached subimage specs

    // Reset everything to initial state
    void init()
    {
        m_tif                     = NULL;
        m_subimage                = -1;
        m_emulate_mipmap          = false;
        m_keep_unassociated_alpha = false;
        m_raw_color               = false;
        m_convert_alpha           = false;
        m_separate                = false;
        m_inputchannels           = 0;
        m_testopenconfig          = false;
        m_colormap.clear();
        m_use_rgba_interface = false;
        m_subimage_specs.clear();
    }

    // Just close the TIFF file handle, but don't forget anything we
    // learned about the contents of the file or any configuration hints.
    void close_tif()
    {
        if (m_tif) {
            TIFFClose(m_tif);
            m_tif = NULL;
            if (m_rgbadata.size())
                std::vector<uint32_t>().swap(m_rgbadata);  // release
        }
    }

    // Read tags from the current directory of m_tif and fill out spec.
    // If read_meta is false, assume that m_spec already contains valid
    // metadata and should not be cleared or rewritten.
    void readspec(bool read_meta = true);

    // Figure out all the photometric-related aspects of the header
    void readspec_photometric();

    // Convert planar separate to contiguous data format
    void separate_to_contig(int nplanes, int nvals,
                            const unsigned char* separate,
                            unsigned char* contig);

    // Convert palette to RGB
    void palette_to_rgb(int n, const unsigned char* palettepels,
                        unsigned char* rgb);

    // Convert in-bits to out-bits (outbits must be 8, 16, 32, and
    // inbits < outbits)
    void bit_convert(int n, const unsigned char* in, int inbits, void* out,
                     int outbits);

    void invert_photometric(int n, void* data);

    // Calling TIFFGetField (tif, tag, &dest) is supposed to work fine for
    // simple types... as long as the tag types in the file are the correct
    // advertised types.  But for some types -- which we never expect, but
    // it turns out can sometimes happen, TIFFGetField will try to pull
    // a second argument (a void**) off the stack, and that can crash the
    // program!  Ick.  So to avoid this, we always push a pointer, which
    // we expect NOT to be altered, and if it is, it's a danger sign (plus
    // we didn't crash).
    bool safe_tiffgetfield(string_view name, int tag, void* dest)
    {
        void* ptr = NULL;  // dummy -- expect it to stay NULL
        bool ok   = TIFFGetField(m_tif, tag, dest, &ptr);
        if (ptr) {
#ifndef NDEBUG
            std::cerr << "Error safe_tiffgetfield : did not expect ptr set on "
                      << name << " " << (void*)ptr << "\n";
#endif
            return false;
        }
        return ok;
    }

    // Get a string tiff tag field and save it it as a string_view. The
    // return value will be true if the tag was found, otherwise false.
    bool tiff_get_string_field(int tag, string_view& result)
    {
        char* s   = NULL;
        void* ptr = NULL;  // dummy -- expect it to stay NULL
        bool ok   = TIFFGetField(m_tif, tag, &s, &ptr);
        if (ok && ptr) {
            // Oy, some tags need 2 args, which are count, then ptr.
            // There's no way to know ahead of time which ones, so we send
            // a second pointer. If it gets overwritten, then we understand
            // and try it again with 2 args, first one is count.
            unsigned short count;
            ok     = TIFFGetField(m_tif, tag, &count, &s);
            result = string_view(s, count);
        } else if (ok && s && *s)
            result = string_view(s);
        return ok;
    }

    // Get a string tiff tag field and put it into extra_params
    void get_string_attribute(string_view name, int tag)
    {
        string_view s;
        if (tiff_get_string_field(tag, s))
            m_spec.attribute(name, s);
    }

    // Get a matrix tiff tag field and put it into extra_params
    void get_matrix_attribute(string_view name, int tag)
    {
        float* f = NULL;
        if (safe_tiffgetfield(name, tag, &f) && f)
            m_spec.attribute(name, TypeMatrix, f);
    }

    // Get a float tiff tag field and put it into extra_params
    void get_float_attribute(string_view name, int tag)
    {
        float f[16];
        if (safe_tiffgetfield(name, tag, f))
            m_spec.attribute(name, f[0]);
    }

    // Get an int tiff tag field and put it into extra_params
    void get_int_attribute(string_view name, int tag)
    {
        int i;
        if (safe_tiffgetfield(name, tag, &i))
            m_spec.attribute(name, i);
    }

    // Get an int tiff tag field and put it into extra_params
    void get_short_attribute(string_view name, int tag)
    {
        // Make room for two shorts, in case the tag is not the type we
        // expect, and libtiff writes a long instead.
        unsigned short s[2] = { 0, 0 };
        if (safe_tiffgetfield(name, tag, &s)) {
            int i = s[0];
            m_spec.attribute(name, i);
        }
    }

#ifdef TIFF_VERSION_BIG
    const TIFFField* find_field(int tifftag, TIFFDataType tifftype)
    {
        return TIFFFindField(m_tif, tifftag, tifftype);
    }
#else
    const TIFFFieldInfo* find_field(int tifftag, TIFFDataType tifftype)
    {
        return TIFFFindFieldInfo(m_tif, tifftag, tifftype);
    }
#endif

    // Search for TIFF tag having type 'tifftype', and if found,
    // add it in the obvious way to m_spec under the name 'oiioname'.
    void find_tag(int tifftag, TIFFDataType tifftype, string_view oiioname)
    {
        auto info = find_field(tifftag, tifftype);
        if (!info) {
            // Something has gone wrong, libtiff doesn't think the field type
            // is the same as we do.
            return;
        }
        if (tifftype == TIFF_ASCII)
            get_string_attribute(oiioname, tifftag);
        else if (tifftype == TIFF_SHORT)
            get_short_attribute(oiioname, tifftag);
        else if (tifftype == TIFF_LONG)
            get_int_attribute(oiioname, tifftag);
        else if (tifftype == TIFF_RATIONAL || tifftype == TIFF_SRATIONAL
                 || tifftype == TIFF_FLOAT || tifftype == TIFF_DOUBLE)
            get_float_attribute(oiioname, tifftag);
    }

    // If we're at scanline y, where does the next strip start?
    int next_strip_boundary(int y)
    {
        return round_to_multiple(y - m_spec.y, m_rowsperstrip) + m_spec.y;
    }

    bool is_strip_boundary(int y)
    {
        return y == next_strip_boundary(y) || y == m_spec.height;
    }

    // Copy a height x width x chans region of src to dst, un-applying a
    // horizontal predictor to each row. It is permitted for src and dst to
    // be the same.
    template<typename T>
    void undo_horizontal_predictor(T* dst, const T* src, int chans, int width,
                                   int height)
    {
        for (int y = 0; y < height;
             ++y, src += chans * width, dst += chans * width)
            for (int c = 0; c < chans; ++c) {
                dst[c] = src[c];  // element 0
                for (int x = 1; x < width; ++x)
                    dst[x * chans + c] = src[(x - 1) * chans + c]
                                         + src[x * chans + c];
            }
    }

    void uncompress_one_strip(void* compressed_buf, unsigned long csize,
                              void* uncompressed_buf, size_t strip_bytes,
                              int channels, int width, int height,
                              int compression, bool* ok)
    {
        ASSERT (compression == COMPRESSION_ADOBE_DEFLATE /*||
                compression == COMPRESSION_NONE*/);
        if (compression == COMPRESSION_NONE) {
            // just copy if there's no compression
            memcpy(uncompressed_buf, compressed_buf, csize);
            if (m_is_byte_swapped && m_spec.format == TypeUInt16)
                TIFFSwabArrayOfShort((unsigned short*)uncompressed_buf,
                                     width * height * channels);
            return;
        }
        uLong uncompressed_size = (uLong)strip_bytes;
        auto zok = uncompress((Bytef*)uncompressed_buf, &uncompressed_size,
                              (const Bytef*)compressed_buf, csize);
        if (zok != Z_OK || uncompressed_size != strip_bytes) {
            *ok = false;
            return;
        }
        if (m_is_byte_swapped && m_spec.format == TypeUInt16)
            TIFFSwabArrayOfShort((unsigned short*)uncompressed_buf,
                                 width * height * channels);
        if (m_predictor == PREDICTOR_HORIZONTAL) {
            if (m_spec.format == TypeUInt8)
                undo_horizontal_predictor((unsigned char*)uncompressed_buf,
                                          (unsigned char*)uncompressed_buf,
                                          channels, width, height);
            else if (m_spec.format == TypeUInt16)
                undo_horizontal_predictor((unsigned short*)uncompressed_buf,
                                          (unsigned short*)uncompressed_buf,
                                          channels, width, height);
        }
    }

    int tile_index(int x, int y, int z)
    {
        int xtile   = (x - m_spec.x) / m_spec.tile_width;
        int ytile   = (y - m_spec.y) / m_spec.tile_height;
        int ztile   = (z - m_spec.z) / m_spec.tile_depth;
        int nxtiles = (m_spec.width + m_spec.tile_width - 1)
                      / m_spec.tile_width;
        int nytiles = (m_spec.height + m_spec.tile_height - 1)
                      / m_spec.tile_height;
        return xtile + ytile * nxtiles + ztile * nxtiles * nytiles;
    }
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
tiff_input_imageio_create()
{
    return new TIFFInput;
}

// OIIO_EXPORT int tiff_imageio_version = OIIO_PLUGIN_VERSION; // it's in tiffoutput.cpp

OIIO_EXPORT const char* tiff_input_extensions[]
    = { "tif", "tiff", "tx", "env", "sm", "vsm", nullptr };

OIIO_PLUGIN_EXPORTS_END



// Someplace to store an error message from the TIFF error handler
// To avoid thread oddities, we have the storage area buffering error
// messages for seterror()/geterror() be thread-specific.
static boost::thread_specific_ptr<std::string> thread_error_msg;
static atomic_int handler_set;
static spin_mutex handler_mutex;



std::string&
oiio_tiff_last_error()
{
    std::string* e = thread_error_msg.get();
    if (!e) {
        e = new std::string;
        thread_error_msg.reset(e);
    }
    return *e;
}



static void
my_error_handler(const char* str, const char* format, va_list ap)
{
    oiio_tiff_last_error() = Strutil::vformat(format, ap);
}



void
oiio_tiff_set_error_handler()
{
    if (!handler_set) {
        spin_lock lock(handler_mutex);
        if (!handler_set) {
            TIFFSetErrorHandler(my_error_handler);
            TIFFSetWarningHandler(my_error_handler);
            handler_set = 1;
        }
    }
}



struct CompressionCode {
    int code;
    const char* name;
};

// clang-format off

static CompressionCode tiff_compressions[] = {
    { COMPRESSION_NONE,          "none" },        // no compression
    { COMPRESSION_LZW,           "lzw" },         // LZW
    { COMPRESSION_ADOBE_DEFLATE, "zip" },         // deflate / zip
    { COMPRESSION_DEFLATE,       "zip" },         // deflate / zip
    { COMPRESSION_CCITTRLE,      "ccittrle" },    // CCITT RLE
    { COMPRESSION_CCITTFAX3,     "ccittfax3" },   // CCITT group 3 fax
    { COMPRESSION_CCITT_T4,      "ccitt_t4" },    // CCITT T.4
    { COMPRESSION_CCITTFAX4,     "ccittfax4" },   // CCITT group 4 fax
    { COMPRESSION_CCITT_T6,      "ccitt_t6" },    // CCITT T.6
    { COMPRESSION_OJPEG,         "ojpeg" },       // old (pre-TIFF6.0) JPEG
    { COMPRESSION_JPEG,          "jpeg" },        // JPEG
    { COMPRESSION_NEXT,          "next" },        // NeXT 2-bit RLE
    { COMPRESSION_CCITTRLEW,     "ccittrle2" },   // #1 w/ word alignment
    { COMPRESSION_PACKBITS,      "packbits" },    // Macintosh RLE
    { COMPRESSION_THUNDERSCAN,   "thunderscan" }, // ThundeScan RLE
    { COMPRESSION_IT8CTPAD,      "IT8CTPAD" },    // IT8 CT w/ patting
    { COMPRESSION_IT8LW,         "IT8LW" },       // IT8 linework RLE
    { COMPRESSION_IT8MP,         "IT8MP" },       // IT8 monochrome picture
    { COMPRESSION_IT8BL,         "IT8BL" },       // IT8 binary line art
    { COMPRESSION_PIXARFILM,     "pixarfilm" },   // Pixar 10 bit LZW
    { COMPRESSION_PIXARLOG,      "pixarlog" },    // Pixar 11 bit ZIP
    { COMPRESSION_DCS,           "dcs" },         // Kodak DCS encoding
    { COMPRESSION_JBIG,          "isojbig" },     // ISO JBIG
    { COMPRESSION_SGILOG,        "sgilog" },      // SGI log luminance RLE
    { COMPRESSION_SGILOG24,      "sgilog24" },    // SGI log 24bit
    { COMPRESSION_JP2000,        "jp2000" },      // Leadtools JPEG2000
#if defined(TIFF_VERSION_BIG) && TIFFLIB_VERSION >= 20120922
    // Others supported in more recent TIFF library versions.
    { COMPRESSION_T85,           "T85" },         // TIFF/FX T.85 JBIG
    { COMPRESSION_T43,           "T43" },         // TIFF/FX T.43 color layered JBIG
    { COMPRESSION_LZMA,          "lzma" },        // LZMA2
#endif
    { -1, NULL }
};

// clang-format on

static const char*
tiff_compression_name(int code)
{
    for (int i = 0; tiff_compressions[i].name; ++i)
        if (code == tiff_compressions[i].code)
            return tiff_compressions[i].name;
    return NULL;
}



TIFFInput::TIFFInput()
{
    oiio_tiff_set_error_handler();
    init();
}



TIFFInput::~TIFFInput()
{
    // Close, if not already done.
    close();
}



bool
TIFFInput::valid_file(const std::string& filename) const
{
    FILE* file = Filesystem::fopen(filename, "r");
    if (!file)
        return false;  // needs to be able to open
    unsigned short magic[2] = { 0, 0 };
    size_t numRead          = fread(magic, sizeof(unsigned short), 2, file);
    fclose(file);
    if (numRead != 2)  // fread failed
        return false;
    if (magic[0] != TIFF_LITTLEENDIAN && magic[0] != TIFF_BIGENDIAN)
        return false;  // not the right byte order
    if ((magic[0] == TIFF_LITTLEENDIAN) != littleendian())
        swap_endian(&magic[1], 1);
    return (magic[1] == 42 /* Classic TIFF */ || magic[1] == 43 /* Big TIFF */);
}



bool
TIFFInput::open(const std::string& name, ImageSpec& newspec)
{
    oiio_tiff_set_error_handler();
    m_filename = name;
    m_subimage = -1;

    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
TIFFInput::open(const std::string& name, ImageSpec& newspec,
                const ImageSpec& config)
{
    // Check 'config' for any special requests
    if (config.get_int_attribute("oiio:UnassociatedAlpha", 0) == 1)
        m_keep_unassociated_alpha = true;
    if (config.get_int_attribute("oiio:RawColor", 0) == 1)
        m_raw_color = true;
    // This configuration hint has no function other than as a debugging aid
    // for testing whether configurations are received properly from other
    // OIIO components.
    if (config.get_int_attribute("oiio:DebugOpenConfig!", 0))
        m_testopenconfig = true;
    return open(name, newspec);
}



bool
TIFFInput::seek_subimage(int subimage, int miplevel)
{
    if (subimage < 0)  // Illegal
        return false;
    if (m_emulate_mipmap) {
        // Emulating MIPmap?  Pretend one subimage, many MIP levels.
        if (subimage != 0)
            return false;
        subimage = miplevel;
    } else {
        // No MIPmap emulation
        if (miplevel != 0)
            return false;
    }

    if (subimage == m_subimage) {
        // We're already pointing to the right subimage
        return true;
    }

    // If we're emulating a MIPmap, only resolution is allowed to change
    // between MIP levels, so if we already have a valid level in m_spec,
    // we don't need to re-parse metadata, it's guaranteed to be the same.
    bool read_meta = !(m_emulate_mipmap && m_tif && m_subimage >= 0);

    if (!m_tif) {
#ifdef _WIN32
        std::wstring wfilename = Strutil::utf8_to_utf16(m_filename);
        m_tif                  = TIFFOpenW(wfilename.c_str(), "rm");
#else
        m_tif = TIFFOpen(m_filename.c_str(), "rm");
#endif
        if (m_tif == NULL) {
            std::string e = oiio_tiff_last_error();
            errorf("Could not open file: %s", e.length() ? e : m_filename);
            return false;
        }
        m_is_byte_swapped = TIFFIsByteSwapped(m_tif);
        m_subimage        = 0;
    }

    m_next_scanline = 0;  // next scanline we'll read
    if (subimage == m_subimage || TIFFSetDirectory(m_tif, subimage)) {
        m_subimage = subimage;
        readspec(read_meta);
        // OK, some edge cases we just don't handle. For those, fall back on
        // the TIFFRGBA interface.
        bool is_jpeg        = (m_compression == COMPRESSION_JPEG
                        || m_compression == COMPRESSION_OJPEG);
        bool is_nonspectral = (m_photometric == PHOTOMETRIC_YCBCR
                               || m_photometric == PHOTOMETRIC_CIELAB
                               || m_photometric == PHOTOMETRIC_ICCLAB
                               || m_photometric == PHOTOMETRIC_ITULAB
                               || m_photometric == PHOTOMETRIC_LOGL
                               || m_photometric == PHOTOMETRIC_LOGLUV);
        if ((is_jpeg && m_spec.nchannels != 3)
            || (is_nonspectral && !m_raw_color)) {
            char emsg[1024];
            m_use_rgba_interface = true;
            if (!TIFFRGBAImageOK(m_tif, emsg)) {
                errorf("No support for this flavor of TIFF file (%s)", emsg);
                return false;
            }
            // This falls back to looking like uint8 images
            m_spec.format = TypeDesc::UINT8;
            m_spec.channelformats.clear();
            m_photometric = PHOTOMETRIC_RGB;
        }
        if (size_t(subimage) >= m_subimage_specs.size())  // make room
            m_subimage_specs.resize(
                subimage > 0 ? round_to_multiple(subimage + 1, 4) : 1);
        if (m_subimage_specs[subimage]
                .undefined())  // haven't cached this spec yet
            m_subimage_specs[subimage] = m_spec;
        if (m_spec.format == TypeDesc::UNKNOWN) {
            errorf("No support for data format of \"%s\"", m_filename);
            return false;
        }
        return true;
    } else {
        std::string e = oiio_tiff_last_error();
        errorf("%s", e.length() ? e : m_filename);
        m_subimage = -1;
        return false;
    }
}



ImageSpec
TIFFInput::spec(int subimage, int miplevel)
{
    ImageSpec ret;
    int s = m_emulate_mipmap ? miplevel : subimage;
    lock_guard lock(m_mutex);
    if (s >= 0 && s < int(m_subimage_specs.size())
        && !m_subimage_specs[s].undefined()) {
        // If we've cached this spec, we don't need to seek and read
        ret = m_subimage_specs[s];
    } else {
        if (seek_subimage(subimage, miplevel))
            ret = m_spec;
    }
    return ret;
}



ImageSpec
TIFFInput::spec_dimensions(int subimage, int miplevel)
{
    ImageSpec ret;
    int s = m_emulate_mipmap ? miplevel : subimage;
    lock_guard lock(m_mutex);
    if (s >= 0 && s < int(m_subimage_specs.size())
        && !m_subimage_specs[s].undefined()) {
        // If we've cached this spec, we don't need to seek and read
        ret.copy_dimensions(m_subimage_specs[s]);
    } else {
        if (seek_subimage(subimage, miplevel))
            ret.copy_dimensions(m_spec);
    }
    return ret;
}



#define ICC_PROFILE_ATTR "ICCProfile"


void
TIFFInput::readspec(bool read_meta)
{
    uint32 width = 0, height = 0, depth = 0;
    TIFFGetField(m_tif, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(m_tif, TIFFTAG_IMAGELENGTH, &height);
    TIFFGetFieldDefaulted(m_tif, TIFFTAG_IMAGEDEPTH, &depth);
    TIFFGetFieldDefaulted(m_tif, TIFFTAG_SAMPLESPERPIXEL, &m_inputchannels);

    if (read_meta) {
        // clear the whole m_spec and start fresh
        m_spec = ImageSpec((int)width, (int)height, (int)m_inputchannels);
    } else {
        // assume m_spec is valid, except for things that might differ
        // between MIP levels
        m_spec.width     = (int)width;
        m_spec.height    = (int)height;
        m_spec.depth     = (int)depth;
        m_spec.nchannels = (int)m_inputchannels;
    }

    float xpos = 0, ypos = 0;
    TIFFGetField(m_tif, TIFFTAG_XPOSITION, &xpos);
    TIFFGetField(m_tif, TIFFTAG_YPOSITION, &ypos);
    if (xpos || ypos) {
        // In the TIFF files, the positions are in resolutionunit. But we
        // didn't used to interpret it that way, hence the mess below.
        float xres = 1, yres = 1;
        TIFFGetField(m_tif, TIFFTAG_XRESOLUTION, &xres);
        TIFFGetField(m_tif, TIFFTAG_YRESOLUTION, &yres);
        // See if the 'Software' field has a clue about what version of OIIO
        // wrote the TIFF file. This can save us from embarrassing mistakes
        // misinterpreting the image offset.
        int oiio_write_version = 0;
        string_view software;
        if (tiff_get_string_field(TIFFTAG_SOFTWARE, software)
            && Strutil::parse_prefix(software, "OpenImageIO")) {
            int major = 0, minor = 0, patch = 0;
            if (Strutil::parse_int(software, major)
                && Strutil::parse_char(software, '.')
                && Strutil::parse_int(software, minor)
                && Strutil::parse_char(software, '.')
                && Strutil::parse_int(software, patch)) {
                oiio_write_version = major * 10000 + minor * 100 + patch;
            }
        }
        // Old version of OIIO did not write the field correctly, so try
        // to compensate.
        if (oiio_write_version && oiio_write_version < 10803) {
            xres = yres = 1.0f;
        }
        m_spec.x = (int)(xpos * xres);
        m_spec.y = (int)(ypos * yres);
    } else {
        m_spec.x = 0;
        m_spec.y = 0;
    }
    m_spec.z = 0;

    // Start by assuming the "full" (aka display) window is the same as the
    // data window. That's what we'll stick to if there is no further
    // information in the file. But if the file has tags for hte "full"
    // size, assume a display window with origin (0,0) and those dimensions.
    // (Unfortunately, there are no TIFF tags for "full" origin.)
    m_spec.full_x      = m_spec.x;
    m_spec.full_y      = m_spec.y;
    m_spec.full_z      = m_spec.z;
    m_spec.full_width  = m_spec.width;
    m_spec.full_height = m_spec.height;
    m_spec.full_depth  = m_spec.depth;
    if (TIFFGetField(m_tif, TIFFTAG_PIXAR_IMAGEFULLWIDTH, &width) == 1
        && TIFFGetField(m_tif, TIFFTAG_PIXAR_IMAGEFULLLENGTH, &height) == 1
        && width > 0 && height > 0) {
        m_spec.full_width  = width;
        m_spec.full_height = height;
        m_spec.full_x      = 0;
        m_spec.full_y      = 0;
    }

    if (TIFFIsTiled(m_tif)) {
        TIFFGetField(m_tif, TIFFTAG_TILEWIDTH, &m_spec.tile_width);
        TIFFGetField(m_tif, TIFFTAG_TILELENGTH, &m_spec.tile_height);
        TIFFGetFieldDefaulted(m_tif, TIFFTAG_TILEDEPTH, &m_spec.tile_depth);
    } else {
        m_spec.tile_width  = 0;
        m_spec.tile_height = 0;
        m_spec.tile_depth  = 0;
    }

    m_bitspersample = 8;
    TIFFGetField(m_tif, TIFFTAG_BITSPERSAMPLE, &m_bitspersample);
    m_spec.attribute("oiio:BitsPerSample", (int)m_bitspersample);

    unsigned short sampleformat = SAMPLEFORMAT_UINT;
    TIFFGetFieldDefaulted(m_tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
    switch (m_bitspersample) {
    case 1:
    case 2:
    case 4:
    case 6:
        // Make 1, 2, 4, 6 bpp look like byte images
    case 8:
        if (sampleformat == SAMPLEFORMAT_UINT)
            m_spec.set_format(TypeDesc::UINT8);
        else if (sampleformat == SAMPLEFORMAT_INT)
            m_spec.set_format(TypeDesc::INT8);
        else
            m_spec.set_format(TypeDesc::UINT8);  // punt
        break;
    case 10:
    case 12:
    case 14:
        // Make 10, 12, 14 bpp look like 16 bit images
    case 16:
        if (sampleformat == SAMPLEFORMAT_UINT)
            m_spec.set_format(TypeDesc::UINT16);
        else if (sampleformat == SAMPLEFORMAT_INT)
            m_spec.set_format(TypeDesc::INT16);
        else if (sampleformat == SAMPLEFORMAT_IEEEFP) {
            m_spec.set_format(TypeDesc::HALF);
            // Adobe extension, see http://chriscox.org/TIFFTN3d1.pdf
        } else
            m_spec.set_format(TypeDesc::UNKNOWN);
        break;
    case 24:
        // Make 24 bit look like 32 bit
    case 32:
        if (sampleformat == SAMPLEFORMAT_IEEEFP)
            m_spec.set_format(TypeDesc::FLOAT);
        else if (sampleformat == SAMPLEFORMAT_UINT)
            m_spec.set_format(TypeDesc::UINT32);
        else if (sampleformat == SAMPLEFORMAT_INT)
            m_spec.set_format(TypeDesc::INT32);
        else
            m_spec.set_format(TypeDesc::UNKNOWN);
        break;
    case 64:
        if (sampleformat == SAMPLEFORMAT_IEEEFP)
            m_spec.set_format(TypeDesc::DOUBLE);
        else
            m_spec.set_format(TypeDesc::UNKNOWN);
        break;
    default: m_spec.set_format(TypeDesc::UNKNOWN); break;
    }

    // Use the table for all the obvious things that can be mindlessly
    // shoved into the image spec.
    if (read_meta) {
        for (const auto& tag : tag_table("TIFF"))
            find_tag(tag.tifftag, tag.tifftype, tag.name);
        for (const auto& tag : tag_table("Exif"))
            find_tag(tag.tifftag, tag.tifftype, tag.name);
    }

    // Now we need to get fields "by hand" for anything else that is less
    // straightforward...

    readspec_photometric();

    TIFFGetFieldDefaulted(m_tif, TIFFTAG_PLANARCONFIG, &m_planarconfig);
    m_separate = (m_planarconfig == PLANARCONFIG_SEPARATE
                  && m_spec.nchannels > 1
                  && m_photometric != PHOTOMETRIC_PALETTE);
    m_spec.attribute("tiff:PlanarConfiguration", (int)m_planarconfig);
    if (m_planarconfig == PLANARCONFIG_SEPARATE)
        m_spec.attribute("planarconfig", "separate");
    else
        m_spec.attribute("planarconfig", "contig");

    m_compression = 0;
    TIFFGetFieldDefaulted(m_tif, TIFFTAG_COMPRESSION, &m_compression);
    m_spec.attribute("tiff:Compression", (int)m_compression);
    if (const char* compressname = tiff_compression_name(m_compression))
        m_spec.attribute("compression", compressname);
    m_predictor = PREDICTOR_NONE;
    if (!safe_tiffgetfield("Predictor", TIFFTAG_PREDICTOR, &m_predictor))
        m_predictor = PREDICTOR_NONE;

    m_rowsperstrip = -1;
    if (!m_spec.tile_width) {
        TIFFGetField(m_tif, TIFFTAG_ROWSPERSTRIP, &m_rowsperstrip);
        if (m_rowsperstrip > 0)
            m_spec.attribute("tiff:RowsPerStrip", m_rowsperstrip);
    }

    // The libtiff docs say that only uncompressed images, or those with
    // rowsperstrip==1, support random access to scanlines.
    m_no_random_access = (m_compression != COMPRESSION_NONE
                          && m_rowsperstrip != 1);

    // Do we care about fillorder?  No, the TIFF spec says, "We
    // recommend that FillOrder=2 (lsb-to-msb) be used only in
    // special-purpose applications".  So OIIO will assume msb-to-lsb
    // convention until somebody finds a TIFF file in the wild that
    // breaks this assumption.

    unsigned short* sampleinfo  = NULL;
    unsigned short extrasamples = 0;
    TIFFGetFieldDefaulted(m_tif, TIFFTAG_EXTRASAMPLES, &extrasamples,
                          &sampleinfo);
    // std::cerr << "Extra samples = " << extrasamples << "\n";
    bool alpha_is_unassociated = false;  // basic assumption
    if (extrasamples) {
        // If the TIFF ExtraSamples tag was specified, use that to figure
        // out the meaning of alpha.
        int colorchannels = 3;
        if (m_photometric == PHOTOMETRIC_MINISWHITE
            || m_photometric == PHOTOMETRIC_MINISBLACK
            || m_photometric == PHOTOMETRIC_PALETTE
            || m_photometric == PHOTOMETRIC_MASK)
            colorchannels = 1;
        for (int i = 0, c = colorchannels;
             i < extrasamples && c < m_inputchannels; ++i, ++c) {
            // std::cerr << "   extra " << i << " " << sampleinfo[i] << "\n";
            if (sampleinfo[i] == EXTRASAMPLE_ASSOCALPHA) {
                // This is the alpha channel, associated as usual
                m_spec.alpha_channel = c;
            } else if (sampleinfo[i] == EXTRASAMPLE_UNASSALPHA) {
                // This is the alpha channel, but color is unassociated
                m_spec.alpha_channel  = c;
                alpha_is_unassociated = true;
                if (m_keep_unassociated_alpha)
                    m_spec.attribute("oiio:UnassociatedAlpha", 1);
            } else {
                DASSERT(sampleinfo[i] == EXTRASAMPLE_UNSPECIFIED);
                // This extra channel is not alpha at all.  Undo any
                // assumptions we previously made about this channel.
                if (m_spec.alpha_channel == c) {
                    m_spec.channelnames[c] = Strutil::sprintf("channel%d", c);
                    m_spec.alpha_channel   = -1;
                }
            }
        }
        if (m_spec.alpha_channel >= 0) {
            m_spec.channelnames[m_spec.alpha_channel] = "A";
            // Special case: "R","A" should really be named "Y","A", since
            // the first channel is luminance, not red.
            if (m_spec.nchannels == 2 && m_spec.alpha_channel == 1)
                m_spec.channelnames[0] = "Y";
        }
    }
    if (alpha_is_unassociated)
        m_spec.attribute("tiff:UnassociatedAlpha", 1);
    // Will we need to do alpha conversions?
    m_convert_alpha = (m_spec.alpha_channel >= 0 && alpha_is_unassociated
                       && !m_keep_unassociated_alpha);

    // N.B. we currently ignore the following TIFF fields:
    // GrayResponseCurve GrayResponseUnit
    // MaxSampleValue MinSampleValue
    // NewSubfileType SubfileType(deprecated)
    // Colorimetry fields

    // If we've been instructed to skip reading metadata, because it is
    // assumed to be identical to what we already have in m_spec,
    // skip everything following.
    if (!read_meta)
        return;

    short resunit = -1;
    TIFFGetField(m_tif, TIFFTAG_RESOLUTIONUNIT, &resunit);
    switch (resunit) {
    case RESUNIT_NONE: m_spec.attribute("ResolutionUnit", "none"); break;
    case RESUNIT_INCH: m_spec.attribute("ResolutionUnit", "in"); break;
    case RESUNIT_CENTIMETER: m_spec.attribute("ResolutionUnit", "cm"); break;
    }
    float xdensity = m_spec.get_float_attribute("XResolution", 0.0f);
    float ydensity = m_spec.get_float_attribute("YResolution", 0.0f);
    if (xdensity && ydensity)
        m_spec.attribute("PixelAspectRatio", ydensity / xdensity);

    get_matrix_attribute("worldtocamera", TIFFTAG_PIXAR_MATRIX_WORLDTOCAMERA);
    get_matrix_attribute("worldtoscreen", TIFFTAG_PIXAR_MATRIX_WORLDTOSCREEN);
    get_int_attribute("tiff:subfiletype", TIFFTAG_SUBFILETYPE);
    // FIXME -- should subfiletype be "conventionized" and used for all
    // plugins uniformly?

    // Special names for shadow maps
    char* s = NULL;
    TIFFGetField(m_tif, TIFFTAG_PIXAR_TEXTUREFORMAT, &s);
    if (s)
        m_emulate_mipmap = true;
    if (s && !strcmp(s, "Shadow")) {
        for (int c = 0; c < m_spec.nchannels; ++c)
            m_spec.channelnames[c] = "z";
    }

    /// read color profile
    unsigned int icc_datasize = 0;
    unsigned char* icc_buf    = NULL;
    TIFFGetField(m_tif, TIFFTAG_ICCPROFILE, &icc_datasize, &icc_buf);
    if (icc_datasize && icc_buf)
        m_spec.attribute(ICC_PROFILE_ATTR,
                         TypeDesc(TypeDesc::UINT8, icc_datasize), icc_buf);

        // Search for an EXIF IFD in the TIFF file, and if found, rummage
        // around for Exif fields.
#if TIFFLIB_VERSION > 20050912 /* compat with old TIFF libs - skip Exif */
    toff_t exifoffset = 0;
    if (TIFFGetField(m_tif, TIFFTAG_EXIFIFD, &exifoffset)
        && TIFFReadEXIFDirectory(m_tif, exifoffset)) {
        for (const auto& tag : tag_table("Exif"))
            find_tag(tag.tifftag, tag.tifftype, tag.name);
        // Look for a Makernote
        auto makerfield = find_field(EXIF_MAKERNOTE, TIFF_UNDEFINED);
        // std::unique_ptr<uint32_t[]> buf (new uint32_t[]);
        if (makerfield) {
            // bool ok = TIFFGetField (m_tif, tag, dest, &ptr);
            unsigned int mn_datasize = 0;
            unsigned char* mn_buf    = NULL;
            TIFFGetField(m_tif, EXIF_MAKERNOTE, &mn_datasize, &mn_buf);
        }
        // I'm not sure what state TIFFReadEXIFDirectory leaves us.
        // So to be safe, close and re-seek.
        TIFFClose(m_tif);
#    ifdef _WIN32
        std::wstring wfilename = Strutil::utf8_to_utf16(m_filename);
        m_tif                  = TIFFOpenW(wfilename.c_str(), "rm");
#    else
        m_tif = TIFFOpen(m_filename.c_str(), "rm");
#    endif
        if (m_subimage)
            TIFFSetDirectory(m_tif, m_subimage);

        // A few tidbits to look for
        ParamValue* p;
        if ((p = m_spec.find_attribute("Exif:ColorSpace", TypeDesc::INT))) {
            // Exif spec says that anything other than 0xffff==uncalibrated
            // should be interpreted to be sRGB.
            if (*(const int*)p->data() != 0xffff)
                m_spec.attribute("oiio:ColorSpace", "sRGB");
        }
    }
#endif

#if TIFFLIB_VERSION >= 20051230
    // Search for IPTC metadata in IIM form -- but older versions of
    // libtiff botch the size, so ignore it for very old libtiff.
    int iptcsize         = 0;
    const void* iptcdata = NULL;
    if (TIFFGetField(m_tif, TIFFTAG_RICHTIFFIPTC, &iptcsize, &iptcdata)) {
        std::vector<uint32> iptc((uint32*)iptcdata,
                                 (uint32*)iptcdata + iptcsize);
        if (TIFFIsByteSwapped(m_tif))
            TIFFSwabArrayOfLong((uint32*)&iptc[0], iptcsize);
        decode_iptc_iim(&iptc[0], iptcsize * 4, m_spec);
    }
#endif

    // Search for an XML packet containing XMP (IPTC, Exif, etc.)
    int xmlsize         = 0;
    const void* xmldata = NULL;
    if (TIFFGetField(m_tif, TIFFTAG_XMLPACKET, &xmlsize, &xmldata)) {
        // std::cerr << "Found XML data, size " << xmlsize << "\n";
        if (xmldata && xmlsize) {
            std::string xml((const char*)xmldata, xmlsize);
            decode_xmp(xml, m_spec);
        }
    }

#if 0
    // Experimental -- look for photoshop data
    int photoshopsize = 0;
    const void *photoshopdata = NULL;
    if (TIFFGetField (m_tif, TIFFTAG_PHOTOSHOP, &photoshopsize, &photoshopdata)) {
        std::cerr << "Found PHOTOSHOP data, size " << photoshopsize << "\n";
        if (photoshopdata && photoshopsize) {
//            std::string photoshop ((const char *)photoshopdata, photoshopsize);
//            std::cerr << "PHOTOSHOP:\n" << photoshop << "\n---\n";
        }
    }
#endif

    // If Software and IPTC:OriginatingProgram are identical, kill the latter
    if (m_spec.get_string_attribute("Software")
        == m_spec.get_string_attribute("IPTC:OriginatingProgram"))
        m_spec.erase_attribute("IPTC:OriginatingProgram");

    std::string desc = m_spec.get_string_attribute("ImageDescription");
    // If ImageDescription and IPTC:Caption are identical, kill the latter
    if (desc == m_spec.get_string_attribute("IPTC:Caption"))
        m_spec.erase_attribute("IPTC:Caption");

    // Because TIFF doesn't support arbitrary metadata, we look for certain
    // hints in the ImageDescription and turn them into metadata, also
    // removing them from the ImageDescrption.
    bool updatedDesc = false;
    auto cc = Strutil::excise_string_after_head(desc, "oiio:ConstantColor=");
    if (cc.size()) {
        m_spec.attribute("oiio:ConstantColor", cc);
        updatedDesc = true;
    }
    auto ac = Strutil::excise_string_after_head(desc, "oiio:AverageColor=");
    if (ac.size()) {
        m_spec.attribute("oiio:AverageColor", ac);
        updatedDesc = true;
    }
    std::string sha = Strutil::excise_string_after_head(desc, "oiio:SHA-1=");
    if (sha.empty())  // back compatibility with OIIO < 1.5
        sha = Strutil::excise_string_after_head(desc, "SHA-1=");
    if (sha.size()) {
        m_spec.attribute("oiio:SHA-1", sha);
        updatedDesc = true;
    }

    if (updatedDesc) {
        string_view d(desc);
        Strutil::skip_whitespace(d);  // erase if it's only whitespace
        if (d.size())
            m_spec.attribute("ImageDescription", desc);
        else
            m_spec.erase_attribute("ImageDescription");
    }

    // Squash some problematic texture metadata if we suspect it's wrong
    pvt::check_texture_metadata_sanity(m_spec);

    if (m_testopenconfig)  // open-with-config debugging
        m_spec.attribute("oiio:DebugOpenConfig!", 42);
}



void
TIFFInput::readspec_photometric()
{
    m_photometric = (m_spec.nchannels == 1 ? PHOTOMETRIC_MINISBLACK
                                           : PHOTOMETRIC_RGB);
    TIFFGetField(m_tif, TIFFTAG_PHOTOMETRIC, &m_photometric);
    m_spec.attribute("tiff:PhotometricInterpretation", (int)m_photometric);
    switch (m_photometric) {
    case PHOTOMETRIC_SEPARATED: {
        // Photometric "separated" is "usually CMYK".
        m_spec.channelnames.clear();
        short inkset       = INKSET_CMYK;
        short numberofinks = 0;
        TIFFGetFieldDefaulted(m_tif, TIFFTAG_INKSET, &inkset);
        TIFFGetFieldDefaulted(m_tif, TIFFTAG_NUMBEROFINKS, &numberofinks);
        if (inkset == INKSET_CMYK && m_spec.nchannels == 4) {
            // True CMYK
            m_spec.attribute("tiff:ColorSpace", "CMYK");
            if (m_raw_color) {
                m_spec.channelnames.resize(4);
                m_spec.channelnames[0] = "C";
                m_spec.channelnames[1] = "M";
                m_spec.channelnames[2] = "Y";
                m_spec.channelnames[3] = "K";
                m_spec.attribute("oiio:ColorSpace", "CMYK");
            } else {
                // Silently convert to RGB
                m_spec.nchannels = 3;
                m_spec.default_channel_names();
            }
        } else {
            // Non-CMYK ink set
            m_spec.attribute("tiff:ColorSpace", "color separated");
            m_spec.attribute("oiio:ColorSpace", "color separated");
            m_raw_color = true;  // Conversion to RGB doesn't make sense
            const char* inknames = NULL;
            safe_tiffgetfield("tiff:InkNames", TIFFTAG_INKNAMES, &inknames);
            if (inknames && inknames[0] && numberofinks) {
                m_spec.channelnames.clear();
                // Decode the ink names, which are all concatenated together.
                for (int i = 0; i < int(numberofinks); ++i) {
                    string_view ink(inknames);
                    if (ink.size()) {
                        m_spec.channelnames.emplace_back(ink);
                        inknames += ink.size() + 1;
                    } else {
                        // Run out of road
                        numberofinks = i;
                    }
                }
            } else {
                numberofinks = 0;
            }
            // No ink names. Make it up.
            for (int i = numberofinks; i < m_spec.nchannels; ++i)
                m_spec.channelnames.emplace_back(Strutil::sprintf("ink%d", i));
        }
        break;
    }
    case PHOTOMETRIC_YCBCR: m_spec.attribute("tiff:ColorSpace", "YCbCr"); break;
    case PHOTOMETRIC_CIELAB:
        m_spec.attribute("tiff:ColorSpace", "CIELAB");
        break;
    case PHOTOMETRIC_ICCLAB:
        m_spec.attribute("tiff:ColorSpace", "ICCLAB");
        break;
    case PHOTOMETRIC_ITULAB:
        m_spec.attribute("tiff:ColorSpace", "ITULAB");
        break;
    case PHOTOMETRIC_LOGL: m_spec.attribute("tiff:ColorSpace", "LOGL"); break;
    case PHOTOMETRIC_LOGLUV:
        m_spec.attribute("tiff:ColorSpace", "LOGLUV");
        break;
    case PHOTOMETRIC_PALETTE: {
        m_spec.attribute("tiff:ColorSpace", "palette");
        // Read the color map
        unsigned short *r = NULL, *g = NULL, *b = NULL;
        TIFFGetField(m_tif, TIFFTAG_COLORMAP, &r, &g, &b);
        ASSERT(r != NULL && g != NULL && b != NULL);
        m_colormap.clear();
        m_colormap.insert(m_colormap.end(), r, r + (1 << m_bitspersample));
        m_colormap.insert(m_colormap.end(), g, g + (1 << m_bitspersample));
        m_colormap.insert(m_colormap.end(), b, b + (1 << m_bitspersample));
        // Palette TIFF images are always 3 channels (to the client)
        m_spec.nchannels = 3;
        m_spec.default_channel_names();
        if (m_bitspersample != m_spec.format.size() * 8) {
            // For palette images with unusual bits per sample, set
            // oiio:BitsPerSample to the "full" version, to avoid problems
            // when copying the file back to a TIFF file (we don't write
            // palette images), but do leave "tiff:BitsPerSample" to reflect
            // the original file.
            m_spec.attribute("tiff:BitsPerSample", (int)m_bitspersample);
            m_spec.attribute("oiio:BitsPerSample",
                             (int)m_spec.format.size() * 8);
        }
        // FIXME - what about palette + extra (alpha?) channels?  Is that
        // allowed?  And if so, ever encountered in the wild?
        break;
    }
    }
}



bool
TIFFInput::close()
{
    close_tif();
    init();  // Reset to initial state
    return true;
}



/// Helper: Convert n pixels from separate (RRRGGGBBB) to contiguous
/// (RGBRGBRGB) planarconfig.
void
TIFFInput::separate_to_contig(int nplanes, int nvals,
                              const unsigned char* separate,
                              unsigned char* contig)
{
    int channelbytes = m_spec.channel_bytes();
    for (int p = 0; p < nvals; ++p)                 // loop over pixels
        for (int c = 0; c < nplanes; ++c)           // loop over channels
            for (int i = 0; i < channelbytes; ++i)  // loop over data bytes
                contig[(p * nplanes + c) * channelbytes + i]
                    = separate[(c * nvals + p) * channelbytes + i];
}



void
TIFFInput::palette_to_rgb(int n, const unsigned char* palettepels,
                          unsigned char* rgb)
{
    size_t vals_per_byte = 8 / m_bitspersample;
    size_t entries       = 1 << m_bitspersample;
    int highest          = entries - 1;
    DASSERT(m_spec.nchannels == 3);
    DASSERT(m_colormap.size() == 3 * entries);
    for (int x = 0; x < n; ++x) {
        int i = palettepels[x / vals_per_byte];
        i >>= (m_bitspersample * (vals_per_byte - 1 - (x % vals_per_byte)));
        i &= highest;
        *rgb++ = m_colormap[0 * entries + i] / 257;
        *rgb++ = m_colormap[1 * entries + i] / 257;
        *rgb++ = m_colormap[2 * entries + i] / 257;
    }
}



void
TIFFInput::bit_convert(int n, const unsigned char* in, int inbits, void* out,
                       int outbits)
{
    ASSERT(inbits >= 1 && inbits < 32);  // surely bugs if not
    uint32_t highest = (1 << inbits) - 1;
    int B = 0, b = 0;
    // Invariant:
    // So far, we have used in[0..B-1] and the high b bits of in[B].
    for (int i = 0; i < n; ++i) {
        long long val = 0;
        int valbits   = 0;  // bits so far we've accumulated in val
        while (valbits < inbits) {
            // Invariant: we have already accumulated valbits of the next
            // needed value (of a total of inbits), living in the valbits
            // low bits of val.
            int out_left = inbits - valbits;  // How much more we still need
            int in_left  = 8 - b;             // Bits still available in in[B].
            if (in_left <= out_left) {
                // Eat the rest of this byte:
                //   |---------|--------|
                //        b      in_left
                val <<= in_left;
                val |= in[B] & ~(0xffffffff << in_left);
                ++B;
                b = 0;
                valbits += in_left;
            } else {
                // Eat just the bits we need:
                //   |--|---------|-----|
                //     b  out_left  extra
                val <<= out_left;
                int extra = 8 - b - out_left;
                val |= (in[B] >> extra) & ~(0xffffffff << out_left);
                b += out_left;
                valbits = inbits;
            }
        }
        if (outbits == 8)
            ((unsigned char*)out)[i] = (unsigned char)((val * 0xff) / highest);
        else if (outbits == 16)
            ((unsigned short*)out)[i] = (unsigned short)((val * 0xffff)
                                                         / highest);
        else
            ((unsigned int*)out)[i] = (unsigned int)((val * 0xffffffff)
                                                     / highest);
    }
}



void
TIFFInput::invert_photometric(int n, void* data)
{
    switch (m_spec.format.basetype) {
    case TypeDesc::UINT8: {
        unsigned char* d = (unsigned char*)data;
        for (int i = 0; i < n; ++i)
            d[i] = 255 - d[i];
        break;
    }
    default: break;
    }
}



template<typename T>
static void
cmyk_to_rgb(int n, const T* cmyk, size_t cmyk_stride, T* rgb, size_t rgb_stride)
{
    for (; n; --n, cmyk += cmyk_stride, rgb += rgb_stride) {
        float C = convert_type<T, float>(cmyk[0]);
        float M = convert_type<T, float>(cmyk[1]);
        float Y = convert_type<T, float>(cmyk[2]);
        float K = convert_type<T, float>(cmyk[3]);
        float R = (1.0f - C) * (1.0f - K);
        float G = (1.0f - M) * (1.0f - K);
        float B = (1.0f - Y) * (1.0f - K);
        rgb[0]  = convert_type<float, T>(R);
        rgb[1]  = convert_type<float, T>(G);
        rgb[2]  = convert_type<float, T>(B);
    }
}



bool
TIFFInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    y -= m_spec.y;

    if (m_use_rgba_interface) {
        // We punted and used the RGBA image interface -- copy from buffer.
        // libtiff has no way to read just one scanline as RGBA. So we
        // buffer the whole image.
        if (!m_rgbadata.size()) {  // first time through: allocate & read
            m_rgbadata.resize(m_spec.width * m_spec.height * m_spec.depth);
            bool ok = TIFFReadRGBAImageOriented(m_tif, m_spec.width,
                                                m_spec.height, &m_rgbadata[0],
                                                ORIENTATION_TOPLEFT, 0);
            if (!ok) {
                errorf("Unknown error trying to read TIFF as RGBA");
                return false;
            }
        }
        copy_image(m_spec.nchannels, m_spec.width, 1, 1,
                   &m_rgbadata[y * m_spec.width], m_spec.nchannels, 4,
                   4 * m_spec.width, AutoStride, data, m_spec.nchannels,
                   m_spec.width * m_spec.nchannels, AutoStride);
        return true;
    }

    // Make sure there's enough scratch space
    int nvals = m_spec.width * m_inputchannels;
    m_scratch.resize(nvals * m_spec.format.size());

    // For compression modes that don't support random access to scanlines
    // (which I *think* is only LZW), we need to emulate random access by
    // re-seeking.
    if (m_no_random_access) {
        if (m_next_scanline > y) {
            // User is trying to read an earlier scanline than the one we're
            // up to.  Easy fix: start over.
            // FIXME: I'm too tired to look into it now, but I wonder if
            // it is able to randomly seek to the first line in any
            // "strip", in which case we don't need to start from 0, just
            // start from the beginning of the strip we need.
            ImageSpec dummyspec;
            int old_subimage = current_subimage();
            int old_miplevel = current_miplevel();
            // We need to close the TIFF file s that we can re-open and
            // seek back to the beginning of this subimage. The close_tif()
            // accomplishes that. It's important not to do a full close()
            // here, because that would also call init() to fully reset
            // to a fresh ImageInput, thus forgetting any configuration
            // settings such as raw_color or keep_unassociated_alpha.
            close_tif();
            if (!open(m_filename, dummyspec)
                || !seek_subimage(old_subimage, old_miplevel)) {
                return false;  // Somehow, the re-open failed
            }
            ASSERT(m_next_scanline == 0 && current_subimage() == old_subimage
                   && current_miplevel() == old_miplevel);
        }
        while (m_next_scanline < y) {
            // Keep reading until we're read the scanline we really need
            if (TIFFReadScanline(m_tif, &m_scratch[0], m_next_scanline) < 0) {
                errorf("%s", oiio_tiff_last_error());
                return false;
            }
            ++m_next_scanline;
        }
    }
    m_next_scanline = y + 1;

    bool need_bit_convert = (m_bitspersample != 8 && m_bitspersample != 16
                             && m_bitspersample != 32);
    if (m_photometric == PHOTOMETRIC_PALETTE) {
        // Convert from palette to RGB
        if (TIFFReadScanline(m_tif, &m_scratch[0], y) < 0) {
            errorf("%s", oiio_tiff_last_error());
            return false;
        }
        palette_to_rgb(m_spec.width, &m_scratch[0], (unsigned char*)data);
        return true;
    }
    // Not palette...

    int plane_bytes = m_spec.width * m_spec.format.size();
    int planes      = m_separate ? m_inputchannels : 1;
    int input_bytes = plane_bytes * m_inputchannels;
    // Where to read?  Directly into user data if no channel shuffling, bit
    // shifting, or CMYK conversion is needed, otherwise into scratch space.
    unsigned char* readbuf = (unsigned char*)data;
    if (need_bit_convert || m_separate
        || (m_photometric == PHOTOMETRIC_SEPARATED && !m_raw_color))
        readbuf = &m_scratch[0];
    // Perform the reads.  Note that for contig, planes==1, so it will
    // only do one TIFFReadScanline.
    for (int c = 0; c < planes; ++c) { /* planes==1 for contig */
        if (TIFFReadScanline(m_tif, &readbuf[plane_bytes * c], y, c) < 0) {
            errorf("%s", oiio_tiff_last_error());
            return false;
        }
    }

    // Handle less-than-full bit depths
    if (m_bitspersample < 8) {
        // m_scratch now holds nvals n-bit values, contig or separate
        m_scratch2.resize(input_bytes);
        m_scratch.swap(m_scratch2);
        for (int c = 0; c < planes; ++c) /* planes==1 for contig */
            bit_convert(m_separate ? m_spec.width : nvals,
                        &m_scratch2[plane_bytes * c], m_bitspersample,
                        m_separate ? &m_scratch[plane_bytes * c]
                                   : (unsigned char*)data + plane_bytes * c,
                        8);
    } else if (m_bitspersample > 8 && m_bitspersample < 16) {
        // m_scratch now holds nvals n-bit values, contig or separate
        m_scratch2.resize(input_bytes);
        m_scratch.swap(m_scratch2);
        for (int c = 0; c < planes; ++c) /* planes==1 for contig */
            bit_convert(m_separate ? m_spec.width : nvals,
                        &m_scratch2[plane_bytes * c], m_bitspersample,
                        m_separate ? &m_scratch[plane_bytes * c]
                                   : (unsigned char*)data + plane_bytes * c,
                        16);
    } else if (m_bitspersample > 16 && m_bitspersample < 32) {
        // m_scratch now holds nvals n-bit values, contig or separate
        m_scratch2.resize(input_bytes);
        m_scratch.swap(m_scratch2);
        for (int c = 0; c < planes; ++c) /* planes==1 for contig */
            bit_convert(m_separate ? m_spec.width : nvals,
                        &m_scratch2[plane_bytes * c], m_bitspersample,
                        m_separate ? &m_scratch[plane_bytes * c]
                                   : (unsigned char*)data + plane_bytes * c,
                        32);
    }

    // Handle "separate" planarconfig
    if (m_separate) {
        // Convert from separate (RRRGGGBBB) to contiguous (RGBRGBRGB).
        // We know the data is in m_scratch at this point, so
        // contiguize it into the user data area.
        if (m_photometric == PHOTOMETRIC_SEPARATED && !m_raw_color) {
            // CMYK->RGB means we need temp storage.
            m_scratch2.resize(input_bytes);
            separate_to_contig(planes, m_spec.width, &m_scratch[0],
                               &m_scratch2[0]);
            m_scratch.swap(m_scratch2);
        } else {
            // If no CMYK->RGB conversion is necessary, we can "separate"
            // straight into the data area.
            separate_to_contig(planes, m_spec.width, &m_scratch[0],
                               (unsigned char*)data);
        }
    }

    // Handle CMYK
    if (m_photometric == PHOTOMETRIC_SEPARATED && !m_raw_color) {
        // The CMYK will be in m_scratch.
        if (spec().format == TypeDesc::UINT8) {
            cmyk_to_rgb(m_spec.width, (unsigned char*)&m_scratch[0],
                        m_inputchannels, (unsigned char*)data,
                        m_spec.nchannels);
        } else if (spec().format == TypeDesc::UINT16) {
            cmyk_to_rgb(m_spec.width, (unsigned short*)&m_scratch[0],
                        m_inputchannels, (unsigned short*)data,
                        m_spec.nchannels);
        } else {
            errorf("CMYK only supported for UINT8, UINT16");
            return false;
        }
    }

    if (m_photometric == PHOTOMETRIC_MINISWHITE)
        invert_photometric(nvals, data);

    return true;
}



bool
TIFFInput::read_native_scanlines(int subimage, int miplevel, int ybegin,
                                 int yend, int z, void* data)
{
    // If the stars all align properly, try to read strips, and use the
    // thread pool to parallelize the decompression. This can give a large
    // speedup (5x or more!) because the zip decompression dwarfs the
    // actual raw I/O. But libtiff is totally serialized, so we can only
    // parallelize by reading raw (compressed) strips then making calls to
    // zlib ourselves to decompress. Don't bother trying to handle any of
    // the uncommon cases with strips. This covers most real-world cases.
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    yend        = std::min(yend, spec().y + spec().height);
    int nstrips = (yend - ybegin + m_rowsperstrip - 1) / m_rowsperstrip;

    // See if it's easy to read this scanline range as strips. For edge
    // cases we don't with to deal with here, we just call the base class
    // read_native_scanlines, which in turn will loop over each scanline in
    // the range to call our read_native_scanline, which does handle the
    // full range of cases.
    bool read_as_strips =
        // we're reading more than one scanline
        (yend - ybegin) > 1  // no advantage to strips for single scanlines
        // scanline range must be complete strips
        && is_strip_boundary(ybegin)
        && is_strip_boundary(yend)
        // and not palette or cmyk color separated conversions
        && (m_photometric != PHOTOMETRIC_SEPARATED
            && m_photometric != PHOTOMETRIC_PALETTE)
        // no non-multiple-of-8 bits per sample
        && (spec().format.size() * 8 == m_bitspersample)
        // No other unusual cases
        && !m_use_rgba_interface;
    if (!read_as_strips) {
        // Punt and call the base class, which loops over scanlines.
        return ImageInput::read_native_scanlines(subimage, miplevel, ybegin,
                                                 yend, z, data);
    }

    // Are we reading raw (compressed) strips and doing the decompression
    // ourselves?
    bool read_raw_strips =
        // only deflate/zip compression
        (m_compression
         == COMPRESSION_ADOBE_DEFLATE /*|| m_compression == COMPRESSION_NONE*/)
        // only horizontal predictor (or none)
        && (m_predictor == PREDICTOR_HORIZONTAL
            || m_predictor == PREDICTOR_NONE)
        // contig planarconfig only (for now?)
        && !m_separate
        // only uint8, uint16
        && (m_spec.format == TypeUInt8 || m_spec.format == TypeUInt16);

    // We know we wish to read as strips. But additionally, there are some
    // circumstances in which we want to read RAW strips, and do the
    // decompression ourselves, which we can feed to the thread pool to
    // perform in parallel.
    thread_pool* pool = default_thread_pool();
    bool parallelize =
        // and more than one, or no point parallelizing
        nstrips > 1
        // only if we are reading scanlines in order
        && ybegin == m_next_scanline
        // only if we're threading and don't enter the thread pool recursively!
        && pool->size() > 1
        && !pool->is_worker()
        // and not if the feature is turned off
        && m_spec.get_int_attribute("tiff:multithread",
                                    OIIO::get_int_attribute("tiff:multithread"));

    // Make room for, and read the raw (still compressed) strips. As each
    // one is read, kick off the decompress and any other extras, to execute
    // in parallel.
    task_set tasks(pool);
    bool ok        = true;  // failed compression will stash a false here
    int y          = ybegin;
    size_t ystride = m_spec.scanline_bytes(true);
    int stripchans = m_separate ? 1 : m_spec.nchannels;  // chans in each strip
    int planes     = m_separate ? m_spec.nchannels : 1;  // color planes
        // N.B. "separate" planarconfig stores only one channel in a strip
    int stripvals = m_spec.width * stripchans
                    * m_rowsperstrip;  // values in a strip
    imagesize_t strip_bytes = stripvals * m_spec.format.size();
    size_t cbound           = compressBound((uLong)strip_bytes);
    std::unique_ptr<char[]> compressed_scratch;
    std::unique_ptr<char[]> separate_tmp(
        m_separate ? new char[strip_bytes * nstrips * planes] : nullptr);

    if (read_raw_strips) {
        // Make room for, and read the raw (still compressed) strips. As each
        // one is read, kick off the decompress and any other extras, to execute
        // in parallel.
        compressed_scratch.reset(new char[cbound * nstrips * planes]);
        for (size_t stripidx = 0; y + m_rowsperstrip <= yend;
             y += m_rowsperstrip, ++stripidx) {
            char* cbuf        = compressed_scratch.get() + stripidx * cbound;
            tstrip_t stripnum = (y - m_spec.y) / m_rowsperstrip;
            tsize_t csize     = TIFFReadRawStrip(m_tif, stripnum, cbuf,
                                             tmsize_t(cbound));
            if (csize < 0) {
                std::string err = oiio_tiff_last_error();
                errorf("TIFFRead%sStrip failed reading line y=%d,z=%d: %s",
                       read_raw_strips ? "Raw" : "Encoded", y, z,
                       err.size() ? err.c_str() : "unknown error");
                ok = false;
            }
            auto uncompress_etc = [=, &ok](int id) {
                uncompress_one_strip(cbuf, (unsigned long)csize, data,
                                     strip_bytes, this->m_spec.nchannels,
                                     this->m_spec.width, m_rowsperstrip,
                                     m_compression, &ok);
                if (m_photometric == PHOTOMETRIC_MINISWHITE)
                    invert_photometric(stripvals * stripchans, data);
            };
            if (parallelize) {
                // Push the rest of the work onto the thread pool queue
                tasks.push(pool->push(uncompress_etc));
            } else {
                uncompress_etc(0);
            }
            data = (char*)data + strip_bytes * planes;
        }

    } else {
        // One of the cases where we don't bother reading raw, we read
        // encoded strips. Still can be a lot more efficient than reading
        // individual scanlines. This is the clause that has to handle
        // "separate" planarconfig.
        int strips_in_file = (m_spec.height + m_rowsperstrip - 1)
                             / m_rowsperstrip;
        for (size_t stripidx = 0; y + m_rowsperstrip <= yend;
             y += m_rowsperstrip, ++stripidx) {
            for (int c = 0; c < planes; ++c) {
                tstrip_t stripnum = ((y - m_spec.y) / m_rowsperstrip)
                                    + c * strips_in_file;
                tsize_t csize = TIFFReadEncodedStrip(m_tif, stripnum,
                                                     (char*)data
                                                         + c * strip_bytes,
                                                     tmsize_t(strip_bytes));
                if (csize < 0) {
                    std::string err = oiio_tiff_last_error();
                    errorf(
                        "TIFFReadEncodedStrip failed reading line y=%d,z=%d: %s",
                        y, z, err.size() ? err.c_str() : "unknown error");
                    ok = false;
                }
            }
            if (m_photometric == PHOTOMETRIC_MINISWHITE)
                invert_photometric(stripvals * planes, data);
            if (m_separate) {
                // handle "separate" planarconfig: copy to temp area, then
                // separate_to_contig it back.
                char* sepbuf = separate_tmp.get()
                               + stripidx * strip_bytes * planes;
                memcpy(sepbuf, data, strip_bytes * planes);
                separate_to_contig(planes, m_spec.width * m_rowsperstrip,
                                   (unsigned char*)sepbuf,
                                   (unsigned char*)data);
            }
            data = (char*)data + strip_bytes * planes;
        }
    }

    // If we have left over scanlines, read them serially
    m_next_scanline = y;
    for (; y < yend; ++y) {
        bool ok = read_native_scanline(subimage, miplevel, y, z, data);
        if (!ok)
            return false;
        data = (char*)data + ystride;
    }
    tasks.wait();
    return true;
}



bool
TIFFInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                            void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    x -= m_spec.x;
    y -= m_spec.y;

    if (m_use_rgba_interface) {
        // We punted and used the RGBA image interface
        // libtiff has a call to read just one tile as RGBA. So that's all
        // we need to do, not buffer the whole image.
        m_rgbadata.resize(m_spec.tile_pixels() * 4);
        bool ok = TIFFReadRGBATile(m_tif, x, y, &m_rgbadata[0]);
        if (!ok) {
            errorf("Unknown error trying to read TIFF as RGBA");
            return false;
        }
        // Copy, and use stride magic to reverse top-to-bottom
        int tw = std::min(m_spec.tile_width, m_spec.width - x);
        int th = std::min(m_spec.tile_height, m_spec.height - y);
        copy_image(m_spec.nchannels, tw, th, 1,
                   &m_rgbadata[(th - 1) * m_spec.tile_width], m_spec.nchannels,
                   4, -m_spec.tile_width * 4, AutoStride, data,
                   m_spec.nchannels, m_spec.nchannels * m_spec.tile_width,
                   AutoStride);
        return true;
    }

    imagesize_t tile_pixels = m_spec.tile_pixels();
    imagesize_t nvals       = tile_pixels * m_spec.nchannels;
    m_scratch.resize(m_spec.tile_bytes());
    bool no_bit_convert = (m_bitspersample == 8 || m_bitspersample == 16
                           || m_bitspersample == 32);
    if (m_photometric == PHOTOMETRIC_PALETTE) {
        // Convert from palette to RGB
        if (TIFFReadTile(m_tif, &m_scratch[0], x, y, z, 0) < 0) {
            errorf("%s", oiio_tiff_last_error());
            return false;
        }
        palette_to_rgb(tile_pixels, &m_scratch[0], (unsigned char*)data);
    } else {
        // Not palette
        imagesize_t plane_bytes = m_spec.tile_pixels() * m_spec.format.size();
        int planes              = m_separate ? m_spec.nchannels : 1;
        std::vector<unsigned char> scratch2(m_separate ? m_spec.tile_bytes()
                                                       : 0);
        // Where to read?  Directly into user data if no channel shuffling
        // or bit shifting is needed, otherwise into scratch space.
        unsigned char* readbuf = (no_bit_convert && !m_separate)
                                     ? (unsigned char*)data
                                     : &m_scratch[0];
        // Perform the reads.  Note that for contig, planes==1, so it will
        // only do one TIFFReadTile.
        for (int c = 0; c < planes; ++c) /* planes==1 for contig */
            if (TIFFReadTile(m_tif, &readbuf[plane_bytes * c], x, y, z, c)
                < 0) {
                errorf("%s", oiio_tiff_last_error());
                return false;
            }
        if (m_bitspersample < 8) {
            // m_scratch now holds nvals n-bit values, contig or separate
            std::swap(m_scratch, scratch2);
            for (int c = 0; c < planes; ++c) /* planes==1 for contig */
                bit_convert(m_separate ? tile_pixels : nvals,
                            &scratch2[plane_bytes * c], m_bitspersample,
                            m_separate ? &m_scratch[plane_bytes * c]
                                       : (unsigned char*)data + plane_bytes * c,
                            8);
        } else if (m_bitspersample > 8 && m_bitspersample < 16) {
            // m_scratch now holds nvals n-bit values, contig or separate
            std::swap(m_scratch, scratch2);
            for (int c = 0; c < planes; ++c) /* planes==1 for contig */
                bit_convert(m_separate ? tile_pixels : nvals,
                            &scratch2[plane_bytes * c], m_bitspersample,
                            m_separate ? &m_scratch[plane_bytes * c]
                                       : (unsigned char*)data + plane_bytes * c,
                            16);
        }
        if (m_separate) {
            // Convert from separate (RRRGGGBBB) to contiguous (RGBRGBRGB).
            // We know the data is in m_scratch at this point, so
            // contiguize it into the user data area.
            separate_to_contig(planes, tile_pixels, &m_scratch[0],
                               (unsigned char*)data);
        }
    }

    if (m_photometric == PHOTOMETRIC_MINISWHITE)
        invert_photometric(nvals, data);

    return true;
}



bool
TIFFInput::read_native_tiles(int subimage, int miplevel, int xbegin, int xend,
                             int ybegin, int yend, int zbegin, int zend,
                             void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (!m_spec.valid_tile_range(xbegin, xend, ybegin, yend, zbegin, zend))
        return false;

    // If the stars all align properly, use the thread pool to parallelize
    // the decompression. This can give a large speedup (5x or more!)
    // because the zip decompression dwarfs the actual raw I/O. But libtiff
    // is totally serialized, so we can only parallelize by making calls to
    // zlib ourselves and then writing "raw" (compressed) strips. Don't
    // bother trying to handle any of the uncommon cases with strips. This
    // covers most real-world cases.
    thread_pool* pool = default_thread_pool();
    ASSERT(m_spec.tile_depth >= 1);
    size_t ntiles = size_t(
        (xend - xbegin + m_spec.tile_width - 1) / m_spec.tile_width
        * (yend - ybegin + m_spec.tile_height - 1) / m_spec.tile_height
        * (zend - zbegin + m_spec.tile_depth - 1) / m_spec.tile_depth);
    bool parallelize =
        // more than one tile, or no point parallelizing
        ntiles > 1
        // and not palette or cmyk color separated conversions
        && (m_photometric != PHOTOMETRIC_SEPARATED
            && m_photometric != PHOTOMETRIC_PALETTE)
        // no non-multiple-of-8 bits per sample
        && (spec().format.size() * 8 == m_bitspersample)
        // contig planarconfig only (for now?)
        && !m_separate
        // only deflate/zip compression with horizontal predictor
        && m_compression == COMPRESSION_ADOBE_DEFLATE
        && m_predictor == PREDICTOR_HORIZONTAL
        // only uint8, uint16
        && (m_spec.format == TypeUInt8 || m_spec.format == TypeUInt16)
        // No other unusual cases
        && !m_use_rgba_interface
        // only if we're threading and don't enter the thread pool recursively!
        && pool->size() > 1
        && !pool->is_worker()
        // and not if the feature is turned off
        && m_spec.get_int_attribute("tiff:multithread",
                                    OIIO::get_int_attribute("tiff:multithread"));

    // If we're not parallelizing, just call the parent class default
    // implementaiton of read_native_tiles, which will loop over the tiles
    // and read each one individually.
    if (!parallelize) {
        return ImageInput::read_native_tiles(subimage, miplevel, xbegin, xend,
                                             ybegin, yend, zbegin, zend, data);
    }

    // Make room for, and read the raw (still compressed) tiles. As each one
    // is read, kick off the decompress and any other extras, to execute in
    // parallel.
    stride_t pixel_bytes   = (stride_t)m_spec.pixel_bytes(true);
    stride_t tileystride   = pixel_bytes * m_spec.tile_width;
    stride_t tilezstride   = tileystride * m_spec.tile_height;
    stride_t ystride       = (xend - xbegin) * pixel_bytes;
    stride_t zstride       = (yend - ybegin) * ystride;
    imagesize_t tile_bytes = m_spec.tile_bytes(true);
    int tilevals           = m_spec.tile_pixels() * m_spec.nchannels;
    size_t cbound          = compressBound((uLong)tile_bytes);
    std::unique_ptr<char[]> compressed_scratch(new char[cbound * ntiles]);
    std::unique_ptr<char[]> scratch(new char[tile_bytes * ntiles]);
    task_set tasks(pool);
    bool ok = true;  // failed compression will stash a false here

    // Strutil::printf ("Parallel tile case %d %d  %d %d  %d %d\n",
    //                  xbegin, xend, ybegin, yend, zbegin, zend);
    size_t tileidx = 0;
    for (int z = zbegin; z < zend; z += m_spec.tile_depth) {
        for (int y = ybegin; y < yend; y += m_spec.tile_height) {
            for (int x = xbegin; x < xend; x += m_spec.tile_width, ++tileidx) {
                char* cbuf = compressed_scratch.get() + tileidx * cbound;
                char* ubuf = scratch.get() + tileidx * tile_bytes;
                auto csize = TIFFReadRawTile(m_tif, tile_index(x, y, z), cbuf,
                                             tmsize_t(cbound));
                if (csize < 0) {
                    std::string err = oiio_tiff_last_error();
                    errorf(
                        "TIFFReadRawTile failed reading tile x=%d,y=%d,z=%d: %s",
                        x, y, z, err.size() ? err.c_str() : "unknown error");
                    return false;
                }
                // Push the rest of the work onto the thread pool queue
                tasks.push(pool->push([=, &ok](int id) {
                    uncompress_one_strip(cbuf, (unsigned long)csize, ubuf,
                                         tile_bytes, this->m_spec.nchannels,
                                         this->m_spec.tile_width,
                                         this->m_spec.tile_height
                                             * this->m_spec.tile_depth,
                                         m_compression, &ok);
                    if (m_photometric == PHOTOMETRIC_MINISWHITE)
                        invert_photometric(tilevals, ubuf);
                    copy_image(this->m_spec.nchannels, this->m_spec.tile_width,
                               this->m_spec.tile_height,
                               this->m_spec.tile_depth, ubuf,
                               size_t(pixel_bytes), pixel_bytes, tileystride,
                               tilezstride,
                               (char*)data + (z - zbegin) * zstride
                                   + (y - ybegin) * ystride
                                   + (x - xbegin) * pixel_bytes,
                               pixel_bytes, ystride, zstride);
                }));
            }
        }
    }
    return ok;
}



bool
TIFFInput::read_scanline(int y, int z, TypeDesc format, void* data,
                         stride_t xstride)
{
    bool ok = ImageInput::read_scanline(y, z, format, data, xstride);
    if (ok && m_convert_alpha) {
        // If alpha is unassociated and we aren't requested to keep it that
        // way, multiply the colors by alpha per the usual OIIO conventions
        // to deliver associated color & alpha.  Any auto-premultiplication
        // by alpha should happen after we've already done data format
        // conversions. That's why we do it here, rather than in
        // read_native_blah.
        {
            lock_guard lock(m_mutex);
            if (format
                == TypeUnknown)  // unknown means retrieve the native type
                format = m_spec.format;
        }
        OIIO::premult(m_spec.nchannels, m_spec.width, 1, 1, 0 /*chbegin*/,
                      m_spec.nchannels /*chend*/, format, data, xstride,
                      AutoStride, AutoStride, m_spec.alpha_channel,
                      m_spec.z_channel);
    }
    return ok;
}



bool
TIFFInput::read_scanlines(int subimage, int miplevel, int ybegin, int yend,
                          int z, int chbegin, int chend, TypeDesc format,
                          void* data, stride_t xstride, stride_t ystride)
{
    bool ok = ImageInput::read_scanlines(subimage, miplevel, ybegin, yend, z,
                                         chbegin, chend, format, data, xstride,
                                         ystride);
    if (ok && m_convert_alpha) {
        // If alpha is unassociated and we aren't requested to keep it that
        // way, multiply the colors by alpha per the usual OIIO conventions
        // to deliver associated color & alpha.  Any auto-premultiplication
        // by alpha should happen after we've already done data format
        // conversions. That's why we do it here, rather than in
        // read_native_blah.
        int nchannels, alpha_channel, z_channel, width;
        {
            lock_guard lock(m_mutex);
            seek_subimage(subimage, miplevel);
            nchannels     = m_spec.nchannels;
            alpha_channel = m_spec.alpha_channel;
            z_channel     = m_spec.z_channel;
            width         = m_spec.width;
            if (format == TypeUnknown)  // unknown == native type
                format = m_spec.format;
        }
        // NOTE: if the channel range we read doesn't include the alpha
        // channel, we don't have the alpha data to do the premult, so skip
        // this. Pity the hapless soul who tries to read only the first
        // three channels of an RGBA file with unassociated alpha. They will
        // not get the premultiplication they deserve.
        if (alpha_channel >= chbegin && alpha_channel < chend)
            OIIO::premult(nchannels, width, yend - ybegin, 1, chbegin, chend,
                          format, data, xstride, ystride, AutoStride,
                          alpha_channel, z_channel);
    }
    return ok;
}



bool
TIFFInput::read_tile(int x, int y, int z, TypeDesc format, void* data,
                     stride_t xstride, stride_t ystride, stride_t zstride)
{
    bool ok = ImageInput::read_tile(x, y, z, format, data, xstride, ystride,
                                    zstride);
    if (ok && m_convert_alpha) {
        // If alpha is unassociated and we aren't requested to keep it that
        // way, multiply the colors by alpha per the usual OIIO conventions
        // to deliver associated color & alpha.  Any auto-premultiplication
        // by alpha should happen after we've already done data format
        // conversions. That's why we do it here, rather than in
        // read_native_blah.
        {
            lock_guard lock(m_mutex);
            if (format
                == TypeUnknown)  // unknown means retrieve the native type
                format = m_spec.format;
        }
        OIIO::premult(m_spec.nchannels, m_spec.tile_width, m_spec.tile_height,
                      std::max(1, m_spec.tile_depth), 0, m_spec.nchannels,
                      format, data, xstride, ystride, zstride,
                      m_spec.alpha_channel, m_spec.z_channel);
    }
    return ok;
}



bool
TIFFInput::read_tiles(int subimage, int miplevel, int xbegin, int xend,
                      int ybegin, int yend, int zbegin, int zend, int chbegin,
                      int chend, TypeDesc format, void* data, stride_t xstride,
                      stride_t ystride, stride_t zstride)
{
    bool ok = ImageInput::read_tiles(subimage, miplevel, xbegin, xend, ybegin,
                                     yend, zbegin, zend, chbegin, chend, format,
                                     data, xstride, ystride, zstride);
    if (ok && m_convert_alpha) {
        // If alpha is unassociated and we aren't requested to keep it that
        // way, multiply the colors by alpha per the usual OIIO conventions
        // to deliver associated color & alpha.  Any auto-premultiplication
        // by alpha should happen after we've already done data format
        // conversions. That's why we do it here, rather than in
        // read_native_blah.
        int nchannels, alpha_channel, z_channel;
        {
            lock_guard lock(m_mutex);
            seek_subimage(subimage, miplevel);
            nchannels     = m_spec.nchannels;
            alpha_channel = m_spec.alpha_channel;
            z_channel     = m_spec.z_channel;
            if (format == TypeUnknown)  // unknown == native type
                format = m_spec.format;
        }
        // NOTE: if the channel range we read doesn't include the alpha
        // channel, we don't have the alpha data to do the premult, so skip
        // this. Pity the hapless soul who tries to read only the first
        // three channels of an RGBA file with unassociated alpha. They will
        // not get the premultiplication they deserve.
        if (alpha_channel >= chbegin && alpha_channel < chend)
            OIIO::premult(nchannels, xend - xbegin, yend - ybegin,
                          zend - zbegin, chbegin, chend, format, data, xstride,
                          ystride, zstride, alpha_channel, z_channel);
    }
    return ok;
}

OIIO_PLUGIN_NAMESPACE_END
