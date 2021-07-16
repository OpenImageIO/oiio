// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/deepdata.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/plugin.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/typedesc.h>

#include "imageio_pvt.h"

#include <boost/thread/tss.hpp>
using boost::thread_specific_ptr;


OIIO_NAMESPACE_BEGIN
using namespace pvt;



class ImageOutput::Impl {
public:
    // Unneeded?
    //  // So we can lock this ImageOutput for the thread-safe methods.
    //  std::recursive_mutex m_mutex;

    // Thread-specific error message for this ImageOutput.
    thread_specific_ptr<std::string> m_errormessage;
    int m_threads = 0;
};



void
ImageOutput::impl_deleter(Impl* impl)
{
    delete impl;
}



void*
ImageOutput::operator new(size_t size)
{
    void* ptr = ::operator new(size);
    return ptr;
}



void
ImageOutput::operator delete(void* ptr)
{
    ImageOutput* in = (ImageOutput*)ptr;
    ::operator delete(in);
}



ImageOutput::ImageOutput()
    : m_impl(new Impl, impl_deleter)
{
}



ImageOutput::~ImageOutput() {}



bool
ImageOutput::write_scanline(int /*y*/, int /*z*/, TypeDesc /*format*/,
                            const void* /*data*/, stride_t /*xstride*/)
{
    // Default implementation: don't know how to write scanlines
    return false;
}



bool
ImageOutput::write_scanlines(int ybegin, int yend, int z, TypeDesc format,
                             const void* data, stride_t xstride,
                             stride_t ystride)
{
    // Default implementation: write each scanline individually
    stride_t native_pixel_bytes = (stride_t)m_spec.pixel_bytes(true);
    if (format == TypeDesc::UNKNOWN && xstride == AutoStride)
        xstride = native_pixel_bytes;
    stride_t zstride = AutoStride;
    m_spec.auto_stride(xstride, ystride, zstride, format, m_spec.nchannels,
                       m_spec.width, yend - ybegin);
    bool ok = true;
    for (int y = ybegin; ok && y < yend; ++y) {
        ok &= write_scanline(y, z, format, data, xstride);
        data = (char*)data + ystride;
    }
    return ok;
}



bool
ImageOutput::write_tile(int /*x*/, int /*y*/, int /*z*/, TypeDesc /*format*/,
                        const void* /*data*/, stride_t /*xstride*/,
                        stride_t /*ystride*/, stride_t /*zstride*/)
{
    // Default implementation: don't know how to write tiles
    return false;
}



bool
ImageOutput::write_tiles(int xbegin, int xend, int ybegin, int yend, int zbegin,
                         int zend, TypeDesc format, const void* data,
                         stride_t xstride, stride_t ystride, stride_t zstride)
{
    if (!m_spec.valid_tile_range(xbegin, xend, ybegin, yend, zbegin, zend))
        return false;

    // Default implementation: write each tile individually
    stride_t native_pixel_bytes = (stride_t)m_spec.pixel_bytes(true);
    if (format == TypeDesc::UNKNOWN && xstride == AutoStride)
        xstride = native_pixel_bytes;
    m_spec.auto_stride(xstride, ystride, zstride, format, m_spec.nchannels,
                       xend - xbegin, yend - ybegin);

    bool ok            = true;
    stride_t pixelsize = format.size() * m_spec.nchannels;
    std::unique_ptr<char[]> buf;
    for (int z = zbegin; z < zend; z += std::max(1, m_spec.tile_depth)) {
        int zd = std::min(zend - z, m_spec.tile_depth);
        for (int y = ybegin; y < yend; y += m_spec.tile_height) {
            char* tilestart = ((char*)data + (z - zbegin) * zstride
                               + (y - ybegin) * ystride);
            int yh          = std::min(yend - y, m_spec.tile_height);
            for (int x = xbegin; ok && x < xend; x += m_spec.tile_width) {
                int xw = std::min(xend - x, m_spec.tile_width);
                // Full tiles are written directly into the user buffer, but
                // Partial tiles (such as at the image edge) are copied into
                // a padded buffer to stage them.
                if (xw == m_spec.tile_width && yh == m_spec.tile_height
                    && zd == m_spec.tile_depth) {
                    ok &= write_tile(x, y, z, format, tilestart, xstride,
                                     ystride, zstride);
                } else {
                    if (!buf.get())
                        buf.reset(new char[pixelsize * m_spec.tile_pixels()]);
                    OIIO::copy_image(m_spec.nchannels, xw, yh, zd, tilestart,
                                     pixelsize, xstride, ystride, zstride,
                                     &buf[0], pixelsize,
                                     pixelsize * m_spec.tile_width,
                                     pixelsize * m_spec.tile_pixels());
                    ok &= write_tile(x, y, z, format, &buf[0], pixelsize,
                                     pixelsize * m_spec.tile_width,
                                     pixelsize * m_spec.tile_pixels());
                }
                tilestart += m_spec.tile_width * xstride;
            }
        }
    }
    return ok;
}



bool
ImageOutput::write_rectangle(int /*xbegin*/, int /*xend*/, int /*ybegin*/,
                             int /*yend*/, int /*zbegin*/, int /*zend*/,
                             TypeDesc /*format*/, const void* /*data*/,
                             stride_t /*xstride*/, stride_t /*ystride*/,
                             stride_t /*zstride*/)
{
    return false;
}



bool
ImageOutput::write_deep_scanlines(int /*ybegin*/, int /*yend*/, int /*z*/,
                                  const DeepData& /*deepdata*/)
{
    return false;  // default: doesn't support deep images
}



bool
ImageOutput::write_deep_tiles(int /*xbegin*/, int /*xend*/, int /*ybegin*/,
                              int /*yend*/, int /*zbegin*/, int /*zend*/,
                              const DeepData& /*deepdata*/)
{
    return false;  // default: doesn't support deep images
}



bool
ImageOutput::write_deep_image(const DeepData& deepdata)
{
    if (m_spec.depth > 1) {
        errorf("write_deep_image is not supported for volume (3D) images.");
        return false;
        // FIXME? - not implementing 3D deep images for now.  The only
        // format that supports deep images at this time is OpenEXR, and
        // it doesn't support volumes.
    }
    if (m_spec.tile_width) {
        // Tiled image
        return write_deep_tiles(m_spec.x, m_spec.x + m_spec.width, m_spec.y,
                                m_spec.y + m_spec.height, m_spec.z,
                                m_spec.z + m_spec.depth, deepdata);
    } else {
        // Scanline image
        return write_deep_scanlines(m_spec.y, m_spec.y + m_spec.height, 0,
                                    deepdata);
    }
}



int
ImageOutput::send_to_output(const char* /*format*/, ...)
{
    // FIXME -- I can't remember how this is supposed to work
    return 0;
}



int
ImageOutput::send_to_client(const char* /*format*/, ...)
{
    // FIXME -- I can't remember how this is supposed to work
    return 0;
}



void
ImageOutput::append_error(string_view message) const
{
    if (message.size() && message.back() == '\n')
        message.remove_suffix(1);
    std::string* errptr = m_impl->m_errormessage.get();
    if (!errptr) {
        errptr = new std::string;
        m_impl->m_errormessage.reset(errptr);
    }
    OIIO_DASSERT(
        errptr->size() < 1024 * 1024 * 16
        && "Accumulated error messages > 16MB. Try checking return codes!");
    if (errptr->size() && errptr->back() != '\n')
        *errptr += '\n';
    *errptr += message;
}



const void*
ImageOutput::to_native_scanline(TypeDesc format, const void* data,
                                stride_t xstride,
                                std::vector<unsigned char>& scratch,
                                unsigned int dither, int yorigin, int zorigin)
{
    return to_native_rectangle(0, m_spec.width, 0, 1, 0, 1, format, data,
                               xstride, 0, 0, scratch, dither, m_spec.x,
                               yorigin, zorigin);
}



const void*
ImageOutput::to_native_tile(TypeDesc format, const void* data, stride_t xstride,
                            stride_t ystride, stride_t zstride,
                            std::vector<unsigned char>& scratch,
                            unsigned int dither, int xorigin, int yorigin,
                            int zorigin)
{
    return to_native_rectangle(0, m_spec.tile_width, 0, m_spec.tile_height, 0,
                               std::max(1, m_spec.tile_depth), format, data,
                               xstride, ystride, zstride, scratch, dither,
                               xorigin, yorigin, zorigin);
}



const void*
ImageOutput::to_native_rectangle(int xbegin, int xend, int ybegin, int yend,
                                 int zbegin, int zend, TypeDesc format,
                                 const void* data, stride_t xstride,
                                 stride_t ystride, stride_t zstride,
                                 std::vector<unsigned char>& scratch,
                                 unsigned int dither, int xorigin, int yorigin,
                                 int zorigin)
{
    // native_pixel_bytes is the size of a pixel in the FILE, including
    // the per-channel format, if specified when the file was opened.
    stride_t native_pixel_bytes = (stride_t)m_spec.pixel_bytes(true);
    // perchanfile is true if the spec has different per-channel data types
    // and the file format supports that feature.
    bool perchanfile = m_spec.channelformats.size()
                       && supports("channelformats");
    // native_data is true if the user is passing data in the native format
    bool native_data           = (format == TypeDesc::UNKNOWN
                        || (format == m_spec.format && !perchanfile));
    stride_t input_pixel_bytes = native_data ? native_pixel_bytes
                                             : stride_t(format.size()
                                                        * m_spec.nchannels);
    // If user is passing native data and it's all one type, go ahead and
    // set format correctly.
    if (format == TypeDesc::UNKNOWN && !perchanfile)
        format = m_spec.format;
    // If the user is passing native data and they've left xstride set
    // to Auto, then we know it's the native pixel size.
    if (native_data && xstride == AutoStride)
        xstride = native_pixel_bytes;
    // Fill in the rest of the strides that haven't been set.
    m_spec.auto_stride(xstride, ystride, zstride, format, m_spec.nchannels,
                       xend - xbegin, yend - ybegin);

    // Compute width and height from the rectangle extents
    int width  = xend - xbegin;
    int height = yend - ybegin;
    int depth  = zend - zbegin;

    // Do the strides indicate that the data area is contiguous?
    bool contiguous;
    if (native_data) {
        // If it's native data, it had better be contiguous by the
        // file's definition.
        contiguous = (xstride == (stride_t)(m_spec.pixel_bytes(native_data)));
    } else {
        // If it's not native data, we only care if the user's buffer
        // is contiguous.
        contiguous = (xstride == (stride_t)(format.size() * m_spec.nchannels));
    }
    contiguous &= ((ystride == xstride * width || height == 1)
                   && (zstride == ystride * height || depth == 1));

    if (native_data && contiguous) {
        // Data are already in the native format and contiguous
        // just return a ptr to the original data.
        return data;
    }

    imagesize_t rectangle_pixels = stride_t(width) * stride_t(height) * depth;
    imagesize_t rectangle_values = rectangle_pixels * m_spec.nchannels;
    imagesize_t native_rectangle_bytes = rectangle_pixels * native_pixel_bytes;

    // Cases to handle:
    // 1. File has per-channel data, user passes native data -- this has
    //    already returned above, since the data didn't need munging.
    // 2. File has per-channel data, user passes some other data type
    // 3. File has uniform data, user passes some other data type
    // 4. File has uniform data, user passes the right data -- note that
    //    this case already returned if the user data was contiguous

    // Handle the per-channel format case (#2) where the user is passing
    // a non-native buffer.
    if (perchanfile) {
        OIIO_DASSERT(
            (contiguous || !native_data)
            && "Per-channel native output requires contiguous strides");
        OIIO_DASSERT(format != TypeDesc::UNKNOWN);
        OIIO_DASSERT(m_spec.channelformats.size() == (size_t)m_spec.nchannels);
        scratch.resize(native_rectangle_bytes);
        size_t offset = 0;
        for (int c = 0; c < m_spec.nchannels; ++c) {
            TypeDesc chanformat = m_spec.channelformats[c];
            convert_image(1 /* channels */, width, height, depth,
                          (char*)data + c * format.size(), format, xstride,
                          ystride, zstride, &scratch[offset], chanformat,
                          native_pixel_bytes, AutoStride, AutoStride);
            offset += chanformat.size();
        }
        return &scratch[0];
    }

    // The remaining code is where all channels in the file have the
    // same data type, which may or may not be what the user passed in
    // (cases #3 and #4 above).
    imagesize_t contiguoussize = contiguous
                                     ? 0
                                     : rectangle_values * input_pixel_bytes;
    contiguoussize             = (contiguoussize + 3)
                     & (~3);  // Round up to 4-byte boundary
    OIIO_DASSERT((contiguoussize & 3) == 0);
    imagesize_t floatsize = rectangle_values * sizeof(float);
    bool do_dither        = (dither && format.is_floating_point()
                      && m_spec.format.basetype == TypeDesc::UINT8);
    scratch.resize(contiguoussize + floatsize + native_rectangle_bytes);

    // Force contiguity if not already present
    if (!contiguous) {
        data = contiguize(data, m_spec.nchannels, xstride, ystride, zstride,
                          (void*)&scratch[0], width, height, depth, format);
    }

    // If the only reason we got this far was because the data was not
    // contiguous, but it was in the correct native data format all along,
    // we can return the contiguized data without needing unnecessary
    // conversion into float and back.
    if (native_data) {
        return data;
    }

    // Rather than implement the entire cross-product of possible
    // conversions, use float as an intermediate format, which generally
    // will always preserve enough precision.
    const float* buf;
    if (format == TypeDesc::FLOAT) {
        if (!do_dither) {
            // Already in float format and no dither -- leave it as-is.
            buf = (float*)data;
        } else {
            // Need to make a copy, even though it's already float, so the
            // dither doesn't overwrite the caller's data.
            buf = (float*)&scratch[contiguoussize];
            memcpy((float*)buf, data, floatsize);
        }
    } else {
        // Convert from 'format' to float.
        buf = convert_to_float(data, (float*)&scratch[contiguoussize],
                               (int)rectangle_values, format);
    }

    if (do_dither) {
        stride_t pixelsize = m_spec.nchannels * sizeof(float);
        OIIO::add_dither(m_spec.nchannels, width, height, depth, (float*)buf,
                         pixelsize, pixelsize * stride_t(width),
                         pixelsize * stride_t(width) * stride_t(height),
                         1.0f / 255.0f, m_spec.alpha_channel, m_spec.z_channel,
                         dither, 0, xorigin, yorigin, zorigin);
    }

    // Convert from float to native format.
    return parallel_convert_from_float(buf,
                                       &scratch[contiguoussize + floatsize],
                                       rectangle_values, m_spec.format);
}



bool
ImageOutput::write_image(TypeDesc format, const void* data, stride_t xstride,
                         stride_t ystride, stride_t zstride,
                         ProgressCallback progress_callback,
                         void* progress_callback_data)
{
    bool native          = (format == TypeDesc::UNKNOWN);
    stride_t pixel_bytes = native ? (stride_t)m_spec.pixel_bytes(native)
                                  : format.size() * m_spec.nchannels;
    if (xstride == AutoStride)
        xstride = pixel_bytes;
    m_spec.auto_stride(xstride, ystride, zstride, format, m_spec.nchannels,
                       m_spec.width, m_spec.height);

    if (supports("rectangles")) {
        // Use a rectangle if we can
        return write_rectangle(0, m_spec.width, 0, m_spec.height, 0,
                               m_spec.depth, format, data, xstride, ystride,
                               zstride);
    }

    bool ok = true;
    if (progress_callback && progress_callback(progress_callback_data, 0.0f))
        return ok;
    if (m_spec.tile_width && supports("tiles")) {  // Tiled image
        // Write chunks of a whole row of tiles at once. If tiles are
        // 64x64, a 2k image has 32 tiles across. That's fine for now (for
        // parallelization purposes), but as typical core counts increase,
        // we may someday want to revisit this to batch multiple rows.
        for (int z = 0; z < m_spec.depth; z += m_spec.tile_depth) {
            int zend = std::min(z + m_spec.z + m_spec.tile_depth,
                                m_spec.z + m_spec.depth);
            for (int y = 0; y < m_spec.height; y += m_spec.tile_height) {
                int yend      = std::min(y + m_spec.y + m_spec.tile_height,
                                    m_spec.y + m_spec.height);
                const char* d = (const char*)data + z * zstride + y * ystride;
                ok &= write_tiles(m_spec.x, m_spec.x + m_spec.width,
                                  y + m_spec.y, yend, z + m_spec.z, zend,
                                  format, d, xstride, ystride, zstride);
                if (progress_callback
                    && progress_callback(progress_callback_data,
                                         (float)(z * m_spec.height + y)
                                             / (m_spec.height * m_spec.depth)))
                    return ok;
            }
        }
    } else {  // Scanline image
        // Split into reasonable chunks -- try to use around 64 MB, but
        // round up to a multiple of the TIFF rows per strip (or 64).
        int rps   = m_spec.get_int_attribute("tiff:RowsPerStrip", 64);
        int chunk = std::max(1, (1 << 26) / int(m_spec.scanline_bytes(true)));
        chunk     = round_to_multiple(chunk, rps);
        for (int z = 0; z < m_spec.depth; ++z)
            for (int y = 0; y < m_spec.height && ok; y += chunk) {
                int yend      = std::min(y + m_spec.y + chunk,
                                    m_spec.y + m_spec.height);
                const char* d = (const char*)data + z * zstride + y * ystride;
                ok &= write_scanlines(y + m_spec.y, yend, z + m_spec.z, format,
                                      d, xstride, ystride);
                if (progress_callback
                    && progress_callback(progress_callback_data,
                                         (float)(z * m_spec.height + y)
                                             / (m_spec.height * m_spec.depth)))
                    return ok;
            }
    }
    if (progress_callback)
        progress_callback(progress_callback_data, 1.0f);

    return ok;
}



bool
ImageOutput::copy_image(ImageInput* in)
{
    if (!in) {
        errorf("copy_image: no input supplied");
        return false;
    }

    // Make sure the images are compatible in size
    const ImageSpec& inspec(in->spec());
    if (inspec.width != spec().width || inspec.height != spec().height
        || inspec.depth != spec().depth
        || inspec.nchannels != spec().nchannels) {
        errorf("Could not copy %d x %d x %d channels to %d x %d x %d channels",
               inspec.width, inspec.height, inspec.nchannels, spec().width,
               spec().height, spec().nchannels);
        return false;
    }

    // in most cases plugins don't allow to copy 0x0 images
    // but there are some exceptions (like in FITS plugin)
    // when we want to do this. Because 0x0 means there is no image
    // data in the file, we simply return true so the application thought
    // that everything went right.
    if (!spec().image_bytes())
        return true;

    if (spec().deep) {
        // Special case for ''deep'' images
        DeepData deepdata;
        bool ok = in->read_native_deep_image(in->current_subimage(),
                                             in->current_miplevel(), deepdata);
        if (ok)
            ok = write_deep_image(deepdata);
        else
            errorf("%s", in->geterror());  // copy err from in to out
        return ok;
    }

    // Naive implementation -- read the whole image and write it back out.
    // FIXME -- a smarter implementation would read scanlines or tiles at
    // a time, to minimize mem footprint.
    bool native = supports("channelformats") && inspec.channelformats.size();
    TypeDesc format = native ? TypeDesc::UNKNOWN : inspec.format;
    std::unique_ptr<char[]> pixels(new char[inspec.image_bytes(native)]);
    bool ok = in->read_image(in->current_subimage(), in->current_miplevel(), 0,
                             inspec.nchannels, format, &pixels[0]);
    if (ok)
        ok = write_image(format, &pixels[0]);
    else
        errorf("%s", in->geterror());  // copy err from in to out
    return ok;
}



bool
ImageOutput::copy_to_image_buffer(int xbegin, int xend, int ybegin, int yend,
                                  int zbegin, int zend, TypeDesc format,
                                  const void* data, stride_t xstride,
                                  stride_t ystride, stride_t zstride,
                                  void* image_buffer, TypeDesc buf_format)
{
    const ImageSpec& spec(this->spec());
    if (buf_format == TypeDesc::UNKNOWN)
        buf_format = spec.format;
    spec.auto_stride(xstride, ystride, zstride, format, spec.nchannels,
                     spec.width, spec.height);
    stride_t buf_xstride = spec.nchannels * buf_format.size();
    stride_t buf_ystride = buf_xstride * spec.width;
    stride_t buf_zstride = buf_ystride * spec.height;
    stride_t offset      = (xbegin - spec.x) * buf_xstride
                      + (ybegin - spec.y) * buf_ystride
                      + (zbegin - spec.z) * buf_zstride;
    int width = xend - xbegin, height = yend - ybegin, depth = zend - zbegin;
    imagesize_t npixels = imagesize_t(width) * imagesize_t(height)
                          * imagesize_t(depth);

    // Add dither if requested -- requires making a temporary staging area
    std::unique_ptr<float[]> ditherarea;
    unsigned int dither = spec.get_int_attribute("oiio:dither", 0);
    if (dither && format.is_floating_point()
        && buf_format.basetype == TypeDesc::UINT8) {
        stride_t pixelsize = spec.nchannels * sizeof(float);
        ditherarea.reset(new float[pixelsize * npixels]);
        OIIO::convert_image(spec.nchannels, width, height, depth, data, format,
                            xstride, ystride, zstride, ditherarea.get(),
                            TypeDesc::FLOAT, pixelsize, pixelsize * width,
                            pixelsize * stride_t(width) * stride_t(height));
        data            = ditherarea.get();
        format          = TypeDesc::FLOAT;
        xstride         = pixelsize;
        ystride         = xstride * width;
        zstride         = ystride * height;
        float ditheramp = spec.get_float_attribute("oiio:ditheramplitude",
                                                   1.0f / 255.0f);
        OIIO::add_dither(spec.nchannels, width, height, depth, (float*)data,
                         pixelsize, pixelsize * stride_t(width),
                         pixelsize * stride_t(width) * stride_t(height),
                         ditheramp, spec.alpha_channel, spec.z_channel, dither,
                         0, xbegin, ybegin, zbegin);
    }

    return OIIO::convert_image(spec.nchannels, width, height, depth, data,
                               format, xstride, ystride, zstride,
                               (char*)image_buffer + offset, buf_format,
                               buf_xstride, buf_ystride, buf_zstride);
}



bool
ImageOutput::copy_tile_to_image_buffer(int x, int y, int z, TypeDesc format,
                                       const void* data, stride_t xstride,
                                       stride_t ystride, stride_t zstride,
                                       void* image_buffer, TypeDesc buf_format)
{
    if (!m_spec.tile_width || !m_spec.tile_height) {
        errorf("Called write_tile for non-tiled image.");
        return false;
    }
    const ImageSpec& spec(this->spec());
    spec.auto_stride(xstride, ystride, zstride, format, spec.nchannels,
                     spec.tile_width, spec.tile_height);
    int xend = std::min(x + spec.tile_width, spec.x + spec.width);
    int yend = std::min(y + spec.tile_height, spec.y + spec.height);
    int zend = std::min(z + spec.tile_depth, spec.z + spec.depth);
    return copy_to_image_buffer(x, xend, y, yend, z, zend, format, data,
                                xstride, ystride, zstride, image_buffer,
                                buf_format);
}



bool
ImageOutput::has_error() const
{
    std::string* errptr = m_impl->m_errormessage.get();
    return (errptr && errptr->size());
}



std::string
ImageOutput::geterror(bool clear) const
{
    std::string e;
    std::string* errptr = m_impl->m_errormessage.get();
    if (errptr) {
        e = *errptr;
        if (clear)
            errptr->clear();
    }
    return e;
}



void
ImageOutput::threads(int n)
{
    m_impl->m_threads = n;
}



int
ImageOutput::threads() const
{
    return m_impl->m_threads;
}



OIIO_NAMESPACE_END
