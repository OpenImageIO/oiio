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
#include <string>
#include <vector>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/filter.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>

#include "oiiotool.h"

using namespace OIIO;
using namespace OiioTool;
using namespace ImageBufAlgo;



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
print_subimage(ImageRec& img0, int subimage, int miplevel)
{
    if (img0.subimages() > 1)
        std::cout << "Subimage " << subimage << ' ';
    if (img0.miplevels(subimage) > 1)
        std::cout << " MIP level " << miplevel << ' ';
    if (img0.subimages() > 1 || img0.miplevels(subimage) > 1)
        std::cout << ": ";
    const ImageSpec& spec(*img0.spec(subimage));
    std::cout << spec.width << " x " << spec.height;
    if (spec.depth > 1)
        std::cout << " x " << spec.depth;
    std::cout << ", " << spec.nchannels << " channel\n";
}



int
OiioTool::do_action_diff(ImageRec& ir0, ImageRec& ir1, Oiiotool& ot,
                         int perceptual)
{
    std::cout << "Computing " << (perceptual ? "perceptual " : "")
              << "diff of \"" << ir0.name() << "\" vs \"" << ir1.name()
              << "\"\n";
    ir0.read();
    ir1.read();

    int ret = DiffErrOK;
    for (int subimage = 0; subimage < ir0.subimages(); ++subimage) {
        if (subimage > 0 && !ot.allsubimages)
            break;
        if (subimage >= ir1.subimages())
            break;

        for (int m = 0; m < ir0.miplevels(subimage); ++m) {
            if (m > 0 && !ot.allsubimages)
                break;
            if (m > 0 && ir0.miplevels(subimage) != ir1.miplevels(subimage)) {
                std::cout
                    << "Files do not match in their number of MIPmap levels\n";
                ret = DiffErrDifferentSize;
                break;
            }

            ImageBuf& img0(ir0(subimage, m));
            ImageBuf& img1(ir1(subimage, m));
            int npels = img0.spec().width * img0.spec().height
                        * img0.spec().depth;
            if (npels == 0)
                npels = 1;  // Avoid divide by zero for 0x0 images
            OIIO_ASSERT(img0.spec().format == TypeDesc::FLOAT);

            // Compare the two images.
            //
            ImageBufAlgo::CompareResults cr;
            int yee_failures = 0;
            switch (perceptual) {
            case 1:
                yee_failures = ImageBufAlgo::compare_Yee(img0, img1, cr);
                break;
            default:
                cr = ImageBufAlgo::compare(img0, img1, ot.diff_failthresh,
                                           ot.diff_warnthresh);
                break;
            }

            if (cr.nfail > (ot.diff_failpercent / 100.0 * npels)
                || cr.maxerror > ot.diff_hardfail
                || yee_failures > (ot.diff_failpercent / 100.0 * npels)) {
                ret = DiffErrFail;
            } else if (cr.nwarn > (ot.diff_warnpercent / 100.0 * npels)
                       || cr.maxerror > ot.diff_hardwarn) {
                if (ret != DiffErrFail)
                    ret = DiffErrWarn;
            }

            // Print the report
            //
            if (ot.verbose || ot.debug || ret != DiffErrOK) {
                if (ot.allsubimages)
                    print_subimage(ir0, subimage, m);
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

                std::streamsize precis = std::cout.precision();
                std::cout << "  " << cr.nwarn << " pixels ("
                          << std::setprecision(3) << (100.0 * cr.nwarn / npels)
                          << std::setprecision(precis) << "%) over "
                          << ot.diff_warnthresh << "\n";
                std::cout << "  " << cr.nfail << " pixels ("
                          << std::setprecision(3) << (100.0 * cr.nfail / npels)
                          << std::setprecision(precis) << "%) over "
                          << ot.diff_failthresh << "\n";
                if (perceptual == 1)
                    std::cout << "  " << yee_failures << " pixels ("
                              << std::setprecision(3)
                              << (100.0 * yee_failures / npels)
                              << std::setprecision(precis)
                              << "%) failed the perceptual test\n";
            }
        }
    }

    if (ot.allsubimages && ir0.subimages() != ir1.subimages()) {
        std::cout << "Images had differing numbers of subimages ("
                  << ir0.subimages() << " vs " << ir1.subimages() << ")\n";
        ret = DiffErrFail;
    }
    if (!ot.allsubimages && (ir0.subimages() > 1 || ir1.subimages() > 1)) {
        std::cout << "Only compared the first subimage (of " << ir0.subimages()
                  << " and " << ir1.subimages() << ", respectively)\n";
    }

    if (ret == DiffErrOK)
        std::cout << "PASS\n";
    else if (ret == DiffErrWarn)
        std::cout << "WARNING\n";
    else {
        std::cout << "FAILURE\n";
        ot.return_value = ret;
    }
    return ret;
}
