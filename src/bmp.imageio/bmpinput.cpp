// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md
#include "bmp_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace bmp_pvt;

// Obligatory material to make this a recognizeable imageio plugin
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int bmp_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
bmp_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT ImageInput*
bmp_input_imageio_create()
{
    return new BmpInput;
}

OIIO_EXPORT const char* bmp_input_extensions[] = { "bmp", nullptr };

OIIO_PLUGIN_EXPORTS_END


bool
BmpInput::valid_file(const std::string& filename) const
{
    FILE* fd = Filesystem::fopen(filename, "rb");
    if (!fd)
        return false;
    bmp_pvt::BmpFileHeader bmp_header;
    bool ok = bmp_header.read_header(fd) && bmp_header.isBmp();
    fclose(fd);
    return ok;
}



bool
BmpInput::open(const std::string& name, ImageSpec& spec)
{
    // saving 'name' for later use
    m_filename = name;

    m_fd = Filesystem::fopen(m_filename, "rb");
    if (!m_fd) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    // we read header of the file that we think is BMP file
    if (!m_bmp_header.read_header(m_fd)) {
        errorf("\"%s\": wrong bmp header size", m_filename);
        close();
        return false;
    }
    if (!m_bmp_header.isBmp()) {
        errorf("\"%s\" is not a BMP file, magic number doesn't match",
               m_filename);
        close();
        return false;
    }
    if (!m_dib_header.read_header(m_fd)) {
        errorf("\"%s\": wrong bitmap header size", m_filename);
        close();
        return false;
    }

    const int nchannels = (m_dib_header.bpp == 32) ? 4 : 3;
    const int height    = (m_dib_header.height >= 0) ? m_dib_header.height
                                                  : -m_dib_header.height;
    m_spec = ImageSpec(m_dib_header.width, height, nchannels, TypeDesc::UINT8);
    m_spec.attribute("XResolution", (int)m_dib_header.hres);
    m_spec.attribute("YResolution", (int)m_dib_header.vres);
    m_spec.attribute("ResolutionUnit", "m");

    // comupting size of one scanline - this is the size of one scanline that
    // is stored in the file, not in the memory
    int swidth = 0;
    switch (m_dib_header.bpp) {
    case 32:
    case 24:
        m_padded_scanline_size = ((m_spec.width * m_spec.nchannels) + 3) & ~3;
        break;
    case 16:
        m_padded_scanline_size = ((m_spec.width << 1) + 3) & ~3;
        m_spec.attribute("oiio:BitsPerSample", 4);
        break;
    case 8:
        m_padded_scanline_size = (m_spec.width + 3) & ~3;
        if (!read_color_table()) {
            close();
            return false;
        }
        break;
    case 4:
        swidth                 = (m_spec.width + 1) / 2;
        m_padded_scanline_size = (swidth + 3) & ~3;
        if (!read_color_table()) {
            close();
            return false;
        }
        break;
    case 1:
        swidth                 = (m_spec.width + 7) / 8;
        m_padded_scanline_size = (swidth + 3) & ~3;
        if (!read_color_table()) {
            close();
            return false;
        }
        break;
    }

    // file pointer is set to the beginning of image data
    // we save this position - it will be helpfull in read_native_scanline
    m_image_start = Filesystem::ftell(m_fd);

    spec = m_spec;
    return true;
}



bool
BmpInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (y < 0 || y > m_spec.height)
        return false;

    // if the height is positive scanlines are stored bottom-up
    if (m_dib_header.width >= 0)
        y = m_spec.height - y - 1;
    const int64_t scanline_off = y * m_padded_scanline_size;

    std::unique_ptr<unsigned char[]> fscanline(
        new unsigned char[m_padded_scanline_size]);
    Filesystem::fseek(m_fd, m_image_start + scanline_off, SEEK_SET);
    size_t n = fread(fscanline.get(), 1, m_padded_scanline_size, m_fd);
    if (n != (size_t)m_padded_scanline_size) {
        if (feof(m_fd))
            errorf("Hit end of file unexpectedly");
        else
            errorf("read error");
        return false;  // Read failed
    }

    // in each case we process only first m_spec.scanline_bytes () bytes
    // as only they contain information about pixels. The rest are just
    // because scanline size have to be 32-bit boundary
    if (m_dib_header.bpp == 24 || m_dib_header.bpp == 32) {
        for (unsigned int i = 0; i < m_spec.scanline_bytes();
             i += m_spec.nchannels)
            std::swap(fscanline[i], fscanline[i + 2]);
        memcpy(data, fscanline.get(), m_spec.scanline_bytes());
        return true;
    }

    size_t scanline_bytes = m_spec.scanline_bytes();
    std::unique_ptr<unsigned char[]> mscanline(
        new unsigned char[scanline_bytes]);
    if (m_dib_header.bpp == 16) {
        const uint16_t RED   = 0x7C00;
        const uint16_t GREEN = 0x03E0;
        const uint16_t BLUE  = 0x001F;
        for (unsigned int i = 0, j = 0; j < scanline_bytes; i += 2, j += 3) {
            uint16_t pixel   = (uint16_t) * (&fscanline[i]);
            mscanline[j]     = (uint8_t)((pixel & RED) >> 8);
            mscanline[j + 1] = (uint8_t)((pixel & GREEN) >> 4);
            mscanline[j + 2] = (uint8_t)(pixel & BLUE);
        }
    }
    if (m_dib_header.bpp == 8) {
        for (unsigned int i = 0, j = 0; j < scanline_bytes; ++i, j += 3) {
            mscanline[j]     = m_colortable[fscanline[i]].r;
            mscanline[j + 1] = m_colortable[fscanline[i]].g;
            mscanline[j + 2] = m_colortable[fscanline[i]].b;
        }
    }
    if (m_dib_header.bpp == 4) {
        for (unsigned int i = 0, j = 0; j + 6 < scanline_bytes; ++i, j += 6) {
            uint8_t mask     = 0xF0;
            mscanline[j]     = m_colortable[(fscanline[i] & mask) >> 4].r;
            mscanline[j + 1] = m_colortable[(fscanline[i] & mask) >> 4].g;
            mscanline[j + 2] = m_colortable[(fscanline[i] & mask) >> 4].b;
            mask             = 0x0F;
            mscanline[j + 3] = m_colortable[fscanline[i] & mask].r;
            mscanline[j + 4] = m_colortable[fscanline[i] & mask].g;
            mscanline[j + 5] = m_colortable[fscanline[i] & mask].b;
        }
    }
    if (m_dib_header.bpp == 1) {
        for (int64_t i = 0, k = 0; i < m_padded_scanline_size; ++i) {
            for (int j = 7; j >= 0; --j, k += 3) {
                if (size_t(k + 2) >= scanline_bytes)
                    break;
                int index = 0;
                if (fscanline[i] & (1 << j))
                    index = 1;
                mscanline[k]     = m_colortable[index].r;
                mscanline[k + 1] = m_colortable[index].g;
                mscanline[k + 2] = m_colortable[index].b;
            }
        }
    }
    memcpy(data, &mscanline[0], scanline_bytes);
    return true;
}



bool inline BmpInput::close(void)
{
    if (m_fd) {
        fclose(m_fd);
        m_fd = NULL;
    }
    init();
    return true;
}



bool
BmpInput::read_color_table(void)
{
    // size of color table is defined  by m_dib_header.cpalete
    // if this field is 0 - color table has max colors:
    // pow(2, m_dib_header.cpalete) otherwise color table have
    // m_dib_header.cpalete entries
    const int32_t colors = (m_dib_header.cpalete) ? m_dib_header.cpalete
                                                  : 1 << m_dib_header.bpp;
    size_t entry_size = 4;
    // if the file is OS V2 bitmap color table entr has only 3 bytes, not four
    if (m_dib_header.size == OS2_V1)
        entry_size = 3;
    m_colortable.resize(colors);
    for (int i = 0; i < colors; i++) {
        size_t n = fread(&m_colortable[i], 1, entry_size, m_fd);
        if (n != entry_size) {
            if (feof(m_fd))
                errorf(
                    "Hit end of file unexpectedly while reading color table");
            else
                errorf("read error while reading color table");
            return false;  // Read failed
        }
    }
    return true;  // ok
}

OIIO_PLUGIN_NAMESPACE_END
