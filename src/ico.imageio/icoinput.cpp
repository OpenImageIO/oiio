// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../png.imageio/png_pvt.h"
#include "ico.h"

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/typedesc.h>

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace ICO_pvt;

class ICOInput final : public ImageInput {
public:
    ICOInput() { init(); }
    virtual ~ICOInput() { close(); }
    virtual const char* format_name(void) const override { return "ico"; }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override
    {
        lock_guard lock(m_mutex);
        return m_subimage;
    }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    std::string m_filename;            ///< Stash the filename
    FILE* m_file;                      ///< Open image handle
    ico_header m_ico;                  ///< ICO header
    std::vector<unsigned char> m_buf;  ///< Buffer the image pixels
    int m_subimage;                    ///< What subimage are we looking at?
    int m_bpp;                         ///< Bits per pixel
    int m_offset;                      ///< Offset to image data
    int m_subimage_size;               ///< Length (in bytes) of image data
    int m_palette_size;  ///< Number of colours in palette (0 means 256)

    png_structp m_png;     ///< PNG read structure pointer
    png_infop m_info;      ///< PNG image info structure pointer
    int m_color_type;      ///< PNG color model type
    int m_interlace_type;  ///< PNG interlace type
    Imath::Color3f m_bg;   ///< PNG background color

    /// Reset everything to initial state
    ///
    void init()
    {
        m_subimage = -1;
        m_file     = NULL;
        m_png      = NULL;
        m_info     = NULL;
        memset(&m_ico, 0, sizeof(m_ico));
        m_buf.clear();
    }

    /// Helper function: read the image.
    ///
    bool readimg();

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
ico_input_imageio_create()
{
    return new ICOInput;
}

OIIO_EXPORT int ico_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
ico_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT const char* ico_input_extensions[] = { "ico", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
ICOInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    m_file = Filesystem::fopen(name, "rb");
    if (!m_file) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    if (!fread(&m_ico, 1, sizeof(m_ico))) {
        error("Could not read from file");
        close();
        return false;
    }

    if (bigendian()) {
        // ICOs are little endian
        //swap_endian (&m_ico.reserved); // no use flipping, it's 0 anyway
        swap_endian(&m_ico.type);
        swap_endian(&m_ico.count);
    }
    if (m_ico.reserved != 0 || m_ico.type != 1) {
        errorf("File failed ICO header check");
        close();
        return false;
    }

    // default to subimage #0, according to convention
    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
ICOInput::seek_subimage(int subimage, int miplevel)
{
    /*std::cerr << "[ico] seeking subimage " << index << " (current "
              << m_subimage << ") out of " << m_ico.count << "\n";*/
    if (miplevel != 0 || subimage < 0 || subimage >= m_ico.count)
        return false;

    if (subimage == m_subimage) {
        return true;
    }

    // clear the buffer of previous data
    m_buf.clear();

    // deinitialize PNG structs, in case they were used
    if (m_png && m_info)
        PNG_pvt::destroy_read_struct(m_png, m_info);

    m_subimage = subimage;

    // read subimage header
    fseek(m_file, sizeof(ico_header) + m_subimage * sizeof(ico_subimage),
          SEEK_SET);
    ico_subimage subimg;
    if (!fread(&subimg, 1, sizeof(subimg)))
        return false;

    if (bigendian()) {
        // ICOs are little endian
        swap_endian(&subimg.bpp);
        swap_endian(&subimg.width);
        swap_endian(&subimg.height);
        swap_endian(&subimg.len);
        swap_endian(&subimg.ofs);
        swap_endian(&subimg.numColours);
    }

    fseek(m_file, subimg.ofs, SEEK_SET);

    // test for a PNG icon
    char temp[8];
    if (!fread(temp, 1, sizeof(temp)))
        return false;
    if (temp[1] == 'P' && temp[2] == 'N' && temp[3] == 'G') {
        // standard PNG initalization
        if (png_sig_cmp((png_bytep)temp, 0, 7)) {
            errorf("Subimage failed PNG signature check");
            return false;
        }

        //std::cerr << "[ico] creating PNG read struct\n";

        std::string s = PNG_pvt::create_read_struct(m_png, m_info, this);
        if (s.length()) {
            errorf("%s", s);
            return false;
        }

        //std::cerr << "[ico] reading PNG info\n";

        png_init_io(m_png, m_file);
        png_set_sig_bytes(m_png, 8);  // already read 8 bytes

        PNG_pvt::read_info(m_png, m_info, m_bpp, m_color_type, m_interlace_type,
                           m_bg, m_spec, true);

        m_spec.attribute("oiio:BitsPerSample", m_bpp / m_spec.nchannels);

        return true;
    }

    // otherwise it's a plain, ol' windoze DIB (device-independent bitmap)
    // roll back to where we began and read in the DIB header
    fseek(m_file, subimg.ofs, SEEK_SET);

    ico_bitmapinfo bmi;
    if (!fread(&bmi, 1, sizeof(bmi)))
        return false;
    if (bigendian()) {
        // ICOs are little endian
        // according to MSDN, only these are valid in an ICO DIB header
        swap_endian(&bmi.size);
        swap_endian(&bmi.bpp);
        swap_endian(&bmi.width);
        swap_endian(&bmi.height);
        swap_endian(&bmi.len);
    }

    /*std::cerr << "[ico] " << (int)subimg.width << "x" << (int)subimg.height << "@"
              << (int)bmi.bpp << " (subimg len=" << (int)subimg.len << ", bm len="
              << (int)bmi.len << ", ofs=" << (int)subimg.ofs << "), c#"
              << (int)subimg.numColours << ", p#" << (int)subimg.planes << ":"
              << (int)bmi.planes << "\n";*/

    // copy off values for later use
    m_bpp = bmi.bpp;
    // some sanity checking
    if (m_bpp != 1 && m_bpp != 4
        && m_bpp != 8
        /*&& m_bpp != 16*/
        && m_bpp != 24 && m_bpp != 32) {
        errorf("Unsupported image color depth, probably corrupt file");
        return false;
    }
    m_offset        = subimg.ofs;
    m_subimage_size = subimg.len;
    // palette size of 0 actually indicates 256 colours
    m_palette_size = (subimg.numColours == 0 && m_bpp < 16)
                         ? 256
                         : (int)subimg.numColours;

    m_spec = ImageSpec((int)subimg.width, (int)subimg.height,
                       4,                 // always RGBA
                       TypeDesc::UINT8);  // 4- and 16-bit are expanded to 8bpp
    m_spec.default_channel_names();
    // add 1 bit for < 32bpp images due to the 1-bit alpha mask
    m_spec.attribute("oiio:BitsPerSample",
                     m_bpp / m_spec.nchannels + (m_bpp == 32 ? 0 : 1));

    /*std::cerr << "[ico] expected bytes: scanline " << m_spec.scanline_bytes()
              << ", image " << m_spec.image_bytes() << "\n";*/

    return true;
}



bool
ICOInput::readimg()
{
    if (m_png) {
        // subimage is a PNG
        std::string s = PNG_pvt::read_into_buffer(m_png, m_info, m_spec, m_bpp,
                                                  m_color_type, m_buf);

        //std::cerr << "[ico] PNG buffer size = " << m_buf.size () << "\n";

        if (s.length()) {
            errorf("%s", s);
            return false;
        }

        return true;
    }

    // otherwise we're dealing with a DIB
    DASSERT(m_spec.scanline_bytes() == ((size_t)m_spec.width * 4));
    m_buf.resize(m_spec.image_bytes());

    //std::cerr << "[ico] DIB buffer size = " << m_buf.size () << "\n";

    // icons < 16bpp are colour-indexed, so load the palette
    // a palette consists of 4-byte BGR quads, with the last byte unused (reserved)
    std::vector<ico_palette_entry> palette(m_palette_size);
    if (m_bpp < 16) {  // >= 16-bit icons are unpaletted
        for (int i = 0; i < m_palette_size; i++)
            if (!fread(&palette[i], 1, sizeof(ico_palette_entry)))
                return false;
    }

    // read the colour data (the 1-bit transparency is added later on)
    // scanline length in bytes (aligned to a multiple of 32 bits)
    int slb = (m_spec.width * m_bpp + 7) / 8  // real data bytes
              + (4 - ((m_spec.width * m_bpp + 7) / 8) % 4) % 4;  // padding
    std::vector<unsigned char> scanline(slb);
    ico_palette_entry* pe;
    int k;
    for (int y = m_spec.height - 1; y >= 0; y--) {
        if (!fread(&scanline[0], 1, slb))
            return false;
        for (int x = 0; x < m_spec.width; x++) {
            k = y * m_spec.width * 4 + x * 4;
            // fill the buffer
            switch (m_bpp) {
            case 1:
                pe = &palette[(scanline[x / 8] & (1 << (7 - x % 8))) != 0];
                m_buf[k + 0] = pe->r;
                m_buf[k + 1] = pe->g;
                m_buf[k + 2] = pe->b;
                break;
            case 4:
                pe           = &palette[(scanline[x / 2] & 0xF0) >> 4];
                m_buf[k + 0] = pe->r;
                m_buf[k + 1] = pe->g;
                m_buf[k + 2] = pe->b;
                // 2 pixels per byte
                pe = &palette[scanline[x / 2] & 0x0F];
                if (x == m_spec.width - 1)
                    break;  // avoid buffer overflows
                x++;
                m_buf[k + 4] = pe->r;
                m_buf[k + 5] = pe->g;
                m_buf[k + 6] = pe->b;
                /*std::cerr << "[ico] " << y << " 2*4bit pixel: "
                          << ((int)scanline[x / 2]) << " -> "
                          << ((int)(scanline[x / 2] & 0xF0) >> 4)
                          << " & " << ((int)(scanline[x / 2]) & 0x0F)
                          << "\n";*/
                break;
            case 8:
                pe           = &palette[scanline[x]];
                m_buf[k + 0] = pe->r;
                m_buf[k + 1] = pe->g;
                m_buf[k + 2] = pe->b;
                break;
                // bpp values > 8 mean non-indexed BGR(A) images
#if 0
            // doesn't seem like ICOs can really be 16-bit, where did I even get
            // this notion from?
            case 16:
                // FIXME: find out exactly which channel gets the 1 extra
                // bit; currently I assume it's green: 5B, 6G, 5R
                // extract and shift the bits
                m_buf[k + 0] = (scanline[x * 2 + 1] & 0x1F) << 3;
                m_buf[k + 1] = ((scanline[x * 2 + 1] & 0xE0) >> 3)
                               | ((scanline[x * 2 + 0] & 0x07) << 5);
                m_buf[k + 2] = scanline[x * 2 + 0] & 0xF8;
                break;
#endif
            case 24:
                m_buf[k + 0] = scanline[x * 3 + 2];
                m_buf[k + 1] = scanline[x * 3 + 1];
                m_buf[k + 2] = scanline[x * 3 + 0];
                break;
            case 32:
                m_buf[k + 0] = scanline[x * 4 + 2];
                m_buf[k + 1] = scanline[x * 4 + 1];
                m_buf[k + 2] = scanline[x * 4 + 0];
                m_buf[k + 3] = scanline[x * 4 + 3];
                break;
            }
        }
    }

    // read the 1-bit transparency for < 32-bit icons
    if (m_bpp < 32) {
        // also aligned to a multiple of 32 bits
        slb = (m_spec.width + 7) / 8                     // real data bytes
              + (4 - ((m_spec.width + 7) / 8) % 4) % 4;  // padding
        scanline.resize(slb);
        for (int y = m_spec.height - 1; y >= 0; y--) {
            if (!fread(&scanline[0], 1, slb))
                return false;
            for (int x = 0; x < m_spec.width; x += 8) {
                for (int b = 0; b < 8; b++) {  // bit
                    k = y * m_spec.width * 4 + (x + 7 - b) * 4;
                    if (scanline[x / 8] & (1 << b))
                        m_buf[k + 3] = 0;
                    else
                        m_buf[k + 3] = 255;
                }
            }
        }
    }

    return true;
}



bool
ICOInput::close()
{
    if (m_png && m_info) {
        PNG_pvt::destroy_read_struct(m_png, m_info);
        m_png  = nullptr;
        m_info = nullptr;
    }
    if (m_file) {
        fclose(m_file);
        m_file = NULL;
    }

    init();  // Reset to initial state
    return true;
}



bool
ICOInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (m_buf.empty()) {
        if (!readimg())
            return false;
    }

    size_t size = spec().scanline_bytes();
    //std::cerr << "[ico] reading scanline " << y << " (" << size << " bytes)\n";
    memcpy(data, &m_buf[y * size], size);
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
