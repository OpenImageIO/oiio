// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


/// \file
/// Implementation of ImageBufAlgo algorithms.

#include <cmath>
#include <iostream>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
using Imath::Color3f;



template<class T>
inline Imath::Vec3<T>
powf(const Imath::Vec3<T>& x, float y)
{
    return Imath::Vec3<T>(powf(x[0], y), powf(x[1], y), powf(x[2], y));
}



OIIO_NAMESPACE_BEGIN

namespace {

#define PYRAMID_MAX_LEVELS 8


class GaussianPyramid {
public:
    GaussianPyramid(ImageBuf& image)
    {
        level[0].swap(image);  // swallow the source as the top level
        ImageBuf kernel = ImageBufAlgo::make_kernel("gaussian", 5, 5);
        for (int i = 1; i < PYRAMID_MAX_LEVELS; ++i)
            ImageBufAlgo::convolve(level[i], level[i - 1], kernel);
    }

    ~GaussianPyramid() {}

    float value(int x, int y, int lev) const
    {
        if (lev >= PYRAMID_MAX_LEVELS)
            return 0.0f;
        else
            return level[lev].getchannel(x, y, 0, 1);
    }

    ImageBuf& operator[](int lev)
    {
        OIIO_DASSERT(lev < PYRAMID_MAX_LEVELS);
        return level[lev];
    }

    float operator()(int x, int y, int lev) const
    {
        OIIO_DASSERT(lev < PYRAMID_MAX_LEVELS);
        return level[lev].getchannel(x, y, 0, 1);
    }

private:
    ImageBuf level[PYRAMID_MAX_LEVELS];
};



// Adobe RGB (1998) with reference white D65 -> XYZ
// matrix is from http://www.brucelindbloom.com/
inline Color3f
AdobeRGBToXYZ_color(const Color3f& rgb)
{
    return Color3f(rgb[0] * 0.576700f + rgb[1] * 0.185556f + rgb[2] * 0.188212f,
                   rgb[0] * 0.297361f + rgb[1] * 0.627355f
                       + rgb[2] * 0.0752847f,
                   rgb[0] * 0.0270328f + rgb[1] * 0.0706879f
                       + rgb[2] * 0.991248f);
}



static bool
AdobeRGBToXYZ(ImageBuf& A, ROI roi, int nthreads)
{
    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        for (ImageBuf::Iterator<float> a(A, roi); !a.done(); ++a) {
            Color3f rgb(a[0], a[1], a[2]);
            Color3f XYZ = AdobeRGBToXYZ_color(rgb);
            a[0]        = XYZ[0];
            a[1]        = XYZ[1];
            a[2]        = XYZ[2];
        }
    });
    return true;
}



/// Convert a color in XYZ space to LAB space.
///
static Color3f
XYZToLAB_color(const Color3f xyz)
{
    // Reference white point
    static const Color3f white(0.576700f + 0.185556f + 0.188212f,
                               0.297361f + 0.627355f + 0.0752847f,
                               0.0270328f + 0.0706879f + 0.991248f);
    const float epsilon = 216.0f / 24389.0f;
    const float kappa   = 24389.0f / 27.0f;

    Color3f r = xyz / white;
    Color3f f;
    for (int i = 0; i < 3; i++) {
        if (r[i] > epsilon)
            f[i] = powf(r[i], 1.0f / 3.0f);
        else
            f[i] = (kappa * r[i] + 16.0f) / 116.0f;
    }
    return Color3f(116.0f * f[1] - 16.0f,    // L
                   500.0f * (f[0] - f[1]),   // A
                   200.0f * (f[1] - f[2]));  // B
}



static bool
XYZToLAB(ImageBuf& A, ROI roi, int nthreads)
{
    ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        for (ImageBuf::Iterator<float> a(A, roi); !a.done(); ++a) {
            Color3f XYZ(a[0], a[1], a[2]);
            Color3f LAB = XYZToLAB_color(XYZ);
            a[0]        = LAB[0];
            a[1]        = LAB[1];
            a[2]        = LAB[2];
        }
    });
    return true;
}



// Contrast sensitivity function (Barten SPIE 1989)
static float
contrast_sensitivity(float cyclesperdegree, float luminance)
{
    float a = 440.0f * powf((1.0f + 0.7f / luminance), -0.2f);
    float b = 0.3f * powf((1.0f + 100.0f / luminance), 0.15f);
    return a * cyclesperdegree * expf(-b * cyclesperdegree)
           * sqrtf(1.0f + 0.06f * expf(b * cyclesperdegree));
}



// Visual Masking Function from Daly 1993
inline float
mask(float contrast)
{
    float a = powf(392.498f * contrast, 0.7f);
    float b = powf(0.0153f * a, 4.0f);
    return powf(1.0f + b, 0.25f);
}



// Given the adaptation luminance, this function returns the
// threshold of visibility in cd per m^2
// TVI means Threshold vs Intensity function
// This version comes from Ward Larson Siggraph 1997
static float
tvi(float adaptation_luminance)
{
    // returns the threshold luminance given the adaptation luminance
    // units are candelas per meter squared
    float r;
    float log_a = log10f(adaptation_luminance);
    if (log_a < -3.94f)
        r = -2.86f;
    else if (log_a < -1.44f)
        r = powf(0.405f * log_a + 1.6f, 2.18f) - 2.86f;
    else if (log_a < -0.0184f)
        r = log_a - 0.395f;
    else if (log_a < 1.9f)
        r = powf(0.249f * log_a + 0.65f, 2.7f) - 0.72f;
    else
        r = log_a - 1.255f;
    return powf(10.0f, r);
}


}  // namespace



int
ImageBufAlgo::compare_Yee(const ImageBuf& img0, const ImageBuf& img1,
                          CompareResults& result, float luminance, float fov,
                          ROI roi, int nthreads)
{
    if (!roi.defined())
        roi = roi_union(get_roi(img0.spec()), get_roi(img1.spec()));
    roi.chend = std::max(roi.chend, roi.chbegin + 3);  // max of 3 channels

    result.maxerror = 0;
    result.maxx = 0, result.maxy = 0, result.maxz = 0, result.maxc = 0;
    result.nfail = 0, result.nwarn = 0;

    int nscanlines = roi.height() * roi.depth();

    bool luminanceOnly = false;

    // assuming colorspaces are in Adobe RGB (1998), convert to LAB

    // paste() to copy of up to 3 channels, converting to float, and
    // ending up with a 0-origin image.  End up with an LAB image in
    // aLAB, and a luminance image in aLum.
    ImageSpec spec(roi.width(), roi.height(), 3 /*chans*/, TypeDesc::FLOAT);
    ImageBuf aLAB(spec);
    ImageBufAlgo::paste(aLAB, 0, 0, 0, 0, img0, roi, nthreads);
    AdobeRGBToXYZ(aLAB, ROI::All(), nthreads);  // contains XYZ now
    ImageBuf aLum;
    int channelorder[] = { 1 };  // channel to copy
    ImageBufAlgo::channels(aLum, aLAB, 1, channelorder);
    ImageBufAlgo::mul(aLum, aLum, luminance, ROI::All(), nthreads);
    XYZToLAB(aLAB, ROI::All(), nthreads);  // now it's LAB

    // Same thing for img1/bLAB/bLum
    ImageBuf bLAB(spec);
    ImageBufAlgo::paste(bLAB, 0, 0, 0, 0, img1, roi, nthreads);
    AdobeRGBToXYZ(bLAB, ROI::All(), nthreads);  // contains XYZ now
    ImageBuf bLum;
    ImageBufAlgo::channels(bLum, bLAB, 1, channelorder);
    ImageBufAlgo::mul(bLum, bLum, luminance, ROI::All(), nthreads);
    XYZToLAB(bLAB, ROI::All(), nthreads);  // now it's LAB

    // Construct Gaussian pyramids (not really pyramids, because they all
    // have the same resolution, but really just a bunch of successively
    // more blurred images).
    GaussianPyramid la(aLum);
    GaussianPyramid lb(bLum);

    float num_one_degree_pixels = (float)(2 * tan(fov * 0.5 * M_PI / 180) * 180
                                          / M_PI);
    float pixels_per_degree     = roi.width() / num_one_degree_pixels;

    unsigned int adaptation_level = 0;
    for (int i = 0, npixels = 1;
         i < PYRAMID_MAX_LEVELS && npixels <= num_one_degree_pixels;
         ++i, npixels *= 2)
        adaptation_level = i;

    float cpd[PYRAMID_MAX_LEVELS];
    cpd[0] = 0.5f * pixels_per_degree;
    for (int i = 1; i < PYRAMID_MAX_LEVELS; ++i)
        cpd[i] = 0.5f * cpd[i - 1];
    float csf_max = contrast_sensitivity(3.248f, 100.0f);

    float F_freq[PYRAMID_MAX_LEVELS - 2];
    for (int i = 0; i < PYRAMID_MAX_LEVELS - 2; ++i)
        F_freq[i] = csf_max / contrast_sensitivity(cpd[i], 100.0f);

    for (int y = 0; y < nscanlines; ++y) {
        for (int x = 0; x < roi.width(); ++x) {
            float contrast[PYRAMID_MAX_LEVELS - 2];
            float sum_contrast = 0;
            for (int i = 0; i < PYRAMID_MAX_LEVELS - 2; i++) {
                float n1 = fabsf(la.value(x, y, i) - la.value(x, y, i + 1));
                float n2 = fabsf(lb.value(x, y, i) - lb.value(x, y, i + 1));
                float numerator   = std::max(n1, n2);
                float d1          = fabsf(la.value(x, y, i + 2));
                float d2          = fabsf(lb.value(x, y, i + 2));
                float denominator = std::max(std::max(d1, d2), 1.0e-5f);
                contrast[i]       = numerator / denominator;
                sum_contrast += contrast[i];
            }
            if (sum_contrast < 1e-5)
                sum_contrast = 1e-5f;
            float F_mask[PYRAMID_MAX_LEVELS - 2];
            float adapt = la.value(x, y, adaptation_level)
                          + lb.value(x, y, adaptation_level);
            adapt *= 0.5f;
            if (adapt < 1e-5)
                adapt = 1e-5f;
            for (int i = 0; i < PYRAMID_MAX_LEVELS - 2; i++)
                F_mask[i] = mask(contrast[i]
                                 * contrast_sensitivity(cpd[i], adapt));
            float factor = 0;
            for (int i = 0; i < PYRAMID_MAX_LEVELS - 2; i++)
                factor += contrast[i] * F_freq[i] * F_mask[i] / sum_contrast;
            factor      = OIIO::clamp(factor, 1.0f, 10.0f);
            float delta = fabsf(la.value(x, y, 0) - lb.value(x, y, 0));
            bool pass   = true;
            // pure luminance test
            delta /= tvi(adapt);
            if (delta > factor) {
                pass = false;
            } else if (!luminanceOnly) {
                // CIE delta E test with modifications
                float color_scale = 1.0f;
                // ramp down the color test in scotopic regions
                if (adapt < 10.0f) {
                    color_scale = 1.0f - (10.0f - color_scale) / 10.0f;
                    color_scale = color_scale * color_scale;
                }
                float da = aLAB.getchannel(x, y, 0, 1)
                           - bLAB.getchannel(x, y, 0, 1);  // diff in A
                float db = aLAB.getchannel(x, y, 0, 2)
                           - bLAB.getchannel(x, y, 0, 2);  // diff in B
                da    = da * da;
                db    = db * db;
                delta = (da + db) * color_scale;
                if (delta > factor)
                    pass = false;
            }
            if (!pass) {
                ++result.nfail;
                if (factor > result.maxerror) {
                    result.maxerror = factor;
                    result.maxx     = x;
                    result.maxy     = y;
                    //                    result.maxz = z;
                }
            }
        }
    }

    return result.nfail;
}


OIIO_NAMESPACE_END
