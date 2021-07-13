// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

/// \file
/// Implementation of ImageBufAlgo algorithms that analyze or compare
/// images.

#include <cmath>
#include <iostream>
#include <limits>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/hash.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/thread.h>

#include "imageio_pvt.h"

OIIO_NAMESPACE_BEGIN


void
ImageBufAlgo::PixelStats::reset(int nchannels)
{
    // clang-format off
    const float inf = std::numeric_limits<float>::infinity();
    min.clear ();          min.resize (nchannels, inf);
    max.clear ();          max.resize (nchannels, -inf);
    avg.clear ();          avg.resize (nchannels);
    stddev.clear ();       stddev.resize (nchannels);
    nancount.clear ();     nancount.resize (nchannels, 0);
    infcount.clear ();     infcount.resize (nchannels, 0);
    finitecount.clear ();  finitecount.resize (nchannels, 0);
    sum.clear ();          sum.resize (nchannels, 0.0);
    sum2.clear ();         sum2.resize (nchannels, 0.0);
    // clang-format on
}



void
ImageBufAlgo::PixelStats::merge(const ImageBufAlgo::PixelStats& p)
{
    OIIO_DASSERT(min.size() == p.min.size());
    for (size_t c = 0, e = min.size(); c < e; ++c) {
        min[c] = std::min(min[c], p.min[c]);
        max[c] = std::max(max[c], p.max[c]);
        nancount[c] += p.nancount[c];
        infcount[c] += p.infcount[c];
        finitecount[c] += p.finitecount[c];
        sum[c] += p.sum[c];
        sum2[c] += p.sum2[c];
    }
}



const ImageBufAlgo::PixelStats&
ImageBufAlgo::PixelStats::operator=(PixelStats&& other)
{
    min         = std::move(other.min);
    max         = std::move(other.max);
    avg         = std::move(other.avg);
    stddev      = std::move(other.stddev);
    nancount    = std::move(other.nancount);
    infcount    = std::move(other.infcount);
    finitecount = std::move(other.finitecount);
    sum         = std::move(other.sum);
    sum2        = std::move(other.sum2);
    return *this;
}



inline void
val(ImageBufAlgo::PixelStats& p, int c, float value)
{
    if (isnan(value)) {
        ++p.nancount[c];
        return;
    }
    if (isinf(value)) {
        ++p.infcount[c];
        return;
    }
    ++p.finitecount[c];
    p.sum[c] += value;
    p.sum2[c] += value * value;
    p.min[c] = std::min(value, p.min[c]);
    p.max[c] = std::max(value, p.max[c]);
}



inline void
finalize(ImageBufAlgo::PixelStats& p)
{
    for (size_t c = 0, e = p.min.size(); c < e; ++c) {
        if (p.finitecount[c] == 0) {
            p.min[c]    = 0.0;
            p.max[c]    = 0.0;
            p.avg[c]    = 0.0;
            p.stddev[c] = 0.0;
        } else {
            double Count = static_cast<double>(p.finitecount[c]);
            double davg  = p.sum[c] / Count;
            p.avg[c]     = static_cast<float>(davg);
            p.stddev[c]  = static_cast<float>(
                safe_sqrt(p.sum2[c] / Count - davg * davg));
        }
    }
}



template<class T>
static bool
computePixelStats_(const ImageBuf& src, ImageBufAlgo::PixelStats& stats,
                   ROI roi, int nthreads)
{
    // clang-format off
    if (!roi.defined())
        roi = get_roi(src.spec());
    else
        roi.chend = std::min(roi.chend, src.nchannels());

    int nchannels = src.spec().nchannels;

    stats.reset(nchannels);
    OIIO::spin_mutex mutex;  // protect the shared stats when merging

    parallel_options opt(nthreads);
    if (src.deep()) {
        parallel_for_chunked(roi.ybegin, roi.yend, 64,
                             [&](int /*id*/, int64_t ybegin, int64_t yend) {
            ROI subroi(roi.xbegin, roi.xend, ybegin, yend, roi.zbegin,
                       roi.zend, roi.chbegin, roi.chend);
            ImageBufAlgo::PixelStats tmp(nchannels);
            for (ImageBuf::ConstIterator<T> s(src, subroi); !s.done();
                 ++s) {
                int samples = s.deep_samples();
                if (!samples)
                    continue;
                for (int c = subroi.chbegin; c < subroi.chend; ++c) {
                    for (int i = 0; i < samples; ++i) {
                        float value = s.deep_value(c, i);
                        val(tmp, c, value);
                    }
                }
            }
            std::lock_guard<OIIO::spin_mutex> lock(mutex);
            stats.merge(tmp);
        }, opt);

    } else {  // Non-deep case
        parallel_for_chunked(roi.ybegin, roi.yend, 64,
                             [&](int /*id*/, int64_t ybegin, int64_t yend) {
            ROI subroi(roi.xbegin, roi.xend, ybegin, yend, roi.zbegin,
                       roi.zend, roi.chbegin, roi.chend);
            ImageBufAlgo::PixelStats tmp(nchannels);
            for (ImageBuf::ConstIterator<T> s(src, subroi); !s.done();
                 ++s) {
                for (int c = subroi.chbegin; c < subroi.chend; ++c) {
                    float value = s[c];
                    val(tmp, c, value);
                }
            }
            std::lock_guard<OIIO::spin_mutex> lock(mutex);
            stats.merge(tmp);
        }, opt);
    }

    // Compute final results
    finalize(stats);

    return !src.has_error();
    // clang-format on
};



ImageBufAlgo::PixelStats
ImageBufAlgo::computePixelStats(const ImageBuf& src, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::computePixelStats");
    ImageBufAlgo::PixelStats stats;
    if (!roi.defined())
        roi = get_roi(src.spec());
    else
        roi.chend = std::min(roi.chend, src.nchannels());
    int nchannels = src.spec().nchannels;
    if (nchannels == 0) {
        src.errorfmt("{}-channel images not supported", nchannels);
        return stats;
    }

    bool ok = true;
    OIIO_DISPATCH_TYPES(ok, "computePixelStats", computePixelStats_,
                        src.spec().format, src, stats, roi, nthreads);
    if (!ok)
        stats.reset(0);
    return stats;
}



// Deprecated:
bool
ImageBufAlgo::computePixelStats(PixelStats& stats, const ImageBuf& src, ROI roi,
                                int nthreads)
{
    stats = computePixelStats(src, roi, nthreads);
    return stats.min.size() == (size_t)src.nchannels();
}



template<class BUFT>
inline void
compare_value(ImageBuf::ConstIterator<BUFT, float>& a, int chan, float aval,
              float bval, ImageBufAlgo::CompareResults& result, float& maxval,
              double& batcherror, double& batch_sqrerror, bool& failed,
              bool& warned, float failthresh, float warnthresh)
{
    if (!isfinite(aval) || !isfinite(bval)) {
        if (isnan(aval) == isnan(bval) && isinf(aval) == isinf(bval))
            return;  // NaN may match NaN, Inf may match Inf
        if (isfinite(result.maxerror)) {
            // non-finite errors trump finite ones
            result.maxerror = std::numeric_limits<float>::infinity();
            result.maxx     = a.x();
            result.maxy     = a.y();
            result.maxz     = a.z();
            result.maxc     = chan;
            return;
        }
    }
    maxval   = std::max(maxval, std::max(aval, bval));
    double f = fabs(aval - bval);
    batcherror += f;
    batch_sqrerror += f * f;
    // We use the awkward '!(a<=threshold)' construct so that we have
    // failures when f is a NaN (since all comparisons involving NaN will
    // return false).
    if (!(f <= result.maxerror)) {
        result.maxerror = f;
        result.maxx     = a.x();
        result.maxy     = a.y();
        result.maxz     = a.z();
        result.maxc     = chan;
    }
    if (!warned && !(f <= warnthresh)) {
        ++result.nwarn;
        warned = true;
    }
    if (!failed && !(f <= failthresh)) {
        ++result.nfail;
        failed = true;
    }
}



template<class Atype, class Btype>
static bool
compare_(const ImageBuf& A, const ImageBuf& B, float failthresh,
         float warnthresh, ImageBufAlgo::CompareResults& result, ROI roi,
         int /*nthreads*/)
{
    imagesize_t npels = roi.npixels();
    imagesize_t nvals = npels * roi.nchannels();
    int Achannels = A.nchannels(), Bchannels = B.nchannels();

    // Compare the two images.
    //
    double totalerror    = 0;
    double totalsqrerror = 0;
    result.maxerror      = 0;
    result.maxx = 0, result.maxy = 0, result.maxz = 0, result.maxc = 0;
    result.nfail = 0, result.nwarn = 0;

    float maxval = 1.0;
    // N.B. [PSNR](https://en.wikipedia.org/wiki/Peak_signal-to-noise_ratio)
    // formula requires the max possible value. We assume a normalized 1.0,
    // but for an HDR image with potentially values > 1.0, there is no true
    // max value, so we punt and use the highest value found in either
    // image. The compare_value() function we call on every pixel value will
    // check and adjust our max as needed.

    ImageBuf::ConstIterator<Atype> a(A, roi, ImageBuf::WrapBlack);
    ImageBuf::ConstIterator<Btype> b(B, roi, ImageBuf::WrapBlack);
    bool deep = A.deep();
    // Break up into batches to reduce cancelation errors as the error
    // sums become too much larger than the error for individual pixels.
    const int batchsize = 4096;  // As good a guess as any
    for (; !a.done();) {
        double batcherror     = 0;
        double batch_sqrerror = 0;
        if (deep) {
            for (int i = 0; i < batchsize && !a.done(); ++i, ++a, ++b) {
                bool warned = false, failed = false;  // For this pixel
                auto nsamps = std::max(a.deep_samples(), b.deep_samples());
                for (int c = roi.chbegin; c < roi.chend; ++c)
                    for (int s = 0, e = nsamps; s < e; ++s) {
                        compare_value(a, c, a.deep_value(c, s),
                                      b.deep_value(c, s), result, maxval,
                                      batcherror, batch_sqrerror, failed,
                                      warned, failthresh, warnthresh);
                    }
            }
        } else {  // non-deep
            for (int i = 0; i < batchsize && !a.done(); ++i, ++a, ++b) {
                bool warned = false, failed = false;  // For this pixel
                for (int c = roi.chbegin; c < roi.chend; ++c)
                    compare_value(a, c, c < Achannels ? a[c] : 0.0f,
                                  c < Bchannels ? b[c] : 0.0f, result, maxval,
                                  batcherror, batch_sqrerror, failed, warned,
                                  failthresh, warnthresh);
            }
        }
        totalerror += batcherror;
        totalsqrerror += batch_sqrerror;
    }
    result.meanerror = totalerror / nvals;
    result.rms_error = sqrt(totalsqrerror / nvals);
    result.PSNR      = 20.0 * log10(maxval / result.rms_error);
    return result.nfail == 0;
}



ImageBufAlgo::CompareResults
ImageBufAlgo::compare(const ImageBuf& A, const ImageBuf& B, float failthresh,
                      float warnthresh, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::compare");
    ImageBufAlgo::CompareResults result;
    result.error = true;

    // If no ROI is defined, use the union of the data windows of the two
    // images.
    if (!roi.defined())
        roi = roi_union(get_roi(A.spec()), get_roi(B.spec()));
    roi.chend = std::min(roi.chend, std::max(A.nchannels(), B.nchannels()));

    // Deep and non-deep images cannot be compared
    if (B.deep() != A.deep()) {
        A.errorfmt("deep and non-deep images cannot be compared");
        return result;
    }

    bool ok;
    OIIO_DISPATCH_COMMON_TYPES2_CONST(ok, "compare", compare_, A.spec().format,
                                      B.spec().format, A, B, failthresh,
                                      warnthresh, result, roi, nthreads);
    // FIXME - The nthreads argument is for symmetry with the rest of
    // ImageBufAlgo and for future expansion. But for right now, we
    // don't actually split by threads.  Maybe later.
    result.error = !ok;
    return result;
}



bool
ImageBufAlgo::compare(const ImageBuf& A, const ImageBuf& B, float failthresh,
                      float warnthresh, ImageBufAlgo::CompareResults& result,
                      ROI roi, int nthreads)
{
    result = compare(A, B, failthresh, warnthresh, roi, nthreads);
    return result.error == false;
}



template<typename T>
static bool
isConstantColor_(const ImageBuf& src, float threshold, span<float> color,
                 ROI roi, int nthreads)
{
    atomic_int result(true);
    if (threshold == 0.0f) {
        // For 0.0 threshold, use shortcut of avoiding the conversion
        // to float, just compare original type values.
        std::vector<T> constval(roi.nchannels());
        ImageBuf::ConstIterator<T, T> s(src, roi);
        for (int c = roi.chbegin; c < roi.chend; ++c)
            constval[c] = s[c];
        ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
            if (!result)
                return;  // another parallel bucket already failed, don't bother
            for (ImageBuf::ConstIterator<T, T> s(src, roi); !s.done(); ++s) {
                for (int c = roi.chbegin; c < roi.chend; ++c)
                    if (s[c] != constval[c]) {
                        result = false;
                        return;
                    }
            }
        });
    } else {
        // Nonzero threshold case
        std::vector<T> constval(roi.nchannels());
        ImageBuf::ConstIterator<T> s(src, roi);
        for (int c = roi.chbegin; c < roi.chend; ++c)
            constval[c] = s[c];
        ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
            if (!result)
                return;  // another parallel bucket already failed, don't bother
            for (ImageBuf::ConstIterator<T> s(src, roi); !s.done(); ++s) {
                for (int c = roi.chbegin; c < roi.chend; ++c)
                    if (std::abs(s[c] - constval[c]) > threshold) {
                        result = false;
                        return;
                    }
            }
        });
    }

    if (color.size()) {
        ImageBuf::ConstIterator<T, float> s(src, roi);
        for (int c = 0; c < roi.chbegin && c < color.size(); ++c)
            color[c] = 0.0f;
        for (int c = roi.chbegin; c < roi.chend && c < color.size(); ++c)
            color[c] = s[c];
        for (int c = roi.chend; c < src.nchannels() && c < color.size(); ++c)
            color[c] = 0.0f;
    }
    return result ? true : false;
}



bool
ImageBufAlgo::isConstantColor(const ImageBuf& src, float threshold,
                              span<float> color, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::isConstantColor");
    // If no ROI is defined, use the data window of src.
    if (!roi.defined())
        roi = get_roi(src.spec());
    roi.chend = std::min(roi.chend, src.nchannels());

    if (roi.nchannels() == 0)
        return true;

    bool ok;
    OIIO_DISPATCH_TYPES(ok, "isConstantColor", isConstantColor_,
                        src.spec().format, src, threshold, color, roi,
                        nthreads);
    return ok;
};



template<typename T>
static bool
isConstantChannel_(const ImageBuf& src, int channel, float val, float threshold,
                   ROI roi, int nthreads)
{
    atomic_int result(true);
    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        if (!result)
            return;  // another parallel bucket already failed, don't bother
        if (threshold == 0.0f) {
            // For 0.0 threshold, use shortcut of avoiding the conversion
            // to float, just compare original type values.
            T constvalue = convert_type<float, T>(val);
            for (ImageBuf::ConstIterator<T, T> s(src, roi); !s.done(); ++s) {
                if (s[channel] != constvalue) {
                    result = false;
                    return;
                }
            }
        } else {
            // Nonzero threshold case
            for (ImageBuf::ConstIterator<T> s(src, roi); !s.done(); ++s) {
                float constvalue = val;
                if (std::abs(s[channel] - constvalue) > threshold) {
                    result = false;
                    return;
                }
            }
        }
    });
    return result ? true : false;
}


bool
ImageBufAlgo::isConstantChannel(const ImageBuf& src, int channel, float val,
                                float threshold, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::isConstantChannel");
    // If no ROI is defined, use the data window of src.
    if (!roi.defined())
        roi = get_roi(src.spec());

    if (channel < 0 || channel >= src.nchannels())
        return false;  // that channel doesn't exist in the image

    bool ok;
    OIIO_DISPATCH_TYPES(ok, "isConstantChannel", isConstantChannel_,
                        src.spec().format, src, channel, val, threshold, roi,
                        nthreads);
    return ok;
};



template<typename T>
static bool
isMonochrome_(const ImageBuf& src, float threshold, ROI roi, int nthreads)
{
    int nchannels = src.nchannels();
    if (nchannels < 2)
        return true;

    atomic_int result(true);
    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        if (!result)
            return;  // another parallel bucket already failed, don't bother
        if (threshold == 0.0f) {
            // For 0.0 threshold, use shortcut of avoiding the conversion
            // to float, just compare original type values.
            for (ImageBuf::ConstIterator<T, T> s(src, roi); !s.done(); ++s) {
                T constvalue = s[roi.chbegin];
                for (int c = roi.chbegin + 1; c < roi.chend; ++c)
                    if (s[c] != constvalue) {
                        result = false;
                        return;
                    }
            }
        } else {
            // Nonzero threshold case
            for (ImageBuf::ConstIterator<T> s(src, roi); !s.done(); ++s) {
                float constvalue = s[roi.chbegin];
                for (int c = roi.chbegin + 1; c < roi.chend; ++c)
                    if (std::abs(s[c] - constvalue) > threshold) {
                        result = false;
                        return;
                    }
            }
        }
    });
    return result ? true : false;
}



bool
ImageBufAlgo::isMonochrome(const ImageBuf& src, float threshold, ROI roi,
                           int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::isMonochrome");
    // If no ROI is defined, use the data window of src.
    if (!roi.defined())
        roi = get_roi(src.spec());
    roi.chend = std::min(roi.chend, src.nchannels());
    if (roi.nchannels() < 2)
        return true;  // 1 or fewer channels are always "monochrome"

    bool ok;
    OIIO_DISPATCH_TYPES(ok, "isMonochrome", isMonochrome_, src.spec().format,
                        src, threshold, roi, nthreads);
    return ok;
};



template<typename T>
static bool
color_count_(const ImageBuf& src, atomic_ll* count, int ncolors,
             const float* color, const float* eps, ROI roi, int nthreads)
{
    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        int nchannels = src.nchannels();
        long long* n  = OIIO_ALLOCA(long long, ncolors);
        for (int col = 0; col < ncolors; ++col)
            n[col] = 0;
        for (ImageBuf::ConstIterator<T> p(src, roi); !p.done(); ++p) {
            int coloffset = 0;
            for (int col = 0; col < ncolors; ++col, coloffset += nchannels) {
                int match = 1;
                for (int c = roi.chbegin; c < roi.chend; ++c) {
                    if (fabsf(p[c] - color[coloffset + c]) > eps[c]) {
                        match = 0;
                        break;
                    }
                }
                n[col] += match;
            }
        }
        for (int col = 0; col < ncolors; ++col)
            count[col] += n[col];
    });
    return true;
}



bool
ImageBufAlgo::color_count(const ImageBuf& src, imagesize_t* count, int ncolors,
                          cspan<float> color, cspan<float> eps, ROI roi,
                          int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::color_count");
    // If no ROI is defined, use the data window of src.
    if (!roi.defined())
        roi = get_roi(src.spec());
    roi.chend = std::min(roi.chend, src.nchannels());

    if (color.size() < ncolors * src.nchannels()) {
        src.errorfmt(
            "ImageBufAlgo::color_count: not enough room in 'color' array");
        return false;
    }
    IBA_FIX_PERCHAN_LEN(eps, src.nchannels(), eps.size() ? eps.back() : 0.001f,
                        0.001f);

    for (int col = 0; col < ncolors; ++col)
        count[col] = 0;
    bool ok;
    OIIO_DISPATCH_TYPES(ok, "color_count", color_count_, src.spec().format, src,
                        (atomic_ll*)count, ncolors, color.data(), eps.data(),
                        roi, nthreads);
    return ok;
}



template<typename T>
static bool
color_range_check_(const ImageBuf& src, atomic_ll* lowcount,
                   atomic_ll* highcount, atomic_ll* inrangecount,
                   const float* low, const float* high, ROI roi, int nthreads)
{
    ImageBufAlgo::parallel_image(roi, nthreads, [=, &src](ROI roi) {
        long long lc = 0, hc = 0, inrange = 0;
        for (ImageBuf::ConstIterator<T> p(src, roi); !p.done(); ++p) {
            bool lowval = false, highval = false;
            for (int c = roi.chbegin; c < roi.chend; ++c) {
                float f = p[c];
                lowval |= (f < low[c]);
                highval |= (f > high[c]);
            }
            if (lowval)
                ++lc;
            if (highval)
                ++hc;
            if (!lowval && !highval)
                ++inrange;
        }

        if (lowcount)
            *lowcount += lc;
        if (highcount)
            *highcount += hc;
        if (inrangecount)
            *inrangecount += inrange;
    });
    return true;
}



bool
ImageBufAlgo::color_range_check(const ImageBuf& src, imagesize_t* lowcount,
                                imagesize_t* highcount,
                                imagesize_t* inrangecount, cspan<float> low,
                                cspan<float> high, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::color_range_check");
    // If no ROI is defined, use the data window of src.
    if (!roi.defined())
        roi = get_roi(src.spec());
    roi.chend       = std::min(roi.chend, src.nchannels());
    const float big = std::numeric_limits<float>::max();
    IBA_FIX_PERCHAN_LEN(low, src.nchannels(), -big, -big);
    IBA_FIX_PERCHAN_LEN(high, src.nchannels(), big, big);

    if (lowcount)
        *lowcount = 0;
    if (highcount)
        *highcount = 0;
    if (inrangecount)
        *inrangecount = 0;
    bool ok;
    OIIO_DISPATCH_TYPES(ok, "color_range_check", color_range_check_,
                        src.spec().format, src, (atomic_ll*)lowcount,
                        (atomic_ll*)highcount, (atomic_ll*)inrangecount,
                        low.data(), high.data(), roi, nthreads);
    return ok;
}



// Helper: is the roi devoid of any deep samples?
static ROI
deep_nonempty_region(const ImageBuf& src, ROI roi)
{
    OIIO_DASSERT(src.deep());
    ROI r;  // Initially undefined
    for (int z = roi.zbegin; z < roi.zend; ++z)
        for (int y = roi.ybegin; y < roi.yend; ++y)
            for (int x = roi.xbegin; x < roi.xend; ++x)
                if (src.deep_samples(x, y, z) != 0) {
                    if (!r.defined()) {
                        r = ROI(x, x + 1, y, y + 1, z, z + 1, 0,
                                src.nchannels());
                    } else {
                        r.xbegin = std::min(x, r.xbegin);
                        r.xend   = std::max(x + 1, r.xend);
                        r.ybegin = std::min(y, r.ybegin);
                        r.yend   = std::max(y + 1, r.yend);
                        r.zbegin = std::min(z, r.zbegin);
                        r.zend   = std::max(z + 1, r.zend);
                    }
                }
    return r;
}



ROI
ImageBufAlgo::nonzero_region(const ImageBuf& src, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::nonzero_region");
    roi = roi_intersection(roi, src.roi());

    if (src.deep()) {
        return deep_nonempty_region(src, roi);
    }

    std::vector<float> zero(src.nchannels(), 0.0f);
    std::vector<float> color(src.nchannels(), 0.0f);
    // Trim bottom
    for (; roi.ybegin < roi.yend; --roi.yend) {
        ROI test    = roi;
        test.ybegin = roi.yend - 1;
        if (!isConstantColor(src, &color[0], test, nthreads) || color != zero)
            break;
    }
    // Trim top
    for (; roi.ybegin < roi.yend; ++roi.ybegin) {
        ROI test  = roi;
        test.yend = roi.ybegin + 1;
        if (!isConstantColor(src, &color[0], test, nthreads) || color != zero)
            break;
    }
    // Trim right
    for (; roi.xbegin < roi.xend; --roi.xend) {
        ROI test    = roi;
        test.xbegin = roi.xend - 1;
        if (!isConstantColor(src, &color[0], test, nthreads) || color != zero)
            break;
    }
    // Trim left
    for (; roi.xbegin < roi.xend; ++roi.xbegin) {
        ROI test  = roi;
        test.xend = roi.xbegin + 1;
        if (!isConstantColor(src, &color[0], test, nthreads) || color != zero)
            break;
    }
    if (roi.depth() > 1) {
        // Trim zbottom
        for (; roi.zbegin < roi.zend; --roi.zend) {
            ROI test    = roi;
            test.zbegin = roi.zend - 1;
            if (!isConstantColor(src, &color[0], test, nthreads)
                || color != zero)
                break;
        }
        // Trim ztop
        for (; roi.zbegin < roi.zend; ++roi.zbegin) {
            ROI test  = roi;
            test.zend = roi.zbegin + 1;
            if (!isConstantColor(src, &color[0], test, nthreads)
                || color != zero)
                break;
        }
    }
    return roi;
}



namespace {

std::string
simplePixelHashSHA1(const ImageBuf& src, string_view extrainfo, ROI roi)
{
    if (!roi.defined())
        roi = get_roi(src.spec());

    bool localpixels           = src.localpixels();
    imagesize_t scanline_bytes = roi.width() * src.spec().pixel_bytes();
    OIIO_ASSERT(scanline_bytes < std::numeric_limits<unsigned int>::max());
    // Do it a few scanlines at a time
    int chunk = std::max(1, int(16 * 1024 * 1024 / scanline_bytes));

    std::vector<unsigned char> tmp;
    if (!localpixels)
        tmp.resize(chunk * scanline_bytes);

    SHA1 sha;
    for (int z = roi.zbegin, zend = roi.zend; z < zend; ++z) {
        for (int y = roi.ybegin, yend = roi.yend; y < yend; y += chunk) {
            int y1 = std::min(y + chunk, yend);
            if (localpixels) {
                sha.append(src.pixeladdr(roi.xbegin, y, z),
                           size_t(scanline_bytes * (y1 - y)));
            } else {
                src.get_pixels(ROI(roi.xbegin, roi.xend, y, y1, z, z + 1),
                               src.spec().format, &tmp[0]);
                sha.append(&tmp[0], size_t(scanline_bytes) * (y1 - y));
            }
        }
    }

    // If extra info is specified, also include it in the sha computation
    sha.append(extrainfo.data(), extrainfo.size());

    return sha.digest();
}

}  // namespace



std::string
ImageBufAlgo::computePixelHashSHA1(const ImageBuf& src, string_view extrainfo,
                                   ROI roi, int blocksize, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::computePixelHashSHA1");
    if (!roi.defined())
        roi = get_roi(src.spec());

    if (blocksize <= 0 || blocksize >= roi.height())
        return simplePixelHashSHA1(src, extrainfo, roi);

    // clang-format off
    int nblocks = (roi.height() + blocksize - 1) / blocksize;
    OIIO_ASSERT(nblocks > 1);
    std::vector<std::string> results(nblocks);
    parallel_for_chunked(roi.ybegin, roi.yend, blocksize,
                         [&](int64_t ybegin, int64_t yend) {
        int64_t b   = (ybegin - src.ybegin()) / blocksize;  // block number
        ROI broi    = roi;
        broi.ybegin = ybegin;
        broi.yend   = yend;
        results[b]  = simplePixelHashSHA1(src, "", broi);
    }, nthreads);
    // clang-format on

    // If there are multiple blocks, hash the block digests to get a final
    // hash. (This makes the parallel loop safe, because the order that the
    // blocks computed doesn't matter.)
    SHA1 sha;
    for (int b = 0; b < nblocks; ++b)
        sha.append(results[b].c_str(), results[b].size());
    sha.append(extrainfo.c_str(), extrainfo.size());
    return sha.digest();
}



template<class Atype>
static bool
histogram_impl(const ImageBuf& src, int channel, std::vector<imagesize_t>& hist,
               int bins, float min, float max, bool ignore_empty, ROI roi,
               int nthreads)
{
    // Double check A's type.
    if (src.spec().format != BaseTypeFromC<Atype>::value) {
        src.errorfmt("Unsupported pixel data format '{}'", src.spec().format);
        return false;
    }

    std::mutex mutex;  // thread safety for the histogram result

    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        float ratio      = bins / (max - min);
        int bins_minus_1 = bins - 1;

        // Compute histogram to thread-local h.
        std::vector<imagesize_t> h(bins, 0);
        for (ImageBuf::ConstIterator<Atype> a(src, roi); !a.done(); a++) {
            if (ignore_empty) {
                bool allblack = true;
                for (int c = roi.chbegin; c < roi.chend; ++c)
                    allblack &= (a[c] == 0.0f);
                if (allblack)
                    continue;
            }
            float val = a[channel];
            val       = clamp(val, min, max);
            int i     = clamp(int((val - min) * ratio), 0, bins_minus_1);
            h[i] += 1;
        }

        // Safely update the master histogram
        lock_guard lock(mutex);
        for (int i = 0; i < bins; ++i)
            hist[i] += h[i];
    });
    return true;
}



std::vector<imagesize_t>
ImageBufAlgo::histogram(const ImageBuf& src, int channel, int bins, float min,
                        float max, bool ignore_empty, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtimer("IBA::histogram");
    std::vector<imagesize_t> h;

    // Sanity checks
    if (src.nchannels() == 0) {
        src.errorfmt("Input image must have at least 1 channel");
        return h;
    }
    if (channel < 0 || channel >= src.nchannels()) {
        src.errorfmt("Invalid channel {} for input image with channels 0 to {}",
                     channel, src.nchannels() - 1);
        return h;
    }
    if (bins < 1) {
        src.errorfmt("The number of bins must be at least 1");
        return h;
    }
    if (max <= min) {
        src.errorfmt("Invalid range, min must be strictly smaller than max");
        return h;
    }

    // Specified ROI -> use it. Unspecified ROI -> initialize from src.
    if (!roi.defined())
        roi = get_roi(src.spec());

    h.resize(bins);
    bool ok = true;
    OIIO_DISPATCH_TYPES(ok, "histogram", histogram_impl, src.spec().format, src,
                        channel, h, bins, min, max, ignore_empty, roi,
                        nthreads);

    if (!ok && src.has_error())
        h.clear();
    return h;
}



/// histogram_impl -----------------------------------------------------------
/// Fully type-specialized version of histogram.
///
/// Pixel values in min->max range are mapped to 0->(bins-1) range, so that
/// each value is placed in the appropriate bin. The formula used is:
/// y = (x-min) * bins/(max-min), where y is the value in the 0->(bins-1)
/// range and x is the value in the min->max range. There is one special
/// case x==max for which the formula is not used and x is assigned to the
/// last bin at position (bins-1) in the vector histogram.
/// --------------------------------------------------------------------------
template<class Atype>
static bool
histogram_impl_old(const ImageBuf& A, int channel,
                   std::vector<imagesize_t>& histogram, int bins, float min,
                   float max, imagesize_t* submin, imagesize_t* supermax,
                   ROI roi)
{
    // Double check A's type.
    if (A.spec().format != BaseTypeFromC<Atype>::value) {
        A.errorfmt("Unsupported pixel data format '{}'", A.spec().format);
        return false;
    }

    // Initialize.
    ImageBuf::ConstIterator<Atype, float> a(A, roi);
    float ratio      = bins / (max - min);
    int bins_minus_1 = bins - 1;
    bool submin_ok   = submin != NULL;
    bool supermax_ok = supermax != NULL;
    if (submin_ok)
        *submin = 0;
    if (supermax_ok)
        *supermax = 0;
    histogram.assign(bins, 0);

    // Compute histogram.
    for (; !a.done(); a++) {
        float c = a[channel];
        if (c >= min && c < max) {
            // Map range min->max to 0->(bins-1).
            histogram[(int)((c - min) * ratio)]++;
        } else if (c == max) {
            histogram[bins_minus_1]++;
        } else {
            if (submin_ok && c < min)
                (*submin)++;
            else if (supermax_ok)
                (*supermax)++;
        }
    }
    return true;
}



bool
ImageBufAlgo::histogram(const ImageBuf& A, int channel,
                        std::vector<imagesize_t>& histogram, int bins,
                        float min, float max, imagesize_t* submin,
                        imagesize_t* supermax, ROI roi)
{
    pvt::LoggedTimer logtimer("IBA::histogram");
    if (A.spec().format != TypeFloat) {
        A.errorfmt("Unsupported pixel data format '{}'", A.spec().format);
        return false;
    }

    if (A.nchannels() == 0) {
        A.errorfmt("Input image must have at least 1 channel");
        return false;
    }

    if (channel < 0 || channel >= A.nchannels()) {
        A.errorfmt("Invalid channel {} for input image with channels 0 to {}",
                   channel, A.nchannels() - 1);
        return false;
    }

    if (bins < 1) {
        A.errorfmt("The number of bins must be at least 1");
        return false;
    }

    if (max <= min) {
        A.errorfmt("Invalid range, min must be strictly smaller than max");
        return false;
    }

    // Specified ROI -> use it. Unspecified ROI -> initialize from A.
    if (!roi.defined())
        roi = get_roi(A.spec());

    histogram_impl_old<float>(A, channel, histogram, bins, min, max, submin,
                              supermax, roi);

    return !A.has_error();
}



bool
ImageBufAlgo::histogram_draw(ImageBuf& R,
                             const std::vector<imagesize_t>& histogram)
{
    pvt::LoggedTimer logtimer("IBA::histogram_draw");
    // Fail if there are no bins to draw.
    int bins = histogram.size();
    if (bins == 0) {
        R.errorfmt("There are no bins to draw, the histogram is empty");
        return false;
    }

    // Check R and modify it if needed.
    int height = R.spec().height;
    if (R.spec().format != TypeFloat || R.nchannels() != 1
        || R.spec().width != bins) {
        ImageSpec newspec = ImageSpec(bins, height, 1, TypeDesc::FLOAT);
        R.reset("dummy", newspec);
    }

    // Fill output image R with white color.
    ImageBuf::Iterator<float, float> r(R);
    for (; !r.done(); ++r)
        r[0] = 1;

    // Draw histogram left->right, bottom->up.
    imagesize_t max = *std::max_element(histogram.begin(), histogram.end());
    for (int b = 0; b < bins; b++) {
        int bin_height = (int)((float)histogram[b] / (float)max * height
                               + 0.5f);
        if (bin_height != 0) {
            // Draw one bin at column b.
            for (int j = 1; j <= bin_height; j++) {
                int row = height - j;
                r.pos(b, row);
                r[0] = 0;
            }
        }
    }
    return true;
}



OIIO_NAMESPACE_END
