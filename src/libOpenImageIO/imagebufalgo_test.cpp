// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <cstdio>
#include <iomanip>
#include <iostream>
#include <string>

#include <OpenImageIO/platform.h>

#if USE_OPENCV
// Suppress gcc 11 / C++20 errors about opencv 4 headers
#    if OIIO_GNUC_VERSION >= 110000 && OIIO_CPLUSPLUS_VERSION >= 20
#        pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#    endif
#    include <opencv2/opencv.hpp>
#endif

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/benchmark.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/unittest.h>

using namespace OIIO;


static int iterations     = 1;
static int numthreads     = 16;
static int ntrials        = 1;
static bool verbose       = false;
static bool wedge         = false;
static int threadcounts[] = { 1,  2,  4,  8,  12,  16,   20,
                              24, 28, 32, 64, 128, 1024, 1 << 30 };


static void
getargs(int argc, char* argv[])
{
    ArgParse ap;
    // clang-format off
    ap.intro("imagebufalgo_test\n" OIIO_INTRO_STRING)
      .usage("imagebufalgo_test [options]");

    ap.arg("-v", &verbose)
      .help("Verbose mode");
    ap.arg("--threads %d", &numthreads)
      .help(Strutil::sprintf("Number of threads (default: %d)", numthreads));
    ap.arg("--iters %d", &iterations)
      .help(Strutil::sprintf("Number of iterations (default: %d)", iterations));
    ap.arg("--trials %d", &ntrials)
      .help("Number of trials");
    ap.arg("--wedge", &wedge)
      .help("Do a wedge test");
    // clang-format on

    ap.parse(argc, (const char**)argv);
}



void
test_type_merge()
{
    std::cout << "test type_merge\n";
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::UINT8, TypeDesc::UINT8),
                     TypeDesc::UINT8);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::UINT8, TypeDesc::FLOAT),
                     TypeDesc::FLOAT);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::FLOAT, TypeDesc::UINT8),
                     TypeDesc::FLOAT);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::UINT8, TypeDesc::UINT16),
                     TypeDesc::UINT16);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::UINT16, TypeDesc::FLOAT),
                     TypeDesc::FLOAT);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::HALF, TypeDesc::FLOAT),
                     TypeDesc::FLOAT);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::HALF, TypeDesc::UINT8),
                     TypeDesc::HALF);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::HALF, TypeDesc::UNKNOWN),
                     TypeDesc::HALF);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::FLOAT,
                                              TypeDesc::UNKNOWN),
                     TypeDesc::FLOAT);
    OIIO_CHECK_EQUAL(TypeDesc::basetype_merge(TypeDesc::UINT8,
                                              TypeDesc::UNKNOWN),
                     TypeDesc::UINT8);
}



// Test ImageBuf::zero and ImageBuf::fill
void
test_zero_fill()
{
    std::cout << "test zero_fill\n";
    const int WIDTH    = 8;
    const int HEIGHT   = 6;
    const int CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    spec.alpha_channel = 3;

    // Create a buffer -- pixels should be undefined
    ImageBuf A(spec);

    // Set a pixel to an odd value, make sure it takes
    const float arbitrary1[CHANNELS] = { 0.2f, 0.3f, 0.4f, 0.5f };
    A.setpixel(1, 1, arbitrary1);
    float pixel[CHANNELS];  // test pixel
    A.getpixel(1, 1, pixel);
    for (int c = 0; c < CHANNELS; ++c)
        OIIO_CHECK_EQUAL(pixel[c], arbitrary1[c]);

    // Zero out and test that it worked
    ImageBufAlgo::zero(A);
    for (int j = 0; j < HEIGHT; ++j) {
        for (int i = 0; i < WIDTH; ++i) {
            float pixel[CHANNELS];
            A.getpixel(i, j, pixel);
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(pixel[c], 0.0f);
        }
    }

    // Test fill of whole image
    const float arbitrary2[CHANNELS] = { 0.6f, 0.7f, 0.3f, 0.9f };
    ImageBufAlgo::fill(A, arbitrary2);
    for (int j = 0; j < HEIGHT; ++j) {
        for (int i = 0; i < WIDTH; ++i) {
            float pixel[CHANNELS];
            A.getpixel(i, j, pixel);
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(pixel[c], arbitrary2[c]);
        }
    }

    // Test fill of partial image
    const float arbitrary3[CHANNELS] = { 0.42f, 0.43f, 0.44f, 0.45f };
    {
        const int xbegin = 3, xend = 5, ybegin = 0, yend = 4;
        ImageBufAlgo::fill(A, arbitrary3, ROI(xbegin, xend, ybegin, yend));
        for (int j = 0; j < HEIGHT; ++j) {
            for (int i = 0; i < WIDTH; ++i) {
                float pixel[CHANNELS];
                A.getpixel(i, j, pixel);
                if (j >= ybegin && j < yend && i >= xbegin && i < xend) {
                    for (int c = 0; c < CHANNELS; ++c)
                        OIIO_CHECK_EQUAL(pixel[c], arbitrary3[c]);
                } else {
                    for (int c = 0; c < CHANNELS; ++c)
                        OIIO_CHECK_EQUAL(pixel[c], arbitrary2[c]);
                }
            }
        }
    }

    // Timing
    Benchmarker bench;
    ImageBuf buf_rgba_float(ImageSpec(1000, 1000, 4, TypeFloat));
    ImageBuf buf_rgba_uint8(ImageSpec(1000, 1000, 4, TypeUInt8));
    ImageBuf buf_rgba_half(ImageSpec(1000, 1000, 4, TypeHalf));
    ImageBuf buf_rgba_uint16(ImageSpec(1000, 1000, 4, TypeDesc::UINT16));
    float vals[] = { 0, 0, 0, 0 };
    bench("  IBA::fill float[4] ",
          [&]() { ImageBufAlgo::fill(buf_rgba_float, vals); });
    bench("  IBA::fill uint8[4] ",
          [&]() { ImageBufAlgo::fill(buf_rgba_uint8, vals); });
    bench("  IBA::fill uint16[4] ",
          [&]() { ImageBufAlgo::fill(buf_rgba_uint16, vals); });
    bench("  IBA::fill half[4] ",
          [&]() { ImageBufAlgo::fill(buf_rgba_half, vals); });
}



// Test ImageBuf::copy
void
test_copy()
{
    std::cout << "test copy\n";

    // Make image A red, image B green, copy part of B to A and check result
    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    // copy region we'll work with
    ROI roi(2, 4, 1, 3);
    ImageBuf A(spec), B(spec);
    float red[4]   = { 1, 0, 0, 1 };
    float green[4] = { 0, 0, 0.5, 0.5 };
    ImageBufAlgo::fill(A, red);
    ImageBufAlgo::fill(B, green);
    ImageBufAlgo::copy(A, B, TypeUnknown, roi);
    for (ImageBuf::ConstIterator<float> r(A); !r.done(); ++r) {
        if (roi.contains(r.x(), r.y())) {
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], green[c]);
        } else {
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], red[c]);
        }
    }

    // Test copying into a blank image
    A.clear();
    ImageBufAlgo::copy(A, B, TypeUnknown, roi);
    for (ImageBuf::ConstIterator<float> r(A); !r.done(); ++r) {
        if (roi.contains(r.x(), r.y())) {
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], green[c]);
        } else {
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], 0.0f);
        }
    }

    // Timing
    Benchmarker bench;
    ImageSpec spec_rgba_float(1000, 1000, 4, TypeFloat);
    ImageSpec spec_rgba_uint8(1000, 1000, 4, TypeUInt8);
    ImageSpec spec_rgba_half(1000, 1000, 4, TypeHalf);
    ImageSpec spec_rgba_int16(1000, 1000, 4, TypeDesc::INT16);
    ImageBuf buf_rgba_uint8(spec_rgba_uint8);
    ImageBuf buf_rgba_float(spec_rgba_float);
    ImageBuf buf_rgba_float2(spec_rgba_float);
    ImageBuf buf_rgba_half(spec_rgba_half);
    ImageBuf buf_rgba_half2(spec_rgba_half);
    ImageBuf empty;
    bench("  IBA::copy float[4] -> float[4] ",
          [&]() { ImageBufAlgo::copy(buf_rgba_float, buf_rgba_float2); });
    bench("  IBA::copy float[4] -> empty ", [&]() {
        empty.clear();
        ImageBufAlgo::copy(empty, buf_rgba_float2);
    });
    bench("  IBA::copy float[4] -> uint8[4] ",
          [&]() { ImageBufAlgo::copy(buf_rgba_uint8, buf_rgba_float2); });
    bench("  IBA::copy half[4] -> half[4] ",
          [&]() { ImageBufAlgo::copy(buf_rgba_half, buf_rgba_half2); });
    bench("  IBA::copy half[4] -> empty ", [&]() {
        empty.clear();
        ImageBufAlgo::copy(empty, buf_rgba_half2);
    });
}



// Test ImageBuf::crop
void
test_crop()
{
    std::cout << "test crop\n";
    int WIDTH = 8, HEIGHT = 6, CHANNELS = 4;
    // Crop region we'll work with
    int xbegin = 3, xend = 5, ybegin = 0, yend = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    spec.alpha_channel = 3;
    ImageBuf A, B;
    A.reset(spec);
    B.reset(spec);
    float arbitrary1[4];
    arbitrary1[0] = 0.2f;
    arbitrary1[1] = 0.3f;
    arbitrary1[2] = 0.4f;
    arbitrary1[3] = 0.5f;
    ImageBufAlgo::fill(A, arbitrary1);

    // Test CUT crop
    ImageBufAlgo::crop(B, A, ROI(xbegin, xend, ybegin, yend));

    // Should have changed the data window (origin and width/height)
    OIIO_CHECK_EQUAL(B.spec().x, xbegin);
    OIIO_CHECK_EQUAL(B.spec().width, xend - xbegin);
    OIIO_CHECK_EQUAL(B.spec().y, ybegin);
    OIIO_CHECK_EQUAL(B.spec().height, yend - ybegin);
    float* pixel = OIIO_ALLOCA(float, CHANNELS);
    for (int j = 0; j < B.spec().height; ++j) {
        for (int i = 0; i < B.spec().width; ++i) {
            B.getpixel(i + B.xbegin(), j + B.ybegin(), pixel);
            // Inside the crop region should match what it always was
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(pixel[c], arbitrary1[c]);
        }
    }
}



void
test_paste()
{
    std::cout << "test paste\n";
    // Create the source image, make it a color gradient
    ImageSpec Aspec(4, 4, 3, TypeDesc::FLOAT);
    ImageBuf A(Aspec);
    for (ImageBuf::Iterator<float> it(A); !it.done(); ++it) {
        it[0] = float(it.x()) / float(Aspec.width - 1);
        it[1] = float(it.y()) / float(Aspec.height - 1);
        it[2] = 0.1f;
    }

    // Create destination image -- fill with grey
    ImageSpec Bspec(8, 8, 3, TypeDesc::FLOAT);
    ImageBuf B(Bspec);
    float gray[3] = { 0.1f, 0.1f, 0.1f };
    ImageBufAlgo::fill(B, gray);

    // Paste a few pixels from A into B -- include offsets
    ImageBufAlgo::paste(B, 2, 2, 0, 1 /* chan offset */,
                        ImageBufAlgo::cut(A, ROI(1, 4, 1, 4)));

    // Spot check
    float a[3], b[3];
    B.getpixel(1, 1, 0, b);
    OIIO_CHECK_EQUAL(b[0], gray[0]);
    OIIO_CHECK_EQUAL(b[1], gray[1]);
    OIIO_CHECK_EQUAL(b[2], gray[2]);

    B.getpixel(2, 2, 0, b);
    A.getpixel(1, 1, 0, a);
    OIIO_CHECK_EQUAL(b[0], gray[0]);
    OIIO_CHECK_EQUAL(b[1], a[0]);
    OIIO_CHECK_EQUAL(b[2], a[1]);

    B.getpixel(3, 4, 0, b);
    A.getpixel(2, 3, 0, a);
    OIIO_CHECK_EQUAL(b[0], gray[0]);
    OIIO_CHECK_EQUAL(b[1], a[0]);
    OIIO_CHECK_EQUAL(b[2], a[1]);
}



void
test_channel_append()
{
    std::cout << "test channel_append\n";
    ImageSpec spec(2, 2, 1, TypeDesc::FLOAT);
    ImageBuf A(spec);
    ImageBuf B(spec);
    float Acolor = 0.1, Bcolor = 0.2;
    ImageBufAlgo::fill(A, &Acolor);
    ImageBufAlgo::fill(B, &Bcolor);

    ImageBuf R = ImageBufAlgo::channel_append(A, B);
    OIIO_CHECK_EQUAL(R.spec().width, spec.width);
    OIIO_CHECK_EQUAL(R.spec().height, spec.height);
    OIIO_CHECK_EQUAL(R.nchannels(), 2);
    for (ImageBuf::ConstIterator<float> r(R); !r.done(); ++r) {
        OIIO_CHECK_EQUAL(r[0], Acolor);
        OIIO_CHECK_EQUAL(r[1], Bcolor);
    }
}



// Tests ImageBufAlgo::add
void
test_add()
{
    std::cout << "test add\n";
    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);

    // Create buffers
    ImageBuf A(spec);
    const float Aval[CHANNELS] = { 0.1f, 0.2f, 0.3f, 0.4f };
    ImageBufAlgo::fill(A, Aval);
    ImageBuf B(spec);
    const float Bval[CHANNELS] = { 0.01f, 0.02f, 0.03f, 0.04f };
    ImageBufAlgo::fill(B, Bval);

    // Test addition of images
    ImageBuf R(spec);
    ImageBufAlgo::add(R, A, B);
    for (int j = 0; j < spec.height; ++j)
        for (int i = 0; i < spec.width; ++i)
            for (int c = 0; c < spec.nchannels; ++c)
                OIIO_CHECK_EQUAL(R.getchannel(i, j, 0, c), Aval[c] + Bval[c]);

    // Test addition of image and constant color
    ImageBuf D(spec);
    ImageBufAlgo::add(D, A, Bval);
    auto comp = ImageBufAlgo::compare(R, D, 1e-6f, 1e-6f);
    OIIO_CHECK_EQUAL(comp.maxerror, 0.0f);
}



// Tests ImageBufAlgo::sub
void
test_sub()
{
    std::cout << "test sub\n";
    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);

    // Create buffers
    ImageBuf A(spec);
    const float Aval[CHANNELS] = { 0.1f, 0.2f, 0.3f, 0.4f };
    ImageBufAlgo::fill(A, Aval);
    ImageBuf B(spec);
    const float Bval[CHANNELS] = { 0.01f, 0.02f, 0.03f, 0.04f };
    ImageBufAlgo::fill(B, Bval);

    // Test subtraction of images
    ImageBuf R(spec);
    ImageBufAlgo::sub(R, A, B);
    for (int j = 0; j < spec.height; ++j)
        for (int i = 0; i < spec.width; ++i)
            for (int c = 0; c < spec.nchannels; ++c)
                OIIO_CHECK_EQUAL(R.getchannel(i, j, 0, c), Aval[c] - Bval[c]);

    // Test subtraction of image and constant color
    ImageBuf D(spec);
    ImageBufAlgo::sub(D, A, Bval);
    auto comp = ImageBufAlgo::compare(R, D, 1e-6f, 1e-6f);
    OIIO_CHECK_EQUAL(comp.maxerror, 0.0f);
}



// Tests ImageBufAlgo::mul
void
test_mul()
{
    std::cout << "test mul\n";
    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);

    // Create buffers
    ImageBuf A(spec);
    const float Aval[CHANNELS] = { 0.1f, 0.2f, 0.3f, 0.4f };
    ImageBufAlgo::fill(A, Aval);
    ImageBuf B(spec);
    const float Bval[CHANNELS] = { 0.01f, 0.02f, 0.03f, 0.04f };
    ImageBufAlgo::fill(B, Bval);

    // Test multiplication of images
    ImageBuf R(spec);
    ImageBufAlgo::mul(R, A, B);
    for (int j = 0; j < spec.height; ++j)
        for (int i = 0; i < spec.width; ++i)
            for (int c = 0; c < spec.nchannels; ++c)
                OIIO_CHECK_EQUAL(R.getchannel(i, j, 0, c), Aval[c] * Bval[c]);

    // Test multiplication of image and constant color
    ImageBuf D(spec);
    ImageBufAlgo::mul(D, A, Bval);
    auto comp = ImageBufAlgo::compare(R, D, 1e-6f, 1e-6f);
    OIIO_CHECK_EQUAL(comp.maxerror, 0.0f);
}



// Tests ImageBufAlgo::mad
void
test_mad()
{
    std::cout << "test mad\n";
    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);

    // Create buffers
    ImageBuf A(spec);
    const float Aval[CHANNELS] = { 0.1f, 0.2f, 0.3f, 0.4f };
    ImageBufAlgo::fill(A, Aval);
    ImageBuf B(spec);
    const float Bval[CHANNELS] = { 1, 2, 3, 4 };
    ImageBufAlgo::fill(B, Bval);
    ImageBuf C(spec);
    const float Cval[CHANNELS] = { 0.01f, 0.02f, 0.03f, 0.04f };
    ImageBufAlgo::fill(C, Cval);

    // Test multiplication of images
    ImageBuf R(spec);
    ImageBufAlgo::mad(R, A, B, C);
    for (int j = 0; j < spec.height; ++j)
        for (int i = 0; i < spec.width; ++i)
            for (int c = 0; c < spec.nchannels; ++c)
                OIIO_CHECK_EQUAL(R.getchannel(i, j, 0, c),
                                 Aval[c] * Bval[c] + Cval[c]);

    // Test multiplication of image and constant color
    ImageBuf D(spec);
    ImageBufAlgo::mad(D, A, Bval, Cval);
    auto comp = ImageBufAlgo::compare(R, D, 1e-6f, 1e-6f);
    OIIO_CHECK_EQUAL(comp.maxerror, 0.0f);
}



// Test ImageBuf::over
void
test_over()
{
    std::cout << "test over\n";

    const int WIDTH = 4, HEIGHT = 4, CHANNELS = 4;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ROI roi(2, 4, 1, 3);  // region with fg

    // Create buffers
    ImageBuf BG(spec);
    const float BGval[CHANNELS] = { 0.5f, 0.0f, 0.0f, 0.5f };
    ImageBufAlgo::fill(BG, BGval);

    ImageBuf FG(spec);
    ImageBufAlgo::zero(FG);
    const float FGval[CHANNELS] = { 0.0f, 0.5f, 0.0f, 0.5f };
    ImageBufAlgo::fill(FG, FGval, roi);

    // value it should be where composited
    const float comp_val[CHANNELS] = { 0.25f, 0.5f, 0.0f, 0.75f };

    // Test over
    ImageBuf R(spec);
    ImageBufAlgo::over(R, FG, BG);
    for (ImageBuf::ConstIterator<float> r(R); !r.done(); ++r) {
        if (roi.contains(r.x(), r.y()))
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], comp_val[c]);
        else
            for (int c = 0; c < CHANNELS; ++c)
                OIIO_CHECK_EQUAL(r[c], BGval[c]);
    }

    // Timing
    Benchmarker bench;
    ImageSpec onekfloat(1000, 1000, 4, TypeFloat);
    BG.reset(onekfloat);
    ImageBufAlgo::fill(BG, BGval);
    FG.reset(onekfloat);
    ImageBufAlgo::zero(FG);
    ImageBufAlgo::fill(FG, FGval, ROI(250, 750, 100, 900));
    R.reset(onekfloat);
    bench("  IBA::over ", [&]() { ImageBufAlgo::over(R, FG, BG); });
}



// Tests ImageBufAlgo::compare
void
test_compare()
{
    std::cout << "test compare\n";
    // Construct two identical 50% grey images
    const int WIDTH = 10, HEIGHT = 10, CHANNELS = 3;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ImageBuf A(spec);
    ImageBuf B(spec);
    const float grey[CHANNELS] = { 0.5f, 0.5f, 0.5f };
    ImageBufAlgo::fill(A, grey);
    ImageBufAlgo::fill(B, grey);

    // Introduce some minor differences
    const int NDIFFS = 10;
    ImageBuf::Iterator<float> a(A);
    for (int i = 0; i < NDIFFS && a.valid(); ++i, ++a) {
        for (int c = 0; c < CHANNELS; ++c)
            a[c] = a[c] + 0.01f * i;
    }
    // We expect the differences to be { 0, 0.01, 0.02, 0.03, 0.04, 0.05,
    // 0.06, 0.07, 0.08, 0.09, 0, 0, ...}.
    const float failthresh = 0.05;
    const float warnthresh = 0.025;
    auto comp = ImageBufAlgo::compare(A, B, failthresh, warnthresh);
    // We expect 5 pixels to exceed the fail threshold, 7 pixels to
    // exceed the warn threshold, the maximum difference to be 0.09,
    // and the maximally different pixel to be (9,0).
    // The total error should be 3 chans * sum{0.01,...,0.09} / (pixels*chans)
    //   = 3 * 0.45 / (100*3) = 0.0045
    std::cout << "Testing comparison: " << comp.nfail << " failed, "
              << comp.nwarn << " warned, max diff = " << comp.maxerror << " @ ("
              << comp.maxx << ',' << comp.maxy << ")\n";
    std::cout << "   mean err " << comp.meanerror << ", RMS err "
              << comp.rms_error << ", PSNR = " << comp.PSNR << "\n";
    OIIO_CHECK_EQUAL(comp.nfail, 5);
    OIIO_CHECK_EQUAL(comp.nwarn, 7);
    OIIO_CHECK_EQUAL_THRESH(comp.maxerror, 0.09f, 1e-6f);
    OIIO_CHECK_EQUAL(comp.maxx, 9);
    OIIO_CHECK_EQUAL(comp.maxy, 0);
    OIIO_CHECK_EQUAL_THRESH(comp.meanerror, 0.0045f, 1.0e-8f);
}



// Tests ImageBufAlgo::isConstantColor
void
test_isConstantColor()
{
    std::cout << "test isConstantColor\n";
    const int WIDTH = 10, HEIGHT = 10, CHANNELS = 3;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ImageBuf A(spec);
    const float col[CHANNELS] = { 0.25, 0.5, 0.75 };
    ImageBufAlgo::fill(A, col);

    float thecolor[CHANNELS] = { 0, 0, 0 };
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A), true);
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A, thecolor), true);
    OIIO_CHECK_EQUAL(col[0], thecolor[0]);
    OIIO_CHECK_EQUAL(col[1], thecolor[1]);
    OIIO_CHECK_EQUAL(col[2], thecolor[2]);

    // Now introduce a difference
    const float another[CHANNELS] = { 0.25f, 0.51f, 0.75f };
    A.setpixel(2, 2, 0, another, 3);
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A), false);
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A, thecolor), false);
    // But not with lower threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A, 0.015f), true);

    // Make sure ROI works
    ROI roi(0, WIDTH, 0, 2, 0, 1, 0, CHANNELS);  // should match for this ROI
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantColor(A, 0.0f, {}, roi), true);
}



// Tests ImageBufAlgo::isConstantChannel
void
test_isConstantChannel()
{
    std::cout << "test isConstantChannel\n";
    const int WIDTH = 10, HEIGHT = 10, CHANNELS = 3;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ImageBuf A(spec);
    const float col[CHANNELS] = { 0.25f, 0.5f, 0.75f };
    ImageBufAlgo::fill(A, col);

    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantChannel(A, 1, 0.5f), true);

    // Now introduce a difference
    const float another[CHANNELS] = { 0.25f, 0.51f, 0.75f };
    A.setpixel(2, 2, 0, another, 3);
    // It should still pass if within the threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantChannel(A, 1, 0.5f, 0.015f), true);
    // But not with lower threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantChannel(A, 1, 0.5f, 0.005), false);
    // And certainly not with zero threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantChannel(A, 1, 0.5f), false);

    // Make sure ROI works
    ROI roi(0, WIDTH, 0, 2, 0, 1, 0, CHANNELS);  // should match for this ROI
    OIIO_CHECK_EQUAL(ImageBufAlgo::isConstantChannel(A, 1, 0.5f, roi = roi),
                     true);
}



// Tests ImageBufAlgo::isMonochrome
void
test_isMonochrome()
{
    std::cout << "test isMonochrome\n";
    const int WIDTH = 10, HEIGHT = 10, CHANNELS = 3;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ImageBuf A(spec);
    const float col[CHANNELS] = { 0.25f, 0.25f, 0.25f };
    ImageBufAlgo::fill(A, col);

    OIIO_CHECK_EQUAL(ImageBufAlgo::isMonochrome(A), true);

    // Now introduce a tiny difference
    const float another[CHANNELS] = { 0.25f, 0.25f, 0.26f };
    A.setpixel(2, 2, 0, another, 3);
    // It should still pass if within the threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isMonochrome(A, 0.015f), true);
    // But not with lower threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isMonochrome(A, 0.005f), false);
    // And certainly not with zero threshold
    OIIO_CHECK_EQUAL(ImageBufAlgo::isMonochrome(A), false);


    // Make sure ROI works
    ROI roi(0, WIDTH, 0, 2, 0, 1, 0, CHANNELS);  // should match for this ROI
    OIIO_CHECK_EQUAL(ImageBufAlgo::isMonochrome(A, roi), true);
}



// Tests ImageBufAlgo::computePixelStats()
void
test_computePixelStats()
{
    std::cout << "test computePixelStats\n";
    ImageBuf img(ImageSpec(2, 2, 3, TypeDesc::FLOAT));
    float black[3] = { 0, 0, 0 }, white[3] = { 1, 1, 1 };
    img.setpixel(0, 0, black);
    img.setpixel(1, 0, white);
    img.setpixel(0, 1, black);
    img.setpixel(1, 1, white);
    auto stats = ImageBufAlgo::computePixelStats(img);
    for (int c = 0; c < 3; ++c) {
        OIIO_CHECK_EQUAL(stats.min[c], 0.0f);
        OIIO_CHECK_EQUAL(stats.max[c], 1.0f);
        OIIO_CHECK_EQUAL(stats.avg[c], 0.5f);
        OIIO_CHECK_EQUAL(stats.stddev[c], 0.5f);
        OIIO_CHECK_EQUAL(stats.nancount[c], 0);
        OIIO_CHECK_EQUAL(stats.infcount[c], 0);
        OIIO_CHECK_EQUAL(stats.finitecount[c], 4);
    }
}



// Tests histogram computation.
void
histogram_computation_test()
{
    const int INPUT_WIDTH   = 64;
    const int INPUT_HEIGHT  = 64;
    const int INPUT_CHANNEL = 0;

    const int HISTOGRAM_BINS = 256;

    const int SPIKE1 = 51;   // 0.2f in range 0->1 maps to 51 in range 0->255
    const int SPIKE2 = 128;  // 0.5f in range 0->1 maps to 128 in range 0->255
    const int SPIKE3 = 204;  // 0.8f in range 0->1 maps to 204 in range 0->255

    const int SPIKE1_COUNT = INPUT_WIDTH * 8;
    const int SPIKE2_COUNT = INPUT_WIDTH * 16;
    const int SPIKE3_COUNT = INPUT_WIDTH * 40;

    // Create input image with three regions with different pixel values.
    ImageSpec spec(INPUT_WIDTH, INPUT_HEIGHT, 1, TypeDesc::FLOAT);
    ImageBuf A(spec);

    float value[] = { 0.2f };
    ImageBufAlgo::fill(A, value, ROI(0, INPUT_WIDTH, 0, 8));

    value[0] = 0.5f;
    ImageBufAlgo::fill(A, value, ROI(0, INPUT_WIDTH, 8, 24));

    value[0] = 0.8f;
    ImageBufAlgo::fill(A, value, ROI(0, INPUT_WIDTH, 24, 64));

    // Compute A's histogram.
    std::vector<imagesize_t> hist = ImageBufAlgo::histogram(A, INPUT_CHANNEL,
                                                            HISTOGRAM_BINS);

    // Does the histogram size equal the number of bins?
    OIIO_CHECK_EQUAL(hist.size(), (imagesize_t)HISTOGRAM_BINS);

    // Are the histogram values as expected?
    OIIO_CHECK_EQUAL(hist[SPIKE1], (imagesize_t)SPIKE1_COUNT);
    OIIO_CHECK_EQUAL(hist[SPIKE2], (imagesize_t)SPIKE2_COUNT);
    OIIO_CHECK_EQUAL(hist[SPIKE3], (imagesize_t)SPIKE3_COUNT);
    for (int i = 0; i < HISTOGRAM_BINS; i++)
        if (i != SPIKE1 && i != SPIKE2 && i != SPIKE3)
            OIIO_CHECK_EQUAL(hist[i], 0);
}



// Test ability to do a maketx directly from an ImageBuf
void
test_maketx_from_imagebuf()
{
    std::cout << "test make_texture from ImageBuf\n";
    // Make a checkerboard
    const int WIDTH = 16, HEIGHT = 16, CHANNELS = 3;
    ImageSpec spec(WIDTH, HEIGHT, CHANNELS, TypeDesc::FLOAT);
    ImageBuf A(spec);
    float pink[] = { 0.5f, 0.3f, 0.3f }, green[] = { 0.1f, 0.5f, 0.1f };
    ImageBufAlgo::checker(A, 4, 4, 4, pink, green);

    // Write it
    const char* pgname = "oiio-pgcheck.tx";
    remove(pgname);  // Remove it first
    ImageSpec configspec;
    ImageBufAlgo::make_texture(ImageBufAlgo::MakeTxTexture, A, pgname,
                               configspec);

    // Read it back and compare it
    ImageBuf B(pgname);
    B.read();
    auto comparison = ImageBufAlgo::compare(A, B, 0, 0);
    OIIO_CHECK_EQUAL(comparison.nwarn, 0);
    OIIO_CHECK_EQUAL(comparison.nfail, 0);
    remove(pgname);  // clean up
}



// Test various IBAprep features
void
test_IBAprep()
{
    std::cout << "test IBAprep\n";
    using namespace ImageBufAlgo;
    ImageBuf rgb(ImageSpec(256, 256, 3));   // Basic RGB uint8 image
    ImageBuf rgba(ImageSpec(256, 256, 4));  // Basic RGBA uint8 image

#define CHECK(...)                               \
    {                                            \
        ImageBuf dst;                            \
        ROI roi;                                 \
        OIIO_CHECK_ASSERT(IBAprep(__VA_ARGS__)); \
    }
#define CHECK0(...)                               \
    {                                             \
        ImageBuf dst;                             \
        ROI roi;                                  \
        OIIO_CHECK_ASSERT(!IBAprep(__VA_ARGS__)); \
    }

    // Test REQUIRE_ALPHA
    CHECK(roi, &dst, &rgba, IBAprep_REQUIRE_ALPHA);
    CHECK0(roi, &dst, &rgb, IBAprep_REQUIRE_ALPHA);

    // Test REQUIRE_Z
    ImageSpec rgbaz_spec(256, 256, 5);
    rgbaz_spec.channelnames[4] = "Z";
    rgbaz_spec.z_channel       = 4;
    ImageBuf rgbaz(rgbaz_spec);
    CHECK(roi, &dst, &rgbaz, IBAprep_REQUIRE_Z);
    CHECK0(roi, &dst, &rgb, IBAprep_REQUIRE_Z);

    // Test REQUIRE_SAME_NCHANNELS
    CHECK(roi, &dst, &rgb, &rgb, NULL, NULL, IBAprep_REQUIRE_SAME_NCHANNELS);
    CHECK0(roi, &dst, &rgb, &rgba, NULL, NULL, IBAprep_REQUIRE_SAME_NCHANNELS);

    // Test NO_SUPPOERT_VOLUME
    ImageSpec volspec(256, 256, 3);
    volspec.depth = 256;
    ImageBuf vol(volspec);
    CHECK(roi, &dst, &rgb, IBAprep_NO_SUPPORT_VOLUME);
    CHECK0(roi, &dst, &vol, IBAprep_NO_SUPPORT_VOLUME);

    // Test SUPPORT_DEEP
    ImageSpec deepspec(256, 256, 3);
    deepspec.deep = true;
    ImageBuf deep(deepspec);
    CHECK(roi, &dst, &deep, IBAprep_SUPPORT_DEEP);
    CHECK0(roi, &dst, &deep);  // deep should be rejected

    // Test DEEP_MIXED
    CHECK(roi, &dst, &deep, &deep, NULL,
          IBAprep_SUPPORT_DEEP | IBAprep_DEEP_MIXED);
    CHECK(roi, &dst, &deep, &rgb, NULL,
          IBAprep_SUPPORT_DEEP | IBAprep_DEEP_MIXED);
    CHECK(roi, &dst, &deep, &deep, NULL, IBAprep_SUPPORT_DEEP);
    CHECK0(roi, &dst, &deep, &rgb, NULL, IBAprep_SUPPORT_DEEP);

    // Test DST_FLOAT_PIXELS
    {
        ROI roi1, roi2;
        ImageBuf dst1, dst2;
        OIIO_CHECK_ASSERT(IBAprep(roi1, &dst1, &rgb));
        OIIO_CHECK_EQUAL(dst1.spec().format, TypeDesc::UINT8);
        OIIO_CHECK_ASSERT(IBAprep(roi2, &dst2, &rgb, IBAprep_DST_FLOAT_PIXELS));
        OIIO_CHECK_EQUAL(dst2.spec().format, TypeDesc::FLOAT);
    }

    // Test MINIMIZE_NCHANNELS
    {
        ROI roi1, roi2;
        ImageBuf dst1, dst2;
        OIIO_CHECK_ASSERT(IBAprep(roi1, &dst1, &rgb, &rgba));
        OIIO_CHECK_EQUAL(dst1.nchannels(), 4);
        OIIO_CHECK_ASSERT(IBAprep(roi2, &dst2, &rgb, &rgba, NULL, NULL,
                                  IBAprep_MINIMIZE_NCHANNELS));
        OIIO_CHECK_EQUAL(dst2.nchannels(), 3);
    }
#undef CHECK
}



void
benchmark_parallel_image(int res, int iters)
{
    using namespace ImageBufAlgo;
    std::cout << "\nTime old parallel_image for " << res << "x" << res
              << std::endl;

    std::cout << "  threads time    rate   (best of " << ntrials << ")\n";
    std::cout << "  ------- ------- -------\n";
    ImageSpec spec(res, res, 3, TypeDesc::FLOAT);
    ImageBuf X(spec), Y(spec);
    float one[] = { 1, 1, 1 };
    ImageBufAlgo::zero(Y);
    ImageBufAlgo::fill(X, one);
    float a = 0.5f;

    // Lambda that does some exercise (a basic SAXPY)
    auto exercise = [&](ROI roi) {
        ImageBuf::Iterator<float> y(Y, roi);
        ImageBuf::ConstIterator<float> x(X, roi);
        for (; !y.done(); ++y, ++x)
            for (int c = roi.chbegin; c < roi.chend; ++c)
                y[c] = a * x[c] + y[c];
    };

    for (int i = 0; threadcounts[i] <= numthreads; ++i) {
        int nt = wedge ? threadcounts[i] : numthreads;
        ImageBufAlgo::zero(Y);
        auto func = [&]() {
            ImageBufAlgo::parallel_image(Y.roi(), nt, exercise);
        };
        double range;
        double t = time_trial(func, ntrials, iters, &range) / iters;
        std::cout << Strutil::sprintf("  %4d   %7.3f ms  %5.1f Mpels/s\n", nt,
                                      t * 1000, double(res * res) / t / 1.0e6);
        if (!wedge)
            break;  // don't loop if we're not wedging
    }

    std::cout << "\nTime new parallel_image for " << res << "x" << res << "\n";

    std::cout << "  threads time    rate   (best of " << ntrials << ")\n";
    std::cout << "  ------- ------- -------\n";
    for (int i = 0; threadcounts[i] <= numthreads; ++i) {
        int nt = wedge ? threadcounts[i] : numthreads;
        // default_thread_pool()->resize (nt);
        zero(Y);
        auto func = [&]() { parallel_image(Y.roi(), nt, exercise); };
        double range;
        double t = time_trial(func, ntrials, iters, &range) / iters;
        std::cout << Strutil::sprintf("  %4d   %6.2f ms  %5.1f Mpels/s\n", nt,
                                      t * 1000, double(res * res) / t / 1.0e6);
        if (!wedge)
            break;  // don't loop if we're not wedging
    }
}



void
test_opencv()
{
#if USE_OPENCV
    std::cout << "Testing OpenCV round trip\n";
    // Make a gradient RGB image, convert to OpenCV cv::Mat, then convert
    // that back to ImageBuf, make sure the round trip has the same pixels
    // as the original image.
    ImageBuf src
        = ImageBufAlgo::fill({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
                             { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f },
                             ROI(0, 64, 0, 64, 0, 1, 0, 3));
    cv::Mat mat;
    ImageBufAlgo::to_OpenCV(mat, src);
    OIIO_CHECK_ASSERT(!mat.empty());
    ImageBuf dst = ImageBufAlgo::from_OpenCV(mat);
    OIIO_CHECK_ASSERT(!dst.has_error());
    auto comp = ImageBufAlgo::compare(src, dst, 0.0f, 0.0f);
    OIIO_CHECK_EQUAL(comp.error, false);
    OIIO_CHECK_EQUAL(comp.maxerror, 0.0f);
#endif
}



int
main(int argc, char** argv)
{
#if !defined(NDEBUG) || defined(OIIO_CI) || defined(OIIO_CODE_COVERAGE)
    // For the sake of test time, reduce the default iterations for DEBUG,
    // CI, and code coverage builds. Explicit use of --iters or --trials
    // will override this, since it comes before the getargs() call.
    iterations /= 10;
    ntrials = 1;
#endif

    getargs(argc, argv);

    test_type_merge();
    test_zero_fill();
    test_copy();
    test_crop();
    test_paste();
    test_channel_append();
    test_add();
    test_sub();
    test_mul();
    test_mad();
    test_over();
    test_compare();
    test_isConstantColor();
    test_isConstantChannel();
    test_isMonochrome();
    test_computePixelStats();
    histogram_computation_test();
    test_maketx_from_imagebuf();
    test_IBAprep();
    test_opencv();

    benchmark_parallel_image(64, iterations * 64);
    benchmark_parallel_image(512, iterations * 16);
    benchmark_parallel_image(1024, iterations * 4);
    benchmark_parallel_image(2048, iterations);

    return unit_test_failures;
}
