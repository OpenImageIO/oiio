// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagecache.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/optparser.h>
#include <OpenImageIO/simd.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/texture.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/typedesc.h>
#include <OpenImageIO/ustring.h>
#include <OpenImageIO/varyingref.h>

#include "imagecache_pvt.h"
#include "imageio_pvt.h"


OIIO_NAMESPACE_BEGIN
using namespace pvt;

namespace pvt {

// The static perthread mutex needs to outlive the shared_image_cache
// instance, so must be declared first in this file to avoid static
// initialization order problems.
spin_mutex ImageCacheImpl::m_perthread_info_mutex;

}  // namespace pvt

namespace {  // anonymous

static std::shared_ptr<ImageCacheImpl> shared_image_cache;
static spin_mutex shared_image_cache_mutex;

// Make some static ustring constants to avoid strcmp's
static ustring s_resolution("resolution"), s_texturetype("texturetype");
static ustring s_textureformat("textureformat"), s_fileformat("fileformat");
static ustring s_format("format"), s_cachedformat("cachedformat");
static ustring s_channels("channels"), s_cachedpixeltype("cachedpixeltype");
static ustring s_exists("exists"), s_broken("broken");
static ustring s_UDIM("UDIM");
static ustring s_subimages("subimages"), s_miplevels("miplevels");
static ustring s_datawindow("datawindow"), s_displaywindow("displaywindow");
static ustring s_averagecolor("averagecolor"), s_averagealpha("averagealpha");
static ustring s_constantcolor("constantcolor");
static ustring s_constantalpha("constantalpha");


// Functor to compare filenames
static bool
filename_compare(const ImageCacheFileRef& a, const ImageCacheFileRef& b)
{
    return a->filename() < b->filename();
}


// Functor to compare read bytes, sort in descending order
static bool
bytesread_compare(const ImageCacheFileRef& a, const ImageCacheFileRef& b)
{
    return a->bytesread() > b->bytesread();
}


// Functor to compare read times, sort in descending order
static bool
iotime_compare(const ImageCacheFileRef& a, const ImageCacheFileRef& b)
{
    return a->iotime() > b->iotime();
}


// Functor to compare read rate (MB/s), sort in ascending order
static bool
iorate_compare(const ImageCacheFileRef& a, const ImageCacheFileRef& b)
{
    double arate = (a->iotime() == 0)
                       ? 0
                       : a->bytesread() / (1024.0 * 1024.0) / a->iotime();
    double brate = (b->iotime() == 0)
                       ? 0
                       : b->bytesread() / (1024.0 * 1024.0) / b->iotime();
    return arate < brate;
}


// Functor to compare amount of redundant reading, sort in descending order
static bool
redundantbytes_compare(const ImageCacheFileRef& a, const ImageCacheFileRef& b)
{
    return a->redundant_bytesread() > b->redundant_bytesread();
}


};  // end anonymous namespace


namespace pvt {  // namespace pvt



void
ImageCacheStatistics::init()
{
    // ImageCache stats:
    find_tile_calls             = 0;
    find_tile_microcache_misses = 0;
    find_tile_cache_misses      = 0;
    //    tiles_created = 0;
    //    tiles_current = 0;
    //    tiles_peak = 0;
    files_totalsize        = 0;
    files_totalsize_ondisk = 0;
    bytes_read             = 0;
    //    open_files_created = 0;
    //    open_files_current = 0;
    //    open_files_peak = 0;
    unique_files      = 0;
    fileio_time       = 0;
    fileopen_time     = 0;
    file_locking_time = 0;
    tile_locking_time = 0;
    find_file_time    = 0;
    find_tile_time    = 0;

    // TextureSystem stats:
    texture_queries     = 0;
    texture_batches     = 0;
    texture3d_queries   = 0;
    texture3d_batches   = 0;
    shadow_queries      = 0;
    shadow_batches      = 0;
    environment_queries = 0;
    environment_batches = 0;
    imageinfo_queries   = 0;
    aniso_queries       = 0;
    aniso_probes        = 0;
    max_aniso           = 1;
    closest_interps     = 0;
    bilinear_interps    = 0;
    cubic_interps       = 0;
    file_retry_success  = 0;
    tile_retry_success  = 0;
}



void
ImageCacheStatistics::merge(const ImageCacheStatistics& s)
{
    // ImageCache stats:
    find_tile_calls += s.find_tile_calls;
    find_tile_microcache_misses += s.find_tile_microcache_misses;
    find_tile_cache_misses += s.find_tile_cache_misses;
    //    tiles_created += s.tiles_created;
    //    tiles_current += s.tiles_current;
    //    tiles_peak += s.tiles_peak;
    files_totalsize += s.files_totalsize;
    files_totalsize_ondisk += s.files_totalsize_ondisk;
    bytes_read += s.bytes_read;
    //    open_files_created += s.open_files_created;
    //    open_files_current += s.open_files_current;
    //    open_files_peak += s.open_files_peak;
    unique_files += s.unique_files;
    fileio_time += s.fileio_time;
    fileopen_time += s.fileopen_time;
    file_locking_time += s.file_locking_time;
    tile_locking_time += s.tile_locking_time;
    find_file_time += s.find_file_time;
    find_tile_time += s.find_tile_time;

    // TextureSystem stats:
    texture_queries += s.texture_queries;
    texture_batches += s.texture_batches;
    texture3d_queries += s.texture3d_queries;
    texture3d_batches += s.texture3d_batches;
    shadow_queries += s.shadow_queries;
    shadow_batches += s.shadow_batches;
    environment_queries += s.environment_queries;
    environment_batches += s.environment_batches;
    imageinfo_queries += s.imageinfo_queries;
    aniso_queries += s.aniso_queries;
    aniso_probes += s.aniso_probes;
    max_aniso = std::max(max_aniso, s.max_aniso);
    closest_interps += s.closest_interps;
    bilinear_interps += s.bilinear_interps;
    cubic_interps += s.cubic_interps;
    file_retry_success += s.file_retry_success;
    tile_retry_success += s.tile_retry_success;
}



ImageCacheFile::LevelInfo::LevelInfo(const ImageSpec& spec_,
                                     const ImageSpec& nativespec_)
    : spec(spec_)
    , nativespec(nativespec_)
{
    full_pixel_range
        = (spec.x == spec.full_x && spec.y == spec.full_y
           && spec.z == spec.full_z && spec.width == spec.full_width
           && spec.height == spec.full_height && spec.depth == spec.full_depth);
    onetile = (spec.width <= spec.tile_width && spec.height <= spec.tile_height
               && spec.depth <= spec.tile_depth);
    polecolorcomputed = false;

    // Allocate bit field for which tiles have been read at least once.
    if (onetile) {
        nxtiles = 1;
        nytiles = 1;
        nztiles = 1;
    } else {
        nxtiles = (spec.width + spec.tile_width - 1) / spec.tile_width;
        nytiles = (spec.height + spec.tile_height - 1) / spec.tile_height;
        nztiles = (spec.depth + spec.tile_depth - 1) / spec.tile_depth;
    }
    int total_tiles = nxtiles * nytiles * nztiles;
    OIIO_DASSERT(total_tiles >= 1);
    const int sz = round_to_multiple(total_tiles, 64) / 64;
    tiles_read   = new atomic_ll[sz];
    for (int i = 0; i < sz; i++)
        tiles_read[i] = 0;
}



ImageCacheFile::LevelInfo::LevelInfo(const LevelInfo& src)
    : spec(src.spec)
    , nativespec(src.nativespec)
    , full_pixel_range(src.full_pixel_range)
    , onetile(src.onetile)
    , polecolorcomputed(src.polecolorcomputed)
    , polecolor(src.polecolor)
    , nxtiles(src.nxtiles)
    , nytiles(src.nytiles)
    , nztiles(src.nztiles)
{
    int nwords = round_to_multiple(nxtiles * nytiles * nztiles, 64) / 64;
    tiles_read = new atomic_ll[nwords];
    for (int i = 0; i < nwords; ++i)
        tiles_read[i] = src.tiles_read[i].load();
}



ImageCacheFile::ImageCacheFile(ImageCacheImpl& imagecache,
                               ImageCachePerThreadInfo* /*thread_info*/,
                               ustring filename, ImageInput::Creator creator,
                               const ImageSpec* config)
    : m_filename(filename)
    , m_used(true)
    , m_broken(false)
    , m_texformat(TexFormatTexture)
    , m_swrap(TextureOpt::WrapBlack)
    , m_twrap(TextureOpt::WrapBlack)
    , m_rwrap(TextureOpt::WrapBlack)
    , m_envlayout(LayoutTexture)
    , m_y_up(false)
    , m_sample_border(false)
    , m_is_udim(false)
    , m_tilesread(0)
    , m_bytesread(0)
    , m_redundant_tiles(0)
    , m_redundant_bytesread(0)
    , m_timesopened(0)
    , m_iotime(0)
    , m_mutex_wait_time(0)
    , m_mipused(false)
    , m_validspec(false)
    , m_errors_issued(0)
    , m_imagecache(imagecache)
    , m_duplicate(NULL)
    , m_total_imagesize(0)
    , m_total_imagesize_ondisk(0)
    , m_inputcreator(creator)
    , m_configspec(config ? new ImageSpec(*config) : NULL)
{
    m_filename_original = m_filename;
    m_filename = imagecache.resolve_filename(m_filename_original.string());
    // N.B. the file is not opened, the ImageInput is NULL.  This is
    // reflected by the fact that m_validspec is false.

    // Figure out if it's a UDIM-like virtual texture
    m_is_udim = (m_filename.find("<UDIM>") != m_filename.npos
                 || m_filename.find("<U>") != m_filename.npos
                 || m_filename.find("<V>") != m_filename.npos
                 || m_filename.find("<u>") != m_filename.npos
                 || m_filename.find("<v>") != m_filename.npos
                 || m_filename.find("_u##v##") != m_filename.npos
                 || m_filename.find("%(UDIM)d") != m_filename.npos)
                && !Filesystem::exists(m_filename);

    // If the config has an IOProxy, remember that we should never actually
    // release() it, because the proxy can't be reopened.
    if (config && config->find_attribute("oiio:ioproxy", TypeDesc::PTR))
        m_allow_release = false;
}



ImageCacheFile::~ImageCacheFile() { close(); }



void
ImageCacheFile::reset(ImageInput::Creator creator, const ImageSpec* config)
{
    m_inputcreator = creator;
    m_configspec.reset(config ? new ImageSpec(*config) : nullptr);
}



std::shared_ptr<ImageInput>
ImageCacheFile::get_imageinput(ImageCachePerThreadInfo* /*thread_info*/)
{
#if defined(__GLIBCXX__) && __GLIBCXX__ < 20160822
    // Older gcc libstdc++ does not properly support std::atomic
    // operations on std::shared_ptr, despite it being a C++11
    // feature. No choice but to lock.
    recursive_lock_guard guard(m_input_mutex);
    return m_input;
#else
    // True C++11: can atomically load a shared_ptr safely.
    return std::atomic_load(&m_input);
#endif
}



void
ImageCacheFile::set_imageinput(std::shared_ptr<ImageInput> newval)
{
    if (newval)
        imagecache().incr_open_files();
#if defined(__GLIBCXX__) && __GLIBCXX__ < 20160822
    // Older gcc libstdc++ does not properly support std::atomic
    // operations on std::shared_ptr, despite it being a C++11
    // feature. No choice but to lock.
    std::shared_ptr<ImageInput> oldval;
    {
        recursive_lock_guard guard(m_input_mutex);
        oldval  = m_input;
        m_input = newval;
    }
#else
    // True C++11: can atomically exchange a shared_ptr safely.
    auto oldval = std::atomic_exchange(&m_input, newval);
#endif
    if (oldval)
        imagecache().decr_open_files();
}



void
ImageCacheFile::SubimageInfo::init(ImageCacheFile& icfile,
                                   const ImageSpec& spec, bool forcefloat)
{
    volume = (spec.depth > 1 || spec.full_depth > 1);
    full_pixel_range
        = (spec.x == spec.full_x && spec.y == spec.full_y
           && spec.z == spec.full_z && spec.width == spec.full_width
           && spec.height == spec.full_height && spec.depth == spec.full_depth);
    if (!full_pixel_range) {
        sscale  = float(spec.full_width) / spec.width;
        soffset = float(spec.full_x - spec.x) / spec.width;
        tscale  = float(spec.full_height) / spec.height;
        toffset = float(spec.full_y - spec.y) / spec.height;
    } else {
        sscale = tscale = 1.0f;
        soffset = toffset = 0.0f;
    }
    subimagename = ustring(spec.get_string_attribute("oiio:subimagename"));
    datatype     = TypeDesc::FLOAT;
    if (!forcefloat) {
        // If we aren't forcing everything to be float internally, then
        // there are a few other types we allow.
        if (spec.format == TypeDesc::UINT8 || spec.format == TypeDesc::UINT16
            || spec.format == TypeDesc::HALF
            /* future expansion:  || spec.format == AnotherFormat ... */)
            datatype = spec.format;
    }
    channelsize = datatype.size();
    pixelsize   = channelsize * spec.nchannels;

    // See if there's a constant color tag
    string_view software = spec.get_string_attribute("Software");
    bool from_maketx     = Strutil::istarts_with(software, "OpenImageIO")
                       || Strutil::istarts_with(software, "maketx");

    string_view constant_color = spec.get_string_attribute(
        "oiio:ConstantColor");
    if (from_maketx && constant_color.size()) {
        while (constant_color.size()) {
            float val;
            if (!Strutil::parse_float(constant_color, val))
                break;
            average_color.push_back(val);
            if (!Strutil::parse_char(constant_color, ','))
                break;
        }
        if (average_color.size() == size_t(spec.nchannels)) {
            is_constant_image = true;
            has_average_color = true;
        }
    }

    // See if there's an average color tag
    string_view avgcolor = spec.get_string_attribute("oiio:AverageColor");
    if (from_maketx && avgcolor.size()) {
        while (avgcolor.size()) {
            float val;
            if (!Strutil::parse_float(avgcolor, val))
                break;
            average_color.push_back(val);
            if (!Strutil::parse_char(avgcolor, ','))
                break;
        }
        if (average_color.size() == size_t(spec.nchannels))
            has_average_color = true;
    }

    const ParamValue* p = spec.find_attribute("worldtolocal", TypeMatrix);
    if (p) {
        Imath::M44f c2w;
        icfile.m_imagecache.get_commontoworld(c2w);
        const Imath::M44f* m = (const Imath::M44f*)p->data();
        Mlocal.reset(new Imath::M44f(c2w * (*m)));
    }
}



std::shared_ptr<ImageInput>
ImageCacheFile::open(ImageCachePerThreadInfo* thread_info)
{
    // Simple case -- no lock needed: atomically retrieve a shared pointer
    // to the ImageInput. If it exists, return it. Unless the file is
    // marked as broken, in which case return an empty pointer.
    std::shared_ptr<ImageInput> inp = get_imageinput(thread_info);
    if (m_broken)
        return {};
    if (inp)
        return inp;

    // The file wasn't already opened and in a good state.

    // Enforce limits on maximum number of open files.
    imagecache().check_max_files(thread_info);

    // Now we do a lock and open it for real, to keep anybody else from
    // going through the whole opening process simultaneously.
    Timer input_mutex_timer;
    recursive_lock_guard guard(m_input_mutex);
    m_mutex_wait_time += input_mutex_timer();

    // JUST IN CASE somebody else opened the file we want, between when we
    // checked and when we acquired the lock, check again.
    //
    // FIXME: do we care? Is it better to block, or is it better to
    // redundantly open the file and one will get quickly closed and
    // discarded?
    inp = get_imageinput(thread_info);
    if (m_broken)
        return {};
    if (inp)
        return inp;

    ImageSpec configspec;
    if (m_configspec)
        configspec = *m_configspec;
    if (imagecache().unassociatedalpha())
        configspec.attribute("oiio:UnassociatedAlpha", 1);

    if (m_inputcreator)
        inp.reset(m_inputcreator());
    else {
        // If we are trusting extensions and this isn't a special "REST-ful"
        // name construction, just open with the extension in order to skip
        // an unnecessary file open.
        std::string fmt;
        if (m_imagecache.trust_file_extensions()
            && m_filename.find('?') != m_filename.npos)
            fmt = OIIO::Filesystem::extension(fmt, false);
        else
            fmt = m_filename.string();
        inp = ImageInput::create(fmt, false, &configspec,
                                 m_imagecache.plugin_searchpath());
    }
    if (!inp) {
        mark_broken(OIIO::geterror());
        invalidate_spec();
        return {};
    }

    ImageSpec nativespec, tempspec;
    mark_not_broken();
    bool ok = true;
    for (int tries = 0; tries <= imagecache().failure_retries(); ++tries) {
        ok = inp->open(m_filename.c_str(), nativespec, configspec);
        if (ok) {
            tempspec = nativespec;
            if (tries)  // succeeded, but only after a failure!
                ++thread_info->m_stats.file_retry_success;
            (void)inp->geterror();  // Eat the errors
            break;
        }
        if (tries < imagecache().failure_retries()) {
            // We failed, but will wait a bit and try again.
            Sysutil::usleep(1000 * 100);  // 100 ms
        }
    }
    if (!ok) {
        mark_broken(inp->geterror());
        inp.reset();
        return {};
    }
    m_fileformat = ustring(inp->format_name());
    ++m_timesopened;
    use();

    // If we are simply re-opening a closed file, and the spec is still
    // valid, we're done, no need to reread the subimage and mip headers.
    if (validspec()) {
        set_imageinput(inp);
        return inp;
    }

    // From here on, we know that we've opened this file for the very
    // first time.  So read all the subimages, fill out all the fields
    // of the ImageCacheFile.
    m_subimages.clear();
    int nsubimages = 0;

    // Since each subimage can potentially have its own mipmap levels,
    // keep track of the highest level discovered
    imagesize_t old_total_imagesize        = m_total_imagesize;
    imagesize_t old_total_imagesize_ondisk = m_total_imagesize_ondisk;
    m_total_imagesize                      = 0;
    do {
        m_subimages.resize(nsubimages + 1);
        SubimageInfo& si(subimageinfo(nsubimages));
        int max_mip_res = imagecache().max_mip_res();
        int nmip        = 0;
        do {
            tempspec = nativespec;
            if (nmip == 0) {
                // Things to do on MIP level 0, i.e. once per subimage
                si.init(*this, tempspec, imagecache().forcefloat());
            }
            if (tempspec.tile_width == 0 || tempspec.tile_height == 0) {
                si.untiled   = true;
                int autotile = imagecache().autotile();
                // Special consideration: for TIFF files, there is extra
                // efficiency from making our fake tiles correspond to the
                // strips. (N.B.: for non-TIFF files, this attribute will
                // be absent and thus default to 0.)
                int rps = tempspec["tiff:RowsPerStrip"].get<int>();
                if (rps > 1)
                    autotile = round_to_multiple(std::max(autotile, 64), rps);
                si.autotiled = (autotile != 0);
                if (autotile) {
                    // Automatically make it appear as if it's tiled
                    if (imagecache().autoscanline() || rps > 1) {
                        tempspec.tile_width = tempspec.width;
                    } else {
                        tempspec.tile_width = std::min(tempspec.width,
                                                       autotile);
                    }
                    tempspec.tile_height = std::min(tempspec.height, autotile);
                    tempspec.tile_depth  = std::min(std::max(tempspec.depth, 1),
                                                   autotile);
                } else {
                    // Don't auto-tile -- which really means, make it look like
                    // a single tile that's as big as the whole image.
                    tempspec.tile_width  = tempspec.width;
                    tempspec.tile_height = tempspec.height;
                    tempspec.tile_depth  = tempspec.depth;
                }
            }
            // If a request was made for a maximum MIP resolution to use for
            // texture lookups and this level is too big, bump up this
            // subimage's "min MIP level".
            if (tempspec.width > max_mip_res || tempspec.height > max_mip_res
                || tempspec.depth > max_mip_res)
                si.min_mip_level = nmip + 1;
            //            thread_info->m_stats.files_totalsize += tempspec.image_bytes();
            m_total_imagesize += tempspec.image_bytes();
            // All MIP levels need the same number of channels
            if (nmip > 0
                && tempspec.nchannels != spec(nsubimages, 0).nchannels) {
                // No idea what to do with a subimage that doesn't have the
                // same number of channels as the others, so just skip it.
                inp.reset();
                mark_broken(
                    "Subimages don't all have the same number of channels");
                invalidate_spec();
                return {};
            }
            // ImageCache can't store differing formats per channel
            tempspec.channelformats.clear();
            LevelInfo levelinfo(tempspec, nativespec);
            si.levels.push_back(levelinfo);
            ++nmip;
        } while (inp->seek_subimage(nsubimages, nmip, nativespec));

        // Special work for non-MIPmapped images -- but only if "automip"
        // is on, it's a non-mipmapped image, and it doesn't have a
        // "textureformat" attribute (because that would indicate somebody
        // constructed it as texture and specifically wants it un-mipmapped).
        // But not volume textures -- don't auto MIP them for now.
        if (nmip == 1 && !si.volume
            && (tempspec.width > 1 || tempspec.height > 1 || tempspec.depth > 1))
            si.unmipped = true;
        if (si.unmipped && imagecache().automip()
            && !tempspec.find_attribute("textureformat", TypeString)) {
            int w = tempspec.full_width;
            int h = tempspec.full_height;
            int d = tempspec.full_depth;
            while (w > 1 || h > 1 || d > 1) {
                w             = std::max(1, w / 2);
                h             = std::max(1, h / 2);
                d             = std::max(1, d / 2);
                ImageSpec s   = tempspec;
                s.width       = w;
                s.height      = h;
                s.depth       = d;
                s.full_width  = w;
                s.full_height = h;
                s.full_depth  = d;
                if (imagecache().autotile()) {
                    if (imagecache().autoscanline()) {
                        s.tile_width = w;
                    } else {
                        s.tile_width = std::min(imagecache().autotile(), w);
                    }
                    s.tile_height = std::min(imagecache().autotile(), h);
                    s.tile_depth  = std::min(imagecache().autotile(), d);
                } else {
                    s.tile_width  = w;
                    s.tile_height = h;
                    s.tile_depth  = d;
                }
                ++nmip;
                LevelInfo levelinfo(s, s);
                si.levels.push_back(levelinfo);
            }
        }
        if (si.untiled && !imagecache().accept_untiled()) {
            mark_broken("image was untiled");
            invalidate_spec();
            inp.reset();
            return {};
        }
        if (si.unmipped && !imagecache().accept_unmipped() &&
            // Allow unmip-mapped for null inputs (user buffers)
            strcmp(inp->format_name(), "null")) {
            mark_broken("image was not MIP-mapped");
            invalidate_spec();
            inp.reset();
            return {};
        }
        // Make sure we didn't set the subimage's mip_mip_level past the end
        si.min_mip_level = std::min(si.min_mip_level, nmip - 1);
        ++nsubimages;
    } while (inp->seek_subimage(nsubimages, 0, nativespec));
    OIIO_DASSERT((size_t)nsubimages == m_subimages.size());

    m_total_imagesize_ondisk = imagesize_t(Filesystem::file_size(m_filename));

    thread_info->m_stats.files_totalsize -= old_total_imagesize;
    thread_info->m_stats.files_totalsize += m_total_imagesize;
    thread_info->m_stats.files_totalsize_ondisk -= old_total_imagesize_ondisk;
    thread_info->m_stats.files_totalsize_ondisk += m_total_imagesize_ondisk;

    init_from_spec();  // Fill in the rest of the fields
    set_imageinput(inp);
    return inp;
}



void
ImageCacheFile::init_from_spec()
{
    ImageSpec& spec(this->spec(0, 0));
    const ParamValue* p;

    // FIXME -- this should really be per-subimage
    if (spec.depth <= 1 && spec.full_depth <= 1)
        m_texformat = TexFormatTexture;
    else
        m_texformat = TexFormatTexture3d;
    if ((p = spec.find_attribute("textureformat", TypeDesc::STRING))) {
        const char* textureformat = *(const char**)p->data();
        for (int i = 0; i < TexFormatLast; ++i)
            if (Strutil::iequals(textureformat,
                                 texture_format_name((TexFormat)i))) {
                m_texformat = (TexFormat)i;
                break;
            }
        // For textures marked as such, doctor the full_width/full_height to
        // not be non-sensical.
        if (m_texformat == TexFormatTexture) {
            for (int s = 0; s < subimages(); ++s) {
                for (int m = 0; m < miplevels(s); ++m) {
                    ImageSpec& spec(this->spec(s, m));
                    if (spec.full_width > spec.width)
                        spec.full_width = spec.width;
                    if (spec.full_height > spec.height)
                        spec.full_height = spec.height;
                    if (spec.full_depth > spec.depth)
                        spec.full_depth = spec.depth;
                }
            }
        }
    }

    if ((p = spec.find_attribute("wrapmodes", TypeDesc::STRING))) {
        const char* wrapmodes = *(const char**)p->data();
        TextureOpt::parse_wrapmodes(wrapmodes, m_swrap, m_twrap);
        m_rwrap = m_swrap;
        // FIXME(volume) -- rwrap
    }

    m_y_up          = m_imagecache.latlong_y_up_default();
    m_sample_border = false;
    if (m_texformat == TexFormatLatLongEnv
        || m_texformat == TexFormatCubeFaceEnv
        || m_texformat == TexFormatCubeFaceShadow) {
        if (spec.get_string_attribute("oiio:updirection") == "y")
            m_y_up = true;
        else if (spec.get_string_attribute("oiio:updirection") == "z")
            m_y_up = false;
        if (spec.get_int_attribute("oiio:sampleborder") != 0)
            m_sample_border = true;
    }

    if (m_texformat == TexFormatCubeFaceEnv
        || m_texformat == TexFormatCubeFaceShadow) {
        int w = std::max(spec.full_width, spec.tile_width);
        int h = std::max(spec.full_height, spec.tile_height);
        if (spec.width == 3 * w && spec.height == 2 * h)
            m_envlayout = LayoutCubeThreeByTwo;
        else if (spec.width == w && spec.height == 6 * h)
            m_envlayout = LayoutCubeOneBySix;
        else
            m_envlayout = LayoutTexture;
    }

    // Squash some problematic texture metadata if we suspect it's wrong
    pvt::check_texture_metadata_sanity(spec);

    // See if there's a SHA-1 hash in the image description
    string_view fing = spec.get_string_attribute("oiio:SHA-1");
    if (fing.length())
        m_fingerprint = ustring(fing);

    m_mod_time = Filesystem::last_write_time(m_filename);

    // Set all mipmap level read counts to zero
    int maxmip = 1;
    for (int s = 0, nsubimages = subimages(); s < nsubimages; ++s)
        maxmip = std::max(maxmip, miplevels(s));
    m_mipreadcount.clear();
    m_mipreadcount.resize(maxmip, 0);

    OIIO_DASSERT(!m_broken);
    m_validspec = true;
}



bool
ImageCacheFile::read_tile(ImageCachePerThreadInfo* thread_info, int subimage,
                          int miplevel, int x, int y, int z, int chbegin,
                          int chend, TypeDesc format, void* data)
{
    OIIO_DASSERT(chend > chbegin);

    // Mark if we ever use a mip level that's not the first
    if (miplevel > 0)
        m_mipused = true;

    // count how many times this mipmap level was read
    m_mipreadcount[miplevel]++;

    SubimageInfo& subinfo(subimageinfo(subimage));

    // Special case for un-MIP-mapped
    if (subinfo.unmipped && miplevel != 0)
        return read_unmipped(thread_info, subimage, miplevel, x, y, z, chbegin,
                             chend, format, data);

    std::shared_ptr<ImageInput> inp = open(thread_info);
    if (!inp)
        return false;

    // Special case for untiled images -- need to do tile emulation
    if (subinfo.untiled)
        return read_untiled(thread_info, inp.get(), subimage, miplevel, x, y, z,
                            chbegin, chend, format, data);

    // Ordinary tiled
    bool ok = true;
    const ImageSpec& spec(this->spec(subimage, miplevel));
    for (int tries = 0; tries <= imagecache().failure_retries(); ++tries) {
        ok = inp->read_tiles(subimage, miplevel, x, x + spec.tile_width, y,
                             y + spec.tile_height, z, z + spec.tile_depth,
                             chbegin, chend, format, data);
        if (ok) {
            if (tries)  // succeeded, but only after a failure!
                ++thread_info->m_stats.tile_retry_success;
            (void)inp->geterror();  // Eat the errors
            break;
        }
        if (tries < imagecache().failure_retries()) {
            // We failed, but will wait a bit and try again.
            Sysutil::usleep(1000 * 100);  // 100 ms
            // TODO: should we attempt to close and re-open the file?
        }
    }
    if (!ok) {
        std::string err = inp->geterror();
        if (!err.empty() && errors_should_issue())
            imagecache().error("{}", err);
    }

    if (ok) {
        size_t b = spec.tile_bytes();
        thread_info->m_stats.bytes_read += b;
        m_bytesread += b;
        ++m_tilesread;
    }
    return ok;
}



bool
ImageCacheFile::read_unmipped(ImageCachePerThreadInfo* thread_info,
                              int subimage, int miplevel, int x, int y, int z,
                              int chbegin, int chend, TypeDesc format,
                              void* data)
{
    // We need a tile from an unmipmapped file, and it doesn't really
    // exist.  So generate it out of thin air by interpolating pixels
    // from the next higher-res level.  Of course, that may also not
    // exist, but it will be generated recursively, since we call
    // imagecache->get_pixels(), and it will ask for other tiles, which
    // will again call read_unmipped... eventually it will hit a subimage 0
    // tile that actually exists.

    // N.B. No need to lock the mutex, since this is only called
    // from read_tile, which already holds the lock.

    // Figure out the size and strides for a single tile, make an ImageBuf
    // to hold it temporarily.
    const ImageSpec& spec(this->spec(subimage, miplevel));
    int tw = spec.tile_width;
    int th = spec.tile_height;
    OIIO_DASSERT(chend > chbegin);
    int nchans = chend - chbegin;
    ImageSpec lospec(tw, th, nchans, TypeDesc::FLOAT);
    ImageBuf lores(lospec);

    // Figure out the range of texels we need for this tile
    x -= spec.x;
    y -= spec.y;
    z -= spec.z;
    int x0 = x - (x % spec.tile_width);
    int x1 = std::min(x0 + spec.tile_width - 1, spec.full_width - 1);
    int y0 = y - (y % spec.tile_height);
    int y1 = std::min(y0 + spec.tile_height - 1, spec.full_height - 1);
    //    int z0 = z - (z % spec.tile_depth);
    //    int z1 = std::min (z0+spec.tile_depth-1, spec.full_depth-1);

    // Save the contents of the per-thread microcache.  This is because
    // a caller several levels up may be retaining a reference to
    // thread_info->tile and expecting it not to suddenly point to a
    // different tile id!  It's a very reasonable assumption that if you
    // ask to read the last-found tile, it will still be the last-found
    // tile after the pixels are read.  Well, except that below our call
    // to get_pixels may recursively trigger more tiles to be read, and
    // totally change the microcache.  Simple solution: save & restore it.
    ImageCacheTileRef oldtile     = thread_info->tile;
    ImageCacheTileRef oldlasttile = thread_info->lasttile;

    // Auto-mipping will totally thrash the cache if the user unwisely
    // sets it to be too small compared to the image file that needs to
    // automipped.  So we simply override bad decisions by adjusting the
    // cache size to be a minimum of twice as big as any image we automip.
    imagecache().set_min_cache_size(
        2 * (long long)this->spec(subimage, 0).image_bytes());

    // Texel by texel, generate the values by interpolating filtered
    // lookups form the next finer subimage.
    const ImageSpec& upspec(
        this->spec(subimage, miplevel - 1));  // next higher level
    float* bilerppels = OIIO_ALLOCA(float, 4 * nchans);
    float* resultpel  = OIIO_ALLOCA(float, nchans);
    bool ok           = true;
    // FIXME(volume) -- loop over z, too
    for (int j = y0; j <= y1; ++j) {
        float yf = (j + 0.5f) / spec.full_height;
        int ylow;
        float yfrac = floorfrac(yf * upspec.full_height - 0.5, &ylow);
        for (int i = x0; i <= x1; ++i) {
            float xf = (i + 0.5f) / spec.full_width;
            int xlow;
            float xfrac = floorfrac(xf * upspec.full_width - 0.5, &xlow);
            ok &= imagecache().get_pixels(this, thread_info, subimage,
                                          miplevel - 1, xlow, xlow + 2, ylow,
                                          ylow + 2, 0, 1, chbegin, chend,
                                          TypeDesc::FLOAT, bilerppels);
            bilerp(bilerppels + 0, bilerppels + nchans, bilerppels + 2 * nchans,
                   bilerppels + 3 * nchans, xfrac, yfrac, nchans, resultpel);
            lores.setpixel(i - x0, j - y0, resultpel);
        }
    }

    // Now convert and copy those values out to the caller's buffer
    lores.get_pixels(ROI(0, tw, 0, th, 0, 1, 0, nchans), format, data);

    // Restore the microcache to the way it was before.
    thread_info->tile     = oldtile;
    thread_info->lasttile = oldlasttile;

    return ok;
}



// Helper routine for read_tile that handles the rare (but tricky) case
// of reading a "tile" from a file that's scanline-oriented.
bool
ImageCacheFile::read_untiled(ImageCachePerThreadInfo* thread_info,
                             ImageInput* inp, int subimage, int miplevel, int x,
                             int y, int z, int chbegin, int chend,
                             TypeDesc format, void* data)
{
    // Strides for a single tile
    const ImageSpec& spec(this->spec(subimage, miplevel));
    int tw = spec.tile_width;
    int th = spec.tile_height;
    OIIO_DASSERT(chend > chbegin);
    int nchans       = chend - chbegin;
    stride_t xstride = AutoStride, ystride = AutoStride, zstride = AutoStride;
    spec.auto_stride(xstride, ystride, zstride, format, nchans, tw, th);

    bool ok = true;
    if (subimageinfo(subimage).autotiled) {
        // Auto-tile is on, with a tile size that isn't the whole image.
        // We're only being asked for one tile, but since it's a
        // scanline image, we are forced to read (at the very least) a
        // whole row of tiles.  So we add all those tiles to the cache,
        // if not already present, on the assumption that it's highly
        // likely that they will also soon be requested.
        // FIXME -- I don't think this works properly for 3D images
        size_t pixelsize = size_t(nchans * format.size());
        // Because of the way we copy below, we need to allocate the
        // buffer to be an even multiple of the tile width, so round up.
        stride_t scanlinesize = tw * ((spec.width + tw - 1) / tw);
        scanlinesize *= pixelsize;
        std::unique_ptr<char[]> buf(
            new char[scanlinesize * th]);  // a whole tile-row size
        int yy = y - spec.y;               // counting from top scanline
        // [y0,y1] is the range of scanlines to read for a tile-row
        int y0 = yy - (yy % th);
        int y1 = std::min(y0 + th - 1, spec.height - 1);
        y0 += spec.y;
        y1 += spec.y;
        // Read the whole tile-row worth of scanlines
        ok = inp->read_scanlines(subimage, miplevel, y0, y1 + 1, z, chbegin,
                                 chend, format, (void*)&buf[0], pixelsize,
                                 scanlinesize);
        if (!ok) {
            std::string err = inp->geterror();
            if (!err.empty() && errors_should_issue())
                imagecache().error("{}", err);
        }
        size_t b = (y1 - y0 + 1) * spec.scanline_bytes();
        thread_info->m_stats.bytes_read += b;
        m_bytesread += b;
        ++m_tilesread;

        // For all tiles in the tile-row, enter them into the cache if not
        // already there.  Special case for the tile we're actually being
        // asked for -- save it in 'data' rather than adding a tile.
        int xx = x - spec.x;      // counting from left row
        int x0 = xx - (xx % tw);  // start of the tile we are retrieving
        for (int i = 0; i < spec.width; i += tw) {
            if (i == xx) {
                // This is the tile we've been asked for
                convert_image(nchans, tw, th, 1, &buf[x0 * pixelsize], format,
                              pixelsize, scanlinesize, scanlinesize * th, data,
                              format, xstride, ystride, zstride);
            } else {
                // Not the tile we asked for, but it's in the same
                // tile-row, so let's put it in the cache anyway so
                // it'll be there when asked for.
                TileID id(*this, subimage, miplevel, i + spec.x, y0, z, chbegin,
                          chend);
                if (!imagecache().tile_in_cache(id, thread_info)) {
                    ImageCacheTileRef tile;
                    tile = new ImageCacheTile(id, &buf[i * pixelsize], format,
                                              pixelsize, scanlinesize,
                                              scanlinesize * th);
                    ok &= tile->valid();
                    imagecache().add_tile_to_cache(tile, thread_info);
                }
            }
        }
    } else {
        // No auto-tile -- the tile is the whole image
        ok = inp->read_image(subimage, miplevel, chbegin, chend, format, data,
                             xstride, ystride, zstride);
        if (!ok) {
            std::string err = inp->geterror();
            if (!err.empty() && errors_should_issue())
                imagecache().error("{}", err);
        }
        size_t b = spec.image_bytes();
        thread_info->m_stats.bytes_read += b;
        m_bytesread += b;
        ++m_tilesread;
        // If we read the whole image (and there are no other subimages),
        // presumably we're done, so release the file handle.
        if (subimages() == 1)
            close();
        // FIXME: ^^^ is that a good idea or not?
    }

    return ok;
}



void
ImageCacheFile::close()
{
    // Clearing the m_input will close and free it when no other threads
    // are still hanging onto it.
    std::shared_ptr<ImageInput> empty;
    set_imageinput(empty);
}



void
ImageCacheFile::release()
{
    Timer input_mutex_timer;
    recursive_lock_guard guard(m_input_mutex);
    m_mutex_wait_time += input_mutex_timer();
    if (m_used)
        m_used = false;
    else if (m_allow_release)
        close();
}



void
ImageCacheFile::invalidate()
{
    Timer input_mutex_timer;
    recursive_lock_guard guard(m_input_mutex);
    m_mutex_wait_time += input_mutex_timer();
    close();
    invalidate_spec();
    mark_not_broken();
    m_fingerprint.clear();
    duplicate(NULL);

    m_filename = m_imagecache.resolve_filename(m_filename_original.string());

    // Eat any errors that occurred in the open/close
    while (!imagecache().geterror().empty())
        ;
    m_errors_issued = 0;  // start error count fresh
}



bool
ImageCacheFile::get_average_color(float* avg, int subimage, int chbegin,
                                  int chend)
{
    if (subimage < 0 || subimage > subimages())
        return false;  // invalid subimage
    SubimageInfo& si(m_subimages[subimage]);

    if (!si.has_average_color) {
        // try to figure it out by grabbing the single pixel at the 1x1
        // MIP level.
        int nlevels = (int)si.levels.size();
        const ImageSpec& spec(si.spec(nlevels - 1));  // The 1x1 level
        if (spec.width != 1 || spec.height != 1 || spec.depth != 1)
            return false;  // no hope, there's no 1x1 MIP level to sample
        spin_lock lock(si.average_color_mutex);
        if (!si.has_average_color) {
            si.average_color.resize(spec.nchannels);
            bool ok = m_imagecache.get_pixels(this, NULL, subimage, nlevels - 1,
                                              spec.x, spec.x + 1, spec.y,
                                              spec.y + 1, spec.z, spec.z + 1, 0,
                                              spec.nchannels, TypeFloat,
                                              &si.average_color[0]);
            si.has_average_color = ok;
        }
    }

    if (si.has_average_color) {
        const ImageSpec& spec(si.spec(0));
        for (int i = 0, c = chbegin; c < chend; ++i, ++c)
            avg[i] = (c < spec.nchannels) ? si.average_color[c] : 0.0f;
        return true;
    }

    return false;
}



int
ImageCacheFile::errors_should_issue() const
{
    return (++m_errors_issued <= imagecache().max_errors_per_file());
}



void
ImageCacheFile::mark_not_broken()
{
    m_broken = false;
    m_broken_message.clear();
}



void
ImageCacheFile::mark_broken(string_view error)
{
    m_broken = true;
    if (!error.size())
        error = string_view("unknown error");
    m_broken_message = error;
    imagecache().error("{}", error);
    invalidate_spec();
}



ImageCacheFile*
ImageCacheImpl::find_file(ustring filename,
                          ImageCachePerThreadInfo* thread_info,
                          ImageInput::Creator creator, const ImageSpec* config,
                          bool replace)
{
    // Debugging aid: attribute "substitute_image" forces all image
    // references to be to one named file.
    if (!m_substitute_image.empty())
        filename = m_substitute_image;

    // Shortcut - check the per-thread microcache before grabbing a more
    // expensive lock on the shared file cache.
    ImageCacheFile* tf = replace ? nullptr : thread_info->find_file(filename);

    // Make sure the ImageCacheFile entry exists and is in the
    // file cache.  For this part, we need to lock the file cache.
    bool newfile = false;
    if (!tf) {  // was not found in microcache
#if IMAGECACHE_TIME_STATS
        Timer timer;
#endif
        size_t bin                  = m_files.lock_bin(filename);
        FilenameMap::iterator found = m_files.find(filename, false);
        if (found) {
            tf = found->second.get();
        } else {
            // No such entry in the file cache.  Add it, but don't open yet.
            tf = new ImageCacheFile(*this, thread_info, filename, creator,
                                    config);
            m_files.insert(filename, tf, false);
            newfile = true;
        }
        m_files.unlock_bin(bin);
        if (replace && found) {
            invalidate(filename, true);
            tf->reset(creator, config);
        }

        if (newfile) {
            check_max_files(thread_info);
            if (!tf->duplicate())
                ++thread_info->m_stats.unique_files;
        }
        thread_info->remember_filename(filename, tf);  // add to the microcache
#if IMAGECACHE_TIME_STATS
        thread_info->m_stats.find_file_time += timer();
#endif
    }

    // Ensure that it's open and do other important housekeeping.
    //    tf = verify_file (tf, thread_info, header_only);
    return tf;
}



ImageCacheFile*
ImageCacheImpl::verify_file(ImageCacheFile* tf,
                            ImageCachePerThreadInfo* thread_info,
                            bool header_only)
{
    if (!tf)
        return NULL;

    if (tf->is_udim()) {
        // Can't really open a UDIM-like virtual file
        return tf;
    }

    // Open the file if it's never been opened before.
    // No need to have the file cache locked for this, though we lock
    // the tf->m_input_mutex if we need to open it.
    if (!tf->validspec()) {
        Timer timer;
        if (!thread_info)
            thread_info = get_perthread_info();
        Timer input_mutex_timer;
        recursive_lock_guard guard(tf->m_input_mutex);
        tf->m_mutex_wait_time += input_mutex_timer();
        if (!tf->validspec()) {
            tf->open(thread_info);
            OIIO_DASSERT(tf->m_broken || tf->validspec());
            double createtime = timer();
            ImageCacheStatistics& stats(thread_info->m_stats);
            stats.fileio_time += createtime;
            stats.fileopen_time += createtime;
            tf->iotime() += createtime;

            // What if we've opened another file, with a different name,
            // but the SAME pixels?  It can happen!  Bad user, bad!  But
            // let's save them from their own foolishness.
            if (!tf->fingerprint().empty() && m_deduplicate) {
                // std::cerr << filename << " hash=" << tf->fingerprint() << "\n";
                ImageCacheFile* dup = find_fingerprint(tf->fingerprint(), tf);
                if (dup != tf) {
                    // Already in fingerprints -- mark this one as a
                    // duplicate, but ONLY if we don't have other
                    // reasons not to consider them true duplicates (the
                    // fingerprint only considers source image pixel values,
                    // and of course only if they are the same "shape".
                    // FIXME -- be sure to add extra tests
                    // here if more metadata have significance later!
                    bool match = (tf->subimages() == dup->subimages());
                    const ImageSpec& tfspec(tf->nativespec(0, 0));
                    const ImageSpec& dupspec(dup->nativespec(0, 0));
                    match &= (tfspec.width == dupspec.width
                              && tfspec.height == dupspec.height
                              && tfspec.depth == dupspec.depth
                              && tfspec.nchannels == dupspec.nchannels
                              && tf->subimages() == dup->subimages()
                              && tf->miplevels(0) == dup->miplevels(0)
                              && tf->m_swrap == dup->m_swrap
                              && tf->m_twrap == dup->m_twrap
                              && tf->m_rwrap == dup->m_rwrap
                              && tf->m_envlayout == dup->m_envlayout
                              && tf->m_y_up == dup->m_y_up
                              && tf->m_sample_border == dup->m_sample_border);
                    for (int s = 0, e = tf->subimages(); match && s < e; ++s) {
                        match &= (tf->datatype(s) == dup->datatype(s));
                    }
                    if (match) {
                        tf->duplicate(dup);
                        tf->close();
                        // std::cerr << "  duplicates "
                        //   << fingerfound->second.get()->filename() << "\n";
                    }
                }
            }
#if IMAGECACHE_TIME_STATS
            stats.find_file_time += timer() - createtime;
#endif
        }
    }

    if (!header_only) {
        // if this is a duplicate texture, switch to the canonical copy
        // N.B. If looking up header info (i.e., get_image_info, rather
        // than getting pixels, use the original not the duplicate, since
        // metadata may differ even if pixels are identical).
        if (tf->duplicate())
            tf = tf->duplicate();
        tf->use();  // Mark it as recently used
    }
    return tf;
}



ImageCacheFile*
ImageCacheImpl::find_fingerprint(ustring finger, ImageCacheFile* file)
{
    spin_lock lock(m_fingerprints_mutex);
    // Insert if missing, otherwise return old value
    return m_fingerprints.emplace(finger, file).first->second.get();
}



void
ImageCacheImpl::clear_fingerprints()
{
    spin_lock lock(m_fingerprints_mutex);
    m_fingerprints.clear();
}



void
ImageCacheImpl::check_max_files(ImageCachePerThreadInfo* /*thread_info*/)
{
#if 0
    if (! (m_stat_open_files_created % 16) || m_stat_open_files_current >= m_max_open_files) {
        std::cerr << "open files " << m_stat_open_files_current << ", max = " << m_max_open_files << "\n";
    std::cout << "    ImageInputs : " << m_stat_open_files_created << " created, " << m_stat_open_files_current << " current, " << m_stat_open_files_peak << " peak\n";
    }
#endif

    // Early out if we aren't exceeding the open file handle limit
    if (m_stat_open_files_current < m_max_open_files)
        return;

    // Try to grab the file_sweep_mutex lock. If somebody else holds it,
    // just return -- leave the handle limit enforcement to whomever is
    // already in this function, no need for two threads to do it at
    // once.  If this means we may ephemerally be over the handle limit,
    // so be it.
    if (!m_file_sweep_mutex.try_lock())
        return;

    // Now, what we want to do is have a "clock hand" that sweeps across
    // the cache, releasing files that haven't been used for a long
    // time.  Because of multi-thread, rather than keep an iterator
    // around for this (which could be invalidated since the last time
    // we used it), we just remember the filename of the next file to
    // check, then look it up fresh.  That is m_file_sweep_name.

    // Get a (locked) iterator for the next tile to be examined.
    FilenameMap::iterator sweep;
    if (!m_file_sweep_name.empty()) {
        // We saved the sweep_id. Find the iterator corresponding to it.
        sweep = m_files.find(m_file_sweep_name);
        // Note: if the sweep_id is no longer in the table, sweep will be an
        // empty iterator. That's ok, it will be fixed early in the main
        // loop below.
    }


    // Loop while we still have too many files open.  Also, be careful
    // of looping for too long, exit the loop if we just keep spinning
    // uncontrollably.
    int full_loops = 0;
    while (m_stat_open_files_current >= m_max_open_files && full_loops <= 100) {
        // If we have fallen off the end of the cache, loop back to the
        // beginning and increment our full_loops count.
        if (!sweep) {
            sweep = m_files.begin();
            ++full_loops;
        }
        // If we're STILL at the end, it must be that somehow the entire
        // cache is empty.  So just declare ourselves done.
        if (!sweep)
            break;
        OIIO_DASSERT(sweep->second);
        sweep->second->release();  // May reduce open files
        ++sweep;
        // Note: This loop is a lot less complicated than the one in
        // ImageCacheImpl::check_max_mem. That's because for the file
        // cache, we just need to release old ones, we don't actually
        // erase entries from the cache, so there is no tricky lock/unlock,
        // erasure, or invalidation of iterators.
    }

    // OK, by this point we have either closed enough files to be below
    // the limit again, or the cache is empty, or we've looped over the
    // cache too many times and are giving up.

    // Now we must save the filename for next time.  Just set it to an
    // empty string if we don't have a valid iterator at this point.
    m_file_sweep_name = (sweep ? sweep->first : ustring());
    m_file_sweep_mutex.unlock();

    // N.B. As we exit, the iterators will go out of scope and we will
    // retain no locks on the cache.
}



void
ImageCacheImpl::set_min_cache_size(long long newsize)
{
    atomic_max(m_max_memory_bytes, newsize);
}



ImageCacheTile::ImageCacheTile(const TileID& id)
    : m_id(id)
    , m_valid(true)
{
    id.file().imagecache().incr_tiles(0);  // mem counted separately in read
}



ImageCacheTile::ImageCacheTile(const TileID& id, const void* pels,
                               TypeDesc format, stride_t xstride,
                               stride_t ystride, stride_t zstride, bool copy)
    : m_id(id)
{
    ImageCacheFile& file(m_id.file());
    const ImageSpec& spec(file.spec(id.subimage(), id.miplevel()));
    m_channelsize = file.datatype(id.subimage()).size();
    m_pixelsize   = id.nchannels() * m_channelsize;
    if (copy) {
        size_t size = memsize_needed();
        OIIO_ASSERT_MSG(size > 0 && memsize() == 0,
                        "size was %llu, memsize = %llu",
                        (unsigned long long)size,
                        (unsigned long long)memsize());
        m_pixels_size = size;
        m_pixels.reset(new char[m_pixels_size]);
        m_valid
            = convert_image(id.nchannels(), spec.tile_width, spec.tile_height,
                            spec.tile_depth, pels, format, xstride, ystride,
                            zstride, &m_pixels[0], file.datatype(id.subimage()),
                            m_pixelsize, m_pixelsize * spec.tile_width,
                            m_pixelsize * spec.tile_width * spec.tile_height);
    } else {
        m_nofree      = true;  // Don't free the pointer!
        m_pixels_size = 0;
        m_pixels.reset((char*)pels);
        m_valid = true;
    }
    id.file().imagecache().incr_tiles(m_pixels_size);
    m_pixels_ready = true;  // Caller sent us the pixels, no read necessary
    // FIXME -- for shadow, fill in mindepth, maxdepth
}



ImageCacheTile::~ImageCacheTile()
{
    m_id.file().imagecache().decr_tiles(memsize());
    if (m_nofree)
        m_pixels.release();  // release without freeing
}



size_t
ImageCacheTile::memsize_needed() const
{
    const ImageSpec& spec(file().spec(m_id.subimage(), m_id.miplevel()));
    size_t s = spec.tile_pixels() * pixelsize();
    // N.B. Round up so we can use a SIMD fetch for the last pixel and
    // channel without running off the end.
    s += OIIO_SIMD_MAX_SIZE_BYTES;
    return s;
}



void
ImageCacheTile::read(ImageCachePerThreadInfo* thread_info)
{
    ImageCacheFile& file(m_id.file());
    m_channelsize = file.datatype(id().subimage()).size();
    m_pixelsize   = m_id.nchannels() * m_channelsize;
    size_t size   = memsize_needed();
    OIIO_ASSERT(memsize() == 0 && size > OIIO_SIMD_MAX_SIZE_BYTES);
    m_pixels.reset(new char[m_pixels_size = size]);
    // Clear the end pad values so there aren't NaNs sucked up by simd loads
    memset(m_pixels.get() + size - OIIO_SIMD_MAX_SIZE_BYTES, 0,
           OIIO_SIMD_MAX_SIZE_BYTES);
    m_valid = file.read_tile(thread_info, m_id.subimage(), m_id.miplevel(),
                             m_id.x(), m_id.y(), m_id.z(), m_id.chbegin(),
                             m_id.chend(), file.datatype(m_id.subimage()),
                             &m_pixels[0]);
    m_id.file().imagecache().incr_mem(size);
    if (m_valid) {
        // Figure out if
        ImageCacheFile::LevelInfo& lev(
            file.levelinfo(m_id.subimage(), m_id.miplevel()));
        int whichtile = ((m_id.x() - lev.spec.x) / lev.spec.tile_width)
                        + ((m_id.y() - lev.spec.y) / lev.spec.tile_height)
                              * lev.nxtiles
                        + ((m_id.z() - lev.spec.z) / lev.spec.tile_depth)
                              * (lev.nxtiles * lev.nytiles);
        int index       = whichtile / 64;
        int64_t bitmask = int64_t(1ULL << (whichtile & 63));
        int64_t oldval  = lev.tiles_read[index].fetch_or(bitmask);
        if (oldval & bitmask)  // Was it previously read?
            file.register_redundant_tile(lev.spec.tile_bytes());
    } else {
        // (! m_valid)
        m_used = false;  // Don't let it hold mem if invalid
        if (file.mod_time() != Filesystem::last_write_time(file.filename()))
            file.imagecache().error(
                "File \"{}\" was modified after being opened by OIIO",
                file.filename());
#if 0
        std::cerr << "(1) error reading tile " << m_id.x() << ' ' << m_id.y()
                  << ' ' << m_id.z()
                  << " subimg=" << m_id.subimage()
                  << " mip=" << m_id.miplevel()
                  << " from " << file.filename() << "\n";
#endif
    }
    m_pixels_ready = true;
    // FIXME -- for shadow, fill in mindepth, maxdepth
}



void
ImageCacheTile::wait_pixels_ready() const
{
    atomic_backoff backoff;
    while (!m_pixels_ready) {
        backoff();
    }
}



const void*
ImageCacheTile::data(int x, int y, int z, int c) const
{
    const ImageSpec& spec = m_id.file().spec(m_id.subimage(), m_id.miplevel());
    size_t w              = spec.tile_width;
    size_t h              = spec.tile_height;
    size_t d              = spec.tile_depth;
    OIIO_DASSERT(d >= 1);
    x -= m_id.x();
    y -= m_id.y();
    z -= m_id.z();
    if (x < 0 || x >= (int)w || y < 0 || y >= (int)h || z < 0 || z >= (int)d
        || c < m_id.chbegin() || c > m_id.chend())
        return NULL;
    size_t offset = ((z * h + y) * w + x) * pixelsize()
                    + (c - m_id.chbegin()) * channelsize();
    return (const void*)&m_pixels[offset];
}



ImageCacheImpl::ImageCacheImpl()
    : m_perthread_info(&cleanup_perthread_info)
{
    init();
}



void
ImageCacheImpl::set_max_open_files(int max_open_files)
{
    // Clamp to somewhat less than the maximum number of files allowed
    // by the system.
    int m = int(std::min(Sysutil::max_open_files(),
                         size_t(std::numeric_limits<int>::max())));
    m     = std::max(10, m - 5 * int(Sysutil::hardware_concurrency()));
    m_max_open_files = std::min(max_open_files, m);
    // std::cout << "clamped max_open_files = " << m_max_open_files << "\n";
}



void
ImageCacheImpl::init()
{
    set_max_open_files(100);
    m_max_memory_bytes     = 256 * 1024 * 1024;  // 256 MB default cache size
    m_autotile             = 0;
    m_autoscanline         = false;
    m_automip              = false;
    m_forcefloat           = false;
    m_accept_untiled       = true;
    m_accept_unmipped      = true;
    m_deduplicate          = true;
    m_unassociatedalpha    = false;
    m_failure_retries      = 0;
    m_latlong_y_up_default = true;
    m_Mw2c.makeIdentity();
    m_mem_used                = 0;
    m_statslevel              = 0;
    m_max_errors_per_file     = 100;
    m_stat_tiles_created      = 0;
    m_stat_tiles_current      = 0;
    m_stat_tiles_peak         = 0;
    m_stat_open_files_created = 0;
    m_stat_open_files_current = 0;
    m_stat_open_files_peak    = 0;

    // Allow environment variable to override default options
    const char* options = getenv("OPENIMAGEIO_IMAGECACHE_OPTIONS");
    if (options)
        attribute("options", options);
}



ImageCacheImpl::~ImageCacheImpl()
{
    printstats();
    erase_perthread_info();
}



void
ImageCacheImpl::mergestats(ImageCacheStatistics& stats) const
{
    stats.init();
    spin_lock lock(m_perthread_info_mutex);
    for (size_t i = 0; i < m_all_perthread_info.size(); ++i)
        stats.merge(m_all_perthread_info[i]->m_stats);
}



std::string
ImageCacheImpl::onefile_stat_line(const ImageCacheFileRef& file, int i,
                                  bool includestats) const
{
    // FIXME -- make meaningful stat printouts for multi-image textures
    std::ostringstream out;
    out.imbue(std::locale::classic());  // Force "C" locale with '.' decimal
    const ImageSpec& spec(file->spec(0, 0));
    const char* formatcode = "u8";
    switch (spec.format.basetype) {
    case TypeDesc::UINT8: formatcode = "u8 "; break;
    case TypeDesc::INT8: formatcode = "i8 "; break;
    case TypeDesc::UINT16: formatcode = "u16"; break;
    case TypeDesc::INT16: formatcode = "i16"; break;
    case TypeDesc::UINT: formatcode = "u32"; break;
    case TypeDesc::INT: formatcode = "i32"; break;
    case TypeDesc::UINT64: formatcode = "i64"; break;
    case TypeDesc::INT64: formatcode = "u64"; break;
    case TypeDesc::HALF: formatcode = "f16"; break;
    case TypeDesc::FLOAT: formatcode = "f32"; break;
    case TypeDesc::DOUBLE: formatcode = "f64"; break;
    default: break;
    }
    if (i >= 0)
        out << Strutil::sprintf("%7d ", i);
    if (includestats) {
        unsigned long long redund_tiles = file->redundant_tiles();
        if (redund_tiles)
            out << Strutil::sprintf(
                "%4llu  %7llu   %8.1f   (%5llu %6.1f) %9s  ",
                (unsigned long long)file->timesopened(),
                (unsigned long long)file->tilesread(),
                file->bytesread() / 1024.0 / 1024.0, redund_tiles,
                file->redundant_bytesread() / 1024.0 / 1024.0,
                Strutil::timeintervalformat(file->iotime()));
        else
            out << Strutil::sprintf(
                "%4llu  %7llu   %8.1f                  %9s  ",
                (unsigned long long)file->timesopened(),
                (unsigned long long)file->tilesread(),
                file->bytesread() / 1024.0 / 1024.0,
                Strutil::timeintervalformat(file->iotime()));
    }
    if (file->subimages() > 1)
        out << Strutil::sprintf("%3d face x%d.%s", file->subimages(),
                                spec.nchannels, formatcode);
    else
        out << Strutil::sprintf("%4dx%4dx%d.%s", spec.width, spec.height,
                                spec.nchannels, formatcode);
    out << "  " << file->filename() << " ";
    if (file->duplicate()) {
        out << " DUPLICATES " << file->duplicate()->filename();
        return out.str();
    }
    for (int s = 0; s < file->subimages(); ++s)
        if (file->subimageinfo(s).untiled) {
            out << " UNTILED";
            break;
        }
    if (automip()) {
        // FIXME -- we should directly measure whether we ever automipped
        // this file.  This is a little inexact.
        for (int s = 0; s < file->subimages(); ++s)
            if (file->subimageinfo(s).unmipped) {
                out << " UNMIPPED";
                break;
            }
    }
    if (!file->mipused()) {
        for (int s = 0; s < file->subimages(); ++s)
            if (!file->subimageinfo(s).unmipped) {
                out << " MIP-UNUSED";
                break;
            }
    }
    if (file->mipreadcount().size() > 1) {
        out << " MIP-COUNT[";
        int nmip = (int)file->mipreadcount().size();
        for (int c = 0; c < nmip; c++)
            out << (c ? "," : "") << file->mipreadcount()[c];
        out << "]";
    }

    return out.str();
}



std::string
ImageCacheImpl::getstats(int level) const
{
    // Merge all the threads
    ImageCacheStatistics stats;
    mergestats(stats);

    // Gather file list and statistics
    size_t total_opens = 0, total_tiles = 0;
    size_t total_redundant_tiles      = 0;
    imagesize_t total_bytes           = 0;
    imagesize_t total_redundant_bytes = 0;
    size_t total_untiled = 0, total_unmipped = 0, total_duplicates = 0;
    size_t total_constant              = 0;
    double total_iotime                = 0;
    double total_input_mutex_wait_time = 0;
    std::vector<ImageCacheFileRef> files;
    {
        for (FilenameMap::iterator f = m_files.begin(); f != m_files.end();
             ++f) {
            const ImageCacheFileRef& file(f->second);
            files.push_back(file);
            total_opens += file->timesopened();
            total_tiles += file->tilesread();
            total_redundant_tiles += file->redundant_tiles();
            total_redundant_bytes += file->redundant_bytesread();
            total_bytes += file->bytesread();
            total_iotime += file->iotime();
            total_input_mutex_wait_time += file->m_mutex_wait_time;
            if (file->duplicate()) {
                ++total_duplicates;
                continue;
            }
            bool found_untiled = false, found_unmipped = false;
            bool found_const = true;
            for (int s = 0, send = file->subimages(); s < send; ++s) {
                const ImageCacheFile::SubimageInfo& si(file->subimageinfo(s));
                found_untiled |= si.untiled;
                found_unmipped |= si.unmipped;
                found_const &= si.is_constant_image;
            }
            total_untiled += found_untiled;
            total_unmipped += found_unmipped;
            total_constant += found_const;
        }
    }

    std::ostringstream out;
    out.imbue(std::locale::classic());  // Force "C" locale with '.' decimal
    if (level > 0) {
        out << "OpenImageIO ImageCache statistics (";
        {
            spin_lock guard(shared_image_cache_mutex);
            if ((void*)this == (void*)shared_image_cache.get())
                out << "shared";
            else
                out << (void*)this;
        }
        out << ") ver " << OIIO_VERSION_STRING << "\n";

        std::string opt;
#define BOOLOPT(name) \
    if (m_##name)     \
    opt += #name " "
#define INTOPT(name) opt += Strutil::sprintf(#name "=%d ", m_##name)
#define STROPT(name)     \
    if (m_##name.size()) \
    opt += Strutil::sprintf(#name "=\"%s\" ", m_##name)
        opt += Strutil::sprintf("max_memory_MB=%0.1f ",
                                m_max_memory_bytes / (1024.0 * 1024.0));
        INTOPT(max_open_files);
        INTOPT(autotile);
        INTOPT(autoscanline);
        INTOPT(automip);
        INTOPT(forcefloat);
        INTOPT(accept_untiled);
        INTOPT(accept_unmipped);
        INTOPT(deduplicate);
        INTOPT(unassociatedalpha);
        INTOPT(failure_retries);
#undef BOOLOPT
#undef INTOPT
#undef STROPT
        out << "  Options:  " << Strutil::wordwrap(opt, 75, 12) << "\n";

        if (stats.unique_files) {
            out << "  Images : " << stats.unique_files << " unique\n";
            out << "    ImageInputs : " << m_stat_open_files_created
                << " created, " << m_stat_open_files_current << " current, "
                << m_stat_open_files_peak << " peak\n";
            out << "    Total pixel data size of all images referenced : "
                << Strutil::memformat(stats.files_totalsize) << "\n";
            out << "    Total actual file size of all images referenced : "
                << Strutil::memformat(stats.files_totalsize_ondisk) << "\n";
            out << "    Pixel data read : "
                << Strutil::memformat(stats.bytes_read) << "\n";
        } else {
            out << "  No images opened\n";
        }
        if (stats.find_file_time > 0.001)
            out << "    Find file time : "
                << Strutil::timeintervalformat(stats.find_file_time) << "\n";
        if (stats.fileio_time > 0.001) {
            out << "    File I/O time : "
                << Strutil::timeintervalformat(stats.fileio_time);
            {
                spin_lock lock(m_perthread_info_mutex);
                size_t nthreads = m_all_perthread_info.size();
                if (nthreads > 1) {
                    double perthreadtime = stats.fileio_time / (float)nthreads;
                    out << " (" << Strutil::timeintervalformat(perthreadtime)
                        << " average per thread, for " << nthreads
                        << " threads)";
                }
            }
            out << "\n";
            out << "    File open time only : "
                << Strutil::timeintervalformat(stats.fileopen_time) << "\n";
        }
        if (stats.file_locking_time > 0.001)
            out << "    File mutex locking time : "
                << Strutil::timeintervalformat(stats.file_locking_time) << "\n";
        if (total_input_mutex_wait_time > 0.001)
            out << "    ImageInput mutex locking time : "
                << Strutil::timeintervalformat(total_input_mutex_wait_time)
                << "\n";
        if (m_stat_tiles_created > 0) {
            out << "  Tiles: " << m_stat_tiles_created << " created, "
                << m_stat_tiles_current << " current, " << m_stat_tiles_peak
                << " peak\n";
            out << "    total tile requests : " << stats.find_tile_calls
                << "\n";
            out << "    micro-cache misses : "
                << stats.find_tile_microcache_misses << " ("
                << 100.0 * (double)stats.find_tile_microcache_misses
                       / (double)stats.find_tile_calls
                << "%)\n";
            out << "    main cache misses : " << stats.find_tile_cache_misses
                << " ("
                << 100.0 * (double)stats.find_tile_cache_misses
                       / (double)stats.find_tile_calls
                << "%)\n";
            out << "    redundant reads: "
                << (unsigned long long)total_redundant_tiles << " tiles, "
                << Strutil::memformat(total_redundant_bytes) << "\n";
        }
        out << "    Peak cache memory : " << Strutil::memformat(m_mem_used)
            << "\n";
        if (stats.tile_locking_time > 0.001)
            out << "    Tile mutex locking time : "
                << Strutil::timeintervalformat(stats.tile_locking_time) << "\n";
        if (stats.find_tile_time > 0.001)
            out << "    Find tile time : "
                << Strutil::timeintervalformat(stats.find_tile_time) << "\n";
        if (stats.file_retry_success || stats.tile_retry_success)
            out << "    Failure reads followed by unexplained success: "
                << stats.file_retry_success << " files, "
                << stats.tile_retry_success << " tiles\n";
    }

    if (level >= 2 && files.size()) {
        out << "  Image file statistics:\n";
        out << "        opens   tiles    MB read   --redundant--   I/O time  res              File\n";
        std::sort(files.begin(), files.end(), filename_compare);
        for (size_t i = 0; i < files.size(); ++i) {
            const ImageCacheFileRef& file(files[i]);
            OIIO_DASSERT(file);
            if (file->is_udim())
                continue;
            if (file->broken() || file->subimages() == 0) {
                out << "  BROKEN                                                                      "
                    << file->filename() << "\n";
                continue;
            }
            out << onefile_stat_line(file, i + 1) << "\n";
        }
        out << Strutil::sprintf(
            "\n  Tot:  %4llu  %7llu   %8.1f   (%5llu %6.1f) %9s\n",
            (unsigned long long)total_opens, (unsigned long long)total_tiles,
            total_bytes / 1024.0 / 1024.0,
            (unsigned long long)total_redundant_tiles,
            total_redundant_bytes / 1024.0 / 1024.0,
            Strutil::timeintervalformat(total_iotime));
    }

    // Try to point out hot spots
    if (level > 0) {
        if (total_duplicates)
            out << "  " << total_duplicates
                << " were exact duplicates of other images\n";
        if (total_untiled || (total_unmipped && automip())) {
            out << "  " << total_untiled << " not tiled, " << total_unmipped
                << " not MIP-mapped\n";
#if 0
            if (files.size() >= 50) {
                out << "  Untiled/unmipped files were:\n";
                for (size_t i = 0;  i < files.size();  ++i) {
                    const ImageCacheFileRef &file (files[i]);
                    if (file->untiled() || (file->unmipped() && automip()))
                        out << onefile_stat_line (file, -1) << "\n";
                }
            }
#endif
        }
        if (total_constant)
            out << "  " << total_constant
                << (total_constant == 1 ? " was" : " were")
                << " constant-valued in all pixels\n";
        if (files.size() >= 50) {
            const int topN = 3;
            int nprinted;
            std::sort(files.begin(), files.end(), bytesread_compare);
            out << "  Top files by bytes read:\n";
            nprinted = 0;
            for (const ImageCacheFileRef& file : files) {
                if (nprinted++ >= topN)
                    break;
                if (file->broken() || !file->validspec())
                    continue;
                out << Strutil::sprintf(
                    "    %d   %6.1f MB (%4.1f%%)  ", nprinted,
                    file->bytesread() / 1024.0 / 1024.0,
                    100.0 * (file->bytesread() / (double)total_bytes));
                out << onefile_stat_line(file, -1, false) << "\n";
            }
            std::sort(files.begin(), files.end(), iotime_compare);
            out << "  Top files by I/O time:\n";
            nprinted = 0;
            for (const ImageCacheFileRef& file : files) {
                if (nprinted++ >= topN)
                    break;
                if (file->broken() || !file->validspec())
                    continue;
                out << Strutil::sprintf(
                    "    %d   %9s (%4.1f%%)   ", nprinted,
                    Strutil::timeintervalformat(file->iotime()).c_str(),
                    100.0 * file->iotime() / total_iotime);
                out << onefile_stat_line(file, -1, false) << "\n";
            }
            std::sort(files.begin(), files.end(), iorate_compare);
            out << "  Files with slowest I/O rates:\n";
            nprinted = 0;
            for (const ImageCacheFileRef& file : files) {
                if (file->broken() || !file->validspec())
                    continue;
                if (file->iotime() < 0.25)
                    continue;
                if (nprinted++ >= topN)
                    break;
                double mb = file->bytesread() / (1024.0 * 1024.0);
                double r  = mb / file->iotime();
                out << Strutil::sprintf("    %d   %6.2f MB/s (%.2fMB/%.2fs)   ",
                                        nprinted, r, mb, file->iotime());
                out << onefile_stat_line(file, -1, false) << "\n";
            }
            if (nprinted == 0)
                out << "    (nothing took more than 0.25s)\n";
            double fast = files.back()->bytesread() / (1024.0 * 1024.0)
                          / files.back()->iotime();
            out << Strutil::sprintf("    (fastest was %.1f MB/s)\n", fast);
            if (total_redundant_tiles > 0) {
                std::sort(files.begin(), files.end(), redundantbytes_compare);
                out << "  Top files by redundant I/O:\n";
                nprinted = 0;
                for (const ImageCacheFileRef& file : files) {
                    if (nprinted++ >= topN)
                        break;
                    if (file->broken() || !file->validspec())
                        continue;
                    out << Strutil::sprintf(
                        "    %d   %6.1f MB (%4.1f%%)  ", nprinted,
                        file->redundant_bytesread() / 1024.0 / 1024.0,
                        100.0
                            * (file->redundant_bytesread()
                               / (double)total_redundant_bytes));
                    out << onefile_stat_line(file, -1, false) << "\n";
                }
            }
        }
        int nbroken = 0;
        for (const ImageCacheFileRef& file : files) {
            if (file->broken())
                ++nbroken;
        }
        out << "  Broken or invalid files: " << nbroken << "\n";
        if (nbroken) {
            std::sort(files.begin(), files.end(), filename_compare);
            int nprinted = 0;
            for (const ImageCacheFileRef& file : files) {
                if (file->broken()) {
                    ++nprinted;
                    out << Strutil::sprintf("   %4d  %s\n", nprinted,
                                            file->filename());
                }
            }
        }
    }

    return out.str();
}



void
ImageCacheImpl::printstats() const
{
    if (m_statslevel == 0)
        return;
    std::cout << getstats(m_statslevel) << "\n\n";
}



void
ImageCacheImpl::reset_stats()
{
    {
        spin_lock lock(m_perthread_info_mutex);
        for (size_t i = 0; i < m_all_perthread_info.size(); ++i)
            m_all_perthread_info[i]->m_stats.init();
    }

    {
        for (FilenameMap::iterator f = m_files.begin(); f != m_files.end();
             ++f) {
            const ImageCacheFileRef& file(f->second);
            file->m_timesopened = 0;
            file->m_tilesread   = 0;
            file->m_bytesread   = 0;
            file->m_iotime      = 0;
        }
    }
}



bool
ImageCacheImpl::attribute(string_view name, TypeDesc type, const void* val)
{
    bool do_invalidate    = false;
    bool force_invalidate = false;
    if (name == "options" && type == TypeDesc::STRING) {
        return optparser(*this, *(const char**)val);
    }
    if (name == "max_open_files" && type == TypeDesc::INT) {
        set_max_open_files(*(const int*)val);
    } else if (name == "max_memory_MB" && type == TypeDesc::FLOAT) {
        float size = *(const float*)val;
#ifdef NDEBUG
        size = std::max(size, 10.0f);  // Don't let users choose < 10 MB
#else
        size = std::max(size, 1.0f);  // But let developers debugging do it
#endif
        m_max_memory_bytes = (long long)(size * (long long)(1024 * 1024));
    } else if (name == "max_memory_MB" && type == TypeDesc::INT) {
        float size = *(const int*)val;
#ifdef NDEBUG
        size = std::max(size, 10.0f);  // Don't let users choose < 10 MB
#else
        size = std::max(size, 1.0f);  // But let developers debugging do it
#endif
        m_max_memory_bytes = (long long)(size * (long long)(1024 * 1024));
    } else if (name == "searchpath" && type == TypeDesc::STRING) {
        std::string s = std::string(*(const char**)val);
        if (s != m_searchpath) {
            m_searchpath = s;
            Filesystem::searchpath_split(m_searchpath, m_searchdirs, true);
            do_invalidate    = true;  // in case file can be found with new path
            force_invalidate = true;
        }
    } else if (name == "plugin_searchpath" && type == TypeDesc::STRING) {
        m_plugin_searchpath = std::string(*(const char**)val);
    } else if (name == "statistics:level" && type == TypeDesc::INT) {
        m_statslevel = *(const int*)val;
    } else if (name == "max_errors_per_file" && type == TypeDesc::INT) {
        m_max_errors_per_file = *(const int*)val;
    } else if (name == "autotile" && type == TypeDesc::INT) {
        int a = ceil2(*(const int*)val);  // guarantee pow2
        // Clamp to minimum 8x8 tiles to protect against stupid user who
        // think this is a boolean rather than the tile size.  Unless
        // we're in DEBUG mode, then allow developers to play with fire.
#ifdef NDEBUG
        if (a > 0 && a < 8)
            a = 8;
#endif
        if (a != m_autotile) {
            m_autotile    = a;
            do_invalidate = true;
        }
    } else if (name == "autoscanline" && type == TypeDesc::INT) {
        bool a = (*(const int*)val != 0);
        if (a != m_autoscanline) {
            m_autoscanline = a;
            do_invalidate  = true;
        }
    } else if (name == "automip" && type == TypeDesc::INT) {
        bool a = (*(const int*)val != 0);
        if (a != m_automip) {
            m_automip     = a;
            do_invalidate = true;
        }
    } else if (name == "forcefloat" && type == TypeDesc::INT) {
        bool a = (*(const int*)val != 0);
        if (a != m_forcefloat) {
            m_forcefloat  = a;
            do_invalidate = true;
        }
    } else if (name == "accept_untiled" && type == TypeDesc::INT) {
        bool a = (*(const int*)val != 0);
        if (a != m_accept_untiled) {
            m_accept_untiled = a;
            do_invalidate    = true;
        }
    } else if (name == "accept_unmipped" && type == TypeDesc::INT) {
        bool a = (*(const int*)val != 0);
        if (a != m_accept_unmipped) {
            m_accept_unmipped = a;
            do_invalidate     = true;
        }
    } else if (name == "deduplicate" && type == TypeDesc::INT) {
        bool r = (*(const int*)val != 0);
        if (r != m_deduplicate) {
            m_deduplicate = r;
            do_invalidate = true;
        }
    } else if (name == "unassociatedalpha" && type == TypeDesc::INT) {
        bool r = (*(const int*)val != 0);
        if (r != m_unassociatedalpha) {
            m_unassociatedalpha = r;
            do_invalidate       = true;
        }
    } else if (name == "failure_retries" && type == TypeDesc::INT) {
        m_failure_retries = *(const int*)val;
    } else if (name == "trust_file_extensions" && type == TypeDesc::INT) {
        m_trust_file_extensions = *(const int*)val;
    } else if (name == "latlong_up" && type == TypeDesc::STRING) {
        bool y_up = !strcmp("y", *(const char**)val);
        if (y_up != m_latlong_y_up_default) {
            m_latlong_y_up_default = y_up;
            do_invalidate          = true;
        }
    } else if (name == "substitute_image" && type == TypeDesc::STRING) {
        m_substitute_image = ustring(*(const char**)val);
        do_invalidate      = true;
    } else if (name == "max_mip_res" && type == TypeInt) {
        m_max_mip_res = *(const int*)val;
        do_invalidate = true;
    } else {
        // Otherwise, unknown name
        return false;
    }

    if (do_invalidate)
        invalidate_all(force_invalidate);
    return true;
}



bool
ImageCacheImpl::getattribute(string_view name, TypeDesc type, void* val) const
{
#define ATTR_DECODE(_name, _ctype, _src)                         \
    if (name == _name && type == BaseTypeFromC<_ctype>::value) { \
        *(_ctype*)(val) = (_ctype)(_src);                        \
        return true;                                             \
    }

    ATTR_DECODE("max_open_files", int, m_max_open_files);
    ATTR_DECODE("max_memory_MB", float, m_max_memory_bytes / (1024.0 * 1024.0));
    ATTR_DECODE("max_memory_MB", int, m_max_memory_bytes / (1024 * 1024));
    ATTR_DECODE("statistics:level", int, m_statslevel);
    ATTR_DECODE("max_errors_per_file", int, m_max_errors_per_file);
    ATTR_DECODE("autotile", int, m_autotile);
    ATTR_DECODE("autoscanline", int, m_autoscanline);
    ATTR_DECODE("automip", int, m_automip);
    ATTR_DECODE("forcefloat", int, m_forcefloat);
    ATTR_DECODE("accept_untiled", int, m_accept_untiled);
    ATTR_DECODE("accept_unmipped", int, m_accept_unmipped);
    ATTR_DECODE("deduplicate", int, m_deduplicate);
    ATTR_DECODE("unassociatedalpha", int, m_unassociatedalpha);
    ATTR_DECODE("trust_file_extensions", int, m_trust_file_extensions);
    ATTR_DECODE("failure_retries", int, m_failure_retries);
    ATTR_DECODE("total_files", int, m_files.size());
    ATTR_DECODE("max_mip_res", int, m_max_mip_res);

    // The cases that don't fit in the simple ATTR_DECODE scheme
    if (name == "searchpath" && type == TypeDesc::STRING) {
        *(ustring*)val = m_searchpath;
        return true;
    }
    if (name == "plugin_searchpath" && type == TypeDesc::STRING) {
        *(ustring*)val = m_plugin_searchpath;
        return true;
    }
    if (name == "worldtocommon"
        && (type == TypeMatrix || type == TypeDesc(TypeDesc::FLOAT, 16))) {
        *(Imath::M44f*)val = m_Mw2c;
        return true;
    }
    if (name == "commontoworld"
        && (type == TypeMatrix || type == TypeDesc(TypeDesc::FLOAT, 16))) {
        *(Imath::M44f*)val = m_Mc2w;
        return true;
    }
    if (name == "latlong_up" && type == TypeDesc::STRING) {
        *(const char**)val = ustring(m_latlong_y_up_default ? "y" : "z").c_str();
        return true;
    }
    if (name == "substitute_image" && type == TypeDesc::STRING) {
        *(const char**)val = m_substitute_image.c_str();
        return true;
    }
    if (name == "all_filenames" && type.basetype == TypeDesc::STRING
        && type.is_sized_array()) {
        ustring* names = (ustring*)val;
        int n          = type.arraylen;
        for (FilenameMap::iterator f = m_files.begin();
             f != m_files.end() && n-- > 0; ++f) {
            *names++ = f->second->filename();
        }
        return true;
    }

    if (Strutil::starts_with(name, "stat:")) {
        // Stats we can just grab
        ATTR_DECODE("stat:cache_memory_used", long long, m_mem_used);
        ATTR_DECODE("stat:tiles_created", int, m_stat_tiles_created);
        ATTR_DECODE("stat:tiles_current", int, m_stat_tiles_current);
        ATTR_DECODE("stat:tiles_peak", int, m_stat_tiles_peak);
        ATTR_DECODE("stat:open_files_created", int, m_stat_open_files_created);
        ATTR_DECODE("stat:open_files_current", int, m_stat_open_files_current);
        ATTR_DECODE("stat:open_files_peak", int, m_stat_open_files_peak);

        // All the other stats are those that need to be summed from all
        // the threads.
        ImageCacheStatistics stats;
        mergestats(stats);
        ATTR_DECODE("stat:find_tile_calls", long long, stats.find_tile_calls);
        ATTR_DECODE("stat:find_tile_microcache_misses", long long,
                    stats.find_tile_microcache_misses);
        ATTR_DECODE("stat:find_tile_cache_misses", int,
                    stats.find_tile_cache_misses);
        ATTR_DECODE("stat:files_totalsize", long long,
                    stats.files_totalsize);  // Old name
        ATTR_DECODE("stat:image_size", long long, stats.files_totalsize);
        ATTR_DECODE("stat:file_size", long long, stats.files_totalsize_ondisk);
        ATTR_DECODE("stat:bytes_read", long long, stats.bytes_read);
        ATTR_DECODE("stat:unique_files", int, stats.unique_files);
        ATTR_DECODE("stat:fileio_time", float, stats.fileio_time);
        ATTR_DECODE("stat:fileopen_time", float, stats.fileopen_time);
        ATTR_DECODE("stat:file_locking_time", float, stats.file_locking_time);
        ATTR_DECODE("stat:tile_locking_time", float, stats.tile_locking_time);
        ATTR_DECODE("stat:find_file_time", float, stats.find_file_time);
        ATTR_DECODE("stat:find_tile_time", float, stats.find_tile_time);
        ATTR_DECODE("stat:texture_queries", long long, stats.texture_queries);
        ATTR_DECODE("stat:texture3d_queries", long long,
                    stats.texture3d_queries);
        ATTR_DECODE("stat:environment_queries", long long,
                    stats.environment_queries);
        ATTR_DECODE("stat:getimageinfo_queries", long long,
                    stats.imageinfo_queries);
        ATTR_DECODE("stat:gettextureinfo_queries", long long,
                    stats.imageinfo_queries);
    }

    return false;
#undef ATTR_DECODE
}



bool
ImageCacheImpl::find_tile_main_cache(const TileID& id, ImageCacheTileRef& tile,
                                     ImageCachePerThreadInfo* thread_info)
{
    OIIO_DASSERT(!id.file().broken());
    ImageCacheStatistics& stats(thread_info->m_stats);

    ++stats.find_tile_microcache_misses;

    {
#if IMAGECACHE_TIME_STATS
        Timer timer1;
#endif
        bool found = m_tilecache.retrieve(id, tile);
#if IMAGECACHE_TIME_STATS
        stats.find_tile_time += timer1();
#endif
        if (found) {
            // We found the tile in the cache, but we need to make sure we
            // wait until the pixels are ready to read.  We purposely have
            // released the lock (above) before calling wait_pixels_ready,
            // otherwise we could deadlock if another thread reading the
            // pixels needs to lock the cache because it's doing automip.
            tile->wait_pixels_ready();
            tile->use();
            OIIO_DASSERT(id == tile->id());
            OIIO_DASSERT(tile);
            return true;
        }
    }

    // The tile was not found in cache.

    ++stats.find_tile_cache_misses;

    // Yes, we're creating and reading a tile with no lock -- this is to
    // prevent all the other threads from blocking because of our
    // expensive disk read.  We believe this is safe, since underneath
    // the ImageCacheFile will lock itself for the read_tile and there are
    // no other non-threadsafe side effects.
    tile = new ImageCacheTile(id);
    // N.B. the ImageCacheTile ctr starts the tile out as 'used'
    OIIO_DASSERT(tile);
    OIIO_DASSERT(id == tile->id());

    add_tile_to_cache(tile, thread_info);
    OIIO_DASSERT(id == tile->id());
    return tile->valid();
}



void
ImageCacheImpl::add_tile_to_cache(ImageCacheTileRef& tile,
                                  ImageCachePerThreadInfo* thread_info)
{
    bool ourtile = m_tilecache.insert_retrieve(tile->id(), tile, tile);

    // If we added a new tile to the cache, we may still need to read the
    // pixels; and if we found the tile in cache, we may need to wait for
    // somebody else to read the pixels.
    if (ourtile) {
        if (!tile->pixels_ready()) {
            Timer timer;
            tile->read(thread_info);
            double readtime = timer();
            thread_info->m_stats.fileio_time += readtime;
            tile->id().file().iotime() += readtime;
        }
        check_max_mem(thread_info);
    } else {
        // Somebody else already added the tile to the cache before we
        // could, so we'll use their reference, but we need to wait until it
        // has read in the pixels.
        tile->wait_pixels_ready();
    }
}



void
ImageCacheImpl::check_max_mem(ImageCachePerThreadInfo* /*thread_info*/)
{
    OIIO_DASSERT(m_mem_used < (long long)m_max_memory_bytes * 10);  // sanity
#if 0
    static atomic_int n;
    if (! (n++ % 64) || m_mem_used >= (long long)m_max_memory_bytes)
        std::cerr << "mem used: " << m_mem_used << ", max = " << m_max_memory_bytes << "\n";
#endif
    // Early out if the cache is empty
    if (m_tilecache.empty())
        return;
    // Early out if we aren't exceeding the tile memory limit
    if (m_mem_used < (long long)m_max_memory_bytes)
        return;

    // Try to grab the tile_sweep_mutex lock. If somebody else holds it,
    // just return -- leave the memory limit enforcement to whomever is
    // already in this function, no need for two threads to do it at
    // once.  If this means we may ephemerally be over the memory limit
    // (because another thread adds a tile before we have freed enough
    // here), so be it.
    if (!m_tile_sweep_mutex.try_lock())
        return;

    // Now, what we want to do is have a "clock hand" that sweeps across
    // the cache, releasing tiles that haven't been used for a long
    // time.  Because of multi-thread, rather than keep an iterator
    // around for this (which could be invalidated since the last time
    // we used it), we just remember the tileID of the next tile to
    // check, then look it up fresh.  That is m_tile_sweep_id.

    // Get a (locked) iterator for the next tile to be examined.
    TileCache::iterator sweep;
    if (!m_tile_sweep_id.empty()) {
        // We saved the sweep_id. Find the iterator corresponding to it.
        sweep = m_tilecache.find(m_tile_sweep_id);
        // Note: if the sweep_id is no longer in the table, sweep will be an
        // empty iterator. That's ok, it will be fixed early in the main
        // loop below.
    }

    // Loop while we still use too much tile memory.  Also, be careful
    // of looping for too long, exit the loop if we just keep spinning
    // uncontrollably.
    int full_loops = 0;
    while (m_mem_used >= (long long)m_max_memory_bytes && full_loops < 100) {
        // If we have fallen off the end of the cache, loop back to the
        // beginning and increment our full_loops count.
        if (!sweep) {
            sweep = m_tilecache.begin();
            ++full_loops;
        }
        // If we're STILL at the end, it must be that somehow the entire
        // cache is empty.  So just declare ourselves done.
        if (!sweep)
            break;
        OIIO_DASSERT(sweep->second);

        if (!sweep->second->release()) {
            // This is a tile we should delete.  To keep iterating
            // safely, we have a good trick:
            // 1. remember the TileID of the tile to delete
            TileID todelete = sweep->first;
            size_t size     = sweep->second->memsize();
            OIIO_DASSERT(m_mem_used >= (long long)size);
            // 2. Find the TileID of the NEXT item. We do this by
            // incrementing the sweep iterator and grabbing its id.
            ++sweep;
            m_tile_sweep_id = (sweep ? sweep->first : TileID());
            // 3. Release the bin lock and erase the tile we wish to delete.
            sweep.unlock();
            m_tilecache.erase(todelete);
            // 4. Re-establish a locked iterator for the next item, since
            // the old iterator may have been invalidated by the erasure.
            if (!m_tile_sweep_id.empty())
                sweep = m_tilecache.find(m_tile_sweep_id);
        } else {
            ++sweep;
        }
    }

    // OK, by this point we have either freed enough tiles to be below
    // the limit again, or the cache is empty, or we've looped over the
    // cache too many times and are giving up.

    // Now we must save the tileid for next time.  Just set it to an
    // empty ID if we don't have a valid iterator at this point.
    m_tile_sweep_id = (sweep ? sweep->first : TileID());
    m_tile_sweep_mutex.unlock();

    // N.B. As we exit, the iterators will go out of scope and we will
    // retain no locks on the cache.
}



std::string
ImageCacheImpl::resolve_filename(const std::string& filename) const
{
    // Ask if the format can generate imagery procedurally. If so, don't
    // go looking for a file.
    auto input      = ImageInput::create(filename);
    bool procedural = input ? input->supports("procedural") : false;
    if (procedural)
        return filename;
    // don't bother with the searchpath_find call since it will do an existence
    // check that we don't need
    if (m_searchdirs.empty())
        return filename;
    std::string s = Filesystem::searchpath_find(filename, m_searchdirs, true);
    return s.empty() ? filename : s;
}



bool
ImageCacheImpl::get_image_info(ustring filename, int subimage, int miplevel,
                               ustring dataname, TypeDesc datatype, void* data)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file = find_file(filename, thread_info, nullptr);
    if (!file && dataname != s_exists) {
        error("Invalid image file \"{}\"", filename);
        return false;
    }
    return get_image_info(file, thread_info, subimage, miplevel, dataname,
                          datatype, data);
}



bool
ImageCacheImpl::get_image_info(ImageCacheFile* file,
                               ImageCachePerThreadInfo* thread_info,
                               int subimage, int miplevel, ustring dataname,
                               TypeDesc datatype, void* data)
{
#define ATTR_DECODE(_name, _ctype, _src)                                 \
    if (dataname == _name && datatype == BaseTypeFromC<_ctype>::value) { \
        *(_ctype*)(data) = (_ctype)(_src);                               \
        return true;                                                     \
    }

    if (!thread_info)
        thread_info = get_perthread_info();
    ImageCacheStatistics& stats(thread_info->m_stats);
    ++stats.imageinfo_queries;
    file = verify_file(file, thread_info, true);
    if (dataname == s_exists && datatype == TypeInt) {
        // Just check for existence.  Need to do this before the invalid
        // file error below, since in this one case, it's not an error
        // for the file to be nonexistent or broken!
        if ((*(int*)data = (file && !file->broken())) == 0) {
            // eat any error generated by find_file
            std::string* errptr = m_errormessage.get();
            if (errptr)
                errptr->clear();
        }
        return true;
    }
    if (!file) {
        error("Invalid image file");
        return false;
    }
    ATTR_DECODE(s_broken, int, file->broken());
    if (Strutil::starts_with(dataname, "stat:")) {
        ATTR_DECODE("stat:tilesread", long long, file->m_tilesread);
        ATTR_DECODE("stat:bytesread", long long, file->m_bytesread);
        ATTR_DECODE("stat:redundant_tiles", long long, file->m_redundant_tiles);
        ATTR_DECODE("stat:redundant_bytesread", long long,
                    file->m_redundant_bytesread);
        ATTR_DECODE("stat:timesopened", int, file->m_timesopened);
        ATTR_DECODE("stat:iotime", float, file->m_iotime);
        ATTR_DECODE("stat:mipused", int, file->m_mipused);
        ATTR_DECODE("stat:is_duplicate", int, bool(file->duplicate()));
        ATTR_DECODE("stat:image_size", long long, file->m_total_imagesize);
        ATTR_DECODE("stat:file_size", long long,
                    file->m_total_imagesize_ondisk);
    }

    if (file->broken()) {
        if (file->errors_should_issue()) {
            // eat any earlier messages
            std::string* errptr = m_errormessage.get();
            if (errptr)
                errptr->clear();
            error("Invalid image file \"{}\": {}", file->filename(),
                  file->broken_error_message());
        }
        return false;
    }
    // No other queries below are expected to work with broken

    if (dataname == s_UDIM && datatype == TypeInt) {
        // Just check for existence.  Need to do this before the invalid
        // file error below, since in this one case, it's not an error
        // for the file to be nonexistent or broken!
        *(int*)data = file->is_udim();
        return true;
    }
    if (file->is_udim() && dataname == s_channels) {
        // Special case -- it's ok to ask for a UDIM's channels. It'll
        // search for a concrete file. Beware, this will cause trouble
        // if different panels of the same UDIM scheme have different
        // numbers of channels! Search the 10x100 files for a match, give
        // up if not found.
        for (int j = 0; j < 100; ++j) {
            for (int i = 0; i < 10; ++i) {
                float s = i + 0.5f, t = j + 0.5f;
                ImageCacheFile* concretefile = resolve_udim(file, thread_info,
                                                            s, t);
                concretefile = verify_file(concretefile, thread_info, true);
                if (concretefile && !concretefile->broken()) {
                    // Recurse to try again with the concrete file
                    bool r = get_image_info(concretefile, thread_info, subimage,
                                            miplevel, dataname, datatype, data);
                    // suppress errors from missing tiles we encountered
                    // prior to finding the one that worked.
                    (void)geterror();
                    return r;
                }
            }
        }
    }

    if (file->is_udim()) {
        return false;  // UDIM-like files fail all other queries
    }
    if (dataname == s_subimages && datatype == TypeInt) {
        *(int*)data = file->subimages();
        return true;
    }

    // Make sure we have a valid subimage and miplevel BEFORE we get a
    // reference to the spec.
    if (subimage < 0 || subimage >= file->subimages()) {
        if (file->errors_should_issue())
            error("Unknown subimage {} (out of {})", subimage,
                  file->subimages());
        return false;
    }
    if (miplevel < 0 || miplevel >= file->miplevels(subimage)) {
        if (file->errors_should_issue())
            error("Unknown mip level {} (out of {})", miplevel,
                  file->miplevels(subimage));
        return false;
    }

    const ImageSpec& spec(file->spec(subimage, miplevel));
    if (dataname == s_resolution && datatype == TypeDesc(TypeDesc::INT, 2)) {
        int* d = (int*)data;
        d[0]   = spec.width;
        d[1]   = spec.height;
        return true;
    }
    if (dataname == s_resolution && datatype == TypeDesc(TypeDesc::INT, 3)) {
        int* d = (int*)data;
        d[0]   = spec.width;
        d[1]   = spec.height;
        d[2]   = spec.depth;
        return true;
    }
    if (dataname == s_texturetype && datatype == TypeString) {
        ustring s(texture_type_name(file->textureformat()));
        *(const char**)data = s.c_str();
        return true;
    }
    if (dataname == s_textureformat && datatype == TypeString) {
        ustring s(texture_format_name(file->textureformat()));
        *(const char**)data = s.c_str();
        return true;
    }
    if (dataname == s_fileformat && datatype == TypeString) {
        *(const char**)data = file->fileformat().c_str();
        return true;
    }
    if (dataname == s_channels && datatype == TypeInt) {
        *(int*)data = spec.nchannels;
        return true;
    }
    if (dataname == s_channels && datatype == TypeFloat) {
        *(float*)data = spec.nchannels;
        return true;
    }
    if (dataname == s_format && datatype == TypeInt) {
        *(int*)data = (int)spec.format.basetype;
        return true;
    }
    if ((dataname == s_cachedformat || dataname == s_cachedpixeltype)
        && datatype == TypeInt) {
        *(int*)data = (int)file->datatype(subimage).basetype;
        return true;
    }
    if (dataname == s_miplevels && datatype == TypeInt) {
        *(int*)data = file->miplevels(subimage);
        return true;
    }
    if (dataname == s_datawindow && datatype.basetype == TypeDesc::INT
        && (datatype == TypeDesc(TypeDesc::INT, 4)
            || datatype == TypeDesc(TypeDesc::INT, 6))) {
        int* d = (int*)data;
        if (datatype.arraylen == 4) {
            d[0] = spec.x;
            d[1] = spec.y;
            d[2] = spec.x + spec.width - 1;
            d[3] = spec.y + spec.height - 1;
        } else {
            d[0] = spec.x;
            d[1] = spec.y;
            d[2] = spec.z;
            d[3] = spec.x + spec.width - 1;
            d[4] = spec.y + spec.height - 1;
            d[5] = spec.z + spec.depth - 1;
        }
        return true;
    }
    if (dataname == s_displaywindow && datatype.basetype == TypeDesc::INT
        && (datatype == TypeDesc(TypeDesc::INT, 4)
            || datatype == TypeDesc(TypeDesc::INT, 6))) {
        int* d = (int*)data;
        if (datatype.arraylen == 4) {
            d[0] = spec.full_x;
            d[1] = spec.full_y;
            d[2] = spec.full_x + spec.full_width - 1;
            d[3] = spec.full_y + spec.full_height - 1;
        } else {
            d[0] = spec.full_x;
            d[1] = spec.full_y;
            d[2] = spec.full_z;
            d[3] = spec.full_x + spec.full_width - 1;
            d[4] = spec.full_y + spec.full_height - 1;
            d[5] = spec.full_z + spec.full_depth - 1;
        }
        return true;
    }
    if (dataname == s_averagecolor && datatype.basetype == TypeDesc::FLOAT) {
        int datalen = datatype.numelements() * datatype.aggregate;
        return file->get_average_color((float*)data, subimage, 0, datalen);
    }
    if (dataname == s_averagealpha && datatype == TypeDesc::FLOAT
        && spec.alpha_channel >= 0) {
        return file->get_average_color((float*)data, subimage,
                                       spec.alpha_channel,
                                       spec.alpha_channel + 1);
    }
    if (dataname == s_constantcolor && datatype.basetype == TypeDesc::FLOAT) {
        if (file->subimageinfo(subimage).is_constant_image) {
            int datalen = datatype.numelements() * datatype.aggregate;
            return file->get_average_color((float*)data, subimage, 0, datalen);
        }
        return false;  // Fail if it's not a constant image
    }
    if (dataname == s_constantalpha && datatype == TypeDesc::FLOAT
        && spec.alpha_channel >= 0) {
        if (file->subimageinfo(subimage).is_constant_image)
            return file->get_average_color((float*)data, subimage,
                                           spec.alpha_channel,
                                           spec.alpha_channel + 1);
        else
            return false;  // Fail if it's not a constant image
    }

    // general case -- handle anything else that's able to be found by
    // spec.find_attribute().
    ParamValue tmpparam;
    const ParamValue* p = spec.find_attribute(dataname, tmpparam);
    if (p && p->type().basevalues() == datatype.basevalues()) {
        // First test for exact base type match
        if (p->type().basetype == datatype.basetype) {
            memcpy(data, p->data(), datatype.size());
            return true;
        }
        // If the real data is int but user asks for float, translate it
        if (p->type().basetype == TypeDesc::FLOAT
            && datatype.basetype == TypeDesc::INT) {
            for (int i = 0; i < int(p->type().basevalues()); ++i)
                ((float*)data)[i] = ((int*)p->data())[i];
            return true;
        }
    }

    return false;

#undef ATTR_DECODE
}



bool
ImageCacheImpl::get_imagespec(ustring filename, ImageSpec& spec, int subimage,
                              int miplevel, bool native)
{
    const ImageSpec* specptr = imagespec(filename, subimage, miplevel, native);
    if (specptr) {
        spec = *specptr;
        return true;
    } else {
        return false;  // imagespec() already handled the errors
    }
}



bool
ImageCacheImpl::get_imagespec(ImageCacheFile* file,
                              ImageCachePerThreadInfo* thread_info,
                              ImageSpec& spec, int subimage, int miplevel,
                              bool native)
{
    const ImageSpec* specptr = imagespec(file, thread_info, subimage, miplevel,
                                         native);
    if (specptr) {
        spec = *specptr;
        return true;
    } else {
        return false;  // imagespec() already handled the errors
    }
}



const ImageSpec*
ImageCacheImpl::imagespec(ustring filename, int subimage, int miplevel,
                          bool native)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file = find_file(filename, thread_info, nullptr);
    if (!file) {
        error("Image file \"{}\" not found", filename);
        return NULL;
    }
    return imagespec(file, thread_info, subimage, miplevel, native);
}



const ImageSpec*
ImageCacheImpl::imagespec(ImageCacheFile* file,
                          ImageCachePerThreadInfo* thread_info, int subimage,
                          int miplevel, bool native)
{
    if (!file) {
        error("Image file handle was NULL");
        return NULL;
    }
    if (!thread_info)
        thread_info = get_perthread_info();
    file = verify_file(file, thread_info, true);
    if (file->broken()) {
        if (file->errors_should_issue())
            error("Invalid image file \"{}\": {}", file->filename(),
                  file->broken_error_message());
        return NULL;
    }
    if (file->is_udim()) {
        error("Cannot retrieve ImageSpec of a UDIM-like virtual file");
        return NULL;  // UDIM-like files don't have an ImageSpec
    }
    if (subimage < 0 || subimage >= file->subimages()) {
        if (file->errors_should_issue())
            error("Unknown subimage {} (out of {})", subimage,
                  file->subimages());
        return NULL;
    }
    if (miplevel < 0 || miplevel >= file->miplevels(subimage)) {
        if (file->errors_should_issue())
            error("Unknown mip level {} (out of {})", miplevel,
                  file->miplevels(subimage));
        return NULL;
    }
    const ImageSpec* spec = native ? &file->nativespec(subimage, miplevel)
                                   : &file->spec(subimage, miplevel);
    return spec;
}



bool
ImageCacheImpl::get_thumbnail(ustring filename, ImageBuf& thumb, int subimage)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file = find_file(filename, thread_info, nullptr);
    if (!file) {
        error("Image file \"{}\" not found", filename);
        return false;
    }
    return get_thumbnail(file, thread_info, thumb, subimage);
}



bool
ImageCacheImpl::get_thumbnail(ImageCacheFile* file,
                              ImageCachePerThreadInfo* thread_info,
                              ImageBuf& thumb, int subimage)
{
    std::shared_ptr<ImageInput> inp = file->open(thread_info);
    if (!inp)
        return false;  // indicates a broken file
    return inp->get_thumbnail(thumb, subimage);
}



int
ImageCacheImpl::subimage_from_name(ImageCacheFile* file, ustring subimagename)
{
    for (int s = 0, send = file->subimages(); s < send; ++s) {
        if (file->subimageinfo(s).subimagename == subimagename)
            return s;
    }
    return -1;  // No matching subimage name
}



bool
ImageCacheImpl::get_pixels(ustring filename, int subimage, int miplevel,
                           int xbegin, int xend, int ybegin, int yend,
                           int zbegin, int zend, TypeDesc format, void* result)
{
    return get_pixels(filename, subimage, miplevel, xbegin, xend, ybegin, yend,
                      zbegin, zend, 0, -1, format, result);
}



bool
ImageCacheImpl::get_pixels(ImageCacheFile* file,
                           ImageCachePerThreadInfo* thread_info, int subimage,
                           int miplevel, int xbegin, int xend, int ybegin,
                           int yend, int zbegin, int zend, TypeDesc format,
                           void* result)
{
    return get_pixels(file, thread_info, subimage, miplevel, xbegin, xend,
                      ybegin, yend, zbegin, zend, 0, -1, format, result);
}



bool
ImageCacheImpl::get_pixels(ustring filename, int subimage, int miplevel,
                           int xbegin, int xend, int ybegin, int yend,
                           int zbegin, int zend, int chbegin, int chend,
                           TypeDesc format, void* result, stride_t xstride,
                           stride_t ystride, stride_t zstride,
                           int cache_chbegin, int cache_chend)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file                 = find_file(filename, thread_info);
    if (!file) {
        error("Image file \"{}\" not found", filename);
        return false;
    }
    return get_pixels(file, thread_info, subimage, miplevel, xbegin, xend,
                      ybegin, yend, zbegin, zend, chbegin, chend, format,
                      result, xstride, ystride, zstride, cache_chbegin,
                      cache_chend);
}



bool
ImageCacheImpl::get_pixels(ImageCacheFile* file,
                           ImageCachePerThreadInfo* thread_info, int subimage,
                           int miplevel, int xbegin, int xend, int ybegin,
                           int yend, int zbegin, int zend, int chbegin,
                           int chend, TypeDesc format, void* result,
                           stride_t xstride, stride_t ystride, stride_t zstride,
                           int cache_chbegin, int cache_chend)
{
    if (!thread_info)
        thread_info = get_perthread_info();
    file = verify_file(file, thread_info);
    if (file->broken()) {
        if (file->errors_should_issue())
            error("Invalid image file \"{}\": {}", file->filename(),
                  file->broken_error_message());
        return false;
    }
    if (file->is_udim()) {
        error("Cannot get_pixels() of a UDIM-like virtual file");
        return false;  // UDIM-like files don't have pixels
    }
    if (subimage < 0 || subimage >= file->subimages()) {
        if (file->errors_should_issue())
            error("get_pixels asked for nonexistent subimage {} of \"{}\"",
                  subimage, file->filename());
        return false;
    }
    if (miplevel < 0 || miplevel >= file->miplevels(subimage)) {
        if (file->errors_should_issue())
            error("get_pixels asked for nonexistent MIP level {} of \"{}\"",
                  miplevel, file->filename());
        return false;
    }

    if (!thread_info)
        thread_info = get_perthread_info();
    const ImageSpec& spec(file->spec(subimage, miplevel));
    bool ok = true;

    // Compute channels and stride if not given (assume all channels,
    // contiguous data layout for strides).
    if (chbegin < 0 || chend < 0) {
        chbegin = 0;
        chend   = spec.nchannels;
    }
    int result_nchans = chend - chbegin;
    if (cache_chbegin < 0 || cache_chend < 0 || cache_chbegin > chbegin
        || cache_chend < chend) {
        cache_chbegin = 0;
        cache_chend   = spec.nchannels;
    }
    int cache_nchans = cache_chend - cache_chbegin;
    ImageSpec::auto_stride(xstride, ystride, zstride, format, result_nchans,
                           xend - xbegin, yend - ybegin);

    // result_pixelsize, scanlinesize, and zplanesize assume contiguous
    // layout.  This may or may not be the same as the strides passed by
    // the caller.
    TypeDesc cachetype          = file->datatype(subimage);
    const size_t cachesize      = cachetype.size();
    const stride_t cache_stride = cachesize * cache_nchans;
    size_t formatsize           = format.size();
    stride_t result_pixelsize   = result_nchans * formatsize;
    bool xcontig                = (result_pixelsize == xstride
                    && result_nchans == cache_nchans);
    stride_t scanlinesize       = (xend - xbegin) * result_pixelsize;
    stride_t zplanesize         = (yend - ybegin) * scanlinesize;
    OIIO_DASSERT(spec.depth >= 1 && spec.tile_depth >= 1);

    imagesize_t npixelsread = 0;
    char* zptr              = (char*)result;
    for (int z = zbegin; z < zend; ++z, zptr += zstride) {
        if (z < spec.z || z >= (spec.z + spec.depth)) {
            // nonexistent planes
            if (xstride == result_pixelsize && ystride == scanlinesize) {
                // Can zero out the plane in one shot
                memset(zptr, 0, zplanesize);
            } else {
                // Non-contiguous strides -- zero out individual pixels
                char* yptr = zptr;
                for (int y = ybegin; y < yend; ++y, yptr += ystride) {
                    char* xptr = yptr;
                    for (int x = xbegin; x < xend; ++x, xptr += xstride)
                        memset(xptr, 0, result_pixelsize);
                }
            }
            continue;
        }
        int old_tx = -100000, old_ty = -100000, old_tz = -100000;
        int tz     = z - ((z - spec.z) % spec.tile_depth);
        char* yptr = zptr;
        int ty     = ybegin - ((ybegin - spec.y) % spec.tile_height);
        int tyend  = ty + spec.tile_height;
        for (int y = ybegin; y < yend; ++y, yptr += ystride) {
            if (y == tyend) {
                ty = tyend;
                tyend += spec.tile_height;
            }
            if (y < spec.y || y >= (spec.y + spec.height)) {
                // nonexistent scanlines
                if (xstride == result_pixelsize) {
                    // Can zero out the scanline in one shot
                    memset(yptr, 0, scanlinesize);
                } else {
                    // Non-contiguous strides -- zero out individual pixels
                    char* xptr = yptr;
                    for (int x = xbegin; x < xend; ++x, xptr += xstride)
                        memset(xptr, 0, result_pixelsize);
                }
                continue;
            }
            // int ty = y - ((y - spec.y) % spec.tile_height);
            char* xptr       = yptr;
            const char* data = NULL;
            for (int x = xbegin; x < xend;
                 ++x, xptr += xstride, ++npixelsread) {
                if (x < spec.x || x >= (spec.x + spec.width)) {
                    // nonexistent columns
                    memset(xptr, 0, result_pixelsize);
                    continue;
                }
                int tx = x - ((x - spec.x) % spec.tile_width);
                if (old_tx != tx || old_ty != ty || old_tz != tz) {
                    // Only do a find_tile and re-setup of the data
                    // pointer when we move across a tile boundary.
                    TileID tileid(*file, subimage, miplevel, tx, ty, tz,
                                  cache_chbegin, cache_chend);
                    ok &= find_tile(tileid, thread_info, npixelsread == 0);
                    if (!ok)
                        return false;  // Just stop if file read failed
                    old_tx = tx;
                    old_ty = ty;
                    old_tz = tz;
                    data   = NULL;
                }
                if (!data) {
                    ImageCacheTileRef& tile(thread_info->tile);
                    OIIO_DASSERT(tile);
                    data = (const char*)tile->data(x, y, z, chbegin);
                    OIIO_DASSERT(data);
                }
                if (xcontig) {
                    // Special case for a contiguous span within one tile
                    int spanend   = std::min(tx + spec.tile_width, xend);
                    stride_t span = spanend - x;
                    convert_types(cachetype, data, format, xptr,
                                  result_nchans * span);
                    x += (span - 1);
                    xptr += xstride * (span - 1);
                    // no need to increment data, since next read will
                    // be from a different tile
                } else {
                    convert_types(cachetype, data, format, xptr, result_nchans);
                    data += cache_stride;
                }
            }
        }
    }

    return ok;
}



ImageCache::Tile*
ImageCacheImpl::get_tile(ustring filename, int subimage, int miplevel, int x,
                         int y, int z, int chbegin, int chend)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file                 = find_file(filename, thread_info);
    return get_tile(file, thread_info, subimage, miplevel, x, y, z, chbegin,
                    chend);
}



ImageCache::Tile*
ImageCacheImpl::get_tile(ImageHandle* file, Perthread* thread_info,
                         int subimage, int miplevel, int x, int y, int z,
                         int chbegin, int chend)
{
    if (!thread_info)
        thread_info = get_perthread_info();
    file = verify_file(file, thread_info);
    if (!file || file->broken() || file->is_udim())
        return NULL;
    const ImageSpec& spec(file->spec(subimage, miplevel));
    // Snap x,y,z to the corner of the tile
    int xtile = (x - spec.x) / spec.tile_width;
    int ytile = (y - spec.y) / spec.tile_height;
    int ztile = (z - spec.z) / spec.tile_depth;
    x         = spec.x + xtile * spec.tile_width;
    y         = spec.y + ytile * spec.tile_height;
    z         = spec.z + ztile * spec.tile_depth;
    if (chend < chbegin) {  // chend < chbegin means "all channels."
        chbegin = 0;
        chend   = spec.nchannels;
    }
    TileID id(*file, subimage, miplevel, x, y, z, chbegin, chend);
    if (find_tile(id, thread_info, true)) {
        ImageCacheTileRef tile(thread_info->tile);
        tile->_incref();  // Fake an extra reference count
        return (ImageCache::Tile*)tile.get();
    } else {
        return NULL;
    }
}



void
ImageCacheImpl::release_tile(ImageCache::Tile* tile) const
{
    if (!tile)
        return;
    ImageCacheTileRef tileref((ImageCacheTile*)tile);
    tileref->use();
    tileref->_decref();  // Reduce ref count that we bumped in get_tile
    // when we exit scope, tileref will do the final dereference
}



TypeDesc
ImageCacheImpl::tile_format(const Tile* tile) const
{
    const TileID& id(((const ImageCacheTile*)tile)->id());
    const ImageSpec& spec(id.file().spec(id.subimage(), id.miplevel()));
    return spec.format;
}



ROI
ImageCacheImpl::tile_roi(const Tile* tile) const
{
    const TileID& id(((const ImageCacheTile*)tile)->id());
    const ImageSpec& spec(id.file().spec(id.subimage(), id.miplevel()));
    return ROI(id.x(), id.x() + spec.tile_width, id.y(),
               id.y() + spec.tile_height, id.z(), id.z() + spec.tile_depth,
               id.chbegin(), id.chend());
}



const void*
ImageCacheImpl::tile_pixels(ImageCache::Tile* tile, TypeDesc& format) const
{
    if (!tile)
        return NULL;
    ImageCacheTile* t = (ImageCacheTile*)tile;
    format            = t->file().datatype(t->id().subimage());
    return t->data();
}



bool
ImageCacheImpl::add_file(ustring filename, ImageInput::Creator creator,
                         const ImageSpec* config, bool replace)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file = find_file(filename, thread_info, creator, config,
                                     replace);
    file                 = verify_file(file, thread_info);
    if (!file || file->broken() || file->is_udim())
        return false;
    return true;
}



bool
ImageCacheImpl::add_tile(ustring filename, int subimage, int miplevel, int x,
                         int y, int z, int chbegin, int chend, TypeDesc format,
                         const void* buffer, stride_t xstride, stride_t ystride,
                         stride_t zstride, bool copy)
{
    ImageCachePerThreadInfo* thread_info = get_perthread_info();
    ImageCacheFile* file                 = find_file(filename, thread_info);
    file                                 = verify_file(file, thread_info);
    if (!file || file->broken()) {
        if (!file || file->errors_should_issue())
            error(
                "Cannot add_tile for an image file that was not set up with add_file()");
        return false;
    }
    if (file->is_udim()) {
        error("Cannot add_tile to a UDIM-like virtual file");
        return false;
    }
    if (chend < chbegin) {  // chend < chbegin means "all channels."
        chbegin = 0;
        chend   = file->spec(subimage, miplevel).nchannels;
    }
    TileID tileid(*file, subimage, miplevel, x, y, z, chbegin, chend);
    ImageCacheTileRef tile = new ImageCacheTile(tileid, buffer, format, xstride,
                                                ystride, zstride, copy);
    if (!tile || !tile->valid()) {
        if (file->errors_should_issue())
            error("Could not construct the tile; unknown reasons.");
        return false;
    }
    add_tile_to_cache(tile, thread_info);
    return true;
}



void
ImageCacheImpl::invalidate(ustring filename, bool force)
{
    ImageCacheFileRef file;
    {
        bool found = m_files.retrieve(filename, file);
        if (!found)
            return;  // no such file
    }

    if (!force) {
        // If not in force mode, we don't do anything if the modification
        // time of the file has not changed since we opened it.
        recursive_lock_guard guard(file->m_input_mutex);
        if (file->mod_time() == Filesystem::last_write_time(filename)
            && !file->broken())
            return;
    }

    // Iterate over the entire tilecache, record the TileID's of all
    // tiles that are from the file we are invalidating.
    std::vector<TileID> tiles_to_delete;
    for (TileCache::iterator tci = m_tilecache.begin(), e = m_tilecache.end();
         tci != e; ++tci) {
        if (&(*tci).second->file() == file)
            tiles_to_delete.push_back((*tci).second->id());
    }
    // N.B. at this point, we hold no locks!

    // Safely erase all the tiles we found
    for (const TileID& id : tiles_to_delete)
        m_tilecache.erase(id);

    const ustring fingerprint = file->fingerprint();

    // Invalidate the file itself (close it and clear its spec)
    file->invalidate();

    // Remove the fingerprint corresponding to this file
    {
        spin_lock lock(m_fingerprints_mutex);
        m_fingerprints.erase(fingerprint);
    }

    purge_perthread_microcaches();
}



void
ImageCacheImpl::invalidate_all(bool force)
{
    // Special case: invalidate EVERYTHING -- we can take some shortcuts
    // to do it all in one shot.
    if (force) {
        // Clear the whole tile cache
        std::vector<TileID> tiles_to_delete;
        for (TileCache::iterator t = m_tilecache.begin(), e = m_tilecache.end();
             t != e; ++t) {
            tiles_to_delete.push_back(t->second->id());
        }
        for (const TileID& id : tiles_to_delete)
            m_tilecache.erase(id);
        // Invalidate (close and clear spec) all individual files
        for (FilenameMap::iterator fileit = m_files.begin(), e = m_files.end();
             fileit != e; ++fileit) {
            fileit->second->invalidate();
        }
        // Clear fingerprints list
        clear_fingerprints();
        // Mark the per-thread microcaches as invalid
        purge_perthread_microcaches();
        return;
    }

    // Not forced... we need to look for particular files that seem
    // to need invalidation.

    // Make a list of all files that need to be invalidated
    std::vector<ustring> all_files;
    for (FilenameMap::iterator fileit = m_files.begin(), e = m_files.end();
         fileit != e; ++fileit) {
        const ImageCacheFileRef& f(fileit->second);
        ustring name = f->filename();
        Timer input_mutex_timer;
        recursive_lock_guard guard(f->m_input_mutex);
        f->m_mutex_wait_time += input_mutex_timer();
        // If the file was broken when we opened it, or if it no longer
        // exists, definitely invalidate it.
        if (f->broken() || !Filesystem::exists(name)) {
            all_files.push_back(name);
            continue;
        }
        // Invalidate the file if it has been modified since it was
        // last opened.
        std::time_t t = Filesystem::last_write_time(name);
        if (t != f->mod_time()) {
            all_files.push_back(name);
            continue;
        }
        for (int s = 0; s < f->subimages(); ++s) {
            const ImageCacheFile::SubimageInfo& sub(f->subimageinfo(s));
            // Invalidate if any unmipped subimage didn't automip but
            // automip is now on, or did automip but automip is now off.
            if (sub.unmipped
                && ((m_automip && f->miplevels(s) <= 1)
                    || (!m_automip && f->miplevels(s) > 1))) {
                all_files.push_back(name);
                break;
            }
            // Invalidate if any untiled subimage doesn't match the current
            // auto-tile setting.
            if (sub.untiled) {
                for (int m = 0, mend = f->miplevels(s); m < mend; ++m) {
                    const ImageCacheFile::LevelInfo& level(f->levelinfo(s, m));
                    if (level.spec.tile_width != m_autotile
                        || level.spec.tile_height != m_autotile) {
                        all_files.push_back(name);
                        break;
                    }
                }
            }
        }
    }

    // Now, invalidate all the files in our "needs invalidation" list
    for (auto f : all_files) {
        // fprintf (stderr, "Invalidating %s\n", f.c_str());
        invalidate(f, true);
    }

    // Mark the per-thread microcaches as invalid
    purge_perthread_microcaches();
}



void
ImageCacheImpl::close(ustring filename)
{
    auto f = m_files.find(filename);
    if (f != m_files.end())
        f->second->close();
}



void
ImageCacheImpl::close_all()
{
    for (auto& f : m_files)
        f.second->close();
}



namespace {
    // Mutex to protect all the UDIM table access.
    static mutex_pool<spin_rw_mutex, ustring, ustringHash, 8>
        udim_lookup_mutex_pool;
    // static spin_rw_mutex udim_lookup_mutex;
}  // namespace



ImageCacheFile*
ImageCacheImpl::resolve_udim(ImageCacheFile* udimfile, Perthread* thread_info,
                             float& s, float& t)
{
    // Find the u and v tile IDs, and adjust s,t to take their floors
    int utile = std::max(0, int(s));
    int vtile = std::max(0, int(t));
    s         = s - utile;
    t         = t - vtile;

    // Synthesized a single combined ID that we'll use as an index.
    uint64_t id = (uint64_t(vtile) << 32) + uint64_t(utile);

    // Which is our mutex from the pool? Use a hash baseed on the filename.
    spin_rw_mutex& udim_lookup_mutex(
        udim_lookup_mutex_pool[udimfile->filename()]);

    // First, try a read lock and see if we already have an entry
    ImageCacheFile* realfile = NULL;
    {
        spin_rw_mutex::read_lock_guard rlock(udim_lookup_mutex);
        UdimLookupMap::iterator f = udimfile->m_udim_lookup.find(id);
        if (f != udimfile->m_udim_lookup.end())
            realfile = f->second;
    }
    // If that didn't work, get a write lock and we'll make the entry for
    // the first time.
    if (!realfile) {
        // Here's the one spot where we do string manipulation -- only the
        // first time a particular tiled region is needed. Just go ahead and
        // do all possible substitutions we support!
        ustring realname = udimfile->filename();
        int udim_tile    = 1001 + utile + 10 * vtile;
        realname         = Strutil::replace(realname, "<UDIM>",
                                    Strutil::sprintf("%04d", udim_tile), true);
        realname         = Strutil::replace(realname, "<u>",
                                    Strutil::sprintf("u%d", utile), true);
        realname         = Strutil::replace(realname, "<v>",
                                    Strutil::sprintf("v%d", vtile), true);
        realname         = Strutil::replace(realname, "<U>",
                                    Strutil::sprintf("u%d", utile + 1), true);
        realname         = Strutil::replace(realname, "<V>",
                                    Strutil::sprintf("v%d", vtile + 1), true);
        realname
            = Strutil::replace(realname, "_u##v##",
                               Strutil::sprintf("_u%02dv%02d", utile, vtile),
                               true);
        realname = Strutil::replace(realname, "%(UDIM)d",
                                    Strutil::sprintf("%04d", udim_tile), true);
        realfile = find_file(realname, thread_info);
        // Now grab the actual write lock, and double check that it hasn't
        // been added by another thread during the brief time when we
        // weren't holding any lock.
        spin_rw_mutex::write_lock_guard rlock(udim_lookup_mutex);
        udimfile->m_udim_lookup.emplace(id, realfile);
    }
    return realfile;
}



ImageCachePerThreadInfo*
ImageCacheImpl::create_thread_info()
{
    ImageCachePerThreadInfo* p = new ImageCachePerThreadInfo;
    // printf ("New perthread %p\n", (void *)p);
    spin_lock lock(m_perthread_info_mutex);
    m_all_perthread_info.push_back(p);
    p->shared = true;  // both the IC and the caller point to it
    return p;
}



void
ImageCacheImpl::destroy_thread_info(ImageCachePerThreadInfo* thread_info)
{
    if (!thread_info)
        return;
    spin_lock lock(m_perthread_info_mutex);
    for (size_t i = 0; i < m_all_perthread_info.size(); ++i) {
        if (m_all_perthread_info[i] == thread_info) {
            m_all_perthread_info[i] = NULL;
            break;
        }
    }
    delete thread_info;
}



ImageCachePerThreadInfo*
ImageCacheImpl::get_perthread_info(ImageCachePerThreadInfo* p)
{
    if (!p)
        p = m_perthread_info.get();
    if (!p) {
        p = new ImageCachePerThreadInfo;
        m_perthread_info.reset(p);
        // printf ("New perthread %p\n", (void *)p);
        spin_lock lock(m_perthread_info_mutex);
        m_all_perthread_info.push_back(p);
        p->shared = true;  // both the IC and the thread point to it
    }
    if (p->purge) {  // has somebody requested a tile purge?
        // This is safe, because it's our thread.
        spin_lock lock(m_perthread_info_mutex);
        p->tile     = NULL;
        p->lasttile = NULL;
        p->purge    = 0;
        p->m_thread_files.clear();
    }
    return p;
}



void
ImageCacheImpl::erase_perthread_info()
{
    spin_lock lock(m_perthread_info_mutex);
    for (size_t i = 0; i < m_all_perthread_info.size(); ++i) {
        ImageCachePerThreadInfo* p = m_all_perthread_info[i];
        if (p) {
            // Clear the microcache.
            p->tile     = NULL;
            p->lasttile = NULL;
            if (p->shared) {
                // Pointed to by both thread-specific-ptr and our list.
                // Just remove from out list, then ownership is only
                // by the thread-specific-ptr.
                p->shared = false;
            } else {
                // Only pointed to by us -- delete it!
                delete p;
            }
            m_all_perthread_info[i] = NULL;
        }
    }
}



void
ImageCacheImpl::cleanup_perthread_info(ImageCachePerThreadInfo* p)
{
    spin_lock lock(m_perthread_info_mutex);
    if (p) {
        // Clear the microcache.
        p->tile     = NULL;
        p->lasttile = NULL;
        if (!p->shared)  // If we own it, delete it
            delete p;
        else
            p->shared = false;  // thread disappearing, no longer shared
    }
}



void
ImageCacheImpl::purge_perthread_microcaches()
{
    // Mark the per-thread microcaches as invalid
    spin_lock lock(m_perthread_info_mutex);
    for (size_t i = 0, e = m_all_perthread_info.size(); i < e; ++i)
        if (m_all_perthread_info[i])
            m_all_perthread_info[i]->purge = 1;
}



bool
ImageCacheImpl::has_error() const
{
    std::string* errptr = m_errormessage.get();
    return (errptr && errptr->size());
}



std::string
ImageCacheImpl::geterror(bool clear) const
{
    std::string e;
    std::string* errptr = m_errormessage.get();
    if (errptr) {
        e = *errptr;
        if (clear)
            errptr->clear();
    }
    return e;
}



void
ImageCacheImpl::append_error(string_view message) const
{
    if (message.size() && message.back() == '\n')
        message.remove_suffix(1);
    std::string* errptr = m_errormessage.get();
    if (!errptr) {
        errptr = new std::string;
        m_errormessage.reset(errptr);
    }
    OIIO_DASSERT(errptr != NULL);
    OIIO_DASSERT(
        errptr->size() < 1024 * 1024 * 16
        && "Accumulated error messages > 16MB. Try checking return codes!");
    if (errptr->size() && errptr->back() != '\n')
        *errptr += '\n';
    *errptr += message;
}



}  // end namespace pvt



ImageCache*
ImageCache::create(bool shared)
{
    if (shared) {
        // They requested a shared cache.  If a shared cache already
        // exists, just return it, otherwise record the new cache.
        spin_lock guard(shared_image_cache_mutex);
        if (!shared_image_cache.get())
            shared_image_cache.reset(aligned_new<ImageCacheImpl>(),
                                     aligned_delete<ImageCacheImpl>);

#if 0
        std::cerr << " shared ImageCache is "
                  << (void *)shared_image_cache.get() << "\n";
#endif
        return shared_image_cache.get();
    }

    // Doesn't need a shared cache
    ImageCacheImpl* ic = aligned_new<ImageCacheImpl>();
#if 0
    std::cerr << "creating new ImageCache " << (void *)ic << "\n";
#endif
    return ic;
}



void
ImageCache::destroy(ImageCache* x, bool teardown)
{
    if (!x)
        return;
    spin_lock guard(shared_image_cache_mutex);
    if (x == shared_image_cache.get()) {
        // This is the shared cache, so don't really delete it. Invalidate
        // it fully, closing the files and throwing out any tiles that
        // nobody is currently holding references to.  But only delete the
        // IC fully if 'teardown' is true, and even then, it won't destroy
        // until nobody else is still holding a shared_ptr to it.
        ((ImageCacheImpl*)x)->invalidate_all(teardown);
        if (teardown)
            shared_image_cache.reset();
    } else {
        // Not a shared cache, we are the only owner, so truly destroy it.
        aligned_delete(x);
    }
}


OIIO_NAMESPACE_END
