// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause and Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO


#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <numeric>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "oiiotool.h"

#include <OpenEXR/ImfTimeCode.h>
#include <OpenImageIO/Imath.h>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/color.h>
#include <OpenImageIO/deepdata.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/filter.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/timer.h>

#ifndef NDEBUG
#    define OIIO_UNIT_TEST_QUIET_SUCCESS
#    include <OpenImageIO/unittest.h>
#endif

#ifdef USE_OPENCV
#    include <OpenImageIO/imagebufalgo_opencv.h>
#endif

using namespace OIIO;
using namespace OiioTool;
using namespace ImageBufAlgo;
using pvt::print_info_options;


#ifndef NDEBUG
#    define OIIO_UNIT_TESTS 1
#endif

#ifndef OPENIMAGEIO_METADATA_HISTORY_DEFAULT
#    define OPENIMAGEIO_METADATA_HISTORY_DEFAULT 0
#endif



// Macro to fully set up the "action" function that straightforwardly calls
// a lambda for each subimage. Beware, the macro expansion rules may require
// you may need to enclose the lambda itself in parenthesis () if there it
// contains commas that are not inside other parentheses.
#define OIIOTOOL_OP(name, ninputs, ...)                              \
    static void action_##name(Oiiotool& ot, cspan<const char*> argv) \
    {                                                                \
        if (ot.postpone_callback(ninputs, action_##name, argv))      \
            return;                                                  \
        OiiotoolOp op(ot, "-" #name, argv, ninputs, __VA_ARGS__);    \
        op();                                                        \
    }

// Lke OIIOTOOL_OP, but designate the op as "inplace" -- which means it
// uses the input image itself as the destination.
#define OIIOTOOL_INPLACE_OP(name, ninputs, ...)                      \
    static void action_##name(Oiiotool& ot, cspan<const char*> argv) \
    {                                                                \
        if (ot.postpone_callback(ninputs, action_##name, argv))      \
            return;                                                  \
        OiiotoolOp op(ot, "-" #name, argv, ninputs, __VA_ARGS__);    \
        op.inplace(true);                                            \
        op();                                                        \
    }

// Canned setup for an op that uses one image on the stack.
#define UNARY_IMAGE_OP(name, impl)                                 \
    OIIOTOOL_OP(name, 1, [](OiiotoolOp& op, span<ImageBuf*> img) { \
        return impl(*img[0], *img[1]);                             \
    })

// Canned setup for an op that uses two images on the stack.
#define BINARY_IMAGE_OP(name, impl)                                \
    OIIOTOOL_OP(name, 2, [](OiiotoolOp& op, span<ImageBuf*> img) { \
        return impl(*img[0], *img[1], *img[2]);                    \
    })

// Canned setup for an op that uses one image on the stack and one float
// on the command line.
#define BINARY_IMAGE_FLOAT_OP(name, impl)                          \
    OIIOTOOL_OP(name, 1, [](OiiotoolOp& op, span<ImageBuf*> img) { \
        float val = Strutil::stof(op.args(1));                     \
        return impl(*img[0], *img[1], val);                        \
    })

// Canned setup for an op that uses one image on the stack and one color
// on the command line.
#define BINARY_IMAGE_COLOR_OP(name, impl, defaultval)                   \
    OIIOTOOL_OP(name, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {      \
        int nchans = img[1]->spec().nchannels;                          \
        std::vector<float> val(nchans, defaultval);                     \
        int nvals = Strutil::extract_from_list_string(val, op.args(1)); \
        val.resize(nvals);                                              \
        val.resize(nchans, val.size() == 1 ? val.back() : defaultval);  \
        return impl(*img[0], *img[1], val, ROI(), 0);                   \
    })

// Macro to fully set up the "action" function that straightforwardly
// calls a custom OiiotoolOp class.
#define OP_CUSTOMCLASS(name, opclass, ninputs)                       \
    static void action_##name(Oiiotool& ot, cspan<const char*> argv) \
    {                                                                \
        if (ot.postpone_callback(ninputs, action_##name, argv))      \
            return;                                                  \
        opclass op(ot, #name, argv);                                 \
        op();                                                        \
    }



Oiiotool::Oiiotool()
{
    clear_options();
}



void
Oiiotool::clear_options()
{
    verbose            = false;
    quiet              = false;
    debug              = false;
    dryrun             = false;
    runstats           = false;
    noclobber          = false;
    allsubimages       = false;
    printinfo          = false;
    printstats         = false;
    dumpdata           = false;
    dumpdata_showempty = true;
    dumpdata_C         = false;
    hash               = false;
    updatemode         = false;
    autoorient         = false;
    autocc             = false;
    autoccunpremult    = false;
    autopremult        = true;
    nativeread         = false;
    metamerge          = false;
    cachesize          = 4096;
    autotile           = 0;  // was: 4096
    // FIXME: Turned off autotile by default Jan 2018 after thinking that
    // it was possible to deadlock when doing certain parallel IBA functions
    // in combination with autotile. When the deadlock possibility is fixed,
    // maybe we'll turn it back to on by default.
    frame_padding   = 0;
    eval_enable     = true;
    skip_bad_frames = false;
    full_command_line.clear();
    printinfo_metamatch.clear();
    printinfo_nometamatch.clear();
    printinfo_verbose = false;
    clear_input_config();
    m_first_input_dimensions = ImageSpec();
    output_dataformat        = TypeDesc::UNKNOWN;
    output_channelformats.clear();
    output_bitspersample      = 0;
    output_scanline           = false;
    output_tilewidth          = 0;
    output_tileheight         = 0;
    output_compression        = "";
    output_quality            = -1;
    output_planarconfig       = "default";
    output_adjust_time        = false;
    output_autocrop           = true;
    output_autotrim           = false;
    output_dither             = false;
    output_force_tiles        = false;
    metadata_nosoftwareattrib = false;
#if OPENIMAGEIO_METADATA_HISTORY_DEFAULT
    metadata_history = Strutil::from_string<int>(
        getenv("OPENIMAGEIO_METADATA_HISTORY", "1"));
#else
    metadata_history = Strutil::from_string<int>(
        getenv("OPENIMAGEIO_METADATA_HISTORY"));
#endif
    diff_warnthresh    = 1.0e-6f;
    diff_warnpercent   = 0;
    diff_hardwarn      = std::numeric_limits<float>::max();
    diff_failthresh    = 1.0e-6f;
    diff_failpercent   = 0;
    diff_hardfail      = std::numeric_limits<float>::max();
    m_pending_callback = nullptr;
    m_pending_argv.clear();
    frame_number        = 0;
    frame_padding       = 0;
    input_dataformat    = TypeUnknown;
    input_bitspersample = 0;
    input_channelformats.clear();
}



ColorConfig&
Oiiotool::colorconfig()
{
    // It's safe to check the pointer and if it exists, return it, since
    // once it's created, it will never be changed.
    if (ColorConfig* cc = m_colorconfig.get())
        return *cc;

    // Otherwise, we need to create it. But we need to be thread-safe.
    static std::mutex colorconfig_mutex;
    std::lock_guard lock(colorconfig_mutex);
    if (!m_colorconfig) {
        if (debug)
            OIIO::print("oiiotool Creating ColorConfig\n");
        m_colorconfig.reset(new ColorConfig);
    }
    return *m_colorconfig.get();
}



void
Oiiotool::clear_input_config()
{
    input_config     = ImageSpec();
    input_config_set = false;
    if (!autopremult) {
        input_config.attribute("oiio:UnassociatedAlpha", 1);
        input_config_set = true;
    }
}



static std::string
format_resolution(int w, int h, int x, int y)
{
    return Strutil::fmt::format("{}x{}{:+d}{:+d}", w, h, x, y);
}



static std::string
format_resolution(float w, float h, float x, float y)
{
    return Strutil::fmt::format("{}x{}{:+g}{:+g}", w, h, x, y);
}



static std::string
format_resolution(int w, int h, int d, int x, int y, int z)
{
    return Strutil::fmt::format("{}x{}x{}{:+d}{:+d}{:+d}", w, h, d, x, y, z);
}



template<typename T>
static bool
scan_resolution(string_view str, T& w, T& h)
{
    return Strutil::parse_value(str, w) && Strutil::parse_char(str, 'x')
           && Strutil::parse_value(str, h);
}


template<typename T>
static bool
scan_offset(string_view str, T& x, T& y)
{
    string_view orig = str;
    if (Strutil::parse_value(str, x)
        && (str.size() && (str[0] == '+' || str[0] == '-'))
        && Strutil::parse_value(str, y))
        return true;
    str = orig;
    if (Strutil::parse_value(str, x) && Strutil::parse_char(str, ',')
        && Strutil::parse_value(str, y))
        return true;
    return false;
}



template<typename T>
static bool
scan_res_offset(string_view str, T& w, T& h, T& x, T& y)
{
    string_view orig = str;
    if (Strutil::parse_value(str, w) && Strutil::parse_char(str, 'x')
        && Strutil::parse_value(str, h)
        && (str.size() && (str[0] == '+' || str[0] == '-'))
        && Strutil::parse_value(str, x)
        && (str.size() && (str[0] == '+' || str[0] == '-'))
        && Strutil::parse_value(str, y))
        return true;
    str = orig;
    if (Strutil::parse_value(str, w) && Strutil::parse_char(str, 'x')
        && Strutil::parse_value(str, h) && Strutil::parse_char(str, ',')
        && Strutil::parse_value(str, x) && Strutil::parse_char(str, ',')
        && Strutil::parse_value(str, y))
        return true;
    return false;
}


static bool
scan_scale_percent(string_view str, float& x, float& y)
{
    return Strutil::parse_value(str, x) && Strutil::parse_char(str, '%')
           && Strutil::parse_char(str, 'x') && Strutil::parse_value(str, y)
           && Strutil::parse_char(str, '%');  // NOSONAR
}

static bool
scan_scale_percent(string_view str, float& x)
{
    return Strutil::parse_value(str, x) && Strutil::parse_char(str, '%');
}


template<typename T>
static bool
scan_box(string_view str, T& xmin, T& ymin, T& xmax, T& ymax)
{
    Strutil::trim_whitespace(str);
    T f[4];
    if (Strutil::parse_values(str, "", f, ",") && str.empty()) {
        xmin = f[0];
        ymin = f[1];
        xmax = f[2];
        ymax = f[3];
        return true;
    }
    return false;
}

#ifdef OIIO_UNIT_TESTS
static void
unit_test_scan_box()
{
    Strutil::print("unit test scan_box...\n");
    {
        int xmin = -1, ymin = -1, xmax = -1, ymax = -1;
        OIIO_CHECK_ASSERT(scan_box("11,12,13,14", xmin, ymin, xmax, ymax)
                          && xmin == 11 && ymin == 12 && xmax == 13
                          && ymax == 14);
        OIIO_CHECK_ASSERT(scan_box("1,2,3", xmin, ymin, xmax, ymax) == false);
        OIIO_CHECK_ASSERT(scan_box("1,2,3,4,5", xmin, ymin, xmax, ymax)
                          == false);
        OIIO_CHECK_ASSERT(scan_box("1,2.5,3,4", xmin, ymin, xmax, ymax)
                          == false);
    }
    {
        float xmin = -1, ymin = -1, xmax = -1, ymax = -1;
        OIIO_CHECK_ASSERT(scan_box("11,12,13,14", xmin, ymin, xmax, ymax)
                          && xmin == 11 && ymin == 12 && xmax == 13
                          && ymax == 14);
        OIIO_CHECK_ASSERT(
            scan_box("11.5,12.5,13.5,14.5", xmin, ymin, xmax, ymax)
            && xmin == 11.5f && ymin == 12.5f && xmax == 13.5f
            && ymax == 14.5f);
        OIIO_CHECK_ASSERT(scan_box("1,2,3", xmin, ymin, xmax, ymax) == false);
        OIIO_CHECK_ASSERT(scan_box("1,2,3,4,5", xmin, ymin, xmax, ymax)
                          == false);
    }
}
#endif



// Helper: Remove an optional modifier ":NAME=value" from command string str
static std::string
remove_modifier(string_view str, string_view name)
{
    std::string sentinel = Strutil::fmt::format(":{}=", name);
    std::string result;
    size_t start = str.find(sentinel);
    if (start != string_view::npos) {
        size_t end = start + sentinel.size();
        end        = std::min(str.find(":", end), str.size());
        result     = Strutil::concat(str.substr(0, start), str.substr(end));
    } else {
        result = str;
    }
    return result;
}



// FIXME -- lots of things we skimped on so far:
// FIXME: reject volume images?
// FIXME: do all ops respect -a (or lack thereof?)


bool
Oiiotool::read(ImageRecRef img, ReadPolicy readpolicy, string_view channel_set)
{
    // If the image is already elaborated, take an early out, both to
    // save time, but also because we only want to do the format and
    // tile adjustments below as images are read in fresh from disk.
    if (img->elaborated())
        return true;

    // Cause the ImageRec to get read.  Try to compute how long it took.
    // Subtract out ImageCache time, to avoid double-accounting it later.
    float pre_ic_time, post_ic_time;
    imagecache->getattribute("stat:fileio_time", pre_ic_time);
    total_readtime.start();
    if (nativeread)
        readpolicy = ReadPolicy(readpolicy | ReadNative);
    bool ok = img->read(readpolicy, channel_set);
    total_readtime.stop();
    imagecache->getattribute("stat:fileio_time", post_ic_time);
    total_imagecache_readtime += post_ic_time - pre_ic_time;
    total_readtime.add_seconds(pre_ic_time - post_ic_time);

    // A failed read leaves the ImageRec with no subimages, so bail out before
    // anything below tries to examine its specs.
    if (!ok || !img->subimages()) {
        error("read", format_read_error(img->name(), img->geterror()));
        return false;
    }

    // If this is the first tiled image we have come across, use it to
    // set our tile size (unless the user explicitly set a tile size, or
    // explicitly instructed scanline output).
    const ImageSpec& nspec((*img)().nativespec());
    if (nspec.tile_width && !output_tilewidth && !output_scanline) {
        output_tilewidth  = nspec.tile_width;
        output_tileheight = nspec.tile_height;
    }
    // Remember the channel format details of the first example of each
    // channel name that we encounter.
    remember_input_channelformats(img);

    return true;
}



bool
Oiiotool::read_nativespec(ImageRecRef img)
{
    // If the image is already elaborated, take an early out, both to
    // save time, but also because we only want to do the format and
    // tile adjustments below as images are read in fresh from disk.
    if (img->elaborated())
        return true;

    // Cause the ImageRec to get read.  Try to compute how long it took.
    // Subtract out ImageCache time, to avoid double-accounting it later.
    float pre_ic_time, post_ic_time;
    imagecache->getattribute("stat:fileio_time", pre_ic_time);
    total_readtime.start();
    bool ok = img->read_nativespec();
    total_readtime.stop();
    imagecache->getattribute("stat:fileio_time", post_ic_time);
    total_imagecache_readtime += post_ic_time - pre_ic_time;

    // A failed read leaves the ImageRec with no subimages, so callers must
    // not be handed back a "success" they will dereference.
    if (!ok || !img->subimages()) {
        error("read", format_read_error(img->name(), img->geterror()));
        return false;
    }
    return true;
}



void
Oiiotool::remember_input_channelformats(ImageRecRef img)
{
    for (int s = 0, subimages = img->subimages(); s < subimages; ++s) {
        const ImageSpec& nspec((*img)(s, 0).nativespec());
        // Overall default format is the merged type of all subimages
        // of the first input image.
        input_dataformat         = TypeDesc::basetype_merge(input_dataformat,
                                                            nspec.format);
        std::string subimagename = nspec.get_string_attribute(
            "oiio:subimagename");
        if (subimagename.size()) {
            // Record a best guess for this subimage, if not already set.
            auto key = Strutil::fmt::format("{}.*", subimagename);
            if (input_channelformats[key] == "")
                input_channelformats[key] = nspec.format.c_str();
        }
        if (!input_bitspersample)
            input_bitspersample = nspec.get_int_attribute("oiio:BitsPerSample");
        for (int c = 0; c < nspec.nchannels; ++c) {
            // For each channel, if we don't already have a type recorded
            // for its name, record it. Both the bare channel name, and also
            // "subimagename.channelname", so that we can remember the same
            // name differently for different subimages.
            std::string chname     = nspec.channel_name(c);
            std::string chtypename = nspec.channelformat(c).c_str();
            if (subimagename.size()) {
                std::string subchname
                    = Strutil::fmt::format("{}.{}", subimagename, chname);
                if (input_channelformats[subchname] == "")
                    input_channelformats[subchname] = chtypename;
            } else {
                if (input_channelformats[chname] == "")
                    input_channelformats[chname] = chtypename;
            }
        }
    }
#if 0
    std::cout << "Input channel type map:\n";
    for (auto& x : input_channelformats)
        std::cout << "   " << x.first << " -> " << x.second << "\n";
#endif
}



bool
Oiiotool::postpone_callback(int required_images, CallbackFunction func,
                            cspan<const char*> argv)
{
    if (image_stack_depth() < required_images) {
        // Not enough have inputs been specified so far, so put this function
        // on the "pending" list. Use ustring to turn it into char*'s that
        // won't disappear out from under us.
        m_pending_callback = func;
        m_pending_argv.assign(argv.begin(), argv.end());
        for (auto& s : m_pending_argv)
            s = ustring(s).c_str();
        return true;
    }
    return false;
}



void
Oiiotool::process_pending()
{
    // Process any pending command -- this is a case where the
    // command line had prefix 'oiiotool --action file1 file2'
    // instead of infix 'oiiotool file1 --action file2'.
    if (m_pending_callback) {
        std::vector<const char*> argv = std::move(m_pending_argv);
        CallbackFunction callback     = m_pending_callback;
        m_pending_callback            = NULL;
        (*callback)(*this, argv);
    }
}



void
Oiiotool::error(string_view command, string_view explanation) const
{
    auto& errstream(nostderr ? std::cout : std::cerr);
    errstream << "oiiotool ERROR";
    if (command.size())
        errstream << ": " << command;
    if (explanation.size())
        errstream << " : " << explanation;
    else
        errstream << " (unknown error)";
    errstream << "\n";
    // Repeat the command line, so if oiiotool is being called from a
    // script, it's easy to debug how the command was mangled.
    errstream << "Full command line was:\n> " << full_command_line << "\n";
    if (!noerrexit) {
        // Cease further processing of the command line
        const_cast<Oiiotool*>(this)->ap.abort();
        const_cast<Oiiotool*>(this)->return_value = EXIT_FAILURE;
    }
}



void
Oiiotool::warning(string_view command, string_view explanation) const
{
    auto& errstream(nostderr ? std::cout : std::cerr);
    errstream << "oiiotool WARNING";
    if (command.size())
        errstream << ": " << command;
    if (explanation.size())
        errstream << " : " << explanation;
    else
        errstream << " (unknown warning)";
    errstream << "\n";
}



ParamValueList
Oiiotool::extract_options(string_view command)
{
    using namespace Strutil;
    ParamValueList optlist;

    // Note: the first execution of the loop test will skip over the initial
    // section through the first colon (--foo:), and the test will fail and
    // end the loop when we've exhausted `command`.
    while (parse_until_char(command, ':') && parse_char(command, ':')) {
        string_view name = parse_identifier(command);
        string_view value;
        bool ok = parse_char(command, '=');
        if (name.size() && ok) {
            if (command.size() && (command[0] == '\'' || command[0] == '\"')) {
                // If single or double quoted, the value is the contents
                // between the quotes.
                ok = parse_string(command, value, true, DeleteQuotes);
            } else {
                // If not quoted, the value is everything until the next ':'
                value = parse_until(command, ":");
            }
        }
        if (ok && name.size() && value.size()) {
            // We seem to have a name and value. Add to the optlist.
            optlist[name] = value;
        }
    }
    return optlist;
}



// --threads
static void
set_threads(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    if (ot.in_parallel_frame_loop())
        return;
    int nthreads = Strutil::stoi(argv[1]);
    OIIO::attribute("threads", nthreads);
    OIIO::attribute("exr_threads", nthreads);
}



// --cache
static void
set_cachesize(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    ot.cachesize = Strutil::stoi(argv[1]);
    if (ot.cachesize) {
        OIIO::attribute("imagebuf:use_imagecache", 1);
        ot.imagecache->attribute("max_memory_MB", float(ot.cachesize));
    } else {
        OIIO::attribute("imagebuf:use_imagecache", 0);
    }
}



// --autotile
static void
set_autotile(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    ot.autotile = Strutil::stoi(argv[1]);
    ot.imagecache->attribute("autotile", ot.autotile);
    ot.imagecache->attribute("autoscanline", int(ot.autotile ? 1 : 0));
}



// --native
static void
set_native(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.nativeread = true;
    ot.imagecache->attribute("forcefloat", 0);
}



// --dumpdata
static void
set_dumpdata(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command   = ot.express(argv[0]);
    auto options          = ot.extract_options(command);
    ot.dumpdata           = true;
    ot.dumpdata_showempty = options.get_int("empty", 1);
    ot.dumpdata_C_name    = options.get_string("C");
    ot.dumpdata_C         = ot.dumpdata_C_name.size();
}



// --info
static void
set_printinfo(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command  = ot.express(argv[0]);
    ot.printinfo         = true;
    auto options         = ot.extract_options(command);
    ot.printinfo_format  = options["format"];
    ot.printinfo_verbose = options.get_int("verbose");
}



// --autocc
static void
set_autocc(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    ot.autocc           = true;
    ot.autoccunpremult  = options.get_int("unpremult");
}



// --autopremult
static void
set_autopremult(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.autopremult = true;
    ot.imagecache->attribute("unassociatedalpha", 0);
    ot.input_config.erase_attribute("oiio:UnassociatedAlpha");
}



// --no-autopremult
static void
unset_autopremult(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.autopremult = false;
    ot.imagecache->attribute("unassociatedalpha", 1);
    ot.input_config.attribute("oiio:UnassociatedAlpha", 1);
    ot.input_config_set = true;
}



// --label
static void
action_label(Oiiotool& ot, cspan<const char*> argv)
{
    string_view command = ot.express(argv[0]);
    string_view name    = ot.express(argv[1]);
    if (!Strutil::string_is_identifier(name)) {
        ot.errorfmt(command, "Invalid label name \"{}\"", name);
        return;
    }
    ot.image_labels[name] = ot.curimg;
}



static void
string_to_dataformat(const std::string& s, TypeDesc& dataformat, int& bits)
{
    struct DataFormat {
        const char* name;
        TypeDesc format;
        int bits;
    };
    // clang-format off
    static DataFormat formats[] = {
        { "uint8",  TypeDesc::UINT8,   8 },
        { "int8",   TypeDesc::INT8,    8 },
        { "uint10", TypeDesc::UINT16, 10 },
        { "uint12", TypeDesc::UINT16, 12 },
        { "uint16", TypeDesc::UINT16, 16 },
        { "int16",  TypeDesc::INT16,  16 },
        { "uint32", TypeDesc::UINT32, 32 },
        { "int32",  TypeDesc::INT32,  32 },
        { "half",   TypeDesc::HALF,   16 },
        { "float",  TypeDesc::FLOAT,  32 },
        { "double", TypeDesc::DOUBLE, 64 },
        { "uint6",  TypeDesc::UINT8,   6 },
        { "uint4",  TypeDesc::UINT8,   4 },
        { "uint2",  TypeDesc::UINT8,   2 },
        { "uint1",  TypeDesc::UINT8,   1 },
    };
    // clang-format on
    for (auto& f : formats) {
        if (s == f.name) {
            dataformat = f.format;
            bits       = f.bits;
            return;
        }
    }
    dataformat = TypeUnknown;
    bits       = 0;
}



inline int
get_value_override(string_view localoption, int defaultval = 0)
{
    return localoption.size() ? Strutil::from_string<int>(localoption)
                              : defaultval;
}


inline float
get_value_override(string_view localoption, float defaultval)
{
    return localoption.size() ? Strutil::from_string<float>(localoption)
                              : defaultval;
}


inline string_view
get_value_override(string_view localoption, string_view defaultval)
{
    return localoption.size() ? localoption : defaultval;
}



// Given a (potentially empty) overall data format, per-channel formats,
// and bit depth, modify the existing spec.
static void
set_output_dataformat(ImageSpec& spec, TypeDesc format,
                      std::map<std::string, std::string>& channelformats,
                      int bitdepth, int subimage_index, int nsubimages)
{
    if (format != TypeUnknown)
        spec.format = format;
    spec.channelformats.resize(spec.nchannels, spec.format);
    if (!channelformats.empty()) {
        std::string subimagename = spec["oiio:subimagename"];
        // If this is one of several subimages and it doesn't have a name, let
        // it be referred to be "subimage0N".
        if (nsubimages > 1 && subimagename.size() == 0)
            subimagename = Strutil::format("subimage{:02}", subimage_index);
        auto subwildcard = Strutil::format("{}.*", subimagename);
        for (int c = 0; c < spec.nchannels; ++c) {
            std::string chname = spec.channel_name(c);
            auto subchname     = Strutil::format("{}.{}", subimagename, chname);
            TypeDesc chtype    = spec.channelformat(c);
            if (subimagename.size() && channelformats[subchname] != "")
                chtype = TypeDesc(channelformats[subchname]);
            else if (subimagename.size() && channelformats[subwildcard] != "")
                chtype = TypeDesc(channelformats[subwildcard]);
            else if (channelformats[chname] != "")
                chtype = TypeDesc(channelformats[chname]);
            if (chtype != TypeUnknown)
                spec.channelformats[c] = chtype;
        }
    }

    // Eliminate the per-channel formats if they are all the same.
    if (spec.channelformats.size()) {
        bool allsame = true;
        for (auto& c : spec.channelformats)
            allsame &= (c == spec.channelformats[0]);
        if (allsame) {
            spec.format = spec.channelformats[0];
            spec.channelformats.clear();
        }
    }

    if (bitdepth)
        spec.attribute("oiio:BitsPerSample", bitdepth);
    else
        spec.erase_attribute("oiio:BitsPerSample");
}



static void
adjust_output_options(string_view filename, ImageSpec& spec,
                      const ImageSpec* nativespec, const Oiiotool& ot,
                      int subimage_index, int nsubimages,
                      bool format_supports_tiles,
                      const ParamValueList& fileoptions,
                      bool was_direct_read = false)
{
    // What data format and bit depth should we use for the output? Here's
    // the logic:
    // * If a specific request was made on this command (e.g. -o:format=half)
    //   or globally (e.g., -d half), honor that, with a per-command request
    //   taking precedence.
    // * Otherwise, If the buffer is more or less a direct copy from an
    //   input image (as read, not the result of subsequent operations,
    //   which will tend to generate float output no matter what the
    //   inputs), write it out in the same format it was read from.
    // * Otherwise, output the same type as the FIRST file that was input
    //   (we are guessing that even if the operations made result buffers
    //   that were float, the user probably wanted to output it the same
    //   format as the input, or else she would have said so).
    // * Otherwise, just write the buffer's format, regardless of how it got
    //   that way.

    // spec is what we're going to use for output.

    // Accumulating results here
    TypeDesc requested_output_dataformat;
    std::map<std::string, std::string> requested_channelformats;
    int requested_output_bits = 0;

    if (was_direct_read && nativespec) {
        // If the image we're outputting is an unmodified direct read of a
        // file, assume that we'll default to outputting the same channel
        // formats it started in.
        requested_output_dataformat = nativespec->format;
        for (int c = 0; c < nativespec->nchannels; ++c) {
            requested_channelformats[nativespec->channel_name(c)]
                = nativespec->channelformat(c).c_str();
        }
        requested_output_bits = nativespec->get_int_attribute(
            "oiio:BitsPerSample");
    } else if (ot.input_dataformat != TypeUnknown) {
        // If the image we're outputting is a computed or modified image, not
        // a direct read, then assume that the FIRST image we read in provides
        // a template for the output we want (if we ever read an image).
        requested_output_dataformat = ot.input_dataformat;
        requested_channelformats    = ot.input_channelformats;
        requested_output_bits       = ot.input_bitspersample;
    }

    // Any "global" format requests set by -d override the above.
    if (ot.output_dataformat != TypeUnknown) {
        // `-d type` clears the board and imposes the request
        requested_output_dataformat = ot.output_dataformat;
        requested_channelformats.clear();
        spec.channelformats.clear();
        if (ot.output_bitspersample)
            requested_output_bits = ot.output_bitspersample;
    }
    if (!ot.output_channelformats.empty()) {
        // `-d chan=type` overrides the format for a specific channel
        for (auto&& c : ot.output_channelformats)
            requested_channelformats[c.first] = c.second;
    }

    // Any override options on the -o command itself take precedence over
    // everything else.
    if (fileoptions.contains("type")) {
        requested_output_dataformat.fromstring(fileoptions.get_string("type"));
        requested_channelformats.clear();
        spec.channelformats.clear();
    } else if (fileoptions.contains("datatype")) {
        requested_output_dataformat.fromstring(
            fileoptions.get_string("datatype"));
        requested_channelformats.clear();
        spec.channelformats.clear();
    }
    requested_output_bits = fileoptions.get_int("bits", requested_output_bits);

    // At this point, the trio of "requested" variable reflect any global or
    // command requests to override the logic of what was found in the input
    // files.

    // Set the types in the spec
    set_output_dataformat(spec, requested_output_dataformat,
                          requested_channelformats, requested_output_bits,
                          subimage_index, nsubimages);


    // Tiling strategy:
    // * If a specific request was made for tiled or scanline output, honor
    //   that (assuming the file format supports it).
    // * Otherwise, if the buffer is a direct copy from an input image, try
    //   to write it with the same tile/scanline choices as the input (if
    //   the file format supports it).
    // * Otherwise, just default to scanline.
    int requested_tilewidth  = ot.output_tilewidth;
    int requested_tileheight = ot.output_tileheight;
    std::string tilesize     = fileoptions["tile"];
    if (tilesize.size()) {
        int x, y;  // dummy vals for adjust_geometry
        ot.adjust_geometry("-o", requested_tilewidth, requested_tileheight, x,
                           y, tilesize.c_str(), false, false);
    }
    bool requested_scanline = fileoptions.get_int("scanline",
                                                  ot.output_scanline);
    if (requested_tilewidth && !requested_scanline && format_supports_tiles) {
        // Explicit request to tile, honor it.
        spec.tile_width  = requested_tilewidth;
        spec.tile_height = requested_tileheight ? requested_tileheight
                                                : requested_tilewidth;
        spec.tile_depth  = 1;  // FIXME if we ever want volume support
    } else if (was_direct_read && nativespec && nativespec->tile_width > 0
               && nativespec->tile_height > 0 && !requested_scanline
               && format_supports_tiles) {
        // No explicit request, but a direct read of a tiled input: keep the
        // input tiling.
        spec.tile_width  = nativespec->tile_width;
        spec.tile_height = nativespec->tile_height;
        spec.tile_depth  = nativespec->tile_depth;
    } else {
        // Otherwise, be safe and force scanline output.
        spec.tile_width = spec.tile_height = spec.tile_depth = 0;
    }

    if (!ot.output_compression.empty()) {
        // Note: may be in the form "name:quality"
        spec.attribute("compression", ot.output_compression);
    }
    if (ot.output_quality > 0)
        spec.attribute("CompressionQuality", ot.output_quality);

    if (fileoptions.get_int("separate"))
        spec.attribute("planarconfig", "separate");
    else if (fileoptions.get_int("contig"))
        spec.attribute("planarconfig", "contig");
    else if (ot.output_planarconfig == "contig"
             || ot.output_planarconfig == "separate")
        spec.attribute("planarconfig", ot.output_planarconfig);

    // Append command to image history.  Sometimes we may not want to recite the
    // entire command line (eg. when we have loaded it up with metadata attributes
    // that will make it into the header anyway).
    if (!ot.metadata_nosoftwareattrib) {
        std::string cmdline;
        if (ot.metadata_history) {
            cmdline = ot.full_command_line;
        } else {
            cmdline = SHA1(ot.full_command_line).digest();
        }
        std::string history = spec.get_string_attribute("Exif:ImageHistory");
        if (ot.metadata_history && !Strutil::iends_with(history, cmdline)) {
            if (history.length() && !Strutil::iends_with(history, "\n"))
                history += std::string("\n");
            history += cmdline;
            spec.attribute("Exif:ImageHistory", history);
        }
        std::string software = Strutil::fmt::format("OpenImageIO {} : {}",
                                                    OIIO_VERSION_STRING,
                                                    cmdline);
        spec.attribute("Software", software);
    }

    int dither = fileoptions.get_int("dither", ot.output_dither);
    if (dither) {
        int h = (int)Strutil::strhash(filename);
        if (!h)
            h = 1;
        spec.attribute("oiio:dither", h);
    }

    // Make sure we kill any special hints that maketx adds and that will
    // no longer be valid after whatever oiiotool operations we've done.
    spec.erase_attribute("oiio:SHA-1");
    spec.erase_attribute("oiio:ConstantColor");
    spec.erase_attribute("oiio:AverageColor");
}



static bool
DateTime_to_time_t(string_view datetime, time_t& timet)
{
    int year, month, day, hour, min, sec;
    if (!Strutil::scan_datetime(datetime, year, month, day, hour, min, sec))
        return false;
    // OIIO::print("{}:{}:{} {}:{}:{}\n", year, month, day, hour, min, sec);
    struct tm tmtime;
    time_t now = time(nullptr);
    Sysutil::get_local_time(&now, &tmtime);  // fill in defaults
    tmtime.tm_sec  = sec;
    tmtime.tm_min  = min;
    tmtime.tm_hour = hour;
    tmtime.tm_mday = day;
    tmtime.tm_mon  = month - 1;
    tmtime.tm_year = year - 1900;
    timet          = mktime(&tmtime);
    return true;
}



#if 0
// For a comma-separated list of channel names (e.g., "B,G,R,A"), compute
// the vector of integer indices for those channels as found in the spec
// (e.g., {2,1,0,3}), using -1 for any channels whose names were not found
// in the spec. Return true if all named channels were found, false if one
// or more were not found.
static bool
parse_channels(const ImageSpec& spec, string_view chanlist,
               std::vector<int>& channels)
{
    bool ok = true;
    channels.clear();
    while (chanlist.length()) {
        int chan = -1;
        Strutil::skip_whitespace(chanlist);
        string_view name = Strutil::parse_until(chanlist, ",");
        if (name.size()) {
            for (int i = 0; i < spec.nchannels; ++i)
                if (spec.channelnames[i] == name) {  // name of a known channel?
                    chan = i;
                    break;
                }
            if (chan < 0) {  // Didn't find a match? Try case-insensitive.
                for (int i = 0; i < spec.nchannels; ++i)
                    if (Strutil::iequals(spec.channelnames[i], name)) {
                        chan = i;
                        break;
                    }
            }
            if (chan < 0)
                ok = false;
            channels.push_back(chan);
        }
        if (!Strutil::parse_char(chanlist, ','))
            break;
    }
    return ok;
}
#endif


static std::string
first_n_channels(const ImageSpec& spec, int n)
{
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i)
            s += ",";
        s += spec.channel_name(i);
    }
    return s;
}



// -d
static void
set_dataformat(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    string_view command = ot.express(argv[0]);
    auto chans          = Strutil::splits(ot.express(argv[1]), ",");

    if (chans.size() == 0) {
        return;  // Nothing to do
    }

    if (chans.size() == 1 && !strchr(chans[0].c_str(), '=')) {
        // Of the form:   -d uint8    (for example)
        // Just one default format designated, apply to all channels
        ot.output_dataformat    = TypeDesc::UNKNOWN;
        ot.output_bitspersample = 0;
        string_to_dataformat(chans[0], ot.output_dataformat,
                             ot.output_bitspersample);
        if (ot.output_dataformat == TypeDesc::UNKNOWN)
            ot.errorfmt(command, "Unknown data format \"{}\"", chans[0]);
        ot.output_channelformats.clear();
        return;  // we're done
    }

    // If we make it here, the format designator was of the form
    //    name0=type0,name1=type1,...
    for (auto& chan : chans) {
        const char* eq = strchr(chan.c_str(), '=');
        if (eq) {
            std::string channame(chan, 0, eq - chan.c_str());
            ot.output_channelformats[channame] = std::string(eq + 1);
        } else {
            ot.errorfmt(command, "Malformed format designator \"{}\"", chan);
        }
    }
}



// --compression
static void
set_compression(Oiiotool& ot, cspan<const char*> argv)
{
    ot.output_compression = ot.express(argv[1]);
}



// --if
static void
control_if(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);

    bool cond = false;
    if (ot.running()) {
        // string_view command = ot.express(argv[0]);
        string_view value = ot.express(argv[1]);
        cond              = Strutil::eval_as_bool(value);
        // Strutil::print("while: val='{}' cond={}\n", value, cond);
    } else {
        // If not running in the outer scope, don't even evaluate the
        // condition.
        // Strutil::print("while: not running\n");
    }

    ot.push_control("if", ot.ap.current_arg(), cond);
}



// --else
static void
control_else(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);

    string_view command = ot.express(argv[0]);
    if (ot.control_stack.empty() || ot.control_stack.top().command != "if") {
        ot.errorfmt(command, "else without matching if");
        return;
    }

    // Pop the control record, flip the condition, and push it back
    auto ctrl = ot.pop_control();
    // Strutil::print("else: running={} old cond={}, new cond={}\n", ot.running(),
    //                ctrl.condition, !ctrl.condition);
    ot.push_control(ctrl.command, ctrl.start_arg, !ctrl.condition);
    // Strutil::print("    (inside else, now running={})\n", ot.running());
}



// --endif
static void
control_endif(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);

    string_view command = ot.express(argv[0]);
    if (ot.control_stack.empty() || ot.control_stack.top().command != "if") {
        ot.errorfmt(command, "endif without matching if");
        return;
    }
    // Strutil::print("endif: running={}\n", ot.running());
    ot.pop_control();
    // Strutil::print("    (after endif, now running={})\n", ot.running());
}



// --while
static void
control_while(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);

    bool cond = false;
    if (ot.running()) {
        // string_view command = ot.express(argv[0]);
        string_view value = ot.express(argv[1]);
        cond              = Strutil::eval_as_bool(value);
        // Strutil::print("while: val='{}' cond={}\n", value, cond);
    } else {
        // If not running in the outer scope, don't even evaluate the
        // condition.
        // Strutil::print("while: not running\n");
    }

    ot.push_control("while", ot.ap.current_arg(), cond);
}



// --endwhile
static void
control_endwhile(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);

    string_view command = ot.express(argv[0]);
    if (ot.control_stack.empty() || ot.control_stack.top().command != "while") {
        ot.errorfmt(command, "endwhile without matching while");
        return;
    }
    // Strutil::print("endwhile: running={}\n", ot.running());
    auto ctl = ot.pop_control();
    if (ctl.condition) {
        // If the while loop was active, loop back and run it again
        ot.ap.set_next_arg(ctl.start_arg);
    }
    // Strutil::print("    (after endwhile, now running={})\n", ot.running());
}



// --for
static void
control_for(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);

    if (ot.running()) {
        // string_view command = ot.express(argv[0]);
        std::string variable = ot.express(argv[1]);
        string_view range    = ot.express(argv[2]);

        float val = 0, limit = 0, step = 1;
        bool valid     = true;
        auto rangevals = Strutil::extract_from_list_string<float>(range);
        if (rangevals.size() == 1) {
            val   = 0.0f;
            limit = rangevals[0];
            step  = limit >= 0.0f ? 1.0f : -1.0f;
        } else if (rangevals.size() == 2) {
            val   = rangevals[0];
            limit = rangevals[1];
            step  = limit >= val ? 1.0f : -1.0f;
        } else if (rangevals.size() == 3) {
            val   = rangevals[0];
            limit = rangevals[1];
            step  = rangevals[2];
        } else {
            valid = false;
        }
        // step can't be zero or be opposite direction of val -> limit
        valid &= (step != 0.0f);
        if ((val < limit && step < 0.0f) || (val > limit && step > 0.0f))
            valid = false;

        if (!valid) {
            ot.errorfmt(argv[0], "Invalid range \"{}\"", range);
            return;
        }
        // TODO? If the range did not consist of well-formed numbers,
        // hilarity ensues.

        // There are two cases here: either we are hitting this --for
        // for the first time (need to initialize and set up the control
        // record), or we are re-iterating on a loop we already set up.
        if (ot.control_stack.empty()
            || ot.control_stack.top().start_arg != ot.ap.current_arg()) {
            // First time through the loop. Note that we recognize our first
            // time by the fact that the top of the control stack doesn't have
            // a start_arg that is this --for command.
            ot.push_control("for", ot.ap.current_arg(), true);
            // Strutil::print("First for!\n");
        } else {
            // We've started this loop already, this is at least our 2nd time
            // through. Just increment the variable and update the condition
            // for another pass through the loop.
            val = ot.uservars.get_float(variable) + step;
            // Strutil::print("Repeat for!\n");
        }
        ot.uservars.attribute(variable, val);
        bool cond = step >= 0.0f ? val < limit : val > limit;
        ot.control_stack.top().condition = cond;
        ot.ap.running(ot.running());
        // Strutil::print("for {} {} : {}={} cond={} (now running={})\n", variable,
        //                range, variable, val, cond, ot.running());
    } else {
        // If not running in the outer scope, don't even evaluate the
        // condition, just push a control record with condition false, that
        // will skip the body and resume execution after the endfor.
        ot.push_control("for", ot.ap.current_arg(), false);
        // Strutil::print("for: not running\n");
    }
}



// --endfor
static void
control_endfor(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);

    string_view command = ot.express(argv[0]);
    if (ot.control_stack.empty() || ot.control_stack.top().command != "for") {
        ot.errorfmt(command, "endfor without matching for");
        return;
    }
    // Strutil::print("endfor: running={}\n", ot.running());

    if (ot.control_stack.top().condition) {
        // If we just executed the loop body, don't pop the control record,
        // just loop again. There is special logic in --for to figure out how
        // to iterate upon hitting the start for the 2nd (or more) time.
        OIIO_DASSERT(ot.running());
        ot.ap.set_next_arg(ot.control_stack.top().start_arg);
        ot.control_stack.top().running = true;
        // Strutil::print("    (at endfor, looping back again\n");
    } else {
        // If we skipped the loop body because it's time to exit the loop, pop
        // the control record and move on.
        ot.pop_control();
        // Strutil::print("    (after endfor, now running={})\n", ot.running());
    }
}



// Centralized logic to set attribute `attribname` on object `obj` to `value`.
// The value is expressed as a string, with the type specified by `type`, or
// if TypeUnknown, inferred from the apparent formatting of the value.
template<typename T>
static void
set_attribute_helper(T& obj, string_view attribname, string_view value,
                     TypeDesc type)
{
    // First, handle the cases where we're told what to expect
    if (type.basetype == TypeDesc::FLOAT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<float> vals(n, 0.0f);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_float(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        obj.attribute(attribname, type, &vals[0]);
        return;
    }
    if (type == TypeTimeCode && value.find(':') != value.npos) {
        // Special case: They are specifying a TimeCode as a "HH:MM:SS:FF"
        // string, we need to re-encode as a uint32[2].
        int hmsf[4] = { 0, 0, 0, 0 };  // hour, min, sec, frame
        Strutil::scan_values(value, "", hmsf, ":");
        Imf::TimeCode tc(hmsf[0], hmsf[1], hmsf[2], hmsf[3]);
        obj.attribute(attribname, type, &tc);
        return;
    }
    if (type == TypeRational && value.find('/') != value.npos) {
        // Special case: They are specifying a rational as "a/b", so we need
        // to re-encode as a int32[2].
        int v[2];
        Strutil::parse_int(value, v[0]);
        Strutil::parse_char(value, '/');
        Strutil::parse_int(value, v[1]);
        obj.attribute(attribname, type, v);
        return;
    }
    if (type.basetype == TypeDesc::INT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<int> vals(n, 0);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_int(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        obj.attribute(attribname, type, &vals[0]);
        return;
    }
    if (type.basetype == TypeDesc::STRING) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<ustring> vals(n, ustring());
        if (n == 1)
            vals[0] = ustring(value);
        else {
            for (size_t i = 0; i < n && value.size(); ++i) {
                string_view s;
                Strutil::parse_string(value, s);
                vals[i] = ustring(s);
                Strutil::parse_char(value, ',');
            }
        }
        obj.attribute(attribname, type, &vals[0]);
        return;
    }

    // No explicit type... guess based on the appearance of the value string.
    if (Strutil::string_is_int(value)) {
        // Does it seem to be an int?
        obj.attribute(attribname, Strutil::stoi(value));
    } else if (Strutil::string_is_float(value)) {
        // Does it seem to be a float?
        obj.attribute(attribname, Strutil::stof(value));
    } else {
        // Otherwise, set it as a string attribute
        obj.attribute(attribname, value);
    }
}



// --set
static int
set_user_variable(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);

    string_view command = ot.express(argv[0]);
    string_view name    = ot.express(argv[1]);
    string_view value   = ot.express(argv[2]);

    if (!Strutil::string_is_identifier(name)) {
        ot.errorfmt(command, "Invalid variable name \"{}\"", name);
        return 0;
    }

    auto options = ot.extract_options(command);
    TypeDesc type(options["type"].as_string());

    set_attribute_helper(ot.uservars, name, value, type);
    return 1;
}



// --oiioattrib
static void
set_oiio_attribute(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);

    string_view command    = ot.express(argv[0]);
    string_view attribname = ot.express(argv[1]);
    string_view value      = ot.express(argv[2]);
    auto options           = ot.extract_options(command);
    TypeDesc type(options["type"].as_string());

    // Rather than duplicate the logic of set_attribute_helper for the case of
    // the global attribute that doesn't have an object to go with it, cheat
    // by putting the attrib into a temporary ParamValueList with
    // set_attribute_helper, then transfer to OIIO global attribs. This
    // doesn't happen often enough to care about the perf hit of the extra
    // copy.
    ParamValueList pl;
    set_attribute_helper(pl, attribname, value, type);
    for (const auto& p : pl)
        OIIO::attribute(p.name(), p.type(), p.data());
}

static std::vector<std::string>
get_regex_list_from_file(string_view filename)
{
    // helper function that takes a text file and return all regex attribname entries as a list
    std::string contents;
    Filesystem::read_text_file(filename, contents);
    return Strutil::splits(contents, "\n");
}


// Special OiiotoolOp whose purpose is to set attributes on the top image.
class OpAttribSetter final : public OiiotoolOp {
public:
    OpAttribSetter(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        inplace(true);  // This action operates in-place
        auto options  = ot.extract_options(opname);
        bool fromfile = options.get_int("fromfile", 0);
        if (!fromfile) {
            attribname = args(1);
        } else {
            // handle erase attribute using regex text file case
            regex_file      = args(1);
            attribname_list = get_regex_list_from_file(regex_file);
        }
        value = (nargs() > 2 ? args(2) : "");
    }
    bool setup() override
    {
        ir(0)->metadata_modified(true);
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        // Because this is an in-place operation, img[0] is the same as
        // img[1].
        if (value.empty()) {
            if (attribname_list.empty()) {
                img[0]->specmod().erase_attribute(attribname);
            } else {
                for (string_view attr : attribname_list) {
                    //std::cout << "list element:" << attr << std::endl;
                    img[0]->specmod().erase_attribute(attr);
                }
            }

        } else {
            TypeDesc type(options()["type"].as_string());
            set_attribute_helper(img[0]->specmod(), attribname, value, type);
        }
        return true;
    }

private:
    string_view attribname;
    string_view value;
    string_view regex_file;
    std::vector<std::string> attribname_list;
};



// Common helper for attrib setting commands
static void
action_attrib_helper(Oiiotool& ot, string_view command, cspan<const char*> argv)
{
    if (!ot.curimg.get()) {
        ot.warning(command, "no current image available to modify");
        return;
    }
    OpAttribSetter op(ot, command, argv);
    op();
}



// --attrib
static void
action_attrib(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);
    action_attrib_helper(ot, argv[0], argv);
}



// --sattrib
static void
action_sattrib(Oiiotool& ot, cspan<const char*> argv)
{
    // Lean on action_attrib, but force it to think it's a string
    action_attrib_helper(
        ot, argv[0],
        { Strutil::fmt::format("{}:type=string", argv[0]).c_str(), argv[1],
          argv[2] });
}



// --eraseattrib
static void
erase_attribute(Oiiotool& ot, cspan<const char*> argv)
{
    // action_attrib already has the property of erasing the attrib if no
    // value is in the args.
    action_attrib_helper(ot, argv[0], argv);
}



#if 0 /* apparently unused */
bool
Oiiotool::get_position(string_view command, string_view geom, int& x, int& y)
{
    string_view orig_geom(geom);
    bool ok = Strutil::parse_int(geom, x) && Strutil::parse_char(geom, ',')
              && Strutil::parse_int(geom, y);
    if (!ok)
        errorfmt(command, "Unrecognized position \"{}\"", orig_geom);
    return ok;
}
#endif



// Tround rounds x to the nearest T type.
template<typename T> inline T Tround(float x);

// Tround of float to float just returns the float, no round necessary.
template<>
inline float
Tround<float>(float x)
{
    return x;
}

// Tround of float to int does a round to nearest int.
template<>
inline int
Tround<int>(float x)
{
    return ifloor(x + 0.5f);
}



template<typename T>
bool
Oiiotool::adjust_geometry(string_view command, T& w, T& h, T& x, T& y,
                          string_view geom, bool allow_scaling,
                          bool allow_size) const
{
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    T ww = w, hh = h;
    T xx = x, yy = y;
    T xmax, ymax;
    if (scan_box(geom, xx, yy, xmax, ymax)) {
        x = xx;
        y = yy;
        if (std::is_integral<T>::value) {
            w = std::max(T(0), xmax - xx + 1);
            h = std::max(T(0), ymax - yy + 1);
        } else {
            w = std::max(T(0), xmax - xx);
            h = std::max(T(0), ymax - yy);
        }
    } else if (scan_res_offset(geom, ww, hh, xx, yy)) {
        if (!allow_size) {
            warning(command,
                    "can't be used to change the size, only the origin");
            return false;
        }
        if (ww == 0 && h != 0)
            ww = Tround<T>(hh * float(w) / float(h));
        if (hh == 0 && w != 0)
            hh = Tround<T>(ww * float(h) / float(w));
        w = ww;
        h = hh;
        x = xx;
        y = yy;
    } else if (scan_resolution(geom, ww, hh)) {
        if (!allow_size) {
            warning(command,
                    "can't be used to change the size, only the origin");
            return false;
        }
        if (ww == 0 && h != 0)
            ww = Tround<T>(hh * float(w) / float(h));
        if (hh == 0 && w != 0)
            hh = Tround<T>(ww * float(h) / float(w));
        w = ww;
        h = hh;
    } else if (scan_scale_percent(geom, scaleX, scaleY)) {
        if (!allow_scaling) {
            warning(command, "can't be used to rescale the size");
            return false;
        }
        scaleX = std::max(0.0f, scaleX * 0.01f);
        scaleY = std::max(0.0f, scaleY * 0.01f);
        if (scaleX == 0 && scaleY != 0)
            scaleX = scaleY;
        if (scaleY == 0 && scaleX != 0)
            scaleY = scaleX;
        w = Tround<T>(w * scaleX);
        h = Tround<T>(h * scaleY);
    } else if (scan_offset(geom, xx, yy)) {
        x = xx;
        y = yy;
    } else if (scan_scale_percent(geom, scaleX)) {
        if (!allow_scaling) {
            warning(command, "can't be used to rescale the size");
            return false;
        }
        scaleX *= 0.01f;
        w = Tround<T>(w * scaleX);
        h = Tround<T>(h * scaleX);
    } else if (Strutil::parse_float(geom, scaleX, false)) {
        if (!allow_scaling) {
            warning(command, "can't be used to rescale the size");
            return false;
        }
        w = Tround<T>(w * scaleX);
        h = Tround<T>(h * scaleX);
    } else {
        errorfmt(command, "Unrecognized geometry \"{}\"", geom);
        return false;
    }
    // Strutil::print("geom {}x{}, {}d{}d\n", w, h, x, y);
    return true;
}

#ifdef OIIO_UNIT_TESTS
static void
unit_test_adjust_geometry(Oiiotool& ot)
{
    // box
    int w, h, x, y;
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10,20,130,145")
                      && x == 10 && y == 20 && w == 121 && h == 126);

    // geom
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10x20+100+200")
                      && x == 100 && y == 200 && w == 10 && h == 20);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10x20-100-200")
                      && x == -100 && y == -200 && w == 10 && h == 20);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "20x0+100+200")
                      && x == 100 && y == 200 && w == 20 && h == 10);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "0x20+100+200")
                      && x == 100 && y == 200 && w == 40 && h == 20);
    OIIO_CHECK_ASSERT(
        !ot.adjust_geometry("foo", w, h, x, y, "10x20+100+200", true, false));

    // geom with comma notation for origin
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10x20,100,200")
                      && x == 100 && y == 200 && w == 10 && h == 20);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10x20,-100,-200")
                      && x == -100 && y == -200 && w == 10 && h == 20);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "20x0,100,200")
                      && x == 100 && y == 200 && w == 20 && h == 10);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "0x20,100,200")
                      && x == 100 && y == 200 && w == 40 && h == 20);
    OIIO_CHECK_ASSERT(
        !ot.adjust_geometry("foo", w, h, x, y, "10x20+100+200", true, false));

    // res only
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "10x20") && x == -42
                      && y == -42 && w == 10 && h == 20);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "20x0") && x == 0
                      && y == 0 && w == 20 && h == 10);
    w = 100, h = 50, x = y = 0;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "0x20") && x == 0
                      && y == 0 && w == 40 && h == 20);
    OIIO_CHECK_ASSERT(
        !ot.adjust_geometry("foo", w, h, x, y, "10x20", true, false));

    // scale by percentage
    w = h = 100;
    x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "200%x50%", true)
                      && x == -42 && y == -42 && w == 200 && h == 50);
    w = h = 100;
    x = y = -42;
    OIIO_CHECK_ASSERT(!ot.adjust_geometry("foo", w, h, x, y, "200%x50%"));
    w = 640;
    h = 480;
    x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "200%", true)
                      && x == -42 && y == -42 && w == 1280 && h == 960);
    OIIO_CHECK_ASSERT(!ot.adjust_geometry("foo", w, h, x, y, "200%"));

    // offset
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "+100+200")
                      && x == 100 && y == 200 && w == -42 && h == -42);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "+100-200")
                      && x == 100 && y == -200 && w == -42 && h == -42);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "-100+200")
                      && x == -100 && y == 200 && w == -42 && h == -42);

    // offset with comma notation
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "100,200")
                      && x == 100 && y == 200 && w == -42 && h == -42);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "100,-200")
                      && x == 100 && y == -200 && w == -42 && h == -42);
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "-100,200")
                      && x == -100 && y == 200 && w == -42 && h == -42);

    // scale by factor
    w = 640;
    h = 480;
    x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "2", true)
                      && x == -42 && y == -42 && w == 1280 && h == 960);
    OIIO_CHECK_ASSERT(!ot.adjust_geometry("foo", w, h, x, y, "2"));
    w = 640;
    h = 480;
    x = y = -42;
    OIIO_CHECK_ASSERT(ot.adjust_geometry("foo", w, h, x, y, "0.5", true)
                      && x == -42 && y == -42 && w == 320 && h == 240);
    OIIO_CHECK_ASSERT(!ot.adjust_geometry("foo", w, h, x, y, "0.5"));

    // errors
    w = h = x = y = -42;
    OIIO_CHECK_ASSERT(!ot.adjust_geometry("foo", w, h, x, y, "invalid")
                      && x == -42 && y == -42 && w == -42 && h == -42);
}
#endif



// --iconfig
static void
set_input_attribute(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);

    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    TypeDesc type(options["type"].as_string());
    string_view attribname = ot.express(argv[1]);
    string_view value      = ot.express(argv[2]);

    if (!value.size()) {
        // If the value is the empty string, clear the attribute
        ot.input_config.erase_attribute(attribname);
        return;
    }

    ot.input_config_set = true;

    // First, handle the cases where we're told what to expect
    if (type.basetype == TypeDesc::FLOAT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<float> vals(n, 0.0f);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_float(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        ot.input_config.attribute(attribname, type, &vals[0]);
        return;
    }
    if (type.basetype == TypeDesc::INT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<int> vals(n, 0);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_int(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        ot.input_config.attribute(attribname, type, &vals[0]);
        return;
    }
    if (type.basetype == TypeDesc::STRING) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<ustring> vals(n, ustring());
        if (n == 1)
            vals[0] = ustring(value);
        else {
            for (size_t i = 0; i < n && value.size(); ++i) {
                string_view s;
                Strutil::parse_string(value, s);
                vals[i] = ustring(s);
                Strutil::parse_char(value, ',');
            }
        }
        ot.input_config.attribute(attribname, type, &vals[0]);
        return;
    }

    if (type == TypeInt
        || (type == TypeUnknown && Strutil::string_is_int(value))) {
        // Does it seem to be an int, or did the caller explicitly request
        // that it be set as an int?
        ot.input_config.attribute(attribname, Strutil::stoi(value));
    } else if (type == TypeFloat
               || (type == TypeUnknown && Strutil::string_is_float(value))) {
        // Does it seem to be a float, or did the caller explicitly request
        // that it be set as a float?
        ot.input_config.attribute(attribname, Strutil::stof(value));
    } else {
        // Otherwise, set it as a string attribute
        ot.input_config.attribute(attribname, value);
    }
}



// --caption
static void
set_caption(Oiiotool& ot, cspan<const char*> argv)
{
    action_sattrib(ot, { argv[0], "ImageDescription", argv[1] });
}



static bool
do_set_keyword(ImageSpec& spec, const std::string& keyword)
{
    std::string oldkw = spec.get_string_attribute("Keywords");
    std::vector<std::string> oldkwlist;
    if (!oldkw.empty())
        Strutil::split(oldkw, oldkwlist, ";");
    bool dup = false;
    for (std::string& ok : oldkwlist) {
        ok = Strutil::strip(ok);
        dup |= (ok == keyword);
    }
    if (!dup) {
        oldkwlist.push_back(keyword);
        spec.attribute("Keywords", Strutil::join(oldkwlist, "; "));
    }
    return true;
}



// --keyword
static void
set_keyword(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return;
    }

    std::string keyword(ot.express(argv[1]));
    if (keyword.size())
        apply_spec_mod(ot, ot.curimg, do_set_keyword, keyword, ot.allsubimages);
}



// --clear-keywords
static void
clear_keywords(Oiiotool& ot, cspan<const char*> argv)
{
    action_sattrib(ot, { argv[0], "Keywords", "" });
}



// --orientation
static void
set_orientation(Oiiotool& ot, cspan<const char*> argv)
{
    action_attrib_helper(ot, argv[0],
                         { Strutil::fmt::format("{}:type=int", argv[0]).c_str(),
                           "Orientation", argv[1] });
}



static bool
do_rotate_orientation(ImageSpec& spec, string_view cmd)
{
    bool rotcw  = (cmd == "--orientcw" || cmd == "-orientcw" || cmd == "--rotcw"
                   || cmd == "-rotcw");
    bool rotccw = (cmd == "--orientccw" || cmd == "-orientccw"
                   || cmd == "--rotccw" || cmd == "-rotccw");
    bool rot180 = (cmd == "--orient180" || cmd == "-orient180"
                   || cmd == "--rot180" || cmd == "-rot180");
    int orientation = spec.get_int_attribute("Orientation", 1);
    if (orientation >= 1 && orientation <= 8) {
        static int cw[] = { 0, 6, 7, 8, 5, 2, 3, 4, 1 };
        if (rotcw || rotccw || rot180)
            orientation = cw[orientation];
        if (rotccw || rot180)
            orientation = cw[orientation];
        if (rotccw)
            orientation = cw[orientation];
        spec.attribute("Orientation", orientation);
    }
    return true;
}



// --orientcw --orientccw --orient180 --rotcw --rotccw --rot180
static void
rotate_orientation(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    if (!ot.curimg.get()) {
        ot.warning(command, "no current image available to modify");
        return;
    }

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    apply_spec_mod(ot, ot.curimg, do_rotate_orientation, command, allsubimages);
}



// --origin
static void
set_origin(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, set_origin, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view origin = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = spec.x, y = spec.y, z = spec.z;
        int w = spec.width, h = spec.height, d = spec.depth;
        ot.adjust_geometry(command, w, h, x, y, origin);
        if (spec.width != w || spec.height != h || spec.depth != d)
            ot.warning(command,
                       "can't be used to change the size, only the origin");
        if (spec.x != x || spec.y != y) {
            ImageBuf& ib = (*A)(s);
            if (ib.storage() == ImageBuf::IMAGECACHE) {
                // If the image is cached, we will totally screw up the IB/IC
                // operations if we try to change the origin in place, so in
                // that case force a full read to convert to a local buffer,
                // which is safe to diddle the origin.
                ib.read(0, 0, true /*force*/, spec.format);
            }
            spec.x = x;
            spec.y = y;
            spec.z = z;
            // That updated the private spec of the ImageRec. In this case
            // we really need to update the underlying IB as well.
            ib.set_origin(x, y, z);
            A->metadata_modified(true);
        }
    }
}



// --originoffset
static void
offset_origin(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, offset_origin, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view origin = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = 0, y = 0, z = 0;  // OFFSETS, not set values
        int w = spec.width, h = spec.height;
        ot.adjust_geometry(command, w, h, x, y, origin, false, false);
        if (x != 0 || y != 0) {
            ImageBuf& ib = (*A)(s);
            if (ib.storage() == ImageBuf::IMAGECACHE) {
                // If the image is cached, we will totally screw up the IB/IC
                // operations if we try to change the origin in place, so in
                // that case force a full read to convert to a local buffer,
                // which is safe to diddle the origin.
                ib.read(0, 0, true /*force*/, spec.format);
            }
            spec.x += x;
            spec.y += y;
            spec.z += z;
            // That updated the private spec of the ImageRec. In this case
            // we really need to update the underlying IB as well.
            ib.set_origin(spec.x, spec.y, spec.z);
            A->metadata_modified(true);
        }
    }
}



// --fullsize
static void
set_fullsize(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, set_fullsize, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view size = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = spec.full_x, y = spec.full_y;
        int w = spec.full_width, h = spec.full_height;
        ot.adjust_geometry(argv[0], w, h, x, y, size);
        if (spec.full_x != x || spec.full_y != y || spec.full_width != w
            || spec.full_height != h) {
            spec.full_x      = x;
            spec.full_y      = y;
            spec.full_width  = w;
            spec.full_height = h;
            // That updated the private spec of the ImageRec. In this case
            // we really need to update the underlying IB as well.
            ImageSpec& ibspec  = (*A)(s).specmod();
            ibspec.full_x      = x;
            ibspec.full_y      = y;
            ibspec.full_width  = w;
            ibspec.full_height = h;
            A->metadata_modified(true);
        }
    }
}



// --fullpixels
static void
set_full_to_pixels(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, set_full_to_pixels, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        for (int m = 0, mend = A->miplevels(s); m < mend; ++m) {
            ImageSpec& spec  = *A->spec(s, m);
            spec.full_x      = spec.x;
            spec.full_y      = spec.y;
            spec.full_z      = spec.z;
            spec.full_width  = spec.width;
            spec.full_height = spec.height;
            spec.full_depth  = spec.depth;
            // That updated the private spec of the ImageRec. In this case
            // we really need to update the underlying IB as well.
            ImageSpec& ibspec  = (*A)(s, m).specmod();
            ibspec.full_x      = spec.x;
            ibspec.full_y      = spec.y;
            ibspec.full_z      = spec.z;
            ibspec.full_width  = spec.width;
            ibspec.full_height = spec.height;
            ibspec.full_depth  = spec.depth;
        }
    }
    A->metadata_modified(true);
}



// --iccwrite  (output extracted ICC profile)
static void
icc_write(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    if (ot.postpone_callback(1, icc_write, argv))
        return;
    string_view command  = ot.express(argv[0]);
    std::string filename = ot.express(argv[1]);
    OTScopedTimer timer(ot, command);
    // auto options      = ot.extract_options(command);
    // bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    ImageSpec& spec(*A->spec(0));
    const ParamValue* icc = spec.find_attribute("ICCProfile");
    if (icc) {
        bool ok = Filesystem::write_binary_file(
            filename, cspan<char>((const char*)icc->data(), icc->datasize()));
        if (!ok) {
            ot.errorfmt(command, "Could not write ICC profile to {}", filename);
        }
    } else {
        ot.errorfmt(command, "No ICC profile found in image.", A->name());
    }
    ot.num_outputs += 1;
}



// --iccread
static void
icc_read(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    if (ot.postpone_callback(1, icc_read, argv))
        return;
    string_view command  = ot.express(argv[0]);
    std::string filename = ot.express(argv[1]);
    OTScopedTimer timer(ot, command);
    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    if (!Filesystem::exists(filename)) {
        ot.errorfmt(command, "ICC profile file {} does not exist", filename);
        return;
    }
    size_t len = Filesystem::file_size(filename);

    // Validity check: ICC profiles have a 128 byte header, and we also
    // presume they are no more than 64k, so file outside that range should
    // be rejected. (Is that a fair assumption?)
    if (len < 1 || len >= 64 * 1024 * 1024) {
        ot.errorfmt(command, "File {} is not a valid ICC profile", filename);
        return;
    }

    std::unique_ptr<char[]> icc(new char[len]);
    size_t size = Filesystem::read_bytes(filename, icc.get(), len);
    if (size != len) {
        ot.errorfmt(command, "Could not read ICC profile from {}", filename);
        return;
    }

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        (*A)(s).specmod().attribute("ICCProfile",
                                    TypeDesc(TypeDesc::UINT8, len), icc.get());
        A->update_spec_from_imagebuf(s);
    }
    A->metadata_modified(true);
}


// Set, modify, or remove the top image's CICP (ITU-T H.273) metadata.
class OpSetCICP final : public OiiotoolOp {
public:
    OpSetCICP(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        inplace(true);  // This action operates in-place
        cicp = args(1);
    }
    OpSetCICP(Oiiotool& ot, string_view opname, int argc, const char* argv[])
        : OpSetCICP(ot, opname, { argv, span_size_t(argc) })
    {
    }
    bool setup() override
    {
        ir(0)->metadata_modified(true);
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        // Because this is an in-place operation, img[0] is the same as
        // img[1].
        if (cicp.empty()) {
            img[0]->specmod().erase_attribute("CICP");
            return true;
        }
        std::vector<int> vals { 0, 0, 0, 1 };
        auto p = img[0]->spec().find_attribute("CICP",
                                               TypeDesc(TypeDesc::INT, 4));
        if (p) {
            const int* existing = static_cast<const int*>(p->data());
            for (int i = 0; i < 4; ++i)
                vals[i] = existing[i];
        }
        Strutil::extract_from_list_string<int>(vals, cicp);
        img[0]->specmod().attribute("CICP", TypeDesc(TypeDesc::INT, 4),
                                    vals.data());
        return true;
    }

private:
    string_view cicp;
};


// --cicp
static void
action_cicp(Oiiotool& ot, cspan<const char*> argv)
{
    OpSetCICP op(ot, "cicp", argv);
    op();
}



// --colorconfig
static void
set_colorconfig(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    if (!ot.colorconfig().reset(argv[1])) {
        ot.errorfmt("--colorconfig", "{}", ot.colorconfig().geterror());
    }
}



// Special OiiotoolOp whose purpose is to set attributes on the top image.
class OpSetColorSpace final : public OiiotoolOp {
public:
    OpSetColorSpace(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        inplace(true);  // This action operates in-place
        colorspace = args(1);
    }
    OpSetColorSpace(Oiiotool& ot, string_view opname, int argc,
                    const char* argv[])
        : OpSetColorSpace(ot, opname, { argv, span_size_t(argc) })
    {
    }
    bool setup() override
    {
        ir(0)->metadata_modified(true);
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        // Because this is an in-place operation, img[0] is the same as
        // img[1].
        img[0]->specmod().set_colorspace(colorspace);
        return true;
    }

private:
    string_view colorspace;
};



// --iscolorspace
static void
action_iscolorspace(Oiiotool& ot, cspan<const char*> argv)
{
    OpSetColorSpace op(ot, "iscolorspace", argv);
    op();
}



// --colorconvert
class OpColorConvert final : public OiiotoolOp {
public:
    OpColorConvert(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        fromspace = args(1);
        tospace   = args(2);
    }
    bool setup() override
    {
        if (fromspace == tospace) {
            // The whole thing is a no-op. Get rid of the empty result we
            // pushed on the stack, replace it with the original image, and
            // signal that we're done.
            ot.pop();
            ot.push(ir(1));
            return false;
        }
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        std::string contextkey   = options()["key"];
        std::string contextvalue = options()["value"];
        bool strict              = options().get_int("strict", 1);
        bool unpremult           = options().get_int("unpremult");
        if (unpremult
            && img[1]->spec().get_int_attribute("oiio:UnassociatedAlpha")
            && img[1]->spec().alpha_channel >= 0) {
            ot.warning(
                opname(),
                "Image appears to already be unassociated alpha (un-premultiplied color), beware double unpremult. Don't use --unpremult and also --colorconvert:unpremult=1.");
        }
        bool ok = ImageBufAlgo::colorconvert(*img[0], *img[1], fromspace,
                                             tospace, unpremult, contextkey,
                                             contextvalue, &ot.colorconfig());
        if (!ok && !strict) {
            // The color transform failed, but we were told not to be
            // strict, so ignore the error and just copy destination to
            // source.
            ot.warning(opname(), img[0]->geterror());
            // ok = ImageBufAlgo::copy (*img[0], *img[1], TypeDesc);
            ok = img[0]->copy(*img[1]);
        }
        return ok;
    }

private:
    string_view fromspace, tospace;
};

OP_CUSTOMCLASS(colorconvert, OpColorConvert, 1);



// --tocolorspace
static void
action_tocolorspace(Oiiotool& ot, cspan<const char*> argv)
{
    // Don't time -- let it get accounted by colorconvert
    OIIO_DASSERT(argv.size() == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return;
    }
    const char* args[3] = { argv[0], "current", argv[1] };
    action_colorconvert(ot, args);
}



// --ccmatrix
OIIOTOOL_OP(ccmatrix, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    bool unpremult = op.options().get_int("unpremult");
    auto M         = Strutil::extract_from_list_string<float>(op.args(1));
    Imath::M44f MM;
    if (M.size() == 9) {
        MM = Imath::M44f(M[0], M[1], M[2], 0.0f, M[3], M[4], M[5], 0.0f, M[6],
                         M[7], M[8], 0.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    } else if (M.size() == 16) {
        memcpy((float*)&MM, M.data(), 16 * sizeof(float));
    } else {
        ot.error(op.opname(),
                 "expected 9 or 16 comma-separated floats to form a matrix");
        return false;
    }
    if (op.options().get_int("transpose"))
        MM.transpose();
    if (op.options().get_int("invert") || op.options().get_int("inverse"))
        MM.invert();
    return ImageBufAlgo::colormatrixtransform(*img[0], *img[1], MM, unpremult);
});



// --ociolook
OIIOTOOL_OP(ociolook, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view lookname     = op.args(1);
    std::string fromspace    = op.options()["from"];
    std::string tospace      = op.options()["to"];
    std::string contextkey   = op.options()["key"];
    std::string contextvalue = op.options()["value"];
    bool inverse             = op.options().get_int("inverse");
    bool unpremult           = op.options().get_int("unpremult");
    if (fromspace == "current" || fromspace == "")
        fromspace = img[1]->spec().get_string_attribute("oiio:Colorspace");
    if (tospace == "current" || tospace == "")
        tospace = img[1]->spec().get_string_attribute("oiio:Colorspace");
    return ImageBufAlgo::ociolook(*img[0], *img[1], lookname, fromspace,
                                  tospace, unpremult, inverse, contextkey,
                                  contextvalue, &ot.colorconfig());
});



// --ociodisplay
OIIOTOOL_OP(ociodisplay, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view displayname  = op.args(1);
    string_view viewname     = op.args(2);
    std::string fromspace    = op.options()["from"];
    std::string contextkey   = op.options()["key"];
    std::string contextvalue = op.options()["value"];
    std::string looks        = op.options()["looks"];
    bool unpremult           = op.options().get_int("unpremult");
    bool inverse             = op.options().get_int("inverse");
    if (fromspace == "current" || fromspace == "")
        fromspace = img[1]->spec().get_string_attribute("oiio:Colorspace");
    return ImageBufAlgo::ociodisplay(*img[0], *img[1], displayname, viewname,
                                     fromspace, looks, unpremult, inverse,
                                     contextkey, contextvalue,
                                     &ot.colorconfig());
});



// --ociofiletransform
OIIOTOOL_OP(ociofiletransform, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view name = op.args(1);
    bool inverse     = op.options().get_int("inverse");
    bool unpremult   = op.options().get_int("unpremult");
    return ImageBufAlgo::ociofiletransform(*img[0], *img[1], name, unpremult,
                                           inverse, &ot.colorconfig());
});



// --ocionamedtransform
OIIOTOOL_OP(ocionamedtransform, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view name         = op.args(1);
    std::string contextkey   = op.options()["key"];
    std::string contextvalue = op.options()["value"];
    bool unpremult           = op.options().get_int("unpremult");
    bool inverse             = op.options().get_int("inverse");
    return ImageBufAlgo::ocionamedtransform(*img[0], *img[1], name, unpremult,
                                            inverse, contextkey, contextvalue,
                                            &ot.colorconfig());
});



static void
output_tiles(Oiiotool& ot, cspan<const char*>)
{
    // the ArgParse will have set the tile size, but we need this routine
    // to clear the scanline flag
    ot.output_scanline = false;
}



// --unmip
// N.B.: This unmips all subimages and does not honor the ':subimages='
// modifier.
static void
action_unmip(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_unmip, argv))
        return;

    // Special case -- detect if there are no MIP-mapped subimages at all,
    // in which case this is a no-op (avoid any copies or allocations).
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    ot.read();
    bool mipmapped = false;
    for (int s = 0, send = ot.curimg->subimages(); s < send; ++s)
        mipmapped |= (ot.curimg->miplevels(s) > 1);
    if (!mipmapped) {
        return;  // --unmip on an unmipped image is a no-op
    }

    // If there is work to be done, fall back on the OiiotoolOp.
    // No subclass needed, default OiiotoolOp removes MIP levels and
    // copies the first input image by default.
    timer.stop();
    OiiotoolOp op(ot, "unmip", argv, 1);
    op();
}



// --chnames
class OpChnames final : public OiiotoolOp {
public:
    OpChnames(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        preserve_miplevels(true);
    }
    // Custom creation of new ImageRec result: don't copy, just change in
    // place.
    ImageRecRef new_output_imagerec() override { return ir(1); }
    bool impl(span<ImageBuf*> img) override
    {
        string_view channelarg = ot.express(args(1));
        auto newchannelnames   = Strutil::splits(channelarg, ",");
        ImageSpec& spec        = img[0]->specmod();
        spec.channelnames.resize(spec.nchannels);
        spec.alpha_channel = -1;
        spec.z_channel     = -1;
        for (int c = 0; c < spec.nchannels; ++c) {
            if (c < (int)newchannelnames.size() && newchannelnames[c].size()) {
                std::string name = newchannelnames[c];
                ot.output_channelformats[name]
                    = ot.output_channelformats[spec.channelnames[c]];
                spec.channelnames[c] = name;
                if (Strutil::iequals(name, "A")
                    || Strutil::iends_with(name, ".A")
                    || Strutil::iequals(name, "Alpha")
                    || Strutil::iends_with(name, ".Alpha"))
                    spec.alpha_channel = c;
                if (Strutil::iequals(name, "Z")
                    || Strutil::iends_with(name, ".Z")
                    || Strutil::iequals(name, "Depth")
                    || Strutil::iends_with(name, ".Depth"))
                    spec.z_channel = c;
            }
        }
        return true;
    }
};

static void
action_set_channelnames(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_set_channelnames, argv))
        return;
    OpChnames op(ot, "chnames", argv);
    op();
}



// For a given spec (which contains the channel names for an image), and
// a comma separated list of channels (e.g., "B,G,R,A"), compute the
// vector of integer indices for those channels (e.g., {2,1,0,3}).
// A channel may be a literal assignment (e.g., "=0.5"), or a literal
// assignment with channel naming (e.g., "Z=0.5"), the name of a channel
// ("A"), or the name of a channel with a new name reassigned ("R=G").
// Return true for success, false for failure, including if any of the
// channels were not present in the image.  Upon return, channels
// will be the indices of the source image channels to copy (-1 for
// channels that are not filled with source data), values will hold
// the value to fill un-sourced channels (defaulting to zero), and
// newchannelnames will be the name of renamed or non-default-named
// channels (defaulting to "" if no special name is needed).
bool
OiioTool::decode_channel_set(const ImageSpec& spec, string_view chanlist,
                             std::vector<std::string>& newchannelnames,
                             std::vector<int>& channels,
                             std::vector<float>& values, ErrorHandler& eh)
{
    // std::cout << "Decode_channel_set '" << chanlist << "'\n";
    channels.clear();
    for (int c = 0; chanlist.length(); ++c) {
        // It looks like:
        //     <int>                (put old channel here, by numeric index)
        //     oldname              (put old named channel here)
        //     newname=oldname      (put old channel here, with new name)
        //     newname=<float>      (put constant value here, with a name)
        //     =<float>             (put constant value here, default name)
        std::string newname;
        int chan  = -1;
        float val = 0.0f;
        Strutil::skip_whitespace(chanlist);
        if (chanlist.empty())
            break;
        if (Strutil::parse_int(chanlist, chan) && chan >= 0
            && chan < spec.nchannels) {
            // case: <int>
            newname = spec.channelnames[chan];
        } else if (Strutil::parse_char(chanlist, '=')) {
            // case: =<float>
            Strutil::parse_float(chanlist, val);
        } else {
            string_view n = Strutil::parse_until(chanlist, "=,");
            string_view oldname;
            if (Strutil::parse_char(chanlist, '=')) {
                if (Strutil::parse_float(chanlist, val)) {
                    // case: newname=float
                    newname = n;
                } else {
                    // case: newname=oldname
                    newname = n;
                    oldname = Strutil::parse_until(chanlist, ",");
                }
            } else {
                // case: oldname
                oldname = n;
            }
            if (oldname.size()) {
                for (int i = 0; i < spec.nchannels; ++i)
                    if (spec.channelnames[i] == oldname) {
                        // name of a known channel
                        chan = i;
                        break;
                    }
                if (chan < 0) {  // Didn't find a match? Try case-insensitive.
                    for (int i = 0; i < spec.nchannels; ++i)
                        if (Strutil::iequals(spec.channelnames[i], oldname)) {
                            chan = i;
                            break;
                        }
                }
                if (chan < 0) {
                    eh.warningfmt(
                        "--ch: Unknown channel name \"{}\", filling with 0 (actual channels: \"{}\")",
                        oldname, Strutil::join(spec.channelnames, ","));
                }
                if (newname.empty() && chan >= 0)
                    newname = spec.channelnames[chan];
            }
        }

        if (!newname.size()) {
            const char* RGBAZ[] = { "R", "G", "B", "A", "Z" };
            if (c <= 4)
                newname = std::string(RGBAZ[c]);
            else
                newname = Strutil::fmt::format("channel{}", c);
        }

        // std::cout << "  Chan " << c << ": " << newname << ' ' << chan << ' ' << val << "\n";
        newchannelnames.push_back(newname);
        channels.push_back(chan);
        values.push_back(val);

        if (!Strutil::parse_char(chanlist, ','))
            break;
    }
    return true;
}



// --ch
static void
action_channels(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_channels, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view chanlist = ot.express(argv[1]);
    auto options         = ot.extract_options(command);
    bool allsubimages    = options.get_int("allsubimages", ot.allsubimages);

    ImageRecRef A(ot.top());
    ot.read(A);

    if (chanlist == "RGB")  // Fix common synonyms/mistakes
        chanlist = "R,G,B";
    else if (chanlist == "RGBA")
        chanlist = "R,G,B,A";

    // Decode the channel set, make the full list of ImageSpec's we'll
    // need to describe the new ImageRec with the altered channels.
    std::vector<int> allmiplevels;
    std::vector<ImageSpec> allspecs;
    bool any_changes = false;
    for (int s = 0, subimages = allsubimages ? A->subimages() : 1;
         s < subimages; ++s) {
        std::vector<std::string> newchannelnames;
        std::vector<int> channels;
        std::vector<float> values;
        bool ok = decode_channel_set(*A->spec(s, 0), chanlist, newchannelnames,
                                     channels, values, ot.eh);
        if (!ok) {
            ot.errorfmt(command, "Invalid or unknown channel selection \"{}\"",
                        chanlist);
            ot.push(A);
            return;
        }
        int miplevels = ot.allsubimages ? A->miplevels(s) : 1;
        allmiplevels.push_back(miplevels);
        for (int m = 0; m < miplevels; ++m) {
            const ImageSpec* mipspec = A->spec(s, m);
            ImageSpec spec           = *mipspec;
            spec.nchannels           = (int)newchannelnames.size();
            spec.channelformats.clear();
            spec.default_channel_names();
            allspecs.push_back(spec);
            // Are we really asking to change anything?
            if (spec.nchannels != mipspec->nchannels) {
                // Adding or dropping channels is definitely a change.
                any_changes = true;
            } else {
                for (int c = 0; c < spec.nchannels; ++c) {
                    // Change in order? For setting channel to a value,
                    // channels[c] == -1, so that will also be caught here.
                    any_changes |= (channels[c] != c);
                    // Change of channel name?
                    any_changes |= (newchannelnames[c]
                                    != mipspec->channel_name(c));
                }
            }
        }
    }

    // If for every subimage and miplevel, the requested channels are
    // identical to the old channels -- no change of channel order, no change
    // of name, no setting to a constant value -- then just leave the top
    // image as it is and slowly back away without doing anything expensive.
    if (!any_changes) {
        return;
    }

    // Create the replacement ImageRec
    ImageRecRef R(new ImageRec(A->name(), (int)allmiplevels.size(),
                               allmiplevels, allspecs));
    ot.pop();
    ot.push(R);

    // Subimage by subimage, MIP level by MIP level, copy/shuffle the
    // channels individually from the source image into the result.
    for (int s = 0, subimages = R->subimages(); s < subimages; ++s) {
        std::vector<std::string> newchannelnames;
        std::vector<int> channels;
        std::vector<float> values;
        decode_channel_set(*A->spec(s, 0), chanlist, newchannelnames, channels,
                           values, ot.eh);
        for (int m = 0, miplevels = R->miplevels(s); m < miplevels; ++m) {
            // Shuffle the indexed/named channels
            bool ok = ImageBufAlgo::channels((*R)(s, m), (*A)(s, m),
                                             (int)channels.size(), channels,
                                             values, newchannelnames, false);
            if (!ok) {
                ot.error(command, (*R)(s, m).geterror());
                break;
            }
            // Tricky subtlety: IBA::channels changed the underlying IB,
            // we may need to update the IR's copy of the spec.
            R->update_spec_from_imagebuf(s, m);
        }
    }
}



// --nchannels
static void
action_nchannels(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_nchannels, argv))
        return;

    // Get the requested number of channels
    int n = Strutil::stoi(ot.express(argv[1]));
    if (n < 1) {
        ot.errorfmt(argv[0], "nchannels must be at least 1 (got {})", n);
        return;
    }

    // We build the indices into a vector, then join them into a string.
    std::vector<int> indices;
    for (int i = 0; i < n; ++i)
        indices.push_back(i);

    // Convert to ustring to ensure the underlying char* is interned.
    // This guarantees the pointer remains valid even if action_channels
    // defers execution (postpones) beyond the scope of this function.
    ustring channel_list(Strutil::join(indices, ","));

    // Hand off to action_channels with our generated index list.
    // this ensures we are always doing the same thing as --ch
    const char* fake_argv[] = { argv[0], channel_list.c_str() };
    action_channels(ot, fake_argv);
}



// --chappend
static void
action_chappend(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_chappend, argv))
        return;
    std::string command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    int n               = OIIO::clamp(options["n"].get<int>(2), 2,
                                      int(ot.image_stack.size() + 1));
    command             = remove_modifier(command, "n");
    bool ok             = true;

    // two at a time
    for (; n >= 2; --n) {
        OiiotoolOp op(ot, "chappend", argv, 2);
        op.preserve_miplevels(true);
        op.set_impl([&ot](OiiotoolOp& op, span<ImageBuf*> img) {
            // Shuffle the indexed/named channels
            bool ok = ImageBufAlgo::channel_append(*img[0], *img[1], *img[2]);
            if (!ok) {
                ot.error(op.opname(), img[0]->geterror());
                return false;
            }
            if (ot.metamerge) {
                img[0]->specmod().extra_attribs.merge(
                    img[1]->spec().extra_attribs);
                img[0]->specmod().extra_attribs.merge(
                    img[2]->spec().extra_attribs);
            }
            return ok;
        });
        ok &= op() != 0;
    }
    (void)ok;
    return;  // FIXME: indicate error?
}



// --selectmip
static void
action_selectmip(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_selectmip, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    int miplevel = Strutil::from_string<int>(ot.express(argv[1]));

    ot.read();
    bool mipmapped = false;
    for (int s = 0, send = ot.curimg->subimages(); s < send; ++s)
        mipmapped |= (ot.curimg->miplevels(s) > 1);
    if (!mipmapped) {
        return;  // --selectmip on an unmipped image is a no-op
    }

    ImageRecRef newimg(new ImageRec(*ot.curimg, -1, miplevel, true, true));
    if (newimg->has_error()) {
        ot.error(command, newimg->geterror());
        return;
    }
    ot.curimg = newimg;
}



// --subimage
static void
action_select_subimage(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_select_subimage, argv))
        return;

    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options              = ot.extract_options(command);
    int subimage              = 0;
    std::string whichsubimage = ot.express(argv[1]);
    string_view w(whichsubimage);

    ot.read();
    if (Strutil::parse_int(w, subimage) && w.empty()) {
        // Subimage specification was an integer: treat as an index
        if (subimage < 0 || subimage >= ot.curimg->subimages()) {
            ot.errorfmt(command, "Invalid -subimage ({}): {} has {} subimage{}",
                        subimage, ot.curimg->name(), ot.curimg->subimages(),
                        ot.curimg->subimages() == 1 ? "" : "s");
            return;
        }
    } else {
        // The subimage specification wasn't an integer. Assume it's a name.
        subimage = -1;
        for (int i = 0, n = ot.curimg->subimages(); i < n; ++i) {
            string_view siname = ot.curimg->spec(i)->get_string_attribute(
                "oiio:subimagename");
            if (siname == whichsubimage) {
                subimage = i;
                break;
            }
        }
        if (subimage < 0) {
            ot.errorfmt(command,
                        "Invalid -subimage ({}): named subimage not found",
                        whichsubimage);
            return;
        }
    }

    if (ot.curimg->subimages() == 1 && subimage == 0)
        return;  // asking for the only subimage is a no-op

    if (options["delete"].get<int>()) {
        // Delete mode: remove the specified subimage
        ot.top()->erase_subimage(subimage);
    } else {
        // Select mode: select just the one specified subimage
        ImageRecRef A = ot.pop();
        ot.push(new ImageRec(*A, subimage, -1, true));
    }
}



// --sisplit
static void
action_subimage_split(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_subimage_split, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    ImageRecRef A = ot.pop();
    ot.read(A);

    // Push the individual subimages onto the stack
    for (int subimage = 0; subimage < A->subimages(); ++subimage)
        ot.push(new ImageRec(*A, subimage, -1, true));
}



// --layersplit
static void
action_layer_split(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_layer_split, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    ImageRecRef A = ot.pop();
    ot.read(A);

    // Split and push the individual channel-name-based layers onto the stack
    ImageSpec* spec = A->spec();
    int chbegin     = 0;
    std::vector<std::string> newchannelnames;
    for (size_t i = 0; i < spec->channelnames.size(); ++i) {
        // Parse full channel name to extract the layer name
        // and the actual channel name, which will be used for
        // renaming channels during the split
        // Examples:
        // chname = "R" -> layername = "", newchname = "R"
        // chname = "diffuse.G" -> layername = "diffuse", newchname = "G"
        const std::string& chname   = spec->channelnames[i];
        const auto parts            = Strutil::splits(chname, ".", 2);
        const std::string layername = (parts.size() < 2) ? "" : parts[0];
        const std::string newchname = (parts.size() < 2) ? parts[0] : parts[1];
        newchannelnames.push_back(newchname);

        bool pushlayer = false;
        if (i < spec->channelnames.size() - 1) {
            // Parse the layer name of the next channel,
            // a different value means that we will be processing
            // a new layer at the next iteration, therefore
            // we should push the current one on the stack
            const std::string& nextchname = spec->channelnames[i + 1];
            const auto nextparts          = Strutil::splits(nextchname, ".", 2);
            const std::string nextlayername = (nextparts.size() < 2)
                                                  ? ""
                                                  : nextparts[0];
            pushlayer                       = (nextlayername != layername);
        } else {
            // Force flag to true at last iteration so that
            // last layer is also pushed on the stack
            pushlayer = true;
        }

        if (pushlayer) {
            // Split the current layer by isolating its channels
            // in a new ImageBuf and renaming them, and store the
            // layer name in the oiio:subimagename metadata so we
            // can reuse it later (e.g for creating a multi-part image)
            const int chend = i + 1;
            ImageBufRef img(new ImageBuf());
            std::vector<int> channelorder(chend - chbegin);
            std::iota(channelorder.begin(), channelorder.end(), chbegin);
            ImageBufAlgo::channels(*img, (*A)(), chend - chbegin, channelorder,
                                   {}, newchannelnames);
            img->specmod().attribute("oiio:subimagename", layername);

            // Create corresponding ImageRec and push it on the stack
            ImageRecRef R(new ImageRec(img, true));
            ot.push(R);

            // Prepare processing of next layer
            chbegin = chend;
            newchannelnames.clear();
        }
    }
}



static void
action_subimage_append_n(Oiiotool& ot, int n, string_view command)
{
    std::vector<ImageRecRef> images(n);
    for (int i = n - 1; i >= 0; --i) {
        images[i] = ot.pop();
        ot.read(images[i]);  // necessary?
    }

    // Find the MIP levels in all the subimages of both A and B
    std::vector<int> allmiplevels;
    for (int i = 0; i < n; ++i) {
        ImageRecRef A = images[i];
        for (int s = 0; s < A->subimages(); ++s) {
            int miplevels = ot.allsubimages ? A->miplevels(s) : 1;
            allmiplevels.push_back(miplevels);
        }
    }

    // Create the replacement ImageRec
    ImageRecRef R(new ImageRec(images[0]->name(), (int)allmiplevels.size(),
                               allmiplevels));
    ot.push(R);

    // Subimage by subimage, MIP level by MIP level, copy
    int sub = 0;
    for (int i = 0; i < n; ++i) {
        ImageRecRef A = images[i];
        for (int s = 0; s < A->subimages(); ++s, ++sub) {
            for (int m = 0; m < allmiplevels[s]; ++m) {
                bool ok = (*R)(sub, m).copy((*A)(s, m));
                if (!ok) {
                    ot.error(command, (*R)(sub, m).geterror());
                    return;
                }
                // Update the IR's copy of the spec.
                R->update_spec_from_imagebuf(sub, m);
            }
            // For subimage append, preserve the notion of whether the
            // format is exactly as read from disk -- this is one of the few
            // operations for which it's true, since we are just appending
            // subimage, not modifying data or data format.
            (*R)[sub].was_direct_read((*A)[s].was_direct_read());
        }
    }
}



// --siappend
static void
action_subimage_append(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_subimage_append, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options = ot.extract_options(command);
    int n        = OIIO::clamp(options["n"].get<int>(2), 2,
                               int(ot.image_stack.size() + 1));

    action_subimage_append_n(ot, n, command);
}



// --siappendall
static void
action_subimage_append_all(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_subimage_append_all, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    action_subimage_append_n(ot, int(ot.image_stack.size() + 1), command);
}



// --thumbnail-get
static void
action_thumbnail_get(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_thumbnail_get, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // Parse options from the command token, e.g.
    //     --thumbnail-get:fail=0:index=0
    auto options = ot.extract_options(command);

    bool fail_if_missing = options.get_int("fail", 1);
    int index            = options.get_int("index", 0);

    // The current ImageInput/ImageBuf API exposes only a single (primary)
    // thumbnail per subimage. Formats that embed multiple thumbnails at
    // different resolutions (such as some camera raw formats) cannot yet be
    // distinguished, so for now only index 0 is valid. The `:index=` modifier
    // reserves the syntax for when the API gains multi-thumbnail support.
    // See https://github.com/AcademySoftwareFoundation/OpenImageIO/issues/4888
    if (index != 0) {
        ot.errorfmt(command,
                    "Thumbnail index {} is not available; only the primary "
                    "thumbnail (index 0) is currently supported",
                    index);
        return;
    }

    ImageRecRef A = ot.pop();
    // Reading the spec loads the thumbnail, so avoid a full pixel read.
    if (!ot.read_nativespec(A)) {
        ot.push(A);
        return;
    }

    auto thumb = (*A)(0, 0).get_thumbnail();
    if (!thumb || !thumb->initialized()) {
        if (fail_if_missing) {
            ot.errorfmt(command, "Image \"{}\" has no thumbnail", A->name());
            ot.push(A);
            return;
        }

        // synthesize a thumbnail from the image itself
        if (!ot.read(A)) {
            ot.push(A);
            return;
        }

        const ImageBuf& Aib((*A)(0, 0));
        const ImageSpec& Aspec(Aib.spec());

        // Fit inside a box, preserving aspect ratio and never enlarging
        const int boxsize = 256;
        int longest = std::max(1,
                               std::max(Aspec.full_width, Aspec.full_height));
        float scale = std::min(1.0f, float(boxsize) / float(longest));
        int tw      = std::max(1, int(roundf(Aspec.full_width * scale)));
        int th      = std::max(1, int(roundf(Aspec.full_height * scale)));
        ROI roi(0, tw, 0, th, 0, 1, 0, Aspec.nchannels);

        ImageBufRef generated(new ImageBuf(
            ImageBufAlgo::resize(Aib, ImageBufAlgo::KWArgs(), roi)));

        if (generated->has_error()) {
            ot.errorfmt(command, "{}", generated->geterror());
            ot.push(A);
            return;
        }
        ot.push(new ImageRec(generated, false));
        return;
    }
    ot.push(new ImageRec(ImageBufRef(new ImageBuf(*thumb)), false));
}



// --thumbnail-set
static void
action_thumbnail_set(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_thumbnail_set, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // Top image is the thumbnail
    ImageRecRef T = ot.pop();
    ImageRecRef A = ot.pop();
    if (!ot.read(T) || !ot.read(A)) {
        ot.push(A);
        ot.push(T);
        return;
    }

    const ImageBuf& thumb((*T)(0, 0));
    if (!thumb.initialized()) {
        ot.errorfmt(command, "Thumbnail image \"{}\" is empty", T->name());
        ot.push(A);
        ot.push(T);
        return;
    }

    (*A)(0, 0).set_thumbnail(thumb);
    A->update_spec_from_imagebuf(0, 0);
    ot.push(A);
}



// --colorcount
static void
action_colorcount(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_colorcount, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view colorarg = ot.express(argv[1]);

    ot.read();
    ImageBuf& Aib((*ot.curimg)(0, 0));
    int nchannels = Aib.nchannels();

    // We assume ';' to split, but for the sake of some command shells,
    // that use ';' as a command separator, also accept ":".
    std::vector<float> colorvalues;
    std::vector<std::string> colorstrings;
    if (colorarg.find(':') != colorarg.npos)
        Strutil::split(colorarg, colorstrings, ":");
    else
        Strutil::split(colorarg, colorstrings, ";");
    int ncolors = (int)colorstrings.size();
    for (int col = 0; col < ncolors; ++col) {
        std::vector<float> color(nchannels, 0.0f);
        Strutil::extract_from_list_string(color, colorstrings[col], ",");
        for (int c = 0; c < nchannels; ++c)
            colorvalues.push_back(c < (int)color.size() ? color[c] : 0.0f);
    }

    std::vector<float> eps(nchannels, 0.001f);
    auto options = ot.extract_options(command);
    Strutil::extract_from_list_string(eps, options.get_string("eps"));

    imagesize_t* count = OIIO_ALLOCA(imagesize_t, ncolors);
    bool ok = ImageBufAlgo::color_count((*ot.curimg)(0, 0), count, ncolors,
                                        colorvalues, eps);
    if (ok) {
        for (int col = 0; col < ncolors; ++col)
            Strutil::print("{:8}  {}\n", count[col], colorstrings[col]);
    } else {
        ot.error(command, (*ot.curimg)(0, 0).geterror());
    }

    ot.printed_info = true;
}



// --rangecheck
static void
action_rangecheck(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_rangecheck, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view lowarg  = ot.express(argv[1]);
    string_view higharg = ot.express(argv[2]);

    ot.read();
    ImageBuf& Aib((*ot.curimg)(0, 0));
    int nchannels = Aib.nchannels();

    std::vector<float> low(nchannels, 0.0f), high(nchannels, 1.0f);
    Strutil::extract_from_list_string(low, lowarg, ",");
    Strutil::extract_from_list_string(high, higharg, ",");

    imagesize_t lowcount = 0, highcount = 0, inrangecount = 0;
    bool ok = ImageBufAlgo::color_range_check((*ot.curimg)(0, 0), &lowcount,
                                              &highcount, &inrangecount, low,
                                              high);
    if (ok) {
        Strutil::print("{:8}  < {}\n", lowcount, lowarg);
        Strutil::print("{:8}  > {}\n", highcount, higharg);
        Strutil::print("{:8}  within range\n", inrangecount);
    } else {
        ot.error(command, (*ot.curimg)(0, 0).geterror());
    }
    ot.printed_info = true;
}



// --diff
static void
action_diff(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_diff, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    int ret = ot.do_action_diff(ot.image_stack.back(), ot.curimg, ot);
    if (ret != DiffErrOK && ret != DiffErrWarn)
        ot.return_value = EXIT_FAILURE;

    if (ret != DiffErrOK && ret != DiffErrWarn && ret != DiffErrFail)
        ot.error(command, "Diff failed");

    ot.printed_info = true;  // because taking the diff has output
}



// --pdiff
static void
action_pdiff(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_pdiff, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    int ret = ot.do_action_diff(ot.image_stack.back(), ot.curimg, ot, 1);
    if (ret != DiffErrOK && ret != DiffErrWarn)
        ot.return_value = EXIT_FAILURE;

    if (ret != DiffErrOK && ret != DiffErrWarn && ret != DiffErrFail)
        ot.error(command, "Diff failed");

    ot.printed_info = true;  // because taking the diff has output
}



// --flipdiff
// See imagebufalgo_flip.cpp for both the underlying aglorithm code, as well
// as combined discussion of OIIO's implementation, both the IBA parts and the
// oiiotool parts.
static void
action_flipdiff(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_flipdiff, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // Pop reference (deeper) and test (top) images.
    ImageRecRef test = ot.pop();
    ImageRecRef ref  = ot.pop();
    ot.read(ref);
    ot.read(test);

    const ImageBuf& img_ref  = (*ref)(0, 0);
    const ImageBuf& img_test = (*test)(0, 0);

    // Parse options from the command token, e.g.
    //     --flipdiff:hdr=1:colormap=magma
    auto options = ot.extract_options(command);

    float fail_threshold = options.get_float("fail", 10.0f);
    bool do_passfail     = (fail_threshold < 10.0f);
    bool do_print        = options.get_int("print", 1);

    ImageBuf dst;
    bool ok = ImageBufAlgo::FLIP_diff(dst, img_ref, img_test, options);
    if (!ok) {
        ot.error(command, dst.geterror());
        return;
    }

    // Read stats from result metadata before any colormap step
    // (colormap creates a new ImageBuf that won't carry the attributes).
    const ImageSpec& spec(dst.spec());
    float meanerror     = spec.get_float_attribute("FLIP:meanerror");
    float maxerror      = spec.get_float_attribute("FLIP:maxerror");
    int maxx            = spec.get_int_attribute("FLIP:maxx");
    int maxy            = spec.get_int_attribute("FLIP:maxy");
    float startExposure = spec.get_float_attribute("FLIP:startExposure");
    float stopExposure  = spec.get_float_attribute("FLIP:stopExposure");
    int numExposures    = spec.get_int_attribute("FLIP:numExposures");

    // oiiotool convenience: if the user passed colormap=NAME, apply a
    // false-color visualization to the single-channel error map.
    string_view cmap = options.get_string("colormap", "");
    if (!cmap.empty()) {
        ImageBuf colorized;
        if (!ImageBufAlgo::color_map(colorized, dst, 0, cmap)) {
            ot.error(command, colorized.geterror());
            return;
        }
        dst = std::move(colorized);
        // Transfer the attribs to the color map
        dst.specmod().attribute("FLIP:meanerror", meanerror);
        dst.specmod().attribute("FLIP:maxerror", maxerror);
        dst.specmod().attribute("FLIP:maxx", maxx);
        dst.specmod().attribute("FLIP:maxy", maxy);
        if (numExposures) {
            dst.specmod().attribute("FLIP:startExposure", startExposure);
            dst.specmod().attribute("FLIP:stopExposure", stopExposure);
            dst.specmod().attribute("FLIP:numExposures", numExposures);
        }
    }

    // Print results unless the user specified --flipdiff:print=0
    if (do_print || do_passfail) {
        OIIO::print("FLIP diff of \"{}\" vs \"{}\"\n", ref->name(),
                    test->name());
        ot.printed_info = true;
    }
    if (do_print || (do_passfail && maxerror > fail_threshold)) {
        // Only print 4 significant digits -- there's not enough precision for
        // more than that to be meaningful.
        OIIO::print("  Mean FLIP error  = {:.4g}\n", meanerror);
        OIIO::print("  Max FLIP error   = {:.4g}", maxerror);
        if (maxerror != 0) {
            OIIO::print("  @ ({}, {})  values are ", maxx, maxy);
            for (int c = 0; c < img_ref.nchannels(); ++c)
                OIIO::print("{}{}", c ? ", " : "",
                            img_ref.getchannel(maxx, maxy, 0, c));
            OIIO::print(" vs ");
            for (int c = 0; c < img_test.nchannels(); ++c)
                OIIO::print("{}{}", c ? ", " : "",
                            img_test.getchannel(maxx, maxy, 0, c));
        }
        OIIO::print("\n");
        if (numExposures > 0) {
            OIIO::print("  Exposure range   = [{:.3g}, {:.3g}] ({} stops)\n",
                        startExposure, stopExposure, numExposures);
        }
        ot.printed_info = true;
    }

    // Push result error-map onto the stack.
    auto dstref = std::make_shared<ImageBuf>(std::move(dst));
    ot.push(new ImageRec(dstref, false));

    if (do_passfail) {
        if (maxerror > fail_threshold) {
            // ot.errorfmt("--flipdiff",
            //             "FAILURE: FLIP comparison max error {} > threshold {}",
            //             maxerror, fail_threshold);
            ot.return_value = EXIT_FAILURE;
            OIIO::print("FAILURE\n");
        } else {
            OIIO::print("PASS\n");
        }
        ot.printed_info = true;
    }
}



BINARY_IMAGE_OP(add, ImageBufAlgo::add);          // --add
BINARY_IMAGE_OP(sub, ImageBufAlgo::sub);          // --sub
BINARY_IMAGE_OP(mul, ImageBufAlgo::mul);          // --mul
BINARY_IMAGE_OP(div, ImageBufAlgo::div);          // --div
BINARY_IMAGE_OP(scale, ImageBufAlgo::scale);      // --scale
BINARY_IMAGE_OP(absdiff, ImageBufAlgo::absdiff);  // --absdiff

BINARY_IMAGE_COLOR_OP(addc, ImageBufAlgo::add, 0);          // --addc
BINARY_IMAGE_COLOR_OP(subc, ImageBufAlgo::sub, 0);          // --subc
BINARY_IMAGE_COLOR_OP(mulc, ImageBufAlgo::mul, 1);          // --mulc
BINARY_IMAGE_COLOR_OP(divc, ImageBufAlgo::div, 1);          // --divc
BINARY_IMAGE_COLOR_OP(absdiffc, ImageBufAlgo::absdiff, 0);  // --absdiffc
BINARY_IMAGE_COLOR_OP(powc, ImageBufAlgo::pow, 1.0f);       // --powc
BINARY_IMAGE_FLOAT_OP(saturate, ImageBufAlgo::saturate);    // --saturate

UNARY_IMAGE_OP(abs, ImageBufAlgo::abs);  // --abs

UNARY_IMAGE_OP(premult, ImageBufAlgo::premult);      // --premult
UNARY_IMAGE_OP(repremult, ImageBufAlgo::repremult);  // --repremult

// --unpremult
OIIOTOOL_OP(unpremult, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    if (img[1]->spec().get_int_attribute("oiio:UnassociatedAlpha")
        && img[1]->spec().alpha_channel >= 0) {
        ot.warning(
            op.opname(),
            "Image appears to already be unassociated alpha (un-premultiplied color), beware double unpremult.");
    }
    return ImageBufAlgo::unpremult(*img[0], *img[1]);
});


// --mad
OIIOTOOL_OP(mad, 3, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    return ImageBufAlgo::mad(*img[0], *img[1], *img[2], *img[3]);
});


// --invert
OIIOTOOL_OP(invert, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    ROI roi = img[1]->roi();
    // By default, we only invert channels [0,3), but this can be overridden
    // by optional modifiers chbegin and chend.
    int chbegin = op.options().get_int("chbegin", 0);
    int chend   = op.options().get_int("chend", std::min(3, roi.chend));
    if (roi.chbegin < chbegin || roi.chend > chend) {
        // If the image has channels beyond what we're inverting, start by
        // copying src to dst first, so we dont lose channels along the way.
        ImageBufAlgo::copy(*img[0], *img[1]);
    }
    roi.chbegin = chbegin;
    roi.chend   = chend;
    return ImageBufAlgo::invert(*img[0], *img[1], roi, 0);
});



// --noise
OIIOTOOL_OP(noise, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    img[0]->copy(*img[1]);
    std::string type = op.options().get_string("type", "gaussian");
    float A          = 0.0f;
    float B          = 0.1f;
    if (type == "gaussian") {
        A = op.options().get_float("mean", 0.0f);
        B = op.options().get_float("stddev", 0.1f);
    } else if (type == "white" || type == "uniform") {
        A = op.options().get_float("min", 0.0f);
        B = op.options().get_float("max", 0.1f);
    } else if (type == "salt") {
        A = op.options().get_float("value", 0.0f);
        B = op.options().get_float("portion", 0.01f);
    } else {
        ot.errorfmt(op.opname(), "Unknown noise type \"{}\"", type);
        return false;
    }
    bool mono     = op.options().get_int("mono");
    int seed      = op.options().get_int("seed");
    int nchannels = op.options().get_int("nchannels", 10000);
    ROI roi       = img[0]->roi();
    roi.chend     = std::min(roi.chend, nchannels);
    return ImageBufAlgo::noise(*img[0], type, A, B, mono, seed, roi);
});



// --chsum
OIIOTOOL_OP(chsum, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    std::vector<float> weight(img[1]->nchannels(), 1.0f);
    Strutil::extract_from_list_string(weight,
                                      op.options().get_string("weight"));
    return ImageBufAlgo::channel_sum(*img[0], *img[1], weight);
});



// --colormap
OIIOTOOL_OP(colormap, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    if (isalpha(op.args(1)[0])) {
        // Named color map
        return ImageBufAlgo::color_map(*img[0], *img[1], -1, op.args(1),
                                       img[1]->roi(), 0);
    } else {
        // Values
        std::vector<float> knots;
        int n = Strutil::extract_from_list_string(knots, op.args(1));
        return ImageBufAlgo::color_map(*img[0], *img[1], -1, n / 3, 3, knots,
                                       img[1]->roi(), 0);
    }
});


// This will eventually be turned into an IBA freestanding function. But while
// we experiment with it, we'll leave it temporarily internal to oiiotool so
// that we don't break compatibility repeatedly.

namespace ImageBufAlgox {
ImageBuf cryptomatte_colors(const ImageBuf& src, span<const int> channelset,
                            ROI roi = {}, int nthreads = 0);
bool cryptomatte_colors(ImageBuf& dst, const ImageBuf& src,
                        span<const int> channelset, ROI roi = {},
                        int nthreads = 0);
}  // namespace ImageBufAlgox

// The cryptomatte spec can be found here:
// https://github.com/Psyop/Cryptomatte
//
// The short version is that there will be a series of channels called
// `<layer>00.red`, `<layer>00.green`, `<layer>00.blue`, `<layer>00.alpha`,
// `<layer>01.red`, `<layer>01.green`, `<layer>01.blue`, `<layer>01.alpha`,
// etc. Each successive pair of these `float`-valued channels (e.g.
// layer00.red + layer00.green, then the next pair is layer00.blue +
// layer00.alpha) constists of (a) the first channel being the float bitcast
// of the 32-bit object ID, and (b) the second channel being the alpha
// coverage of how much that object is visible in the pixel.
//

template<class D, class S>
static bool
cryptomatte_colors_(ImageBuf& dst, const ImageBuf& src,
                    span<const int> channelset, ROI roi, int nthreads)
{
    OIIO::ImageBufAlgo::parallel_image(roi, nthreads, [&](ROI roi) {
        ImageBuf::Iterator<D> d(dst, roi);
        ImageBuf::ConstIterator<S> s(src, roi);
        int nids = int(channelset.size()) / 2;  // 2 chans per ID
        for (; !d.done(); ++d, ++s) {
            float pixel[4] = { 0, 0, 0, 0 };
            for (int id = 0; id < nids; ++id) {
                // Grab a pair of channels: encoded hash, coverage
                float en    = s[channelset[2 * id]];
                float alpha = s[channelset[2 * id + 1]];
                if (alpha < 1.0e-5 || en == 0)
                    continue;
                // fmix the hash to expand to all the bits and scramble
                uint32_t h = murmur::fmix(bitcast<uint32_t, float>(en));
                // For each of R,G,B, convert the hash to a float for that
                // channel, then rehash to make the next channel appear
                // decorrelated.
                pixel[0] += alpha * convert_type<uint32_t, float>(h);
                h = murmur::fmix(h + 0x0101);
                pixel[1] += alpha * convert_type<uint32_t, float>(h);
                h = murmur::fmix(h + 0x020000);
                pixel[2] += alpha * convert_type<uint32_t, float>(h);
                // h = murmur::fmix(h);  // no need to rehash for alpha
                pixel[3] += alpha;
            }
            for (int c = 0; c < 3; ++c)
                d[c] = pixel[c];
        }
    });
    return true;
}



bool
ImageBufAlgox::cryptomatte_colors(ImageBuf& dst, const ImageBuf& src,
                                  span<const int> channelset, ROI roi,
                                  int nthreads)
{
    // OIIO::pvt::LoggedTimer logtime("IBA::cryptomatte_colors");
    if (!roi.defined())
        roi = get_roi(src.spec());
    roi.chend = std::min(roi.chend, src.nchannels());
    // Always output a 3-channel image
    ROI dstroi     = roi;
    dstroi.chbegin = 0;
    dstroi.chend   = 3;
    if (!IBAprep(dstroi, &dst))
        return false;
    bool ok;
    OIIO_DISPATCH_COMMON_TYPES2(ok, "cryptomatte_colors", cryptomatte_colors_,
                                dst.spec().format, src.spec().format, dst, src,
                                channelset, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgox::cryptomatte_colors(const ImageBuf& src,
                                  span<const int> channelset, ROI roi,
                                  int nthreads)
{
    ImageBuf result;
    bool ok = cryptomatte_colors(result, src, channelset, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::cryptomatte_colors() error");
    return result;
}


// --cryptomatte-colors
class OpCryptomatteColors final : public OiiotoolOp {
public:
    std::string m_cmpattern;

    OpCryptomatteColors(Oiiotool& ot, string_view opname,
                        cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
        m_cmpattern = Strutil::fmt::format("{}[0-9][0-9]\\..*", args(1));
    }
    bool setup() override
    {
        int subimages = compute_subimages();
        std::vector<ImageSpec> newspecs(subimages);
        for (int s = 0; s < subimages; ++s) {
            ImageSpec& newspec(newspecs[s]);
            newspec           = *ir(1)->spec(s);
            newspec.nchannels = 3;
            newspec.set_format(newspec.format);
            newspec.default_channel_names();
        }
        for (int s = 0; s < subimages; ++s)
            (*ir(0))(s).reset(newspecs[s]);
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        const std::regex repattern(m_cmpattern);
        std::vector<int> channels;
        const ImageBuf& src(*img[1]);
        const ImageSpec& srcspec(src.spec());
        for (int c = 0; c < srcspec.nchannels; ++c) {
            std::string name = srcspec.channel_name(c);
            if (std::regex_match(name, repattern))
                channels.push_back(c);
        }
        return ImageBufAlgox::cryptomatte_colors(*img[0], src, channels);
    }
};

OP_CUSTOMCLASS(cryptomatte_colors, OpCryptomatteColors, 1);



UNARY_IMAGE_OP(flip, ImageBufAlgo::flip);            // --flip
UNARY_IMAGE_OP(flop, ImageBufAlgo::flop);            // --flop
UNARY_IMAGE_OP(rotate180, ImageBufAlgo::rotate180);  // --rotate180
UNARY_IMAGE_OP(rotate90, ImageBufAlgo::rotate90);    // --rotate90
UNARY_IMAGE_OP(rotate270, ImageBufAlgo::rotate270);  // --rotate270
UNARY_IMAGE_OP(transpose, ImageBufAlgo::transpose);  // --transpose



// --reorient
void
action_reorient(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_reorient, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // Make sure time in the rotate functions is charged to reorient
    bool old_enable_function_timing = ot.enable_function_timing;
    ot.enable_function_timing       = false;

    ImageRecRef A = ot.pop();
    ot.read(A);

    // See if any subimages need to be reoriented
    bool needs_reorient = false;
    for (int s = 0, subimages = A->subimages(); s < subimages; ++s) {
        int orientation = (*A)(s).orientation();
        needs_reorient |= (orientation != 1);
    }

    if (needs_reorient) {
        ImageRecRef R(
            new ImageRec("reorient", ot.allsubimages ? A->subimages() : 1));
        ot.push(R);
        for (int s = 0, subimages = R->subimages(); s < subimages; ++s) {
            ImageBufAlgo::reorient((*R)(s), (*A)(s));
            R->update_spec_from_imagebuf(s);
        }
    } else {
        // No subimages need modification, just leave the whole thing in
        // place.
        ot.push(A);
    }

    ot.enable_function_timing = old_enable_function_timing;
}



// --rotate
OIIOTOOL_OP(rotate, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    float angle            = Strutil::from_string<float>(op.args(1));
    std::string filtername = op.options()["filter"];
    bool highlightcomp     = op.options().get_int("highlightcomp");
    bool recompute_roi     = op.options().get_int("recompute_roi");
    std::string cent       = op.options()["center"];
    string_view center(cent);
    float cx = 0.0f;
    float cy = 0.0f;
    if (center.size() && Strutil::parse_float(center, cx)
        && Strutil::parse_char(center, ',')
        && Strutil::parse_float(center, cy)) {
        // center supplied
    } else {
        ROI src_roi_full = img[1]->roi_full();
        cx               = 0.5f * (src_roi_full.xbegin + src_roi_full.xend);
        cy               = 0.5f * (src_roi_full.ybegin + src_roi_full.yend);
    }
    bool ok = true;
    ImageBuf tmpimg;
    ImageBuf* src = img[1];
    if (highlightcomp) {
        // If the caller requested highlight compensation for an HDR image to
        // prevent ringing artifacts, we make a temporary image with the
        // reduced-contrast data.
        ok &= ImageBufAlgo::rangecompress(tmpimg, *src);
        src = &tmpimg;
    }
    ok &= ImageBufAlgo::rotate(*img[0], *src, angle * float(M_PI / 180.0), cx,
                               cy, filtername, 0.0f, recompute_roi);
    if (highlightcomp && ok) {
        // re-expand the range in place
        ok &= ImageBufAlgo::rangeexpand(*img[0], *img[0]);
    }
    return ok;
});



// --warp
OIIOTOOL_OP(warp, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    std::string filtername = op.options()["filter"];
    bool highlightcomp     = op.options().get_int("highlightcomp");
    bool recompute_roi     = op.options().get_int("recompute_roi");
    std::string wrapname   = op.options().get_string("wrap", "default");
    std::vector<float> M(9);
    if (Strutil::extract_from_list_string(M, op.args(1)) != 9) {
        ot.error(op.opname(),
                 "expected 9 comma-separated floats to form a 3x3 matrix");
        return false;
    }
    bool ok = true;
    ImageBuf tmpimg;
    ImageBuf* src = img[1];
    if (highlightcomp) {
        // If the caller requested highlight compensation for an HDR image to
        // prevent ringing artifacts, we make a temporary image with the
        // reduced-contrast data.
        ok &= ImageBufAlgo::rangecompress(tmpimg, *src);
        src = &tmpimg;
    }
    ok &= ImageBufAlgo::warp(*img[0], *src, *(Imath::M33f*)&M[0],
                             { { "filtername", filtername },
                               { "recompute_roi", int(recompute_roi) },
                               { "wrap", wrapname } });
    if (highlightcomp && ok) {
        // re-expand the range in place
        ok &= ImageBufAlgo::rangeexpand(*img[0], *img[0]);
    }
    return ok;
});

// --demosaic
OIIOTOOL_OP(demosaic, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    ParamValueList list;
    const std::vector<std::string> keys = { "pattern", "algorithm", "layout",
                                            "white_balance_mode" };
    for (const auto& key : keys) {
        auto iter = op.options().find(key);
        if (iter != op.options().cend()) {
            list.push_back(iter[0]);
        }
    }

    std::string wb = op.options()["white_balance"];
    string_view str(wb);
    float f3[3];
    float f4[4];
    if (Strutil::parse_values(str, "", f4, ",") && str.empty()) {
        ParamValue pv("white_balance", TypeFloat, 4, make_cspan(f4));
        list.push_back(pv);
    } else if (Strutil::parse_values(str, "", f3, ",") && str.empty()) {
        ParamValue pv("white_balance", TypeFloat, 3, make_cspan(f3));
        list.push_back(pv);
    }

    return ImageBufAlgo::demosaic(*img[0], *img[1], KWArgs(list));
});


// --decorrstretch
OIIOTOOL_OP(decorrstretch, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    ParamValueList list;
    const std::vector<std::string> keys = { "firstchannel", "nchannels",
                                            "scale",        "sigma",
                                            "mean",         "percentile",
                                            "mode" };
    for (const auto& key : keys) {
        auto iter = op.options().find(key);
        if (iter != op.options().cend())
            list.push_back(iter[0]);
    }
    return ImageBufAlgo::decorr_stretch(*img[0], *img[1], KWArgs(list));
});


// --st_warp
OIIOTOOL_OP(st_warp, 2, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    std::string filtername = op.options()["filter"];
    int chan_s             = op.options().get_int("chan_s");
    int chan_t             = op.options().get_int("chan_t", 1);
    bool flip_s            = static_cast<bool>(op.options().get_int("flip_s"));
    bool flip_t            = static_cast<bool>(op.options().get_int("flip_t"));
    return ImageBufAlgo::st_warp(*img[0], *img[1], *img[2], filtername, 0.0f,
                                 chan_s, chan_t, flip_s, flip_t);
});



// --cshift
OIIOTOOL_OP(cshift, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    int xyz[3] = { 0, 0, 0 };
    if (!(Strutil::scan_values(op.args(1), "", span<int>(xyz, 3))
          || Strutil::scan_values(op.args(1), "", span<int>(xyz, 2)))) {
        ot.errorfmt(op.opname(), "Invalid shift offset '{}'", op.args(1));
        return false;
    }
    return ImageBufAlgo::circular_shift(*img[0], *img[1], xyz[0], xyz[1],
                                        xyz[2]);
});



// --pop
static void
action_pop(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.pop();
}



// --popbottom
static void
action_popbottom(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.popbottom();
}



// --dup
static void
action_dup(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.push(ot.curimg);
}



// --swap
static void
action_swap(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    if (ot.image_stack.size() < 1) {
        ot.error(command, "requires at least two loaded images");
        return;
    }
    ImageRecRef B(ot.pop());
    ImageRecRef A(ot.pop());
    ot.push(B);
    ot.push(A);
}



// --stackreverse
static void
action_stackreverse(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    if (!ot.curimg) {
        ot.error(command, "requires at least one loaded images");
        return;
    }
    if (ot.image_stack.empty())
        return;  // only curimg -- reversing does nothing
    ot.image_stack.push_back(ot.curimg);
    std::reverse(ot.image_stack.begin(), ot.image_stack.end());
    ot.curimg = ot.image_stack.back();
    ot.image_stack.pop_back();
}



// --stackextract
static void
action_stackextract(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    string_view command = ot.express(argv[0]);
    int index           = Strutil::stoi(ot.express(argv[1]));
    if (index < 0 || index >= ot.image_stack_depth()) {
        ot.errorfmt(command, "index {} out of range for stack depth {}", index,
                    ot.image_stack_depth());
        return;
    }
    if (ot.image_stack.empty())
        return;  // only curimg -- extract does nothing
    ot.image_stack.push_back(ot.curimg);
    // Transform the index to the index of the stack data structure
    index = int(ot.image_stack.size()) - 1 - index;
    // Copy that item for safe keeping
    ImageRecRef newtop = ot.image_stack[index];
    // Remove it from the stack
    ot.image_stack.erase(ot.image_stack.begin() + size_t(index));
    // Now put it back on the top
    ot.curimg = newtop;
}



// --stackclear
static void
action_stackclear(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.image_stack.clear();
    ot.curimg = ImageRecRef();
}



// --create
static void
action_create(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 3);
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options     = ot.extract_options(command);
    string_view size = ot.express(argv[1]);
    int nchans       = Strutil::from_string<int>(ot.express(argv[2]));
    if (nchans < 1 || nchans > 1024) {
        ot.warningfmt(argv[0], "Invalid number of channels: {}", nchans);
        nchans = 3;
    }
    ImageSpec spec(64, 64, nchans,
                   TypeDesc(options["type"].as_string("float")));
    ot.adjust_geometry(argv[0], spec.width, spec.height, spec.x, spec.y, size);
    spec.full_x      = spec.x;
    spec.full_y      = spec.y;
    spec.full_z      = spec.z;
    spec.full_width  = spec.width;
    spec.full_height = spec.height;
    spec.full_depth  = spec.depth;
    ImageRecRef img(new ImageRec("new", spec, ot.imagecache));
    // No need to zero, the allocation of the IB in the call above it will
    // automatically zero it.
    // bool ok = ImageBufAlgo::zero((*img)());
    // if (!ok)
    //     ot.error(command, (*img)().geterror());
    if (ot.curimg)
        ot.image_stack.push_back(ot.curimg);
    ot.curimg = img;
}



// --pattern
static void
action_pattern(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 4);
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options        = ot.extract_options(command);
    std::string pattern = ot.express(argv[1]);
    std::string size    = ot.express(argv[2]);
    int nchans          = Strutil::from_string<int>(ot.express(argv[3]));
    if (nchans < 1 || nchans > 1024) {
        ot.warningfmt(argv[0], "Invalid number of channels: {}", nchans);
        nchans = 3;
    }
    ImageSpec spec(64, 64, nchans,
                   TypeDesc(options["type"].as_string("float")));
    ot.adjust_geometry(argv[0], spec.width, spec.height, spec.x, spec.y, size);
    spec.full_x      = spec.x;
    spec.full_y      = spec.y;
    spec.full_z      = spec.z;
    spec.full_width  = spec.width;
    spec.full_height = spec.height;
    spec.full_depth  = spec.depth;
    ImageRecRef img(new ImageRec("new", spec, ot.imagecache));
    ot.push(img);
    ImageBuf& ib((*img)());
    bool ok = true;
    if (Strutil::iequals(pattern, "black")) {
        ok = ImageBufAlgo::zero(ib);
    } else if (Strutil::istarts_with(pattern, "constant")) {
        auto options = ot.extract_options(pattern);
        std::vector<float> fill(nchans, 1.0f);
        Strutil::extract_from_list_string(fill, options.get_string("color"));
        ok = ImageBufAlgo::fill(ib, fill);
    } else if (Strutil::istarts_with(pattern, "fill")) {
        auto options = ot.extract_options(pattern);
        std::vector<float> topleft(nchans, 1.0f);
        std::vector<float> topright(nchans, 1.0f);
        std::vector<float> bottomleft(nchans, 1.0f);
        std::vector<float> bottomright(nchans, 1.0f);
        if (Strutil::extract_from_list_string(topleft,
                                              options.get_string("topleft"))
            && Strutil::extract_from_list_string(topright,
                                                 options.get_string("topright"))
            && Strutil::extract_from_list_string(bottomleft, options.get_string(
                                                                 "bottomleft"))
            && Strutil::extract_from_list_string(
                bottomright, options.get_string("bottomright"))) {
            ok = ImageBufAlgo::fill(ib, topleft, topright, bottomleft,
                                    bottomright);
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("top"))
                   && Strutil::extract_from_list_string(
                       bottomleft, options.get_string("bottom"))) {
            ok = ImageBufAlgo::fill(ib, topleft, bottomleft);
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("left"))
                   && Strutil::extract_from_list_string(
                       topright, options.get_string("right"))) {
            ok = ImageBufAlgo::fill(ib, topleft, topright, topleft, topright);
        } else if (Strutil::extract_from_list_string(
                       topleft, options.get_string("color"))) {
            ok = ImageBufAlgo::fill(ib, topleft);
        }
    } else if (Strutil::istarts_with(pattern, "checker")) {
        auto options = ot.extract_options(pattern);
        int width    = options.get_int("width", 8);
        int height   = options.get_int("height", width);
        int depth    = options.get_int("depth", width);
        std::vector<float> color1(nchans, 0.0f);
        std::vector<float> color2(nchans, 1.0f);
        Strutil::extract_from_list_string(color1, options.get_string("color1"));
        Strutil::extract_from_list_string(color2, options.get_string("color2"));
        ok = ImageBufAlgo::checker(ib, width, height, depth, color1, color2, 0,
                                   0, 0);
    } else if (Strutil::istarts_with(pattern, "noise")) {
        auto options     = ot.extract_options(pattern);
        std::string type = options.get_string("type", "gaussian");
        float A = 0, B = 1;
        if (type == "gaussian") {
            A = options.get_float("mean", 0.5f);
            B = options.get_float("stddev", 0.1f);
        } else if (type == "white" || type == "uniform" || type == "blue") {
            A = options.get_float("min", 0.5f);
            B = options.get_float("max", 1.0f);
        } else if (type == "salt") {
            A = options.get_float("value", 0.01f);
            B = options.get_float("portion", 0.0f);
        } else {
            ot.errorfmt(command, "Unknown noise type \"{}\"", type);
            ok = false;
        }
        bool mono = options.get_int("mono");
        int seed  = options.get_int("seed");
        ImageBufAlgo::zero(ib);
        if (ok)
            ok = ImageBufAlgo::noise(ib, type, A, B, mono, seed);
    } else {
        ok = ImageBufAlgo::zero(ib);
        ot.warningfmt(command, "Unknown pattern \"{}\"", pattern);
    }
    if (!ok)
        ot.error(command, ib.geterror());
}



// --kernel
OIIOTOOL_OP(kernel, 0, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view kernelname(op.args(1));
    string_view kernelsize(op.args(2));
    float w = 1.0f;
    float h = 1.0f;
    if (!scan_resolution(kernelsize, w, h))
        ot.errorfmt(op.opname(), "Unknown size {}", kernelsize);
    *img[0] = ImageBufAlgo::make_kernel(kernelname, w, h);
    return !img[0]->has_error();
});



// --capture
static void
action_capture(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

#ifdef USE_OPENCV
    auto options = ot.extract_options(command);
    int camera   = options.get_int("camera");
    ImageBuf ib  = ImageBufAlgo::capture_image(camera /*, TypeDesc::FLOAT*/);
    if (ib.has_error()) {
        ot.error(command, ib.geterror());
        return;
    }
#else
    ot.warning(command, "capture requires OpenCV support");
    ImageBuf ib(ImageSpec(640, 480, 3, TypeDesc::FLOAT));
#endif
    ImageRecRef img(new ImageRec("capture", ib.spec(), ot.imagecache));
    (*img)().copy(ib);
    ot.push(img);
}



// --crop
void
action_crop(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_crop, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view size = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A     = ot.curimg;
    bool crops_needed = false;
    int subimages     = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s, 0));
        int w = spec.width, h = spec.height, d = spec.depth;
        int x = spec.x, y = spec.y, z = spec.z;
        ot.adjust_geometry(argv[0], w, h, x, y, size);
        crops_needed |= (w != spec.width || h != spec.height || d != spec.depth
                         || x != spec.x || y != spec.y || z != spec.z);
    }

    if (crops_needed) {
        ot.pop();
        ImageRecRef R(new ImageRec(A->name(), subimages));
        ot.push(R);
        for (int s = 0; s < subimages; ++s) {
            ImageSpec& spec(*A->spec(s, 0));
            int w = spec.width, h = spec.height, d = spec.depth;
            int x = spec.x, y = spec.y, z = spec.z;
            ot.adjust_geometry(argv[0], w, h, x, y, size);
            const ImageBuf& Aib((*A)(s, 0));
            ImageBuf& Rib((*R)(s, 0));
            ROI roi = Aib.roi();
            if (w != spec.width || h != spec.height || d != spec.depth
                || x != spec.x || y != spec.y || z != spec.z) {
                roi = ROI(x, x + w, y, y + h, z, z + d);
            }
            bool ok = ImageBufAlgo::crop(Rib, Aib, roi);
            if (!ok) {
                ot.error(command, Rib.geterror());
                break;
            }
            R->update_spec_from_imagebuf(s, 0);
        }
    }
}



// --croptofull
void
action_croptofull(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_croptofull, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A     = ot.curimg;
    int subimages     = allsubimages ? A->subimages() : 1;
    bool crops_needed = false;
    for (int s = 0; s < subimages; ++s) {
        crops_needed |= ((*A)(s).roi() != (*A)(s).roi_full());
    }

    if (crops_needed) {
        ot.pop();
        ImageRecRef R(new ImageRec(A->name(), subimages));
        ot.push(R);
        for (int s = 0; s < subimages; ++s) {
            const ImageBuf& Aib((*A)(s, 0));
            ImageBuf& Rib((*R)(s, 0));
            ROI roi = (Aib.roi() != Aib.roi_full()) ? Aib.roi_full()
                                                    : Aib.roi();
            bool ok = ImageBufAlgo::crop(Rib, Aib, roi);
            if (!ok) {
                ot.error(command, Rib.geterror());
                break;
            }
            R->update_spec_from_imagebuf(s, 0);
        }
    }
}



// Even though OpenEXR technically allows each "part" (what we call a
// subimage) to have a different data window, it seems that many apps
// get flummoxed by such input files, so for their sake we ensure that
// all parts share a single data window. This helper function computes
// a shared nonzero region for all subimages of A.
static ROI
nonzero_region_all_subimages(ImageRecRef A)
{
    ROI nonzero_region;
    for (int s = 0; s < A->subimages(); ++s) {
        ROI roi = ImageBufAlgo::nonzero_region((*A)(s));
        if (roi.npixels() != 0) {
            nonzero_region = roi_union(nonzero_region, roi);
        }
    }
    if (!nonzero_region.defined()) {
        // Special case -- all zero
        // fallback to 1st subimage ROI, but doctor to make it 1 zero pixel
        nonzero_region      = (*A)(0).roi();
        nonzero_region.xend = nonzero_region.xbegin + 1;
        nonzero_region.yend = nonzero_region.ybegin + 1;
        nonzero_region.zend = nonzero_region.zbegin + 1;
    }
    return nonzero_region;
}



// --trim
void
action_trim(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_trim, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // auto options      = ot.extract_options(command);
    // bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = A->subimages();

    // First, figure out shared nonzero region.
    ROI nonzero_region = nonzero_region_all_subimages(A);

    // Now see if any subimges need cropping
    bool crops_needed = false;
    for (int s = 0; s < subimages; ++s) {
        crops_needed |= (nonzero_region != (*A)(s).roi());
    }
    if (crops_needed) {
        ot.pop();
        ImageRecRef R(new ImageRec(A->name(), subimages));
        ot.push(R);
        for (int s = 0; s < subimages; ++s) {
            const ImageBuf& Aib((*A)(s, 0));
            ImageBuf& Rib((*R)(s, 0));
            bool ok = ImageBufAlgo::crop(Rib, Aib, nonzero_region);
            if (!ok) {
                ot.error(command, Rib.geterror());
                break;
            }
            R->update_spec_from_imagebuf(s, 0);
        }
    }
}



// --cut
void
action_cut(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_cut, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view size = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    // Operate on (and replace) the top-of-stack image
    ot.read();
    ImageRecRef A = ot.pop();

    // First, compute the specs of the cropped subimages
    int subimages = allsubimages ? A->subimages() : 1;
    std::vector<ImageSpec> newspecs(subimages);
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& newspec(newspecs[s]);
        newspec = *A->spec(s, 0);
        ot.adjust_geometry(argv[0], newspec.width, newspec.height, newspec.x,
                           newspec.y, size);
    }

    // Make a new ImageRec sized according to the new set of specs
    ImageRecRef R(new ImageRec(A->name(), subimages, {}, newspecs));

    // Crop and populate the new ImageRec
    for (int s = 0; s < subimages; ++s) {
        const ImageBuf& Aib((*A)(s, 0));
        ImageBuf& Rib((*R)(s, 0));
        ImageBufAlgo::cut(Rib, Aib, get_roi(newspecs[s]));
        ImageSpec& spec(*R->spec(s, 0));
        set_roi(spec, Rib.roi());
        set_roi_full(spec, Rib.roi());
    }

    R->metadata_modified(true);
    ot.push(R);
}



// --resample
class OpResample final : public OiiotoolOp {
public:
    OpResample(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
    }
    bool setup() override
    {
        int subimages = compute_subimages();
        bool nochange = true;
        std::vector<ImageSpec> newspecs(subimages);
        for (int s = 0; s < subimages; ++s) {
            // The size argument will be the resulting display (full) window.
            const ImageSpec& Aspec(*ir(1)->spec(s));
            ImageSpec& newspec(newspecs[s]);
            newspec = Aspec;
            ot.adjust_geometry(args(0), newspec.full_width, newspec.full_height,
                               newspec.full_x, newspec.full_y, args(1) /*size*/,
                               true);
            if (newspec.full_width == Aspec.full_width
                && newspec.full_height == Aspec.full_height) {
                continue;
            }
            nochange = false;
            // Compute corresponding data window.
            float wratio  = float(newspec.full_width) / float(Aspec.full_width);
            float hratio  = float(newspec.full_height)
                            / float(Aspec.full_height);
            newspec.x     = newspec.full_x
                            + int(floorf((Aspec.x - Aspec.full_x) * wratio));
            newspec.y     = newspec.full_y
                            + int(floorf((Aspec.y - Aspec.full_y) * hratio));
            newspec.width = int(ceilf(Aspec.width * wratio));
            newspec.height = int(ceilf(Aspec.height * hratio));
        }
        if (nochange) {
            // No change -- pop the temp result and restore the original
            ot.pop();
            ot.push(ir(1));
            return false;  // nothing more to do
        }
        for (int s = 0; s < subimages; ++s)
            (*ir(0))(s).reset(newspecs[s]);
        return true;
    }
    bool impl(span<ImageBuf*> img) override
    {
        bool interp = options().get_int("interp", 1);
        return ImageBufAlgo::resample(*img[0], *img[1], interp);
    }
};

OP_CUSTOMCLASS(resample, OpResample, 1);



// --resize
class OpResize final : public OiiotoolOp {
public:
    OpResize(Oiiotool& ot, string_view opname, cspan<const char*> argv)
        : OiiotoolOp(ot, opname, argv, 1)
    {
    }

    bool setup() override
    {
        from_geom = options("from");
        to_geom   = options("to");

        int subimages = compute_subimages();
        bool nochange = true;
        std::vector<ImageSpec> newspecs(subimages);
        M.resize(subimages);
        do_warp.resize(subimages, false);
        for (int s = 0; s < subimages; ++s) {
            const ImageSpec& Aspec(*ir(1)->spec(s));
            ImageSpec& newspec(newspecs[s]);
            do_warp[s] = compute_warp(Aspec, newspec, M[s]);
            if (!do_warp[s] && newspec.full_width == Aspec.full_width
                && newspec.full_height == Aspec.full_height) {
                // simple resize, but actually, there is no change
                continue;
            }
            nochange = false;
            if (!do_warp[s]) {
                // Not an identity transform
                // Compute corresponding data window.
                float wratio = float(newspec.full_width)
                               / float(Aspec.full_width);
                float hratio = float(newspec.full_height)
                               / float(Aspec.full_height);
                newspec.x    = newspec.full_x
                               + int(floorf((Aspec.x - Aspec.full_x) * wratio));
                newspec.y    = newspec.full_y
                               + int(floorf((Aspec.y - Aspec.full_y) * hratio));
                newspec.width  = int(ceilf(Aspec.width * wratio));
                newspec.height = int(ceilf(Aspec.height * hratio));
            }
        }
        if (nochange) {
            // No change necessary to any subimage -- pop the temp result and
            // restore the original.
            ot.pop();
            ot.push(ir(1));
            return false;  // nothing more to do
        }
        // If a change is necessary to any subimage, allocate the new images
        for (int s = 0; s < subimages; ++s)
            (*ir(0))(s).reset(newspecs[s]);
        return true;
    }

    bool impl(span<ImageBuf*> img) override
    {
        std::string filtername = options()["filter"];
        bool highlightcomp     = options().get_int("highlightcomp");
        bool edgeclamp         = options().get_int("edgeclamp");
        bool ok                = true;
        ImageBuf tmpimg;
        ImageBuf* src = img[1];
        if (highlightcomp) {
            // If the caller requested highlight compensation for an HDR image
            // to prevent ringing artifacts, we make a temporary image with
            // the reduced-contrast data.
            ok &= ImageBufAlgo::rangecompress(tmpimg, *src);
            src = &tmpimg;
        }
        if (do_warp[current_subimage()])
            ok &= ImageBufAlgo::warp(*img[0], *src, M[current_subimage()],
                                     { { "filtername", filtername },
                                       { "recompute_roi", 0 },
                                       { "edgeclamp", edgeclamp } });
        else
            ok &= ImageBufAlgo::resize(*img[0], *src,
                                       { { "filtername", filtername } },
                                       img[0]->roi());
        if (highlightcomp && ok) {
            // re-expand the range in place
            ok &= ImageBufAlgo::rangeexpand(*img[0], *img[0]);
        }
        return ok;
    }

    // Based on requested output size, and optional from/to geometry,
    // compute (a) newspec, the spec of the output, (b) M, the transformation
    // matrix. Return true if the transformation requires a full warp, or
    // false if a separable resize will do.
    bool compute_warp(const ImageSpec& Aspec, ImageSpec& newspec,
                      Imath::M33f& M)
    {
        newspec = Aspec;
        ot.adjust_geometry(args(0), newspec.full_width, newspec.full_height,
                           newspec.full_x, newspec.full_y, args(1) /*size*/,
                           true);
        newspec.x      = newspec.full_x;
        newspec.y      = newspec.full_y;
        newspec.width  = newspec.full_width;
        newspec.height = newspec.full_height;

        float from_x                   = Aspec.full_x;
        float from_y                   = Aspec.full_y;
        float from_w                   = Aspec.full_width;
        float from_h                   = Aspec.full_height;
        OIIO_MAYBE_UNUSED bool from_ok = true;
        if (from_geom.size())
            from_ok = ot.adjust_geometry(args(0), from_w, from_h, from_x,
                                         from_y, from_geom, false);
        float to_x                   = newspec.full_x;
        float to_y                   = newspec.full_y;
        float to_w                   = newspec.full_width;
        float to_h                   = newspec.full_height;
        OIIO_MAYBE_UNUSED bool to_ok = true;
        if (to_geom.size())
            to_ok = ot.adjust_geometry(args(0), to_w, to_h, to_x, to_y, to_geom,
                                       false);

        M.makeIdentity();
        M.translate(Imath::V2f(to_x, to_y));
        M.scale(Imath::V2f(to_w / from_w, to_h / from_h));
        M.translate(Imath::V2f(-from_x, -from_y));

        bool do_warp = (from_x != Aspec.full_x || from_y != Aspec.full_y
                        || from_w != Aspec.full_width
                        || from_h != Aspec.full_height);
        do_warp |= (to_x != newspec.full_x || to_y != newspec.full_y
                    || to_w != newspec.full_width
                    || to_h != newspec.full_height);

        // Safety valve: undocumented "forcewarp" lets you force a warp if
        // it's 1, force a resize if it's 0 (default behavior if it's not set).
        int forcewarp = options().get_int("forcewarp", -1);
        if (forcewarp >= 0)
            do_warp = (forcewarp > 0);

        if (ot.debug) {
            std::string filtername = options("filter");
            OIIO::print("  Resizing input {} full {}\n"
                        "   -> output {} full {}\n"
                        "     mapping {} to {}\n"
                        "     using {} filter\n",
                        Aspec.roi_full(), Aspec.roi(), newspec.roi(),
                        newspec.roi_full(),
                        format_resolution(from_w, from_h, from_x, from_y),
                        format_resolution(to_w, to_h, to_x, to_y),
                        (filtername.size() ? filtername.c_str() : "default"));
            OIIO::print("  M = {}\n", M);
            OIIO::print("  implementing with {}\n",
                        do_warp ? "warp" : "resize");
        }
        return do_warp;
    }

private:
    std::string from_geom, to_geom;
    std::vector<Imath::M33f> M;
    std::vector<bool> do_warp;
};

OP_CUSTOMCLASS(resize, OpResize, 1);



// --fit
static void
action_fit(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_fit, argv))
        return;
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);
    OTScopedTimer timer(ot, command);
    bool old_enable_function_timing = ot.enable_function_timing;
    ot.enable_function_timing       = false;

    // Examine the top of stack
    ImageRecRef A = ot.top();
    ot.read();
    const ImageSpec* Aspec = A->spec(0, 0);

    // Parse the user request for resolution to fit
    int fit_full_width  = Aspec->full_width;
    int fit_full_height = Aspec->full_height;
    int fit_full_x      = Aspec->full_x;
    int fit_full_y      = Aspec->full_y;
    ot.adjust_geometry(argv[0], fit_full_width, fit_full_height, fit_full_x,
                       fit_full_y, size, false);

    auto options           = ot.extract_options(command);
    bool allsubimages      = options.get_int("allsubimages", ot.allsubimages);
    bool pad               = options.get_int("pad");
    std::string filtername = options["filter"];
    std::string fillmode   = options["fillmode"];
    int exact              = options.get_int("exact");
    bool highlightcomp     = options.get_int("highlightcomp");

    int subimages = allsubimages ? A->subimages() : 1;
    ImageRecRef R(new ImageRec(A->name(), subimages));
    for (int s = 0; s < subimages; ++s) {
        ImageSpec newspec = (*A)(s, 0).spec();
        ImageBuf tmpimg;
        ImageBuf* src = &((*A)(s, 0));
        if (highlightcomp) {
            // If the caller requested highlight compensation for an HDR image
            // to prevent ringing artifacts, we make a temporary image with
            // the reduced-contrast data.
            ImageBufAlgo::rangecompress(tmpimg, *src);
            src = &tmpimg;
        }
        newspec.width = newspec.full_width = fit_full_width;
        newspec.height = newspec.full_height = fit_full_height;
        newspec.x = newspec.full_x = fit_full_x;
        newspec.y = newspec.full_y = fit_full_y;
        (*R)(s, 0).reset(newspec);
        ImageBufAlgo::fit((*R)(s, 0), *src,
                          { { "filtername", filtername },
                            { "fillmode", fillmode },
                            { "exact", exact } });
        if (highlightcomp) {
            // re-expand the range in place
            ImageBufAlgo::rangeexpand((*R)(s, 0), (*R)(s, 0));
        }
        R->update_spec_from_imagebuf(s, 0);
    }
    ot.pop();
    ot.push(R);
    A     = ot.top();
    Aspec = A->spec(0, 0);

    if (pad
        && (fit_full_width != Aspec->width
            || fit_full_height != Aspec->height)) {
        // Needs padding
        if (ot.debug)
            std::cout << "   performing a croptofull\n";
        const char* argv[] = { "croptofull" };
        action_croptofull(ot, argv);
    }

    ot.enable_function_timing = old_enable_function_timing;
}



// --pixelaspect
static void
action_pixelaspect(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_pixelaspect, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    bool old_enable_function_timing = ot.enable_function_timing;
    ot.enable_function_timing       = false;

    float new_paspect = Strutil::from_string<float>(ot.express(argv[1]));
    if (new_paspect <= 0.0f) {
        ot.errorfmt(command, "Invalid pixel aspect ratio '{:g}'", new_paspect);
        return;
    }

    // Examine the top of stack
    ImageRecRef A = ot.top();
    ot.read();
    const ImageSpec* Aspec = A->spec(0, 0);

    // Get the current pixel aspect ratio
    float paspect = Aspec->get_float_attribute("PixelAspectRatio", 1.0);
    if (paspect <= 0.0f) {
        ot.errorfmt(command, "Invalid pixel aspect ratio '{:g}' in source",
                    paspect);
        return;
    }

    // Get the current (if any) XResolution/YResolution attributes
    float xres = Aspec->get_float_attribute("XResolution", 0.0);
    float yres = Aspec->get_float_attribute("YResolution", 0.0);

    // Compute scaling factors and use action_resize to do the heavy lifting
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    float factor = paspect / new_paspect;
    if (factor > 1.0)
        scaleX = factor;
    else if (factor < 1.0)
        scaleY = 1.0 / factor;

    int scale_full_width  = (int)(Aspec->full_width * scaleX + 0.5f);
    int scale_full_height = (int)(Aspec->full_height * scaleY + 0.5f);

    float scale_xres = xres * scaleX;
    float scale_yres = yres * scaleY;

    auto options           = ot.extract_options(command);
    std::string filtername = options["filter"];
    bool highlightcomp     = options.get_int("highlightcomp");

    if (ot.debug) {
        std::cout << "Performing '" << command << "'\n";
        std::cout << "  Scaling "
                  << format_resolution(Aspec->full_width, Aspec->full_height,
                                       Aspec->full_x, Aspec->full_y)
                  << " with a pixel aspect ratio of " << paspect << " to "
                  << format_resolution(scale_full_width, scale_full_height,
                                       Aspec->full_x, Aspec->full_y)
                  << "\n";
    }
    if (scale_full_width != Aspec->full_width
        || scale_full_height != Aspec->full_height) {
        std::string resize  = format_resolution(scale_full_width,
                                                scale_full_height, 0, 0);
        std::string command = "resize";
        if (filtername.size())
            command += Strutil::fmt::format(":filter={}", filtername);
        if (highlightcomp)
            command += ":highlightcomp=1";
        const char* newargv[2] = { command.c_str(), resize.c_str() };
        action_resize(ot, newargv);
        A                         = ot.top();
        A->spec(0, 0)->full_width = (*A)(0, 0).specmod().full_width
            = scale_full_width;
        A->spec(0, 0)->full_height = (*A)(0, 0).specmod().full_height
            = scale_full_height;
        (*A)(0, 0).specmod().attribute("PixelAspectRatio", new_paspect);
        if (xres)
            (*A)(0, 0).specmod().attribute("XResolution", scale_xres);
        if (yres)
            (*A)(0, 0).specmod().attribute("YResolution", scale_yres);
        A->update_spec_from_imagebuf(0, 0);
        // Now A,Aspec are for the NEW resized top of stack
    }

    ot.enable_function_timing = old_enable_function_timing;
}



// --convolve
BINARY_IMAGE_OP(convolve, ImageBufAlgo::convolve);



// --blur
OIIOTOOL_OP(blur, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view kernopt = op.options().get_string("kernel", "gaussian");
    float w             = 1.0f;
    float h             = 1.0f;
    if (!scan_resolution(op.args(1), w, h))
        ot.errorfmt(op.opname(), "Unknown size {}", op.args(1));
    ImageBuf Kernel = ImageBufAlgo::make_kernel(kernopt, w, h);
    if (Kernel.has_error()) {
        ot.error(op.opname(), Kernel.geterror());
        return false;
    }
    return ImageBufAlgo::convolve(*img[0], *img[1], Kernel);
});



// --median
OIIOTOOL_OP(median, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (!scan_resolution(size, w, h))
        ot.errorfmt(op.opname(), "Unknown size {}", size);
    return ImageBufAlgo::median_filter(*img[0], *img[1], w, h);
});



// --dilate
OIIOTOOL_OP(dilate, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (!scan_resolution(size, w, h))
        ot.errorfmt(op.opname(), "Unknown size {}", size);
    return ImageBufAlgo::dilate(*img[0], *img[1], w, h);
});



// --erode
OIIOTOOL_OP(erode, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (!scan_resolution(size, w, h))
        ot.errorfmt(op.opname(), "Unknown size {}", size);
    return ImageBufAlgo::erode(*img[0], *img[1], w, h);
});



// --unsharp
OIIOTOOL_OP(unsharp, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    std::string kernel = op.options().get_string("kernel", "gaussian");
    float width        = op.options().get_float("width", 3.0f);
    float contrast     = op.options().get_float("contrast", 1.0f);
    float threshold    = op.options().get_float("threshold", 0.0f);
    return ImageBufAlgo::unsharp_mask(*img[0], *img[1], kernel, width, contrast,
                                      threshold);
});

UNARY_IMAGE_OP(laplacian, ImageBufAlgo::laplacian);       // --laplacian
UNARY_IMAGE_OP(fft, ImageBufAlgo::fft);                   // --fft
UNARY_IMAGE_OP(ifft, ImageBufAlgo::ifft);                 // --ifft
UNARY_IMAGE_OP(polar, ImageBufAlgo::complex_to_polar);    // --polar
UNARY_IMAGE_OP(unpolar, ImageBufAlgo::polar_to_complex);  // --unpolar



// --normalize
OIIOTOOL_OP(normalize, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    float inCenter  = op.options().get_float("incenter", 0.0f);
    float outCenter = op.options().get_float("outcenter", 0.0f);
    float scale     = op.options().get_float("scale", 1.0f);
    return ImageBufAlgo::normalize(*img[0], *img[1], inCenter, outCenter,
                                   scale);
});



// --fixnan
void
action_fixnan(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_fixnan, argv))
        return;
    string_view command  = ot.express(argv[0]);
    string_view modename = ot.express(argv[1]);
    OTScopedTimer timer(ot, command);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    NonFiniteFixMode mode = NONFINITE_BOX3;
    if (modename == "black")
        mode = NONFINITE_BLACK;
    else if (modename == "box3")
        mode = NONFINITE_BOX3;
    else if (modename == "error")
        mode = NONFINITE_ERROR;
    else {
        ot.warningfmt(argv[0],
                      "\"{}\" not recognized. Valid choices: black, box3, error",
                      modename);
    }
    ot.read();
    ImageRecRef A = ot.pop();
    ot.push(new ImageRec(*A, allsubimages ? -1 : 0, allsubimages ? -1 : 0, true,
                         false));
    imagesize_t total_nonfinite = 0;
    int subimages               = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        int miplevels = ot.curimg->miplevels(s);
        for (int m = 0; m < miplevels; ++m) {
            const ImageBuf& Aib((*A)(s, m));
            ImageBuf& Rib((*ot.curimg)(s, m));
            int num_nonfinite = 0;
            bool ok           = ImageBufAlgo::fixNonFinite(Rib, Aib, mode,
                                                           &num_nonfinite);
            if (!ok)
                ot.error(command, Rib.geterror());
            total_nonfinite += num_nonfinite;
        }
    }
    // Set user variable NONFINITE_COUNT to the number of pixels modified.
    ot.uservars["NONFINITE_COUNT"] = int(total_nonfinite);
}



// --fillholes
static void
action_fillholes(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_fillholes, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    // Read and copy the top-of-stack image
    ImageRecRef A(ot.pop());
    ot.read(A);
    ImageSpec spec = (*A)(0, 0).spec();
    set_roi(spec, roi_union(get_roi(spec), get_roi_full(spec)));
    ImageRecRef B(new ImageRec("filled", spec, ot.imagecache));
    ot.push(B);
    ImageBuf& Rib((*B)(0, 0));
    bool ok = ImageBufAlgo::fillholes_pushpull(Rib, (*A)(0, 0));
    if (!ok)
        ot.error(command, Rib.geterror());
}



// --paste
static void
action_paste(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(2, action_paste, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view position = ot.express(argv[1]);
    auto options         = ot.extract_options(command);
    bool do_merge        = options.get_int("mergeroi");
    bool merge_all       = options.get_int("all");

    // Because we're popping off the stack, the background image is going
    // to be FIRST, and the foreground-most image will be LAST.
    int ninputs = merge_all ? ot.image_stack_depth() : 2;
    std::vector<ImageRecRef> inputs;
    for (int i = 0; i < ninputs; ++i)
        inputs.push_back(ot.pop());

    // Take the metadata from the bg image
    ot.read(inputs.front());  // FIXME: find a way to avoid this
    ImageSpec spec = *(inputs.front()->spec());

    // Compute the merged ROIs
    ROI roi_all, roi_full_all;
    for (int i = 0; i < ninputs; ++i) {
        if (ot.debug && ninputs > 4)
            Strutil::print("    paste/1 {} (total time {}, mem {})\n", i,
                           Strutil::timeintervalformat(ot.total_runtime(), 2),
                           Strutil::memformat(Sysutil::memory_used()));
        ot.read(inputs[i]);
        roi_all      = roi_union(roi_all, inputs[i]->spec()->roi());
        roi_full_all = roi_union(roi_full_all, inputs[i]->spec()->roi_full());
    }

    // Create result image
    ROI roi      = do_merge ? roi_all : inputs[0]->spec()->roi();
    ROI roi_full = do_merge ? roi_full_all : inputs[0]->spec()->roi_full();
    spec.set_roi(roi);
    spec.set_roi_full(roi_full);
    ImageBufRef Rbuf(new ImageBuf(spec, InitializePixels::No));

    int x = 0, y = 0, z = 0;
    if (position == "-" || position == "auto") {
        // Come back to this
    } else if (!scan_offset(position, x, y)) {
        ot.errorfmt(command, "Invalid offset '{}'", position);
        return;
    }

    if (spec.deep) {
        // Special work for deep images -- to make it efficient, we need
        // to pre-allocate the fully merged set of samples.
        for (int i = 0; i < ninputs; ++i) {
            if (ot.debug && ninputs > 4)
                Strutil::print("    paste/2 {} (total time {}, mem {})\n", i,
                               Strutil::timeintervalformat(ot.total_runtime(),
                                                           2),
                               Strutil::memformat(Sysutil::memory_used()));
            ImageRecRef FG = inputs[i];
            if (!FG->spec()->deep)
                break;
            const ImageBuf& fg((*FG)());
            const DeepData* fgdd(fg.deepdata());
            for (ImageBuf::ConstIterator<float> r(fg); !r.done(); ++r) {
                int srcpixel = fg.pixelindex(r.x(), r.y(), r.z(), true);
                if (srcpixel < 0)
                    continue;  // Nothing in this pixel
                int dstpixel = Rbuf->pixelindex(r.x() + x, r.y() + y,
                                                r.z() + z);
                Rbuf->deepdata()->set_samples(dstpixel,
                                              fgdd->samples(srcpixel));
            }
        }
    }

    // Start by just copying the most background image
    bool ok = ImageBufAlgo::copy(*Rbuf, (*inputs[0])());
    if (!ok) {
        ot.error(command, Rbuf->geterror());
        return;
    }

    // Now paste the other images, back to front
    for (int i = 1; i < ninputs && ok; ++i) {
        if (ot.debug && ninputs > 4)
            Strutil::print("    paste/3 {} (total time {}, mem {})\n", i,
                           Strutil::timeintervalformat(ot.total_runtime(), 2),
                           Strutil::memformat(Sysutil::memory_used()));
        ImageRecRef FG = inputs[i];
        ok             = ImageBufAlgo::paste(*Rbuf, x, y, 0, 0, (*FG)());
        if (!ok)
            ot.error(command, Rbuf->geterror());
    }

    ImageRecRef R(new ImageRec(Rbuf, /*copy_pixels=*/false));
    ot.push(R);
}



// --pastemeta
OIIOTOOL_OP(
    pastemeta, 2,
    [&](OiiotoolOp& op, span<ImageBuf*> img) {
        std::string pattern = op.options("pattern");
        int merge           = op.options("merge").get<int>(0);

        *img[0] = *img[2];
        if (merge == 0) {
            // merge strategy 0 completely discards the existing metadata
            img[0]->specmod().extra_attribs.clear();
        }

        img[0]->merge_metadata(*img[1], merge > 1, pattern);
        return true;
    },
    // Special policy to ensure we consider all subimages of second image
    // (pixel inputs), not the default of considering only the first image.
    { { "subimage_policy", "last" } });



// --mosaic
static void
action_mosaic(Oiiotool& ot, cspan<const char*> argv)
{
    // Mosaic is tricky. We have to parse the argument before we know
    // how many images it wants to pull off the stack.
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view size = ot.express(argv[1]);
    int ximages = 0, yimages = 0;
    if (!scan_resolution(size, ximages, yimages) || ximages < 1
        || yimages < 1) {
        ot.errorfmt(command, "Invalid size '{}'", size);
        return;
    }
    int nimages = ximages * yimages;

    // Make the matrix complete with placeholder images
    ImageRecRef blank_img;
    while (ot.image_stack_depth() < nimages) {
        if (!blank_img) {
            ImageSpec blankspec(1, 1, 1, TypeDesc::UINT8);
            blank_img.reset(new ImageRec("blank", blankspec, ot.imagecache));
            ImageBufAlgo::zero((*blank_img)());
        }
        ot.push(blank_img);
    }

    int widest = 0, highest = 0, nchannels = 0;
    TypeDesc outtype = TypeUnknown;
    std::vector<ImageRecRef> images(nimages);
    for (int i = nimages - 1; i >= 0; --i) {
        ImageRecRef img = ot.pop();
        images[i]       = img;
        ot.read(img);
        widest    = std::max(widest, img->spec()->full_width);
        highest   = std::max(highest, img->spec()->full_height);
        nchannels = std::max(nchannels, img->spec()->nchannels);
        outtype   = TypeDesc::basetype_merge(outtype, img->spec()->format);
    }

    auto options = ot.extract_options(command);
    int pad      = options.get_int("pad");

    std::string fit = options["fit"];
    if (fit.size()) {
        int fitw = 0, fith = 0;
        if (scan_resolution(fit, fitw, fith) && fitw >= 1 && fith >= 1) {
            widest  = fitw;
            highest = fith;
            // Do the equivalent of a --fit on each image
            const char* fitargs[] = { "--fit:allsubimages=0:pad=1",
                                      fit.c_str() };
            for (int i = 0; i < nimages; ++i) {
                ot.push(images[i]);
                action_fit(ot, fitargs);
                images[i] = ot.pop();
            }
        }
    }

    ImageSpec Rspec(ximages * widest + (ximages - 1) * pad,
                    yimages * highest + (yimages - 1) * pad, nchannels,
                    outtype);
    ImageRecRef R(new ImageRec("mosaic", Rspec, ot.imagecache));
    ot.push(R);

    ImageBufAlgo::zero((*R)());
    for (int j = 0; j < yimages; ++j) {
        int y = j * (highest + pad);
        for (int i = 0; i < ximages; ++i) {
            int x   = i * (widest + pad);
            bool ok = ImageBufAlgo::paste((*R)(), x, y, 0, 0,
                                          (*images[j * ximages + i])(0));
            if (!ok) {
                ot.error(command, (*R)().geterror());
                return;
            }
        }
    }
}



// --over
BINARY_IMAGE_OP(over, ImageBufAlgo::over);



// --zover
OIIOTOOL_OP(zover, 2, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    bool zeroisinf = op.options().get_int("zeroisinf");
    return ImageBufAlgo::zover(*img[0], *img[1], *img[2], zeroisinf, ROI(), 0);
});



BINARY_IMAGE_OP(deepmerge, ImageBufAlgo::deep_merge);      // --deepmerge
BINARY_IMAGE_OP(deepholdout, ImageBufAlgo::deep_holdout);  // --deepholdout



// --deepen
OIIOTOOL_OP(deepen, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    float z = op.options().get_float("z", 1.0f);
    return ImageBufAlgo::deepen(*img[0], *img[1], z);
});


// --flatten
UNARY_IMAGE_OP(flatten, ImageBufAlgo::flatten);



static void
action_fill(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_fill, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    string_view size  = ot.express(argv[1]);
    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    // Read and copy the top-of-stack image
    ImageRecRef A(ot.pop());
    ot.read(A);
    ot.push(new ImageRec(*A, allsubimages ? -1 : 0, allsubimages ? -1 : 0,
                         /*writable=*/true, /*copy_pixels=*/true));

    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageBuf& Rib((*ot.curimg)(s));
        const ImageSpec& Rspec = Rib.spec();
        int w = Rib.spec().width, h = Rib.spec().height;
        int x = Rib.spec().x, y = Rib.spec().y;
        if (!ot.adjust_geometry(argv[0], w, h, x, y, size, true))
            continue;
        std::vector<float> topleft(Rspec.nchannels, 1.0f);
        std::vector<float> topright(Rspec.nchannels, 1.0f);
        std::vector<float> bottomleft(Rspec.nchannels, 1.0f);
        std::vector<float> bottomright(Rspec.nchannels, 1.0f);
        bool ok = true;
        if (Strutil::extract_from_list_string(topleft,
                                              options.get_string("topleft"))
            && Strutil::extract_from_list_string(topright,
                                                 options.get_string("topright"))
            && Strutil::extract_from_list_string(bottomleft, options.get_string(
                                                                 "bottomleft"))
            && Strutil::extract_from_list_string(
                bottomright, options.get_string("bottomright"))) {
            ok = ImageBufAlgo::fill(Rib, topleft, topright, bottomleft,
                                    bottomright, ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("top"))
                   && Strutil::extract_from_list_string(
                       bottomleft, options.get_string("bottom"))) {
            ok = ImageBufAlgo::fill(Rib, topleft, bottomleft,
                                    ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("left"))
                   && Strutil::extract_from_list_string(
                       topright, options.get_string("right"))) {
            ok = ImageBufAlgo::fill(Rib, topleft, topright, topleft, topright,
                                    ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(
                       topleft, options.get_string("color"))) {
            ok = ImageBufAlgo::fill(Rib, topleft, ROI(x, x + w, y, y + h));
        } else {
            ot.warning(command,
                       "No recognized fill parameters: filling with white.");
            ok = ImageBufAlgo::fill(Rib, topleft, ROI(x, x + w, y, y + h));
        }
        if (!ok) {
            ot.error(command, Rib.geterror());
            break;
        }
    }
}



BINARY_IMAGE_OP(max, ImageBufAlgo::max);            // --max
BINARY_IMAGE_COLOR_OP(maxc, ImageBufAlgo::max, 0);  // --maxc
UNARY_IMAGE_OP(maxchan, ImageBufAlgo::maxchan);     // --maxchan
BINARY_IMAGE_OP(min, ImageBufAlgo::min);            // --min
BINARY_IMAGE_COLOR_OP(minc, ImageBufAlgo::min, 0);  // --minc
UNARY_IMAGE_OP(minchan, ImageBufAlgo::minchan);     // --minchan



// --clamp
static void
action_clamp(Oiiotool& ot, cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_clamp, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ImageRecRef A = ot.pop();
    ot.read(A);
    int subimages = allsubimages ? A->subimages() : 1;
    ImageRecRef R(new ImageRec(*A, allsubimages ? -1 : 0, allsubimages ? -1 : 0,
                               true /*writable*/, false /*copy_pixels*/));
    ot.push(R);
    for (int s = 0; s < subimages; ++s) {
        int nchans      = (*R)(s, 0).nchannels();
        const float big = std::numeric_limits<float>::max();
        std::vector<float> min(nchans, -big);
        std::vector<float> max(nchans, big);
        Strutil::extract_from_list_string(min, options.get_string("min"));
        Strutil::extract_from_list_string(max, options.get_string("max"));
        bool clampalpha01 = options.get_int("clampalpha");

        for (int m = 0, miplevels = R->miplevels(s); m < miplevels; ++m) {
            ImageBuf& Rib((*R)(s, m));
            ImageBuf& Aib((*A)(s, m));
            bool ok = ImageBufAlgo::clamp(Rib, Aib, min, max, clampalpha01);
            if (!ok) {
                ot.error(command, Rib.geterror());
                return;
            }
        }
    }
}



// --rangecompress
OIIOTOOL_OP(rangecompress, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    bool useluma = op.options().get_int("luma");
    return ImageBufAlgo::rangecompress(*img[0], *img[1], useluma);
});

// --rangeexpand
OIIOTOOL_OP(rangeexpand, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    bool useluma = op.options().get_int("luma");
    return ImageBufAlgo::rangeexpand(*img[0], *img[1], useluma);
});



// --contrast
OIIOTOOL_OP(contrast, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    size_t n   = size_t((*img[0]).nchannels());
    auto black = Strutil::extract_from_list_string(
        op.options().get_string("black", "0"), n, 0.0f);
    auto white = Strutil::extract_from_list_string(
        op.options().get_string("white", "1"), n, 1.0f);
    auto min = Strutil::extract_from_list_string(op.options().get_string("min",
                                                                         "0"),
                                                 n, 0.0f);
    auto max = Strutil::extract_from_list_string(op.options().get_string("max",
                                                                         "1"),
                                                 n, 1.0f);
    auto scontrast = Strutil::extract_from_list_string(
        op.options().get_string("scontrast", "1"), n, 1.0f);
    auto sthresh = Strutil::extract_from_list_string(
        op.options().get_string("sthresh", "0.5"), n, 0.50f);
    bool ok = ImageBufAlgo::contrast_remap(*img[0], *img[1], black, white, min,
                                           max, scontrast, sthresh);
    if (ok && op.options().get_int("clamp"))
        ok &= ImageBufAlgo::clamp(*img[0], *img[0], min, max);
    return int(ok);
});



// --box
// clang-format off
OIIOTOOL_INPLACE_OP(box, 1, nullptr, ([&](OiiotoolOp& op, span<ImageBuf*> img) {
    OIIO_ASSERT(img[0] == img[1]);  // Assume we're operating in-place
    // img[0]->copy(*img[1]);  // what we'd do if we weren't in-place
    const ImageSpec& Rspec(img[0]->spec());
    int x1, y1, x2, y2;
    string_view s(op.args(1));
    if (Strutil::parse_int(s, x1) && Strutil::parse_char(s, ',')
        && Strutil::parse_int(s, y1) && Strutil::parse_char(s, ',')
        && Strutil::parse_int(s, x2) && Strutil::parse_char(s, ',')
        && Strutil::parse_int(s, y2)) {
        std::vector<float> color(Rspec.nchannels + 1, 1.0f);
        Strutil::extract_from_list_string(color,
                                          op.options().get_string(
                                              "color"));
        bool fill = op.options().get_int("fill");
        return ImageBufAlgo::render_box(*img[0], x1, y1, x2, y2, color, fill);
    } else {
        return false;
    }
}));
// clang-format on


// --line
OIIOTOOL_INPLACE_OP(line, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    OIIO_ASSERT(img[0] == img[1]);  // Assume we're operating in-place
    // img[0]->copy(*img[1]);  // what we'd do if we weren't in-place
    const ImageSpec& Rspec(img[0]->spec());
    std::vector<int> points;
    Strutil::extract_from_list_string(points, op.args(1));
    std::vector<float> color(Rspec.nchannels + 1, 1.0f);
    Strutil::extract_from_list_string(color, op.options().get_string("color"));
    bool closed = (points.size() > 4 && points[0] == points[points.size() - 2]
                   && points[1] == points[points.size() - 1]);
    bool ok     = true;
    for (size_t i = 0, e = points.size() - 3; i < e; i += 2)
        ok &= ImageBufAlgo::render_line(*img[0], points[i + 0], points[i + 1],
                                        points[i + 2], points[i + 3], color,
                                        closed || i > 0 /*skip_first_point*/);
    return ok;
});



// --point
OIIOTOOL_INPLACE_OP(point, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    OIIO_ASSERT(img[0] == img[1]);  // Assume we're operating in-place
    // img[0]->copy(*img[1]);  // what we'd do if we weren't in-place
    const ImageSpec& Rspec(img[0]->spec());
    std::vector<int> points;
    Strutil::extract_from_list_string(points, op.args(1));
    std::vector<float> color(Rspec.nchannels, 1.0f);
    Strutil::extract_from_list_string(color, op.options().get_string("color"));
    bool ok = true;
    for (size_t i = 0, e = points.size() - 1; i < e; i += 2)
        ok &= ImageBufAlgo::render_point(*img[0], points[i + 0], points[i + 1],
                                         color);
    return ok;
});



// --text
OIIOTOOL_INPLACE_OP(text, 1, [&](OiiotoolOp& op, span<ImageBuf*> img) {
    OIIO_ASSERT(img[0] == img[1]);  // Assume we're operating in-place
    // img[0]->copy(*img[1]);  // what we'd do if we weren't in-place
    const ImageSpec& Rspec(img[0]->spec());
    int x            = op.options().get_int("x", Rspec.x + Rspec.width / 2);
    int y            = op.options().get_int("y", Rspec.y + Rspec.height / 2);
    int fontsize     = op.options().get_int("size", 16);
    std::string font = op.options()["font"];
    std::vector<float> textcolor(Rspec.nchannels + 1, 1.0f);
    Strutil::extract_from_list_string(textcolor,
                                      op.options().get_string("color"));
    std::string ax = op.options()["xalign"];
    std::string ay = op.options()["yalign"];
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
    int shadow  = op.options().get_int("shadow");
    int measure = op.options().get_int("measure");
    int render  = op.options().get_int("render", 1);
    if (measure) {
        ROI roi = ImageBufAlgo::text_size(op.args(1), fontsize, font);
        ot.uservars["TEXT_X"]      = roi.xbegin;
        ot.uservars["TEXT_Y"]      = roi.ybegin;
        ot.uservars["TEXT_WIDTH"]  = roi.width();
        ot.uservars["TEXT_HEIGHT"] = roi.height();
    }
    if (render)
        return ImageBufAlgo::render_text(*img[0], x, y, op.args(1), fontsize,
                                         font, textcolor, alignx, aligny,
                                         shadow);
    return true;
});



// -i
static int
input_file(Oiiotool& ot, cspan<const char*> argv)
{
    // ot.total_readtime.start();
    string_view command = ot.express(argv[0]);
    if (argv.size() > 1
        && (Strutil::starts_with(command, "-i")
            || Strutil::starts_with(command, "--i"))) {
        argv = argv.subspan(1);
    } else {
        command = "-i";
    }
    auto fileoptions     = ot.extract_options(command);
    int printinfo        = fileoptions.get_int("info", ot.printinfo);
    bool readnow         = fileoptions.get_int("now", 0);
    bool native          = fileoptions.get_int("native", int(ot.nativeread));
    bool autocc          = fileoptions.get_int("autocc", ot.autocc);
    bool autoccunpremult = fileoptions.get_int("unpremult", ot.autoccunpremult);
    std::string infoformat = fileoptions.get_string("infoformat",
                                                    ot.printinfo_format);
    TypeDesc input_dataformat(fileoptions.get_string("type"));
    std::string channel_set = fileoptions["ch"];
    bool get_thumbnail      = fileoptions.get_int("get_thumbnail", 0);

    for (int i = 0; i < std::ssize(argv); i++) {
        // FIXME: this loop is pointless, since there is ever only one arg
        OTScopedTimer timer(ot, command);
        string_view filename = ot.express(argv[i]);
        auto found           = ot.image_labels.find(filename);
        if (found != ot.image_labels.end()) {
            if (ot.debug)
                std::cout << "Referencing labeled image " << filename << "\n";
            ot.push(found->second);
            ot.process_pending();
            break;
        }
        int exists = 1;
        if (ot.input_config_set) {
            // User has set some input configuration, so seed the cache with
            // that information.
            ustring fn(filename);
            ot.imagecache->invalidate(fn, true);
            bool ok = ot.imagecache->add_file(fn, nullptr, &ot.input_config);
            if (!ok) {
                ot.error("read",
                         ot.format_read_error(filename,
                                              ot.imagecache->geterror()));
                break;
            }
        }
        if (!ot.imagecache->get_image_info(ustring(filename), 0, 0,
                                           ustring("exists"), TypeInt, &exists))
            exists = 0;
        // N.B. ImageCache "exists" really means "can be opened and read."
        if (!exists) {
            auto input = ImageInput::create(filename);
            if (input && input->supports("procedural")) {
                // If the image doesn't appear to exist, but it's a procedural
                // image generator, then that's ok.
                exists = 1;
            }
            if (!input) {
                // If the create call failed, eat any stray global errors it
                // may have issued.
                (void)OIIO::geterror();
            }
        }
        ImageBufRef substitute;  // possible substitute for missing image
        if (!exists) {
            // It neither can be opened nor is it a procedural.
            std::string errmsg;
            if (!Filesystem::exists(filename)) {
                // It literally is a nonexistant file.
                errmsg = Strutil::format("File does not exist: \"{}\"",
                                         filename);
            } else {
                // The file exists, but it can't be opened as an image.
                // Try to get a more precise error message to report.
                auto in         = ImageInput::open(filename);
                std::string err = in ? in->geterror() : OIIO::geterror();
                errmsg          = ot.format_read_error(filename, err);
            }
            // Second chances: do we have a substitute image policy?
            ImageSpec substitute_spec;
            if (ot.first_input_dimensions_is_set())
                ot.get_first_input_dimensions(substitute_spec);
            else if (ot.parent_oiiotool
                     && ot.parent_oiiotool->first_input_dimensions_is_set())
                ot.parent_oiiotool->get_first_input_dimensions(substitute_spec);
            else {
                // How big to make the substitute image if we haven't yet read
                // any image successfully? Punt and guess HD res RGBA.
                substitute_spec = ImageSpec(1920, 1080, 4);
            }
            if (ot.missingfile_policy == "black") {
                substitute.reset(
                    new ImageBuf(substitute_spec, InitializePixels::Yes));
            } else if (ot.missingfile_policy == "checker") {
                substitute.reset(new ImageBuf(substitute_spec));
                ImageBufAlgo::checker(*substitute, 64, 64, 1,
                                      { 0.0f, 0.0f, 0.0f, 1.0f },
                                      { 1.0f, 1.0f, 1.0f, 1.0f });
            }
            if (!exists) {
                if (substitute) {
                    ot.warningfmt("read", "{}, Substituting {}", errmsg,
                                  ot.missingfile_policy);
                } else {
                    ot.error("read", errmsg);
                    break;
                }
            }
        }
        if (channel_set.size()) {
            ot.input_channel_set = channel_set;
            readnow              = true;
        }
        readnow |= native;

        if (substitute) {
            ot.push(ImageRecRef(new ImageRec(substitute)));
            readnow = false;
            ot.ap.abort(false);
        } else {
            if (ot.debug || ot.verbose)
                std::cout << "Reading " << filename << "\n";
            ot.push(ImageRecRef(new ImageRec(filename, ot.imagecache)));
            if (ot.input_config_set)
                ot.curimg->configspec(ot.input_config);
            ot.curimg->input_dataformat(input_dataformat);
            bool ok;
            if (readnow) {
                ReadPolicy policy = native ? ReadNativeNoCache : ReadNoCache;
                ok                = ot.read(policy, channel_set);
            } else {
                ok = ot.read_nativespec();
            }
            if (!ok)
                break;  // error already reported by the read
            const ImageSpec* nspec = ot.curimg->nativespec();
            if (!ot.first_input_dimensions_is_set()) {
                ImageSpec new_first_dims;
                new_first_dims.copy_dimensions(*nspec);
                new_first_dims.channelnames = nspec->channelnames;
                ot.set_first_input_dimensions(new_first_dims);
                if (ot.parent_oiiotool)
                    ot.parent_oiiotool->set_first_input_dimensions(
                        new_first_dims);
            }
        }
        if ((printinfo || ot.printstats || ot.dumpdata || ot.hash)
            && !substitute) {
            print_info_options pio = ot.info_opts();
            pio.verbose |= printinfo > 1;
            pio.subimages |= printinfo > 1;
            pio.infoformat = infoformat;
            std::string error;
            bool ok = OiioTool::print_info(std::cout, ot, filename, pio, error);
            if (!ok) {
                ot.error("read", ot.format_read_error(filename, error));
                break;
            }
            ot.printed_info = true;
        }

        // Everything past this point should be credited to other ops, so stop
        // the input timer.
        timer.stop();

        if (get_thumbnail && !substitute) {
            // Swap in the embedded thumbnail via the --thumbnail-get logic.
            // Done before autoorient/autocc so they apply to the thumbnail.
            std::string thumbcmd = "--thumbnail-get";
            if (fileoptions.contains("fail"))
                thumbcmd += Strutil::fmt::format(":fail={}",
                                                 fileoptions.get_int("fail"));
            if (fileoptions.contains("index"))
                thumbcmd += Strutil::fmt::format(":index={}",
                                                 fileoptions.get_int("index"));
            const char* argv[] = { thumbcmd.c_str() };
            action_thumbnail_get(ot, argv);
        }

        if (ot.autoorient) {
            void action_reorient(Oiiotool & ot, cspan<const char*> argv);
            const char* argv[] = { "--reorient" };
            action_reorient(ot, argv);
        }

        if (autocc) {
            // Try to deduce the color space it's in
            std::string colorspace(
                ot.colorconfig().getColorSpaceFromFilepath(filename, "", true));
            if (colorspace == "unknown")
                colorspace.clear();
            if (colorspace.size() && ot.debug)
                OIIO::print("  From {}, we deduce color space \"{}\"\n",
                            filename, colorspace);
            if (colorspace.empty()) {
                ot.read();
                colorspace = ot.curimg->spec()->get_string_attribute(
                    "oiio:ColorSpace");
                if (ot.debug)
                    OIIO::print(
                        "  Metadata of {} indicates color space \"{}\"\n",
                        colorspace, filename);
            }
            std::string linearspace = ot.colorconfig().resolve("scene_linear");
            if (colorspace.size()
                && !ot.colorconfig().equivalent(colorspace, linearspace)) {
                std::string cmd = "colorconvert:strict=0";
                if (autoccunpremult)
                    cmd += ":unpremult=1";
                const char* argv[] = { cmd.c_str(), colorspace.c_str(),
                                       linearspace.c_str() };
                if (ot.debug)
                    OIIO::print("  Converting {} from {} to {}\n", filename,
                                colorspace, linearspace);
                action_colorconvert(ot, argv);
            } else if (ot.debug) {
                OIIO::print("  no auto conversion necessary for {}->{}\n",
                            colorspace, linearspace);
            }
        }

        ot.process_pending();
    }

    ot.clear_input_config();
    ot.input_channel_set.clear();
    ot.check_peak_memory();
    // ot.total_readtime.stop();
    return 0;
}



// --testread
static int
test_read(Oiiotool& ot, cspan<const char*> argv)
{
    // ot.total_readtime.start();
    string_view command  = ot.express(argv[0]);
    string_view filename = ot.express(argv[1]);
    auto fileoptions     = ot.extract_options(command);
    TypeDesc input_dataformat(fileoptions.get_string("type", "uint8"));

    OTScopedTimer timer(ot, command);

    bool ok  = true;
    auto inp = OIIO::ImageInput::open(filename, ot.input_config_set
                                                    ? &ot.input_config
                                                    : nullptr);
    if (inp)
        ok = OIIO::pvt::test_read_all_images(*inp, input_dataformat);
    if (!ok || !inp) {
        ot.error("testread",
                 ot.format_read_error(filename, inp ? inp->geterror()
                                                    : OIIO::geterror()));
    } else {
        OIIO::print("OK {}\n", filename);
    }
    ot.printed_info = true;

    // Everything past this point should be credited to other ops, so stop
    // the input timer.
    timer.stop();

    ot.process_pending();
    ot.check_peak_memory();
    // ot.total_readtime.stop();
    return 0;
}



static void
prep_texture_config(Oiiotool& ot, ImageSpec& configspec,
                    ParamValueList& fileoptions)
{
    configspec.tile_width  = ot.output_tilewidth ? ot.output_tilewidth : 64;
    configspec.tile_height = ot.output_tileheight ? ot.output_tileheight : 64;
    configspec.tile_depth  = 1;
    std::string wrap       = fileoptions.get_string("wrap", "black");
    std::string swrap      = fileoptions.get_string("swrap", wrap);
    std::string twrap      = fileoptions.get_string("twrap", wrap);
    configspec.attribute("wrapmodes",
                         Strutil::fmt::format("{},{}", swrap, twrap));
    configspec.attribute("maketx:verbose", ot.verbose);
    configspec.attribute("maketx:runstats", ot.runstats);
    configspec.attribute("maketx:resize", fileoptions.get_int("resize"));
    configspec.attribute("maketx:keepaspect",
                         fileoptions.get_int("keepaspect"));
    configspec.attribute("maketx:nomipmap", fileoptions.get_int("nomipmap"));
    configspec.attribute("maketx:updatemode",
                         fileoptions.get_int("updatemode"));
    configspec.attribute("maketx:constant_color_detect",
                         fileoptions.get_int("constant_color_detect"));
    configspec.attribute("maketx:monochrome_detect",
                         fileoptions.get_int("monochrome_detect"));
    configspec.attribute("maketx:opaque_detect",
                         fileoptions.get_int("opaque_detect"));
    configspec.attribute("maketx:compute_average",
                         fileoptions.get_int("compute_average", 1));
    configspec.attribute("maketx:unpremult", fileoptions.get_int("unpremult"));
    configspec.attribute("maketx:incolorspace",
                         fileoptions.get_string("incolorspace"));
    configspec.attribute("maketx:outcolorspace",
                         fileoptions.get_string("outcolorspace"));
    configspec.attribute(
        "maketx:highlightcomp",
        fileoptions.get_int("highlightcomp",
                            fileoptions.get_int("highlightcomp",
                                                fileoptions.get_int("hicomp"))));
    configspec.attribute("maketx:sharpen", fileoptions.get_float("sharpen"));
    if (fileoptions.contains("filter") || fileoptions.contains("filtername"))
        configspec.attribute("maketx:filtername",
                             fileoptions.get_string("filtername",
                                                    fileoptions.get_string(
                                                        "filter")));
    if (fileoptions.contains("fileformatname"))
        configspec.attribute("maketx:fileformatname",
                             fileoptions.get_string("fileformatname"));
    configspec.attribute("maketx:prman_metadata",
                         fileoptions.get_int("prman_metadata"));
    configspec.attribute("maketx:oiio_options",
                         fileoptions.get_string("oiio_options",
                                                fileoptions.get_string("oiio")));
    configspec.attribute("maketx:prman_options",
                         fileoptions.get_string(
                             "prman_options", fileoptions.get_string("prman")));
    configspec.attribute("maketx:bumpformat",
                         fileoptions.get_string("bumpformat", "auto"));
    configspec.attribute("maketx:bumpscale",
                         fileoptions.get_float("bumpscale", 1.0f));
    configspec.attribute("maketx:bumpinverts",
                         fileoptions.get_int("bumpinverts"));
    configspec.attribute("maketx:bumpinvertt",
                         fileoptions.get_int("bumpinvertt"));
    configspec.attribute("maketx:slopefilter",
                         fileoptions.get_string("slopefilter", "sobel"));
    configspec.attribute("maketx:bumprange",
                         fileoptions.get_string("bumprange", "auto"));
    configspec.attribute("maketx:uvslopes_scale",
                         fileoptions.get_float("uvslopes_scale", 0.0f));
    if (fileoptions.contains("handed"))
        configspec.attribute("handed", fileoptions.get_string("handed"));
    if (fileoptions.contains("forcefloat"))
        configspec.attribute("maketx:forcefloat",
                             fileoptions.get_int("forcefloat"));

    // The default values here should match the initialized values
    // in src/maketx/maketx.cpp
    configspec.attribute("maketx:cdf", fileoptions.get_int("cdf"));
    configspec.attribute("maketx:cdfbits", fileoptions.get_int("cdfbits", 8));
    configspec.attribute("maketx:cdfsigma",
                         fileoptions.get_float("cdfsigma", 1.0f / 6));

    // if (mipimages.size())
    //     configspec.attribute ("maketx:mipimages", Strutil::join(mipimages,";"));

    std::string software = configspec.get_string_attribute("Software");
    if (software.size())
        configspec.attribute("maketx:full_command_line", software);
}



// Helper: Remove ":all=[0-9]+" from str
static void
remove_all_cmd(std::string& str)
{
    size_t start = str.find(":all=");
    if (start != std::string::npos) {
        size_t end = start + 5;  // : a l l =
        end = std::min(str.find_first_not_of("0123456789", end), str.size());
        str = std::string(str, 0, start) + std::string(str, end);
    }
}



// -o
static void
output_file(Oiiotool& ot, cspan<const char*> argv)
{
    ot.total_writetime.start();
    string_view command  = ot.express(argv[0]);
    std::string filename = ot.express(argv[1]);
    OTScopedTimer timer(ot, command);

    auto fileoptions = ot.extract_options(command);

    string_view stripped_command = command;
    Strutil::parse_char(stripped_command, '-');
    Strutil::parse_char(stripped_command, '-');
    bool do_tex        = Strutil::starts_with(stripped_command, "otex");
    bool do_latlong    = Strutil::starts_with(stripped_command, "oenv")
                         || Strutil::starts_with(stripped_command, "olatlong");
    bool do_shad       = Strutil::starts_with(stripped_command, "oshad");
    bool do_bumpslopes = Strutil::starts_with(stripped_command, "obump");

    if (ot.debug)
        std::cout << "Output: " << filename << "\n";
    if (!ot.curimg.get()) {
        ot.warningfmt(command, "{} did not have any current image to output.",
                      filename);
        return;
    }

    if (fileoptions.contains("all")) {
        // Special case: if they requested outputting all images on the
        // stack, handle it recursively. The filename, then, is the pattern,
        // presumed to have a %d in it somewhere, which we will substitute
        // with the image index.
        int startnumber = fileoptions.get_int("all");
        int nimages     = 1 /*curimg*/ + int(ot.image_stack.size());
        const char* new_argv[2];
        // Git rid of the ":all=" part of the command so we don't infinitely
        // recurse.
        std::string newcmd = command;
        remove_all_cmd(newcmd);
        new_argv[0]              = newcmd.c_str();
        ImageRecRef saved_curimg = ot.curimg;  // because we'll overwrite it
        // Revert back to the UN-expression-substituted filename. It will get
        // expression substitution in the subsequent call to output_file.
        filename = argv[1];
        for (int i = 0; i < nimages; ++i) {
            if (i < nimages - 1)
                ot.curimg = ot.image_stack[i];
            else
                ot.curimg
                    = saved_curimg;  // note: last iteration also restores it!
            // Skip 0x0 images. Yes, this can happen.
            if (!ot.read())
                return;
            const ImageSpec* spec(ot.curimg->spec());
            if (spec->width < 1 || spec->height < 1 || spec->depth < 1)
                continue;
            // Use the filename as a pattern, format with the frame number
            new_argv[1]
                = ustring::sprintf(filename.c_str(), i + startnumber).c_str();
            // recurse for this file
            output_file(ot, new_argv);
        }
        return;
    }

    const std::string outputdirpath = Filesystem::parent_path(filename);
    if (!outputdirpath.empty() && !Filesystem::exists(outputdirpath)) {
        if (ot.create_dir) {
            std::string err;
            const bool ok = Filesystem::create_directories(outputdirpath, err);
            if (!ok) {
                ot.errorfmt(command,
                            "Failed to create output directory: {}\n\tError: {}",
                            outputdirpath, err);
                return;
            }
        } else {
            ot.errorfmt(command,
                        "Non-existent output directory: {}\n"
                        "\t--create-dir to create missing output directories",
                        outputdirpath);
            return;
        }
    }

    if (ot.noclobber && Filesystem::exists(filename)) {
        ot.warningfmt(command, "{} already exists, not overwriting.", filename);
        return;
    }
    std::string formatname = fileoptions.get_string("fileformatname", filename);
    auto out               = ImageOutput::create(formatname);
    if (!out) {
        std::string err = OIIO::geterror();
        ot.error(command, err.size() ? err.c_str()
                                     : "unknown error creating an ImageOutput");
        return;
    }
    bool supports_displaywindow  = out->supports("displaywindow");
    bool supports_negativeorigin = out->supports("negativeorigin");
    bool supports_tiles = out->supports("tiles") || ot.output_force_tiles;
    bool procedural     = out->supports("procedural");
    if (!ot.read()) {
        return;
    }
    ImageRecRef saveimg = ot.curimg;
    ImageRecRef ir(ot.curimg);
    TypeDesc saved_output_dataformat = ot.output_dataformat;
    int saved_bitspersample          = ot.output_bitspersample;

    timer.stop();  // resume after all these auto-transforms

    // Automatically drop channels we can't support in output.
    int nchans = ir->spec()->nchannels;
    if (nchans > 3) {
        int trimchans = nchans;
        bool chan3_is_alpha
            = (nchans > 3
               && (ir->spec()->alpha_channel == 3
                   || Strutil::iequals(ir->spec()->channel_name(3), "A")
                   || Strutil::iequals(ir->spec()->channel_name(3), "Alpha")));
        if (nchans > 4 && !out->supports("nchannels"))
            trimchans = 4;
        if ((chan3_is_alpha && !out->supports("alpha"))
            || (!chan3_is_alpha && !out->supports("nchannels")))
            trimchans = 3;
        if (trimchans < nchans) {
            std::string chanlist = first_n_channels(*ir->spec(), trimchans);
            ot.warningfmt(
                command,
                "Can't save {} channels to {}... saving only channels {}",
                ir->spec()->nchannels, out->format_name(), chanlist);
            const char* argv[] = { "channels:allsubimages=1",
                                   chanlist.c_str() };
            void action_channels(Oiiotool & ot,
                                 cspan<const char*> argv);  // forward decl
            action_channels(ot, argv);
            ir = ot.curimg;
        }
    }

    // Handle --autotrim
    int autotrim = fileoptions.get_int("autotrim", ot.output_autotrim);
    if (supports_displaywindow && autotrim) {
        ROI roi           = nonzero_region_all_subimages(ir);
        bool crops_needed = false;
        for (int s = 0; s < ir->subimages(); ++s)
            crops_needed |= (roi != (*ir)(s).roi());
        if (crops_needed) {
            std::string crop = (ir->spec(0, 0)->depth == 1)
                                   ? format_resolution(roi.width(),
                                                       roi.height(), roi.xbegin,
                                                       roi.ybegin)
                                   : format_resolution(roi.width(),
                                                       roi.height(),
                                                       roi.depth(), roi.xbegin,
                                                       roi.ybegin, roi.zbegin);
            const char* argv[] = { "crop:allsubimages=1", crop.c_str() };
            void action_crop(Oiiotool & ot,
                             cspan<const char*> argv);  // forward decl
            action_crop(ot, argv);
            ir = ot.curimg;
        }
    }

    // Automatically crop/pad if outputting to a format that doesn't
    // support display windows, unless autocrop is disabled.
    int autocrop = fileoptions.get_int("autocrop", ot.output_autocrop);
    if (!supports_displaywindow && autocrop
        && (ir->spec()->x != ir->spec()->full_x
            || ir->spec()->y != ir->spec()->full_y
            || ir->spec()->width != ir->spec()->full_width
            || ir->spec()->height != ir->spec()->full_height)) {
        const char* argv[] = { "croptofull:allsubimages=1" };
        void action_croptofull(Oiiotool & ot,
                               cspan<const char*> argv);  // forward decl
        action_croptofull(ot, argv);
        ir = ot.curimg;
    }

    // See if the filename appears to contain a color space name embedded.
    // Automatically color convert if --autocc is used and the current
    // color space doesn't match that implied by the filename, and
    // automatically set -d based on the name if --autocc is used.
    bool autocc          = fileoptions.get_int("autocc", ot.autocc);
    bool autoccunpremult = fileoptions.get_int("unpremult", ot.autoccunpremult);
    std::string outcolorspace
        = ot.colorconfig().getColorSpaceFromFilepath(filename, "", true);
    if (autocc && outcolorspace.size()) {
        TypeDesc type;
        int bits;
        type = ot.colorconfig().getColorSpaceDataType(outcolorspace, &bits);
        if (type != TypeUnknown && type != TypeFloat) {
            if (ot.debug)
                OIIO::print(
                    "  Deduced data type {} ({} bits) for output to {}\n", type,
                    bits, filename);
            if ((ot.output_dataformat && ot.output_dataformat != type)
                || (bits && ot.output_bitspersample
                    && ot.output_bitspersample != bits)) {
                ot.warningfmt(
                    command,
                    "Output filename ({}) colorspace \"{}\" implies {} ({} bits), overriding prior request for {}.",
                    filename, outcolorspace, type, bits, ot.output_dataformat);
            }
            ot.output_dataformat    = type;
            ot.output_bitspersample = bits;
        }
    }
    if (autocc) {
        string_view linearspace = ot.colorconfig().resolve("scene_linear");
        std::string currentspace
            = ir->spec()->get_string_attribute("oiio:ColorSpace", linearspace);
        // Special cases where we know formats should be particular color
        // spaces
        if (outcolorspace.empty()
            && (Strutil::iends_with(filename, ".jpg")
                || Strutil::iends_with(filename, ".jpeg")
                || Strutil::iends_with(filename, ".gif")
                || Strutil::iends_with(filename, ".webp")))
            outcolorspace = string_view("srgb_rec709_scene");
        if (outcolorspace.empty()
            && (Strutil::iends_with(filename, ".ppm")
                || Strutil::iends_with(filename, ".pnm")))
            outcolorspace = string_view("Rec709");
        if (outcolorspace.size() && currentspace != outcolorspace) {
            if (ot.debug)
                std::cout << "  Converting from " << currentspace << " to "
                          << outcolorspace << " for output to " << filename
                          << "\n";
            std::string cmd = "colorconvert:strict=0:allsubimages=1";
            if (autoccunpremult)
                cmd += ":unpremult=1";
            const char* argv[] = { cmd.c_str(), currentspace.c_str(),
                                   outcolorspace.c_str() };
            action_colorconvert(ot, argv);
            ir = ot.curimg;
        }
    }

    // Automatically crop out the negative areas if outputting to a format
    // that doesn't support negative origins.
    if (!supports_negativeorigin && autocrop
        && (ir->spec()->x < 0 || ir->spec()->y < 0 || ir->spec()->z < 0)) {
        ROI roi            = get_roi(*ir->spec(0, 0));
        roi.xbegin         = std::max(0, roi.xbegin);
        roi.ybegin         = std::max(0, roi.ybegin);
        roi.zbegin         = std::max(0, roi.zbegin);
        roi.xend           = std::max(roi.xbegin + 1, roi.xend);
        roi.yend           = std::max(roi.ybegin + 1, roi.yend);
        roi.zend           = std::max(roi.zbegin + 1, roi.zend);
        std::string crop   = (ir->spec(0, 0)->depth == 1)
                                 ? format_resolution(roi.width(), roi.height(),
                                                     roi.xbegin, roi.ybegin)
                                 : format_resolution(roi.width(), roi.height(),
                                                     roi.depth(), roi.xbegin,
                                                     roi.ybegin, roi.zbegin);
        const char* argv[] = { "crop:allsubimages=1", crop.c_str() };
        void action_crop(Oiiotool & ot,
                         cspan<const char*> argv);  // forward decl
        action_crop(ot, argv);
        ir = ot.curimg;
    }

    if (ot.dryrun) {
        ot.curimg               = saveimg;
        ot.output_dataformat    = saved_output_dataformat;
        ot.output_bitspersample = saved_bitspersample;
        return;
    }

    timer.start();
    if (ot.debug || ot.verbose)
        std::cout << "Writing " << filename << "\n";

    // FIXME -- the various automatic transformations above neglect to handle
    // MIPmaps or subimages with full generality.

    bool ok = true;
    if (do_tex || do_latlong || do_bumpslopes) {
        ImageSpec configspec;
        adjust_output_options(filename, configspec, nullptr, ot, 0, 1,
                              supports_tiles, fileoptions);
        prep_texture_config(ot, configspec, fileoptions);
        ImageBufAlgo::MakeTextureMode mode = ImageBufAlgo::MakeTxTexture;
        if (do_shad)
            mode = ImageBufAlgo::MakeTxShadow;
        if (do_latlong)
            mode = ImageBufAlgo::MakeTxEnvLatl;
        if (do_bumpslopes)
            mode = ImageBufAlgo::MakeTxBumpWithSlopes;
        // if (lightprobemode)
        //     mode = ImageBufAlgo::MakeTxEnvLatlFromLightProbe;
        if (ot.verbose || ot.debug)
            configspec.attribute("maketx:verbose", 1);
        ok = ImageBufAlgo::make_texture(mode, (*ir)(0, 0), filename, configspec,
                                        ot.verbose || ot.debug ? &std::cout
                                                               : nullptr);
        if (!ok) {
            ot.errorfmt(command, "Could not make texture: {}",
                        OIIO::geterror());
            return;
        }
        // N.B. make_texture already internally writes to a temp file and
        // then atomically moves it to the final destination, so we don't
        // need to explicitly do that here.
    } else {
        // Non-texture case
        std::vector<ImageSpec> subimagespecs(ir->subimages());
        for (int s = 0, send = ir->subimages(); s < send; ++s) {
            ImageSpec spec = *ir->spec(s, 0);
            adjust_output_options(filename, spec, ir->nativespec(s), ot, s,
                                  send, supports_tiles, fileoptions,
                                  (*ir)[s].was_direct_read());
            // If it's not tiled and MIP-mapped (except for certain formats that
            // support non-MIP-mapped textures), remove any "textureformat"
            if (!spec.tile_pixels()
                || (string_view(out->format_name()) != "ktx"
                    && ir->miplevels(s) <= 1))
                spec.erase_attribute("textureformat");
            subimagespecs[s] = spec;
        }

        // Write the output to a temp file first, then rename it to the
        // final destination (same directory). This improves robustness.
        // There is less chance a crash during execution will leave behind a
        // partially formed file, and it also protects us against corrupting
        // an input if they are "oiiotooling in place" (especially
        // problematic for large files that are ImageCache-based and so only
        // partially read at the point that we open the file. We also force
        // a unique filename to protect against multiple processes running
        // at the same time on the same file.
        std::string extension = Filesystem::extension(filename);
        std::string tmpfilename
            = Filesystem::replace_extension(filename,
                                            ".%%%%%%%%.temp" + extension);
        tmpfilename = Filesystem::unique_path(tmpfilename);

        // Do the initial open
        ImageOutput::OpenMode mode = ImageOutput::Create;
        if (ir->subimages() > 1 && out->supports("multiimage")) {
            if (!out->open(tmpfilename, ir->subimages(), &subimagespecs[0])) {
                ot.error(command, out->geterror());
                return;
            }
        } else {
            if (!out->open(tmpfilename, subimagespecs[0], mode)) {
                ot.error(command, out->geterror());
                return;
            }
        }

        // Output all the subimages and MIP levels
        for (int s = 0, send = ir->subimages(); s < send; ++s) {
            for (int m = 0, mend = ir->miplevels(s); m < mend && ok; ++m) {
                ImageSpec spec = *ir->spec(s, m);
                adjust_output_options(filename, spec, ir->nativespec(s, m), ot,
                                      s, send, supports_tiles, fileoptions,
                                      (*ir)[s].was_direct_read());
                if (s > 0 || m > 0) {  // already opened first subimage/level
                    if (!out->open(tmpfilename, spec, mode)) {
                        ot.error(command, out->geterror());
                        ok = false;
                        break;
                    }
                }
                if (!(*ir)(s, m).write(out.get())) {
                    ot.error(command, (*ir)(s, m).geterror());
                    ok = false;
                    break;
                }
                ot.check_peak_memory();
                if (mend > 1) {
                    if (out->supports("mipmap")) {
                        mode = ImageOutput::AppendMIPLevel;  // for next level
                    } else if (out->supports("multiimage")) {
                        mode = ImageOutput::AppendSubimage;
                    } else {
                        ot.warningfmt(command,
                                      "{} does not support MIP-maps for {}",
                                      out->format_name(), filename);
                        break;
                    }
                }
            }
            mode = ImageOutput::AppendSubimage;  // for next subimage
            if (send > 1 && !out->supports("multiimage")) {
                ot.warningfmt(command,
                              "{} does not support multiple subimages for {}",
                              out->format_name(), filename);
                break;
            }
        }

        if (!out->close()) {
            ot.error(command, out->geterror());
            ok = false;
        }
        out.reset();  // make extra sure it's cleaned up

        // We wrote to a temporary file, so now atomically move it to the
        // original desired location.
        if (ok && !procedural) {
            std::string err;
            ok = Filesystem::rename(tmpfilename, filename, err);
            if (!ok)
                ot.errorfmt(
                    command,
                    "oiiotool ERROR: could not move temp file {} to {}: {}",
                    tmpfilename, filename, err);
        }
        if (!ok)
            Filesystem::remove(tmpfilename);
    }

    // Make sure to invalidate any IC entries that think they are the
    // file we just wrote.
    ot.imagecache->invalidate(ustring(filename), true);

    if (ot.output_adjust_time && ok) {
        std::string metadatatime = ir->spec(0, 0)->get_string_attribute(
            "DateTime");
        std::time_t in_time = ir->time();
        if (!metadatatime.empty())
            DateTime_to_time_t(metadatatime.c_str(), in_time);
        Filesystem::last_write_time(filename, in_time);
    }

    ot.check_peak_memory();
    ot.curimg               = saveimg;
    ot.output_dataformat    = saved_output_dataformat;
    ot.output_bitspersample = saved_bitspersample;
    ot.curimg->was_output(true);
    ot.total_writetime.stop();
    double optime = timer();
    ot.num_outputs += 1;

    if (ot.debug && ot.runstats)
        Strutil::print("    output took {}  (total time {}, mem {})\n",
                       Strutil::timeintervalformat(optime, 2),
                       Strutil::timeintervalformat(ot.total_runtime(), 2),
                       Strutil::memformat(Sysutil::memory_used()));
}



// --echo
static void
do_echo(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);

    string_view command = ot.express(argv[0]);
    std::string message = ot.express(Strutil::unescape_chars(argv[1]));

    auto options = ot.extract_options(command);
    int newline  = options.get_int("newline", 1);

    std::cout << message;
    for (int i = 0; i < newline; ++i)
        std::cout << '\n';
    std::cout.flush();
    ot.printed_info = true;
}



// --printstats
static void
action_printstats(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    if (ot.postpone_callback(1, action_printstats, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    if (!ot.read())
        return;
    ImageRecRef top = ot.top();

    print_info_options opt = ot.info_opts();

    opt.subimages     = allsubimages;
    opt.compute_stats = true;
    opt.roi           = top->spec(0, 0)->roi();
    std::string geom  = options["window"];
    if (!geom.empty()) {
        int x = opt.roi.xbegin, y = opt.roi.ybegin;
        int w = opt.roi.width(), h = opt.roi.height();
        ot.adjust_geometry(command, w, h, x, y, geom.c_str(), true, true);
        opt.roi = ROI(x, x + w, y, y + h, 0, opt.roi.zend, opt.roi.chbegin,
                      opt.roi.chend);
    }
    std::string errstring;
    OIIO_ASSERT(top.get());
    print_info(std::cout, ot, top.get(), opt, errstring);

    ot.printed_info = true;
}



// --printinfo
static void
action_printinfo(Oiiotool& ot, cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    if (ot.postpone_callback(1, action_printinfo, argv))
        return;
    string_view command = ot.express(argv[0]);
    OTScopedTimer timer(ot, command);
    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);
    bool stats        = options.get_int("stats", ot.printstats);
    bool verb         = options.get_int("verbose", 1);
    bool native       = options.get_int("native", 0);

    ot.read();
    ImageRecRef top = ot.top();

    print_info_options opt = ot.info_opts();

    opt.verbose       = verb;
    opt.subimages     = allsubimages;
    opt.compute_stats = stats;
    opt.native        = native;
    std::string errstring;
    print_info(std::cout, ot, top.get(), opt, errstring);

    ot.printed_info = true;
}


namespace pvtcrash {
size_t crasher = 37;
}


static void
crash_me()
{
    char* addr = (char*)pvtcrash::crasher;
    OIIO_PRAGMA_WARNING_PUSH
#if OIIO_GNUC_VERSION >= 110000
    OIIO_GCC_ONLY_PRAGMA(GCC diagnostic ignored "-Wstringop-overflow")
    OIIO_GCC_ONLY_PRAGMA(GCC diagnostic ignored "-Warray-bounds")
#endif
    *addr = 0;  // This should crash
    OIIO_PRAGMA_WARNING_POP
}



// Concatenate the command line into one string, optionally filtering out
// verbose attribute commands. Escape control chars in the arguments, and
// double-quote any that contain spaces.  Arguments that can be positively
// identified as existing filenames are "genericized" (on Windows,
// backslashes converted to forward slashes).
static std::string
command_line_string(int argc, char* argv[], bool sansattrib)
{
    std::string s;
    for (int i = 0; i < argc; ++i) {
        if (sansattrib) {
            // skip any filtered attributes
            if (!strcmp(argv[i], "--attrib") || !strcmp(argv[i], "-attrib")
                || !strcmp(argv[i], "--sattrib") || !strcmp(argv[i], "-sattrib")
                || !strcmp(argv[i], "--oiioattrib")
                || !strcmp(argv[i], "-oiioattrib")) {
                i += 2;  // also skip the following arguments
                continue;
            }
            if (!strcmp(argv[i], "--sansattrib")
                || !strcmp(argv[i], "-sansattrib")) {
                continue;
            }
        }
        std::string a(argv[i]);
        // For the first argument, which is the program name, strip off the
        // directory path.
        if (i == 0)
            a = Filesystem::filename(a);
#ifdef _WIN32
        // Genericize directory separators in filenames. This is especially
        // helpful for testsuite.
        if (Filesystem::exists(a))
            a = Filesystem::generic_filepath(a);
#endif
        a = Strutil::escape_chars(a);
        // If the string contains spaces
        if (a.find(' ') != std::string::npos) {
            // double quote args with spaces
            s += '\"';
            s += a;
            s += '\"';
        } else {
            s += a;
        }
        if (i < argc - 1)
            s += ' ';
    }
    return s;
}



static std::string
formatted_format_list(string_view format_typename, string_view attr)
{
    int columns = Sysutil::terminal_columns() - 2;
    std::stringstream s;
    s << format_typename << " formats supported: ";
    auto formats = Strutil::splitsv(OIIO::get_string_attribute(attr), ",");
    std::sort(formats.begin(), formats.end());
    s << Strutil::join(formats, ", ");
    return Strutil::wordwrap(s.str(), columns, 4);
}



static std::string
print_usage_tips()
{
    int columns = Sysutil::terminal_columns() - 2;

    std::stringstream out;
    out << "Important usage tips:\n"
        << Strutil::wordwrap(
               "  * The oiiotool command line is processed in order, LEFT to RIGHT.\n",
               columns, 4)
        << Strutil::wordwrap(
               "  * The command line consists of image NAMES ('image.tif') and "
               "COMMANDS ('--over'). Commands start with dashes (one or two dashes "
               "are equivalent). Some commands have required arguments which "
               "must follow on the command line. For example, the '-o' command is "
               "followed by a filename.\n",
               columns, 4)
        << Strutil::wordwrap(
               "  * oiiotool is STACK-based: naming an image pushes it on the stack, and "
               "most commands pop the top image (or sometimes more than one image), "
               "perform a calculation, and push the result image back on the stack. "
               "For example, the '--over' command pops the top two images off the "
               "stack, composites them, then pushes the result back onto the stack.\n",
               columns, 4)
        << Strutil::wordwrap(
               "  * Some commands allow one or more optional MODIFIERS in the form "
               "'name=value', which are appended directly to the command itself "
               "(no spaces), separated by colons ':'. For example,\n",
               columns, 4)
        << "       oiiotool in.tif --text:x=100:y=200:color=1,0,0 \"Hello\" -o out.tif\n"
        << Strutil::wordwrap(
               "  * Using numerical wildcards will run the whole command line on each of "
               "several sequentially-named files, for example:\n",
               columns, 4)
        << "       oiiotool fg.#.tif bg.#.tif -over -o comp.#.tif\n"
        << Strutil::wordwrap(
               "    See the manual for info about subranges, number of digits, etc.\n",
               columns, 4)
        << Strutil::wordwrap(
               "  * Command line arguments containing substrings enclosed in braces "
               "{} are replaced by evaluating their contents as expressions. Simple "
               "math is allowed as well as retrieving metadata such as {TOP.'foo:bar'}, "
               "{IMG[0].filename}, or {FRAME_NUMBER/24.0}.\n",
               columns, 4);
    return out.str();
}



inline bool
has_space(string_view s)
{
    return s.find(' ') != string_view::npos;
}



inline std::string
quote_if_spaces(string_view s)
{
    return has_space(s) ? Strutil::fmt::format("\"{}\"", s) : std::string(s);
}



template<class Sequence>
inline std::string
join_with_quotes(const Sequence& seq, string_view sep = "")
{
    std::ostringstream out;
    out.imbue(std::locale::classic());  // Force "C" locale
    bool first = true;
    for (auto&& s : seq) {
        if (!first && sep.size())
            out << sep;
        out << quote_if_spaces(s);
        first = false;
    }
    return out.str();
}


static void
print_ocio_info(Oiiotool& ot, std::ostream& out)
{
    using Strutil::print;
    int columns = Sysutil::terminal_columns() - 1;

    ColorConfig& colorconfig = ot.colorconfig();
    if (int ociover = colorconfig.OpenColorIO_version_hex())
        out << "OpenColorIO " << (ociover >> 24) << '.'
            << ((ociover >> 16) & 0xff) << '.' << ((ociover >> 8) & 0xff);
    else
        out << "No OpenColorIO";
    out << "\nColor config: " << colorconfig.configname() << "\n";
    out << "Known color spaces: \n";
    const char* linear       = colorconfig.getColorSpaceNameByRole("linear");
    const char* scene_linear = colorconfig.getColorSpaceNameByRole(
        "scene_linear");
    for (int i = 0, e = colorconfig.getNumColorSpaces(); i < e; ++i) {
        const char* n = colorconfig.getColorSpaceNameByIndex(i);
        out << "    - " << quote_if_spaces(n);
        if ((scene_linear && !colorconfig.equivalent(n, "scene_linear")
             && colorconfig.equivalent(n, scene_linear))
            || (linear && !colorconfig.equivalent(n, "linear")
                && colorconfig.equivalent(n, linear))
            || colorconfig.isColorSpaceLinear(n))
            out << " (linear)";
        if (!colorconfig.isColorSpaceActive(n))
            out << " (inactive)";
        out << "\n";
        auto aliases = colorconfig.getAliases(n);
        if (aliases.size()) {
            std::stringstream s;
            s << "      aliases: " << join_with_quotes(aliases, ", ");
            out << Strutil::wordwrap(s.str(), columns, 6) << "\n";
        }
    }

    int roles = colorconfig.getNumRoles();
    if (roles) {
        OIIO::print(out, "Known roles:\n");
        for (int i = 0; i < roles; ++i) {
            const char* r = colorconfig.getRoleByIndex(i);
            OIIO::print(out, "    - {} -> {}\n", quote_if_spaces(r),
                        quote_if_spaces(colorconfig.getColorSpaceNameByRole(r)));
        }
    }

    int nlooks = colorconfig.getNumLooks();
    if (nlooks) {
        OIIO::print(out, "Known looks:\n");
        for (int i = 0; i < nlooks; ++i)
            OIIO::print(out, "    - {}\n",
                        quote_if_spaces(colorconfig.getLookNameByIndex(i)));
    }

    const char* default_display = colorconfig.getDefaultDisplayName();
    int ndisplays               = colorconfig.getNumDisplays();
    if (ndisplays) {
        out << "Known displays: (* indicates default)\n";
        for (int i = 0; i < ndisplays; ++i) {
            const char* d = colorconfig.getDisplayNameByIndex(i);
            out << "    - " << quote_if_spaces(d);
            if (!strcmp(d, default_display))
                out << " (*)";
            const char* default_view = colorconfig.getDefaultViewName(d);
            int nviews               = colorconfig.getNumViews(d);
            if (nviews) {
                out << "\n      ";
                std::stringstream s;
                s << "views: ";
                for (int i = 0; i < nviews; ++i) {
                    const char* v = colorconfig.getViewNameByIndex(d, i);
                    s << quote_if_spaces(v);
                    if (!strcmp(v, default_view))
                        s << " (*)";
                    if (i < nviews - 1)
                        s << ", ";
                }
                out << Strutil::wordwrap(s.str(), columns, 6, " ") /*<< "\n"*/;
            }
            out << "\n";
        }
    }

    int nnamed_transforms = colorconfig.getNumNamedTransforms();
    if (nnamed_transforms) {
        out << "Named transforms:\n";
        for (int i = 0; i < nnamed_transforms; ++i) {
            const char* x = colorconfig.getNamedTransformNameByIndex(i);
            out << "    - " << quote_if_spaces(x) << "\n";
        }
    }
    if (!colorconfig.supportsOpenColorIO())
        out << "No OpenColorIO support was enabled at build time.\n";
}



static void
print_build_info(Oiiotool& ot, std::ostream& out)
{
    using Strutil::fmt::format;
    int columns = Sysutil::terminal_columns() - 2;

    auto platform = format("OIIO {} | {}", OIIO_VERSION_STRING,
                           OIIO::get_string_attribute("build:platform"));
    OIIO::print(out, "{}\n", Strutil::wordwrap(platform, columns, 4));

    auto buildinfo = format("    Build compiler: {} | C++{}/{}",
                            OIIO::get_string_attribute("build:compiler"),
                            OIIO_CPLUSPLUS_VERSION, __cplusplus);
    OIIO::print(out, "{}\n", Strutil::wordwrap(buildinfo, columns, 4));

    auto hwbuildfeats = format("    HW features enabled at build: {}",
                               OIIO::get_string_attribute("build:simd",
                                                          "no SIMD"));
    OIIO::print(out, "{}\n", Strutil::wordwrap(hwbuildfeats, columns, 4));
#ifdef OIIO_USE_CUDA
    int cudaver = OIIO::get_int_attribute("cuda:build_version");
    OIIO::print(out, "    CUDA {}.{}.{} support enabled at build time\n",
                cudaver / 10000, (cudaver / 100) % 100, cudaver % 100);
#else
    OIIO::print(out,
                "    No CUDA support (disabled / unavailable at build time)\n");
#endif

    std::string libs = OIIO::get_string_attribute("build:dependencies");
    if (libs.size()) {
        auto libvec = Strutil::splitsv(libs, ";");
        for (auto& lib : libvec) {
            size_t pos = lib.find(':');
            lib.remove_prefix(pos + 1);
        }
        OIIO::print(out, "{}\n",
                    Strutil::wordwrap("Dependencies: "
                                          + Strutil::join(libvec, ", "),
                                      columns, 4));
    }
}



static void
print_help_end(Oiiotool& ot, std::ostream& out)
{
    OIIO::print(out, "\n");
    int columns = Sysutil::terminal_columns() - 2;

    out << formatted_format_list("Input", "input_format_list") << "\n";
    out << formatted_format_list("Output", "output_format_list") << "\n";

    if (int ociover = ot.colorconfig().OpenColorIO_version_hex())
        OIIO::print(out, "OpenColorIO {}.{}.{}\n", (ociover >> 24),
                    ((ociover >> 16) & 0xff), ((ociover >> 8) & 0xff));
    else
        OIIO::print(out, "No OpenColorIO\n");
    OIIO::print(out, "    Color config: {}\n", ot.colorconfig().configname());
    OIIO::print(out, "    Run `oiiotool --colorconfiginfo` for a "
                     "full color management inventory.\n");

    OIIO::print(
        out, "{}\n",
        Strutil::wordwrap("Filters available: "
                              + Strutil::replace(OIIO::get_string_attribute(
                                                     "filter_list"),
                                                 ";", ", ", true),
                          columns, 4));

    print_build_info(ot, out);

    // Print the current HW info
    auto hwinfo = Strutil::fmt::format("Running on {} cores {:.1f}GB {}",
                                       Sysutil::hardware_concurrency(),
                                       Sysutil::physical_memory()
                                           / float(1 << 30),
                                       OIIO::get_string_attribute("hw:simd"));
    OIIO::print(out, "{}\n", Strutil::wordwrap(hwinfo, columns, 4, " ", ","));
    if (OIIO::get_int_attribute("cuda:devices_found")
        /*pvt::compute_device() == pvt::ComputeDevice::CUDA*/) {
        auto compinfo = Strutil::fmt::format(
            "Compute hardware available{}: CUDA on {}, driver {}, runtime {}, compat {}, memory {:.1f} GB",
            pvt::compute_device() == pvt::ComputeDevice::CUDA
                ? ""
                : " (but not enabled)",
            OIIO::get_string_attribute("cuda:device_name"),
            OIIO::get_int_attribute("cuda:driver_version"),
            OIIO::get_int_attribute("cuda:runtime_version"),
            OIIO::get_int_attribute("cuda:compatibility"),
            OIIO::get_int_attribute("cuda:total_memory_MB") / 1024.0);
        OIIO::print(out, "{}\n",
                    Strutil::wordwrap(compinfo, columns, 4, " ", ","));
    } else {
        OIIO::print(out, "    No compute specific hardware enabled.\n");
    }

    // Print the path to the docs. If found, use the one installed in the
    // same area is this executable, otherwise just point to the copy on
    // GitHub corresponding to our version of the softare.
    OIIO::print(out, "Full OIIO documentation can be found at\n");
    OIIO::print(out, "    https://docs.openimageio.org\n");
}



static void
print_help(Oiiotool& ot, ArgParse& ap)
{
    ot.ap.print_help();
    print_help_end(ot, std::cout);
}



static void
list_formats(Oiiotool& ot, cspan<const char*> argv)
{
    int columns = Sysutil::terminal_columns() - 2;
    std::cout << "All OIIO supported formats and their extensions:\n";
    auto map = OIIO::get_extension_map();
    for (const auto& f : map) {
        auto s = Strutil::fmt::format("    {} : {}", f.first,
                                      Strutil::join(f.second, ", "));
        std::cout << Strutil::wordwrap(s, columns, 8) << "\n";
    }
    ot.printed_info = true;
}



static void
oiiotool_unit_tests(Oiiotool& ot)
{
#ifdef OIIO_UNIT_TESTS
    using Strutil::print;
    OIIO::print("Running unit tests...\n");
    auto e       = ot.noerrexit;
    ot.noerrexit = true;
    unit_test_scan_box();
    unit_test_adjust_geometry(ot);
    ot.noerrexit = e;
    OIIO::print("...end of unit tests\n");
#endif
}



void
Oiiotool::getargs(int argc, char* argv[])
{
    Oiiotool& ot(*this);  // Local reference alias for *this

// Macro that wraps a call to prepend a ref to the ot
#define OTACTION(act) \
    action([&ot](cspan<const char*> argv) { return act(ot, argv); })
// Macro that wraps a call to an ot method
#define OTMACTION(act) \
    action([&ot](cspan<const char*> argv) { return ot.act(argv); })

    bool help = false;

    bool sansattrib = false;
    for (int i = 0; i < argc; ++i)
        if (!strcmp(argv[i], "--sansattrib") || !strcmp(argv[i], "-sansattrib"))
            sansattrib = true;
    ot.full_command_line = command_line_string(argc, argv, sansattrib);

    // clang-format off
    ap.intro("oiiotool -- simple image processing operations\n"
              OIIO_INTRO_STRING)
      .usage("oiiotool [filename|command]...")
      .description(print_usage_tips())
      .add_help(false)
      .exit_on_error(false);

    ap.arg("filename")
      .hidden()
      .OTACTION(input_file);

    ap.separator("Options (general flags):");
    ap.arg("--help", &help)
      .help("Print help message");
    ap.arg("--version")
      .help("Print version")
      .action([&](cspan<const char*>){
            Strutil::print("{}\n", OIIO_VERSION_STRING);
            ot.printed_info = true;
        });
    ap.arg("--unittest")
      .hidden()
      .action([&](cspan<const char*>){
            oiiotool_unit_tests(ot);
      });
    ap.arg("-v", &ot.verbose)
      .help("Verbose status messages");
    ap.arg("-q")
      .help("Quiet mode (turn verbose off and reduce printed output)")
      .action([&](cspan<const char*>){ ot.verbose = false; ot.quiet = true; });
    ap.arg("-n", &ot.dryrun)
      .help("No saved output (dry run)");
    ap.arg("--no-error-exit", ot.noerrexit)
      .help("Do not exit upon error, try to process additional commands (danger!)");
    ap.arg("-a", &ot.allsubimages)
      .help("Do operations on all subimages/miplevels");
    ap.arg("--debug", &ot.debug)
      .help("Debug mode");
    ap.arg("--runstats", &ot.runstats)
      .help("Print runtime statistics");
    ap.arg("--buildinfo")
      .help("Print OIIO build information")
      .action([&](cspan<const char*>){
            print_build_info(ot, std::cout);
            ot.printed_info = true;
        });
    ap.arg("--info")
      .help("Print resolution and basic info on all inputs, detailed metadata if -v is also used (options: format=xml:verbose=1)")
      .OTACTION(set_printinfo);
    ap.arg("--list-formats")
      .help("List all supported file formats and their filename extensions")
      .OTACTION(list_formats);
    ap.arg("--metamatch %s:REGEX", &ot.printinfo_metamatch)
      .help("Which metadata is printed with -info -v");
    ap.arg("--no-metamatch %s:REGEX", &ot.printinfo_nometamatch)
      .help("Which metadata is excluded with -info -v");
    ap.arg("--stats", &ot.printstats)
      .help("Print pixel statistics of all inputs files");
    ap.arg("--dumpdata")
      .help("Print all pixel data values of input files (options: empty=1, C=arrayname)")
      .OTACTION(set_dumpdata);
    ap.arg("--hash", &ot.hash)
      .help("Print SHA-1 hash of each input image");
    ap.arg("-u", &ot.updatemode)
      .help("Update mode: skip outputs when the file exists and is newer than all inputs");
    ap.arg("--no-clobber", &ot.noclobber)
      .help("Do not overwrite existing files");
    ap.arg("--noclobber", &ot.noclobber)
      .hidden(); // synonym
    ap.arg("--create-dir", &ot.create_dir)
      .help("Create output directories if it doesn't exists");
    ap.arg("--threads %d:N")
      .help("Number of threads (default 0 == #cores)")
      .OTACTION(set_threads);
    ap.arg("--gpu")
      .help("[EXPERIMENTAL] Use GPU if available (options: device=...)")
      .action([&](cspan<const char*>){
                  OIIO::attribute("gpu:device", "CUDA");
              });
    ap.arg("--no-autopremult")
      .help("Turn off automatic premultiplication of images with unassociated alpha")
      .OTACTION(unset_autopremult);
    ap.arg("--autopremult")
      .help("Turn on automatic premultiplication of images with unassociated alpha")
      .OTACTION(set_autopremult);
    ap.arg("--autoorient", &ot.autoorient)
      .help("Automatically --reorient all images upon input");
    ap.arg("--auto-orient", &ot.autoorient)
      .hidden(); // synonym for --autoorient
    ap.arg("--autocc")
      .help("Automatically color convert based on filename (options: unpremult=)")
      .OTACTION(set_autocc);
    ap.arg("--noautocc %!", &ot.autocc)
      .help("Turn off automatic color conversion");
    ap.arg("--native")
      .help("Keep native pixel data type (bypass cache if necessary)")
      .OTACTION(set_native);
    ap.arg("--cache %d:MB")
      .help("ImageCache size (in MB: default=4096)")
      .OTACTION(set_cachesize);
    ap.arg("--autotile %d:TILESIZE")
      .help("Autotile enable for cached images (the argument is the tile size, default 0 means no autotile)")
      .OTACTION(set_autotile);
    ap.arg("--metamerge", &ot.metamerge)
      .help("Always merge metadata of all inputs into output");
    ap.arg("--oiioattrib %s:NAME %s:VALUE")
      .help("Sets global OpenImageIO attribute (options: type=...)")
      .OTACTION(set_oiio_attribute);
    ap.arg("--nostderr", &ot.nostderr)
      .help("Do not use stderr, output error messages to stdout")
      .hidden();
    ap.arg("--experimental", &ot.experimental)
      .help("Allow experimental features");

    ap.separator("Control flow and scripting:");
    ap.arg("--set %s:NAME %s:VALUE")
      .help("Set a user variable (options: type=...)")
      .OTACTION(set_user_variable);
    ap.arg("--if %s:VALUE")
      .help("If VALUE is not 0 or empty, execute commands until --endif")
      .OTACTION(control_if)
      .always_run();
    ap.arg("--else")
      .help("Else clause of the current 'if' block")
      .OTACTION(control_else)
      .always_run();
    ap.arg("--endif")
      .help("End the current 'if' block")
      .OTACTION(control_endif)
      .always_run();
    ap.arg("--while %s:VALUE")
      .help("If VALUE is not 0 or empty, execute commands until --endwhile and loop")
      .OTACTION(control_while)
      .always_run();
    ap.arg("--endwhile")
      .help("End the current 'while' block")
      .OTACTION(control_endwhile)
      .always_run();
    ap.arg("--for %s:VARIABLE %s:RANGE")
      .help("Iterate over a range the commands between here and --endfor. "
            " The range may be END (implied begin 0 and step 1), START,END (implied step 1) or START,END,STEP")
      .OTACTION(control_for)
      .always_run();
    ap.arg("--endfor")
      .help("End the current 'for' block")
      .OTACTION(control_endfor)
      .always_run();
    ap.arg("--frames %s:FRAMERANGE")
      .help("Frame range for '#' or printf-style wildcards");
    ap.arg("--framepadding %d:NDIGITS", &ot.frame_padding)
      .help("Frame number padding digits (ignored when using printf-style wildcards)");
    ap.arg("--views %s:VIEWNAMES")
      .help("Views for %V/%v wildcards (comma-separated, defaults to \"left,right\")");
    ap.arg("--skip-bad-frames", &ot.skip_bad_frames)
      .help("Skip to next frame in range if there's an error, rather than exiting");
    ap.arg("--parallel-frames")
      .help("Parallelize evaluation of frame range");
    ap.arg("--wildcardoff")
      .help("Disable numeric wildcard expansion for subsequent command line arguments");
    ap.arg("--wildcardon")
      .help("Enable numeric wildcard expansion for subsequent command line arguments");
    ap.arg("--evaloff")
      .help("Disable {expression} evaluation for subsequent command line arguments")
      .action([&](cspan<const char*>){ ot.eval_enable = false; });
    ap.arg("--evalon")
      .help("Enable {expression} evaluation for subsequent command line arguments")
      .action([&](cspan<const char*>){ ot.eval_enable = true; });
    ap.arg("--crash")
      .hidden()
      .action(crash_me);
    ap.arg("--test-bad-format")
      .hidden()
      .action([&](cspan<const char*>){
                  OIIO::print("{}\n", Strutil::fmt::format("hey hey {:d} {}",
                                                     "foo", "bar", "oops"));
              });

    ap.separator("Commands that read images:");
    ap.arg("-i %s:FILENAME")
      .help("Input file (options: autocc=, ch=, get_thumbnail=, info=, "
            "infoformat=, native=, now=, type=, unpremult=)")
      .OTACTION(input_file);
    ap.arg("--iconfig %s:NAME %s:VALUE")
      .help("Sets input config attribute (options: type=...)")
      .OTACTION(set_input_attribute);
    ap.arg("--missingfile %s:OPTION", &ot.missingfile_policy)
      .help("Set policy for missing input files: 'error' (default), 'black', 'checker'");
    ap.arg("--testread %s:FILENAME")
      .help("Test file read (do not keep image; used for debugging)")
      .OTACTION(test_read);

    ap.separator("Commands that write images:");
    ap.arg("-o %s:FILENAME")
      .help("Output the current image to the named file (options: "
            "all=, autocc=, autocrop=, autotrim=, bits=, contig=, datatype=, "
            "dither=, fileformatname=, scanline=, separate=, tile=, unpremult=)")
      .OTACTION(output_file);
    ap.arg("-otex %s:FILENAME")
      .help("Output the current image as a texture")
      .OTACTION(output_file);
    ap.arg("-oenv %s:FILENAME")
      .help("Output the current image as a latlong env map")
      .OTACTION(output_file);
    ap.arg("-obump %s:FILENAME")
      .help("Output the current bump texture map as a 6 channels texture including the first and second moment of the bump slopes (options: bumpformat=height|normal|auto, uvslopes_scale=val>=0, bumpscale=%f, bumpinverts=false, bumpinvertt=false, slopefilter=sobel|centraldiff, bumprange=centered|positive|auto)")
      .OTACTION(output_file);

    ap.separator("Options that affect subsequent image output:");
    ap.arg("-d %s:TYPE")
      .help("'-d TYPE' sets the output data format of all channels, "
            "'-d CHAN=TYPE' overrides a single named channel (multiple -d args are allowed). "
            "Data types include: uint8, sint8, uint10, uint12, uint16, sint16, uint32, sint32, half, float, double")
      .OTACTION(set_dataformat);
    ap.arg("--scanline", &ot.output_scanline)
      .help("Output scanline images");
    ap.arg("--tile %d:WIDTH %d:HEIGHT", &ot.output_tilewidth, &ot.output_tileheight)
      .help("Output tiled images with this tile size")
      .OTACTION(output_tiles);
    ap.arg("--force-tiles", &ot.output_force_tiles)
      .hidden(); // undocumented
    ap.arg("--compression %s:NAME")
      .help("Set the compression method (in the form \"name\" or \"name:quality\")")
      .OTACTION(set_compression);
    ap.arg("--quality %d:QUALITY", &ot.output_quality)
      .hidden(); // DEPRECATED(2.1)
    ap.arg("--dither", &ot.output_dither)
      .help("Add dither when writing <= 8-bit output from > 8 bit input");
    ap.arg("--planarconfig %s:CONFIG", &ot.output_planarconfig)
      .help("Force planarconfig (contig, separate, default)");
    ap.arg("--adjust-time", &ot.output_adjust_time)
      .help("Adjust file times to match DateTime metadata");
    ap.arg("--noautocrop %!", &ot.output_autocrop)
      .help("Do not automatically crop images whose formats don't support separate pixel data and full/display windows");
    ap.arg("--autotrim", &ot.output_autotrim)
      .help("Automatically trim black borders upon output to file formats that support separate pixel data and full/display windows");

    ap.separator("Options that print data (usually about the current image):");
    ap.arg("--echo %s:TEXT")
      .help("Echo message to console (options: newline=0)")
      .OTACTION(do_echo);
    ap.arg("--printinfo")
      .help("Print info and metadata of the current top image (options: allsubimages=, native=1, stats=1, verbose=0)")
      .OTACTION(action_printinfo);
    ap.arg("--printstats")
      .help("Print pixel statistics of the current top image (options: allsubimages=, window=<geom>)")
      .OTACTION(action_printstats);
    ap.arg("--colorcount %s:COLORLIST")
       .help("Count of how many pixels have the given color (argument: color;color;...) (options: eps=color)")
       .OTACTION(action_colorcount);
    ap.arg("--rangecheck %s:MIN %s:MAX")
       .help("Count of how many pixels are outside the min/max color range (each is a comma-separated color value list)")
       .OTACTION(action_rangecheck);

    ap.separator("Options that change current image metadata (but not pixel values):");
    ap.arg("--attrib %s:NAME %s:VALUE")
      .help("Sets metadata attribute (options: type=...)")
      .OTACTION(action_attrib);
    ap.arg("--sattrib %s:NAME %s:VALUE")
      .help("Sets string metadata attribute")
      .OTACTION(action_sattrib);
    ap.arg("--eraseattrib %s:REGEX")
      .help("Erase attributes matching regex (options: fromfile=1 filename-containing-patterns")
      .OTACTION(erase_attribute);
    ap.arg("--caption %s:TEXT")
      .help("Sets caption (ImageDescription metadata)")
      .OTACTION(set_caption);
    ap.arg("--keyword %s:KEYWORD")
      .help("Add a keyword")
      .OTACTION(set_keyword);
    ap.arg("--clear-keywords")
      .help("Clear all keywords")
      .OTACTION(clear_keywords);
    ap.arg("--history", &ot.metadata_history)
      .help("Write full command line into Exif:ImageHistory, Software metadata attributes");
    ap.arg("--no-history %!", &ot.metadata_history)
      .help("Do not write full command line into Exif:ImageHistory, Software metadata attributes");
    ap.arg("--nosoftwareattrib", &ot.metadata_nosoftwareattrib)
      .help("Do not write any Exif:ImageHistory or Software metadata attributes");
    ap.arg("--sansattrib", &sansattrib)
      .help("Write command line into Software & ImageHistory but remove --sattrib and --attrib options");
    ap.arg("--orientation %d:ORIENT")
      .help("Set the assumed orientation")
      .OTACTION(set_orientation);
    ap.arg("--orientcw")
      .help("Rotate orientation metadata 90 deg clockwise")
      .OTACTION(rotate_orientation);
    ap.arg("--orientccw")
      .help("Rotate orientation metadata 90 deg counter-clockwise")
      .OTACTION(rotate_orientation);
    ap.arg("--orient180")
      .help("Rotate orientation metadata 180 deg")
      .OTACTION(rotate_orientation);
    ap.arg("--rotcw")
      .hidden() // DEPRECATED(1.5), back compatibility
      .OTACTION(rotate_orientation);
    ap.arg("--rotccw")
      .hidden() // DEPRECATED(1.5), back compatibility
      .OTACTION(rotate_orientation);
    ap.arg("--rot180")
      .hidden() // DEPRECATED(1.5), back compatibility
      .OTACTION(rotate_orientation);
    ap.arg("--origin %s:+X+Y")
      .help("Set the pixel data window origin (e.g. +20+10, -16-16)")
      .OTACTION(set_origin);
    ap.arg("--originoffset %s:+X+Y")
      .help("Offset the pixel data window origin from its current position (e.g. +20+10, -16-16)")
      .OTACTION(offset_origin);
    ap.arg("--fullsize %s:GEOM")
      .help("Set the display window (e.g., 1920x1080, 1024x768+100+0, -20-30)")
      .OTACTION(set_fullsize);
    ap.arg("--fullpixels")
      .help("Set the 'full' image range to be the pixel data window")
      .OTACTION(set_full_to_pixels);
    ap.arg("--chnames %s:NAMELIST")
      .help("Set the channel names (comma-separated)")
      .OTACTION(action_set_channelnames);

    ap.separator("Options that affect subsequent actions:");
    ap.arg("--fail %g:THRESH", &ot.diff_failthresh)
      .help("Failure threshold difference (0.000001)");
    ap.arg("--failpercent %g:PCNT", &ot.diff_failpercent)
      .help("Allow this percentage of failures in diff (0)");
    ap.arg("--hardfail %g:THRESH", &ot.diff_hardfail)
      .help("Fail diff if any one pixel exceeds this error (infinity)");
    ap.arg("--warn %g:THRESH", &ot.diff_warnthresh)
      .help("Warning threshold difference (0.00001)");
    ap.arg("--warnpercent %g:PCNT", &ot.diff_warnpercent)
      .help("Allow this percentage of warnings in diff (0)");
    ap.arg("--hardwarn %g:THRESH", &ot.diff_hardwarn)
      .help("Warn if any one pixel difference exceeds this error (infinity)");

    ap.separator("Actions:");
    ap.arg("--create %s:GEOM %d:NCHANS")
      .help("Create a blank image")
      .OTACTION( action_create);
    ap.arg("--pattern %s:NAME %s:GEOM %d:NCHANS")
      .help("Create a patterned image. Pattern name choices: black, constant, fill, checker, noise")
      .OTACTION( action_pattern);
    ap.arg("--kernel %s:NAME %s:GEOM")
      .help("Create a centered convolution kernel")
      .OTACTION(action_kernel);
    ap.arg("--capture")
          .help("Capture an image (options: camera=%d)")
      .OTACTION(action_capture);
    ap.arg("--diff")
      .help("Print report on the difference of two images (modified by --fail, --failpercent, --hardfail, --warn, --warnpercent --hardwarn)")
      .OTACTION(action_diff);
    ap.arg("--pdiff")
      .help("Print report on the Yee perceptual difference of two images (modified by --fail, --failpercent, --hardfail, --warn, --warnpercent --hardwarn)")
      .OTACTION(action_pdiff);
    ap.arg("--flipdiff")
      .help("Compute FLIP perceptual difference of two images (options: hdr=1, colormap=NAME, ppd=67.02, maxluminance=2.0)")
      .OTACTION(action_flipdiff);
    ap.arg("--add")
      .help("Add two images")
      .OTACTION(action_add);
    ap.arg("--addc %s:VAL")
      .help("Add to all channels a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_addc);
    ap.arg("--cadd %s:VAL")
      .hidden() // Deprecated synonym
      .OTACTION(action_addc);
    ap.arg("--sub")
      .help("Subtract two images")
      .OTACTION(action_sub);
    ap.arg("--subc %s:VAL")
      .help("Subtract from all channels a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_subc);
    ap.arg("--csub %s:VAL")
      .hidden() // Deprecated synonym
      .OTACTION(action_subc);
    ap.arg("--scale")
      .help("Scale all channels of one image by the single channel of another image")
      .OTACTION(action_scale);
    ap.arg("--mul")
      .help("Multiply two images")
      .OTACTION(action_mul);
    ap.arg("--mulc %s:VAL")
      .help("Multiply the image values by a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_mulc);
    ap.arg("--cmul %s:VAL")
      .hidden() // Deprecated synonym
      .OTACTION(action_mulc);
    ap.arg("--div")
      .help("Divide first image by second image")
      .OTACTION(action_div);
    ap.arg("--divc %s:VAL")
      .help("Divide the image values by a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_divc);
    ap.arg("--mad")
      .help("Multiply two images, add a third")
      .OTACTION(action_mad);
    ap.arg("--invert")
      .help("Take the color inverse (subtract from 1) (options: chbegin=0, chend=3")
      .OTACTION(action_invert);
    ap.arg("--abs")
      .help("Take the absolute value of the image pixels")
      .OTACTION(action_abs);
    ap.arg("--absdiff")
      .help("Absolute difference between two images")
      .OTACTION(action_absdiff);
    ap.arg("--absdiffc %s:VAL")
      .help("Absolute difference versus a scalar or per-channel constant (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_absdiffc);
    ap.arg("--powc %s:VAL")
      .help("Raise the image values to a scalar or per-channel power (e.g.: 2.2 or 2.2,2.2,2.2,1.0)")
      .OTACTION(action_powc);
    ap.arg("--cpow %s:VAL")
      .hidden() // Deprecated synonym
      .OTACTION(action_powc);
    ap.arg("--noise")
      .help("Add noise to an image (options: type=gaussian:mean=0:stddev=0.1, type=uniform:min=0:max=0.1, type=salt:value=0:portion=0.1, seed=0")
      .OTACTION(action_noise);
    ap.arg("--chsum")
      .help("Turn into 1-channel image by summing channels (options: weight=r,g,...)")
      .OTACTION(action_chsum);
    ap.arg("--colormap %s:MAPNAME")
      .help("Color map based on channel 0 (arg: \"inferno\", \"viridis\", \"magma\", \"turbo\", \"plasma\", \"blue-red\", \"spectrum\", \"heat\", or comma-separated list of RGB triples)")
      .OTACTION(action_colormap);
    ap.arg("--cryptomatte-colors %s:NAME")
      .help("Convert the named cryptomatte channels into a color matte image")
      .OTACTION(action_cryptomatte_colors);
    ap.arg("--crop %s:GEOM")
      .help("Set pixel data resolution and offset, cropping or padding if necessary (WxH+X+Y or xmin,ymin,xmax,ymax)")
      .OTACTION(action_crop);
    ap.arg("--croptofull")
      .help("Crop or pad to make pixel data region match the \"full\" region")
      .OTACTION(action_croptofull);
    ap.arg("--trim")
      .help("Crop to the minimal ROI containing nonzero pixel values")
      .OTACTION(action_trim);
    ap.arg("--cut %s:GEOM")
      .help("Cut out the ROI and reposition to the origin (WxH+X+Y or xmin,ymin,xmax,ymax)")
      .OTACTION(action_cut);
    ap.arg("--paste %s:+X+Y")
      .help("Paste fg over bg at the given position (e.g., +100+50; '-' or 'auto' indicates using the data window position as-is; options: all=%d, mergeroi=%d)")
      .OTACTION(action_paste);
    ap.arg("--pastemeta")
      .help("Paste the metadata from the first image into the second image (options: merge=%d, pattern=REGEX)")
      .OTACTION(action_pastemeta);
    ap.arg("--mosaic %s:WxH")
      .help("Assemble images into a mosaic (arg: WxH; options: pad=0, fit=WxH)")
      .OTACTION(action_mosaic);
    ap.arg("--over")
      .help("'Over' composite of two images")
      .OTACTION(action_over);
    ap.arg("--zover")
      .help("Depth composite two images with Z channels (options: zeroisinf=%d)")
      .OTACTION(action_zover);
    ap.arg("--deepmerge")
      .help("Merge/composite two deep images")
      .OTACTION(action_deepmerge);
    ap.arg("--deepholdout")
      .help("Hold out one deep image by another")
      .OTACTION(action_deepholdout);
    ap.arg("--rotate90")
      .help("Rotate the image 90 degrees clockwise")
      .OTACTION(action_rotate90);
    ap.arg("--rotate180")
      .help("Rotate the image 180 degrees")
      .OTACTION(action_rotate180);
    ap.arg("--flipflop")
      .hidden() // Deprecated synonym for --rotate180
      .OTACTION(action_rotate180);
    ap.arg("--rotate270")
      .help("Rotate the image 270 degrees clockwise (or 90 degrees CCW)")
      .OTACTION(action_rotate270);
    ap.arg("--flip")
      .help("Flip the image vertically (top<->bottom)")
      .OTACTION(action_flip);
    ap.arg("--flop")
      .help("Flop the image horizontally (left<->right)")
      .OTACTION(action_flop);
    ap.arg("--reorient")
      .help("Rotate and/or flop the image to transform the pixels to match the Orientation metadata")
      .OTACTION(action_reorient);
    ap.arg("--transpose")
      .help("Transpose the image")
      .OTACTION(action_transpose);
    ap.arg("--cshift %s:+X+Y")
      .help("Circular shift the image (e.g.: +20-10)")
      .OTACTION(action_cshift);
    ap.arg("--resample %s:GEOM")
      .help("Resample (640x480, 50%) (options: interp=0)")
      .OTACTION(action_resample);
    ap.arg("--resize %s:GEOM")
      .help("Resize (640x480, 50%) (options: from=<geom>, to=<geom>, filter=%s, highlightcomp=%d, edgeclamp=%d)")
      .OTACTION(action_resize);
    ap.arg("--fit %s:GEOM")
      .help("Resize to fit within a window size (options: filter=%s, pad=%d, fillmode=%s, exact=%d, highlightcomp=%d)")
      .OTACTION(action_fit);
    ap.arg("--pixelaspect %g:ASPECT")
      .help("Scale up the image's width or height to match the given pixel aspect ratio (options: filter=%s, highlightcomp=%d)")
      .OTACTION(action_pixelaspect);
    ap.arg("--rotate %g:DEGREES")
      .help("Rotate pixels (degrees clockwise) around the center of the display window (options: filter=%s, center=%f,%f, recompute_roi=%d, highlightcomp=%d")
      .OTACTION(action_rotate);
    ap.arg("--warp %s:MATRIX")
      .help("Warp pixels (argument is a 3x3 matrix, separated by commas) (options: filter=%s, recompute_roi=%d, highlightcomp=%d)")
      .OTACTION(action_warp);
    ap.arg("--st_warp")
      .help("Warp the first image using normalized \"st\" coordinates from the second image (options: filter=%s, chan_s=0, chan_t=1, flip_s=0, flip_t=0)")
      .OTACTION(action_st_warp);
    ap.arg("--convolve")
      .help("Convolve with a kernel")
      .OTACTION(action_convolve);
    ap.arg("--blur %s:WxH")
      .help("Blur the image (options: kernel=name)")
      .OTACTION(action_blur);
    ap.arg("--median %s:WxH")
      .help("Median filter the image")
      .OTACTION(action_median);
    ap.arg("--dilate %s:WxH")
      .help("Dilate (area maximum) the image")
      .OTACTION(action_dilate);
    ap.arg("--erode %s:WxH")
      .help("Erode (area minimum) the image")
      .OTACTION(action_erode);
    ap.arg("--unsharp")
      .help("Unsharp mask (options: kernel=gaussian, width=3, contrast=1, threshold=0)")
      .OTACTION(action_unsharp);
    ap.arg("--laplacian")
      .help("Laplacian filter the image")
      .OTACTION(action_laplacian);
    ap.arg("--normalize")
      .help("Normalize the image (options: incenter=0.5, outcenter=0.5, scale=0.5)")
	  .OTACTION(action_normalize);
    ap.arg("--fft")
      .help("Take the FFT of the image")
      .OTACTION(action_fft);
    ap.arg("--ifft")
      .help("Take the inverse FFT of the image")
      .OTACTION(action_ifft);
    ap.arg("--polar")
      .help("Convert complex (real,imag) to polar (amplitude,phase)")
      .OTACTION(action_polar);
    ap.arg("--unpolar")
      .help("Convert polar (amplitude,phase) to complex (real,imag)")
      .OTACTION(action_unpolar);
    ap.arg("--fixnan %s:STRATEGY")
      .help("Fix NaN/Inf values in the image (choices: none, black, box3, error)")
      .OTACTION(action_fixnan);
    ap.arg("--fillholes")
      .help("Fill in holes (where alpha is not 1)")
      .OTACTION(action_fillholes);
    ap.arg("--max")
      .help("Pixel-by-pixel max of two images")
      .OTACTION(action_max);
    ap.arg("--maxc %s:VAL")
      .help("Max all values with a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_maxc);
    ap.arg("--maxchan")
      .help("Maximum of all channels of the image")
      .OTACTION(action_maxchan);
    ap.arg("--min")
      .help("Pixel-by-pixel min of two images")
      .OTACTION(action_min);
    ap.arg("--minc %s:VAL")
      .help("Min all values with a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .OTACTION(action_minc);
    ap.arg("--minchan")
      .help("Minimum of all channels of the image")
      .OTACTION(action_minchan);
    ap.arg("--clamp")
      .help("Clamp values (options: min=..., max=..., clampalpha=0)")
      .OTACTION(action_clamp);
    ap.arg("--contrast")
      .help("Remap values (options: black=0..., white=1..., sthresh=0.5..., scontrast=1.0..., gamma=1, clamp=0|1)")
      .OTACTION(action_contrast);
    ap.arg("--saturate %f:SCALE")
      .help("Scale saturation of the color channels")
      .OTACTION(action_saturate);
    ap.arg("--decorrstretch")
      .help("Decorrelation stretch, exaggerating subtle color variation (options: firstchannel=0, nchannels=, scale=1.0, sigma=, mean=, percentile=, mode=covariance|correlation)")
      .OTACTION(action_decorrstretch);
    ap.arg("--rangecompress")
      .help("Compress the range of pixel values with a log scale (options: luma=0|1)")
      .OTACTION(action_rangecompress);
    ap.arg("--rangeexpand")
      .help("Un-rangecompress pixel values back to a linear scale (options: luma=0|1)")
      .OTACTION(action_rangeexpand);
    ap.arg("--line %s:X1,Y1,X2,Y2,...")
      .help("Render a poly-line (options: color=)")
      .OTACTION(action_line);
    ap.arg("--point %s:X1,Y1,X2,Y2,...")
      .help("Render points (options: color=)")
      .OTACTION(action_point);
    ap.arg("--box %s:X1,Y1,X2,Y2")
      .help("Render a box (options: color=)")
      .OTACTION(action_box);
    ap.arg("--fill %s:GEOM")
      .help("Fill a region (options: color=)")
      .OTACTION(action_fill);
    ap.arg("--text %s:TEXT")
      .help("Render text into the current image (options: x=, y=, size=, color=)")
      .OTACTION(action_text);
    ap.arg("--demosaic")
      .help("Demosaic (options: pattern=%s, algorithm=%s, layout=%s, white_balance=%f:R,G1,G2,B)")
      .OTACTION(action_demosaic);

    ap.separator("Manipulating channels or subimages:");
    ap.arg("--ch %s:CHANLIST")
      .help("Select or shuffle channels (e.g., \"R,G,B\", \"B,G,R\", \"2,3,4\")")
      .OTACTION(action_channels);
    ap.arg("--nchannels %d:N")
      .help("Force the current image to have N channels (padding with 0.0 as needed)")
      .OTACTION(action_nchannels);
    ap.arg("--chappend")
      .help("Append the channels of the last two images")
      .OTACTION(action_chappend);
    ap.arg("--unmip")
      .help("Discard all but the top level of a MIPmap")
      .OTACTION(action_unmip);
    ap.arg("--selectmip %d:MIPLEVEL")
      .help("Select just one MIP level (0 = highest res)")
      .OTACTION(action_selectmip);
    ap.arg("--subimage %s:SUBIMAGEINDEX")
      .help("Select just one subimage by index or name (options: delete=1)")
      .OTACTION(action_select_subimage);
    ap.arg("--sisplit")
      .help("Split the top image's subimges into separate images")
      .OTACTION(action_subimage_split);
    ap.arg("--siappend")
      .help("Append the last two images into one multi-subimage image")
      .OTACTION(action_subimage_append);
    ap.arg("--siappendall")
      .help("Append all images on the stack into a single multi-subimage image")
      .OTACTION(action_subimage_append_all);
    ap.arg("--layersplit")
      .help("Split the top image's channel-name-based layers into separate images on the stack")
      .OTACTION(action_layer_split);
    ap.arg("--deepen")
      .help("Deepen normal 2D image to deep")
      .OTACTION(action_deepen);
    ap.arg("--flatten")
      .help("Flatten deep image to non-deep")
      .OTACTION(action_flatten);
    ap.arg("--thumbnail-get")
      .help("Extract an embedded thumbnail (options: fail=, index=)")
      .OTACTION(action_thumbnail_get);
    ap.arg("--thumbnail-set")
      .help("Attach the top image as the thumbnail of the image below it")
      .OTACTION(action_thumbnail_set);

    ap.separator("Image stack manipulation:");
    ap.arg("--label %s")
      .help("Label the top image")
      .OTACTION(action_label);
    ap.arg("--dup")
      .help("Duplicate the current image (push a copy onto the stack)")
      .OTACTION(action_dup);
    ap.arg("--swap")
      .help("Swap the top two images on the stack.")
      .OTACTION(action_swap);
    ap.arg("--pop")
      .help("Throw away the current image")
      .OTACTION(action_pop);
    ap.arg("--popbottom")
      .help("Throw away the image on the bottom of the stack")
      .OTACTION(action_popbottom);
    ap.arg("--stackreverse")
      .help("Throw away the image on the bottom of the stack")
      .OTACTION(action_stackreverse);
    ap.arg("--stackextract %d:INDEX")
      .help("Move an indexed stack item to the top of the stack")
      .OTACTION(action_stackextract);
    ap.arg("--stackclear")
      .help("Remove all images from the stack, leaving it empty")
      .OTACTION(action_stackclear);

    ap.separator("Color management:");
    ap.arg("--colorconfiginfo")
      .help("Print extensive details about the color management configuration")
      .action([&](cspan<const char*>){
            print_ocio_info(ot, std::cout);
            ot.printed_info = true;
        });
    ap.arg("--colorconfig %s:FILENAME")
      .help("Explicitly specify an OCIO configuration file")
      .OTACTION(set_colorconfig);
    ap.arg("--iscolorspace %s:COLORSPACE")
      .help("Set the assumed color space (without altering pixels)")
      .OTACTION(action_iscolorspace);
    ap.arg("--tocolorspace %s:COLORSPACE")
      .help("Convert the current image's pixels to a named color space")
      .OTACTION(action_tocolorspace);
    ap.arg("--colorconvert %s:SRC %s:DST")
      .help("Convert pixels from 'src' to 'dst' color space (options: key=, value=, unpremult=, strict=)")
      .OTACTION(action_colorconvert);
    ap.arg("--ccmatrix %s:MATRIXVALS")
      .help("Color convert pixels with a 3x3 or 4x4 matrix (options: unpremult=,transpose=)")
      .OTACTION(action_ccmatrix);
    ap.arg("--ociolook %s:LOOK")
      .help("Apply the named OCIO look (options: from=, to=, inverse=, key=, value=, unpremult=)")
      .OTACTION(action_ociolook);
    ap.arg("--ociodisplay %s:DISPLAY %s:VIEW")
      .help("Apply the named OCIO display and view (options: from=, looks=, key=, value=, unpremult=, inverse=)")
      .OTACTION(action_ociodisplay);
    ap.arg("--ociofiletransform %s:FILENAME")
      .help("Apply the named OCIO filetransform (options: inverse=, unpremult=)")
      .OTACTION(action_ociofiletransform);
    ap.arg("--ocionamedtransform %s:NAME")
      .help("Apply the named OCIO namedtransform (options: inverse=, key=, value=, unpremult=)")
      .OTACTION(action_ocionamedtransform);
    ap.arg("--unpremult")
      .help("Divide all color channels of the current image by the alpha to \"un-premultiply\"")
      .OTACTION(action_unpremult);
    ap.arg("--premult")
      .help("Multiply all color channels of the current image by the alpha")
      .OTACTION(action_premult);
    ap.arg("--repremult")
      .help("Multiply all color channels of the current image by the alpha, but don't crush alpha=0 pixels to black.")
      .OTACTION(action_repremult);
    ap.arg("--iccwrite %s:FILENAME")
      .help("Output the current image's ICC profile as a separate file")
      .OTACTION(icc_write);
    ap.arg("--iccread %s:FILENAME")
      .help("Add the contents of the file to the top image as its ICC profile")
      .OTACTION(icc_read);
    ap.arg("--cicp %s:CICP")
       .help("Set or modifiy CICP metadata for supporting output formats (e.g., \"12,16,0,1\")") //; selectively persist existing values if not specified (e.g., \",,,0\")")
       .OTACTION(action_cicp);
    // clang-format on

    if (ap.parse_args(argc, (const char**)argv) < 0) {
        auto& errstream(ot.nostderr ? std::cout : std::cerr);
        errstream << ap.geterror() << std::endl;
        if (!ot.quiet)
            print_help(ot, ap);
        // Repeat the command line, so if oiiotool is being called from a
        // script, it's easy to debug how the command was mangled.
        errstream << "\nFull command line was:\n> " << ot.full_command_line
                  << "\n";
        ap.abort();
        ot.return_value = EXIT_FAILURE;
        // exit(EXIT_FAILURE);
    }
    if (help || ap["help"].get<int>()) {
        print_help(ot, ap);
        ap.abort();
        // exit(EXIT_SUCCESS);
    }
    if (argc <= 1) {
        if (!ot.quiet) {
            ap.briefusage();
            std::cout << "\nFor detailed help: oiiotool --help\n";
        }
        ap.abort();
        // exit(EXIT_SUCCESS);
    }
#undef OTACTION
#undef OTMACTION
}



void
Oiiotool::merge_stats(const Oiiotool& ot)
{
    std::lock_guard<std::mutex> lock(m_stat_mutex);
    total_readtime.add_ticks(ot.total_readtime.ticks());
    total_writetime.add_ticks(ot.total_writetime.ticks());
    total_imagecache_readtime += ot.total_imagecache_readtime;
    for (auto& t : ot.function_times) {
        function_times[t.first] += t.second;
    }
    peak_memory = std::max(peak_memory, ot.peak_memory);
    if (ot.return_value != EXIT_SUCCESS)
        return_value = ot.return_value;
    num_outputs += ot.num_outputs;
    printed_info |= ot.printed_info;
}



static void
one_sequence_iteration(Oiiotool& otmain, size_t i, int frame_number,
                       cspan<int>(sequence_args),
                       cspan<std::vector<std::string>> filenames,
                       cspan<const char*> argv_main)
{
    // If another iteration being processed asked us all to abort, don't
    // launch this iteration.
    if (otmain.ap.aborted())
        return;

    if (otmain.debug)
        OIIO::print("Begin sequence iteration {}\n", i);

    // Prepare the arguments for this iteration
    std::vector<const char*> seq_argv(argv_main.begin(), argv_main.end());
    for (size_t a : sequence_args) {
        seq_argv[a] = filenames[a][i].c_str();
        if (otmain.debug)
            OIIO::print("  {} -> {}\n", argv_main[a], seq_argv[a]);
    }

    Oiiotool otit;  // Oiiotool for this iteration
    otit.imagecache               = otmain.imagecache;
    otit.frame_number             = frame_number;
    otit.parent_oiiotool          = &otmain;
    otit.m_in_parallel_frame_loop = otmain.in_parallel_frame_loop();
    otit.getargs((int)seq_argv.size(), (char**)&seq_argv[0]);

    if (otit.ap.aborted()) {
        if (!otit.skip_bad_frames) {
            // If we are allowing bad frames to be a full error, and not just
            // skipping the bad frames only, propagate the abort signal to the
            // main otmain.
            otmain.ap.abort(false);
        }
    } else {
        otmain.process_pending();
        if (otmain.pending_callback())
            otmain.warning(otmain.pending_callback_name(),
                           "pending command never executed");
        if (!otmain.control_stack.empty())
            otmain.warningfmt(otmain.control_stack.top().command,
                              "unterminated {}",
                              otmain.control_stack.top().command);
    }

    // Merge this iteration's stats into the main OT
    otmain.merge_stats(otit);

    // A few settings that may have occurred in the iteration oiiotool must be
    // propagated back up to the main, or certain end-of-run behaviors will be
    // wrong.
    if (otit.verbose)
        otmain.verbose = true;
    if (otit.debug)
        otmain.debug = true;
    if (otit.noerrexit)
        otmain.noerrexit = true;
    if (otit.runstats) {
        std::lock_guard<std::mutex> lock(otmain.m_stat_mutex);
        otmain.runstats = true;
        OIIO::print("End sequence iteration {}: {} (total {}) mem {}\n\n", i,
                    Strutil::timeintervalformat(otit.total_runtime(), 2),
                    Strutil::timeintervalformat(otmain.total_runtime(), 2),
                    Strutil::memformat(Sysutil::memory_used()));
    } else if (otmain.debug) {
        OIIO::print("\n");
    }
}



// Check if any of the command line arguments contains numeric ranges or
// wildcards.  If not, just return 'false'.  But if they do, the
// remainder of processing will happen here (and return 'true').
static bool
handle_sequence(Oiiotool& ot, int argc, const char** argv)
{
    // First, scan the original command line arguments for '#', '@', '%0Nd',
    // '%v' or '%V' characters.  Any found indicate that there are numeric
    // range or wildcards to deal with.  Also look for --frames,
    // --framepadding and --views options.
#define ONERANGE_SPEC  "-?[0-9]+(--?[0-9]+((x|y)-?[0-9]+)?)?"
#define MANYRANGE_SPEC ONERANGE_SPEC "(," ONERANGE_SPEC ")*"
#define VIEW_SPEC      "%[Vv]"
#define SEQUENCE_SPEC        \
    "((" MANYRANGE_SPEC ")?" \
    "((#|@)+|(%[0-9]*d)))"   \
    "|"                      \
    "(" VIEW_SPEC ")"
    static std::regex sequence_re(SEQUENCE_SPEC);
    std::string framespec = "";

    static const char* default_views = "left,right";
    std::vector<string_view> views;
    Strutil::split(default_views, views, ",");

    int framepadding = 0;
    std::vector<int> sequence_args;  // Args with sequence numbers
    std::vector<bool> sequence_is_output;
    int parallel_frame_threads = OIIO::get_int_attribute("threads");
    bool is_sequence           = false;
    bool wildcard_on           = true;
    for (int a = 1; a < argc; ++a) {
        bool is_output     = false;
        bool is_output_all = false;
        if (Strutil::starts_with(argv[a], "-o") && a < argc - 1) {
            is_output = true;
            if (Strutil::contains(argv[a], ":all=")) {
                // skip wildcard expansion for -o:all, because the name
                // will be a pattern for expansion of the subimage number.
                is_output_all = true;
            }
            a++;
        }
        std::string strarg(argv[a]);
        std::match_results<std::string::const_iterator> range_match;
        if (strarg == "--debug" || strarg == "-debug")
            ot.debug = true;
        else if ((strarg == "--frames" || strarg == "-frames")
                 && a < argc - 1) {
            framespec   = argv[++a];
            is_sequence = true;
            // std::cout << "Frame range " << framespec << "\n";
        } else if ((strarg == "--framepadding" || strarg == "-framepadding")
                   && a < argc - 1) {
            int f = Strutil::stoi(argv[++a]);
            if (f >= 1 && f < 10)
                framepadding = f;
        } else if ((strarg == "--views" || strarg == "-views")
                   && a < argc - 1) {
            Strutil::split(argv[++a], views, ",");
        } else if ((strarg == "--threads" || strarg == "-threads")
                   && a < argc - 1) {
            int t = Strutil::stoi(argv[++a]);
            t     = OIIO::clamp(t, 0, int(Sysutil::hardware_concurrency()));
            parallel_frame_threads = t;
        } else if (strarg == "--wildcardoff" || strarg == "-wildcardoff") {
            wildcard_on = false;
        } else if (strarg == "--parallel-frames"
                   || strarg == "-parallel-frames") {
            ot.parallel_frames = true;
        } else if (strarg == "--wildcardon" || strarg == "-wildcardon") {
            wildcard_on = true;
        } else if (wildcard_on && !is_output_all
                   && std::regex_search(strarg, range_match, sequence_re)) {
            is_sequence = true;
            sequence_args.push_back(a);
            sequence_is_output.push_back(is_output);
        }
    }

    // No ranges or wildcards?
    if (!is_sequence)
        return false;

    // For each of the arguments that contains a wildcard, get a normalized
    // pattern in printf style (e.g. "foo.%04d.exr"). Next, either expand the
    // frame pattern to a list of frame numbers and use enumerate_file_sequence
    // to fully elaborate all the filenames in the sequence, or if no frame
    // range was specified, scan the filesystem for matching frames. Output
    // sequences without explicit frame ranges inherit the frame numbers of
    // the first input sequence. It's an error if the sequences are not all
    // of the same length.
    std::vector<std::vector<std::string>> filenames(argc + 1);
    std::vector<std::vector<int>> frame_numbers(argc + 1);
    std::vector<std::vector<string_view>> frame_views(argc + 1);
    std::string normalized_pattern, sequence_framespec;
    size_t nfilenames = 0;
    bool result;
    for (size_t i = 0; i < sequence_args.size(); ++i) {
        int a  = sequence_args[i];
        result = Filesystem::parse_pattern(argv[a], framepadding,
                                           normalized_pattern,
                                           sequence_framespec);
        if (!result) {
            ot.errorfmt("", "Could not parse pattern: {}", argv[a]);
            return true;
        }

        if (sequence_framespec.empty())
            sequence_framespec = framespec;
        if (!sequence_framespec.empty()) {
            Filesystem::enumerate_sequence(sequence_framespec.c_str(),
                                           frame_numbers[a]);
            Filesystem::enumerate_file_sequence(normalized_pattern,
                                                frame_numbers[a],
                                                frame_views[a], filenames[a]);
        } else if (sequence_is_output[i]) {
            // use frame numbers from first sequence
            Filesystem::enumerate_file_sequence(normalized_pattern,
                                                frame_numbers[sequence_args[0]],
                                                frame_views[sequence_args[0]],
                                                filenames[a]);
        } else if (!sequence_is_output[i]) {
            result = Filesystem::scan_for_matching_filenames(normalized_pattern,
                                                             views,
                                                             frame_numbers[a],
                                                             frame_views[a],
                                                             filenames[a]);
            if (!result) {
                ot.errorfmt(
                    "",
                    "No filenames found matching pattern: \"{}\" (did you intend to use --wildcardoff?)",
                    argv[a]);
                return true;
            }
        }

        if (i == 0) {
            nfilenames = filenames[a].size();
        } else if (nfilenames != filenames[a].size()) {
            ot.errorfmt(
                "",
                "Not all sequence specifications matched: {} ({} frames) vs. {} ({} frames)",
                argv[sequence_args[0]], nfilenames, argv[a],
                filenames[a].size());
            return true;
        }
    }

    if (!nfilenames && !framespec.empty()) {
        // Frame sequence specified, but no wildcards used
        Filesystem::enumerate_sequence(framespec, frame_numbers[0]);
        nfilenames = frame_numbers[0].size();
    }

    if (!nfilenames) {
        // No filenames matched the first wildcard pattern
        ot.warning("", "No frame number or views matched the wildcards");
        return false;
    }

    // Make sure frame_numbers[0] has the canonical frame number list
    if (sequence_args.size() && frame_numbers[0].empty())
        frame_numbers[0] = frame_numbers[sequence_args[0]];

    // OK, now we just call getargs once for each item in the sequences,
    // substituting the i-th sequence entry for its respective argument
    // every time.
    // Note: nfilenames really means, number of frame number iterations.
    if (ot.parallel_frames) {
        // If --parframes was used, run the iterations in parallel, but
        // each iteration should itself not try to internally parallelize.
        if (ot.debug)
            OIIO::print("Running {} frames in parallel with {} threads\n",
                        nfilenames, parallel_frame_threads);
        ot.begin_parallel_frame_loop(parallel_frame_threads);
        OIIO::parallel_for(
            uint64_t(0), uint64_t(nfilenames),
            [&](uint64_t i) {
                one_sequence_iteration(ot, i, frame_numbers[0][i],
                                       sequence_args, filenames,
                                       { argv, argv + argc });
            },
            paropt().minitems(1).maxthreads(parallel_frame_threads));
        ot.end_parallel_frame_loop();
    } else {
        // Fully serialized over the frame range, multithreaded for each frame
        // individually.
        for (size_t i = 0; i < nfilenames; ++i) {
            one_sequence_iteration(ot, i, frame_numbers[0][i], sequence_args,
                                   filenames, { argv, argv + argc });
        }
    }
    return true;
}



void
Oiiotool::begin_parallel_frame_loop(int nthreads)
{
    OIIO::attribute("threads", nthreads);
    OIIO::attribute("exr_threads", 1);
    m_in_parallel_frame_loop = true;
}



void
Oiiotool::end_parallel_frame_loop()
{
    m_in_parallel_frame_loop = false;
}



int
main(int argc, char* argv[])
{
#if OIIO_SIMD_SSE && !OIIO_F16C_ENABLED
    // We've found old versions of libopenjpeg (either by itself, or
    // pulled in by ffmpeg libraries that link against it) that upon its
    // dso load will turn on the cpu mode that causes floating point
    // denormals get crushed to 0.0 in certain ops, and leave it that
    // way! This can give us the wrong results for the particular
    // sequence of SSE intrinsics we use to convert half->float for exr
    // files containing pixels with denorm values. Can't fix everywhere,
    // but at least for oiiotool we know it's safe to just fix the flag
    // for our app. We only need to do this if using sse instructions and
    // the f16c hardware half<->float ops are not enabled. This does not
    // seem to be a problem in libopenjpeg > 1.5.
    simd::set_denorms_zero_mode(false);
#endif
    {
        // DEBUG -- this checks some problematic half->float values if the
        // denorms zero mode is not set correctly. Leave this fragment in
        // case we ever need to check it again.
        // using namespace OIIO::simd;
        const unsigned short bad[] = { 59, 12928, 2146, 32805 };
        const half* h              = (half*)bad;
        simd::vfloat4 vf(h);
        if (vf[0] == 0.0f || *h != vf[0])
            Strutil::print(stderr,
                           "Bad half conversion, code {} {} -> {} "
                           "(suspect badly set DENORMS_ZERO_MODE)\n",
                           bad[0], float(h[0]), vf[0]);
    }

    // Helpful for debugging to make sure that any crashes dump a stack
    // trace.
    Sysutil::setup_crash_stacktrace("stdout");

    // Globally force classic "C" locale, and turn off all formatting
    // internationalization, for the entire oiiotool application.
    std::locale::global(std::locale::classic());

    Oiiotool ot;

    ot.imagecache = ImageCache::create();
    OIIO_DASSERT(ot.imagecache);
    ot.imagecache->attribute("forcefloat", 1);
    ot.imagecache->attribute("max_memory_MB", float(ot.cachesize));
    ot.imagecache->attribute("autotile", ot.autotile);
    ot.imagecache->attribute("autoscanline", int(ot.autotile ? 1 : 0));

    Filesystem::convert_native_arguments(argc, (const char**)argv);
    if (handle_sequence(ot, argc, (const char**)argv)) {
        // Deal with sequence

    } else {
        // Not a sequence
        ot.getargs(argc, argv);
        if (!ot.ap.aborted()) {
            ot.process_pending();
            if (ot.pending_callback())
                ot.warning(ot.pending_callback_name(),
                           "pending command never executed");
            if (!ot.control_stack.empty())
                ot.warningfmt(ot.control_stack.top().command, "unterminated {}",
                              ot.control_stack.top().command);
        }
    }

    if (!ot.printinfo && !ot.printstats && !ot.dumpdata && !ot.dryrun
        && !ot.printed_info && !ot.ap.aborted()) {
        if (ot.curimg && !ot.curimg->was_output()
            && (ot.curimg->metadata_modified() || ot.curimg->pixels_modified()))
            ot.warning(
                "",
                "modified images without outputting them. Did you forget -o?");
        else if (ot.num_outputs == 0)
            ot.warning("", "oiiotool produced no output. Did you forget -o?");
    }

    if (ot.runstats) {
        double total_time  = ot.total_runtime();
        double unaccounted = total_time;
        OIIO::print("\n");
        OIIO::print("Threads: {}\n", OIIO::get_int_attribute("threads"));
        OIIO::print("oiiotool runtime statistics:\n");
        OIIO::print("  Total time: {}\n",
                    Strutil::timeintervalformat(total_time, 2));
        for (auto& func : ot.function_times) {
            double t = func.second;
            if (t > 0.0) {
                Strutil::print("      {:<12} : {:5.2f}\n", func.first, t);
                unaccounted -= t;
            }
        }
        if (unaccounted > 0.0)
            Strutil::print("      {:<12} : {:5.2f}\n", "unaccounted",
                           unaccounted);
        ot.check_peak_memory();
        OIIO::print("  Peak memory:    {}\n",
                    Strutil::memformat(ot.peak_memory));
        OIIO::print("  Current memory: {}\n",
                    Strutil::memformat(Sysutil::memory_used()));
        {
            int64_t current = 0, peak = 0;
            OIIO::getattribute("IB_local_mem_current", TypeInt64, &current);
            OIIO::getattribute("IB_local_mem_peak", TypeInt64, &peak);
            OIIO::print("\nImageBuf local memory: current {}, peak {}\n",
                        Strutil::memformat(current), Strutil::memformat(peak));
            float opentime = OIIO::get_float_attribute("IB_total_open_time");
            float readtime = OIIO::get_float_attribute(
                "IB_total_image_read_time");
            OIIO::print("ImageBuf direct read time: {}, open time {}\n",
                        Strutil::timeintervalformat(readtime, 2),
                        Strutil::timeintervalformat(opentime, 2));
        }
        OIIO::print("\n{}\n", ot.imagecache->getstats(2));
    }

    // Release references of images that might hold onto a shared
    // image cache. Otherwise they would get released at static destruction
    // time, at which point due to undefined destruction order the shared
    // cache might be already gone.
    ot.curimg = nullptr;
    ot.image_stack.clear();
    ot.image_labels.clear();
    shutdown();
    return ot.return_value;
}
