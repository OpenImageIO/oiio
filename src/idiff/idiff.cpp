// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <iterator>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/sysutil.h>


using namespace OIIO;


enum idiffErrors {
    ErrOK = 0,         ///< No errors, the images match exactly
    ErrWarn,           ///< Warning: the errors differ a little
    ErrFail,           ///< Failure: the errors differ a lot
    ErrDifferentSize,  ///< Images aren't even the same size
    ErrFile,           ///< Could not find or open input files, etc.
    ErrLast
};



static ArgParse
getargs(int argc, char* argv[])
{
    // clang-format off
    ArgParse ap;
    ap.intro("idiff -- compare two images\n"
             OIIO_INTRO_STRING)
      .usage("idiff [options] image1 image2")
      .print_defaults(true);

    ap.arg("filename")
      .hidden()
      .action(ArgParse::append());
    ap.arg("-v")
      .help("Verbose status messages");
    ap.arg("-q")
      .help("Quiet (minimal messages)");
    ap.arg("-a")
      .help("Compare all subimages/miplevels");

    ap.separator("Thresholding and comparison options");
    ap.arg("-fail")
      .help("Failure threshold difference")
      .metavar("VAL")
      .defaultval(1.0e-6f);
    ap.arg("-failpercent")
      .help("Allow this percentage of failures")
      .metavar("PERCENT")
      .defaultval(0.0f);
    ap.arg("-hardfail")
      .help("Fail if any one pixel exceeds this error")
      .metavar("VAL")
      .defaultval(std::numeric_limits<float>::infinity());
    ap.arg("-warn")
      .help("Warning threshold difference")
      .metavar("VAL")
      .defaultval(1.0e-6f);
    ap.arg("-warnpercent")
      .help("Allow this percentage of warnings")
      .metavar("PERCENT")
      .defaultval(0.0f);
    ap.arg("-hardwarn")
      .help("Warn if any one pixel exceeds this error")
      .metavar("VAL")
      .defaultval(std::numeric_limits<float>::infinity());
    ap.arg("-p")
      .help("Perform perceptual (rather than numeric) comparison");

    ap.separator("Difference image options");
    ap.arg("-o")
      .help("Output difference image")
      .metavar("FILENAME");
    ap.arg("-od")
      .help("Output image only if nonzero difference");
    ap.arg("-abs")
      .help("Output image of absolute value, not signed difference");
    ap.arg("-scale")
      .help("Scale the output image by this factor")
      .defaultval(1.0f)
      .metavar("FACTOR");

    ap.parse(argc, (const char**)argv);

    return ap;
    // clang-format on
}



static bool
read_input(const std::string& filename, ImageBuf& img, ImageCache* cache,
           int subimage = 0, int miplevel = 0)
{
    if (img.subimage() >= 0 && img.subimage() == subimage
        && img.miplevel() == miplevel)
        return true;

    img.reset(filename, cache);
    if (img.read(subimage, miplevel, false, TypeFloat))
        return true;

    std::cerr << "idiff ERROR: Could not read " << filename << ":\n\t"
              << img.geterror() << "\n";
    return false;
}



// function that standardize printing NaN and Inf values on
// Windows (where they are in 1.#INF, 1.#NAN format) and all
// others platform
inline void
safe_double_print(double val)
{
    if (OIIO::isnan(val))
        std::cout << "nan";
    else if (OIIO::isinf(val))
        std::cout << "inf";
    else
        std::cout << val;
    std::cout << '\n';
}



inline void
print_subimage(ImageBuf& img0, int subimage, int miplevel)
{
    if (img0.nsubimages() > 1)
        std::cout << "Subimage " << subimage << ' ';
    if (img0.nmiplevels() > 1)
        std::cout << " MIP level " << miplevel << ' ';
    if (img0.nsubimages() > 1 || img0.nmiplevels() > 1)
        std::cout << ": ";
    std::cout << img0.spec().width << " x " << img0.spec().height;
    if (img0.spec().depth > 1)
        std::cout << " x " << img0.spec().depth;
    std::cout << ", " << img0.spec().nchannels << " channel\n";
}



int
main(int argc, char* argv[])
{
    // Helpful for debugging to make sure that any crashes dump a stack
    // trace.
    Sysutil::setup_crash_stacktrace("stdout");

    Filesystem::convert_native_arguments(argc, (const char**)argv);
    ArgParse ap = getargs(argc, argv);

    std::vector<std::string> filenames = ap["filename"].as_vec<std::string>();
    if (filenames.size() != 2) {
        std::cerr << "idiff: Must have two input filenames.\n";
        std::cout << "> " << Strutil::join(filenames, ", ") << "\n";
        ap.usage();
        return EXIT_FAILURE;
    }
    bool verbose          = ap["v"].get<int>();
    bool quiet            = ap["q"].get<int>();
    bool compareall       = ap["a"].get<int>();
    bool outdiffonly      = ap["od"].get<int>();
    bool diffabs          = ap["abs"].get<int>();
    bool perceptual       = ap["p"].get<int>();
    std::string diffimage = ap["o"].get();
    float diffscale       = ap["scale"].get<float>();
    float failthresh      = ap["fail"].get<float>();
    float failpercent     = ap["failpercent"].get<float>();
    float hardfail        = ap["hardfail"].get<float>();
    float warnthresh      = ap["warn"].get<float>();
    float warnpercent     = ap["warnpercent"].get<float>();
    float hardwarn        = ap["hardwarn"].get<float>();

    if (!quiet)
        std::cout << "Comparing \"" << filenames[0] << "\" and \""
                  << filenames[1] << "\"\n";

    // Create a private ImageCache so we can customize its cache size
    // and instruct it store everything internally as floats.
    ImageCache* imagecache = ImageCache::create(true);
    imagecache->attribute("forcefloat", 1);
    if (sizeof(void*) == 4)  // 32 bit or 64?
        imagecache->attribute("max_memory_MB", 512.0);
    else
        imagecache->attribute("max_memory_MB", 2048.0);
    imagecache->attribute("autotile", 256);
    // force a full diff, even for files tagged with the same
    // fingerprint, just in case some mistake has been made.
    imagecache->attribute("deduplicate", 0);

    ImageBuf img0, img1;
    if (!read_input(filenames[0], img0, imagecache)
        || !read_input(filenames[1], img1, imagecache))
        return ErrFile;
    //    ImageSpec spec0 = img0.spec();  // stash it

    int ret = ErrOK;
    for (int subimage = 0; subimage < img0.nsubimages(); ++subimage) {
        if (subimage > 0 && !compareall)
            break;
        if (subimage >= img1.nsubimages())
            break;

        if (!read_input(filenames[0], img0, imagecache, subimage)
            || !read_input(filenames[1], img1, imagecache, subimage)) {
            std::cerr << "Failed to read subimage " << subimage << "\n";
            return ErrFile;
        }

        if (img0.nmiplevels() != img1.nmiplevels()) {
            if (!quiet)
                std::cout
                    << "Files do not match in their number of MIPmap levels\n";
        }

        for (int m = 0; m < img0.nmiplevels(); ++m) {
            if (m > 0 && !compareall)
                break;
            if (m > 0 && img0.nmiplevels() != img1.nmiplevels()) {
                std::cerr
                    << "Files do not match in their number of MIPmap levels\n";
                ret = ErrDifferentSize;
                break;
            }

            if (!read_input(filenames[0], img0, imagecache, subimage, m)
                || !read_input(filenames[1], img1, imagecache, subimage, m))
                return ErrFile;

            if (img0.deep() != img1.deep()) {
                std::cerr
                    << "One image contains deep data, the other does not\n";
                ret = ErrDifferentSize;
                break;
            }

            int npels = img0.spec().width * img0.spec().height
                        * img0.spec().depth;
            if (npels == 0)
                npels = 1;  // Avoid divide by zero for 0x0 images
            OIIO_ASSERT(img0.spec().format == TypeFloat);

            // Compare the two images.
            //
            auto cr = ImageBufAlgo::compare(img0, img1, failthresh, warnthresh);

            int yee_failures = 0;
            if (perceptual && !img0.deep()) {
                ImageBufAlgo::CompareResults cr;
                yee_failures = ImageBufAlgo::compare_Yee(img0, img1, cr);
            }

            if (cr.nfail > (failpercent / 100.0 * npels)
                || cr.maxerror > hardfail
                || yee_failures > (failpercent / 100.0 * npels)) {
                ret = ErrFail;
            } else if (cr.nwarn > (warnpercent / 100.0 * npels)
                       || cr.maxerror > hardwarn) {
                if (ret != ErrFail)
                    ret = ErrWarn;
            }

            // Print the report
            //
            if (verbose || (ret != ErrOK && !quiet)) {
                if (compareall)
                    print_subimage(img0, subimage, m);
                std::cout << "  Mean error = ";
                safe_double_print(cr.meanerror);
                std::cout << "  RMS error = ";
                safe_double_print(cr.rms_error);
                std::cout << "  Peak SNR = ";
                safe_double_print(cr.PSNR);
                std::cout << "  Max error  = " << cr.maxerror;
                if (cr.maxerror != 0) {
                    std::cout << " @ (" << cr.maxx << ", " << cr.maxy;
                    if (img0.spec().depth > 1)
                        std::cout << ", " << cr.maxz;
                    if (cr.maxc < (int)img0.spec().channelnames.size())
                        std::cout << ", " << img0.spec().channelnames[cr.maxc]
                                  << ')';
                    else if (cr.maxc < (int)img1.spec().channelnames.size())
                        std::cout << ", " << img1.spec().channelnames[cr.maxc]
                                  << ')';
                    else
                        std::cout << ", channel " << cr.maxc << ')';
                    if (!img0.deep()) {
                        std::cout << "  values are ";
                        for (int c = 0; c < img0.spec().nchannels; ++c)
                            std::cout
                                << (c ? ", " : "")
                                << img0.getchannel(cr.maxx, cr.maxy, 0, c);
                        std::cout << " vs ";
                        for (int c = 0; c < img1.spec().nchannels; ++c)
                            std::cout
                                << (c ? ", " : "")
                                << img1.getchannel(cr.maxx, cr.maxy, 0, c);
                    }
                }
                std::cout << "\n";
#if OIIO_MSVS_BEFORE_2015
                // When older Visual Studio is used, float values in
                // scientific foramt are printed with three digit exponent.
                // We change this behaviour to fit Linux way.
                _set_output_format(_TWO_DIGIT_EXPONENT);
#endif
                std::streamsize precis = std::cout.precision();
                std::cout << "  " << cr.nwarn << " pixels ("
                          << std::setprecision(3) << (100.0 * cr.nwarn / npels)
                          << std::setprecision(precis) << "%) over "
                          << warnthresh << "\n";
                std::cout << "  " << cr.nfail << " pixels ("
                          << std::setprecision(3) << (100.0 * cr.nfail / npels)
                          << std::setprecision(precis) << "%) over "
                          << failthresh << "\n";
                if (perceptual)
                    std::cout << "  " << yee_failures << " pixels ("
                              << std::setprecision(3)
                              << (100.0 * yee_failures / npels)
                              << std::setprecision(precis)
                              << "%) failed the perceptual test\n";
            }

            // If the user requested that a difference image be output,
            // do that.  N.B. we only do this for the first subimage
            // right now, because ImageBuf doesn't really know how to
            // write subimages.
            if (diffimage.size() && (cr.maxerror != 0 || !outdiffonly)) {
                ImageBuf diff;
                if (diffabs)
                    ImageBufAlgo::absdiff(diff, img0, img1);
                else
                    ImageBufAlgo::sub(diff, img0, img1);
                if (diffscale != 1.0f)
                    ImageBufAlgo::mul(diff, diff, diffscale);
                diff.write(diffimage);

                // Clear diff image name so we only save the first
                // non-matching subimage.
                diffimage = "";
            }
        }
    }

    if (compareall && img0.nsubimages() != img1.nsubimages()) {
        if (!quiet)
            std::cerr << "Images had differing numbers of subimages ("
                      << img0.nsubimages() << " vs " << img1.nsubimages()
                      << ")\n";
        ret = ErrFail;
    }
    if (!compareall && (img0.nsubimages() > 1 || img1.nsubimages() > 1)) {
        if (!quiet)
            std::cout << "Only compared the first subimage (of "
                      << img0.nsubimages() << " and " << img1.nsubimages()
                      << ", respectively)\n";
    }

    if (ret == ErrOK) {
        if (!quiet)
            std::cout << "PASS\n";
    } else if (ret == ErrWarn) {
        if (!quiet)
            std::cout << "WARNING\n";
    } else if (ret) {
        if (quiet)
            std::cerr << "FAILURE\n";
        else
            std::cout << "FAILURE\n";
    }

    imagecache->invalidate_all(true);
    ImageCache::destroy(imagecache);
    return ret;
}
