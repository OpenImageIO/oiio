// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <OpenImageIO/argparse.h>
#include <OpenImageIO/benchmark.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/unittest.h>
#include <OpenImageIO/ustring.h>

#include <functional>
#include <iostream>
#include <vector>

using namespace OIIO;

static bool verbose      = false;
static int iterations    = 1;
static int ntrials       = 1;
static int numthreads    = 0;
static int autotile_size = 64;
static bool iter_only    = false;
static bool no_iter      = false;
static std::string conversionname;
static TypeDesc conversion = TypeDesc::UNKNOWN;  // native by default
static std::vector<ustring> input_filename;
static std::string output_filename;
static std::string output_format;
static std::vector<char> buffer;
static ImageSpec bufspec, outspec;
static ImageCache* imagecache         = NULL;
static imagesize_t total_image_pixels = 0;
static float cache_size               = 0;



static void
getargs(int argc, char* argv[])
{
    ArgParse ap;
    // clang-format off
    ap.intro("imagespeed_test\n" OIIO_INTRO_STRING)
      .usage("imagespeed_test [options]");

    ap.arg("filename")
      .hidden()
      .action([&](cspan<const char*> argv){ input_filename.emplace_back(argv[0]); });
    ap.arg("-v", &verbose)
      .help("Verbose mode");
    ap.arg("--threads %d", &numthreads)
      .help(Strutil::sprintf("Number of threads (default: %d)", numthreads));
    ap.arg("--iters %d", &iterations)
      .help(Strutil::sprintf("Number of iterations (default: %d)", iterations));
    ap.arg("--trials %d", &ntrials)
      .help("Number of trials");
    ap.arg("--autotile %d", &autotile_size)
      .help(Strutil::sprintf("Autotile size (when used; default: %d)", autotile_size));
    ap.arg("--iteronly", &iter_only)
      .help("Run ImageBuf iteration tests only (not read tests)");
    ap.arg("--noiter", &no_iter)
      .help("Don't run ImageBuf iteration tests");
    ap.arg("--convert %s", &conversionname)
      .help("Convert to named type upon read (default: native)");
    ap.arg("--cache %f", &cache_size)
      .help("Specify ImageCache size, in MB");
    ap.arg("-o %s", &output_filename)
      .help("Test output by writing to this file");
    ap.arg("-od %s", &output_format)
      .help("Requested output format");
    // clang-format on

    ap.parse(argc, (const char**)argv);
}



static void
time_read_image()
{
    for (ustring filename : input_filename) {
        auto in = ImageInput::open(filename.c_str());
        OIIO_ASSERT(in);
        in->read_image(0, 0, 0, in->spec().nchannels, conversion, &buffer[0]);
        in->close();
    }
}



static void
time_read_scanline_at_a_time()
{
    for (ustring filename : input_filename) {
        auto in = ImageInput::open(filename.c_str());
        OIIO_ASSERT(in);
        const ImageSpec& spec(in->spec());
        size_t pixelsize = spec.nchannels * conversion.size();
        if (!pixelsize)
            pixelsize = spec.pixel_bytes(true);  // UNKNOWN -> native
        imagesize_t scanlinesize = spec.width * pixelsize;
        for (int y = 0; y < spec.height; ++y) {
            in->read_scanline(y + spec.y, 0, conversion,
                              &buffer[scanlinesize * y]);
        }
        in->close();
    }
}



static void
time_read_64_scanlines_at_a_time()
{
    for (ustring filename : input_filename) {
        auto in = ImageInput::open(filename.c_str());
        OIIO_ASSERT(in);
        const ImageSpec& spec(in->spec());
        size_t pixelsize = spec.nchannels * conversion.size();
        if (!pixelsize)
            pixelsize = spec.pixel_bytes(true);  // UNKNOWN -> native
        imagesize_t scanlinesize = spec.width * pixelsize;
        for (int y = 0; y < spec.height; y += 64) {
            in->read_scanlines(0, 0, y + spec.y,
                               std::min(y + spec.y + 64, spec.y + spec.height),
                               0, conversion, &buffer[scanlinesize * y]);
        }
        in->close();
    }
}



static void
time_read_imagebuf()
{
    imagecache->invalidate_all(true);
    for (ustring filename : input_filename) {
        ImageBuf ib(filename.string(), imagecache);
        ib.read(0, 0, true, conversion);
    }
}



static void
time_ic_get_pixels()
{
    imagecache->invalidate_all(true);
    for (ustring filename : input_filename) {
        const ImageSpec spec = (*imagecache->imagespec(filename));
        imagecache->get_pixels(filename, 0, 0, spec.x, spec.x + spec.width,
                               spec.y, spec.y + spec.height, spec.z,
                               spec.z + spec.depth, conversion, &buffer[0]);
    }
}



static void
test_read(const std::string& explanation, void (*func)(), int autotile = 64,
          int autoscanline = 1)
{
    imagecache->invalidate_all(true);  // Don't hold anything
    imagecache->attribute("autotile", autotile);
    imagecache->attribute("autoscanline", autoscanline);
    double t    = time_trial(func, ntrials);
    double rate = double(total_image_pixels) / t;
    std::cout << "  " << explanation << ": "
              << Strutil::timeintervalformat(t, 2) << " = "
              << Strutil::sprintf("%5.1f", rate / 1.0e6) << " Mpel/s"
              << std::endl;
}



static void
time_write_image()
{
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);
    out->write_image(bufspec.format, &buffer[0]);
}



static void
time_write_scanline_at_a_time()
{
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);

    size_t pixelsize         = outspec.nchannels * sizeof(float);
    imagesize_t scanlinesize = outspec.width * pixelsize;
    for (int y = 0; y < outspec.height; ++y) {
        out->write_scanline(y + outspec.y, outspec.z, bufspec.format,
                            &buffer[scanlinesize * y]);
    }
}



static void
time_write_64_scanlines_at_a_time()
{
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);

    size_t pixelsize         = outspec.nchannels * sizeof(float);
    imagesize_t scanlinesize = outspec.width * pixelsize;
    for (int y = 0; y < outspec.height; y += 64) {
        out->write_scanlines(y + outspec.y,
                             std::min(y + outspec.y + 64,
                                      outspec.y + outspec.height),
                             outspec.z, bufspec.format,
                             &buffer[scanlinesize * y]);
    }
}



static void
time_write_tile_at_a_time()
{
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);

    size_t pixelsize         = outspec.nchannels * sizeof(float);
    imagesize_t scanlinesize = outspec.width * pixelsize;
    imagesize_t planesize    = outspec.height * scanlinesize;
    for (int z = 0; z < outspec.depth; z += outspec.tile_depth) {
        for (int y = 0; y < outspec.height; y += outspec.tile_height) {
            for (int x = 0; x < outspec.width; x += outspec.tile_width) {
                out->write_tile(x + outspec.x, y + outspec.y, z + outspec.z,
                                bufspec.format,
                                &buffer[scanlinesize * y + pixelsize * x],
                                pixelsize, scanlinesize, planesize);
            }
        }
    }
}



static void
time_write_tiles_row_at_a_time()
{
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);

    size_t pixelsize         = outspec.nchannels * sizeof(float);
    imagesize_t scanlinesize = outspec.width * pixelsize;
    for (int z = 0; z < outspec.depth; z += outspec.tile_depth) {
        for (int y = 0; y < outspec.height; y += outspec.tile_height) {
            out->write_tiles(outspec.x, outspec.x + outspec.width,
                             y + outspec.y, y + outspec.y + outspec.tile_height,
                             z + outspec.z, z + outspec.z + outspec.tile_depth,
                             bufspec.format, &buffer[scanlinesize * y],
                             pixelsize /*xstride*/, scanlinesize /*ystride*/);
        }
    }
}



static void
time_write_imagebuf()
{
    ImageBuf ib(output_filename, bufspec, &buffer[0]);  // wrap the buffer
    auto out = ImageOutput::create(output_filename);
    OIIO_ASSERT(out);
    bool ok = out->open(output_filename, outspec);
    OIIO_ASSERT(ok);
    ib.write(out.get());
}


static void
test_write(const std::string& explanation, void (*func)(), int tilesize = 0)
{
    outspec.tile_width  = tilesize;
    outspec.tile_height = tilesize;
    outspec.tile_depth  = 1;
    double t            = time_trial(func, ntrials);
    double rate         = double(total_image_pixels) / t;
    std::cout << "  " << explanation << ": "
              << Strutil::timeintervalformat(t, 2) << " = "
              << Strutil::sprintf("%5.1f", rate / 1.0e6) << " Mpel/s"
              << std::endl;
}



static float
time_loop_pixels_1D(ImageBuf& ib, int iters)
{
    OIIO_ASSERT(ib.localpixels() && ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    int nchannels       = spec.nchannels;
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        const float* f = (const float*)ib.pixeladdr(spec.x, spec.y, spec.z);
        OIIO_DASSERT(f);
        for (imagesize_t p = 0; p < npixels; ++p) {
            sum += f[0];
            f += nchannels;
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static float
time_loop_pixels_3D(ImageBuf& ib, int iters)
{
    OIIO_ASSERT(ib.localpixels() && ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    int nchannels       = spec.nchannels;
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        const float* f = (const float*)ib.pixeladdr(spec.x, spec.y, spec.z);
        OIIO_DASSERT(f);
        for (int z = spec.z, ze = spec.z + spec.depth; z < ze; ++z) {
            for (int y = spec.y, ye = spec.y + spec.height; y < ye; ++y) {
                for (int x = spec.x, xe = spec.x + spec.width; x < xe; ++x) {
                    sum += f[0];
                    f += nchannels;
                }
            }
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static float
time_loop_pixels_3D_getchannel(ImageBuf& ib, int iters)
{
    OIIO_DASSERT(ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        for (int z = spec.z, ze = spec.z + spec.depth; z < ze; ++z) {
            for (int y = spec.y, ye = spec.y + spec.height; y < ye; ++y) {
                for (int x = spec.x, xe = spec.x + spec.width; x < xe; ++x) {
                    sum += ib.getchannel(x, y, 0, 0);
                }
            }
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static float
time_iterate_pixels(ImageBuf& ib, int iters)
{
    OIIO_DASSERT(ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        for (ImageBuf::ConstIterator<float, float> p(ib); !p.done(); ++p) {
            sum += p[0];
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static float
time_iterate_pixels_slave_pos(ImageBuf& ib, int iters)
{
    OIIO_DASSERT(ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        ImageBuf::ConstIterator<float, float> slave(ib);
        for (ImageBuf::ConstIterator<float, float> p(ib); !p.done(); ++p) {
            slave.pos(p.x(), p.y());
            sum += p[0];
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static float
time_iterate_pixels_slave_incr(ImageBuf& ib, int iters)
{
    OIIO_DASSERT(ib.pixeltype() == TypeFloat);
    const ImageSpec& spec(ib.spec());
    imagesize_t npixels = spec.image_pixels();
    double sum          = 0.0f;
    for (int i = 0; i < iters; ++i) {
        ImageBuf::ConstIterator<float, float> slave(ib);
        for (ImageBuf::ConstIterator<float, float> p(ib); !p.done(); ++p) {
            sum += p[0];
            ++slave;
        }
    }
    // std::cout << float(sum/npixels/iters) << "\n";
    return float(sum / npixels / iters);
}



static void
test_pixel_iteration(const std::string& explanation,
                     float (*func)(ImageBuf&, int), bool preload,
                     int iters = 100, int autotile = 64)
{
    imagecache->invalidate_all(true);  // Don't hold anything
    // Force the whole image to be read at once
    imagecache->attribute("autotile", autotile);
    imagecache->attribute("autoscanline", 1);
    ImageBuf ib(input_filename[0].string(), imagecache);
    ib.read(0, 0, preload, TypeFloat);
    double t    = time_trial(std::bind(func, std::ref(ib), iters), ntrials);
    double rate = double(ib.spec().image_pixels()) / (t / iters);
    std::cout << "  " << explanation << ": "
              << Strutil::timeintervalformat(t / iters, 3) << " = "
              << Strutil::sprintf("%5.1f", rate / 1.0e6) << " Mpel/s"
              << std::endl;
}



static void
set_dataformat(const std::string& output_format, ImageSpec& outspec)
{
    if (output_format == "uint8")
        outspec.format = TypeDesc::UINT8;
    else if (output_format == "int8")
        outspec.format = TypeDesc::INT8;
    else if (output_format == "uint16")
        outspec.format = TypeDesc::UINT16;
    else if (output_format == "int16")
        outspec.format = TypeDesc::INT16;
    else if (output_format == "half")
        outspec.format = TypeDesc::HALF;
    else if (output_format == "float")
        outspec.format = TypeDesc::FLOAT;
    else if (output_format == "double")
        outspec.format = TypeDesc::DOUBLE;
    // Otherwise leave at the default
}



int
main(int argc, char** argv)
{
    getargs(argc, argv);
    if (input_filename.size() == 0) {
        std::cout << "Error: Must supply a filename.\n";
        return -1;
    }

    OIIO::attribute("threads", numthreads);
    OIIO::attribute("exr_threads", numthreads);
    conversion.fromstring(conversionname);

    imagecache = ImageCache::create();
    if (cache_size)
        imagecache->attribute("max_memory_MB", cache_size);
    imagecache->attribute("forcefloat", 1);

    // Allocate a buffer big enough (for floats)
    bool all_scanline       = true;
    total_image_pixels      = 0;
    imagesize_t maxpelchans = 0;
    for (auto&& fn : input_filename) {
        ImageSpec spec;
        if (!imagecache->get_imagespec(fn, spec, 0, 0, true)) {
            std::cout << "File \"" << fn << "\" could not be opened.\n";
            return -1;
        }
        total_image_pixels += spec.image_pixels();
        maxpelchans = std::max(maxpelchans,
                               spec.image_pixels() * spec.nchannels);
        all_scanline &= (spec.tile_width == 0);
    }
    imagecache->invalidate_all(true);  // Don't hold anything

    if (!iter_only) {
        std::cout << "Timing various ways of reading images:\n";
        if (conversion == TypeDesc::UNKNOWN)
            std::cout
                << "    ImageInput reads will keep data in native format.\n";
        else
            std::cout << "    ImageInput reads will convert data to "
                      << conversion << "\n";
        buffer.resize(maxpelchans * sizeof(float), 0);
        test_read("read_image                                   ",
                  time_read_image, 0, 0);
        if (all_scanline) {
            test_read("read_scanline (1 at a time)                  ",
                      time_read_scanline_at_a_time, 0, 0);
            test_read("read_scanlines (64 at a time)                ",
                      time_read_64_scanlines_at_a_time, 0, 0);
        }
        test_read("ImageBuf read                                ",
                  time_read_imagebuf, 0, 0);
        test_read("ImageCache get_pixels                        ",
                  time_ic_get_pixels, 0, 0);
        test_read("ImageBuf read (autotile)                     ",
                  time_read_imagebuf, autotile_size, 0);
        test_read("ImageCache get_pixels (autotile)             ",
                  time_ic_get_pixels, autotile_size, 0);
        if (all_scanline) {  // don't bother for tiled images
            test_read("ImageBuf read (autotile+autoscanline)        ",
                      time_read_imagebuf, autotile_size, 1);
            test_read("ImageCache get_pixels (autotile+autoscanline)",
                      time_ic_get_pixels, autotile_size, 1);
        }
        if (verbose)
            std::cout << "\n" << imagecache->getstats(2) << "\n";
        std::cout << std::endl;
    }

    if (output_filename.size()) {
        // Use the first image
        auto in = ImageInput::open(input_filename[0].c_str());
        OIIO_ASSERT(in);
        bufspec = in->spec();
        in->read_image(conversion, &buffer[0]);
        in->close();
        in.reset();
        std::cout << "Timing ways of writing images:\n";
        // imagecache->get_imagespec (input_filename[0], bufspec, 0, 0, true);
        auto out = ImageOutput::create(output_filename);
        OIIO_ASSERT(out);
        bool supports_tiles = out->supports("tiles");
        out.reset();
        outspec = bufspec;
        set_dataformat(output_format, outspec);
        std::cout << "    writing as format " << outspec.format << "\n";

        test_write("write_image (scanline)                       ",
                   time_write_image, 0);
        if (supports_tiles)
            test_write("write_image (tiled)                          ",
                       time_write_image, 64);
        test_write("write_scanline (one at a time)               ",
                   time_write_scanline_at_a_time, 0);
        test_write("write_scanlines (64 at a time)               ",
                   time_write_64_scanlines_at_a_time, 0);
        if (supports_tiles) {
            test_write("write_tile (one at a time)                   ",
                       time_write_tile_at_a_time, 64);
            test_write("write_tiles (a whole row at a time)          ",
                       time_write_tiles_row_at_a_time, 64);
        }
        test_write("ImageBuf::write (scanline)                   ",
                   time_write_imagebuf, 0);
        if (supports_tiles)
            test_write("ImageBuf::write (tiled)                      ",
                       time_write_imagebuf, 64);
        std::cout << std::endl;
    }

    if (!no_iter) {
        const int iters = 64;
        std::cout << "Timing ways of iterating over an image:\n";
        test_pixel_iteration("Loop pointers on loaded image (\"1D\")    ",
                             time_loop_pixels_1D, true, iters);
        test_pixel_iteration("Loop pointers on loaded image (\"3D\")    ",
                             time_loop_pixels_3D, true, iters);
        test_pixel_iteration("Loop + getchannel on loaded image (\"3D\")",
                             time_loop_pixels_3D_getchannel, true, iters / 32);
        test_pixel_iteration("Loop + getchannel on cached image (\"3D\")",
                             time_loop_pixels_3D_getchannel, false, iters / 32);
        test_pixel_iteration("Iterate over a loaded image             ",
                             time_iterate_pixels, true, iters);
        test_pixel_iteration("Iterate over a cache image              ",
                             time_iterate_pixels, false, iters);
        test_pixel_iteration("Iterate over a loaded image (pos slave) ",
                             time_iterate_pixels_slave_pos, true, iters);
        test_pixel_iteration("Iterate over a cache image (pos slave)  ",
                             time_iterate_pixels_slave_pos, false, iters);
        test_pixel_iteration("Iterate over a loaded image (incr slave)",
                             time_iterate_pixels_slave_incr, true, iters);
        test_pixel_iteration("Iterate over a cache image (incr slave) ",
                             time_iterate_pixels_slave_incr, false, iters);
    }
    if (verbose)
        std::cout << "\n" << imagecache->getstats(2) << "\n";

    ImageCache::destroy(imagecache);
    return unit_test_failures;
}
