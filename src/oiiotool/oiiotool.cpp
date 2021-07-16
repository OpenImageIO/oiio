// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <OpenEXR/ImfTimeCode.h>

#include <OpenImageIO/argparse.h>
#include <OpenImageIO/color.h>
#include <OpenImageIO/deepdata.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/filter.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/timer.h>

#include "oiiotool.h"

using namespace OIIO;
using namespace OiioTool;
using namespace ImageBufAlgo;


static Oiiotool ot;
static ArgParse ap;



// Macro to fully set up the "action" function that straightforwardly calls
// a lambda for each subimage. Beware, the macro expansion rules may require
// you may need to enclose the lambda itself in parenthesis () if there it
// contains commas that are not inside other parentheses.
#define OIIOTOOL_OP(name, ninputs, ...)                               \
    static int action_##name(int argc, const char* argv[])            \
    {                                                                 \
        if (ot.postpone_callback(ninputs, action_##name, argc, argv)) \
            return 0;                                                 \
        OiiotoolOp op(ot, #name, argc, argv, ninputs, __VA_ARGS__);   \
        return op();                                                  \
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
#define OP_CUSTOMCLASS(name, opclass, ninputs)                        \
    static int action_##name(int argc, const char* argv[])            \
    {                                                                 \
        if (ot.postpone_callback(ninputs, action_##name, argc, argv)) \
            return 0;                                                 \
        opclass op(ot, #name, argc, argv);                            \
        return op();                                                  \
    }



Oiiotool::Oiiotool() { clear_options(); }



void
Oiiotool::clear_options()
{
    verbose            = false;
    debug              = false;
    dryrun             = false;
    runstats           = false;
    noclobber          = false;
    allsubimages       = false;
    printinfo          = false;
    printstats         = false;
    dumpdata           = false;
    dumpdata_showempty = true;
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
    first_input_dimensions = ImageSpec();
    output_dataformat      = TypeDesc::UNKNOWN;
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
    diff_warnthresh           = 1.0e-6f;
    diff_warnpercent          = 0;
    diff_hardwarn             = std::numeric_limits<float>::max();
    diff_failthresh           = 1.0e-6f;
    diff_failpercent          = 0;
    diff_hardfail             = std::numeric_limits<float>::max();
    m_pending_callback        = NULL;
    m_pending_argc            = 0;
    frame_number              = 0;
    frame_padding             = 0;
    input_dataformat          = TypeUnknown;
    input_bitspersample       = 0;
    input_channelformats.clear();
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



std::string
format_resolution(int w, int h, int x, int y)
{
    return Strutil::sprintf("%dx%d%+d%+d", w, h, x, y);
}



std::string
format_resolution(int w, int h, int d, int x, int y, int z)
{
    return Strutil::sprintf("%dx%dx%d%+d%+d%+d", w, h, d, x, y, z);
}



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
    if (ot.nativeread)
        readpolicy = ReadPolicy(readpolicy | ReadNative);
    bool ok = img->read(readpolicy, channel_set);
    total_readtime.stop();
    imagecache->getattribute("stat:fileio_time", post_ic_time);
    total_imagecache_readtime += post_ic_time - pre_ic_time;

    // If this is the first tiled image we have come across, use it to
    // set our tile size (unless the user explicitly set a tile size, or
    // explicitly instructed scanline output).
    const ImageSpec& nspec((*img)().nativespec());
    if (nspec.tile_width && !output_tilewidth && !ot.output_scanline) {
        output_tilewidth  = nspec.tile_width;
        output_tileheight = nspec.tile_height;
    }
    // Remember the channel format details of the first example of each
    // channel name that we encounter.
    remember_input_channelformats(img);

    if (!ok)
        error("read", format_read_error(img->name(), img->geterror()));
    return ok;
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

    if (!ok)
        error("read", format_read_error(img->name(), img->geterror()));
    return ok;
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
                if (input_channelformats[chname] != "")
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
                            int argc, const char* argv[])
{
    if (image_stack_depth() < required_images) {
        // Not enough have inputs been specified so far, so put this
        // function on the "pending" list.
        m_pending_callback = func;
        m_pending_argc     = argc;
        for (int i = 0; i < argc; ++i)
            m_pending_argv[i] = ustring(argv[i]).c_str();
        return true;
    }
    return false;
}



bool
Oiiotool::postpone_callback(int required_images, ArgParse::Action func,
                            cspan<const char*> argv)
{
    if (image_stack_depth() < required_images) {
        // Not enough have inputs been specified so far, so put this
        // function on the "pending" list.
        m_pending_action = func;
        m_pending_argc   = int(argv.size());
        for (int i = 0; i < m_pending_argc; ++i)
            m_pending_argv[i] = ustring(argv[i]).c_str();
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
        int argc = m_pending_argc;
        const char* argv[4];
        for (int i = 0; i < argc; ++i)
            argv[i] = m_pending_argv[i];
        CallbackFunction callback = m_pending_callback;
        m_pending_callback        = NULL;
        m_pending_argc            = 0;
        (*callback)(argc, argv);
    }
}



void
Oiiotool::error(string_view command, string_view explanation) const
{
    std::cerr << "oiiotool ERROR";
    if (command.size())
        std::cerr << ": " << command;
    if (explanation.size())
        std::cerr << " : " << explanation;
    else
        std::cerr << " (unknown error)";
    std::cerr << "\n";
    // Repeat the command line, so if oiiotool is being called from a
    // script, it's easy to debug how the command was mangled.
    std::cerr << "Full command line was:\n> " << full_command_line << "\n";
    ap.abort();  // Cease further processing of the command line
    ot.return_value = EXIT_FAILURE;
}



void
Oiiotool::warning(string_view command, string_view explanation) const
{
    std::cerr << "oiiotool WARNING";
    if (command.size())
        std::cerr << ": " << command;
    if (explanation.size())
        std::cerr << " : " << explanation;
    else
        std::cerr << " (unknown warning)";
    std::cerr << "\n";
}



ParamValueList
Oiiotool::extract_options(string_view command)
{
    ParamValueList optlist;
    // Separate option list by splitting on ":", the first is the command
    // itself.
    auto opts = Strutil::splitsv(command, ":");
    for (size_t i = 1; i < opts.size(); ++i) {
        // Split each op on the first "=".
        auto optpieces = Strutil::splitsv(opts[i], "=", 2);
        if (optpieces.size() == 2)
            optlist[optpieces[0]] = optpieces[1];
    }
    return optlist;
}



// --threads
static void
set_threads(cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 2);
    int nthreads = Strutil::stoi(argv[1]);
    OIIO::attribute("threads", nthreads);
    OIIO::attribute("exr_threads", nthreads);
}



// --cache
static int
set_cachesize(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    ot.cachesize = Strutil::stoi(argv[1]);
    ot.imagecache->attribute("max_memory_MB", float(ot.cachesize));
    return 0;
}



// --autotile
static int
set_autotile(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    ot.autotile = Strutil::stoi(argv[1]);
    ot.imagecache->attribute("autotile", ot.autotile);
    ot.imagecache->attribute("autoscanline", int(ot.autotile ? 1 : 0));
    return 0;
}



// --native
static int
set_native(int argc, const char* /*argv*/[])
{
    OIIO_DASSERT(argc == 1);
    ot.nativeread = true;
    ot.imagecache->attribute("forcefloat", 0);
    return 0;
}



// --dumpdata
static void
set_dumpdata(cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command   = ot.express(argv[0]);
    auto options          = ot.extract_options(command);
    ot.dumpdata           = true;
    ot.dumpdata_showempty = options.get_int("empty", 1);
}



// --info
static void
set_printinfo(cspan<const char*> argv)
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
set_autocc(cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    ot.autocc           = true;
    ot.autoccunpremult  = options.get_int("unpremult");
}



// --autopremult
static void
set_autopremult(cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.autopremult = true;
    ot.imagecache->attribute("unassociatedalpha", 0);
    ot.input_config.erase_attribute("oiio:UnassociatedAlpha");
}



// --no-autopremult
static void
unset_autopremult(cspan<const char*> argv)
{
    OIIO_DASSERT(argv.size() == 1);
    ot.autopremult = false;
    ot.imagecache->attribute("unassociatedalpha", 1);
    ot.input_config.attribute("oiio:UnassociatedAlpha", 1);
    ot.input_config_set = true;
}



// --label
static int
action_label(int argc OIIO_MAYBE_UNUSED, const char* argv[])
{
    string_view labelname      = ot.express(argv[1]);
    ot.image_labels[labelname] = ot.curimg;
    return 0;
}



static void
string_to_dataformat(const std::string& s, TypeDesc& dataformat, int& bits)
{
    if (s == "uint8") {
        dataformat = TypeDesc::UINT8;
        bits       = 0;
    } else if (s == "int8") {
        dataformat = TypeDesc::INT8;
        bits       = 0;
    } else if (s == "uint10") {
        dataformat = TypeDesc::UINT16;
        bits       = 10;
    } else if (s == "uint12") {
        dataformat = TypeDesc::UINT16;
        bits       = 12;
    } else if (s == "uint16") {
        dataformat = TypeDesc::UINT16;
        bits       = 0;
    } else if (s == "int16") {
        dataformat = TypeDesc::INT16;
        bits       = 0;
    } else if (s == "uint32") {
        dataformat = TypeDesc::UINT32;
        bits       = 0;
    } else if (s == "int32") {
        dataformat = TypeDesc::INT32;
        bits       = 0;
    } else if (s == "half") {
        dataformat = TypeDesc::HALF;
        bits       = 0;
    } else if (s == "float") {
        dataformat = TypeDesc::FLOAT;
        bits       = 0;
    } else if (s == "double") {
        dataformat = TypeDesc::DOUBLE;
        bits       = 0;
    }
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
                      int bitdepth)
{
    // Account for default requested format
    if (format != TypeUnknown)
        spec.format = format;
    if (bitdepth)
        spec.attribute("oiio:BitsPerSample", bitdepth);
    else
        spec.erase_attribute("oiio:BitsPerSample");

    // See if there's a recommended format for this subimage
    std::string subimagename = spec["oiio:subimagename"];
    if (format == TypeUnknown && subimagename.size()) {
        auto key = Strutil::fmt::format("{}.*", subimagename);
        if (channelformats[key] != "")
            spec.format = TypeDesc(channelformats[key]);
    }

    // Honor any per-channel requests
    if (channelformats.size()) {
        spec.channelformats.clear();
        spec.channelformats.resize(spec.nchannels, spec.format);
        for (int c = 0; c < spec.nchannels; ++c) {
            std::string chname = spec.channel_name(c);
            auto subchname     = Strutil::fmt::format("{}.{}", subimagename,
                                                  chname);
            if (channelformats[subchname] != "" && subimagename.size())
                spec.channelformats[c] = TypeDesc(channelformats[subchname]);
            else if (channelformats[chname] != "")
                spec.channelformats[c] = TypeDesc(channelformats[chname]);
            else
                spec.channelformats[c] = spec.format;
        }
    } else {
        spec.channelformats.clear();
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
}



static void
adjust_output_options(string_view filename, ImageSpec& spec,
                      const ImageSpec* nativespec, const Oiiotool& ot,
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
    TypeDesc requested_output_dataformat = ot.output_dataformat;
    auto requested_output_channelformats = ot.output_channelformats;
    if (fileoptions.contains("type")) {
        requested_output_dataformat.fromstring(fileoptions.get_string("type"));
        requested_output_channelformats.clear();
    } else if (fileoptions.contains("datatype")) {
        requested_output_dataformat.fromstring(
            fileoptions.get_string("datatype"));
        requested_output_channelformats.clear();
    }
    int requested_output_bits = fileoptions.get_int("bits",
                                                    ot.output_bitspersample);

    if (requested_output_dataformat != TypeUnknown) {
        // Requested an explicit override of datatype
        set_output_dataformat(spec, requested_output_dataformat,
                              requested_output_channelformats,
                              requested_output_bits);
    } else if (was_direct_read && nativespec) {
        // Do nothing -- use the file's native data format
        spec.channelformats = nativespec->channelformats;
        set_output_dataformat(spec, nativespec->format,
                              requested_output_channelformats,
                              (*nativespec)["oiio:BitsPerSample"].get<int>());
    } else if (ot.input_dataformat != TypeUnknown) {
        auto mergedlist = ot.input_channelformats;
        for (auto& c : requested_output_channelformats)
            mergedlist[c.first] = c.second;
        set_output_dataformat(spec, ot.input_dataformat, mergedlist,
                              ot.input_bitspersample);
    }

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
                           y, tilesize.c_str(), false);
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
        std::string history = spec.get_string_attribute("Exif:ImageHistory");
        if (!Strutil::iends_with(history,
                                 ot.full_command_line)) {  // don't add twice
            if (history.length() && !Strutil::iends_with(history, "\n"))
                history += std::string("\n");
            history += ot.full_command_line;
            spec.attribute("Exif:ImageHistory", history);
        }

        std::string software = Strutil::sprintf("OpenImageIO %s : %s",
                                                OIIO_VERSION_STRING,
                                                ot.full_command_line);
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
DateTime_to_time_t(const char* datetime, time_t& timet)
{
    int year, month, day, hour, min, sec;
    int r = sscanf(datetime, "%d:%d:%d %d:%d:%d", &year, &month, &day, &hour,
                   &min, &sec);
    // printf ("%d  %d:%d:%d %d:%d:%d\n", r, year, month, day, hour, min, sec);
    if (r != 6)
        return false;
    struct tm tmtime;
    time_t now;
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
    for (int c = 0; chanlist.length(); ++c) {
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



// -d
static int
set_dataformat(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    string_view command = ot.express(argv[0]);
    std::vector<std::string> chans;
    Strutil::split(ot.express(argv[1]), chans, ",");

    if (chans.size() == 0) {
        return 0;  // Nothing to do
    }

    if (chans.size() == 1 && !strchr(chans[0].c_str(), '=')) {
        // Of the form:   -d uint8    (for example)
        // Just one default format designated, apply to all channels
        ot.output_dataformat    = TypeDesc::UNKNOWN;
        ot.output_bitspersample = 0;
        string_to_dataformat(chans[0], ot.output_dataformat,
                             ot.output_bitspersample);
        if (ot.output_dataformat == TypeDesc::UNKNOWN)
            ot.errorf(command, "Unknown data format \"%s\"", chans[0]);
        ot.output_channelformats.clear();
        return 0;  // we're done
    }

    // If we make it here, the format designator was of the form
    //    name0=type0,name1=type1,...
    for (auto& chan : chans) {
        const char* eq = strchr(chan.c_str(), '=');
        if (eq) {
            std::string channame(chan, 0, eq - chan.c_str());
            ot.output_channelformats[channame] = std::string(eq + 1);
        } else {
            ot.errorf(command, "Malformed format designator \"%s\"", chan);
        }
    }

    return 0;
}



static int
set_string_attribute(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 3);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }

    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    bool allsubimages   = options.get_int("allsubimages", ot.allsubimages);
    set_attribute(ot.curimg, argv[1], TypeString, argv[2], allsubimages);
    // N.B. set_attribute does expression expansion on its args
    return 0;
}



static int
set_any_attribute(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 3);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }

    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    bool allsubimages   = options.get_int("allsubimages", ot.allsubimages);
    TypeDesc type(options["type"].as_string());

    set_attribute(ot.curimg, argv[1], type, argv[2], allsubimages);
    // N.B. set_attribute does expression expansion on its args
    return 0;
}



static bool
do_erase_attribute(ImageSpec& spec, string_view attribname)
{
    spec.erase_attribute(attribname);
    return true;
}



// --eraseattrib
static int
erase_attribute(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    bool allsubimages   = options.get_int("allsubimages", ot.allsubimages);
    string_view pattern = ot.express(argv[1]);
    return apply_spec_mod(*ot.curimg, do_erase_attribute, pattern,
                          allsubimages);
}



template<class T>
static bool
do_set_any_attribute(ImageSpec& spec, const std::pair<std::string, T>& x)
{
    spec.attribute(x.first, x.second);
    return true;
}



bool
Oiiotool::get_position(string_view command, string_view geom, int& x, int& y)
{
    string_view orig_geom(geom);
    bool ok = Strutil::parse_int(geom, x) && Strutil::parse_char(geom, ',')
              && Strutil::parse_int(geom, y);
    if (!ok)
        errorf(command, "Unrecognized position \"%s\"", orig_geom);
    return ok;
}



bool
Oiiotool::adjust_geometry(string_view command, int& w, int& h, int& x, int& y,
                          const char* geom, bool allow_scaling,
                          bool allow_size) const
{
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    int ww = w, hh = h;
    int xx = x, yy = y;
    int xmax, ymax;
    if (sscanf(geom, "%d,%d,%d,%d", &xx, &yy, &xmax, &ymax) == 4) {
        x = xx;
        y = yy;
        w = std::max(0, xmax - xx + 1);
        h = std::max(0, ymax - yy + 1);
    } else if (sscanf(geom, "%dx%d%d%d", &ww, &hh, &xx, &yy) == 4
               || sscanf(geom, "%dx%d+%d+%d", &ww, &hh, &xx, &yy) == 4) {
        if (!allow_size) {
            warning(command,
                    "can't be used to change the size, only the origin");
            return false;
        }
        if (ww == 0 && h != 0)
            ww = int(hh * float(w) / float(h) + 0.5f);
        if (hh == 0 && w != 0)
            hh = int(ww * float(h) / float(w) + 0.5f);
        w = ww;
        h = hh;
        x = xx;
        y = yy;
    } else if (sscanf(geom, "%dx%d", &ww, &hh) == 2) {
        if (!allow_size) {
            warning(command,
                    "can't be used to change the size, only the origin");
            return false;
        }
        if (ww == 0 && h != 0)
            ww = int(hh * float(w) / float(h) + 0.5f);
        if (hh == 0 && w != 0)
            hh = int(ww * float(h) / float(w) + 0.5f);
        w = ww;
        h = hh;
    } else if (sscanf(geom, "%f%%x%f%%", &scaleX, &scaleY) == 2) {
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
        w = (int)(w * scaleX + 0.5f);
        h = (int)(h * scaleY + 0.5f);
    } else if (sscanf(geom, "%d%d", &xx, &yy) == 2) {
        x = xx;
        y = yy;
    } else if (sscanf(geom, "%f%%", &scaleX) == 1) {
        if (!allow_scaling) {
            warning(command, "can't be used to rescale the size");
            return false;
        }
        scaleX *= 0.01f;
        w = (int)(w * scaleX + 0.5f);
        h = (int)(h * scaleX + 0.5f);
    } else if (sscanf(geom, "%f", &scaleX) == 1) {
        if (!allow_scaling) {
            warning(command, "can't be used to rescale the size");
            return false;
        }
        w = (int)(w * scaleX + 0.5f);
        h = (int)(h * scaleX + 0.5f);
    } else {
        errorf(command, "Unrecognized geometry \"%s\"", geom);
        return false;
    }
    // printf ("geom %dx%d, %+d%+d\n", w, h, x, y);
    return true;
}



void
Oiiotool::express_error(const string_view expr, const string_view s,
                        string_view explanation)
{
    int offset = expr.rfind(s) + 1;
    errorf("expression", "%s at char %d of `%s'", explanation, offset, expr);
}



bool
Oiiotool::express_parse_atom(const string_view expr, string_view& s,
                             std::string& result)
{
    // std::cout << " Entering express_parse_atom, s='" << s << "'\n";

    string_view orig = s;
    float floatval;

    Strutil::skip_whitespace(s);

    // handle + or - prefixes
    bool negative = false;
    while (s.size()) {
        if (Strutil::parse_char(s, '-')) {
            negative = !negative;
        } else if (Strutil::parse_char(s, '+')) {
            // no op
        } else {
            break;
        }
    }

    if (Strutil::parse_char(s, '(')) {
        // handle parentheses
        if (express_parse_summands(expr, s, result)) {
            if (!Strutil::parse_char(s, ')')) {
                express_error(expr, s, "missing `)'");
                result = orig;
                return false;
            }
        } else {
            result = orig;
            return false;
        }

    } else if (Strutil::parse_identifier_if(s, "getattribute")
               && Strutil::parse_char(s, '(')) {
        // "{getattribute(name)}" retrieves global attribute `name`
        bool ok = true;
        Strutil::skip_whitespace(s);
        string_view name;
        if (s.size() && (s.front() == '\"' || s.front() == '\''))
            ok = Strutil::parse_string(s, name);
        else {
            name = Strutil::parse_until(s, ")");
        }
        if (name.size()) {
            std::string rs;
            int ri;
            float rf;
            if (OIIO::getattribute(name, rs))
                result = rs;
            else if (OIIO::getattribute(name, ri))
                result = Strutil::to_string(ri);
            else if (OIIO::getattribute(name, rf))
                result = Strutil::to_string(rf);
            else
                ok = false;
        }
        return Strutil::parse_char(s, ')') && ok;

    } else if (Strutil::starts_with(s, "TOP")
               || Strutil::starts_with(s, "IMG[")) {
        // metadata substitution
        ImageRecRef img;
        if (Strutil::parse_prefix(s, "TOP")) {
            img = curimg;
        } else if (Strutil::parse_prefix(s, "IMG[")) {
            int index = -1;
            if (Strutil::parse_int(s, index) && Strutil::parse_char(s, ']')
                && index >= 0 && index <= (int)image_stack.size()) {
                if (index == 0)
                    img = curimg;
                else
                    img = image_stack[image_stack.size() - index];
            } else {
                string_view name = Strutil::parse_until(s, "]");
                auto found       = ot.image_labels.find(name);
                if (found != ot.image_labels.end())
                    img = found->second;
                else
                    img = ImageRecRef(new ImageRec(name, ot.imagecache));
                Strutil::parse_char(s, ']');
            }
        }
        if (!img.get()) {
            express_error(expr, s, "not a valid image");
            result = orig;
            return false;
        }
        if (!Strutil::parse_char(s, '.')) {
            express_error(expr, s, "expected `.'");
            result = orig;
            return false;
        }
        string_view metadata;
        char quote             = s.size() ? s.front() : ' ';
        bool metadata_in_quote = quote == '\"' || quote == '\'';
        if (metadata_in_quote)
            Strutil::parse_string(s, metadata);
        else
            metadata = Strutil::parse_identifier(s, ":");

        if (metadata.size()) {
            read(img);
            ParamValue tmpparam;
            const ParamValue* p = img->spec(0, 0)->find_attribute(metadata,
                                                                  tmpparam);
            if (p) {
                std::string val = ImageSpec::metadata_val(*p);
                if (p->type().basetype == TypeDesc::STRING) {
                    // metadata_val returns strings double quoted, strip
                    val.erase(0, 1);
                    val.erase(val.size() - 1, 1);
                }
                result = val;
            } else if (metadata == "filename")
                result = img->name();
            else if (metadata == "file_extension")
                result = Filesystem::extension(img->name());
            else if (metadata == "file_noextension") {
                std::string filename = img->name();
                std::string ext      = Filesystem::extension(img->name());
                result = filename.substr(0, filename.size() - ext.size());
            } else if (metadata == "MINCOLOR") {
                auto pixstat = ImageBufAlgo::computePixelStats((*img)(0, 0));
                std::stringstream out;
                for (size_t i = 0; i < pixstat.min.size(); ++i)
                    out << (i ? "," : "") << pixstat.min[i];
                result = out.str();
            } else if (metadata == "MAXCOLOR") {
                auto pixstat = ImageBufAlgo::computePixelStats((*img)(0, 0));
                std::stringstream out;
                for (size_t i = 0; i < pixstat.max.size(); ++i)
                    out << (i ? "," : "") << pixstat.max[i];
                result = out.str();
            } else if (metadata == "AVGCOLOR") {
                auto pixstat = ImageBufAlgo::computePixelStats((*img)(0, 0));
                std::stringstream out;
                for (size_t i = 0; i < pixstat.avg.size(); ++i)
                    out << (i ? "," : "") << pixstat.avg[i];
                result = out.str();
            } else if (metadata == "META") {
                std::stringstream out;
                print_info_options opt;
                opt.verbose   = true;
                opt.subimages = true;
                std::string error;
                OiioTool::print_info(out, *this, img.get(), opt, error);
                result = out.str();
                if (result.size() && result.back() == '\n')
                    result.pop_back();
            } else if (metadata == "METABRIEF") {
                std::stringstream out;
                print_info_options opt;
                opt.verbose   = false;
                opt.subimages = false;
                std::string error;
                OiioTool::print_info(out, *this, img.get(), opt, error);
                result = out.str();
                if (result.size() && result.back() == '\n')
                    result.pop_back();
            } else if (metadata == "STATS") {
                std::stringstream out;
                OiioTool::print_stats(out, *this, (*img)());
                result = out.str();
                if (result.size() && result.back() == '\n')
                    result.pop_back();
            } else {
                express_error(expr, s,
                              Strutil::sprintf("unknown attribute name `%s'",
                                               metadata));
                result = orig;
                return false;
            }
        }
    } else if (Strutil::parse_float(s, floatval)) {
        result = Strutil::sprintf("%g", floatval);
    }
    // Test some special identifiers
    else if (Strutil::parse_identifier_if(s, "FRAME_NUMBER")) {
        result = Strutil::sprintf("%d", ot.frame_number);
    } else if (Strutil::parse_identifier_if(s, "FRAME_NUMBER_PAD")) {
        std::string fmt = ot.frame_padding == 0
                              ? std::string("%d")
                              : Strutil::sprintf("\"%%0%dd\"",
                                                 ot.frame_padding);
        result          = Strutil::sprintf(fmt.c_str(), ot.frame_number);
    } else {
        express_error(expr, s, "syntax error");
        result = orig;
        return false;
    }

    if (negative)
        result = "-" + result;

    // std::cout << " Exiting express_parse_atom, result='" << result << "'\n";

    return true;
}



bool
Oiiotool::express_parse_factors(const string_view expr, string_view& s,
                                std::string& result)
{
    // std::cout << " Entering express_parse_factors, s='" << s << "'\n";

    string_view orig = s;
    std::string atom;
    float lval, rval;

    // parse the first factor
    if (!express_parse_atom(expr, s, atom)) {
        result = orig;
        return false;
    }

    if (atom.size() >= 2 && atom.front() == '\"' && atom.back() == '\"') {
        // Double quoted is string, return it
        result = atom;
    } else if (Strutil::string_is<float>(atom)) {
        // lval is a number
        lval = Strutil::from_string<float>(atom);
        while (s.size()) {
            enum class Ops { mul, div, idiv, imod };
            Ops op;
            if (Strutil::parse_char(s, '*'))
                op = Ops::mul;
            else if (Strutil::parse_prefix(s, "//"))
                op = Ops::idiv;
            else if (Strutil::parse_char(s, '/'))
                op = Ops::div;
            else if (Strutil::parse_char(s, '%'))
                op = Ops::imod;
            else {
                // no more factors
                break;
            }

            // parse the next factor
            if (!express_parse_atom(expr, s, atom)) {
                result = orig;
                return false;
            }

            if (!Strutil::string_is<float>(atom)) {
                express_error(expr, s,
                              Strutil::sprintf("expected number but got `%s'",
                                               atom));
                result = orig;
                return false;
            }

            // rval is a number, so we can math
            rval = Strutil::from_string<float>(atom);
            if (op == Ops::mul)
                lval *= rval;
            else if (op == Ops::div)
                lval /= rval;
            else if (op == Ops::idiv) {
                int ilval(lval), irval(rval);
                lval = float(rval ? ilval / irval : 0);
            } else if (op == Ops::imod) {
                int ilval(lval), irval(rval);
                lval = float(rval ? ilval % irval : 0);
            }
        }

        result = Strutil::sprintf("%g", lval);

    } else {
        // atom is not a number, so we're done
        result = atom;
    }

    // std::cout << " Exiting express_parse_factors, result='" << result << "'\n";

    return true;
}



bool
Oiiotool::express_parse_summands(const string_view expr, string_view& s,
                                 std::string& result)
{
    // std::cout << " Entering express_parse_summands, s='" << s << "'\n";

    string_view orig = s;
    std::string atom;
    float lval, rval;

    // parse the first summand
    if (!express_parse_factors(expr, s, atom)) {
        result = orig;
        return false;
    }

    if (atom.size() >= 2 && atom.front() == '\"' && atom.back() == '\"') {
        // Double quoted is string, strip it
        result = atom.substr(1, atom.size() - 2);
    } else if (Strutil::string_is<float>(atom)) {
        // lval is a number
        lval = Strutil::from_string<float>(atom);
        while (s.size()) {
            char op;
            if (Strutil::parse_char(s, '+'))
                op = '+';
            else if (Strutil::parse_char(s, '-'))
                op = '-';
            else {
                // no more summands
                break;
            }

            // parse the next summand
            if (!express_parse_factors(expr, s, atom)) {
                result = orig;
                return false;
            }

            if (!Strutil::string_is<float>(atom)) {
                express_error(expr, s,
                              Strutil::sprintf("`%s' is not a number", atom));
                result = orig;
                return false;
            }

            // rval is also a number, we can math
            rval = Strutil::from_string<float>(atom);
            if (op == '+')
                lval += rval;
            else  // op == '-'
                lval -= rval;
        }

        result = Strutil::sprintf("%g", lval);

    } else {
        // atom is not a number, so we're done
        result = atom;
    }

    // std::cout << " Exiting express_parse_summands, result='" << result << "'\n";

    return true;
}



// Expression evaluation and substitution for a single expression
std::string
Oiiotool::express_impl(string_view s)
{
    std::string result;
    string_view orig = s;
    if (!express_parse_summands(orig, s, result)) {
        result = orig;
    }
    return result;
}



// Perform expression evaluation and substitution on a string
string_view
Oiiotool::express(string_view str)
{
    if (!eval_enable)
        return str;  // Expression evaluation disabled

    string_view s = str;
    // eg. s="ab{cde}fg"
    size_t openbrace = s.find('{');
    if (openbrace == s.npos)
        return str;  // No open brace found -- no expresion substitution

    string_view prefix = s.substr(0, openbrace);
    s.remove_prefix(openbrace);
    // eg. s="{cde}fg", prefix="ab"
    string_view expr = Strutil::parse_nested(s);
    if (expr.empty())
        return str;  // No corresponding close brace found -- give up
    // eg. prefix="ab", expr="{cde}", s="fg", prefix="ab"
    OIIO_ASSERT(expr.front() == '{' && expr.back() == '}');
    expr.remove_prefix(1);
    expr.remove_suffix(1);
    // eg. expr="cde"
    ustring result = ustring::sprintf("%s%s%s", prefix, express_impl(expr),
                                      express(s));
    if (ot.debug)
        std::cout << "Expanding expression \"" << str << "\" -> \"" << result
                  << "\"\n";
    return result;
}



// --iconfig
static int
set_input_attribute(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 3);

    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    TypeDesc type(options["type"].as_string());
    string_view attribname = ot.express(argv[1]);
    string_view value      = ot.express(argv[2]);

    if (!value.size()) {
        // If the value is the empty string, clear the attribute
        ot.input_config.erase_attribute(attribname);
        return 0;
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
        return 0;
    }
    if (type.basetype == TypeDesc::INT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<int> vals(n, 0);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_int(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        ot.input_config.attribute(attribname, type, &vals[0]);
        return 0;
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
        return 0;
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
    return 0;
}



bool
OiioTool::set_attribute(ImageRecRef img, string_view attribname, TypeDesc type,
                        string_view value, bool allsubimages)
{
    // Expression substitution
    attribname = ot.express(attribname);
    value      = ot.express(value);

    ot.read(img);
    img->metadata_modified(true);
    if (!value.size()) {
        // If the value is the empty string, clear the attribute
        return apply_spec_mod(*img, do_erase_attribute, attribname,
                              allsubimages);
    }

    // First, handle the cases where we're told what to expect
    if (type.basetype == TypeDesc::FLOAT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<float> vals(n, 0.0f);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_float(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        for (int s = 0, send = img->subimages(); s < send; ++s) {
            for (int m = 0, mend = img->miplevels(s); m < mend; ++m) {
                ((*img)(s, m).specmod()).attribute(attribname, type, &vals[0]);
                img->update_spec_from_imagebuf(s, m);
                if (!allsubimages)
                    break;
            }
            if (!allsubimages)
                break;
        }
        return true;
    }
    if (type == TypeTimeCode && value.find(':') != value.npos) {
        // Special case: They are specifying a TimeCode as a "HH:MM:SS:FF"
        // string, we need to re-encode as a uint32[2].
        int hour = 0, min = 0, sec = 0, frame = 0;
        sscanf(value.c_str(), "%d:%d:%d:%d", &hour, &min, &sec, &frame);
        Imf::TimeCode tc(hour, min, sec, frame);
        for (int s = 0, send = img->subimages(); s < send; ++s) {
            for (int m = 0, mend = img->miplevels(s); m < mend; ++m) {
                ((*img)(s, m).specmod()).attribute(attribname, type, &tc);
                img->update_spec_from_imagebuf(s, m);
                if (!allsubimages)
                    break;
            }
            if (!allsubimages)
                break;
        }
        return true;
    }
    if (type == TypeRational && value.find('/') != value.npos) {
        // Special case: They are specifying a rational as "a/b", so we need
        // to re-encode as a int32[2].
        int v[2];
        Strutil::parse_int(value, v[0]);
        Strutil::parse_char(value, '/');
        Strutil::parse_int(value, v[1]);
        for (int s = 0, send = img->subimages(); s < send; ++s) {
            for (int m = 0, mend = img->miplevels(s); m < mend; ++m) {
                ((*img)(s, m).specmod()).attribute(attribname, type, v);
                img->update_spec_from_imagebuf(s, m);
                if (!allsubimages)
                    break;
            }
            if (!allsubimages)
                break;
        }
        return true;
    }
    if (type.basetype == TypeDesc::INT) {
        size_t n = type.numelements() * type.aggregate;
        std::vector<int> vals(n, 0);
        for (size_t i = 0; i < n && value.size(); ++i) {
            Strutil::parse_int(value, vals[i]);
            Strutil::parse_char(value, ',');
        }
        for (int s = 0, send = img->subimages(); s < send; ++s) {
            for (int m = 0, mend = img->miplevels(s); m < mend; ++m) {
                ((*img)(s, m).specmod()).attribute(attribname, type, &vals[0]);
                img->update_spec_from_imagebuf(s, m);
                if (!allsubimages)
                    break;
            }
            if (!allsubimages)
                break;
        }
        return true;
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
        for (int s = 0, send = img->subimages(); s < send; ++s) {
            for (int m = 0, mend = img->miplevels(s); m < mend; ++m) {
                ((*img)(s, m).specmod()).attribute(attribname, type, &vals[0]);
                img->update_spec_from_imagebuf(s, m);
                if (!allsubimages)
                    break;
            }
            if (!allsubimages)
                break;
        }
        return true;
    }

    if (type == TypeInt
        || (type == TypeUnknown && Strutil::string_is_int(value))) {
        // Does it seem to be an int, or did the caller explicitly request
        // that it be set as an int?
        int v = Strutil::stoi(value);
        return apply_spec_mod(*img, do_set_any_attribute<int>,
                              std::pair<std::string, int>(attribname, v),
                              allsubimages);
    } else if (type == TypeFloat
               || (type == TypeUnknown && Strutil::string_is_float(value))) {
        // Does it seem to be a float, or did the caller explicitly request
        // that it be set as a float?
        float v = Strutil::stof(value);
        return apply_spec_mod(*img, do_set_any_attribute<float>,
                              std::pair<std::string, float>(attribname, v),
                              allsubimages);
    } else {
        // Otherwise, set it as a string attribute
        return apply_spec_mod(*img, do_set_any_attribute<std::string>,
                              std::pair<std::string, std::string>(attribname,
                                                                  value),
                              allsubimages);
    }
}



// --caption
static int
set_caption(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    const char* newargs[3] = { argv[0], "ImageDescription", argv[1] };
    return set_string_attribute(3, newargs);
    // N.B. set_string_attribute does expression expansion on its args
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
static int
set_keyword(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }

    std::string keyword(ot.express(argv[1]));
    if (keyword.size())
        apply_spec_mod(*ot.curimg, do_set_keyword, keyword, ot.allsubimages);

    return 0;
}



// --clear-keywords
static int
clear_keywords(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 1);
    const char* newargs[3];
    newargs[0] = argv[0];
    newargs[1] = "Keywords";
    newargs[2] = "";
    return set_string_attribute(3, newargs);
}



// --orientation
static int
set_orientation(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }

    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    bool allsubimages   = options.get_int("allsubimages", ot.allsubimages);

    return set_attribute(ot.curimg, "Orientation", TypeDesc::INT, argv[1],
                         allsubimages);
    // N.B. set_attribute does expression expansion on its args
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
static int
rotate_orientation(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 1);
    string_view command = ot.express(argv[0]);
    if (!ot.curimg.get()) {
        ot.warning(command, "no current image available to modify");
        return 0;
    }

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    apply_spec_mod(*ot.curimg, do_rotate_orientation, command, allsubimages);
    return 0;
}



// --origin
static int
set_origin(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, set_origin, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view origin  = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = spec.x, y = spec.y, z = spec.z;
        int w = spec.width, h = spec.height, d = spec.depth;
        ot.adjust_geometry(command, w, h, x, y, origin.c_str());
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
    ot.function_times[command] += timer();
    return 0;
}



// --originoffset
static int
offset_origin(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, offset_origin, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view origin  = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = 0, y = 0, z = 0;  // OFFSETS, not set values
        int w = spec.width, h = spec.height;
        ot.adjust_geometry(command, w, h, x, y, origin.c_str(), false, false);
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
    ot.function_times[command] += timer();
    return 0;
}



// --fullsize
static int
set_fullsize(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, set_fullsize, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ot.read();
    ImageRecRef A = ot.curimg;
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        ImageSpec& spec(*A->spec(s));
        int x = spec.full_x, y = spec.full_y;
        int w = spec.full_width, h = spec.full_height;
        ot.adjust_geometry(argv[0], w, h, x, y, size.c_str());
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
    ot.function_times[command] += timer();
    return 0;
}



// --fullpixels
static int
set_full_to_pixels(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, set_full_to_pixels, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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
    ot.function_times[command] += timer();
    return 0;
}



// --colorconfig
static int
set_colorconfig(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    ot.colorconfig.reset(argv[1]);
    return 0;
}



// --iscolorspace
static int
set_colorspace(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 2);
    const char* args[3] = { argv[0], "oiio:ColorSpace", argv[1] };
    return set_string_attribute(3, args);
    // N.B. set_string_attribute does expression expansion on its args
}



// --colorconvert
class OpColorConvert final : public OiiotoolOp {
public:
    OpColorConvert(Oiiotool& ot, string_view opname, int argc,
                   const char* argv[])
        : OiiotoolOp(ot, opname, argc, argv, 1)
    {
        fromspace = args(1);
        tospace   = args(2);
    }
    virtual bool setup() override
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
    virtual bool impl(span<ImageBuf*> img) override
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
                                             contextvalue, &ot.colorconfig);
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
static int
action_tocolorspace(int argc, const char* argv[])
{
    // Don't time -- let it get accounted by colorconvert
    OIIO_DASSERT(argc == 2);
    if (!ot.curimg.get()) {
        ot.warning(argv[0], "no current image available to modify");
        return 0;
    }
    const char* args[3] = { argv[0], "current", argv[1] };
    return action_colorconvert(3, args);
}



// --ccmatrix
OIIOTOOL_OP(ccmatrix, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
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
OIIOTOOL_OP(ociolook, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view lookname     = op.args(1);
    std::string fromspace    = op.options()["from"];
    std::string tospace      = op.options()["to"];
    std::string contextkey   = op.options()["key"];
    std::string contextvalue = op.options()["value"];
    bool inverse             = op.options().get_int("inverse");
    bool unpremult           = op.options().get_int("unpremult");
    if (fromspace == "current" || fromspace == "")
        fromspace = img[1]->spec().get_string_attribute("oiio:Colorspace",
                                                        "Linear");
    if (tospace == "current" || tospace == "")
        tospace = img[1]->spec().get_string_attribute("oiio:Colorspace",
                                                      "Linear");
    return ImageBufAlgo::ociolook(*img[0], *img[1], lookname, fromspace,
                                  tospace, unpremult, inverse, contextkey,
                                  contextvalue, &ot.colorconfig);
});



// --ociodisplay
OIIOTOOL_OP(ociodisplay, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view displayname  = op.args(1);
    string_view viewname     = op.args(2);
    std::string fromspace    = op.options()["from"];
    std::string contextkey   = op.options()["key"];
    std::string contextvalue = op.options()["value"];
    std::string looks        = op.options()["looks"];
    bool unpremult           = op.options().get_int("unpremult");
    if (fromspace == "current" || fromspace == "")
        fromspace = img[1]->spec().get_string_attribute("oiio:Colorspace",
                                                        "Linear");
    return ImageBufAlgo::ociodisplay(*img[0], *img[1], displayname, viewname,
                                     fromspace, looks, unpremult, contextkey,
                                     contextvalue, &ot.colorconfig);
});



// --ociofiletransform
OIIOTOOL_OP(ociofiletransform, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view name = op.args(1);
    bool inverse     = op.options().get_int("inverse");
    bool unpremult   = op.options().get_int("unpremult");
    return ImageBufAlgo::ociofiletransform(*img[0], *img[1], name, unpremult,
                                           inverse, &ot.colorconfig);
});



static int
output_tiles(int /*argc*/, const char* /*argv*/[])
{
    // the ArgParse will have set the tile size, but we need this routine
    // to clear the scanline flag
    ot.output_scanline = false;
    return 0;
}



// --unmip
// N.B.: This unmips all subimages and does not honor the ':subimages='
// modifier.
static int
action_unmip(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_unmip, argc, argv))
        return 0;

    // Special case -- detect if there are no MIP-mapped subimages at all,
    // in which case this is a no-op (avoid any copies or allocations).
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    ot.read();
    bool mipmapped = false;
    for (int s = 0, send = ot.curimg->subimages(); s < send; ++s)
        mipmapped |= (ot.curimg->miplevels(s) > 1);
    if (!mipmapped) {
        ot.function_times[command] += timer();
        return 0;  // --unmip on an unmipped image is a no-op
    }

    // If there is work to be done, fall back on the OiiotoolOp.
    // No subclass needed, default OiiotoolOp removes MIP levels and
    // copies the first input image by default.
    OiiotoolOp op(ot, "unmip", argc, argv, 1);
    return op();
}



// --chnames
class OpChnames final : public OiiotoolOp {
public:
    OpChnames(Oiiotool& ot, string_view opname, int argc, const char* argv[])
        : OiiotoolOp(ot, opname, argc, argv, 1)
    {
        preserve_miplevels(true);
    }
    // Custom creation of new ImageRec result: don't copy, just change in
    // place.
    virtual ImageRecRef new_output_imagerec() override { return ir(1); }
    virtual bool impl(span<ImageBuf*> img) override
    {
        string_view channelarg = ot.express(args(1));
        auto newchannelnames   = Strutil::splits(channelarg, ",");
        ImageSpec& spec        = img[0]->specmod();
        spec.channelnames.resize(spec.nchannels);
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

static int
action_set_channelnames(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_set_channelnames, argc, argv))
        return 0;
    OpChnames op(ot, "chnames", argc, argv);
    return op();
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
                             std::vector<float>& values)
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
                    if (spec.channelnames[i]
                        == oldname) {  // name of a known channel?
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
                if (newname.empty() && chan >= 0)
                    newname = spec.channelnames[chan];
            }
        }

        if (!newname.size()) {
            const char* RGBAZ[] = { "R", "G", "B", "A", "Z" };
            if (c <= 4)
                newname = std::string(RGBAZ[c]);
            else
                newname = Strutil::sprintf("channel%d", c);
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



// --channels
int
action_channels(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_channels, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command  = ot.express(argv[0]);
    string_view chanlist = ot.express(argv[1]);

    auto options      = ot.extract_options(command);
    bool allsubimages = options.get_int("allsubimages", ot.allsubimages);

    ImageRecRef A(ot.pop());
    ot.read(A);

    if (chanlist == "RGB")  // Fix common synonyms/mistakes
        chanlist = "R,G,B";
    else if (chanlist == "RGBA")
        chanlist = "R,G,B,A";

    // Decode the channel set, make the full list of ImageSpec's we'll
    // need to describe the new ImageRec with the altered channels.
    std::vector<int> allmiplevels;
    std::vector<ImageSpec> allspecs;
    for (int s = 0, subimages = allsubimages ? A->subimages() : 1;
         s < subimages; ++s) {
        std::vector<std::string> newchannelnames;
        std::vector<int> channels;
        std::vector<float> values;
        bool ok = decode_channel_set(*A->spec(s, 0), chanlist, newchannelnames,
                                     channels, values);
        if (!ok) {
            ot.errorf(command, "Invalid or unknown channel selection \"%s\"",
                      chanlist);
            ot.push(A);
            return 0;
        }
        int miplevels = ot.allsubimages ? A->miplevels(s) : 1;
        allmiplevels.push_back(miplevels);
        for (int m = 0; m < miplevels; ++m) {
            ImageSpec spec = *A->spec(s, m);
            spec.nchannels = (int)newchannelnames.size();
            spec.channelformats.clear();
            spec.default_channel_names();
            allspecs.push_back(spec);
        }
    }

    // Create the replacement ImageRec
    ImageRecRef R(new ImageRec(A->name(), (int)allmiplevels.size(),
                               allmiplevels, allspecs));
    ot.push(R);

    // Subimage by subimage, MIP level by MIP level, copy/shuffle the
    // channels individually from the source image into the result.
    for (int s = 0, subimages = R->subimages(); s < subimages; ++s) {
        std::vector<std::string> newchannelnames;
        std::vector<int> channels;
        std::vector<float> values;
        decode_channel_set(*A->spec(s, 0), chanlist, newchannelnames, channels,
                           values);
        for (int m = 0, miplevels = R->miplevels(s); m < miplevels; ++m) {
            // Shuffle the indexed/named channels
            bool ok = ImageBufAlgo::channels((*R)(s, m), (*A)(s, m),
                                             (int)channels.size(), &channels[0],
                                             &values[0], &newchannelnames[0],
                                             false);
            if (!ok) {
                ot.error(command, (*R)(s, m).geterror());
                break;
            }
            // Tricky subtlety: IBA::channels changed the underlying IB,
            // we may need to update the IR's copy of the spec.
            R->update_spec_from_imagebuf(s, m);
        }
    }

    ot.function_times[command] += timer();
    return 0;
}



// --chappend
static int
action_chappend(int argc, const char* argv[])
{
    if (ot.postpone_callback(2, action_chappend, argc, argv))
        return 0;
    std::string command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    int n               = OIIO::clamp(options["n"].get<int>(2), 2,
                        int(ot.image_stack.size() + 1));
    command             = remove_modifier(command, "n");
    bool ok             = true;

    // two at a time
    for (; n >= 2; --n) {
        OiiotoolOp op(ot, "chappend", argc, argv, 2);
        op.preserve_miplevels(true);
        op.set_impl([](OiiotoolOp& op, span<ImageBuf*> img) {
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
        ok &= op();
    }
    return ok;
}



// --selectmip
static int
action_selectmip(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_selectmip, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    int miplevel        = Strutil::from_string<int>(ot.express(argv[1]));

    ot.read();
    bool mipmapped = false;
    for (int s = 0, send = ot.curimg->subimages(); s < send; ++s)
        mipmapped |= (ot.curimg->miplevels(s) > 1);
    if (!mipmapped) {
        return 0;  // --selectmip on an unmipped image is a no-op
    }

    ImageRecRef newimg(new ImageRec(*ot.curimg, -1, miplevel, true, true));
    ot.curimg = newimg;
    ot.function_times[command] += timer();
    return 0;
}



// --subimage
static int
action_select_subimage(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_select_subimage, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    ot.read();

    string_view command       = ot.express(argv[0]);
    auto options              = ot.extract_options(command);
    int subimage              = 0;
    std::string whichsubimage = ot.express(argv[1]);
    string_view w(whichsubimage);
    if (Strutil::parse_int(w, subimage) && w.empty()) {
        // Subimage specification was an integer: treat as an index
        if (subimage < 0 || subimage >= ot.curimg->subimages()) {
            ot.errorf(command, "Invalid -subimage (%d): %s has %d subimage%s",
                      subimage, ot.curimg->name(), ot.curimg->subimages(),
                      ot.curimg->subimages() == 1 ? "" : "s");
            return 0;
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
            ot.errorf(command,
                      "Invalid -subimage (%s): named subimage not found",
                      whichsubimage);
            return 0;
        }
    }

    if (ot.curimg->subimages() == 1 && subimage == 0)
        return 0;  // asking for the only subimage is a no-op

    if (options["delete"].get<int>()) {
        // Delete mode: remove the specified subimage
        ot.top()->erase_subimage(subimage);
    } else {
        // Select mode: select just the one specified subimage
        ImageRecRef A = ot.pop();
        ot.push(new ImageRec(*A, subimage, -1, true));
    }
    ot.function_times[command] += timer();
    return 0;
}



// --sisplit
static int
action_subimage_split(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_subimage_split, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

    ImageRecRef A = ot.pop();
    ot.read(A);

    // Push the individual subimages onto the stack
    for (int subimage = 0; subimage < A->subimages(); ++subimage)
        ot.push(new ImageRec(*A, subimage, -1, true));

    ot.function_times[command] += timer();
    return 0;
}



static void
action_subimage_append_n(int n, string_view command)
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
            for (int m = 0; m < A->miplevels(s); ++m) {
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
static int
action_subimage_append(int argc, const char* argv[])
{
    if (ot.postpone_callback(2, action_subimage_append, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    int n               = OIIO::clamp(options["n"].get<int>(2), 2,
                        int(ot.image_stack.size() + 1));

    action_subimage_append_n(n, command);

    ot.function_times[command] += timer();
    return 0;
}



// --siappendall
static int
action_subimage_append_all(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_subimage_append_all, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

    action_subimage_append_n(int(ot.image_stack.size() + 1), command);

    ot.function_times[command] += timer();
    return 0;
}



// --colorcount
static void
action_colorcount(cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_colorcount, argv))
        return;
    Timer timer(ot.enable_function_timing);
    string_view command  = ot.express(argv[0]);
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
                                        &colorvalues[0], &eps[0]);
    if (ok) {
        for (int col = 0; col < ncolors; ++col)
            Strutil::printf("%8d  %s\n", count[col], colorstrings[col]);
    } else {
        ot.error(command, (*ot.curimg)(0, 0).geterror());
    }

    ot.function_times[command] += timer();
    return;
}



// --rangecheck
static void
action_rangecheck(cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_rangecheck, argv))
        return;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
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
                                              &highcount, &inrangecount,
                                              &low[0], &high[0]);
    if (ok) {
        Strutil::printf("%8d  < %s\n", lowcount, lowarg);
        Strutil::printf("%8d  > %s\n", highcount, higharg);
        Strutil::printf("%8d  within range\n", inrangecount);
    } else {
        ot.error(command, (*ot.curimg)(0, 0).geterror());
    }

    ot.function_times[command] += timer();
}



// --diff
static int
action_diff(int argc, const char* argv[])
{
    if (ot.postpone_callback(2, action_diff, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

    int ret = do_action_diff(*ot.image_stack.back(), *ot.curimg, ot);
    if (ret != DiffErrOK && ret != DiffErrWarn)
        ot.return_value = EXIT_FAILURE;

    if (ret != DiffErrOK && ret != DiffErrWarn && ret != DiffErrFail)
        ot.errorf(command, "Diff failed");

    ot.printed_info = true;  // because taking the diff has output
    ot.function_times[command] += timer();
    return 0;
}



// --pdiff
static int
action_pdiff(int argc, const char* argv[])
{
    if (ot.postpone_callback(2, action_pdiff, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

    int ret = do_action_diff(*ot.image_stack.back(), *ot.curimg, ot, 1);
    if (ret != DiffErrOK && ret != DiffErrWarn)
        ot.return_value = EXIT_FAILURE;

    if (ret != DiffErrOK && ret != DiffErrWarn && ret != DiffErrFail)
        ot.errorf(command, "Diff failed");

    ot.function_times[command] += timer();
    return 0;
}



BINARY_IMAGE_OP(add, ImageBufAlgo::add);          // --add
BINARY_IMAGE_OP(sub, ImageBufAlgo::sub);          // --sub
BINARY_IMAGE_OP(mul, ImageBufAlgo::mul);          // --mul
BINARY_IMAGE_OP(div, ImageBufAlgo::div);          // --div
BINARY_IMAGE_OP(absdiff, ImageBufAlgo::absdiff);  // --absdiff

BINARY_IMAGE_COLOR_OP(addc, ImageBufAlgo::add, 0);          // --addc
BINARY_IMAGE_COLOR_OP(subc, ImageBufAlgo::sub, 0);          // --subc
BINARY_IMAGE_COLOR_OP(mulc, ImageBufAlgo::mul, 1);          // --mulc
BINARY_IMAGE_COLOR_OP(divc, ImageBufAlgo::div, 1);          // --divc
BINARY_IMAGE_COLOR_OP(absdiffc, ImageBufAlgo::absdiff, 0);  // --absdiffc
BINARY_IMAGE_COLOR_OP(powc, ImageBufAlgo::pow, 1.0f);       // --powc

UNARY_IMAGE_OP(abs, ImageBufAlgo::abs);  // --abs

UNARY_IMAGE_OP(premult, ImageBufAlgo::premult);  // --premult

// --unpremult
OIIOTOOL_OP(unpremult, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    if (img[1]->spec().get_int_attribute("oiio:UnassociatedAlpha")
        && img[1]->spec().alpha_channel >= 0) {
        ot.warning(
            op.opname(),
            "Image appears to already be unassociated alpha (un-premultiplied color), beware double unpremult.");
    }
    return ImageBufAlgo::unpremult(*img[0], *img[1]);
});


// --mad
OIIOTOOL_OP(mad, 3, [](OiiotoolOp& op, span<ImageBuf*> img) {
    return ImageBufAlgo::mad(*img[0], *img[1], *img[2], *img[3]);
});


// --invert
OIIOTOOL_OP(invert, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    // invert the first three channels only, spare alpha
    ROI roi   = img[1]->roi();
    roi.chend = std::min(3, roi.chend);
    return ImageBufAlgo::invert(*img[0], *img[1], roi, 0);
});



// --noise
OIIOTOOL_OP(noise, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    img[0]->copy(*img[1]);
    std::string type = op.options().get_string("type", "gaussian");
    float A          = 0.0f;
    float B          = 0.1f;
    if (type == "gaussian") {
        A = op.options().get_float("mean", 0.0f);
        B = op.options().get_float("stddev", 0.1f);
    } else if (type == "uniform") {
        A = op.options().get_float("min", 0.0f);
        B = op.options().get_float("max", 0.1f);
    } else if (type == "salt") {
        A = op.options().get_float("value", 0.0f);
        B = op.options().get_float("portion", 0.01f);
    } else {
        ot.errorf(op.opname(), "Unknown noise type \"%s\"", type);
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
OIIOTOOL_OP(chsum, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    std::vector<float> weight(img[1]->nchannels(), 1.0f);
    Strutil::extract_from_list_string(weight,
                                      op.options().get_string("weight"));
    return ImageBufAlgo::channel_sum(*img[0], *img[1], weight);
});



// --colormap
OIIOTOOL_OP(colormap, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
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



UNARY_IMAGE_OP(flip, ImageBufAlgo::flip);            // --flip
UNARY_IMAGE_OP(flop, ImageBufAlgo::flop);            // --flop
UNARY_IMAGE_OP(rotate180, ImageBufAlgo::rotate180);  // --rotate180
UNARY_IMAGE_OP(rotate90, ImageBufAlgo::rotate90);    // --rotate90
UNARY_IMAGE_OP(rotate270, ImageBufAlgo::rotate270);  // --rotate270
UNARY_IMAGE_OP(transpose, ImageBufAlgo::transpose);  // --transpose



// --reorient
int
action_reorient(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_reorient, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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

    ot.function_times[command] += timer();
    ot.enable_function_timing = old_enable_function_timing;
    return 0;
}



// --rotate
OIIOTOOL_OP(rotate, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    float angle            = Strutil::from_string<float>(op.args(1));
    std::string filtername = op.options()["filter"];
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
    return ImageBufAlgo::rotate(*img[0], *img[1], angle * float(M_PI / 180.0),
                                cx, cy, filtername, 0.0f, recompute_roi);
});



// --warp
OIIOTOOL_OP(warp, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    std::string filtername = op.options()["filter"];
    bool recompute_roi     = op.options().get_int("recompute_roi");
    std::vector<float> M(9);
    if (Strutil::extract_from_list_string(M, op.args(1)) != 9) {
        ot.error(op.opname(),
                 "expected 9 comma-separatd floats to form a 3x3 matrix");
        return false;
    }
    return ImageBufAlgo::warp(*img[0], *img[1], *(Imath::M33f*)&M[0],
                              filtername, 0.0f, recompute_roi,
                              ImageBuf::WrapDefault);
});



// --cshift
OIIOTOOL_OP(cshift, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    int x = 0;
    int y = 0;
    int z = 0;
    if (sscanf(op.args(1).c_str(), "%d%d%d", &x, &y, &z) < 2) {
        ot.errorf(op.opname(), "Invalid shift offset '%s'", op.args(1));
        return false;
    }
    return ImageBufAlgo::circular_shift(*img[0], *img[1], x, y, z);
});



// --pop
static int
action_pop(int argc, const char* /*argv*/[])
{
    OIIO_DASSERT(argc == 1);
    ot.pop();
    return 0;
}



// --dup
static int
action_dup(int argc, const char* /*argv*/[])
{
    OIIO_DASSERT(argc == 1);
    ot.push(ot.curimg);
    return 0;
}


// --swap
static int
action_swap(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 1);
    string_view command = ot.express(argv[0]);
    if (ot.image_stack.size() < 1) {
        ot.errorf(command, "requires at least two loaded images");
        return 0;
    }
    ImageRecRef B(ot.pop());
    ImageRecRef A(ot.pop());
    ot.push(B);
    ot.push(A);
    return 0;
}


// --create
static int
action_create(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 3);
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    string_view size    = ot.express(argv[1]);
    int nchans          = Strutil::from_string<int>(ot.express(argv[2]));
    if (nchans < 1 || nchans > 1024) {
        ot.warningf(argv[0], "Invalid number of channels: %d", nchans);
        nchans = 3;
    }
    ImageSpec spec(64, 64, nchans,
                   TypeDesc(options["type"].as_string("float")));
    ot.adjust_geometry(argv[0], spec.width, spec.height, spec.x, spec.y,
                       size.c_str());
    spec.full_x      = spec.x;
    spec.full_y      = spec.y;
    spec.full_z      = spec.z;
    spec.full_width  = spec.width;
    spec.full_height = spec.height;
    spec.full_depth  = spec.depth;
    ImageRecRef img(new ImageRec("new", spec, ot.imagecache));
    bool ok = ImageBufAlgo::zero((*img)());
    if (!ok)
        ot.error(command, (*img)().geterror());
    if (ot.curimg)
        ot.image_stack.push_back(ot.curimg);
    ot.curimg = img;
    ot.function_times[command] += timer();
    return 0;
}



// --pattern
static int
action_pattern(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 4);
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    std::string pattern = ot.express(argv[1]);
    std::string size    = ot.express(argv[2]);
    int nchans          = Strutil::from_string<int>(ot.express(argv[3]));
    if (nchans < 1 || nchans > 1024) {
        ot.warningf(argv[0], "Invalid number of channels: %d", nchans);
        nchans = 3;
    }
    ImageSpec spec(64, 64, nchans,
                   TypeDesc(options["type"].as_string("float")));
    ot.adjust_geometry(argv[0], spec.width, spec.height, spec.x, spec.y,
                       size.c_str());
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
        ok = ImageBufAlgo::fill(ib, &fill[0]);
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
            ok = ImageBufAlgo::fill(ib, &topleft[0], &topright[0],
                                    &bottomleft[0], &bottomright[0]);
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("top"))
                   && Strutil::extract_from_list_string(
                       bottomleft, options.get_string("bottom"))) {
            ok = ImageBufAlgo::fill(ib, &topleft[0], &bottomleft[0]);
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("left"))
                   && Strutil::extract_from_list_string(
                       topright, options.get_string("right"))) {
            ok = ImageBufAlgo::fill(ib, &topleft[0], &topright[0], &topleft[0],
                                    &topright[0]);
        } else if (Strutil::extract_from_list_string(
                       topleft, options.get_string("color"))) {
            ok = ImageBufAlgo::fill(ib, &topleft[0]);
        }
    } else if (Strutil::istarts_with(pattern, "checker")) {
        auto options = ot.extract_options(pattern);
        int width    = options.get_int("width", 8);
        int height   = options.get_int("height", 8);
        int depth    = options.get_int("depth", 8);
        std::vector<float> color1(nchans, 0.0f);
        std::vector<float> color2(nchans, 1.0f);
        Strutil::extract_from_list_string(color1, options.get_string("color1"));
        Strutil::extract_from_list_string(color2, options.get_string("color2"));
        ok = ImageBufAlgo::checker(ib, width, height, depth, &color1[0],
                                   &color2[0], 0, 0, 0);
    } else if (Strutil::istarts_with(pattern, "noise")) {
        auto options     = ot.extract_options(pattern);
        std::string type = options.get_string("type", "gaussian");
        float A = 0, B = 1;
        if (type == "gaussian") {
            A = options.get_float("mean", 0.5f);
            B = options.get_float("stddev", 0.1f);
        } else if (type == "uniform") {
            A = options.get_float("min", 0.5f);
            B = options.get_float("max", 1.0f);
        } else if (type == "salt") {
            A = options.get_float("value", 0.01f);
            B = options.get_float("portion", 0.0f);
        } else {
            ot.errorf(command, "Unknown noise type \"%s\"", type);
            ok = false;
        }
        bool mono = options.get_int("mono");
        int seed  = options.get_int("seed");
        ImageBufAlgo::zero(ib);
        if (ok)
            ok = ImageBufAlgo::noise(ib, type, A, B, mono, seed);
    } else {
        ok = ImageBufAlgo::zero(ib);
        ot.warningf(command, "Unknown pattern \"%s\"", pattern);
    }
    if (!ok)
        ot.error(command, ib.geterror());
    ot.function_times[command] += timer();
    return 0;
}



// --kernel
OIIOTOOL_OP(kernel, 0, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view kernelname(op.args(1));
    string_view kernelsize(op.args(2));
    float w = 1.0f;
    float h = 1.0f;
    if (sscanf(kernelsize.c_str(), "%fx%f", &w, &h) != 2)
        ot.errorf(op.opname(), "Unknown size %s", kernelsize);
    *img[0] = ImageBufAlgo::make_kernel(kernelname, w, h);
    return !img[0]->has_error();
});



// --capture
static int
action_capture(int argc, const char* argv[])
{
    OIIO_DASSERT(argc == 1);
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    auto options        = ot.extract_options(command);
    int camera          = options.get_int("camera");

    ImageBuf ib = ImageBufAlgo::capture_image(camera /*, TypeDesc::FLOAT*/);
    if (ib.has_error()) {
        ot.error(command, ib.geterror());
        return 0;
    }
    ImageRecRef img(new ImageRec("capture", ib.spec(), ot.imagecache));
    (*img)().copy(ib);
    ot.push(img);
    ot.function_times[command] += timer();
    return 0;
}



// --crop
int
action_crop(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_crop, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);

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
        ot.adjust_geometry(argv[0], w, h, x, y, size.c_str());
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
            ot.adjust_geometry(argv[0], w, h, x, y, size.c_str());
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

    ot.function_times[command] += timer();
    return 0;
}



// --croptofull
int
action_croptofull(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_croptofull, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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
    ot.function_times[command] += timer();
    return 0;
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
        if (roi.npixels() == 0) {
            // Special case -- all zero; but doctor to make it 1 zero pixel
            roi      = (*A)(s).roi();
            roi.xend = roi.xbegin + 1;
            roi.yend = roi.ybegin + 1;
            roi.zend = roi.zbegin + 1;
        }
        nonzero_region = roi_union(nonzero_region, roi);
    }
    return nonzero_region;
}



// --trim
int
action_trim(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_trim, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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
    ot.function_times[command] += timer();
    return 0;
}



// --cut
int
action_cut(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_cut, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);

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
                           newspec.y, size.c_str());
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

    ot.function_times[command] += timer();
    return 0;
}



// --resample
class OpResample final : public OiiotoolOp {
public:
    OpResample(Oiiotool& ot, string_view opname, int argc, const char* argv[])
        : OiiotoolOp(ot, opname, argc, argv, 1)
    {
    }
    virtual bool setup() override
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
                               newspec.full_x, newspec.full_y,
                               args(1).c_str() /*size*/, true);
            if (newspec.full_width == Aspec.full_width
                && newspec.full_height == Aspec.full_height) {
                continue;
            }
            nochange = false;
            // Compute corresponding data window.
            float wratio = float(newspec.full_width) / float(Aspec.full_width);
            float hratio = float(newspec.full_height)
                           / float(Aspec.full_height);
            newspec.x = newspec.full_x
                        + int(floorf((Aspec.x - Aspec.full_x) * wratio));
            newspec.y = newspec.full_y
                        + int(floorf((Aspec.y - Aspec.full_y) * hratio));
            newspec.width  = int(ceilf(Aspec.width * wratio));
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
    virtual bool impl(span<ImageBuf*> img) override
    {
        bool interp = options().get_int("interp", 1);
        return ImageBufAlgo::resample(*img[0], *img[1], interp);
    }
};

OP_CUSTOMCLASS(resample, OpResample, 1);



// --resize
class OpResize final : public OiiotoolOp {
public:
    OpResize(Oiiotool& ot, string_view opname, int argc, const char* argv[])
        : OiiotoolOp(ot, opname, argc, argv, 1)
    {
    }
    virtual bool setup() override
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
                               newspec.full_x, newspec.full_y,
                               args(1).c_str() /*size*/, true);
            if (newspec.full_width == Aspec.full_width
                && newspec.full_height == Aspec.full_height) {
                continue;
            }
            nochange = false;
            // Compute corresponding data window.
            float wratio = float(newspec.full_width) / float(Aspec.full_width);
            float hratio = float(newspec.full_height)
                           / float(Aspec.full_height);
            newspec.x = newspec.full_x
                        + int(floorf((Aspec.x - Aspec.full_x) * wratio));
            newspec.y = newspec.full_y
                        + int(floorf((Aspec.y - Aspec.full_y) * hratio));
            newspec.width  = int(ceilf(Aspec.width * wratio));
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
    virtual bool impl(span<ImageBuf*> img) override
    {
        std::string filtername = options()["filter"];
        if (ot.debug) {
            const ImageSpec& newspec(img[0]->spec());
            const ImageSpec& Aspec(img[1]->spec());
            std::cout << "  Resizing " << Aspec.width << "x" << Aspec.height
                      << " to " << newspec.width << "x" << newspec.height
                      << " using "
                      << (filtername.size() ? filtername.c_str() : "default")
                      << " filter\n";
        }
        return ImageBufAlgo::resize(*img[0], *img[1], filtername, 0.0f,
                                    img[0]->roi());
    }
};

OP_CUSTOMCLASS(resize, OpResize, 1);



// --fit
static int
action_fit(cspan<const char*> argv)
{
    if (ot.postpone_callback(1, action_fit, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    bool old_enable_function_timing = ot.enable_function_timing;
    ot.enable_function_timing       = false;
    string_view command             = ot.express(argv[0]);
    string_view size                = ot.express(argv[1]);

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
                       fit_full_y, size.c_str(), false);

    auto options           = ot.extract_options(command);
    bool allsubimages      = options.get_int("allsubimages", ot.allsubimages);
    bool pad               = options.get_int("pad");
    std::string filtername = options["filter"];
    std::string fillmode   = options["fillmode"];
    bool exact             = options.get_int("exact");

    int subimages = allsubimages ? A->subimages() : 1;
    ImageRecRef R(new ImageRec(A->name(), subimages));
    for (int s = 0; s < subimages; ++s) {
        ImageSpec newspec = (*A)(s, 0).spec();
        newspec.width = newspec.full_width = fit_full_width;
        newspec.height = newspec.full_height = fit_full_height;
        newspec.x = newspec.full_x = fit_full_x;
        newspec.y = newspec.full_y = fit_full_y;
        (*R)(s, 0).reset(newspec);
        ImageBufAlgo::fit((*R)(s, 0), (*A)(s, 0), filtername, 0.0f, fillmode,
                          exact);
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
        action_croptofull(1, argv);
    }

    ot.function_times[command] += timer();
    ot.enable_function_timing = old_enable_function_timing;
    return 0;
}



// --pixelaspect
static int
action_pixelaspect(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_pixelaspect, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    bool old_enable_function_timing = ot.enable_function_timing;
    ot.enable_function_timing       = false;
    string_view command             = ot.express(argv[0]);

    float new_paspect = Strutil::from_string<float>(ot.express(argv[1]));
    if (new_paspect <= 0.0f) {
        ot.errorf(command, "Invalid pixel aspect ratio '%g'", new_paspect);
        return 0;
    }

    // Examine the top of stack
    ImageRecRef A = ot.top();
    ot.read();
    const ImageSpec* Aspec = A->spec(0, 0);

    // Get the current pixel aspect ratio
    float paspect = Aspec->get_float_attribute("PixelAspectRatio", 1.0);
    if (paspect <= 0.0f) {
        ot.errorf(command, "Invalid pixel aspect ratio '%g' in source",
                  paspect);
        return 0;
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

    if (ot.debug) {
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
            command += Strutil::sprintf(":filter=%s", filtername);
        const char* newargv[2] = { command.c_str(), resize.c_str() };
        action_resize(2, newargv);
        A                         = ot.top();
        A->spec(0, 0)->full_width = (*A)(0, 0).specmod().full_width
            = scale_full_width;
        A->spec(0, 0)->full_height = (*A)(0, 0).specmod().full_height
            = scale_full_height;
        A->spec(0, 0)->attribute("PixelAspectRatio", new_paspect);
        if (xres)
            A->spec(0, 0)->attribute("XResolution", scale_xres);
        if (yres)
            A->spec(0, 0)->attribute("YResolution", scale_yres);
        // Now A,Aspec are for the NEW resized top of stack
    }

    ot.function_times[command] += timer();
    ot.enable_function_timing = old_enable_function_timing;
    return 0;
}



// --convolve
BINARY_IMAGE_OP(convolve, ImageBufAlgo::convolve);



// --blur
OIIOTOOL_OP(blur, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view kernopt = op.options().get_string("kernel", "gaussian");
    float w             = 1.0f;
    float h             = 1.0f;
    if (sscanf(op.args(1).c_str(), "%fx%f", &w, &h) != 2)
        ot.errorf(op.opname(), "Unknown size %s", op.args(1));
    ImageBuf Kernel = ImageBufAlgo::make_kernel(kernopt, w, h);
    if (Kernel.has_error()) {
        ot.error(op.opname(), Kernel.geterror());
        return false;
    }
    return ImageBufAlgo::convolve(*img[0], *img[1], Kernel);
});



// --median
OIIOTOOL_OP(median, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (sscanf(size.c_str(), "%dx%d", &w, &h) != 2)
        ot.errorf(op.opname(), "Unknown size %s", size);
    return ImageBufAlgo::median_filter(*img[0], *img[1], w, h);
});



// --dilate
OIIOTOOL_OP(dilate, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (sscanf(size.c_str(), "%dx%d", &w, &h) != 2)
        ot.errorf(op.opname(), "Unknown size %s", size);
    return ImageBufAlgo::dilate(*img[0], *img[1], w, h);
});



// --erode
OIIOTOOL_OP(erode, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    string_view size(op.args(1));
    int w = 3;
    int h = 3;
    if (sscanf(size.c_str(), "%dx%d", &w, &h) != 2)
        ot.errorf(op.opname(), "Unknown size %s", size);
    return ImageBufAlgo::erode(*img[0], *img[1], w, h);
});



// --unsharp
OIIOTOOL_OP(unsharp, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
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



// --fixnan
int
action_fixnan(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_fixnan, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command  = ot.express(argv[0]);
    string_view modename = ot.express(argv[1]);

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
        ot.warningf(argv[0],
                    "\"%s\" not recognized. Valid choices: black, box3, error",
                    modename);
    }
    ot.read();
    ImageRecRef A = ot.pop();
    ot.push(new ImageRec(*A, allsubimages ? -1 : 0, allsubimages ? -1 : 0, true,
                         false));
    int subimages = allsubimages ? A->subimages() : 1;
    for (int s = 0; s < subimages; ++s) {
        int miplevels = ot.curimg->miplevels(s);
        for (int m = 0; m < miplevels; ++m) {
            const ImageBuf& Aib((*A)(s, m));
            ImageBuf& Rib((*ot.curimg)(s, m));
            bool ok = ImageBufAlgo::fixNonFinite(Rib, Aib, mode);
            if (!ok) {
                ot.error(command, Rib.geterror());
                return 0;
            }
        }
    }

    ot.function_times[command] += timer();
    return 0;
}



// --fillholes
static int
action_fillholes(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_fillholes, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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

    ot.function_times[command] += timer();
    return 0;
}



// --paste
static int
action_paste(int argc, const char* argv[])
{
    if (ot.postpone_callback(2, action_paste, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command  = ot.express(argv[0]);
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
            Strutil::printf("    paste/1 %d (total time %s, mem %s)\n", i,
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
    } else if (sscanf(position.c_str(), "%d%d", &x, &y) != 2) {
        ot.errorf(command, "Invalid offset '%s'", position);
        return 0;
    }

    if (spec.deep) {
        // Special work for deep images -- to make it efficient, we need
        // to pre-allocate the fully merged set of samples.
        for (int i = 0; i < ninputs; ++i) {
            if (ot.debug && ninputs > 4)
                Strutil::printf("    paste/2 %d (total time %s, mem %s)\n", i,
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
        return 0;
    }

    // Now paste the other images, back to front
    for (int i = 1; i < ninputs && ok; ++i) {
        if (ot.debug && ninputs > 4)
            Strutil::printf("    paste/3 %d (total time %s, mem %s)\n", i,
                            Strutil::timeintervalformat(ot.total_runtime(), 2),
                            Strutil::memformat(Sysutil::memory_used()));
        ImageRecRef FG = inputs[i];
        ok             = ImageBufAlgo::paste(*Rbuf, x, y, 0, 0, (*FG)());
        if (!ok)
            ot.error(command, Rbuf->geterror());
    }

    ImageRecRef R(new ImageRec(Rbuf, /*copy_pixels=*/false));
    ot.push(R);

    ot.function_times[command] += timer();
    return 0;
}



// --pastemeta
OIIOTOOL_OP(pastemeta, 2, [](OiiotoolOp& op, span<ImageBuf*> img) {
    *img[0] = *img[2];
    img[0]->copy_metadata(*img[1]);
    return true;
});



// --mosaic
static int
action_mosaic(int /*argc*/, const char* argv[])
{
    Timer timer(ot.enable_function_timing);

    // Mosaic is tricky. We have to parse the argument before we know
    // how many images it wants to pull off the stack.
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);
    int ximages = 0, yimages = 0;
    if (sscanf(size.c_str(), "%dx%d", &ximages, &yimages) != 2 || ximages < 1
        || yimages < 1) {
        ot.errorf(command, "Invalid size '%s'", size);
        return 0;
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
    std::vector<ImageRecRef> images(nimages);
    for (int i = nimages - 1; i >= 0; --i) {
        ImageRecRef img = ot.pop();
        images[i]       = img;
        ot.read(img);
        widest    = std::max(widest, img->spec()->full_width);
        highest   = std::max(highest, img->spec()->full_height);
        nchannels = std::max(nchannels, img->spec()->nchannels);
    }

    auto options = ot.extract_options(command);
    int pad      = options.get_int("pad");

    std::string fit = options["fit"];
    if (fit.size()) {
        int fitw = 0, fith = 0;
        if (sscanf(fit.c_str(), "%dx%d", &fitw, &fith) == 2 && fitw >= 1
            && fith >= 1) {
            widest  = fitw;
            highest = fith;
            // Do the equivalent of a --fit on each image
            const char* fitargs[] = { "--fit:allsubimages=0:pad=1",
                                      fit.c_str() };
            for (int i = 0; i < nimages; ++i) {
                ot.push(images[i]);
                action_fit(fitargs);
                images[i] = ot.pop();
            }
        }
    }

    ImageSpec Rspec(ximages * widest + (ximages - 1) * pad,
                    yimages * highest + (yimages - 1) * pad, nchannels,
                    TypeDesc::FLOAT);
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
                return 0;
            }
        }
    }

    ot.function_times[command] += timer();
    return 0;
}



// --over
BINARY_IMAGE_OP(over, ImageBufAlgo::over);



// --zover
OIIOTOOL_OP(zover, 2, [](OiiotoolOp& op, span<ImageBuf*> img) {
    bool zeroisinf = op.options().get_int("zeroisinf");
    return ImageBufAlgo::zover(*img[0], *img[1], *img[2], zeroisinf, ROI(), 0);
});



BINARY_IMAGE_OP(deepmerge, ImageBufAlgo::deep_merge);      // --deepmerge
BINARY_IMAGE_OP(deepholdout, ImageBufAlgo::deep_holdout);  // --deepholdout



// --deepen
OIIOTOOL_OP(deepen, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    float z = op.options().get_float("z", 1.0f);
    return ImageBufAlgo::deepen(*img[0], *img[1], z);
});


// --flatten
UNARY_IMAGE_OP(flatten, ImageBufAlgo::flatten);



static int
action_fill(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_fill, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);
    string_view size    = ot.express(argv[1]);

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
        if (!ot.adjust_geometry(argv[0], w, h, x, y, size.c_str(), true))
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
            ok = ImageBufAlgo::fill(Rib, &topleft[0], &topright[0],
                                    &bottomleft[0], &bottomright[0],
                                    ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("top"))
                   && Strutil::extract_from_list_string(
                       bottomleft, options.get_string("bottom"))) {
            ok = ImageBufAlgo::fill(Rib, &topleft[0], &bottomleft[0],
                                    ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(topleft,
                                                     options.get_string("left"))
                   && Strutil::extract_from_list_string(
                       topright, options.get_string("right"))) {
            ok = ImageBufAlgo::fill(Rib, &topleft[0], &topright[0], &topleft[0],
                                    &topright[0], ROI(x, x + w, y, y + h));
        } else if (Strutil::extract_from_list_string(
                       topleft, options.get_string("color"))) {
            ok = ImageBufAlgo::fill(Rib, &topleft[0], ROI(x, x + w, y, y + h));
        } else {
            ot.warning(command,
                       "No recognized fill parameters: filling with white.");
            ok = ImageBufAlgo::fill(Rib, &topleft[0], ROI(x, x + w, y, y + h));
        }
        if (!ok) {
            ot.error(command, Rib.geterror());
            break;
        }
    }

    ot.function_times[command] += timer();
    return 0;
}



BINARY_IMAGE_OP(max, ImageBufAlgo::max);            // --max
BINARY_IMAGE_COLOR_OP(maxc, ImageBufAlgo::max, 0);  // --maxc
BINARY_IMAGE_OP(min, ImageBufAlgo::min);            // --min
BINARY_IMAGE_COLOR_OP(minc, ImageBufAlgo::min, 0);  // --minc



// --clamp
static int
action_clamp(int argc, const char* argv[])
{
    if (ot.postpone_callback(1, action_clamp, argc, argv))
        return 0;
    Timer timer(ot.enable_function_timing);
    string_view command = ot.express(argv[0]);

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
            bool ok = ImageBufAlgo::clamp(Rib, Aib, &min[0], &max[0],
                                          clampalpha01);
            if (!ok) {
                ot.error(command, Rib.geterror());
                return 0;
            }
        }
    }

    ot.function_times[command] += timer();
    return 0;
}



// --rangecompress
OIIOTOOL_OP(rangecompress, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    bool useluma = op.options().get_int("luma");
    return ImageBufAlgo::rangecompress(*img[0], *img[1], useluma);
});

// --rangeexpand
OIIOTOOL_OP(rangeexpand, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    bool useluma = op.options().get_int("luma");
    return ImageBufAlgo::rangeexpand(*img[0], *img[1], useluma);
});



// --contrast
OIIOTOOL_OP(contrast, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    size_t n   = size_t((*img[0]).nchannels());
    auto black = Strutil::extract_from_list_string(
        op.options().get_string("black", "0"), n, 0.0f);
    auto white = Strutil::extract_from_list_string(
        op.options().get_string("white", "1"), n, 1.0f);
    auto min
        = Strutil::extract_from_list_string(op.options().get_string("min", "0"),
                                            n, 0.0f);
    auto max
        = Strutil::extract_from_list_string(op.options().get_string("max", "1"),
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
OIIOTOOL_OP(box, 1, ([](OiiotoolOp& op, span<ImageBuf*> img) {
    img[0]->copy(*img[1]);
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
OIIOTOOL_OP(line, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    img[0]->copy(*img[1]);
    const ImageSpec& Rspec(img[0]->spec());
    std::vector<int> points;
    Strutil::extract_from_list_string(points, op.args(1));
    std::vector<float> color(Rspec.nchannels + 1, 1.0f);
    Strutil::extract_from_list_string(color, op.options().get_string("color"));
    bool closed = (points.size() > 4 && points[0] == points[points.size() - 2]
                   && points[1] == points[points.size() - 1]);
    bool ok     = true;
    for (size_t i = 0, e = points.size() - 2; i < e; i += 2)
        ok &= ImageBufAlgo::render_line(*img[0], points[i + 0], points[i + 1],
                                        points[i + 2], points[i + 3], color,
                                        closed || i > 0 /*skip_first_point*/);
    return ok;
});



// --text
OIIOTOOL_OP(text, 1, [](OiiotoolOp& op, span<ImageBuf*> img) {
    img[0]->copy(*img[1]);
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
    int shadow = op.options().get_int("shadow");
    return ImageBufAlgo::render_text(*img[0], x, y, op.args(1), fontsize, font,
                                     textcolor, alignx, aligny, shadow);
});



// -i
static int
input_file(int argc, const char* argv[])
{
    ot.total_readtime.start();
    string_view command = ot.express(argv[0]);
    if (argc > 1 && Strutil::starts_with(command, "-i")) {
        --argc;
        ++argv;
    } else {
        command = "-i";
    }
    auto fileoptions     = ot.extract_options(command);
    int printinfo        = fileoptions.get_int("info", ot.printinfo);
    bool readnow         = fileoptions.get_int("now", 0);
    bool autocc          = fileoptions.get_int("autocc", ot.autocc);
    bool autoccunpremult = fileoptions.get_int("unpremult", ot.autoccunpremult);
    std::string infoformat = fileoptions.get_string("infoformat",
                                                    ot.printinfo_format);
    TypeDesc input_dataformat(fileoptions.get_string("type"));
    std::string channel_set = fileoptions["ch"];

    for (int i = 0; i < argc; i++) {  // FIXME: this loop is pointless
        string_view filename = ot.express(argv[i]);
        auto found           = ot.image_labels.find(filename);
        if (found != ot.image_labels.end()) {
            if (ot.debug)
                std::cout << "Referencing labeled image " << filename << "\n";
            ot.push(found->second);
            ot.process_pending();
            break;
        }
        Timer timer(ot.enable_function_timing);
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
        // If the image doesn't appear t exist, but it's a procedural image
        // generator, then that's ok.
        if (!exists) {
            auto input = ImageInput::create(filename);
            if (input && input->supports("procedural"))
                exists = 1;
        }
        ImageBufRef substitute;  // possible substitute for missing image
        if (!exists) {
            // Try to get a more precise error message to report
            if (!Filesystem::exists(filename))
                ot.errorf("read", "File does not exist: \"%s\"", filename);
            else {
                auto in         = ImageInput::open(filename);
                std::string err = in ? in->geterror() : OIIO::geterror();
                ot.error("read", ot.format_read_error(filename, err));
            }
            // Second chances: do we have a substitute image policy?
            if (ot.missingfile_policy == "black") {
                ImageSpec substitute_spec = ot.first_input_dimensions;
                if (substitute_spec.format == TypeUnknown
                    || !substitute_spec.width || !substitute_spec.height
                    || !substitute_spec.nchannels)
                    substitute_spec = ImageSpec(1920, 1080, 4);
                substitute.reset(
                    new ImageBuf(substitute_spec, InitializePixels::Yes));
            } else if (ot.missingfile_policy == "checker") {
                ImageSpec substitute_spec = ot.first_input_dimensions;
                if (substitute_spec.format == TypeUnknown
                    || !substitute_spec.width || !substitute_spec.height
                    || !substitute_spec.nchannels)
                    substitute_spec = ImageSpec(1920, 1080, 4);
                substitute.reset(new ImageBuf(substitute_spec));
                ImageBufAlgo::checker(*substitute, 64, 64, 1,
                                      { 0.0f, 0.0f, 0.0f, 1.0f },
                                      { 1.0f, 1.0f, 1.0f, 1.0f });
            }
            if (!substitute)
                break;
        }
        if (channel_set.size()) {
            ot.input_channel_set = channel_set;
            readnow              = true;
        }

        if (substitute) {
            ot.push(ImageRecRef(new ImageRec(substitute)));
            readnow = false;
            ap.abort(false);
        } else {
            if (ot.debug || ot.verbose)
                std::cout << "Reading " << filename << "\n";
            ot.push(ImageRecRef(new ImageRec(filename, ot.imagecache)));
            if (ot.input_config_set)
                ot.curimg->configspec(ot.input_config);
            ot.curimg->input_dataformat(input_dataformat);
            if (readnow)
                ot.curimg->read(ReadNoCache, channel_set);
            else
                ot.curimg->read_nativespec();
            if (ot.first_input_dimensions.format == TypeUnknown) {
                ot.first_input_dimensions.copy_dimensions(
                    *ot.curimg->nativespec());
                ot.first_input_dimensions.channelnames
                    = ot.curimg->nativespec()->channelnames;
            }
        }
        if ((printinfo || ot.printstats || ot.dumpdata || ot.hash)
            && !substitute) {
            OiioTool::print_info_options pio;
            pio.verbose   = ot.verbose || printinfo > 1 || ot.printinfo_verbose;
            pio.subimages = ot.allsubimages || printinfo > 1;
            pio.compute_stats      = ot.printstats;
            pio.dumpdata           = ot.dumpdata;
            pio.dumpdata_showempty = ot.dumpdata_showempty;
            pio.compute_sha1       = ot.hash;
            pio.metamatch          = ot.printinfo_metamatch;
            pio.nometamatch        = ot.printinfo_nometamatch;
            pio.infoformat         = infoformat;
            std::string error;
            bool ok = OiioTool::print_info(std::cout, ot, filename, pio, error);
            if (!ok) {
                ot.error("read", ot.format_read_error(filename, error));
                break;
            }
            ot.printed_info = true;
        }
        ot.function_times["input"] += timer();
        if (ot.autoorient) {
            int action_reorient(int argc, const char* argv[]);
            const char* argv[] = { "--reorient" };
            action_reorient(1, argv);
        }

        if (autocc) {
            // Try to deduce the color space it's in
            string_view colorspace(
                ot.colorconfig.getColorSpaceFromFilepath(filename));
            if (colorspace.size() && ot.debug)
                std::cout << "  From " << filename
                          << ", we deduce color space \"" << colorspace
                          << "\"\n";
            if (colorspace.empty()) {
                ot.read();
                colorspace = ot.curimg->spec()->get_string_attribute(
                    "oiio:ColorSpace");
                if (ot.debug)
                    std::cout << "  Metadata of " << filename
                              << " indicates color space \"" << colorspace
                              << "\"\n";
            }
            string_view linearspace = ot.colorconfig.getColorSpaceNameByRole(
                "linear");
            if (linearspace.empty())
                linearspace = string_view("Linear");
            if (colorspace.size()
                && !Strutil::iequals(colorspace, linearspace)) {
                std::string cmd = "colorconvert:strict=0";
                if (autoccunpremult)
                    cmd += ":unpremult=1";
                const char* argv[] = { cmd.c_str(), colorspace.c_str(),
                                       linearspace.c_str() };
                if (ot.debug)
                    std::cout << "  Converting " << filename << " from "
                              << colorspace << " to " << linearspace << "\n";
                action_colorconvert(3, argv);
            }
        }

        ot.process_pending();
    }

    ot.clear_input_config();
    ot.input_channel_set.clear();
    ot.check_peak_memory();
    ot.total_readtime.stop();
    return 0;
}



static void
prep_texture_config(ImageSpec& configspec, ParamValueList& fileoptions)
{
    configspec.tile_width  = ot.output_tilewidth ? ot.output_tilewidth : 64;
    configspec.tile_height = ot.output_tileheight ? ot.output_tileheight : 64;
    configspec.tile_depth  = 1;
    std::string wrap       = fileoptions.get_string("wrap", "black");
    std::string swrap      = fileoptions.get_string("swrap", wrap);
    std::string twrap      = fileoptions.get_string("twrap", wrap);
    configspec.attribute("wrapmodes", Strutil::sprintf("%s,%s", swrap, twrap));
    configspec.attribute("maketx:verbose", ot.verbose);
    configspec.attribute("maketx:runstats", ot.runstats);
    configspec.attribute("maketx:resize", fileoptions.get_int("resize"));
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
                            fileoptions.get_int("hilightcomp",
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
    configspec.attribute("maketx:uvslopes_scale",
                         fileoptions.get_float("uvslopes_scale", 0.0f));
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
static int
output_file(int /*argc*/, const char* argv[])
{
    Timer timer(ot.enable_function_timing);
    ot.total_writetime.start();
    string_view command  = ot.express(argv[0]);
    string_view filename = ot.express(argv[1]);

    auto fileoptions = ot.extract_options(command);

    string_view stripped_command = command;
    Strutil::parse_char(stripped_command, '-');
    Strutil::parse_char(stripped_command, '-');
    bool do_tex     = Strutil::starts_with(stripped_command, "otex");
    bool do_latlong = Strutil::starts_with(stripped_command, "oenv")
                      || Strutil::starts_with(stripped_command, "olatlong");
    bool do_shad       = Strutil::starts_with(stripped_command, "oshad");
    bool do_bumpslopes = Strutil::starts_with(stripped_command, "obump");

    if (ot.debug)
        std::cout << "Output: " << filename << "\n";
    if (!ot.curimg.get()) {
        ot.warningf(command, "%s did not have any current image to output.",
                    filename);
        return 0;
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
        for (int i = 0; i < nimages; ++i) {
            if (i < nimages - 1)
                ot.curimg = ot.image_stack[i];
            else
                ot.curimg
                    = saved_curimg;  // note: last iteration also restores it!
            // Skip 0x0 images. Yes, this can happen.
            ot.curimg->read();
            const ImageSpec* spec(ot.curimg->spec());
            if (spec->width < 1 || spec->height < 1 || spec->depth < 1)
                continue;
            // Use the filename as a pattern, format with the frame number
            new_argv[1]
                = ustring::sprintf(filename.c_str(), i + startnumber).c_str();
            // recurse for this file
            output_file(2, new_argv);
        }
        return 0;
    }

    if (ot.noclobber && Filesystem::exists(filename)) {
        ot.warningf(command, "%s already exists, not overwriting.", filename);
        return 0;
    }
    std::string formatname = fileoptions.get_string("fileformatname", filename);
    auto out               = ImageOutput::create(formatname);
    if (!out) {
        std::string err = OIIO::geterror();
        ot.error(command, err.size() ? err.c_str()
                                     : "unknown error creating an ImageOutput");
        return 0;
    }
    bool supports_displaywindow  = out->supports("displaywindow");
    bool supports_negativeorigin = out->supports("negativeorigin");
    bool supports_tiles = out->supports("tiles") || ot.output_force_tiles;
    bool procedural     = out->supports("procedural");
    ot.read();
    ImageRecRef saveimg = ot.curimg;
    ImageRecRef ir(ot.curimg);
    TypeDesc saved_output_dataformat = ot.output_dataformat;
    int saved_bitspersample          = ot.output_bitspersample;

    timer.stop();  // resume after all these auto-transforms

    // Automatically drop channels we can't support in output
    if ((ir->spec()->nchannels > 4 && !out->supports("nchannels"))
        || (ir->spec()->nchannels > 3 && !out->supports("alpha"))) {
        bool alpha = (ir->spec()->nchannels > 3 && out->supports("alpha"));
        string_view chanlist = alpha ? "R,G,B,A" : "R,G,B";
        std::vector<int> channels;
        bool found = parse_channels(*ir->spec(), chanlist, channels);
        if (!found)
            chanlist = alpha ? "0,1,2,3" : "0,1,2";
        const char* argv[] = { "channels:allsubimages=1", chanlist.c_str() };
        int action_channels(int argc, const char* argv[]);  // forward decl
        action_channels(2, argv);
        ot.warningf(command, "Can't save %d channels to %s... saving only %s",
                    ir->spec()->nchannels, out->format_name(), chanlist);
        ir = ot.curimg;
    }

    // Handle --autotrim
    int autotrim = fileoptions.get_int("autotrim", ot.output_autotrim);
    if (supports_displaywindow && autotrim) {
        ROI roi           = nonzero_region_all_subimages(ir);
        bool crops_needed = false;
        for (int s = 0; s < ir->subimages(); ++s)
            crops_needed |= (roi != (*ir)(s).roi());
        if (crops_needed) {
            std::string crop
                = (ir->spec(0, 0)->depth == 1)
                      ? format_resolution(roi.width(), roi.height(), roi.xbegin,
                                          roi.ybegin)
                      : format_resolution(roi.width(), roi.height(),
                                          roi.depth(), roi.xbegin, roi.ybegin,
                                          roi.zbegin);
            const char* argv[] = { "crop:allsubimages=1", crop.c_str() };
            int action_crop(int argc, const char* argv[]);  // forward decl
            action_crop(2, argv);
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
        int action_croptofull(int argc, const char* argv[]);  // forward decl
        action_croptofull(1, argv);
        ir = ot.curimg;
    }

    // See if the filename appears to contain a color space name embedded.
    // Automatically color convert if --autocc is used and the current
    // color space doesn't match that implied by the filename, and
    // automatically set -d based on the name if --autocc is used.
    bool autocc          = fileoptions.get_int("autocc", ot.autocc);
    bool autoccunpremult = fileoptions.get_int("unpremult", ot.autoccunpremult);
    string_view outcolorspace = ot.colorconfig.getColorSpaceFromFilepath(
        filename);
    if (autocc && outcolorspace.size()) {
        TypeDesc type;
        int bits;
        type = ot.colorconfig.getColorSpaceDataType(outcolorspace, &bits);
        if (type.basetype != TypeDesc::UNKNOWN) {
            if (ot.debug)
                std::cout << "  Deduced data type " << type << " (" << bits
                          << "bits) for output to " << filename << "\n";
            if ((ot.output_dataformat && ot.output_dataformat != type)
                || (bits && ot.output_bitspersample
                    && ot.output_bitspersample != bits)) {
                ot.warningf(
                    command,
                    "Output filename (%s) colorspace \"%s\" implies %s (%d bits), overriding prior request for %s.",
                    filename, outcolorspace, type, bits, ot.output_dataformat);
            }
            ot.output_dataformat    = type;
            ot.output_bitspersample = bits;
        }
    }
    if (autocc) {
        string_view linearspace = ot.colorconfig.getColorSpaceNameByRole(
            "linear");
        if (linearspace.empty())
            linearspace = string_view("Linear");
        string_view currentspace
            = ir->spec()->get_string_attribute("oiio:ColorSpace", linearspace);
        // Special cases where we know formats should be particular color
        // spaces
        if (outcolorspace.empty()
            && (Strutil::iends_with(filename, ".jpg")
                || Strutil::iends_with(filename, ".jpeg")
                || Strutil::iends_with(filename, ".gif")
                || Strutil::iends_with(filename, ".webp")))
            outcolorspace = string_view("sRGB");
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
            action_colorconvert(3, argv);
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
        std::string crop   = (ir->spec(0, 0)->depth == 1)
                                 ? format_resolution(roi.width(), roi.height(),
                                                   roi.xbegin, roi.ybegin)
                                 : format_resolution(roi.width(), roi.height(),
                                                   roi.depth(), roi.xbegin,
                                                   roi.ybegin, roi.zbegin);
        const char* argv[] = { "crop:allsubimages=1", crop.c_str() };
        int action_crop(int argc, const char* argv[]);  // forward decl
        action_crop(2, argv);
        ir = ot.curimg;
    }

    if (ot.dryrun) {
        ot.curimg               = saveimg;
        ot.output_dataformat    = saved_output_dataformat;
        ot.output_bitspersample = saved_bitspersample;
        return 0;
    }

    timer.start();
    if (ot.debug || ot.verbose)
        std::cout << "Writing " << filename << "\n";

    // FIXME -- the various automatic transformations above neglect to handle
    // MIPmaps or subimages with full generality.

    bool ok = true;
    if (do_tex || do_latlong || do_bumpslopes) {
        ImageSpec configspec;
        adjust_output_options(filename, configspec, nullptr, ot, supports_tiles,
                              fileoptions);
        prep_texture_config(configspec, fileoptions);
        ImageBufAlgo::MakeTextureMode mode = ImageBufAlgo::MakeTxTexture;
        if (do_shad)
            mode = ImageBufAlgo::MakeTxShadow;
        if (do_latlong)
            mode = ImageBufAlgo::MakeTxEnvLatl;
        if (do_bumpslopes)
            mode = ImageBufAlgo::MakeTxBumpWithSlopes;
        // if (lightprobemode)
        //     mode = ImageBufAlgo::MakeTxEnvLatlFromLightProbe;
        ok = ImageBufAlgo::make_texture(mode, (*ir)(0, 0), filename,
                                        configspec);
        if (!ok) {
            ot.errorfmt(command, "Could not make texture: {}",
                        OIIO::geterror());
            return 0;
        }
        // N.B. make_texture already internally writes to a temp file and
        // then atomically moves it to the final destination, so we don't
        // need to explicitly do that here.
    } else {
        // Non-texture case
        std::vector<ImageSpec> subimagespecs(ir->subimages());
        for (int s = 0; s < ir->subimages(); ++s) {
            ImageSpec spec = *ir->spec(s, 0);
            adjust_output_options(filename, spec, ir->nativespec(s), ot,
                                  supports_tiles, fileoptions,
                                  (*ir)[s].was_direct_read());
            // For deep files, must copy the native deep channelformats
            if (spec.deep)
                spec.channelformats = (*ir)(s, 0).nativespec().channelformats;
            // If it's not tiled and MIP-mapped, remove any "textureformat"
            if (!spec.tile_pixels() || ir->miplevels(s) <= 1)
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
                return 0;
            }
        } else {
            if (!out->open(tmpfilename, subimagespecs[0], mode)) {
                ot.error(command, out->geterror());
                return 0;
            }
        }

        // Output all the subimages and MIP levels
        for (int s = 0, send = ir->subimages(); s < send; ++s) {
            for (int m = 0, mend = ir->miplevels(s); m < mend && ok; ++m) {
                ImageSpec spec = *ir->spec(s, m);
                adjust_output_options(filename, spec, ir->nativespec(s, m), ot,
                                      supports_tiles, fileoptions,
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
                        ot.warningf(command,
                                    "%s does not support MIP-maps for %s",
                                    out->format_name(), filename);
                        break;
                    }
                }
            }
            mode = ImageOutput::AppendSubimage;  // for next subimage
            if (send > 1 && !out->supports("multiimage")) {
                ot.warningf(command,
                            "%s does not support multiple subimages for %s",
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
                ot.errorf(
                    command,
                    "oiiotool ERROR: could not move temp file %s to %s: %s",
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
    ot.function_times[command] += optime;
    ot.num_outputs += 1;

    if (ot.debug)
        Strutil::printf("    output took %s  (total time %s, mem %s)\n",
                        Strutil::timeintervalformat(optime, 2),
                        Strutil::timeintervalformat(ot.total_runtime(), 2),
                        Strutil::memformat(Sysutil::memory_used()));
    return 0;
}



// --echo
static void
do_echo(cspan<const char*> argv)
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



static void
crash_me()
{
    size_t a   = 37;
    char* addr = (char*)a;
    OIIO_PRAGMA_WARNING_PUSH
#if OIIO_GNUC_VERSION >= 110000
    OIIO_GCC_ONLY_PRAGMA(GCC diagnostic ignored "-Wstringop-overflow")
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
                || !strcmp(argv[i], "--sattrib")
                || !strcmp(argv[i], "-sattrib")) {
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



static void
print_help_end(std::ostream& out)
{
    out << "\n";
    int columns = Sysutil::terminal_columns() - 2;

    out << formatted_format_list("Input", "input_format_list") << "\n";
    out << formatted_format_list("Output", "output_format_list") << "\n";

    // debugging color space names
    int ociover = ot.colorconfig.OpenColorIO_version_hex();
    if (ociover)
        out << "OpenColorIO " << (ociover >> 24) << '.'
            << ((ociover >> 16) & 0xff) << '.' << ((ociover >> 8) & 0xff);
    else
        out << "No OpenColorIO";
    out << ", color config: " << ot.colorconfig.configname() << "\n";
    std::stringstream s;
    s << "Known color spaces: ";
    const char* linear = ot.colorconfig.getColorSpaceNameByRole("linear");
    for (int i = 0, e = ot.colorconfig.getNumColorSpaces(); i < e; ++i) {
        const char* n = ot.colorconfig.getColorSpaceNameByIndex(i);
        s << "\"" << n << "\"";
        if (linear && !Strutil::iequals(n, "linear")
            && Strutil::iequals(n, linear))
            s << " (linear)";
        if (i < e - 1)
            s << ", ";
    }
    out << Strutil::wordwrap(s.str(), columns, 4) << "\n";

    int nlooks = ot.colorconfig.getNumLooks();
    if (nlooks) {
        std::stringstream s;
        s << "Known looks: ";
        for (int i = 0; i < nlooks; ++i) {
            const char* n = ot.colorconfig.getLookNameByIndex(i);
            s << "\"" << n << "\"";
            if (i < nlooks - 1)
                s << ", ";
        }
        out << Strutil::wordwrap(s.str(), columns, 4) << "\n";
    }

    const char* default_display = ot.colorconfig.getDefaultDisplayName();
    int ndisplays               = ot.colorconfig.getNumDisplays();
    if (ndisplays) {
        std::stringstream s;
        s << "Known displays: ";
        for (int i = 0; i < ndisplays; ++i) {
            const char* d = ot.colorconfig.getDisplayNameByIndex(i);
            s << "\"" << d << "\"";
            if (!strcmp(d, default_display))
                s << "*";
            const char* default_view = ot.colorconfig.getDefaultViewName(d);
            int nviews               = ot.colorconfig.getNumViews(d);
            if (nviews) {
                s << " (views: ";
                for (int i = 0; i < nviews; ++i) {
                    const char* v = ot.colorconfig.getViewNameByIndex(d, i);
                    s << "\"" << v << "\"";
                    if (!strcmp(v, default_view))
                        s << "*";
                    if (i < nviews - 1)
                        s << ", ";
                }
                s << ")";
            }
            if (i < ndisplays - 1)
                s << ", ";
        }
        s << " (* = default)";
        out << Strutil::wordwrap(s.str(), columns, 4) << "\n";
    }
    if (!ot.colorconfig.supportsOpenColorIO())
        out << "No OpenColorIO support was enabled at build time.\n";

    std::vector<string_view> filternames;
    for (int i = 0, e = Filter2D::num_filters(); i < e; ++i)
        filternames.emplace_back(Filter2D::get_filterdesc(i).name);
    out << Strutil::wordwrap("Filters available: "
                                 + Strutil::join(filternames, ", "),
                             columns, 4)
        << "\n";

    std::string libs = OIIO::get_string_attribute("library_list");
    if (libs.size()) {
        std::vector<string_view> libvec;
        Strutil::split(libs, libvec, ";");
        for (auto& lib : libvec) {
            size_t pos = lib.find(':');
            lib.remove_prefix(pos + 1);
        }
        out << Strutil::wordwrap("Dependent libraries: "
                                     + Strutil::join(libvec, ", "),
                                 columns, 4)
            << std::endl;
    }

    // Print the HW info
    std::string buildsimd = OIIO::get_string_attribute("oiio:simd");
    if (!buildsimd.size())
        buildsimd = "no SIMD";
    auto buildinfo
        = Strutil::sprintf("OIIO %s built for C++%d/%d %s", OIIO_VERSION_STRING,
                           OIIO_CPLUSPLUS_VERSION, __cplusplus, buildsimd);
    out << Strutil::wordwrap(buildinfo, columns, 4) << std::endl;
    auto hwinfo = Strutil::sprintf("Running on %d cores %.1fGB %s",
                                   Sysutil::hardware_concurrency(),
                                   Sysutil::physical_memory() / float(1 << 30),
                                   OIIO::get_string_attribute("hw:simd"));
    out << Strutil::wordwrap(hwinfo, columns, 4) << std::endl;

    // Print the path to the docs. If found, use the one installed in the
    // same area is this executable, otherwise just point to the copy on
    // GitHub corresponding to our version of the softare.
    out << "Full OIIO documentation can be found at\n";
    out << "    https://openimageio.readthedocs.io\n";
}



static void
print_help(ArgParse& ap)
{
    ap.print_help();
    print_help_end(std::cout);
}



static void list_formats(cspan<const char*>)
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
getargs(int argc, char* argv[])
{
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
      .action(input_file);
    ap.separator("Options (general):");
    ap.arg("--help", &help)
      .help("Print help message");
    ap.arg("-v", &ot.verbose)
      .help("Verbose status messages");
    ap.arg("-q %!", &ot.verbose)
      .help("Quiet mode (turn verbose off)");
    ap.arg("-n", &ot.dryrun)
      .help("No saved output (dry run)");
    ap.arg("-a", &ot.allsubimages)
      .help("Do operations on all subimages/miplevels");
    ap.arg("--debug", &ot.debug)
      .help("Debug mode");
    ap.arg("--runstats", &ot.runstats)
      .help("Print runtime statistics");
    ap.arg("--info")
      .help("Print resolution and basic info on all inputs, detailed metadata if -v is also used (options: format=xml:verbose=1)")
      .action(set_printinfo);
    ap.arg("--list-formats")
      .help("List all supported file formats and their filename extensions")
      .action(list_formats);
    ap.arg("--echo %s:TEXT")
      .help("Echo message to console (options: newline=0)")
      .action(do_echo);
    ap.arg("--metamatch %s:REGEX", &ot.printinfo_metamatch)
      .help("Which metadata is printed with -info -v");
    ap.arg("--no-metamatch %s:REGEX", &ot.printinfo_nometamatch)
      .help("Which metadata is excluded with -info -v");
    ap.arg("--stats", &ot.printstats)
      .help("Print pixel statistics on all inputs");
    ap.arg("--dumpdata")
      .help("Print all pixel data values (options: empty=0)")
      .action(set_dumpdata);
    ap.arg("--hash", &ot.hash)
      .help("Print SHA-1 hash of each input image");
    ap.arg("--colorcount %s:COLORLIST")
       .help("Count of how many pixels have the given color (argument: color;color;...) (options: eps=color)")
       .action(action_colorcount);
    ap.arg("--rangecheck %s:MIN %s:MAX")
       .help("Count of how many pixels are outside the min/max color range (each is a comma-separated color value list)")
       .action(action_rangecheck);
    ap.arg("-u", &ot.updatemode)
      .help("Update mode: skip outputs when the file exists and is newer than all inputs");
    ap.arg("--no-clobber", &ot.noclobber)
      .help("Do not overwrite existing files");
    ap.arg("--noclobber", &ot.noclobber)
      .hidden(); // synonym
    ap.arg("--threads %d:N")
      .help("Number of threads (default 0 == #cores)")
      .action(set_threads);
    ap.arg("--frames %s:FRAMERANGE")
      .help("Frame range for '#' or printf-style wildcards");
    ap.arg("--framepadding %d:NDIGITS", &ot.frame_padding)
      .help("Frame number padding digits (ignored when using printf-style wildcards)");
    ap.arg("--views %s:VIEWNAMES")
      .help("Views for %V/%v wildcards (comma-separated, defaults to \"left,right\")");
    ap.arg("--skip-bad-frames", &ot.skip_bad_frames)
      .help("Skip to next frame in range if there's an error, rather than exiting");
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
    ap.arg("--no-autopremult")
      .help("Turn off automatic premultiplication of images with unassociated alpha")
      .action(unset_autopremult);
    ap.arg("--autopremult")
      .help("Turn on automatic premultiplication of images with unassociated alpha")
      .action(set_autopremult);
    ap.arg("--autoorient", &ot.autoorient)
      .help("Automatically --reorient all images upon input");
    ap.arg("--auto-orient", &ot.autoorient)
      .hidden(); // synonym for --autoorient
    ap.arg("--autocc")
      .help("Automatically color convert based on filename (options: unpremult=)")
      .action(set_autocc);
    ap.arg("--noautocc %!", &ot.autocc)
      .help("Turn off automatic color conversion");
    ap.arg("--native")
      .help("Keep native pixel data type (bypass cache if necessary)")
      .action(set_native);
    ap.arg("--cache %d:MB")
      .help("ImageCache size (in MB: default=4096)")
      .action(set_cachesize);
    ap.arg("--autotile %d:TILESIZE")
      .help("Autotile enable for cached images (the argument is the tile size, default 0 means no autotile)")
      .action(set_autotile);
    ap.arg("--metamerge", &ot.metamerge)
      .help("Always merge metadata of all inputs into output");
    ap.arg("--crash")
      .hidden()
      .action(crash_me);
    ap.separator("Commands that read images:");
    ap.arg("-i %s:FILENAME")
      .help("Input file (options: autocc=, ch=, info=, infoformat=, now=, type=, unpremult=)")
      .action(input_file);
    ap.arg("--iconfig %s:NAME %s:VALUE")
      .help("Sets input config attribute (options: type=...)")
      .action(set_input_attribute);
    ap.arg("--missingfile %s:OPTION", &ot.missingfile_policy)
      .help("Set policy for missing input files: 'error' (default), 'black', 'checker'");
    ap.separator("Commands that write images:");
    ap.arg("-o %s:FILENAME")
      .help("Output the current image to the named file (options: "
            "all=, autocc=, autocrop=, autotrim=, bits=, contig=, datatype=, "
            "dither=, fileformatname=, scanline=, separate=, tile=, unpremult=)")
      .action(output_file);
    ap.arg("-otex %s:FILENAME")
      .help("Output the current image as a texture")
      .action(output_file);
    ap.arg("-oenv %s:FILENAME")
      .help("Output the current image as a latlong env map")
      .action(output_file);
    ap.arg("-obump %s:FILENAME")
      .help("Output the current bump texture map as a 6 channels texture including the first and second moment of the bump slopes (options: bumpformat=height|normal|auto, uvslopes_scale=val>=0)")
      .action(output_file);
    ap.separator("Options that affect subsequent image output:");
    ap.arg("-d %s:TYPE")
      .help("'-d TYPE' sets the output data format of all channels, "
            "'-d CHAN=TYPE' overrides a single named channel (multiple -d args are allowed). "
            "Data types include: uint8, sint8, uint10, uint12, uint16, sint16, uint32, sint32, half, float, double")
      .action(set_dataformat);
    ap.arg("--scanline", &ot.output_scanline)
      .help("Output scanline images");
    ap.arg("--tile %d:WIDTH %d:HEIGHT", &ot.output_tilewidth, &ot.output_tileheight)
      .help("Output tiled images with this tile size")
      .action(output_tiles);
    ap.arg("--force-tiles", &ot.output_force_tiles)
      .hidden(); // undocumented
    ap.arg("--compression %s:NAME", &ot.output_compression)
      .help("Set the compression method (in the form \"name\" or \"name:quality\")");
    ap.arg("--quality %d:QUALITY", &ot.output_quality)
      .hidden(); // DEPRECATED(2.1)
    ap.arg("--dither", &ot.output_dither)
      .help("Add dither to 8-bit output");
    ap.arg("--planarconfig %s:CONFIG", &ot.output_planarconfig)
      .help("Force planarconfig (contig, separate, default)");
    ap.arg("--adjust-time", &ot.output_adjust_time)
      .help("Adjust file times to match DateTime metadata");
    ap.arg("--noautocrop %!", &ot.output_autocrop)
      .help("Do not automatically crop images whose formats don't support separate pixel data and full/display windows");
    ap.arg("--autotrim", &ot.output_autotrim)
      .help("Automatically trim black borders upon output to file formats that support separate pixel data and full/display windows");
    ap.separator("Options that change current image metadata (but not pixel values):");
    ap.arg("--attrib %s:NAME %s:VALUE")
      .help("Sets metadata attribute (options: type=...)")
      .action(set_any_attribute);
    ap.arg("--sattrib %s:NAME %s:VALUE")
      .help("Sets string metadata attribute")
      .action(set_string_attribute);
    ap.arg("--eraseattrib %s:REGEX")
      .help("Erase attributes matching regex")
      .action(erase_attribute);
    ap.arg("--caption %s:TEXT")
      .help("Sets caption (ImageDescription metadata)")
      .action(set_caption);
    ap.arg("--keyword %s:KEYWORD")
      .help("Add a keyword")
      .action(set_keyword);
    ap.arg("--clear-keywords")
      .help("Clear all keywords")
      .action(clear_keywords);
    ap.arg("--nosoftwareattrib", &ot.metadata_nosoftwareattrib)
      .help("Do not write command line into Exif:ImageHistory, Software metadata attributes");
    ap.arg("--sansattrib", &sansattrib)
      .help("Write command line into Software & ImageHistory but remove --sattrib and --attrib options");
    ap.arg("--orientation %d:ORIENT")
      .help("Set the assumed orientation")
      .action(set_orientation);
    ap.arg("--orientcw")
      .help("Rotate orientation metadata 90 deg clockwise")
      .action(rotate_orientation);
    ap.arg("--orientccw")
      .help("Rotate orientation metadata 90 deg counter-clockwise")
      .action(rotate_orientation);
    ap.arg("--orient180")
      .help("Rotate orientation metadata 180 deg")
      .action(rotate_orientation);
    ap.arg("--rotcw")
      .hidden() // DEPRECATED(1.5), back compatibility
      .action(rotate_orientation);
    ap.arg("--rotccw")
      .hidden() // DEPRECATED(1.5), back compatibility
      .action(rotate_orientation);
    ap.arg("--rot180")
      .hidden() // DEPRECATED(1.5), back compatibility
      .action(rotate_orientation);
    ap.arg("--origin %s:+X+Y")
      .help("Set the pixel data window origin (e.g. +20+10, -16-16)")
      .action(set_origin);
    ap.arg("--originoffset %s:+X+Y")
      .help("Offset the pixel data window origin from its current position (e.g. +20+10, -16-16)")
      .action(offset_origin);
    ap.arg("--fullsize %s:GEOM")
      .help("Set the display window (e.g., 1920x1080, 1024x768+100+0, -20-30)")
      .action(set_fullsize);
    ap.arg("--fullpixels")
      .help("Set the 'full' image range to be the pixel data window")
      .action(set_full_to_pixels);
    ap.arg("--chnames %s:NAMELIST")
      .help("Set the channel names (comma-separated)")
      .action(action_set_channelnames);
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
      .action( action_create);
    ap.arg("--pattern %s:NAME %s:GEOM %d:NCHANS")
      .help("Create a patterned image. Pattern name choices: black, constant, fill, checker, noise")
      .action( action_pattern);
    ap.arg("--kernel %s:NAME %s:GEOM")
      .help("Create a centered convolution kernel")
      .action( action_kernel);
    ap.arg("--capture")
          .help("Capture an image (options: camera=%d)")
      .action(action_capture);
    ap.arg("--diff")
      .help("Print report on the difference of two images (modified by --fail, --failpercent, --hardfail, --warn, --warnpercent --hardwarn)")
      .action(action_diff);
    ap.arg("--pdiff")
      .help("Print report on the perceptual difference of two images (modified by --fail, --failpercent, --hardfail, --warn, --warnpercent --hardwarn)")
      .action(action_pdiff);
    ap.arg("--add")
      .help("Add two images")
      .action(action_add);
    ap.arg("--addc %s:VAL")
      .help("Add to all channels a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_addc);
    ap.arg("--cadd %s:VAL")
      .hidden() // Deprecated synonym
      .action(action_addc);
    ap.arg("--sub")
      .help("Subtract two images")
      .action(action_sub);
    ap.arg("--subc %s:VAL")
      .help("Subtract from all channels a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_subc);
    ap.arg("--csub %s:VAL")
      .hidden() // Deprecated synonym
      .action(action_subc);
    ap.arg("--mul")
      .help("Multiply two images")
      .action(action_mul);
    ap.arg("--mulc %s:VAL")
      .help("Multiply the image values by a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_mulc);
    ap.arg("--cmul %s:VAL")
      .hidden() // Deprecated synonym
      .action(action_mulc);
    ap.arg("--div")
      .help("Divide first image by second image")
      .action(action_div);
    ap.arg("--divc %s:VAL")
      .help("Divide the image values by a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_divc);
    ap.arg("--mad")
      .help("Multiply two images, add a third")
      .action(action_mad);
    ap.arg("--invert")
      .help("Take the color inverse (subtract from 1)")
      .action(action_invert);
    ap.arg("--abs")
      .help("Take the absolute value of the image pixels")
      .action(action_abs);
    ap.arg("--absdiff")
      .help("Absolute difference between two images")
      .action(action_absdiff);
    ap.arg("--absdiffc %s:VAL")
      .help("Absolute difference versus a scalar or per-channel constant (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_absdiffc);
    ap.arg("--powc %s:VAL")
      .help("Raise the image values to a scalar or per-channel power (e.g.: 2.2 or 2.2,2.2,2.2,1.0)")
      .action(action_powc);
    ap.arg("--cpow %s:VAL")
      .hidden() // Deprecated synonym
      .action(action_powc);
    ap.arg("--noise")
      .help("Add noise to an image (options: type=gaussian:mean=0:stddev=0.1, type=uniform:min=0:max=0.1, type=salt:value=0:portion=0.1, seed=0")
      .action(action_noise);
    ap.arg("--chsum")
      .help("Turn into 1-channel image by summing channels (options: weight=r,g,...)")
      .action(action_chsum);
    ap.arg("--colormap %s:MAPNAME")
      .help("Color map based on channel 0 (arg: \"inferno\", \"viridis\", \"magma\", \"turbo\", \"plasma\", \"blue-red\", \"spectrum\", \"heat\", or comma-separated list of RGB triples)")
      .action(action_colormap);
    ap.arg("--crop %s:GEOM")
      .help("Set pixel data resolution and offset, cropping or padding if necessary (WxH+X+Y or xmin,ymin,xmax,ymax)")
      .action(action_crop);
    ap.arg("--croptofull")
      .help("Crop or pad to make pixel data region match the \"full\" region")
      .action(action_croptofull);
    ap.arg("--trim")
      .help("Crop to the minimal ROI containing nonzero pixel values")
      .action(action_trim);
    ap.arg("--cut %s:GEOM")
      .help("Cut out the ROI and reposition to the origin (WxH+X+Y or xmin,ymin,xmax,ymax)")
      .action(action_cut);
    ap.arg("--paste %s:+X+Y")
      .help("Paste fg over bg at the given position (e.g., +100+50; '-' or 'auto' indicates using the data window position as-is; options: all=%d, mergeroi=%d)")
      .action(action_paste);
    ap.arg("--pastemeta")
      .help("Copy the metadata from the first image to the second image and write the combined result.")
      .action(action_pastemeta);
    ap.arg("--mosaic %s:WxH")
      .help("Assemble images into a mosaic (arg: WxH; options: pad=0, fit=WxH)")
      .action(action_mosaic);
    ap.arg("--over")
      .help("'Over' composite of two images")
      .action(action_over);
    ap.arg("--zover")
      .help("Depth composite two images with Z channels (options: zeroisinf=%d)")
      .action(action_zover);
    ap.arg("--deepmerge")
      .help("Merge/composite two deep images")
      .action(action_deepmerge);
    ap.arg("--deepholdout")
      .help("Hold out one deep image by another")
      .action(action_deepholdout);
    ap.arg("--rotate90")
      .help("Rotate the image 90 degrees clockwise")
      .action(action_rotate90);
    ap.arg("--rotate180")
      .help("Rotate the image 180 degrees")
      .action(action_rotate180);
    ap.arg("--flipflop")
      .hidden() // Deprecated synonym for --rotate180
      .action(action_rotate180);
    ap.arg("--rotate270")
      .help("Rotate the image 270 degrees clockwise (or 90 degrees CCW)")
      .action(action_rotate270);
    ap.arg("--flip")
      .help("Flip the image vertically (top<->bottom)")
      .action(action_flip);
    ap.arg("--flop")
      .help("Flop the image horizontally (left<->right)")
      .action(action_flop);
    ap.arg("--reorient")
      .help("Rotate and/or flop the image to transform the pixels to match the Orientation metadata")
      .action(action_reorient);
    ap.arg("--transpose")
      .help("Transpose the image")
      .action(action_transpose);
    ap.arg("--cshift %s:+X+Y")
      .help("Circular shift the image (e.g.: +20-10)")
      .action(action_cshift);
    ap.arg("--resample %s:GEOM")
      .help("Resample (640x480, 50%) (options: interp=0)")
      .action(action_resample);
    ap.arg("--resize %s:GEOM")
      .help("Resize (640x480, 50%) (options: filter=%s)")
      .action(action_resize);
    ap.arg("--fit %s:GEOM")
      .help("Resize to fit within a window size (options: filter=%s, pad=%d, exact=%d)")
      .action(action_fit);
    ap.arg("--pixelaspect %g:ASPECT")
      .help("Scale up the image's width or height to match the given pixel aspect ratio (options: filter=%s)")
      .action(action_pixelaspect);
    ap.arg("--rotate %g:DEGREES")
      .help("Rotate pixels (degrees clockwise) around the center of the display window (options: filter=%s, center=%f,%f, recompute_roi=%d")
      .action(action_rotate);
    ap.arg("--warp %s:MATRIX")
      .help("Warp pixels (argument is a 3x3 matrix, separated by commas) (options: filter=%s, recompute_roi=%d)")
      .action(action_warp);
    ap.arg("--convolve")
      .help("Convolve with a kernel")
      .action(action_convolve);
    ap.arg("--blur %s:WxH")
      .help("Blur the image (options: kernel=name)")
      .action(action_blur);
    ap.arg("--median %s:WxH")
      .help("Median filter the image")
      .action(action_median);
    ap.arg("--dilate %s:WxH")
      .help("Dilate (area maximum) the image")
      .action(action_dilate);
    ap.arg("--erode %s:WxH")
      .help("Erode (area minimum) the image")
      .action(action_erode);
    ap.arg("--unsharp")
      .help("Unsharp mask (options: kernel=gaussian, width=3, contrast=1, threshold=0)")
      .action(action_unsharp);
    ap.arg("--laplacian")
      .help("Laplacian filter the image")
      .action(action_laplacian);
    ap.arg("--fft")
      .help("Take the FFT of the image")
      .action(action_fft);
    ap.arg("--ifft")
      .help("Take the inverse FFT of the image")
      .action(action_ifft);
    ap.arg("--polar")
      .help("Convert complex (real,imag) to polar (amplitude,phase)")
      .action(action_polar);
    ap.arg("--unpolar")
      .help("Convert polar (amplitude,phase) to complex (real,imag)")
      .action(action_unpolar);
    ap.arg("--fixnan %s:STRATEGY")
      .help("Fix NaN/Inf values in the image (choices: none, black, box3, error)")
      .action(action_fixnan);
    ap.arg("--fillholes")
      .help("Fill in holes (where alpha is not 1)")
      .action(action_fillholes);
    ap.arg("--max")
      .help("Pixel-by-pixel max of two images")
      .action(action_max);
    ap.arg("--maxc %s:VAL")
      .help("Max all values with a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_maxc);
    ap.arg("--min")
      .help("Pixel-by-pixel min of two images")
      .action(action_min);
    ap.arg("--minc %s:VAL")
      .help("Min all values with a scalar or per-channel constants (e.g.: 0.5 or 1,1.25,0.5)")
      .action(action_minc);
    ap.arg("--clamp")
      .help("Clamp values (options: min=..., max=..., clampalpha=0)")
      .action(action_clamp);
    ap.arg("--contrast")
      .help("Remap values (options: black=0..., white=1..., sthresh=0.5..., scontrast=1.0..., gamma=1, clamp=0|1)")
      .action(action_contrast);
    ap.arg("--rangecompress")
      .help("Compress the range of pixel values with a log scale (options: luma=0|1)")
      .action(action_rangecompress);
    ap.arg("--rangeexpand")
      .help("Un-rangecompress pixel values back to a linear scale (options: luma=0|1)")
      .action(action_rangeexpand);
    ap.arg("--line %s:X1,Y1,X2,Y2,...")
      .help("Render a poly-line (options: color=)")
      .action(action_line);
    ap.arg("--box %s:X1,Y1,X2,Y2")
      .help("Render a box (options: color=)")
      .action(action_box);
    ap.arg("--fill %s:GEOM")
      .help("Fill a region (options: color=)")
      .action(action_fill);
    ap.arg("--text %s:TEXT")
      .help("Render text into the current image (options: x=, y=, size=, color=)")
      .action(action_text);
    ap.separator("Manipulating channels or subimages:");
    ap.arg("--ch %s:CHANLIST")
      .help("Select or shuffle channels (e.g., \"R,G,B\", \"B,G,R\", \"2,3,4\")")
      .action(action_channels);
    ap.arg("--chappend")
      .help("Append the channels of the last two images")
      .action(action_chappend);
    ap.arg("--unmip")
      .help("Discard all but the top level of a MIPmap")
      .action(action_unmip);
    ap.arg("--selectmip %d:MIPLEVEL")
      .help("Select just one MIP level (0 = highest res)")
      .action(action_selectmip);
    ap.arg("--subimage %s:SUBIMAGEINDEX")
      .help("Select just one subimage by index or name (options: delete=1)")
      .action(action_select_subimage);
    ap.arg("--sisplit")
      .help("Split the top image's subimges into separate images")
      .action(action_subimage_split);
    ap.arg("--siappend")
      .help("Append the last two images into one multi-subimage image")
      .action(action_subimage_append);
    ap.arg("--siappendall")
      .help("Append all images on the stack into a single multi-subimage image")
      .action(action_subimage_append_all);
    ap.arg("--deepen")
      .help("Deepen normal 2D image to deep")
      .action(action_deepen);
    ap.arg("--flatten")
      .help("Flatten deep image to non-deep")
      .action(action_flatten);
    ap.separator("Image stack manipulation:");
    ap.arg("--dup")
      .help("Duplicate the current image (push a copy onto the stack)")
      .action(action_dup);
    ap.arg("--swap")
      .help("Swap the top two images on the stack.")
      .action(action_swap);
    ap.arg("--pop")
      .help("Throw away the current image")
      .action(action_pop);
    ap.arg("--label %s")
      .help("Label the top image")
      .action(action_label);
    ap.separator("Color management:");
    ap.arg("--colorconfig %s:FILENAME")
      .help("Explicitly specify an OCIO configuration file")
      .action(set_colorconfig);
    ap.arg("--iscolorspace %s:COLORSPACE")
      .help("Set the assumed color space (without altering pixels)")
      .action(set_colorspace);
    ap.arg("--tocolorspace %s:COLORSPACE")
      .help("Convert the current image's pixels to a named color space")
      .action(action_tocolorspace);
    ap.arg("--colorconvert %s:SRC %s:DST")
      .help("Convert pixels from 'src' to 'dst' color space (options: key=, value=, unpremult=, strict=)")
      .action(action_colorconvert);
    ap.arg("--ccmatrix %s:MATRIXVALS")
      .help("Color convert pixels with a 3x3 or 4x4 matrix (options: unpremult=,transpose=)")
      .action(action_ccmatrix);
    ap.arg("--ociolook %s:LOOK")
      .help("Apply the named OCIO look (options: from=, to=, inverse=, key=, value=, unpremult=)")
      .action(action_ociolook);
    ap.arg("--ociodisplay %s:DISPLAY %s:VIEW")
      .help("Apply the named OCIO display and view (options: from=, looks=, key=, value=, unpremult=)")
      .action(action_ociodisplay);
    ap.arg("--ociofiletransform %s:FILENAME")
      .help("Apply the named OCIO filetransform (options: inverse=, unpremult=)")
      .action(action_ociofiletransform);
    ap.arg("--unpremult")
      .help("Divide all color channels of the current image by the alpha to \"un-premultiply\"")
      .action(action_unpremult);
    ap.arg("--premult")
      .help("Multiply all color channels of the current image by the alpha")
      .action(action_premult);
    // clang-format on

    if (ap.parse_args(argc, (const char**)argv) < 0) {
        std::cerr << ap.geterror() << std::endl;
        print_help(ap);
        // Repeat the command line, so if oiiotool is being called from a
        // script, it's easy to debug how the command was mangled.
        std::cerr << "\nFull command line was:\n> " << ot.full_command_line
                  << "\n";
        ap.abort();
        ot.return_value = EXIT_FAILURE;
        // exit(EXIT_FAILURE);
    }
    if (help || ap["help"].get<int>()) {
        print_help(ap);
        ap.abort();
        // exit(EXIT_SUCCESS);
    }
    if (argc <= 1) {
        ap.briefusage();
        std::cout << "\nFor detailed help: oiiotool --help\n";
        ap.abort();
        // exit(EXIT_SUCCESS);
    }
}



// Check if any of the command line arguments contains numeric ranges or
// wildcards.  If not, just return 'false'.  But if they do, the
// remainder of processing will happen here (and return 'true').
static bool
handle_sequence(int argc, const char** argv)
{
    // First, scan the original command line arguments for '#', '@', '%0Nd',
    // '%v' or '%V' characters.  Any found indicate that there are numeric
    // range or wildcards to deal with.  Also look for --frames,
    // --framepadding and --views options.
#define ONERANGE_SPEC "-?[0-9]+(--?[0-9]+((x|y)-?[0-9]+)?)?"
#define MANYRANGE_SPEC ONERANGE_SPEC "(," ONERANGE_SPEC ")*"
#define VIEW_SPEC "%[Vv]"
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
    bool is_sequence = false;
    bool wildcard_on = true;
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
        } else if (strarg == "--wildcardoff" || strarg == "-wildcardoff") {
            wildcard_on = false;
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
            ot.errorf("", "Could not parse pattern: %s", argv[a]);
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
                ot.errorf(
                    "",
                    "No filenames found matching pattern: \"%s\" (did you intend to use --wildcardoff?)",
                    argv[a]);
                return true;
            }
        }

        if (i == 0) {
            nfilenames = filenames[a].size();
        } else if (nfilenames != filenames[a].size()) {
            ot.errorf(
                "",
                "Not all sequence specifications matched: %s (%d frames) vs. %s (%d frames)",
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

    // Make sure frame_numbers[0] has the canonical frame number list
    if (sequence_args.size() && frame_numbers[0].empty())
        frame_numbers[0] = frame_numbers[sequence_args[0]];

    // OK, now we just call getargs once for each item in the sequences,
    // substituting the i-th sequence entry for its respective argument
    // every time.
    // Note: nfilenames really means, number of frame number iterations.
    std::vector<const char*> seq_argv(argv, argv + argc + 1);
    for (size_t i = 0; i < nfilenames; ++i) {
        if (ot.debug)
            std::cout << "SEQUENCE " << i << "\n";
        for (size_t a : sequence_args) {
            seq_argv[a] = filenames[a][i].c_str();
            if (ot.debug)
                std::cout << "  " << argv[a] << " -> " << seq_argv[a] << "\n";
        }

        ot.clear_options();  // Careful to reset all command line options!
        ot.frame_number = frame_numbers[0][i];
        getargs(argc, (char**)&seq_argv[0]);

        if (ap.aborted()) {
            if (!ot.skip_bad_frames)
                break;
            else
                ap.abort(false);
        } else {
            ot.process_pending();
            if (ot.pending_callback())
                ot.warning(ot.pending_callback_name(),
                           "pending command never executed");
        }

        // Clear the stack at the end of each iteration
        ot.curimg.reset();
        ot.image_stack.clear();

        if (ot.runstats)
            std::cout << "End iteration " << i << ": "
                      << Strutil::timeintervalformat(ot.total_runtime(), 2)
                      << "  " << Strutil::memformat(Sysutil::memory_used())
                      << "\n";
        if (ot.debug)
            std::cout << "\n";
    }

    return true;
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
    // And, oy, just to mess with us, this doesn't work in gcc 4.8.
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
            Strutil::fprintf(stderr,
                             "Bad half conversion, code %d %f -> %f "
                             "(suspect badly set DENORMS_ZERO_MODE)\n",
                             bad[0], h[0], vf[0]);
    }

    // Helpful for debugging to make sure that any crashes dump a stack
    // trace.
    Sysutil::setup_crash_stacktrace("stdout");

    // Globally force classic "C" locale, and turn off all formatting
    // internationalization, for the entire oiiotool application.
    std::locale::global(std::locale::classic());

    ot.imagecache = ImageCache::create();
    OIIO_DASSERT(ot.imagecache);
    ot.imagecache->attribute("forcefloat", 1);
    ot.imagecache->attribute("max_memory_MB", float(ot.cachesize));
    ot.imagecache->attribute("autotile", ot.autotile);
    ot.imagecache->attribute("autoscanline", int(ot.autotile ? 1 : 0));

    Filesystem::convert_native_arguments(argc, (const char**)argv);
    if (handle_sequence(argc, (const char**)argv)) {
        // Deal with sequence

    } else {
        // Not a sequence
        getargs(argc, argv);
        if (!ap.aborted()) {
            ot.process_pending();
            if (ot.pending_callback())
                ot.warning(ot.pending_callback_name(),
                           "pending command never executed");
        }
    }

    if (!ot.printinfo && !ot.printstats && !ot.dumpdata && !ot.dryrun
        && !ot.printed_info && !ap.aborted()) {
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
        std::cout << "\n";
        int threads = -1;
        OIIO::getattribute("threads", threads);
        std::cout << "Threads: " << threads << "\n";
        std::cout << "oiiotool runtime statistics:\n";
        std::cout << "  Total time: "
                  << Strutil::timeintervalformat(total_time, 2) << "\n";
        static const char* timeformat = "      %-12s : %5.2f\n";
        for (Oiiotool::TimingMap::const_iterator func
             = ot.function_times.begin();
             func != ot.function_times.end(); ++func) {
            double t = func->second;
            Strutil::printf(timeformat, func->first, t);
            unaccounted -= t;
        }
        Strutil::printf(timeformat, "unaccounted", std::max(unaccounted, 0.0));
        ot.check_peak_memory();
        std::cout << "  Peak memory:    " << Strutil::memformat(ot.peak_memory)
                  << "\n";
        std::cout << "  Current memory: "
                  << Strutil::memformat(Sysutil::memory_used()) << "\n";
        std::cout << "\n" << ot.imagecache->getstats(2) << "\n";
    }

    // Force all files to close, ugh, it's the only way I can find to solve
    // an occasional problem with static destructor order fiasco with
    // field3d when building with EMBEDPLUGINS=0 on MacOS.
    ot.imagecache->close_all();

    return ot.return_value;
}
