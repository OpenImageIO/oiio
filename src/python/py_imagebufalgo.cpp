// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include "py_oiio.h"
#include <OpenImageIO/color.h>
#include <OpenImageIO/imagebufalgo.h>


namespace PyOpenImageIO {


class IBA_dummy {
};  // dummy class to establish a scope


bool
IBA_zero(ImageBuf& dst, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::zero(dst, roi, nthreads);
}

ImageBuf
IBA_zero_ret(ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::zero(roi, nthreads);
}



bool
IBA_fill(ImageBuf& dst, py::object values_tuple, ROI roi = ROI::All(),
         int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (dst.initialized())
        values.resize(dst.nchannels(), 0.0f);
    else if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::fill(dst, values, roi, nthreads);
}


bool
IBA_fill2(ImageBuf& dst, py::object top_tuple, py::object bottom_tuple,
          ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> top, bottom;
    py_to_stdvector(top, top_tuple);
    py_to_stdvector(bottom, bottom_tuple);
    if (dst.initialized()) {
        top.resize(dst.nchannels(), 0.0f);
        bottom.resize(dst.nchannels(), 0.0f);
    } else if (roi.defined()) {
        top.resize(roi.nchannels(), 0.0f);
        bottom.resize(roi.nchannels(), 0.0f);
    } else
        return false;
    OIIO_ASSERT(top.size() > 0 && bottom.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::fill(dst, top, bottom, roi, nthreads);
}


bool
IBA_fill4(ImageBuf& dst, py::object top_left_tuple, py::object top_right_tuple,
          py::object bottom_left_tuple, py::object bottom_right_tuple,
          ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> top_left, top_right, bottom_left, bottom_right;
    py_to_stdvector(top_left, top_left_tuple);
    py_to_stdvector(top_right, top_right_tuple);
    py_to_stdvector(bottom_left, bottom_left_tuple);
    py_to_stdvector(bottom_right, bottom_right_tuple);
    if (dst.initialized()) {
        top_left.resize(dst.nchannels(), 0.0f);
        top_right.resize(dst.nchannels(), 0.0f);
        bottom_left.resize(dst.nchannels(), 0.0f);
        bottom_right.resize(dst.nchannels(), 0.0f);
    } else if (roi.defined()) {
        top_left.resize(roi.nchannels(), 0.0f);
        top_right.resize(roi.nchannels(), 0.0f);
        bottom_left.resize(roi.nchannels(), 0.0f);
        bottom_right.resize(roi.nchannels(), 0.0f);
    } else
        return false;
    OIIO_ASSERT(top_left.size() > 0 && top_right.size() > 0
                && bottom_left.size() > 0 && bottom_right.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::fill(dst, top_left, top_right, bottom_left,
                              bottom_right, roi, nthreads);
}



ImageBuf
IBA_fill_ret(py::object values_tuple, ROI roi, int nthreads = 0)
{
    ImageBuf result;
    IBA_fill(result, values_tuple, roi, nthreads);
    return result;
}


ImageBuf
IBA_fill2_ret(py::object top_tuple, py::object bottom_tuple, ROI roi,
              int nthreads = 0)
{
    ImageBuf result;
    IBA_fill2(result, top_tuple, bottom_tuple, roi, nthreads);
    return result;
}


ImageBuf
IBA_fill4_ret(py::object top_left_tuple, py::object top_right_tuple,
              py::object bottom_left_tuple, py::object bottom_right_tuple,
              ROI roi, int nthreads = 0)
{
    ImageBuf result;
    IBA_fill4(result, top_left_tuple, top_right_tuple, bottom_left_tuple,
              bottom_right_tuple, roi, nthreads);
    return result;
}



bool
IBA_checker(ImageBuf& dst, int width, int height, int depth,
            py::object color1_tuple, py::object color2_tuple, int xoffset,
            int yoffset, int zoffset, ROI roi, int nthreads)
{
    std::vector<float> color1, color2;
    py_to_stdvector(color1, color1_tuple);
    py_to_stdvector(color2, color2_tuple);
    if (dst.initialized())
        color1.resize(dst.nchannels(), 0.0f);
    else if (roi.defined())
        color1.resize(roi.nchannels(), 0.0f);
    else
        return false;
    if (dst.initialized())
        color2.resize(dst.nchannels(), 0.0f);
    else if (roi.defined())
        color2.resize(roi.nchannels(), 0.0f);
    else
        return false;
    py::gil_scoped_release gil;
    return ImageBufAlgo::checker(dst, width, height, depth, color1, color2,
                                 xoffset, yoffset, zoffset, roi, nthreads);
}



ImageBuf
IBA_checker_ret(int width, int height, int depth, py::object color1_tuple,
                py::object color2_tuple, int xoffset, int yoffset, int zoffset,
                ROI roi, int nthreads)
{
    ImageBuf result;
    IBA_checker(result, width, height, depth, color1_tuple, color2_tuple,
                xoffset, yoffset, zoffset, roi, nthreads);
    return result;
}



bool
IBA_noise(ImageBuf& dst, const std::string& type, float A, float B, bool mono,
          int seed, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::noise(dst, type, A, B, mono, seed, roi, nthreads);
}


ImageBuf
IBA_noise_ret(const std::string& type, float A, float B, bool mono, int seed,
              ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::noise(type, A, B, mono, seed, roi, nthreads);
}



bool
IBA_channels(ImageBuf& dst, const ImageBuf& src, py::tuple channelorder_,
             py::tuple newchannelnames_, bool shuffle_channel_names,
             int nthreads)
{
    size_t nchannels = (size_t)len(channelorder_);
    if (nchannels < 1) {
        dst.errorfmt("No channels selected");
        return false;
    }
    std::vector<int> channelorder(nchannels, -1);
    std::vector<float> channelvalues(nchannels, 0.0f);
    for (size_t i = 0; i < nchannels; ++i) {
        auto orderi = channelorder_[i];
        if (py::isinstance<py::int_>(orderi)) {
            channelorder[i] = orderi.cast<py::int_>();
        } else if (py::isinstance<py::float_>(orderi)) {
            channelvalues[i] = orderi.cast<py::float_>();
        } else if (py::isinstance<py::str>(orderi)) {
            std::string chname = orderi.cast<py::str>();
            for (int c = 0; c < src.nchannels(); ++c) {
                if (src.spec().channelnames[c] == chname)
                    channelorder[i] = c;
            }
        }
    }
    std::vector<std::string> newchannelnames;
    py_to_stdvector(newchannelnames, newchannelnames_);
    if (newchannelnames.size() != 0 && newchannelnames.size() != nchannels) {
        dst.errorfmt("Inconsistent number of channel arguments");
        return false;
    }
    py::gil_scoped_release gil;
    return ImageBufAlgo::channels(dst, src, (int)nchannels, &channelorder[0],
                                  channelvalues.size() ? &channelvalues[0]
                                                       : nullptr,
                                  newchannelnames.size() ? &newchannelnames[0]
                                                         : nullptr,
                                  shuffle_channel_names, nthreads);
}


ImageBuf
IBA_channels_ret(const ImageBuf& src, py::tuple channelorder,
                 py::tuple newchannelnames, bool shuffle_channel_names,
                 int nthreads)
{
    ImageBuf result;
    IBA_channels(result, src, channelorder, newchannelnames,
                 shuffle_channel_names, nthreads);
    return result;
}


bool
IBA_channel_append(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B, ROI roi,
                   int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::channel_append(dst, A, B, roi, nthreads);
}


ImageBuf
IBA_channel_append_ret(const ImageBuf& A, const ImageBuf& B, ROI roi,
                       int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::channel_append(A, B, roi, nthreads);
}



bool
IBA_deepen(ImageBuf& dst, const ImageBuf& src, float zvalue, ROI roi,
           int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deepen(dst, src, zvalue, roi, nthreads);
}



ImageBuf
IBA_deepen_ret(const ImageBuf& src, float zvalue, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deepen(src, zvalue, roi, nthreads);
}



bool
IBA_flatten(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flatten(dst, src, roi, nthreads);
}



ImageBuf
IBA_flatten_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flatten(src, roi, nthreads);
}



bool
IBA_deep_merge(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               bool occlusion_cull, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deep_merge(dst, A, B, occlusion_cull, roi, nthreads);
}



ImageBuf
IBA_deep_merge_ret(const ImageBuf& A, const ImageBuf& B, bool occlusion_cull,
                   ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deep_merge(A, B, occlusion_cull, roi, nthreads);
}



bool
IBA_deep_holdout(ImageBuf& dst, const ImageBuf& src, const ImageBuf& holdout,
                 ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deep_holdout(dst, src, holdout, roi, nthreads);
}



ImageBuf
IBA_deep_holdout_ret(const ImageBuf& src, const ImageBuf& holdout, ROI roi,
                     int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::deep_holdout(src, holdout, roi, nthreads);
}



bool
IBA_copy(ImageBuf& dst, const ImageBuf& src, TypeDesc convert, ROI roi,
         int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::copy(dst, src, convert, roi, nthreads);
}



ImageBuf
IBA_copy_ret(const ImageBuf& src, TypeDesc convert, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::copy(src, convert, roi, nthreads);
}



bool
IBA_crop(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::crop(dst, src, roi, nthreads);
}



ImageBuf
IBA_crop_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::crop(src, roi, nthreads);
}



bool
IBA_cut(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::cut(dst, src, roi, nthreads);
}



ImageBuf
IBA_cut_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::cut(src, roi, nthreads);
}



bool
IBA_paste(ImageBuf& dst, int xbegin, int ybegin, int zbegin, int chbegin,
          const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::paste(dst, xbegin, ybegin, zbegin, chbegin, src, roi,
                               nthreads);
}



bool
IBA_rotate90(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate90(dst, src, roi, nthreads);
}

ImageBuf
IBA_rotate90_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate90(src, roi, nthreads);
}


bool
IBA_rotate180(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate180(dst, src, roi, nthreads);
}

ImageBuf
IBA_rotate180_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate180(src, roi, nthreads);
}


bool
IBA_rotate270(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate270(dst, src, roi, nthreads);
}

ImageBuf
IBA_rotate270_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate270(src, roi, nthreads);
}


bool
IBA_flip(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flip(dst, src, roi, nthreads);
}

ImageBuf
IBA_flip_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flip(src, roi, nthreads);
}


bool
IBA_flop(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flop(dst, src, roi, nthreads);
}

ImageBuf
IBA_flop_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::flop(src, roi, nthreads);
}


bool
IBA_reorient(ImageBuf& dst, const ImageBuf& src, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::reorient(dst, src, nthreads);
}

ImageBuf
IBA_reorient_ret(const ImageBuf& src, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::reorient(src, nthreads);
}


bool
IBA_transpose(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::transpose(dst, src, roi, nthreads);
}

ImageBuf
IBA_transpose_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::transpose(src, roi, nthreads);
}


bool
IBA_circular_shift(ImageBuf& dst, const ImageBuf& src, int xshift, int yshift,
                   int zshift, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::circular_shift(dst, src, xshift, yshift, zshift, roi,
                                        nthreads);
}

ImageBuf
IBA_circular_shift_ret(const ImageBuf& src, int xshift, int yshift, int zshift,
                       ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::circular_shift(src, xshift, yshift, zshift, roi,
                                        nthreads);
}



bool
IBA_add_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::add(dst, A, &values[0], roi, nthreads);
}

bool
IBA_add_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::add(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_add_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::add(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_add_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    ImageBuf result = ImageBufAlgo::add(A, B, roi, nthreads);
    return result;
}



bool
IBA_sub_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::sub(dst, A, &values[0], roi, nthreads);
}

bool
IBA_sub_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::sub(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_sub_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::sub(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_sub_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::sub(A, B, roi, nthreads);
}



bool
IBA_absdiff_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::absdiff(dst, A, &values[0], roi, nthreads);
}

bool
IBA_absdiff_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
                   ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::absdiff(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_absdiff_color_ref(const ImageBuf& A, py::object values_tuple,
                      ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::absdiff(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_absdiff_images_ret(const ImageBuf& A, const ImageBuf& B,
                       ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    ImageBuf result = ImageBufAlgo::absdiff(A, B, roi, nthreads);
    return result;
}



bool
IBA_abs(ImageBuf& dst, const ImageBuf& A, ROI roi = ROI::All(),
        int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::abs(dst, A, roi, nthreads);
}

ImageBuf
IBA_abs_ret(const ImageBuf& A, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    ImageBuf result = ImageBufAlgo::abs(A, roi, nthreads);
    return result;
}



bool
IBA_mul_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::mul(dst, A, &values[0], roi, nthreads);
}

bool
IBA_mul_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::mul(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_mul_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::mul(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_mul_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::mul(A, B, roi, nthreads);
}



bool
IBA_div_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::div(dst, A, &values[0], roi, nthreads);
}

bool
IBA_div_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::div(dst, A, B, roi, nthreads);
}



ImageBuf
IBA_div_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::div(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_div_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::div(A, B, roi, nthreads);
}



bool
IBA_mad_color(ImageBuf& dst, const ImageBuf& A, py::object Bvalues_tuple,
              py::object Cvalues_tuple, ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> Bvalues, Cvalues;
    py_to_stdvector(Bvalues, Bvalues_tuple);
    if (roi.defined())
        Bvalues.resize(roi.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else if (A.initialized())
        Bvalues.resize(A.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else
        return false;
    py_to_stdvector(Cvalues, Cvalues_tuple);
    if (roi.defined())
        Cvalues.resize(roi.nchannels(), Cvalues.size() ? Cvalues.back() : 0.0f);
    else if (A.initialized())
        Cvalues.resize(A.nchannels(), Cvalues.size() ? Cvalues.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(Bvalues.size() > 0 && Cvalues.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::mad(dst, A, Bvalues, Cvalues, roi, nthreads);
}

bool
IBA_mad_ici(ImageBuf& dst, const ImageBuf& A, py::object Bvalues_tuple,
            const ImageBuf& C, ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> Bvalues, Cvalues;
    py_to_stdvector(Bvalues, Bvalues_tuple);
    if (roi.defined())
        Bvalues.resize(roi.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else if (A.initialized())
        Bvalues.resize(A.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(Bvalues.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::mad(dst, A, Bvalues, C, roi, nthreads);
}

bool
IBA_mad_cii(ImageBuf& dst, py::object Avalues_tuple, const ImageBuf& B,
            const ImageBuf& C, ROI roi = ROI::All(), int nthreads = 0)
{
    return IBA_mad_ici(dst, B, Avalues_tuple, C, roi, nthreads);
}

bool
IBA_mad_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               const ImageBuf& C, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::mad(dst, A, B, C, roi, nthreads);
}



ImageBuf
IBA_mad_color_ret(const ImageBuf& A, py::object Bvalues_tuple,
                  py::object Cvalues_tuple, ROI roi = ROI::All(),
                  int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> Bvalues, Cvalues;
    py_to_stdvector(Bvalues, Bvalues_tuple);
    if (roi.defined())
        Bvalues.resize(roi.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else if (A.initialized())
        Bvalues.resize(A.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else
        return result;
    py_to_stdvector(Cvalues, Cvalues_tuple);
    if (roi.defined())
        Cvalues.resize(roi.nchannels(), Cvalues.size() ? Cvalues.back() : 0.0f);
    else if (A.initialized())
        Cvalues.resize(A.nchannels(), Cvalues.size() ? Cvalues.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(Bvalues.size() > 0 && Cvalues.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::mad(A, Bvalues, Cvalues, roi, nthreads);
    return result;
}

ImageBuf
IBA_mad_ici_ret(const ImageBuf& A, py::object Bvalues_tuple, const ImageBuf& C,
                ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> Bvalues, Cvalues;
    py_to_stdvector(Bvalues, Bvalues_tuple);
    if (roi.defined())
        Bvalues.resize(roi.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else if (A.initialized())
        Bvalues.resize(A.nchannels(), Bvalues.size() ? Bvalues.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(Bvalues.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::mad(A, Bvalues, C, roi, nthreads);
    return result;
}

ImageBuf
IBA_mad_cii_ret(py::object Avalues_tuple, const ImageBuf& B, const ImageBuf& C,
                ROI roi = ROI::All(), int nthreads = 0)
{
    return IBA_mad_ici_ret(B, Avalues_tuple, C, roi, nthreads);
}

ImageBuf
IBA_mad_images_ret(const ImageBuf& A, const ImageBuf& B, const ImageBuf& C,
                   ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::mad(A, B, C, roi, nthreads);
}



bool
IBA_invert(ImageBuf& dst, const ImageBuf& A, ROI roi = ROI::All(),
           int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::invert(dst, A, roi, nthreads);
}


ImageBuf
IBA_invert_ret(const ImageBuf& A, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::invert(A, roi, nthreads);
}



bool
IBA_pow_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::pow(dst, A, values, roi, nthreads);
}


ImageBuf
IBA_pow_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::pow(A, values, roi, nthreads);
    return result;
}



bool
IBA_min_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::min(dst, A, values, roi, nthreads);
}

bool
IBA_min_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::min(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_min_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::min(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_min_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    ImageBuf result = ImageBufAlgo::min(A, B, roi, nthreads);
    return result;
}



bool
IBA_max_color(ImageBuf& dst, const ImageBuf& A, py::object values_tuple,
              ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return false;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    return ImageBufAlgo::max(dst, A, values, roi, nthreads);
}

bool
IBA_max_images(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
               ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::max(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_max_color_ret(const ImageBuf& A, py::object values_tuple,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> values;
    py_to_stdvector(values, values_tuple);
    if (roi.defined())
        values.resize(roi.nchannels(), values.size() ? values.back() : 0.0f);
    else if (A.initialized())
        values.resize(A.nchannels(), values.size() ? values.back() : 0.0f);
    else
        return result;
    OIIO_ASSERT(values.size() > 0);
    py::gil_scoped_release gil;
    result = ImageBufAlgo::max(A, values, roi, nthreads);
    return result;
}

ImageBuf
IBA_max_images_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    py::gil_scoped_release gil;
    ImageBuf result = ImageBufAlgo::max(A, B, roi, nthreads);
    return result;
}



bool
IBA_clamp(ImageBuf& dst, const ImageBuf& src, py::object min_, py::object max_,
          bool clampalpha01 = false, ROI roi = ROI::All(), int nthreads = 0)
{
    if (!src.initialized())
        return false;
    std::vector<float> min, max;
    py_to_stdvector(min, min_);
    py_to_stdvector(max, max_);
    min.resize(src.nchannels(), -std::numeric_limits<float>::max());
    max.resize(src.nchannels(), std::numeric_limits<float>::max());
    py::gil_scoped_release gil;
    return ImageBufAlgo::clamp(dst, src, min, max, clampalpha01, roi, nthreads);
}

ImageBuf
IBA_clamp_ret(const ImageBuf& src, py::object min_, py::object max_,
              bool clampalpha01 = false, ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf dst;
    IBA_clamp(dst, src, min_, max_, clampalpha01, roi, nthreads);
    return dst;
}



bool
IBA_channel_sum_weight(ImageBuf& dst, const ImageBuf& src,
                       py::object weight_tuple, ROI roi = ROI::All(),
                       int nthreads = 0)
{
    std::vector<float> weight;
    py_to_stdvector(weight, weight_tuple);
    if (!src.initialized()) {
        dst.errorfmt("Uninitialized source image for channel_sum");
        return false;
    }
    if (weight.size() == 0)
        weight.resize(src.nchannels(), 1.0f);  // no weights -> uniform
    else
        weight.resize(src.nchannels(), 0.0f);  // missing weights -> 0
    py::gil_scoped_release gil;
    return ImageBufAlgo::channel_sum(dst, src, &weight[0], roi, nthreads);
}

bool
IBA_channel_sum(ImageBuf& dst, const ImageBuf& src, ROI roi = ROI::All(),
                int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::channel_sum(dst, src, NULL, roi, nthreads);
}



ImageBuf
IBA_channel_sum_weight_ret(const ImageBuf& src, py::object weight_tuple,
                           ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf result;
    std::vector<float> weight;
    py_to_stdvector(weight, weight_tuple);
    if (!src.initialized()) {
        result.errorfmt("Uninitialized source image for channel_sum");
        return result;
    }
    if (weight.size() == 0)
        weight.resize(src.nchannels(), 1.0f);  // no weights -> uniform
    else
        weight.resize(src.nchannels(), 0.0f);  // missing weights -> 0
    py::gil_scoped_release gil;
    result = ImageBufAlgo::channel_sum(src, weight, roi, nthreads);
    return result;
}

ImageBuf
IBA_channel_sum_ret(const ImageBuf& src, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::channel_sum(src, {}, roi, nthreads);
}



bool
IBA_color_map_values(ImageBuf& dst, const ImageBuf& src, int srcchannel,
                     int nknots, int channels, py::object knots_tuple,
                     ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> knots;
    py_to_stdvector(knots, knots_tuple);
    if (!src.initialized()) {
        dst.errorfmt("Uninitialized source image for color_map");
        return false;
    }
    if (!knots.size()) {
        dst.errorfmt("No knot values supplied");
        return false;
    }
    py::gil_scoped_release gil;
    return ImageBufAlgo::color_map(dst, src, srcchannel, nknots, channels,
                                   knots, roi, nthreads);
}


bool
IBA_color_map_name(ImageBuf& dst, const ImageBuf& src, int srcchannel,
                   const std::string& mapname, ROI roi = ROI::All(),
                   int nthreads = 0)
{
    if (!src.initialized()) {
        dst.errorfmt("Uninitialized source image for color_map");
        return false;
    }
    py::gil_scoped_release gil;
    return ImageBufAlgo::color_map(dst, src, srcchannel, mapname, roi,
                                   nthreads);
}



ImageBuf
IBA_color_map_values_ret(const ImageBuf& src, int srcchannel, int nknots,
                         int channels, py::object knots_tuple,
                         ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf dst;
    IBA_color_map_values(dst, src, srcchannel, nknots, channels, knots_tuple,
                         roi, nthreads);
    return dst;
}


ImageBuf
IBA_color_map_name_ret(const ImageBuf& src, int srcchannel,
                       const std::string& mapname, ROI roi = ROI::All(),
                       int nthreads = 0)
{
    ImageBuf dst;
    IBA_color_map_name(dst, src, srcchannel, mapname, roi, nthreads);
    return dst;
}



bool
IBA_rangeexpand(ImageBuf& dst, const ImageBuf& src, bool useluma = false,
                ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rangeexpand(dst, src, useluma, roi, nthreads);
}


ImageBuf
IBA_rangeexpand_ret(const ImageBuf& src, bool useluma = false,
                    ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rangeexpand(src, useluma, roi, nthreads);
}


bool
IBA_rangecompress(ImageBuf& dst, const ImageBuf& src, bool useluma = false,
                  ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rangecompress(dst, src, useluma, roi, nthreads);
}



ImageBuf
IBA_rangecompress_ret(const ImageBuf& src, bool useluma = false,
                      ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rangecompress(src, useluma, roi, nthreads);
}



bool
IBA_premult(ImageBuf& dst, const ImageBuf& src, ROI roi = ROI::All(),
            int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::premult(dst, src, roi, nthreads);
}


ImageBuf
IBA_premult_ret(const ImageBuf& src, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::premult(src, roi, nthreads);
}


bool
IBA_unpremult(ImageBuf& dst, const ImageBuf& src, ROI roi = ROI::All(),
              int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::unpremult(dst, src, roi, nthreads);
}



ImageBuf
IBA_unpremult_ret(const ImageBuf& src, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::unpremult(src, roi, nthreads);
}


bool
IBA_repremult(ImageBuf& dst, const ImageBuf& src, ROI roi = ROI::All(),
              int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::repremult(dst, src, roi, nthreads);
}


ImageBuf
IBA_repremult_ret(const ImageBuf& src, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::repremult(src, roi, nthreads);
}



bool
IBA_contrast_remap(ImageBuf& dst, const ImageBuf& src, py::object black_,
                   py::object white_, py::object min_, py::object max_,
                   py::object scontrast_, py::object sthresh_,
                   ROI roi = ROI::All(), int nthreads = 0)
{
    if (!src.initialized())
        return false;
    std::vector<float> black, white, sthresh, scontrast, min, max;
    py_to_stdvector(black, black_);
    py_to_stdvector(white, white_);
    py_to_stdvector(min, min_);
    py_to_stdvector(max, max_);
    py_to_stdvector(sthresh, sthresh_);
    py_to_stdvector(scontrast, scontrast_);
    // black.resize (src.nchannels(), black.size() ? black.back() : 0.0f);
    // white.resize (src.nchannels(), white.size() ? white.back() : 1.0f);
    // min.resize (src.nchannels(), min.size() ? min.back() : 0.0f);
    // max.resize (src.nchannels(), max.size() ? max.back() : 1.0f);
    // sthresh.resize (src.nchannels(), sthresh.size() ? sthresh.back() : 0.5f);
    // scontrast.resize (src.nchannels(), scontrast.size() ? scontrast.back() : 1.0f);
    py::gil_scoped_release gil;
    return ImageBufAlgo::contrast_remap(dst, src, black, white, min, max,
                                        scontrast, sthresh, roi, nthreads);
}

ImageBuf
IBA_contrast_remap_ret(const ImageBuf& src, py::object black_,
                       py::object white_, py::object min_, py::object max_,
                       py::object scontrast_, py::object sthresh_,
                       ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf dst;
    IBA_contrast_remap(dst, src, black_, white_, min_, max_, scontrast_,
                       sthresh_, roi, nthreads);
    return dst;
}



ImageBufAlgo::PixelStats
IBA_computePixelStats_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::computePixelStats(src, roi, nthreads);
}

// deprecated:
bool
IBA_computePixelStats(const ImageBuf& src, ImageBufAlgo::PixelStats& stats,
                      ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    stats = ImageBufAlgo::computePixelStats(src, roi, nthreads);
    return stats.min.size() != 0;
}



ImageBufAlgo::CompareResults
IBA_compare_ret(const ImageBuf& A, const ImageBuf& B, float failthresh,
                float warnthresh, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::compare(A, B, failthresh, warnthresh, roi, nthreads);
}

// deprecated:
bool
IBA_compare(const ImageBuf& A, const ImageBuf& B, float failthresh,
            float warnthresh, ImageBufAlgo::CompareResults& result, ROI roi,
            int nthreads)
{
    py::gil_scoped_release gil;
    result = ImageBufAlgo::compare(A, B, failthresh, warnthresh, roi, nthreads);
    return result.error;
}



bool
IBA_compare_Yee(const ImageBuf& A, const ImageBuf& B,
                ImageBufAlgo::CompareResults& result, float luminance,
                float fov, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::compare_Yee(A, B, result, luminance, fov, roi,
                                     nthreads);
}



std::string
IBA_computePixelHashSHA1(const ImageBuf& src, const std::string& extrainfo,
                         ROI roi = ROI::All(), int blocksize = 0,
                         int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::computePixelHashSHA1(src, extrainfo, roi, blocksize,
                                              nthreads);
}



bool
IBA_warp(ImageBuf& dst, const ImageBuf& src, py::object values_M,
         const std::string& filtername = "", float filterwidth = 0.0f,
         bool recompute_roi = false, const std::string& wrapname = "default",
         ROI roi = ROI::All(), int nthreads = 0)
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    std::vector<float> M;
    py_to_stdvector(M, values_M);
    if (M.size() != 9)
        return false;
    py::gil_scoped_release gil;
    return ImageBufAlgo::warp(dst, src, *(Imath::M33f*)&M[0], filtername,
                              filterwidth, recompute_roi, wrap, roi, nthreads);
}


ImageBuf
IBA_warp_ret(const ImageBuf& src, py::object values_M,
             const std::string& filtername = "", float filterwidth = 0.0f,
             bool recompute_roi          = false,
             const std::string& wrapname = "default", ROI roi = ROI::All(),
             int nthreads = 0)
{
    ImageBuf dst;
    IBA_warp(dst, src, values_M, filtername, filterwidth, recompute_roi,
             wrapname, roi, nthreads);
    return dst;
}



bool
IBA_rotate(ImageBuf& dst, const ImageBuf& src, float angle,
           const std::string& filtername = "", float filterwidth = 0.0f,
           bool recompute_roi = false, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate(dst, src, angle, filtername, filterwidth,
                                recompute_roi, roi, nthreads);
}


bool
IBA_rotate2(ImageBuf& dst, const ImageBuf& src, float angle, float center_x,
            float center_y, const std::string& filtername = "",
            float filterwidth = 0.0f, bool recompute_roi = false,
            ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate(dst, src, angle, center_x, center_y, filtername,
                                filterwidth, recompute_roi, roi, nthreads);
}



ImageBuf
IBA_rotate_ret(const ImageBuf& src, float angle,
               const std::string& filtername = "", float filterwidth = 0.0f,
               bool recompute_roi = false, ROI roi = ROI::All(),
               int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate(src, angle, filtername, filterwidth,
                                recompute_roi, roi, nthreads);
}


ImageBuf
IBA_rotate2_ret(const ImageBuf& src, float angle, float center_x,
                float center_y, const std::string& filtername = "",
                float filterwidth = 0.0f, bool recompute_roi = false,
                ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::rotate(src, angle, center_x, center_y, filtername,
                                filterwidth, recompute_roi, roi, nthreads);
}



bool
IBA_resize(ImageBuf& dst, const ImageBuf& src,
           const std::string& filtername = "", float filterwidth = 0.0f,
           ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::resize(dst, src, filtername, filterwidth, roi,
                                nthreads);
}

ImageBuf
IBA_resize_ret(const ImageBuf& src, const std::string& filtername = "",
               float filterwidth = 0.0f, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::resize(src, filtername, filterwidth, roi, nthreads);
}



bool
IBA_resample(ImageBuf& dst, const ImageBuf& src, bool interpolate, ROI roi,
             int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::resample(dst, src, interpolate, roi, nthreads);
}

ImageBuf
IBA_resample_ret(const ImageBuf& src, bool interpolate, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::resample(src, interpolate, roi, nthreads);
}



bool
IBA_fit(ImageBuf& dst, const ImageBuf& src, const std::string& filtername = "",
        float filterwidth = 0.0f, const std::string& fillmode = "letterbox",
        bool exact = false, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fit(dst, src, filtername, filterwidth, fillmode, exact,
                             roi, nthreads);
}

ImageBuf
IBA_fit_ret(const ImageBuf& src, const std::string& filtername = "",
            float filterwidth = 0.0f, const std::string& fillmode = "letterbox",
            bool exact = false, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fit(src, filtername, filterwidth, fillmode, exact, roi,
                             nthreads);
}



// DEPRECATED(2.3)
bool
IBA_fit_old(ImageBuf& dst, const ImageBuf& src,
            const std::string& filtername = "", float filterwidth = 0.0f,
            bool exact = false, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fit(dst, src, filtername, filterwidth, "letterbox",
                             exact, roi, nthreads);
}

// DEPRECATED(2.3)
ImageBuf
IBA_fit_ret_old(const ImageBuf& src, const std::string& filtername = "",
                float filterwidth = 0.0f, bool exact = false,
                ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fit(src, filtername, filterwidth, "letterbox", exact,
                             roi, nthreads);
}



bool
IBA_make_kernel(ImageBuf& dst, const std::string& name, float width,
                float height, float depth, bool normalize)
{
    py::gil_scoped_release gil;
    dst = ImageBufAlgo::make_kernel(name, width, height, depth, normalize);
    return !dst.has_error();
}

ImageBuf
IBA_make_kernel_ret(const std::string& name, float width, float height,
                    float depth, bool normalize)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::make_kernel(name, width, height, depth, normalize);
}



bool
IBA_convolve(ImageBuf& dst, const ImageBuf& src, const ImageBuf& kernel,
             bool normalize, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::convolve(dst, src, kernel, normalize, roi, nthreads);
}

ImageBuf
IBA_convolve_ret(const ImageBuf& src, const ImageBuf& kernel, bool normalize,
                 ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::convolve(src, kernel, normalize, roi, nthreads);
}



bool
IBA_unsharp_mask(ImageBuf& dst, const ImageBuf& src, const std::string& kernel,
                 float width, float contrast, float threshold, ROI roi,
                 int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::unsharp_mask(dst, src, kernel, width, contrast,
                                      threshold, roi, nthreads);
}

ImageBuf
IBA_unsharp_mask_ret(const ImageBuf& src, const std::string& kernel,
                     float width, float contrast, float threshold, ROI roi,
                     int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::unsharp_mask(src, kernel, width, contrast, threshold,
                                      roi, nthreads);
}



bool
IBA_median_filter(ImageBuf& dst, const ImageBuf& src, int width, int height,
                  ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::median_filter(dst, src, width, height, roi, nthreads);
}

ImageBuf
IBA_median_filter_ret(const ImageBuf& src, int width, int height, ROI roi,
                      int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::median_filter(src, width, height, roi, nthreads);
}



bool
IBA_dilate(ImageBuf& dst, const ImageBuf& src, int width, int height, ROI roi,
           int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::dilate(dst, src, width, height, roi, nthreads);
}

ImageBuf
IBA_dilate_ret(const ImageBuf& src, int width, int height, ROI roi,
               int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::dilate(src, width, height, roi, nthreads);
}



bool
IBA_erode(ImageBuf& dst, const ImageBuf& src, int width, int height, ROI roi,
          int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::erode(dst, src, width, height, roi, nthreads);
}

ImageBuf
IBA_erode_ret(const ImageBuf& src, int width, int height, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::erode(src, width, height, roi, nthreads);
}



bool
IBA_laplacian(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::laplacian(dst, src, roi, nthreads);
}

ImageBuf
IBA_laplacian_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::laplacian(src, roi, nthreads);
}



bool
IBA_fft(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fft(dst, src, roi, nthreads);
}

ImageBuf
IBA_fft_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fft(src, roi, nthreads);
}



bool
IBA_ifft(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ifft(dst, src, roi, nthreads);
}

ImageBuf
IBA_ifft_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ifft(src, roi, nthreads);
}



bool
IBA_polar_to_complex(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::polar_to_complex(dst, src, roi, nthreads);
}

ImageBuf
IBA_polar_to_complex_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::polar_to_complex(src, roi, nthreads);
}



bool
IBA_complex_to_polar(ImageBuf& dst, const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::complex_to_polar(dst, src, roi, nthreads);
}

ImageBuf
IBA_complex_to_polar_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::complex_to_polar(src, roi, nthreads);
}



bool
IBA_fillholes_pushpull(ImageBuf& dst, const ImageBuf& src, ROI roi,
                       int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fillholes_pushpull(dst, src, roi, nthreads);
}

ImageBuf
IBA_fillholes_pushpull_ret(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fillholes_pushpull(src, roi, nthreads);
}



bool
IBA_over(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
         ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::over(dst, A, B, roi, nthreads);
}

ImageBuf
IBA_over_ret(const ImageBuf& A, const ImageBuf& B, ROI roi = ROI::All(),
             int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::over(A, B, roi, nthreads);
}



bool
IBA_zover(ImageBuf& dst, const ImageBuf& A, const ImageBuf& B,
          bool z_zeroisinf = false, ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::zover(dst, A, B, z_zeroisinf, roi, nthreads);
}

ImageBuf
IBA_zover_ret(const ImageBuf& A, const ImageBuf& B, bool z_zeroisinf = false,
              ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::zover(A, B, z_zeroisinf, roi, nthreads);
}



bool
IBA_colorconvert(ImageBuf& dst, const ImageBuf& src, const std::string& from,
                 const std::string& to, bool unpremult = true,
                 ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::colorconvert(dst, src, from, to, unpremult, "", "",
                                      nullptr, roi, nthreads);
}


bool
IBA_colorconvert_colorconfig(ImageBuf& dst, const ImageBuf& src,
                             const std::string& from, const std::string& to,
                             bool unpremult                   = true,
                             const std::string& context_key   = "",
                             const std::string& context_value = "",
                             const std::string& colorconfig   = "",
                             ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::colorconvert(dst, src, from, to, unpremult,
                                      context_key, context_value, &config, roi,
                                      nthreads);
}



ImageBuf
IBA_colorconvert_ret(const ImageBuf& src, const std::string& from,
                     const std::string& to, bool unpremult = true,
                     ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::colorconvert(src, from, to, unpremult, "", "", nullptr,
                                      roi, nthreads);
}


ImageBuf
IBA_colorconvert_colorconfig_ret(const ImageBuf& src, const std::string& from,
                                 const std::string& to, bool unpremult = true,
                                 const std::string& context_key   = "",
                                 const std::string& context_value = "",
                                 const std::string& colorconfig   = "",
                                 ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::colorconvert(src, from, to, unpremult, context_key,
                                      context_value, &config, roi, nthreads);
}



bool
IBA_colormatrixtransform(ImageBuf& dst, const ImageBuf& src,
                         const py::object& Mobj, bool unpremult = true,
                         ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> Mvals;
    bool ok = py_to_stdvector(Mvals, Mobj);
    if (!ok || Mvals.size() != 16) {
        dst.errorfmt(
            "colormatrixtransform did not receive 16 elements to make a 4x4 matrix");
        return false;
    }
    py::gil_scoped_release gil;
    const Imath::M44f* M = (const Imath::M44f*)Mvals.data();
    return ImageBufAlgo::colormatrixtransform(dst, src, *M, unpremult, roi,
                                              nthreads);
}


ImageBuf
IBA_colormatrixtransform_ret(const ImageBuf& src, const py::object& Mobj,
                             bool unpremult = true, ROI roi = ROI::All(),
                             int nthreads = 0)
{
    ImageBuf dst;
    IBA_colormatrixtransform(dst, src, Mobj, unpremult, roi, nthreads);
    return dst;
}



bool
IBA_ociolook(ImageBuf& dst, const ImageBuf& src, const std::string& looks,
             const std::string& from, const std::string& to, bool unpremult,
             bool inverse, const std::string& context_key,
             const std::string& context_value, ROI roi = ROI::All(),
             int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociolook(dst, src, looks, from, to, unpremult, inverse,
                                  context_key, context_value, NULL, roi,
                                  nthreads);
}


bool
IBA_ociolook_colorconfig(ImageBuf& dst, const ImageBuf& src,
                         const std::string& looks, const std::string& from,
                         const std::string& to, bool unpremult, bool inverse,
                         const std::string& context_key,
                         const std::string& context_value,
                         const std::string& colorconfig = "",
                         ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociolook(dst, src, looks, from, to, unpremult, inverse,
                                  context_key, context_value, &config, roi,
                                  nthreads);
}



ImageBuf
IBA_ociolook_ret(const ImageBuf& src, const std::string& looks,
                 const std::string& from, const std::string& to, bool unpremult,
                 bool inverse, const std::string& context_key,
                 const std::string& context_value, ROI roi = ROI::All(),
                 int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociolook(src, looks, from, to, unpremult, inverse,
                                  context_key, context_value, NULL, roi,
                                  nthreads);
}


ImageBuf
IBA_ociolook_colorconfig_ret(const ImageBuf& src, const std::string& looks,
                             const std::string& from, const std::string& to,
                             bool unpremult, bool inverse,
                             const std::string& context_key,
                             const std::string& context_value,
                             const std::string& colorconfig = "",
                             ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociolook(src, looks, from, to, unpremult, inverse,
                                  context_key, context_value, &config, roi,
                                  nthreads);
}



bool
IBA_ociodisplay(ImageBuf& dst, const ImageBuf& src, const std::string& display,
                const std::string& view, const std::string& from,
                const std::string& looks, bool unpremult,
                const std::string& context_key,
                const std::string& context_value, ROI roi = ROI::All(),
                int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociodisplay(dst, src, display, view, from, looks,
                                     unpremult, context_key, context_value,
                                     NULL, roi, nthreads);
}


bool
IBA_ociodisplay_colorconfig(ImageBuf& dst, const ImageBuf& src,
                            const std::string& display, const std::string& view,
                            const std::string& from, const std::string& looks,
                            bool unpremult, const std::string& context_key,
                            const std::string& context_value,
                            const std::string& colorconfig = "",
                            ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociodisplay(dst, src, display, view, from, looks,
                                     unpremult, context_key, context_value,
                                     &config, roi, nthreads);
}



ImageBuf
IBA_ociodisplay_ret(const ImageBuf& src, const std::string& display,
                    const std::string& view, const std::string& from,
                    const std::string& looks, bool unpremult,
                    const std::string& context_key,
                    const std::string& context_value, ROI roi = ROI::All(),
                    int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociodisplay(src, display, view, from, looks, unpremult,
                                     context_key, context_value, NULL, roi,
                                     nthreads);
}


ImageBuf
IBA_ociodisplay_colorconfig_ret(
    const ImageBuf& src, const std::string& display, const std::string& view,
    const std::string& from, const std::string& looks, bool unpremult,
    const std::string& context_key, const std::string& context_value,
    const std::string& colorconfig = "", ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociodisplay(src, display, view, from, looks, unpremult,
                                     context_key, context_value, &config, roi,
                                     nthreads);
}



bool
IBA_ociofiletransform(ImageBuf& dst, const ImageBuf& src,
                      const std::string& name, bool unpremult, bool inverse,
                      ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociofiletransform(dst, src, name, unpremult, inverse,
                                           NULL, roi, nthreads);
}


bool
IBA_ociofiletransform_colorconfig(ImageBuf& dst, const ImageBuf& src,
                                  const std::string& name, bool unpremult,
                                  bool inverse,
                                  const std::string& colorconfig = "",
                                  ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociofiletransform(dst, src, name, unpremult, inverse,
                                           &config, roi, nthreads);
}



ImageBuf
IBA_ociofiletransform_ret(const ImageBuf& src, const std::string& name,
                          bool unpremult, bool inverse, ROI roi = ROI::All(),
                          int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociofiletransform(src, name, unpremult, inverse, NULL,
                                           roi, nthreads);
}


ImageBuf
IBA_ociofiletransform_colorconfig_ret(const ImageBuf& src,
                                      const std::string& name, bool unpremult,
                                      bool inverse,
                                      const std::string& colorconfig = "",
                                      ROI roi = ROI::All(), int nthreads = 0)
{
    ColorConfig config(colorconfig);
    py::gil_scoped_release gil;
    return ImageBufAlgo::ociofiletransform(src, name, unpremult, inverse,
                                           &config, roi, nthreads);
}



py::object
IBA_isConstantColor(const ImageBuf& src, float threshold, ROI roi = ROI::All(),
                    int nthreads = 0)
{
    std::vector<float> constcolor(src.nchannels());
    bool r;
    {
        py::gil_scoped_release gil;
        r = ImageBufAlgo::isConstantColor(src, threshold, constcolor, roi,
                                          nthreads);
    }
    if (r) {
        return C_to_tuple(&constcolor[0], (int)constcolor.size());
    } else {
        return py::none();
    }
}



bool
IBA_isConstantChannel(const ImageBuf& src, int channel, float val,
                      float threshold, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::isConstantChannel(src, channel, val, threshold, roi,
                                           nthreads);
}



bool
IBA_isMonochrome(const ImageBuf& src, float threshold, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::isMonochrome(src, threshold, roi, nthreads);
}



ROI
IBA_nonzero_region(const ImageBuf& src, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::nonzero_region(src, roi, nthreads);
}



py::object
IBA_color_range_check(ImageBuf& src, const py::object& low,
                      const py::object& high, ROI roi, int nthreads)
{
    py::gil_scoped_release gil;
    std::vector<int64_t> counts { 0, 0, 0 };
    imagesize_t lowcount = 0, highcount = 0, inrangecount = 0;
    std::vector<float> lowvec, highvec;
    py_to_stdvector(lowvec, low);
    py_to_stdvector(highvec, high);
    bool ok   = ImageBufAlgo::color_range_check(src, &lowcount, &highcount,
                                              &inrangecount, lowvec, highvec,
                                              roi, nthreads);
    counts[0] = lowcount;
    counts[1] = highcount;
    counts[2] = inrangecount;
    py::object result;
    if (ok)
        result = C_to_tuple(counts.data(), 3);
    else
        result = py::none();
    return result;
}



bool
IBA_fixNonFinite(ImageBuf& dst, const ImageBuf& src,
                 ImageBufAlgo::NonFiniteFixMode mode
                 = ImageBufAlgo::NONFINITE_BOX3,
                 ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fixNonFinite(dst, src, mode, NULL, roi, nthreads);
}

ImageBuf
IBA_fixNonFinite_ret(const ImageBuf& src,
                     ImageBufAlgo::NonFiniteFixMode mode
                     = ImageBufAlgo::NONFINITE_BOX3,
                     ROI roi = ROI::All(), int nthreads = 0)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::fixNonFinite(src, mode, NULL, roi, nthreads);
}



bool
IBA_render_point(ImageBuf& dst, int x, int y, py::object color_)
{
    std::vector<float> color;
    py_to_stdvector(color, color_);
    color.resize(dst.nchannels(), 1.0f);
    py::gil_scoped_release gil;
    return ImageBufAlgo::render_point(dst, x, y, color);
}


bool
IBA_render_line(ImageBuf& dst, int x1, int y1, int x2, int y2,
                py::object color_, bool skip_first_point = false)
{
    std::vector<float> color;
    py_to_stdvector(color, color_);
    color.resize(dst.nchannels(), 1.0f);
    py::gil_scoped_release gil;
    return ImageBufAlgo::render_line(dst, x1, y1, x2, y2, color,
                                     skip_first_point);
}


bool
IBA_render_box(ImageBuf& dst, int x1, int y1, int x2, int y2, py::object color_,
               bool fill = false)
{
    std::vector<float> color;
    py_to_stdvector(color, color_);
    color.resize(dst.nchannels(), 1.0f);
    py::gil_scoped_release gil;
    return ImageBufAlgo::render_box(dst, x1, y1, x2, y2, color, fill);
}


bool
IBA_render_text(ImageBuf& dst, int x, int y, const std::string& text,
                int fontsize = 16, const std::string& fontname = "",
                py::object textcolor_ = py::none(),
                const std::string ax  = "left",
                const std::string ay = "baseline", int shadow = 0,
                ROI roi = ROI::All(), int nthreads = 0)
{
    std::vector<float> textcolor;
    py_to_stdvector(textcolor, textcolor_);
    textcolor.resize(dst.nchannels(), 1.0f);
    py::gil_scoped_release gil;
    using ImageBufAlgo::TextAlignX;
    using ImageBufAlgo::TextAlignY;
    TextAlignX alignx(TextAlignX::Left);
    TextAlignY aligny(TextAlignY::Baseline);
    if (Strutil::iequals(ax, "right") || Strutil::iequals(ax, "r"))
        alignx = TextAlignX::Right;
    if (Strutil::iequals(ax, "center") || Strutil::iequals(ax, "c"))
        alignx = TextAlignX::Center;
    if (Strutil::iequals(ay, "top") || Strutil::iequals(ay, "t"))
        aligny = TextAlignY::Top;
    if (Strutil::iequals(ay, "bottom") || Strutil::iequals(ay, "b"))
        aligny = TextAlignY::Bottom;
    if (Strutil::iequals(ay, "center") || Strutil::iequals(ay, "c"))
        aligny = TextAlignY::Center;
    return ImageBufAlgo::render_text(dst, x, y, text, fontsize, fontname,
                                     textcolor, alignx, aligny, shadow, roi,
                                     nthreads);
}


ROI
IBA_text_size(const std::string& text, int fontsize = 16,
              const std::string& fontname = "")
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::text_size(text, fontsize, fontname);
}



py::object
IBA_histogram(const ImageBuf& src, int channel = 0, int bins = 256,
              float min = 0.0f, float max = 1.0f, bool ignore_empty = false,
              ROI roi = {}, int nthreads = 0)
{
    py::gil_scoped_release gil;
    auto hist = ImageBufAlgo::histogram(src, channel, bins, min, max,
                                        ignore_empty, roi, nthreads);
    std::vector<int> h(bins);
    for (int i = 0; i < bins; ++i)
        h[i] = int(hist[i]);
    return C_to_tuple<int>(h);
}



bool
IBA_capture_image(ImageBuf& dst, int cameranum,
                  TypeDesc::BASETYPE convert = TypeDesc::UNKNOWN)
{
    py::gil_scoped_release gil;
    dst = ImageBufAlgo::capture_image(cameranum, convert);
    return !dst.has_error();
}

ImageBuf
IBA_capture_image_ret(int cameranum,
                      TypeDesc::BASETYPE convert = TypeDesc::UNKNOWN)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::capture_image(cameranum, convert);
}



bool
IBA_make_texture_ib(ImageBufAlgo::MakeTextureMode mode, const ImageBuf& buf,
                    const std::string& outputfilename, const ImageSpec& config)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::make_texture(mode, buf, outputfilename, config);
}


bool
IBA_make_texture_filename(ImageBufAlgo::MakeTextureMode mode,
                          const std::string& filename,
                          const std::string& outputfilename,
                          const ImageSpec& config)
{
    py::gil_scoped_release gil;
    return ImageBufAlgo::make_texture(mode, filename, outputfilename, config);
}



void
declare_imagebufalgo(py::module& m)
{
    using namespace pybind11::literals;
    using py::arg;

    py::enum_<ImageBufAlgo::NonFiniteFixMode>(m, "NonFiniteFixMode")
        .value("NONFINITE_NONE", ImageBufAlgo::NONFINITE_NONE)
        .value("NONFINITE_BLACK", ImageBufAlgo::NONFINITE_BLACK)
        .value("NONFINITE_BOX3", ImageBufAlgo::NONFINITE_BOX3)
        .export_values();

    py::enum_<ImageBufAlgo::MakeTextureMode>(m, "MakeTextureMode")
        .value("MakeTxTexture", ImageBufAlgo::MakeTxTexture)
        .value("MakeTxShadow", ImageBufAlgo::MakeTxShadow)
        .value("MakeTxEnvLatl", ImageBufAlgo::MakeTxEnvLatl)
        .value("MakeTxEnvLatlFromLightProbe",
               ImageBufAlgo::MakeTxEnvLatlFromLightProbe)
        .value("MakeTxBumpWithSlopes", ImageBufAlgo::MakeTxBumpWithSlopes)
        .export_values();

    py::class_<ImageBufAlgo::PixelStats>(m, "PixelStats")
        .def(py::init<>())
        .def_readonly("min", &ImageBufAlgo::PixelStats::min)
        .def_readonly("max", &ImageBufAlgo::PixelStats::max)
        .def_readonly("avg", &ImageBufAlgo::PixelStats::avg)
        .def_readonly("stddev", &ImageBufAlgo::PixelStats::stddev)
        .def_readonly("nancount", &ImageBufAlgo::PixelStats::nancount)
        .def_readonly("infcount", &ImageBufAlgo::PixelStats::infcount)
        .def_readonly("finitecount", &ImageBufAlgo::PixelStats::finitecount)
        .def_readonly("sum", &ImageBufAlgo::PixelStats::sum)
        .def_readonly("sum2", &ImageBufAlgo::PixelStats::sum2);

    py::class_<ImageBufAlgo::CompareResults>(m, "CompareResults")
        .def(py::init<>())
        .def_readonly("meanerror", &ImageBufAlgo::CompareResults::meanerror)
        .def_readonly("rms_error", &ImageBufAlgo::CompareResults::rms_error)
        .def_readonly("PSNR", &ImageBufAlgo::CompareResults::PSNR)
        .def_readonly("maxerror", &ImageBufAlgo::CompareResults::maxerror)
        .def_readonly("maxx", &ImageBufAlgo::CompareResults::maxx)
        .def_readonly("maxy", &ImageBufAlgo::CompareResults::maxy)
        .def_readonly("maxz", &ImageBufAlgo::CompareResults::maxz)
        .def_readonly("maxc", &ImageBufAlgo::CompareResults::maxc)
        .def_readonly("nwarn", &ImageBufAlgo::CompareResults::nwarn)
        .def_readonly("nfail", &ImageBufAlgo::CompareResults::nfail)
        .def_readonly("error", &ImageBufAlgo::CompareResults::error);

    // Use a boost::python::scope to put this all inside "ImageBufAlgo"
    py::class_<IBA_dummy>(m, "ImageBufAlgo")
        .def_static("zero", &IBA_zero, "dst"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("zero", &IBA_zero_ret, "roi"_a, "nthreads"_a = 0)

        .def_static("fill", &IBA_fill, "dst"_a, "values"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fill", &IBA_fill2, "dst"_a, "top"_a, "bottom"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fill", &IBA_fill4, "dst"_a, "topleft"_a, "topright"_a,
                    "bottomleft"_a, "bottomright"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("fill", &IBA_fill_ret, "values"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("fill", &IBA_fill2_ret, "top"_a, "bottom"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fill", &IBA_fill4_ret, "topleft"_a, "topright"_a,
                    "bottomleft"_a, "bottomright"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("checker", &IBA_checker, "dst"_a, "width"_a, "height"_a,
                    "depth"_a, "color1"_a, "color2"_a, "xoffset"_a = 0,
                    "yoffset"_a = 0, "zoffset"_a = 0, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("checker", &IBA_checker_ret, "width"_a, "height"_a,
                    "depth"_a, "color1"_a, "color2"_a, "xoffset"_a = 0,
                    "yoffset"_a = 0, "zoffset"_a = 0, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("noise", &IBA_noise, "dst"_a, "type"_a = "gaussian",
                    "A"_a = 0.0f, "B"_a = 0.1f, "mono"_a = false, "seed"_a = 0,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("noise", &IBA_noise_ret, "type"_a = "gaussian",
                    "A"_a = 0.0f, "B"_a = 0.1f, "mono"_a = false, "seed"_a = 0,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("channels", &IBA_channels, "dst"_a, "src"_a,
                    "channelorder"_a, "newchannelnames"_a = py::tuple(),
                    "shuffle_channel_names"_a = false, "nthreads"_a = 0)
        .def_static("channels", &IBA_channels_ret, "src"_a, "channelorder"_a,
                    "newchannelnames"_a       = py::tuple(),
                    "shuffle_channel_names"_a = false, "nthreads"_a = 0)

        .def_static("channel_append", IBA_channel_append, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("channel_append", IBA_channel_append_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("deepen", IBA_deepen, "dst"_a, "src"_a, "zvalue"_a = 1.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("deepen", IBA_deepen_ret, "src"_a, "zvalue"_a = 1.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("flatten", IBA_flatten, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("flatten", IBA_flatten_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("deep_merge", IBA_deep_merge, "dst"_a, "A"_a, "B"_a,
                    "occlusion_cull"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("deep_merge", IBA_deep_merge_ret, "A"_a, "B"_a,
                    "occlusion_cull"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("deep_holdout", IBA_deep_holdout, "dst"_a, "src"_a,
                    "holdout"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("deep_holdout", IBA_deep_holdout_ret, "src"_a, "holdout"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("copy", IBA_copy, "dst"_a, "src"_a,
                    "convert"_a = TypeUnknown, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("copy", IBA_copy_ret, "src"_a, "convert"_a = TypeUnknown,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("crop", IBA_crop, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("crop", IBA_crop_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("cut", IBA_cut, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("cut", IBA_cut_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("paste", IBA_paste, "dst"_a, "xbegin"_a, "ybegin"_a,
                    "zbegin"_a, "chbegin"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("rotate90", IBA_rotate90, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rotate90", IBA_rotate90_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("rotate180", IBA_rotate180, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rotate180", IBA_rotate180_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("rotate270", IBA_rotate270, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rotate270", IBA_rotate270_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("flip", IBA_flip, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("flip", IBA_flip_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("flop", IBA_flop, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("flop", IBA_flop_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("reorient", IBA_reorient, "dst"_a, "src"_a,
                    "nthreads"_a = 0)
        .def_static("reorient", IBA_reorient_ret, "src"_a, "nthreads"_a = 0)

        .def_static("transpose", IBA_transpose, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("transpose", IBA_transpose_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("circular_shift", &IBA_circular_shift, "dst"_a, "src"_a,
                    "xshift"_a, "yshift"_a, "zshift"_a = 0,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("circular_shift", &IBA_circular_shift_ret, "src"_a,
                    "xshift"_a, "yshift"_a, "zshift"_a = 0,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("add", &IBA_add_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("add", IBA_add_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("add", &IBA_add_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("add", IBA_add_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("sub", &IBA_sub_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("sub", IBA_sub_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("sub", &IBA_sub_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("sub", IBA_sub_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("absdiff", &IBA_absdiff_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("absdiff", IBA_absdiff_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("absdiff", &IBA_absdiff_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("absdiff", IBA_absdiff_color_ref, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("abs", &IBA_abs, "dst"_a, "A"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("abs", &IBA_abs_ret, "A"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("mul", &IBA_mul_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mul", &IBA_mul_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mul", &IBA_mul_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mul", &IBA_mul_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("div", &IBA_div_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("div", &IBA_div_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("div", &IBA_div_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("div", &IBA_div_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("mad", &IBA_mad_images, "dst"_a, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_ici, "dst"_a, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_cii, "dst"_a, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_color, "dst"_a, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_images_ret, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_ici_ret, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_cii_ret, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("mad", &IBA_mad_color_ret, "A"_a, "B"_a, "C"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("invert", &IBA_invert, "dst"_a, "A"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("invert", &IBA_invert_ret, "A"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("pow", &IBA_pow_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("pow", &IBA_pow_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("channel_sum", &IBA_channel_sum, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("channel_sum", &IBA_channel_sum_weight, "dst"_a, "src"_a,
                    "weight"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("channel_sum", &IBA_channel_sum_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("channel_sum", &IBA_channel_sum_weight_ret, "src"_a,
                    "weight"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("color_map", &IBA_color_map_name, "dst"_a, "src"_a,
                    "srcchannel"_a, "mapname"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("color_map", &IBA_color_map_values, "dst"_a, "src"_a,
                    "srcchannel"_a, "nknots"_a, "channels"_a, "knots"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("color_map", &IBA_color_map_name_ret, "src"_a,
                    "srcchannel"_a, "mapname"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("color_map", &IBA_color_map_values_ret, "src"_a,
                    "srcchannel"_a, "nknots"_a, "channels"_a, "knots"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("rangecompress", &IBA_rangecompress, "dst"_a, "src"_a,
                    "useluma"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rangecompress", &IBA_rangecompress_ret, "src"_a,
                    "useluma"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("rangeexpand", &IBA_rangeexpand, "dst"_a, "src"_a,
                    "useluma"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rangeexpand", &IBA_rangeexpand_ret, "src"_a,
                    "useluma"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("premult", &IBA_premult, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("premult", &IBA_premult_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("unpremult", &IBA_unpremult, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("unpremult", &IBA_unpremult_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("repremult", &IBA_repremult, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("repremult", &IBA_repremult_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("min", &IBA_min_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("min", IBA_min_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("min", &IBA_min_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("min", IBA_min_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("max", &IBA_max_images, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("max", IBA_max_color, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("max", &IBA_max_images_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("max", IBA_max_color_ret, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("clamp", &IBA_clamp, "dst"_a, "src"_a, "min"_a, "max"_a,
                    "clampalpha01"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("clamp", &IBA_clamp_ret, "src"_a, "min"_a, "max"_a,
                    "clampalpha01"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("contrast_remap", &IBA_contrast_remap, "dst"_a, "src"_a,
                    "black"_a = 0.0f, "white"_a = 1.0f, "min"_a = 0.0f,
                    "max"_a = 1.0f, "scontrast"_a = 1.0f, "sthresh"_a = 0.5f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("contrast_remap", &IBA_contrast_remap_ret, "src"_a,
                    "black"_a = 0.0f, "white"_a = 1.0f, "min"_a = 0.0f,
                    "max"_a = 1.0f, "scontrast"_a = 1.0f, "sthresh"_a = 0.5f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("colorconvert", &IBA_colorconvert, "dst"_a, "src"_a,
                    "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("colorconvert", &IBA_colorconvert_colorconfig, "dst"_a,
                    "src"_a, "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "context_key"_a = "", "context_value"_a = "",
                    "colorconfig"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("colorconvert", &IBA_colorconvert_ret, "src"_a,
                    "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("colorconvert", &IBA_colorconvert_colorconfig_ret, "src"_a,
                    "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "context_key"_a = "", "context_value"_a = "",
                    "colorconfig"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("colormatrixtransform", &IBA_colormatrixtransform, "dst"_a,
                    "src"_a, "M"_a, "unpremult"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("colormatrixtransform", &IBA_colormatrixtransform_ret,
                    "src"_a, "M"_a, "unpremult"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("ociolook", &IBA_ociolook, "dst"_a, "src"_a, "looks"_a,
                    "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "inverse"_a = false, "context_key"_a = "",
                    "context_value"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("ociolook", &IBA_ociolook_colorconfig, "dst"_a, "src"_a,
                    "looks"_a, "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "inverse"_a = false, "context_key"_a = "",
                    "context_value"_a = "", "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("ociolook", &IBA_ociolook_ret, "src"_a, "looks"_a,
                    "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "inverse"_a = false, "context_key"_a = "",
                    "context_value"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("ociolook", &IBA_ociolook_colorconfig_ret, "src"_a,
                    "looks"_a, "fromspace"_a, "tospace"_a, "unpremult"_a = true,
                    "inverse"_a = false, "context_key"_a = "",
                    "context_value"_a = "", "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("ociodisplay", &IBA_ociodisplay, "dst"_a, "src"_a,
                    "display"_a, "view"_a, "fromspace"_a = "", "looks"_a = "",
                    "unpremult"_a = true, "context_key"_a = "",
                    "context_value"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("ociodisplay", &IBA_ociodisplay_colorconfig, "dst"_a,
                    "src"_a, "display"_a, "view"_a, "fromspace"_a = "",
                    "looks"_a = "", "unpremult"_a = true, "context_key"_a = "",
                    "context_value"_a = "", "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("ociodisplay", &IBA_ociodisplay_ret, "src"_a, "display"_a,
                    "view"_a, "fromspace"_a = "", "looks"_a = "",
                    "unpremult"_a = true, "context_key"_a = "",
                    "context_value"_a = "", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("ociodisplay", &IBA_ociodisplay_colorconfig_ret, "src"_a,
                    "display"_a, "view"_a, "fromspace"_a = "", "looks"_a = "",
                    "unpremult"_a = true, "context_key"_a = "",
                    "context_value"_a = "", "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("ociofiletransform", &IBA_ociofiletransform, "dst"_a,
                    "src"_a, "name"_a, "unpremult"_a = true,
                    "inverse"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("ociofiletransform", &IBA_ociofiletransform_colorconfig,
                    "dst"_a, "src"_a, "name"_a, "unpremult"_a = true,
                    "inverse"_a = false, "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("ociofiletransform", &IBA_ociofiletransform_ret, "src"_a,
                    "name"_a, "unpremult"_a = true, "inverse"_a = false,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("ociofiletransform", &IBA_ociofiletransform_colorconfig_ret,
                    "src"_a, "name"_a, "unpremult"_a = true,
                    "inverse"_a = false, "colorconfig"_a = "",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("computePixelStats", &IBA_computePixelStats, "src"_a,
                    "stats"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("computePixelStats", &IBA_computePixelStats_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("compare", &IBA_compare, "A"_a, "B"_a, "failthresh"_a,
                    "warnthresh"_a, "result"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("compare", &IBA_compare_ret, "A"_a, "B"_a, "failthresh"_a,
                    "warnthresh"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("compare_Yee", &IBA_compare_Yee, "A"_a, "B"_a, "result"_a,
                    "luminance"_a = 100, "fov"_a = 45, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("isConstantColor", &IBA_isConstantColor, "src"_a,
                    "threshold"_a = 0.0f, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("isConstantChannel", &IBA_isConstantChannel, "src"_a,
                    "channel"_a, "val"_a, "threshold"_a = 0.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("isMonochrome", &IBA_isMonochrome, "src"_a,
                    "threshold"_a = 0.0f, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        // color_count

        .def_static("color_range_check", &IBA_color_range_check, "src"_a,
                    "low"_a, "high"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("nonzero_region", &IBA_nonzero_region, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("computePixelHashSHA1", &IBA_computePixelHashSHA1, "src"_a,
                    "extrainfo"_a = "", "roi"_a = ROI::All(), "blocksize"_a = 0,
                    "nthreads"_a = 0)

        .def_static("warp", &IBA_warp, "dst"_a, "src"_a, "M"_a,
                    "filtername"_a = "", "filterwidth"_a = 0.0f,
                    "recompute_roi"_a = false, "wrap"_a = "default",
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("warp", &IBA_warp_ret, "src"_a, "M"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "recompute_roi"_a = false,
                    "wrap"_a = "default", "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("rotate", &IBA_rotate, "dst"_a, "src"_a, "angle"_a,
                    "filtername"_a = "", "filterwidth"_a = 0.0f,
                    "recompute_roi"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("rotate", &IBA_rotate2, "dst"_a, "src"_a, "angle"_a,
                    "center_x"_a, "center_y"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "recompute_roi"_a = false,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("rotate", &IBA_rotate_ret, "src"_a, "angle"_a,
                    "filtername"_a = "", "filterwidth"_a = 0.0f,
                    "recompute_roi"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("rotate", &IBA_rotate2_ret, "src"_a, "angle"_a,
                    "center_x"_a, "center_y"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "recompute_roi"_a = false,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("resize", &IBA_resize, "dst"_a, "src"_a,
                    "filtername"_a = "", "filterwidth"_a = 0.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("resize", &IBA_resize_ret, "src"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("resample", &IBA_resample, "dst"_a, "src"_a,
                    "interpolate"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("resample", &IBA_resample_ret, "src"_a,
                    "interpolate"_a = true, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("fit", &IBA_fit, "dst"_a, "src"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "fillmode"_a = "letterbox",
                    "exact"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fit", &IBA_fit_ret, "src"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "fillmode"_a = "letterbox",
                    "exact"_a = false, "roi"_a = ROI::All(), "nthreads"_a = 0)
        // DEPRECATED(2.3) versions
        .def_static("fit", &IBA_fit_old, "dst"_a, "src"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "exact"_a = false,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fit", &IBA_fit_ret_old, "src"_a, "filtername"_a = "",
                    "filterwidth"_a = 0.0f, "exact"_a = false,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("make_kernel", &IBA_make_kernel, "dst"_a, "name"_a,
                    "width"_a, "height"_a, "depth"_a = 1.0f,
                    "normalize"_a = true)
        .def_static("make_kernel", &IBA_make_kernel_ret, "name"_a, "width"_a,
                    "height"_a, "depth"_a = 1.0f, "normalize"_a = true)

        .def_static("convolve", &IBA_convolve, "dst"_a, "src"_a, "kernel"_a,
                    "normalze"_a = true, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("convolve", &IBA_convolve_ret, "src"_a, "kernel"_a,
                    "normalze"_a = true, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("unsharp_mask", &IBA_unsharp_mask, "dst"_a, "src"_a,
                    "kernel"_a = "gaussian", "width"_a = 3.0f,
                    "contrast"_a = 1.0f, "threshold"_a = 0.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("unsharp_mask", &IBA_unsharp_mask_ret, "src"_a,
                    "kernel"_a = "gaussian", "width"_a = 3.0f,
                    "contrast"_a = 1.0f, "threshold"_a = 0.0f,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("median_filter", &IBA_median_filter, "dst"_a, "src"_a,
                    "width"_a = 3, "height"_a = -1, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("median_filter", &IBA_median_filter_ret, "src"_a,
                    "width"_a = 3, "height"_a = -1, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("dilate", &IBA_dilate, "dst"_a, "src"_a, "width"_a = 3,
                    "height"_a = -1, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("dilate", &IBA_dilate_ret, "src"_a, "width"_a = 3,
                    "height"_a = -1, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("erode", &IBA_erode, "dst"_a, "src"_a, "width"_a = 3,
                    "height"_a = -1, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("erode", &IBA_erode_ret, "src"_a, "width"_a = 3,
                    "height"_a = -1, "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("laplacian", &IBA_laplacian, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("laplacian", &IBA_laplacian_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("fft", &IBA_fft, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("fft", &IBA_fft_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("ifft", &IBA_ifft, "dst"_a, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("ifft", &IBA_ifft_ret, "src"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("polar_to_complex", &IBA_polar_to_complex, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("polar_to_complex", &IBA_polar_to_complex_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("complex_to_polar", &IBA_complex_to_polar, "dst"_a, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("complex_to_polar", &IBA_complex_to_polar_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("fixNonFinite", &IBA_fixNonFinite, "dst"_a, "src"_a,
                    "mode"_a = ImageBufAlgo::NONFINITE_BOX3,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fixNonFinite", &IBA_fixNonFinite_ret, "src"_a,
                    "mode"_a = ImageBufAlgo::NONFINITE_BOX3,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("fillholes_pushpull", &IBA_fillholes_pushpull, "dst"_a,
                    "src"_a, "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("fillholes_pushpull", &IBA_fillholes_pushpull_ret, "src"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("capture_image", &IBA_capture_image, "dst"_a,
                    "cameranum"_a = 0, "convert"_a = TypeDesc::UNKNOWN)
        .def_static("capture_image", &IBA_capture_image_ret, "cameranum"_a = 0,
                    "convert"_a = TypeDesc::UNKNOWN)

        .def_static("over", &IBA_over, "dst"_a, "A"_a, "B"_a,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)
        .def_static("over", &IBA_over_ret, "A"_a, "B"_a, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("zover", &IBA_zover, "dst"_a, "A"_a, "B"_a,
                    "z_zeroisinf"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)
        .def_static("zover", &IBA_zover_ret, "A"_a, "B"_a,
                    "z_zeroisinf"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("render_point", &IBA_render_point, "dst"_a, "x"_a, "y"_a,
                    "color"_a = py::none())

        .def_static("render_line", &IBA_render_line, "dst"_a, "x1"_a, "y1"_a,
                    "x2"_a, "y2"_a, "color"_a = py::none(),
                    "skip_first_point"_a = false)

        .def_static("render_box", &IBA_render_box, "dst"_a, "x1"_a, "y1"_a,
                    "x2"_a, "y2"_a, "color"_a = py::none(), "fill"_a = false)

        .def_static("render_text", &IBA_render_text, "dst"_a, "x"_a, "y"_a,
                    "text"_a, "fontsize"_a = 16, "fontname"_a = "",
                    "textcolor"_a = py::tuple(), "alignx"_a = "left",
                    "aligny"_a = "baseline", "shadow"_a = 0,
                    "roi"_a = ROI::All(), "nthreads"_a = 0)

        .def_static("text_size", &IBA_text_size, "text"_a, "fontsize"_a = 16,
                    "fontname"_a = "")

        .def_static("histogram", &IBA_histogram, "src"_a, "channel"_a = 0,
                    "bins"_a = 256, "min"_a = 0.0f, "max"_a = 1.0f,
                    "ignore_empty"_a = false, "roi"_a = ROI::All(),
                    "nthreads"_a = 0)

        .def_static("make_texture", &IBA_make_texture_filename, "mode"_a,
                    "filename"_a, "outputfilename"_a, "config"_a = ImageSpec())
        .def_static("make_texture", &IBA_make_texture_ib, "mode"_a, "buf"_a,
                    "outputfilename"_a, "config"_a = ImageSpec());
}

}  // namespace PyOpenImageIO
