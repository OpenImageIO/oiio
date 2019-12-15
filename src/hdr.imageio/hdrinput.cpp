// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <cassert>
#include <cstdio>
#include <iostream>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>

#include "rgbe.h"


OIIO_PLUGIN_NAMESPACE_BEGIN

/////////////////////////////////////////////////////////////////////////////
// .hdr / .rgbe files - HDR files from Radiance
//
// General info on the hdr/rgbe format can be found at:
//     http://local.wasp.uwa.edu.au/~pbourke/dataformats/pic/
// The source code in rgbe.{h,cpp} originally came from:
//     http://www.graphics.cornell.edu/~bjw/rgbe.html
// But it's been modified in several minor ways by LG.
// Also see Greg Ward's "Real Pixels" chapter in Graphics Gems II for an
// explanation of the encoding that's used in Radiance rgba files.
/////////////////////////////////////////////////////////////////////////////



class HdrInput final : public ImageInput {
public:
    HdrInput() { init(); }
    virtual ~HdrInput() { close(); }
    virtual const char* format_name(void) const override { return "hdr"; }
    virtual bool valid_file(const std::string& filename) const override;
    virtual bool open(const std::string& name, ImageSpec& spec) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override { return m_subimage; }
    virtual bool seek_subimage(int subimage, int miplevel) override;

private:
    std::string m_filename;  ///< File name
    FILE* m_fd;              ///< The open file handle
    int m_subimage;          ///< What subimage are we looking at?
    int m_next_scanline;     ///< Next scanline to read
    std::string rgbe_error;  ///< Buffer for RGBE library error msgs

    void init()
    {
        m_fd            = NULL;
        m_subimage      = -1;
        m_next_scanline = 0;
        rgbe_error.clear();
    }
};



// Export version number and create function symbols
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int hdr_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
hdr_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT ImageInput*
hdr_input_imageio_create()
{
    return new HdrInput;
}

OIIO_EXPORT const char* hdr_input_extensions[] = { "hdr", "rgbe", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
HdrInput::valid_file(const std::string& filename) const
{
    // Check that file exists and can be opened
    FILE* fd = Filesystem::fopen(filename, "rb");
    if (fd == nullptr)
        return false;

    rgbe_header_info h;
    int width, height;
    std::string err;
    int r = RGBE_ReadHeader(fd, &width, &height, &h, err);
    fclose(fd);
    return (r == RGBE_RETURN_SUCCESS);
}



bool
HdrInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
HdrInput::seek_subimage(int subimage, int miplevel)
{
    // HDR doesn't support multiple subimages or mipmaps
    if (subimage != 0 || miplevel != 0)
        return false;

    // Skip the hard work if we're already on the requested subimage
    if (subimage == current_subimage()) {
        return true;
    }

    close();

    // Check that file exists and can be opened
    m_fd = Filesystem::fopen(m_filename, "rb");
    if (m_fd == NULL) {
        errorf("Could not open file \"%s\"", m_filename);
        return false;
    }

    rgbe_header_info h;
    int width, height;
    int r = RGBE_ReadHeader(m_fd, &width, &height, &h, rgbe_error);
    if (r != RGBE_RETURN_SUCCESS) {
        errorf("%s", rgbe_error);
        close();
        return false;
    }

    m_spec = ImageSpec(width, height, 3, TypeDesc::FLOAT);

    if (h.valid & RGBE_VALID_GAMMA) {
        // Round gamma to the nearest hundredth to prevent stupid
        // precision choices and make it easier for apps to make
        // decisions based on known gamma values. For example, you want
        // 2.2, not 2.19998.
        float g = float(1.0 / h.gamma);
        g       = roundf(100.0 * g) / 100.0f;
        m_spec.attribute("oiio:Gamma", g);
        if (g == 1.0f)
            m_spec.attribute("oiio:ColorSpace", "linear");
        else
            m_spec.attribute("oiio:ColorSpace",
                             Strutil::sprintf("GammaCorrected%.2g", g));
    } else {
        // Presume linear color space
        m_spec.attribute("oiio:ColorSpace", "linear");
    }
    if (h.valid & RGBE_VALID_ORIENTATION)
        m_spec.attribute("Orientation", h.orientation);

    // FIXME -- should we do anything about exposure, software,
    // pixaspect, primaries?  (N.B. rgbe.c doesn't even handle most of them)

    m_subimage      = subimage;
    m_next_scanline = 0;
    return true;
}



bool
HdrInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (m_next_scanline > y) {
        // User is trying to read an earlier scanline than the one we're
        // up to.  Easy fix: close the file and re-open.
        ImageSpec dummyspec;
        int subimage = current_subimage();
        int miplevel = current_miplevel();
        if (!close() || !open(m_filename, dummyspec)
            || !seek_subimage(subimage, miplevel))
            return false;  // Somehow, the re-open failed
        assert(m_next_scanline == 0 && current_subimage() == subimage
               && current_miplevel() == miplevel);
    }
    while (m_next_scanline <= y) {
        // Keep reading until we're read the scanline we really need
        int r = RGBE_ReadPixels_RLE(m_fd, (float*)data, m_spec.width, 1,
                                    rgbe_error);
        ++m_next_scanline;
        if (r != RGBE_RETURN_SUCCESS) {
            errorf("%s", rgbe_error);
            return false;
        }
    }
    return true;
}



bool
HdrInput::close()
{
    if (m_fd)
        fclose(m_fd);
    init();  // Reset to initial state
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
