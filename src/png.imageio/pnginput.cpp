// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <OpenEXR/ImathColor.h>

#include "png_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN


class PNGInput final : public ImageInput {
public:
    PNGInput() { init(); }
    virtual ~PNGInput() { close(); }
    virtual const char* format_name(void) const override { return "png"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "ioproxy");
    }
    virtual bool valid_file(const std::string& filename) const override;
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override
    {
        lock_guard lock(m_mutex);
        return m_subimage;
    }
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    std::string m_filename;            ///< Stash the filename
    png_structp m_png;                 ///< PNG read structure pointer
    png_infop m_info;                  ///< PNG image info structure pointer
    int m_bit_depth;                   ///< PNG bit depth
    int m_color_type;                  ///< PNG color model type
    int m_interlace_type;              ///< PNG interlace type
    std::vector<unsigned char> m_buf;  ///< Buffer the image pixels
    int m_subimage;                    ///< What subimage are we looking at?
    Imath::Color3f m_bg;               ///< Background color
    int m_next_scanline;
    bool m_keep_unassociated_alpha;  ///< Do not convert unassociated alpha
    std::unique_ptr<Filesystem::IOProxy> m_io_local;
    Filesystem::IOProxy* m_io = nullptr;
    int64_t m_io_offset       = 0;
    bool m_err                = false;

    /// Reset everything to initial state
    ///
    void init()
    {
        m_subimage = -1;
        m_png      = nullptr;
        m_info     = nullptr;
        m_buf.clear();
        m_next_scanline           = 0;
        m_keep_unassociated_alpha = false;
        m_err                     = false;
    }

    /// Helper function: read the image.
    ///
    bool readimg();

    /// Extract the background color.
    ///
    bool get_background(float* red, float* green, float* blue);

    // Callback for PNG that reads from an IOProxy.
    static void PngReadCallback(png_structp png_ptr, png_bytep data,
                                png_size_t length)
    {
        PNGInput* pnginput = (PNGInput*)png_get_io_ptr(png_ptr);
        DASSERT(pnginput);
        size_t bytes = pnginput->m_io->read(data, length);
        if (bytes != length) {
            pnginput->errorf("Read error: requested %d got %d", length, bytes);
            pnginput->m_err = true;
        }
    }
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
png_input_imageio_create()
{
    return new PNGInput;
}

OIIO_EXPORT int png_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
png_imageio_library_version()
{
    return "libpng " PNG_LIBPNG_VER_STRING;
}

OIIO_EXPORT const char* png_input_extensions[] = { "png", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
PNGInput::valid_file(const std::string& filename) const
{
    FILE* fd = Filesystem::fopen(filename, "rb");
    if (!fd)
        return false;
    unsigned char sig[8];
    bool ok = (fread(sig, 1, sizeof(sig), fd) == sizeof(sig)
               && png_sig_cmp(sig, 0, 7) == 0);
    fclose(fd);
    return ok;
}



bool
PNGInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;
    m_subimage = 0;

    if (!m_io) {
        // If no proxy was supplied, create a file reader
        m_io = new Filesystem::IOFile(name, Filesystem::IOProxy::Mode::Read);
        m_io_local.reset(m_io);
    }
    if (!m_io || m_io->mode() != Filesystem::IOProxy::Mode::Read) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }
    m_io_offset = m_io->tell();

    unsigned char sig[8];
    if (m_io->pread(sig, sizeof(sig), 0) != sizeof(sig)
        || png_sig_cmp(sig, 0, 7)) {
        errorf("Not a PNG file");
        close();
        return false;  // Read failed
    }

    std::string s = PNG_pvt::create_read_struct(m_png, m_info, this);
    if (s.length()) {
        errorf("%s", s);
        close();
        return false;
    }

    // Tell libpng to use our read callback to read from the IOProxy
    png_set_read_fn(m_png, this, PngReadCallback);

    bool ok = PNG_pvt::read_info(m_png, m_info, m_bit_depth, m_color_type,
                                 m_interlace_type, m_bg, m_spec,
                                 m_keep_unassociated_alpha);
    if (!ok || m_err) {
        close();
        return false;
    }

    newspec         = spec();
    m_next_scanline = 0;

    return ok;
}



bool
PNGInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    // Check 'config' for any special requests
    if (config.get_int_attribute("oiio:UnassociatedAlpha", 0) == 1)
        m_keep_unassociated_alpha = true;
    auto ioparam = config.find_attribute("oiio:ioproxy", TypeDesc::PTR);
    m_io_local.reset();
    m_io = ioparam ? ioparam->get<Filesystem::IOProxy*>() : nullptr;
    return open(name, newspec);
}



bool
PNGInput::readimg()
{
    std::string s = PNG_pvt::read_into_buffer(m_png, m_info, m_spec,
                                              m_bit_depth, m_color_type, m_buf);
    if (s.length()) {
        close();
        errorf("%s", s);
        return false;
    }

    return true;
}



bool
PNGInput::close()
{
    if (m_png && m_info)
        PNG_pvt::destroy_read_struct(m_png, m_info);
    if (m_io_local) {
        // If we allocated our own ioproxy, close it.
        m_io_local.reset();
        m_io = nullptr;
    } else if (m_io) {
        // We were passed an ioproxy from the user. Don't actually close it,
        // just reset it to the original position. This makes it possible to
        // "re-open".
        m_io->seek(m_io_offset);
    }
    init();  // Reset to initial state
    return true;
}



template<class T>
static void
associateAlpha(T* data, int size, int channels, int alpha_channel, float gamma)
{
    T max = std::numeric_limits<T>::max();
    if (gamma == 1) {
        for (int x = 0; x < size; ++x, data += channels)
            for (int c = 0; c < channels; c++)
                if (c != alpha_channel) {
                    unsigned int f = data[c];
                    data[c]        = (f * data[alpha_channel]) / max;
                }
    } else {  //With gamma correction
        float inv_max = 1.0 / max;
        for (int x = 0; x < size; ++x, data += channels) {
            float alpha_associate = pow(data[alpha_channel] * inv_max, gamma);
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
PNGInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    y -= m_spec.y;
    if (y < 0 || y >= m_spec.height)  // out of range scanline
        return false;

    if (m_interlace_type != 0) {
        // Interlaced.  Punt and read the whole image
        if (m_buf.empty())
            readimg();
        size_t size = spec().scanline_bytes();
        memcpy(data, &m_buf[0] + y * size, size);
    } else {
        // Not an interlaced image -- read just one row
        if (m_next_scanline > y) {
            // User is trying to read an earlier scanline than the one we're
            // up to.  Easy fix: close the file and re-open.
            ImageSpec dummyspec;
            int subimage = current_subimage();
            if (!close() || !open(m_filename, dummyspec)
                || !seek_subimage(subimage, miplevel))
                return false;  // Somehow, the re-open failed
            assert(m_next_scanline == 0 && current_subimage() == subimage);
        }
        while (m_next_scanline <= y) {
            // Keep reading until we're read the scanline we really need
            // std::cerr << "reading scanline " << m_next_scanline << "\n";
            std::string s = PNG_pvt::read_next_scanline(m_png, data);
            if (s.length()) {
                errorf("%s", s);
                return false;
            }
            if (m_err)
                return false;  // error is already registered
            ++m_next_scanline;
        }
    }

    // PNG specifically dictates unassociated (un-"premultiplied") alpha.
    // Convert to associated unless we were requested not to do so.
    if (m_spec.alpha_channel != -1 && !m_keep_unassociated_alpha) {
        float gamma = m_spec.get_float_attribute("oiio:Gamma", 1.0f);
        if (m_spec.format == TypeDesc::UINT16)
            associateAlpha((unsigned short*)data, m_spec.width,
                           m_spec.nchannels, m_spec.alpha_channel, gamma);
        else
            associateAlpha((unsigned char*)data, m_spec.width, m_spec.nchannels,
                           m_spec.alpha_channel, gamma);
    }

    return true;
}

OIIO_PLUGIN_NAMESPACE_END
