// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "dds_pvt.h"

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/typedesc.h>

#include "squish/squish.h"

OIIO_PLUGIN_NAMESPACE_BEGIN

using namespace DDS_pvt;

// uncomment the following define to enable 3x2 cube map layout
//#define DDS_3X2_CUBE_MAP_LAYOUT

class DDSInput final : public ImageInput {
public:
    DDSInput() { init(); }
    virtual ~DDSInput() { close(); }
    virtual const char* format_name(void) const override { return "dds"; }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override
    {
        lock_guard lock(m_mutex);
        return m_subimage;
    }
    virtual int current_miplevel(void) const override
    {
        lock_guard lock(m_mutex);
        return m_miplevel;
    }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool read_native_tile(int subimage, int miplevel, int x, int y,
                                  int z, void* data) override;

private:
    std::string m_filename;            ///< Stash the filename
    FILE* m_file;                      ///< Open image handle
    std::vector<unsigned char> m_buf;  ///< Buffer the image pixels
    int m_subimage;
    int m_miplevel;
    int m_nchans;            ///< Number of colour channels in image
    int m_nfaces;            ///< Number of cube map sides in image
    int m_Bpp;               ///< Number of bytes per pixel
    int m_redL, m_redR;      ///< Bit shifts to extract red channel
    int m_greenL, m_greenR;  ///< Bit shifts to extract green channel
    int m_blueL, m_blueR;    ///< Bit shifts to extract blue channel
    int m_alphaL, m_alphaR;  ///< Bit shifts to extract alpha channel

    dds_header m_dds;  ///< DDS header

    /// Reset everything to initial state
    ///
    void init()
    {
        m_file     = NULL;
        m_subimage = -1;
        m_miplevel = -1;
        m_buf.clear();
    }

    /// Helper function: read the image as scanlines (all but cubemaps).
    ///
    bool readimg_scanlines();

    /// Helper function: read the image as tiles (cubemaps only).
    ///
    bool readimg_tiles();

    /// Helper function: calculate bit shifts to properly extract channel data
    ///
    inline void calc_shifts(int mask, int& left, int& right);

    /// Helper function: performs the actual file seeking.
    ///
    void internal_seek_subimage(int cubeface, int miplevel, unsigned int& w,
                                unsigned int& h, unsigned int& d);

    /// Helper function: performs the actual pixel decoding.
    bool internal_readimg(unsigned char* dst, int w, int h, int d);

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
dds_input_imageio_create()
{
    return new DDSInput;
}

OIIO_EXPORT int dds_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
dds_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT const char* dds_input_extensions[] = { "dds", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
DDSInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    m_file = Filesystem::fopen(name, "rb");
    if (!m_file) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

// due to struct packing, we may get a corrupt header if we just load the
// struct from file; to adress that, read every member individually
// save some typing
#define RH(memb)                                                               \
    if (!fread(&m_dds.memb, sizeof(m_dds.memb), 1)) {                          \
        close();                                                               \
        return false;                                                          \
    }

    RH(fourCC);
    RH(size);
    RH(flags);
    RH(height);
    RH(width);
    RH(pitch);
    RH(depth);
    RH(mipmaps);

    // advance the file pointer by 44 bytes (reserved fields)
    fseek(m_file, 44, SEEK_CUR);

    // pixel format struct
    RH(fmt.size);
    RH(fmt.flags);
    RH(fmt.fourCC);
    RH(fmt.bpp);
    RH(fmt.rmask);
    RH(fmt.gmask);
    RH(fmt.bmask);
    RH(fmt.amask);

    // caps
    RH(caps.flags1);
    RH(caps.flags2);

    // advance the file pointer by 8 bytes (reserved fields)
    fseek(m_file, 8, SEEK_CUR);
#undef RH
    if (bigendian()) {
        // DDS files are little-endian
        // only swap values which are not flags or bitmasks
        swap_endian(&m_dds.size);
        swap_endian(&m_dds.height);
        swap_endian(&m_dds.width);
        swap_endian(&m_dds.pitch);
        swap_endian(&m_dds.depth);
        swap_endian(&m_dds.mipmaps);

        swap_endian(&m_dds.fmt.size);
        swap_endian(&m_dds.fmt.bpp);
    }

    /*std::cerr << "[dds] fourCC: " << ((char *)&m_dds.fourCC)[0]
                                  << ((char *)&m_dds.fourCC)[1]
                                  << ((char *)&m_dds.fourCC)[2]
                                  << ((char *)&m_dds.fourCC)[3]
                                  << " (" << m_dds.fourCC << ")\n";
    std::cerr << "[dds] size: " << m_dds.size << "\n";
    std::cerr << "[dds] flags: " << m_dds.flags << "\n";
    std::cerr << "[dds] pitch: " << m_dds.pitch << "\n";
    std::cerr << "[dds] width: " << m_dds.width << "\n";
    std::cerr << "[dds] height: " << m_dds.height << "\n";
    std::cerr << "[dds] depth: " << m_dds.depth << "\n";
    std::cerr << "[dds] mipmaps: " << m_dds.mipmaps << "\n";
    std::cerr << "[dds] fmt.size: " << m_dds.fmt.size << "\n";
    std::cerr << "[dds] fmt.flags: " << m_dds.fmt.flags << "\n";
    
    std::cerr << "[dds] fmt.fourCC: " << ((char *)&m_dds.fmt.fourCC)[0]
                                      << ((char *)&m_dds.fmt.fourCC)[1]
                                      << ((char *)&m_dds.fmt.fourCC)[2]
                                      << ((char *)&m_dds.fmt.fourCC)[3]
                                      << " (" << m_dds.fmt.fourCC << ")\n";
    std::cerr << "[dds] fmt.bpp: " << m_dds.fmt.bpp << "\n";
    std::cerr << "[dds] caps.flags1: " << m_dds.caps.flags1 << "\n";
    std::cerr << "[dds] caps.flags2: " << m_dds.caps.flags2 << "\n";*/

    // sanity checks - valid 4CC, correct struct sizes and flags which should
    // be always present, regardless of the image type, size etc., also check
    // for impossible flag combinations
    if (m_dds.fourCC != DDS_MAKE4CC('D', 'D', 'S', ' ') || m_dds.size != 124
        || m_dds.fmt.size != 32 || !(m_dds.caps.flags1 & DDS_CAPS1_TEXTURE)
        || !(m_dds.flags & DDS_CAPS) || !(m_dds.flags & DDS_PIXELFORMAT)
        || (m_dds.caps.flags2 & DDS_CAPS2_VOLUME
            && !(m_dds.caps.flags1 & DDS_CAPS1_COMPLEX
                 && m_dds.flags & DDS_DEPTH))
        || (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP
            && !(m_dds.caps.flags1 & DDS_CAPS1_COMPLEX))) {
        errorf("Invalid DDS header, possibly corrupt file");
        close();
        return false;
    }

    // make sure all dimensions are > 0 and that we have at least one channel
    // (for uncompressed images)
    if (!(m_dds.flags & DDS_WIDTH) || !m_dds.width
        || !(m_dds.flags & DDS_HEIGHT) || !m_dds.height
        || ((m_dds.flags & DDS_DEPTH) && !m_dds.depth)
        || (!(m_dds.fmt.flags & DDS_PF_FOURCC)
            && !((m_dds.fmt.flags & DDS_PF_RGB)
                 | (m_dds.fmt.flags & DDS_PF_LUMINANCE)
                 | (m_dds.fmt.flags & DDS_PF_ALPHA)))) {
        errorf("Image with no data");
        close();
        return false;
    }

    // validate the pixel format
    // TODO: support DXGI and the "wackier" uncompressed formats
    if (m_dds.fmt.flags & DDS_PF_FOURCC && m_dds.fmt.fourCC != DDS_4CC_DXT1
        && m_dds.fmt.fourCC != DDS_4CC_DXT2 && m_dds.fmt.fourCC != DDS_4CC_DXT3
        && m_dds.fmt.fourCC != DDS_4CC_DXT4
        && m_dds.fmt.fourCC != DDS_4CC_DXT5) {
        errorf("Unsupported compression type");
        close();
        return false;
    }

    // determine the number of channels we have
    if (m_dds.fmt.flags & DDS_PF_FOURCC) {
        // squish decompresses everything to RGBA anyway
        /*if (m_dds.fmt.fourCC == DDS_4CC_DXT1)
            m_nchans = 3; // no alpha in DXT1
        else*/
        m_nchans = 4;
    } else {
        m_nchans = ((m_dds.fmt.flags & DDS_PF_LUMINANCE) ? 1 : 3)
                   + ((m_dds.fmt.flags & DDS_PF_ALPHA) ? 1 : 0);
        // also calculate bytes per pixel and the bit shifts
        m_Bpp = (m_dds.fmt.bpp + 7) >> 3;
        if (!(m_dds.fmt.flags & DDS_PF_LUMINANCE)) {
            calc_shifts(m_dds.fmt.rmask, m_redL, m_redR);
            calc_shifts(m_dds.fmt.gmask, m_greenL, m_greenR);
            calc_shifts(m_dds.fmt.bmask, m_blueL, m_blueR);
            calc_shifts(m_dds.fmt.amask, m_alphaL, m_alphaR);
        }
    }

    // fix depth, pitch and mipmaps for later use, if needed
    if (!(m_dds.fmt.flags & DDS_PF_FOURCC && m_dds.flags & DDS_PITCH))
        m_dds.pitch = m_dds.width * m_Bpp;
    if (!(m_dds.caps.flags2 & DDS_CAPS2_VOLUME))
        m_dds.depth = 1;
    if (!(m_dds.flags & DDS_MIPMAPCOUNT))
        m_dds.mipmaps = 1;
    // count cube map faces
    if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP) {
        m_nfaces = 0;
        for (int flag = DDS_CAPS2_CUBEMAP_POSITIVEX;
             flag <= DDS_CAPS2_CUBEMAP_NEGATIVEZ; flag <<= 1) {
            if (m_dds.caps.flags2 & flag)
                m_nfaces++;
        }
    } else
        m_nfaces = 1;

    seek_subimage(0, 0);
    newspec = spec();
    return true;
}



inline void
DDSInput::calc_shifts(int mask, int& left, int& right)
{
    if (mask == 0) {
        left = right = 0;
        return;
    }

    int i, tmp = mask;
    for (i = 0; i < 32; i++, tmp >>= 1) {
        if (tmp & 1)
            break;
    }
    right = i;

    for (i = 0; i < 8; i++, tmp >>= 1) {
        if (!(tmp & 1))
            break;
    }
    left = 8 - i;
}



// NOTE: This function has no sanity checks! It's a private method and relies
// on the input being correct and valid!
void
DDSInput::internal_seek_subimage(int cubeface, int miplevel, unsigned int& w,
                                 unsigned int& h, unsigned int& d)
{
    // early out for cubemaps that don't contain the requested face
    if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP
        && !(m_dds.caps.flags2 & (DDS_CAPS2_CUBEMAP_POSITIVEX << cubeface))) {
        w = h = d = 0;
        return;
    }
    // we can easily calculate the offsets because both compressed and
    // uncompressed images have predictable length
    // calculate the offset; start with after the header
    unsigned int ofs = 128;
    unsigned int len;
    // this loop is used to iterate over cube map sides, or run once in the
    // case of ordinary 2D or 3D images
    for (int j = 0; j <= cubeface; j++) {
        w = m_dds.width;
        h = m_dds.height;
        d = m_dds.depth;
        // skip subimages preceding the one we're seeking to
        // if we have no mipmaps, the modulo formula doesn't work and we
        // don't skip at all, so just add the offset and continue
        if (m_dds.mipmaps < 2) {
            if (j > 0) {
                if (m_dds.fmt.flags & DDS_PF_FOURCC)
                    // only check for DXT1 - all other formats have same block
                    // size
                    len = squish::GetStorageRequirements(w, h,
                                                         m_dds.fmt.fourCC
                                                                 == DDS_4CC_DXT1
                                                             ? squish::kDxt1
                                                             : squish::kDxt5);
                else
                    len = w * h * d * m_Bpp;
                ofs += len;
            }
            continue;
        }
        for (int i = 0; i < miplevel; i++) {
            if (m_dds.fmt.flags & DDS_PF_FOURCC)
                // only check for DXT1 - all other formats have same block size
                len = squish::GetStorageRequirements(w, h,
                                                     m_dds.fmt.fourCC
                                                             == DDS_4CC_DXT1
                                                         ? squish::kDxt1
                                                         : squish::kDxt5);
            else
                len = w * h * d * m_Bpp;
            ofs += len;
            w >>= 1;
            if (!w)
                w = 1;
            h >>= 1;
            if (!h)
                h = 1;
            d >>= 1;
            if (!d)
                d = 1;
        }
    }
    // seek to the offset we've found
    fseek(m_file, ofs, SEEK_SET);
}



bool
DDSInput::seek_subimage(int subimage, int miplevel)
{
    if (subimage != 0)
        return false;

    // early out
    if (subimage == current_subimage() && miplevel == current_miplevel()) {
        return true;
    }

    // don't seek if the image doesn't contain mipmaps, isn't 3D or a cube map,
    // and don't seek out of bounds
    if (miplevel < 0
        || (!(m_dds.caps.flags1 & DDS_CAPS1_COMPLEX) && miplevel != 0)
        || (unsigned int)miplevel >= m_dds.mipmaps)
        return false;

    // clear buffer so that readimage is called
    m_buf.clear();

    // for cube maps, the seek will be performed when reading a tile instead
    unsigned int w = 0, h = 0, d = 0;
    if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP) {
        // calc sizes separately for cube maps
        w = m_dds.width;
        h = m_dds.height;
        d = m_dds.depth;
        for (int i = 1; i < miplevel; i++) {
            w >>= 1;
            if (w < 1)
                w = 1;
            h >>= 1;
            if (h < 1)
                h = 1;
            d >>= 1;
            if (d < 1)
                d = 1;
        }
        // create imagespec for the 3x2 cube map layout
#ifdef DDS_3X2_CUBE_MAP_LAYOUT
        m_spec = ImageSpec(w * 3, h * 2, m_nchans, TypeDesc::UINT8);
#else   // 1x6 layout
        m_spec = ImageSpec(w, h * 6, m_nchans, TypeDesc::UINT8);
#endif  // DDS_3X2_CUBE_MAP_LAYOUT
        m_spec.depth      = d;
        m_spec.tile_width = m_spec.full_width = w;
        m_spec.tile_height = m_spec.full_height = h;
        m_spec.tile_depth = m_spec.full_depth = d;
    } else {
        internal_seek_subimage(0, miplevel, w, h, d);
        // create imagespec
        m_spec       = ImageSpec(w, h, m_nchans, TypeDesc::UINT8);
        m_spec.depth = d;
    }

    // fill the imagespec
    if (m_dds.fmt.flags & DDS_PF_FOURCC) {
        std::string tempstr = "";
        tempstr += ((char*)&m_dds.fmt.fourCC)[0];
        tempstr += ((char*)&m_dds.fmt.fourCC)[1];
        tempstr += ((char*)&m_dds.fmt.fourCC)[2];
        tempstr += ((char*)&m_dds.fmt.fourCC)[3];
        m_spec.attribute("compression", tempstr);
    }
    m_spec.attribute("oiio:BitsPerSample", m_dds.fmt.bpp);
    m_spec.default_channel_names();

    // detect texture type
    if (m_dds.caps.flags2 & DDS_CAPS2_VOLUME) {
        m_spec.attribute("texturetype", "Volume Texture");
        m_spec.attribute("textureformat", "Volume Texture");
    } else if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP) {
        m_spec.attribute("texturetype", "Environment");
        m_spec.attribute("textureformat", "CubeFace Environment");
        // check available cube map sides
        std::string sides = "";
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_POSITIVEX)
            sides += "+x";
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_NEGATIVEX) {
            if (sides.size())
                sides += " ";
            sides += "-x";
        }
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_POSITIVEY) {
            if (sides.size())
                sides += " ";
            sides += "+y";
        }
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_NEGATIVEY) {
            if (sides.size())
                sides += " ";
            sides += "-y";
        }
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_POSITIVEZ) {
            if (sides.size())
                sides += " ";
            sides += "+z";
        }
        if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP_NEGATIVEZ) {
            if (sides.size())
                sides += " ";
            sides += "-z";
        }
        m_spec.attribute("dds:CubeMapSides", sides);
    } else {
        m_spec.attribute("texturetype", "Plain Texture");
        m_spec.attribute("textureformat", "Plain Texture");
    }

    m_subimage = subimage;
    m_miplevel = miplevel;
    return true;
}



bool
DDSInput::internal_readimg(unsigned char* dst, int w, int h, int d)
{
    if (m_dds.fmt.flags & DDS_PF_FOURCC) {
        // compressed image
        int flags = 0;
        switch (m_dds.fmt.fourCC) {
        case DDS_4CC_DXT1: flags = squish::kDxt1; break;
        // DXT2 and 3 are the same, only 2 has pre-multiplied alpha
        case DDS_4CC_DXT2:
        case DDS_4CC_DXT3: flags = squish::kDxt3; break;
        // DXT4 and 5 are the same, only 4 has pre-multiplied alpha
        case DDS_4CC_DXT4:
        case DDS_4CC_DXT5: flags = squish::kDxt5; break;
        }
        // create source buffer
        std::vector<squish::u8> tmp(
            squish::GetStorageRequirements(w, h, flags));
        // load image into buffer
        if (!fread(&tmp[0], tmp.size(), 1))
            return false;
        // decompress image
        squish::DecompressImage(dst, w, h, &tmp[0], flags);
        tmp.clear();
        // correct pre-multiplied alpha, if necessary
        if (m_dds.fmt.fourCC == DDS_4CC_DXT2
            || m_dds.fmt.fourCC == DDS_4CC_DXT4) {
            int k;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    k          = (y * w + x) * 4;
                    dst[k + 0] = (unsigned char)((int)dst[k + 0] * 255
                                                 / (int)dst[k + 3]);
                    dst[k + 1] = (unsigned char)((int)dst[k + 1] * 255
                                                 / (int)dst[k + 3]);
                    dst[k + 2] = (unsigned char)((int)dst[k + 2] * 255
                                                 / (int)dst[k + 3]);
                }
            }
        }
    } else {
        // uncompressed image

        // HACK: shortcut for luminance
        if (m_dds.fmt.flags & DDS_PF_LUMINANCE) {
            return fread(dst, w * m_Bpp, h);
        }

        int k, pixel = 0;
        for (int z = 0; z < d; z++) {
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    if (!fread(&pixel, 1, m_Bpp))
                        return false;
                    k          = (z * h * w + y * w + x) * m_spec.nchannels;
                    dst[k + 0] = ((pixel & m_dds.fmt.rmask) >> m_redR)
                                 << m_redL;
                    dst[k + 1] = ((pixel & m_dds.fmt.gmask) >> m_greenR)
                                 << m_greenL;
                    dst[k + 2] = ((pixel & m_dds.fmt.bmask) >> m_blueR)
                                 << m_blueL;
                    if (m_dds.fmt.flags & DDS_PF_ALPHA)
                        dst[k + 3] = ((pixel & m_dds.fmt.amask) >> m_alphaR)
                                     << m_alphaL;
                }
            }
        }
    }
    return true;
}



bool
DDSInput::readimg_scanlines()
{
    //std::cerr << "[dds] readimg: " << ftell (m_file) << "\n";
    // resize destination buffer
    m_buf.resize(m_spec.scanline_bytes() * m_spec.height * m_spec.depth
                 /*/ (1 << m_miplevel)*/);

    return internal_readimg(&m_buf[0], m_spec.width, m_spec.height,
                            m_spec.depth);
}



bool
DDSInput::readimg_tiles()
{
    // resize destination buffer
    m_buf.resize(m_spec.tile_bytes());

    return internal_readimg(&m_buf[0], m_spec.tile_width, m_spec.tile_height,
                            m_spec.tile_depth);
}



bool
DDSInput::close()
{
    if (m_file) {
        fclose(m_file);
        m_file = NULL;
    }

    init();  // Reset to initial state
    return true;
}



bool
DDSInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    // don't proceed if a cube map - use tiles then instead
    if (m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP)
        return false;
    if (m_buf.empty())
        readimg_scanlines();

    size_t size = spec().scanline_bytes();
    memcpy(data, &m_buf[0] + z * m_spec.height * size + y * size, size);
    return true;
}



bool
DDSInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                           void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    // static ints to keep track of the current cube face and re-seek and
    // re-read face
    static int lastx = -1, lasty = -1, lastz = -1;
    // don't proceed if not a cube map - use scanlines then instead
    if (!(m_dds.caps.flags2 & DDS_CAPS2_CUBEMAP))
        return false;
    // make sure we get the right dimensions
    if (x % m_spec.tile_width || y % m_spec.tile_height
        || z % m_spec.tile_width)
        return false;
    if (m_buf.empty() || x != lastx || y != lasty || z != lastz) {
        lastx          = x;
        lasty          = y;
        lastz          = z;
        unsigned int w = 0, h = 0, d = 0;
#ifdef DDS_3X2_CUBE_MAP_LAYOUT
        internal_seek_subimage(((x / m_spec.tile_width) << 1)
                                   + y / m_spec.tile_height,
                               m_miplevel, w, h, d);
#else   // 1x6 layout
        internal_seek_subimage(y / m_spec.tile_height, m_miplevel, w, h, d);
#endif  // DDS_3X2_CUBE_MAP_LAYOUT
        if (!w && !h && !d)
            // face not present in file, black-pad the image
            memset(&m_buf[0], 0, m_spec.tile_bytes());
        else
            readimg_tiles();
    }

    memcpy(data, &m_buf[0], m_spec.tile_bytes());
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
