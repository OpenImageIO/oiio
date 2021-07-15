// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/typedesc.h>

#include "targa_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace TGA_pvt;


class TGAInput final : public ImageInput {
public:
    TGAInput() { init(); }
    virtual ~TGAInput() { close(); }
    virtual const char* format_name(void) const override { return "targa"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "thumbnail");
    }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool get_thumbnail(ImageBuf& thumb, int subimage) override;

private:
    std::string m_filename;            ///< Stash the filename
    FILE* m_file;                      ///< Open image handle
    tga_header m_tga;                  ///< Targa header
    tga_footer m_foot;                 ///< Targa 2.0 footer
    tga_alpha_type m_alpha_type;       ///< Alpha type
    int64_t m_ofs_thumb       = 0;     ///< Offset of thumbnail info
    int64_t m_ofs_palette     = 0;     ///< Offset of palette
    int64_t m_ofs_colcorr_tbl = 0;     ///< Offset to colour correction table
    bool m_keep_unassociated_alpha;    ///< Do not convert unassociated alpha
    std::vector<unsigned char> m_buf;  ///< Buffer the image pixels

    /// Reset everything to initial state
    ///
    void init()
    {
        m_file = NULL;
        m_buf.clear();
        m_ofs_thumb               = 0;
        m_ofs_palette             = 0;
        m_ofs_colcorr_tbl         = 0;
        m_alpha_type              = TGA_ALPHA_NONE;
        m_keep_unassociated_alpha = false;
    }

    /// Helper function: read the image.
    ///
    bool readimg();

    /// Helper function: decode a pixel.
    inline void decode_pixel(unsigned char* in, unsigned char* out,
                             unsigned char* palette, int bytespp,
                             int palbytespp);

    /// Helper: read, with error detection
    ///
    bool fread(void* buf, size_t itemsize, size_t nitems)
    {
        size_t n = ::fread(buf, itemsize, nitems, m_file);
        if (n != nitems)
            errorf("Read error");
        return n == nitems;
    }
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
targa_input_imageio_create()
{
    return new TGAInput;
}

OIIO_EXPORT int targa_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char* targa_input_extensions[] = { "tga", "tpic", nullptr };

OIIO_EXPORT const char*
targa_imageio_library_version()
{
    return nullptr;
}

OIIO_PLUGIN_EXPORTS_END



bool
TGAInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    m_file = Filesystem::fopen(name, "rb");
    if (!m_file) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    // due to struct packing, we may get a corrupt header if we just load the
    // struct from file; to address that, read every member individually
    // save some typing
#define RH(memb)                                    \
    if (!fread(&m_tga.memb, sizeof(m_tga.memb), 1)) \
    return false

    RH(idlen);
    RH(cmap_type);
    RH(type);
    RH(cmap_first);
    RH(cmap_length);
    RH(cmap_size);
    RH(x_origin);
    RH(y_origin);
    RH(width);
    RH(height);
    RH(bpp);
    RH(attr);
#undef RH
    if (bigendian()) {
        // TGAs are little-endian
        swap_endian(&m_tga.idlen);
        swap_endian(&m_tga.cmap_type);
        swap_endian(&m_tga.type);
        swap_endian(&m_tga.cmap_first);
        swap_endian(&m_tga.cmap_length);
        swap_endian(&m_tga.cmap_size);
        swap_endian(&m_tga.x_origin);
        swap_endian(&m_tga.y_origin);
        swap_endian(&m_tga.width);
        swap_endian(&m_tga.height);
        swap_endian(&m_tga.bpp);
        swap_endian(&m_tga.attr);
    }

    if (m_tga.bpp != 8 && m_tga.bpp != 15 && m_tga.bpp != 16 && m_tga.bpp != 24
        && m_tga.bpp != 32) {
        errorf("Illegal pixel size: %d bits per pixel", m_tga.bpp);
        return false;
    }

    if (m_tga.type == TYPE_NODATA) {
        errorf("Image with no data");
        return false;
    }
    if (m_tga.type != TYPE_PALETTED && m_tga.type != TYPE_RGB
        && m_tga.type != TYPE_GRAY && m_tga.type != TYPE_PALETTED_RLE
        && m_tga.type != TYPE_RGB_RLE && m_tga.type != TYPE_GRAY_RLE) {
        errorf("Illegal image type: %d", m_tga.type);
        return false;
    }

    if (m_tga.cmap_type
        && (m_tga.type == TYPE_GRAY || m_tga.type == TYPE_GRAY_RLE)) {
        // it should be an error for TYPE_RGB* as well, but apparently some
        // *very* old TGAs can be this way, so we'll hack around it
        errorf("Palette defined for grayscale image");
        return false;
    }

    if (m_tga.cmap_type
        && (m_tga.cmap_size != 15 && m_tga.cmap_size != 16
            && m_tga.cmap_size != 24 && m_tga.cmap_size != 32)) {
        errorf("Illegal palette entry size: %d bits", m_tga.cmap_size);
        return false;
    }

    m_alpha_type = TGA_ALPHA_NONE;
    if (((m_tga.type == TYPE_RGB || m_tga.type == TYPE_RGB_RLE)
         && m_tga.bpp == 32)
        || ((m_tga.type == TYPE_GRAY || m_tga.type == TYPE_GRAY_RLE)
            && m_tga.bpp > 8)) {
        m_alpha_type = (m_tga.attr & 0x08) > 0 ? TGA_ALPHA_USEFUL
                                               : TGA_ALPHA_NONE;
    }


    m_spec = ImageSpec(
        (int)m_tga.width, (int)m_tga.height,
        // colour channels
        ((m_tga.type == TYPE_GRAY || m_tga.type == TYPE_GRAY_RLE) ? 1 : 3)
            // have we got alpha?
            + (m_tga.bpp == 32 || m_alpha_type >= TGA_ALPHA_UNDEFINED_RETAIN),
        TypeDesc::UINT8);
    m_spec.attribute("oiio:BitsPerSample", m_tga.bpp / m_spec.nchannels);
    m_spec.default_channel_names();
#if 0  // no one seems to adhere to this part of the spec...
    if (m_tga.attr & FLAG_X_FLIP)
        m_spec.x = m_spec.width - m_tga.x_origin - 1;
    else
        m_spec.x = m_tga.x_origin;
    if (m_tga.attr & FLAG_Y_FLIP)
        m_spec.y = m_tga.y_origin;
    else
        m_spec.y = m_spec.width - m_tga.y_origin - 1;
#endif
    if (m_tga.type >= TYPE_PALETTED_RLE)
        m_spec.attribute("compression", "rle");

    /*std::cerr << "[tga] " << m_tga.width << "x" << m_tga.height << "@"
              << (int)m_tga.bpp << " (" << m_spec.nchannels
              << ") type " << (int)m_tga.type << "\n";*/

    // load image ID
    if (m_tga.idlen) {
        // TGA comments can be at most 255 bytes long, but we add 1 extra byte
        // in case the comment lacks null termination
        char id[256];
        memset(id, 0, sizeof(id));
        if (!fread(id, m_tga.idlen, 1))
            return false;
        m_spec.attribute("targa:ImageID", id);
    }

    int64_t ofs   = Filesystem::ftell(m_file);
    m_ofs_palette = ofs;

    // now try and see if it's a TGA 2.0 image
    // TGA 2.0 files are identified by a nifty "TRUEVISION-XFILE.\0" signature
    Filesystem::fseek(m_file, -26, SEEK_END);
    if (fread(&m_foot.ofs_ext, sizeof(m_foot.ofs_ext), 1)
        && fread(&m_foot.ofs_dev, sizeof(m_foot.ofs_dev), 1)
        && fread(&m_foot.signature, sizeof(m_foot.signature), 1)
        && !strncmp(m_foot.signature, "TRUEVISION-XFILE.", 17)) {
        //std::cerr << "[tga] this is a TGA 2.0 file\n";
        if (bigendian()) {
            swap_endian(&m_foot.ofs_ext);
            swap_endian(&m_foot.ofs_dev);
        }

        // read the extension area
        Filesystem::fseek(m_file, m_foot.ofs_ext, SEEK_SET);
        // check if this is a TGA 2.0 extension area
        // according to the 2.0 spec, the size for valid 2.0 files is exactly
        // 495 bytes, and the reader should only read as much as it understands
        // for < 495, we ignore this section of the file altogether
        // for > 495, we only read what we know
        uint16_t s;
        if (!fread(&s, 2, 1))
            return false;
        if (bigendian())
            swap_endian(&s);
        //std::cerr << "[tga] extension area size: " << s << "\n";
        if (s >= 495) {
            union {
                unsigned char c[324];  // so as to accommodate the comments
                uint16_t s[6];
                uint32_t l;
            } buf;

            // load image author
            if (!fread(buf.c, 41, 1))
                return false;
            if (buf.c[0])
                m_spec.attribute("Artist", (char*)buf.c);

            // load image comments
            if (!fread(buf.c, 324, 1))
                return false;

            // concatenate the lines into a single string
            std::string tmpstr((const char*)buf.c);
            if (buf.c[81]) {
                tmpstr += "\n";
                tmpstr += (const char*)&buf.c[81];
            }
            if (buf.c[162]) {
                tmpstr += "\n";
                tmpstr += (const char*)&buf.c[162];
            }
            if (buf.c[243]) {
                tmpstr += "\n";
                tmpstr += (const char*)&buf.c[243];
            }
            if (tmpstr.length() > 0)
                m_spec.attribute("ImageDescription", tmpstr);

            // timestamp
            if (!fread(buf.s, 2, 6))
                return false;
            if (buf.s[0] || buf.s[1] || buf.s[2] || buf.s[3] || buf.s[4]
                || buf.s[5]) {
                if (bigendian())
                    swap_endian(&buf.s[0], 6);
                sprintf((char*)&buf.c[12], "%04u:%02u:%02u %02u:%02u:%02u",
                        buf.s[2], buf.s[0], buf.s[1], buf.s[3], buf.s[4],
                        buf.s[5]);
                m_spec.attribute("DateTime", (char*)&buf.c[12]);
            }

            // job name/ID
            if (!fread(buf.c, 41, 1))
                return false;
            if (buf.c[0])
                m_spec.attribute("DocumentName", (char*)buf.c);

            // job time
            if (!fread(buf.s, 2, 3))
                return false;
            if (buf.s[0] || buf.s[1] || buf.s[2]) {
                if (bigendian())
                    swap_endian(&buf.s[0], 3);
                sprintf((char*)&buf.c[6], "%u:%02u:%02u", buf.s[0], buf.s[1],
                        buf.s[2]);
                m_spec.attribute("targa:JobTime", (char*)&buf.c[6]);
            }

            // software
            if (!fread(buf.c, 41, 1))
                return false;
            if (buf.c[0]) {
                // tack on the version number and letter
                uint16_t n;
                char l;
                if (!fread(&n, 2, 1) || !fread(&l, 1, 1))
                    return false;
                sprintf((char*)&buf.c[strlen((char*)buf.c)], " %u.%u%c",
                        n / 100, n % 100, l != ' ' ? l : 0);
                m_spec.attribute("Software", (char*)buf.c);
            }

            // background (key) colour
            if (!fread(buf.c, 4, 1))
                return false;
            // FIXME: what do we do with it?

            // aspect ratio
            if (!fread(buf.s, 2, 2))
                return false;
            // if the denominator is zero, it's unused
            if (buf.s[1]) {
                if (bigendian())
                    swap_endian(&buf.s[0], 2);
                m_spec.attribute("PixelAspectRatio",
                                 (float)buf.s[0] / (float)buf.s[1]);
            }

            // gamma
            if (!fread(buf.s, 2, 2))
                return false;
            // if the denominator is zero, it's unused
            if (buf.s[1]) {
                if (bigendian())
                    swap_endian(&buf.s[0], 2);
                float gamma = (float)buf.s[0] / (float)buf.s[1];
                // Round gamma to the nearest hundredth to prevent stupid
                // precision choices and make it easier for apps to make
                // decisions based on known gamma values. For example, you want
                // 2.2, not 2.19998.
                gamma = roundf(100.0 * gamma) / 100.0f;
                if (gamma == 1.f) {
                    m_spec.attribute("oiio:ColorSpace", "linear");
                } else {
                    m_spec.attribute("oiio:ColorSpace",
                                     Strutil::sprintf("GammaCorrected%.2g",
                                                      gamma));
                    m_spec.attribute("oiio:Gamma", gamma);
                }
            }

            // offset to colour correction table
            if (!fread(&buf.l, 4, 1))
                return false;
            if (bigendian())
                swap_endian(&buf.l);
            m_ofs_colcorr_tbl = buf.l;
            /*std::cerr << "[tga] colour correction table offset: "
                      << (int)m_ofs_colcorr_tbl << "\n";*/

            // offset to thumbnail
            if (!fread(&buf.l, 4, 1))
                return false;
            if (bigendian())
                swap_endian(&buf.l);
            m_ofs_thumb = buf.l;

            // offset to scan-line table
            if (!fread(&buf.l, 4, 1))
                return false;
            // TODO: can we find any use for this? we can't advertise random
            // access anyway, because not all RLE-compressed files will have
            // this table

            // alpha type
            if (!fread(buf.c, 1, 1))
                return false;
            m_alpha_type = (tga_alpha_type)buf.c[0];
            if (m_alpha_type)
                m_spec.attribute("tga:alpha_type", m_alpha_type);

            // Check for presence of a thumbnail and set the metadata that
            // says its dimensions, but don't read and decode it unless
            // thumbnail() is called.
            if (m_ofs_thumb > 0) {
                Filesystem::fseek(m_file, m_ofs_thumb, SEEK_SET);
                // Read the thumbnail dimensions -- sometimes it's 0x0 to
                // indicate no thumbnail.
                unsigned char res[2];
                if (fread(&res, 2, 1) && res[0] > 0 && res[1] > 0) {
                    m_spec.attribute("thumbnail_width", (int)res[0]);
                    m_spec.attribute("thumbnail_height", (int)res[1]);
                    m_spec.attribute("thumbnail_nchannels", m_spec.nchannels);
                }
            }
        }
        // FIXME: provide access to the developer area; according to Larry,
        // it's probably safe to ignore it altogether until someone complains
        // that it's missing :)
    }

    if (m_spec.alpha_channel != -1 && m_alpha_type == TGA_ALPHA_USEFUL)
        if (m_keep_unassociated_alpha)
            m_spec.attribute("oiio:UnassociatedAlpha", 1);

    // Reposition back to where the palette starts
    Filesystem::fseek(m_file, ofs, SEEK_SET);

    newspec = spec();
    return true;
}



bool
TGAInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    // Check 'config' for any special requests
    if (config.get_int_attribute("oiio:UnassociatedAlpha", 0) == 1)
        m_keep_unassociated_alpha = true;
    return open(name, newspec);
}



bool
TGAInput::get_thumbnail(ImageBuf& thumb, int subimage)
{
    if (m_ofs_thumb <= 0)
        return false;  // no thumbnail info

    lock_guard lock(*this);
    bool result         = false;
    int64_t save_offset = Filesystem::ftell(m_file);

    Filesystem::fseek(m_file, m_ofs_thumb, SEEK_SET);

    // Read the thumbnail dimensions -- sometimes it's 0x0 to indicate no
    // thumbnail.
    unsigned char res[2];
    if (fread(&res, 2, 1) && res[0] > 0 && res[1] > 0) {
        // Most of this code is a dupe of readimg(); according to the spec,
        // the thumbnail is in the same format as the main image but
        // uncompressed.
        ImageSpec thumbspec(res[0], res[1], m_spec.nchannels, TypeUInt8);
        thumbspec.attribute("oiio:ColorSpace", "sRGB");
        thumb.reset(thumbspec);
        int bytespp    = (m_tga.bpp == 15) ? 2 : (m_tga.bpp / 8);
        int palbytespp = (m_tga.cmap_size == 15) ? 2 : (m_tga.cmap_size / 8);
        int alphabits  = m_tga.attr & 0x0F;
        if (alphabits == 0 && m_tga.bpp == 32)
            alphabits = 8;
        // read palette, if there is any
        std::unique_ptr<unsigned char[]> palette;
        if (m_tga.cmap_type) {
            Filesystem::fseek(m_file, m_ofs_palette, SEEK_SET);
            palette.reset(new unsigned char[palbytespp * m_tga.cmap_length]);
            if (!fread(palette.get(), palbytespp, m_tga.cmap_length))
                return false;
            Filesystem::fseek(m_file, m_ofs_thumb + 2, SEEK_SET);
        }
        // load pixel data
        unsigned char pixel[4];
        unsigned char in[4];
        for (int64_t y = thumbspec.height - 1; y >= 0; y--) {
            char* img = (char*)thumb.pixeladdr(0, y);
            for (int64_t x = 0; x < thumbspec.width;
                 x++, img += m_spec.nchannels) {
                if (!fread(in, bytespp, 1))
                    return false;
                decode_pixel(in, pixel, palette.get(), bytespp, palbytespp);
                memcpy(img, pixel, m_spec.nchannels);
            }
        }
        result = true;
    }

    Filesystem::fseek(m_file, save_offset, SEEK_SET);
    return result;
}



inline void
TGAInput::decode_pixel(unsigned char* in, unsigned char* out,
                       unsigned char* palette, int bytespp, int palbytespp)
{
    unsigned int k = 0;
    // I hate nested switches...
    switch (m_tga.type) {
    case TYPE_PALETTED:
    case TYPE_PALETTED_RLE:
        for (int i = 0; i < bytespp; ++i)
            k |= in[i] << (8 * i);  // Assemble it in little endian order
        k = (m_tga.cmap_first + k) * palbytespp;
        switch (palbytespp) {
        case 2:
            // see the comment for 16bpp RGB below for an explanation of this
            out[0] = bit_range_convert<5, 8>((palette[k + 1] & 0x7C) >> 2);
            out[1] = bit_range_convert<5, 8>(((palette[k + 0] & 0xE0) >> 5)
                                             | ((palette[k + 1] & 0x03) << 3));
            out[2] = bit_range_convert<5, 8>(palette[k + 0] & 0x1F);
            break;
        case 3:
            out[0] = palette[k + 2];
            out[1] = palette[k + 1];
            out[2] = palette[k + 0];
            break;
        case 4:
            out[0] = palette[k + 2];
            out[1] = palette[k + 1];
            out[2] = palette[k + 0];
            out[3] = palette[k + 3];
            break;
        }
        break;
    case TYPE_RGB:
    case TYPE_RGB_RLE:
        switch (bytespp) {
        case 2:
            // This format is pretty funky. It's a 1A-5R-5G-5B layout,
            // with the first bit alpha (or unused if only 3 channels),
            // but thanks to the little-endianness, the order is pretty
            // bizarre. The bits are non-contiguous, so we have to
            // extract the relevant ones and synthesize the colour
            // values from the two bytes.
            // NOTE: This way of handling the pixel as two independent bytes
            // (as opposed to a single short int) makes it independent from
            // endianness.
            // Here's what the layout looks like:
            // MSb       unused   LSb
            //  v           v      v
            //  GGGBBBBB     RRRRRGG
            // [||||||||]  [||||||||]
            // While red and blue channels are quite self-explanatory, the
            // green one needs a few words. The 5 bits are composed of the
            // 2 from the second byte as the more significant and the 3 from
            // the first one as the less significant ones.

            // extract the bits to valid 5-bit integers and expand to full range
            out[0] = bit_range_convert<5, 8>((in[1] & 0x7C) >> 2);
            out[1] = bit_range_convert<5, 8>(((in[0] & 0xE0) >> 5)
                                             | ((in[1] & 0x03) << 3));
            out[2] = bit_range_convert<5, 8>(in[0] & 0x1F);
            if (m_spec.nchannels > 3)
                out[3] = (in[0] & 0x80) ? 255 : 0;
            break;
        case 3:
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            break;
        case 4:
            out[0] = in[2];
            out[1] = in[1];
            out[2] = in[0];
            out[3] = in[3];
            break;
        }
        break;
    case TYPE_GRAY:
    case TYPE_GRAY_RLE:
        if (bigendian()) {
            for (int i = bytespp - 1; i >= 0; i--)
                out[i] = in[bytespp - i - 1];
        } else
            memcpy(out, in, bytespp);
        break;
    }
}



template<class T>
static void
associateAlpha(T* data, int64_t size, int channels, int alpha_channel,
               float gamma)
{
    T max = std::numeric_limits<T>::max();
    if (gamma == 1) {
        for (int64_t x = 0; x < size; ++x, data += channels)
            for (int c = 0; c < channels; c++)
                if (c != alpha_channel) {
                    unsigned int f = data[c];
                    data[c]        = (f * data[alpha_channel]) / max;
                }
    } else {  //With gamma correction
        float inv_max = 1.0 / max;
        for (int64_t x = 0; x < size; ++x, data += channels) {
            float alpha_associate
                = OIIO::fast_pow_pos(data[alpha_channel] * inv_max, gamma);
            // We need to transform to linear space, associate the alpha, and
            // then transform back.  That is, if D = data[c], we want
            //
            // D' = max * ( (D/max)^(1/gamma) * (alpha/max) ) ^ gamma
            //
            // This happens to simplify to something which looks like
            // multiplying by a nonlinear alpha:
            //
            // D' = D * (alpha/max)^gamma
            for (int c = 0; c < channels; c++)
                if (c != alpha_channel)
                    data[c] = static_cast<T>(data[c] * alpha_associate);
        }
    }
}



bool
TGAInput::readimg()
{
    // how many bytes we actually read
    // for 15-bit read 2 bytes and ignore the 16th bit
    int bytespp    = (m_tga.bpp == 15) ? 2 : (m_tga.bpp / 8);
    int palbytespp = (m_tga.cmap_size == 15) ? 2 : (m_tga.cmap_size / 8);
    int alphabits  = m_tga.attr & 0x0F;
    if (alphabits == 0 && m_tga.bpp == 32)
        alphabits = 8;

    /*std::cerr << "[tga] bytespp = " << bytespp
              << " palbytespp = " << palbytespp
              << " alphabits = " << alphabits
              << "\n";*/

    m_buf.resize(m_spec.image_bytes());

    // read palette, if there is any
    unsigned char* palette = NULL;
    if (m_tga.cmap_type) {
        palette = new unsigned char[palbytespp * m_tga.cmap_length];
        if (!fread(palette, palbytespp, m_tga.cmap_length))
            return false;
    }

    unsigned char pixel[4];
    if (m_tga.type < TYPE_PALETTED_RLE) {
        // uncompressed image data
        unsigned char in[4];
        for (int64_t y = m_spec.height - 1; y >= 0; y--) {
            for (int64_t x = 0; x < m_spec.width; x++) {
                if (!fread(in, bytespp, 1))
                    return false;
                decode_pixel(in, pixel, palette, bytespp, palbytespp);
                memcpy(&m_buf[y * m_spec.width * m_spec.nchannels
                              + x * m_spec.nchannels],
                       pixel, m_spec.nchannels);
            }
        }
    } else {
        // Run Length Encoded image
        unsigned char in[5];
        int packet_size;
        for (int64_t y = m_spec.height - 1; y >= 0; y--) {
            for (int64_t x = 0; x < m_spec.width; x++) {
                if (!fread(in, 1 + bytespp, 1))
                    return false;
                packet_size = 1 + (in[0] & 0x7f);
                decode_pixel(&in[1], pixel, palette, bytespp, palbytespp);
                if (in[0] & 0x80) {  // run length packet
                    /*std::cerr << "[tga] run length packet "
                              << packet_size << "\n";*/
                    for (int i = 0; i < packet_size; i++) {
                        memcpy(&m_buf[y * m_spec.width * m_spec.nchannels
                                      + x * m_spec.nchannels],
                               pixel, m_spec.nchannels);
                        if (i < packet_size - 1) {
                            x++;
                            if (x >= m_spec.width) {
                                // run spans across multiple scanlines
                                x = 0;
                                if (y > 0)
                                    y--;
                                else
                                    goto loop_break;
                            }
                        }
                    }
                } else {  // non-rle packet
                    /*std::cerr << "[tga] non-run length packet "
                              << packet_size << "\n";*/
                    for (int i = 0; i < packet_size; i++) {
                        memcpy(&m_buf[y * m_spec.width * m_spec.nchannels
                                      + x * m_spec.nchannels],
                               pixel, m_spec.nchannels);
                        if (i < packet_size - 1) {
                            x++;
                            if (x >= m_spec.width) {
                                // run spans across multiple scanlines
                                x = 0;
                                if (y > 0)
                                    y--;
                                else
                                    goto loop_break;
                            }
                            // skip the packet header byte
                            if (!fread(&in[1], bytespp, 1))
                                return false;
                            decode_pixel(&in[1], pixel, palette, bytespp,
                                         palbytespp);
                        }
                    }
                }
            }
        loop_break:;
        }
    }

    delete[] palette;

    // flip the image, if necessary
    if (m_tga.cmap_type)
        bytespp = palbytespp;
    // Y-flipping is now done in read_native_scanline instead
    /*if (m_tga.attr & FLAG_Y_FLIP) {
        //std::cerr << "[tga] y flipping\n";

        std::vector<unsigned char> flip (m_spec.width * bytespp);
        unsigned char *src, *dst, *tmp = &flip[0];
        for (int y = 0; y < m_spec.height / 2; y++) {
            src = &m_buf[(m_spec.height - y - 1) * m_spec.width * bytespp];
            dst = &m_buf[y * m_spec.width * bytespp];

            memcpy(tmp, src, m_spec.width * bytespp);
            memcpy(src, dst, m_spec.width * bytespp);
            memcpy(dst, tmp, m_spec.width * bytespp);
        }
    }*/
    if (m_tga.attr & FLAG_X_FLIP) {
        //std::cerr << "[tga] x flipping\n";

        std::vector<unsigned char> flip(bytespp * m_spec.width / 2);
        unsigned char *src, *dst, *tmp = &flip[0];
        for (int64_t y = 0; y < m_spec.height; y++) {
            src = &m_buf[y * m_spec.width * bytespp];
            dst = &m_buf[(y * m_spec.width + m_spec.width / 2) * bytespp];

            memcpy(tmp, src, bytespp * m_spec.width / 2);
            memcpy(src, dst, bytespp * m_spec.width / 2);
            memcpy(dst, tmp, bytespp * m_spec.width / 2);
        }
    }

    if (m_alpha_type != TGA_ALPHA_PREMULTIPLIED) {
        // Convert to associated unless we were requested not to do so
        if (m_spec.alpha_channel != -1 && !m_keep_unassociated_alpha) {
            int64_t size = m_spec.image_pixels();
            float gamma  = m_spec.get_float_attribute("oiio:Gamma", 1.0f);

            associateAlpha((unsigned char*)m_buf.data(), size, m_spec.nchannels,
                           m_spec.alpha_channel, gamma);
        }
    }

    return true;
}



bool
TGAInput::close()
{
    if (m_file) {
        fclose(m_file);
        m_file = NULL;
    }

    init();  // Reset to initial state
    return true;
}



bool
TGAInput::read_native_scanline(int subimage, int miplevel, int y, int /*z*/,
                               void* data)
{
    lock_guard lock(*this);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (m_buf.empty())
        readimg();

    if (m_tga.attr & FLAG_Y_FLIP)
        y = m_spec.height - y - 1;
    size_t size = spec().scanline_bytes();
    memcpy(data, m_buf.data() + y * size, size);
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
