// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md
#include "sgi_pvt.h"
#include <OpenImageIO/dassert.h>

OIIO_PLUGIN_NAMESPACE_BEGIN

// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int sgi_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
sgi_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT ImageInput*
sgi_input_imageio_create()
{
    return new SgiInput;
}

OIIO_EXPORT const char* sgi_input_extensions[] = { "sgi", "rgb",  "rgba", "bw",
                                                   "int", "inta", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
SgiInput::valid_file(const std::string& filename) const
{
    FILE* fd = Filesystem::fopen(filename, "rb");
    if (!fd)
        return false;
    int16_t magic;
    bool ok = (::fread(&magic, sizeof(magic), 1, fd) == 1);
    if (littleendian())
        swap_endian(&magic);
    ok &= (magic == sgi_pvt::SGI_MAGIC);
    fclose(fd);
    return ok;
}



bool
SgiInput::open(const std::string& name, ImageSpec& spec)
{
    // saving name for later use
    m_filename = name;

    m_fd = Filesystem::fopen(m_filename, "rb");
    if (!m_fd) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    if (!read_header()) {
        close();
        return false;
    }

    if (m_sgi_header.magic != sgi_pvt::SGI_MAGIC) {
        errorf("\"%s\" is not a SGI file, magic number doesn't match",
               m_filename);
        close();
        return false;
    }

    int height    = 0;
    int nchannels = 0;
    switch (m_sgi_header.dimension) {
    case sgi_pvt::ONE_SCANLINE_ONE_CHANNEL:
        height    = 1;
        nchannels = 1;
        break;
    case sgi_pvt::MULTI_SCANLINE_ONE_CHANNEL:
        height    = m_sgi_header.ysize;
        nchannels = 1;
        break;
    case sgi_pvt::MULTI_SCANLINE_MULTI_CHANNEL:
        height    = m_sgi_header.ysize;
        nchannels = m_sgi_header.zsize;
        break;
    default:
        errorf("Bad dimension: %d", m_sgi_header.dimension);
        close();
        return false;
    }

    if (m_sgi_header.colormap == sgi_pvt::COLORMAP
        || m_sgi_header.colormap == sgi_pvt::SCREEN) {
        errorf("COLORMAP and SCREEN color map types aren't supported");
        close();
        return false;
    }

    m_spec = ImageSpec(m_sgi_header.xsize, height, nchannels,
                       m_sgi_header.bpc == 1 ? TypeDesc::UINT8
                                             : TypeDesc::UINT16);
    if (strlen(m_sgi_header.imagename))
        m_spec.attribute("ImageDescription", m_sgi_header.imagename);

    if (m_sgi_header.storage == sgi_pvt::RLE) {
        m_spec.attribute("compression", "rle");
        if (!read_offset_tables()) {
            close();
            return false;
        }
    }

    spec = m_spec;
    return true;
}



bool
SgiInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (y < 0 || y > m_spec.height)
        return false;

    y = m_spec.height - y - 1;

    ptrdiff_t bpc = m_sgi_header.bpc;
    std::vector<std::vector<unsigned char>> channeldata(m_spec.nchannels);
    if (m_sgi_header.storage == sgi_pvt::RLE) {
        // reading and uncompressing first channel (red in RGBA images)
        for (int c = 0; c < m_spec.nchannels; ++c) {
            // offset for this scanline/channel
            ptrdiff_t off             = y + c * m_spec.height;
            ptrdiff_t scanline_offset = start_tab[off];
            ptrdiff_t scanline_length = length_tab[off];
            channeldata[c].resize(m_spec.width * bpc);
            uncompress_rle_channel(scanline_offset, scanline_length,
                                   &(channeldata[c][0]));
        }
    } else {
        // non-RLE case -- just read directly into our channel data
        for (int c = 0; c < m_spec.nchannels; ++c) {
            // offset for this scanline/channel
            ptrdiff_t off             = y + c * m_spec.height;
            ptrdiff_t scanline_offset = sgi_pvt::SGI_HEADER_LEN
                                        + off * m_spec.width * bpc;
            Filesystem::fseek(m_fd, scanline_offset, SEEK_SET);
            channeldata[c].resize(m_spec.width * bpc);
            if (!fread(&(channeldata[c][0]), 1, m_spec.width * bpc))
                return false;
        }
    }

    if (m_spec.nchannels == 1) {
        // If just one channel, no interleaving is necessary, just memcpy
        memcpy(data, &(channeldata[0][0]), channeldata[0].size());
    } else {
        unsigned char* cdata = (unsigned char*)data;
        for (int x = 0; x < m_spec.width; ++x) {
            for (int c = 0; c < m_spec.nchannels; ++c) {
                *cdata++ = channeldata[c][x * bpc];
                if (bpc == 2)
                    *cdata++ = channeldata[c][x * bpc + 1];
            }
        }
    }

    // Swap endianness if needed
    if (bpc == 2 && littleendian())
        swap_endian((unsigned short*)data, m_spec.width * m_spec.nchannels);

    return true;
}



bool
SgiInput::uncompress_rle_channel(int scanline_off, int scanline_len,
                                 unsigned char* out)
{
    int bpc = m_sgi_header.bpc;
    std::unique_ptr<unsigned char[]> rle_scanline(
        new unsigned char[scanline_len]);
    Filesystem::fseek(m_fd, scanline_off, SEEK_SET);
    if (!fread(&rle_scanline[0], 1, scanline_len))
        return false;
    int limit = m_spec.width;
    int i     = 0;
    if (bpc == 1) {
        // 1 bit per channel
        while (i < scanline_len) {
            // Read a byte, it is the count.
            unsigned char value = rle_scanline[i++];
            int count           = value & 0x7F;
            // If the count is zero, we're done
            if (!count)
                break;
            // If the high bit is set, we just copy the next 'count' values
            if (value & 0x80) {
                while (count--) {
                    DASSERT(i < scanline_len && limit > 0);
                    *(out++) = rle_scanline[i++];
                    --limit;
                }
            }
            // If the high bit is zero, we copy the NEXT value, count times
            else {
                value = rle_scanline[i++];
                while (count--) {
                    DASSERT(limit > 0);
                    *(out++) = value;
                    --limit;
                }
            }
        }
    } else {
        // 2 bits per channel
        ASSERT(bpc == 2);
        while (i < scanline_len) {
            // Read a byte, it is the count.
            unsigned short value = (rle_scanline[i] << 8) | rle_scanline[i + 1];
            i += 2;
            int count = value & 0x7F;
            // If the count is zero, we're done
            if (!count)
                break;
            // If the high bit is set, we just copy the next 'count' values
            if (value & 0x80) {
                while (count--) {
                    DASSERT(i + 1 < scanline_len && limit > 0);
                    *(out++) = rle_scanline[i++];
                    *(out++) = rle_scanline[i++];
                    --limit;
                }
            }
            // If the high bit is zero, we copy the NEXT value, count times
            else {
                while (count--) {
                    DASSERT(limit > 0);
                    *(out++) = rle_scanline[i];
                    *(out++) = rle_scanline[i + 1];
                    --limit;
                }
                i += 2;
            }
        }
    }
    if (i != scanline_len || limit != 0) {
        errorf("Corrupt RLE data");
        return false;
    }

    return true;
}



bool
SgiInput::close()
{
    if (m_fd)
        fclose(m_fd);
    init();
    return true;
}



bool
SgiInput::read_header()
{
    if (!fread(&m_sgi_header.magic, sizeof(m_sgi_header.magic), 1)
        || !fread(&m_sgi_header.storage, sizeof(m_sgi_header.storage), 1)
        || !fread(&m_sgi_header.bpc, sizeof(m_sgi_header.bpc), 1)
        || !fread(&m_sgi_header.dimension, sizeof(m_sgi_header.dimension), 1)
        || !fread(&m_sgi_header.xsize, sizeof(m_sgi_header.xsize), 1)
        || !fread(&m_sgi_header.ysize, sizeof(m_sgi_header.ysize), 1)
        || !fread(&m_sgi_header.zsize, sizeof(m_sgi_header.zsize), 1)
        || !fread(&m_sgi_header.pixmin, sizeof(m_sgi_header.pixmin), 1)
        || !fread(&m_sgi_header.pixmax, sizeof(m_sgi_header.pixmax), 1)
        || !fread(&m_sgi_header.dummy, sizeof(m_sgi_header.dummy), 1)
        || !fread(&m_sgi_header.imagename, sizeof(m_sgi_header.imagename), 1))
        return false;

    m_sgi_header.imagename[79] = '\0';
    if (!fread(&m_sgi_header.colormap, sizeof(m_sgi_header.colormap), 1))
        return false;

    //don't read dummy bytes
    Filesystem::fseek(m_fd, 404, SEEK_CUR);

    if (littleendian()) {
        swap_endian(&m_sgi_header.magic);
        swap_endian(&m_sgi_header.dimension);
        swap_endian(&m_sgi_header.xsize);
        swap_endian(&m_sgi_header.ysize);
        swap_endian(&m_sgi_header.zsize);
        swap_endian(&m_sgi_header.pixmin);
        swap_endian(&m_sgi_header.pixmax);
        swap_endian(&m_sgi_header.colormap);
    }
    return true;
}



bool
SgiInput::read_offset_tables()
{
    int tables_size = m_sgi_header.ysize * m_sgi_header.zsize;
    start_tab.resize(tables_size);
    length_tab.resize(tables_size);
    if (!fread(&start_tab[0], sizeof(uint32_t), tables_size)
        || !fread(&length_tab[0], sizeof(uint32_t), tables_size))
        return false;

    if (littleendian()) {
        swap_endian(&length_tab[0], length_tab.size());
        swap_endian(&start_tab[0], start_tab.size());
    }
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
