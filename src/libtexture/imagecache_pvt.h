// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


/// \file
/// Non-public classes used internally by ImgeCacheImpl.


#ifndef OPENIMAGEIO_IMAGECACHE_PVT_H
#define OPENIMAGEIO_IMAGECACHE_PVT_H

#include <tsl/robin_map.h>

#include <boost/container/flat_map.hpp>
#include <boost/thread/tss.hpp>

#include <OpenImageIO/export.h>
#include <OpenImageIO/hash.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/refcnt.h>
#include <OpenImageIO/texture.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/unordered_map_concurrent.h>


OIIO_NAMESPACE_BEGIN

namespace pvt {

#ifndef NDEBUG
#    define IMAGECACHE_TIME_STATS 1
#else
// Change the following to 1 to get timing statistics even for
// optimized runs.  Note that this has some performance penalty.
#    define IMAGECACHE_TIME_STATS 0
#endif

#define IMAGECACHE_USE_RW_MUTEX 1

#define FILE_CACHE_SHARDS 64
#define TILE_CACHE_SHARDS 128

using boost::thread_specific_ptr;

class ImageCacheImpl;
class ImageCachePerThreadInfo;

const char*
texture_format_name(TexFormat f);
const char*
texture_type_name(TexFormat f);

#ifdef BOOST_CONTAINER_FLAT_MAP_HPP
typedef boost::container::flat_map<uint64_t, ImageCacheFile*> UdimLookupMap;
#else
typedef unordered_map<uint64_t, ImageCacheFile*> UdimLookupMap;
#endif



/// Structure to hold IC and TS statistics.  We combine into a single
/// structure to minimize the number of costly thread_specific_ptr
/// retrievals.  If somebody is using the ImageCache without a
/// TextureSystem, a few extra stats come along for the ride, but this
/// has no performance penalty.
struct ImageCacheStatistics {
    // First, the ImageCache-specific fields:
    long long find_tile_calls;
    long long find_tile_microcache_misses;
    int find_tile_cache_misses;
    long long files_totalsize;
    long long files_totalsize_ondisk;
    long long bytes_read;
    // These stats are hard to deal with on a per-thread basis, so for
    // now, they are still atomics shared by the whole IC.
    // int tiles_created;
    // int tiles_current;
    // int tiles_peak;
    // int open_files_created;
    // int open_files_current;
    // int open_files_peak;
    int unique_files;
    double fileio_time;
    double fileopen_time;
    double file_locking_time;
    double tile_locking_time;
    double find_file_time;
    double find_tile_time;

    // TextureSystem-specific fields below:
    long long texture_queries;
    long long texture_batches;
    long long texture3d_queries;
    long long texture3d_batches;
    long long shadow_queries;
    long long shadow_batches;
    long long environment_queries;
    long long environment_batches;
    long long imageinfo_queries;
    long long aniso_queries;
    long long aniso_probes;
    float max_aniso;
    long long closest_interps;
    long long bilinear_interps;
    long long cubic_interps;
    int file_retry_success;
    int tile_retry_success;

    ImageCacheStatistics() { init(); }
    void init();
    void merge(const ImageCacheStatistics& s);
};



/// Unique in-memory record for each image file on disk.  Note that
/// this class is not in and of itself thread-safe.  It's critical that
/// any calling routine use a mutex any time a ImageCacheFile's methods are
/// being called, including constructing a new ImageCacheFile.
///
/// The public routines of ImageCacheFile are thread-safe!  In
/// particular, callers do not need to lock around calls to read_tile.
/// However, a few of them require passing in a pointer to the
/// thread-specific IC data including microcache and statistics.
///
class OIIO_API ImageCacheFile final : public RefCnt {
public:
    ImageCacheFile(ImageCacheImpl& imagecache,
                   ImageCachePerThreadInfo* thread_info, ustring filename,
                   ImageInput::Creator creator = nullptr,
                   const ImageSpec* config     = nullptr);
    ~ImageCacheFile();

    void reset(ImageInput::Creator creator, const ImageSpec* config);
    bool broken() const { return m_broken; }
    int subimages() const { return (int)m_subimages.size(); }
    int miplevels(int subimage) const
    {
        return (int)m_subimages[subimage].levels.size();
    }
    const ImageSpec& spec(int subimage, int miplevel) const
    {
        return levelinfo(subimage, miplevel).spec;
    }
    ImageSpec& spec(int subimage, int miplevel)
    {
        return levelinfo(subimage, miplevel).spec;
    }
    const ImageSpec& nativespec(int subimage, int miplevel) const
    {
        return levelinfo(subimage, miplevel).nativespec;
    }
    ustring filename(void) const { return m_filename; }
    ustring fileformat(void) const { return m_fileformat; }
    TexFormat textureformat() const { return m_texformat; }
    TextureOpt::Wrap swrap() const { return m_swrap; }
    TextureOpt::Wrap twrap() const { return m_twrap; }
    TextureOpt::Wrap rwrap() const { return m_rwrap; }
    TypeDesc datatype(int subimage) const
    {
        return m_subimages[subimage].datatype;
    }
    ImageCacheImpl& imagecache() const { return m_imagecache; }
    ImageInput::Creator creator() const { return m_inputcreator; }

    /// Load new data tile
    ///
    bool read_tile(ImageCachePerThreadInfo* thread_info, int subimage,
                   int miplevel, int x, int y, int z, int chbegin, int chend,
                   TypeDesc format, void* data);

    /// Mark the file as recently used.
    ///
    void use(void) { m_used = true; }

    /// Try to release resources for this file -- if recently used, mark
    /// as not recently used; if already not recently used, close the
    /// file and return true.
    void release(void);

    size_t channelsize(int subimage) const
    {
        return m_subimages[subimage].channelsize;
    }
    size_t pixelsize(int subimage) const
    {
        return m_subimages[subimage].pixelsize;
    }
    TypeDesc::BASETYPE pixeltype(int subimage) const
    {
        return (TypeDesc::BASETYPE)m_subimages[subimage].datatype.basetype;
    }
    bool mipused(void) const { return m_mipused; }
    bool sample_border(void) const { return m_sample_border; }
    bool is_udim(void) const { return m_is_udim; }
    const std::vector<size_t>& mipreadcount(void) const
    {
        return m_mipreadcount;
    }

    void invalidate();

    size_t timesopened() const { return m_timesopened; }
    size_t tilesread() const { return m_tilesread; }
    imagesize_t bytesread() const { return m_bytesread; }
    double& iotime() { return m_iotime; }
    size_t redundant_tiles() const { return (size_t)m_redundant_tiles.load(); }
    imagesize_t redundant_bytesread() const
    {
        return (imagesize_t)m_redundant_bytesread.load();
    }
    void register_redundant_tile(imagesize_t bytesread)
    {
        m_redundant_tiles += 1;
        m_redundant_bytesread += (long long)bytesread;
    }

    std::time_t mod_time() const { return m_mod_time; }
    ustring fingerprint() const { return m_fingerprint; }
    void duplicate(ImageCacheFile* dup) { m_duplicate = dup; }
    ImageCacheFile* duplicate() const { return m_duplicate; }

    // Retrieve the average color, or try to compute it. Return true on
    // success, false on failure.
    bool get_average_color(float* avg, int subimage, int chbegin, int chend);

    /// Info for each MIP level that isn't in the ImageSpec, or that we
    /// precompute.
    struct LevelInfo {
        ImageSpec spec;         ///< ImageSpec for the mip level
        ImageSpec nativespec;   ///< Native ImageSpec for the mip level
        bool full_pixel_range;  ///< pixel data window matches image window
        bool onetile;           ///< Whole level fits on one tile
        mutable bool polecolorcomputed;        ///< Pole color was computed
        mutable std::vector<float> polecolor;  ///< Pole colors
        int nxtiles, nytiles, nztiles;  ///< Number of tiles in each dimension
        atomic_ll* tiles_read;  ///< Bitfield for tiles read at least once
        LevelInfo(const ImageSpec& spec,
                  const ImageSpec& nativespec);  ///< Initialize based on spec
        LevelInfo(const LevelInfo& src);         // needed for vector<LevelInfo>
        ~LevelInfo() { delete[] tiles_read; }
    };

    /// Info for each subimage
    ///
    struct SubimageInfo {
        std::vector<LevelInfo> levels;  ///< Extra per-level info
        TypeDesc datatype;              ///< Type of pixels we store internally
        unsigned int channelsize = 0;   ///< Channel size, in bytes
        unsigned int pixelsize   = 0;   ///< Pixel size, in bytes
        bool untiled             = false;  ///< Not tiled
        bool unmipped            = false;  ///< Not really MIP-mapped
        bool volume              = false;  ///< It's a volume image
        bool autotiled           = false;  ///< We are autotiling this image
        bool full_pixel_range
            = false;  ///< pixel data window matches image window
        bool is_constant_image = false;    ///< Is the image a constant color?
        bool has_average_color = false;    ///< We have an average color
        std::vector<float> average_color;  ///< Average color
        spin_mutex average_color_mutex;    ///< protect average_color
        std::unique_ptr<Imath::M44f> Mlocal;  ///< shadows/volumes: world-to-local
        // The scale/offset accounts for crops or overscans, converting
        // 0-1 texture space relative to the "display/full window" into
        // 0-1 relative to the "pixel window".
        float sscale = 1.0f, soffset = 0.0f;
        float tscale = 1.0f, toffset = 0.0f;
        int min_mip_level = 0;  // Start with this MIP
        ustring subimagename;

        SubimageInfo() {}
        void init(ImageCacheFile& icfile, const ImageSpec& spec,
                  bool forcefloat);
        ImageSpec& spec(int m) { return levels[m].spec; }
        const ImageSpec& spec(int m) const { return levels[m].spec; }
        const ImageSpec& nativespec(int m) const
        {
            return levels[m].nativespec;
        }
        int miplevels() const { return (int)levels.size(); }
    };

    const SubimageInfo& subimageinfo(int subimage) const
    {
        return m_subimages[subimage];
    }

    SubimageInfo& subimageinfo(int subimage) { return m_subimages[subimage]; }

    const LevelInfo& levelinfo(int subimage, int miplevel) const
    {
        OIIO_DASSERT((int)m_subimages.size() > subimage);
        OIIO_DASSERT((int)m_subimages[subimage].levels.size() > miplevel);
        return m_subimages[subimage].levels[miplevel];
    }
    LevelInfo& levelinfo(int subimage, int miplevel)
    {
        OIIO_DASSERT((int)m_subimages.size() > subimage);
        OIIO_DASSERT((int)m_subimages[subimage].levels.size() > miplevel);
        return m_subimages[subimage].levels[miplevel];
    }

    /// Do we currently have a valid spec?
    bool validspec() const
    {
        OIIO_DASSERT((m_validspec == false || m_subimages.size() > 0)
                     && "validspec is true, but subimages are empty");
        return m_validspec;
    }

    /// Forget the specs we know
    void invalidate_spec()
    {
        m_validspec = false;
        m_subimages.clear();
    }

    /// Should we print an error message? Keeps track of whether the
    /// number of errors so far, including this one, is a above the limit
    /// set for errors to print for each file.
    int errors_should_issue() const;

    /// Mark the file as "not broken"
    void mark_not_broken();

    /// Mark the file as "broken" with an error message, and send the error
    /// message to the imagecache.
    void mark_broken(string_view error);

    /// Return the error message that explains why the file is broken.
    string_view broken_error_message() const { return m_broken_message; }

private:
    ustring m_filename_original;   ///< original filename before search path
    ustring m_filename;            ///< Filename
    bool m_used;                   ///< Recently used (in the LRU sense)
    bool m_broken;                 ///< has errors; can't be used properly
    bool m_allow_release = true;   ///< Allow the file to release()?
    std::string m_broken_message;  ///< Error message for why it's broken
    std::shared_ptr<ImageInput> m_input;  ///< Open ImageInput, NULL if closed
        // Note that m_input, the shared pointer itself, is NOT safe to
        // access directly. ALWAYS retrieve its value with get_imageinput
        // (it's thread-safe to use that result) and set its value with
        // get_imageinput -- those are guaranteed thread-safe.
    std::vector<SubimageInfo> m_subimages;  ///< Info on each subimage
    TexFormat m_texformat;                  ///< Which texture format
    TextureOpt::Wrap m_swrap;               ///< Default wrap modes
    TextureOpt::Wrap m_twrap;               ///< Default wrap modes
    TextureOpt::Wrap m_rwrap;               ///< Default wrap modes
    EnvLayout m_envlayout;                  ///< env map: which layout?
    bool m_y_up;                  ///< latlong: is y "up"? (else z is up)
    bool m_sample_border;         ///< are edge samples exactly on the border?
    bool m_is_udim;               ///< Is tiled/UDIM?
    ustring m_fileformat;         ///< File format name
    size_t m_tilesread;           ///< Tiles read from this file
    imagesize_t m_bytesread;      ///< Bytes read from this file
    atomic_ll m_redundant_tiles;  ///< Redundant tile reads
    atomic_ll m_redundant_bytesread;     ///< Redundant bytes read
    size_t m_timesopened;                ///< Separate times we opened this file
    double m_iotime;                     ///< I/O time for this file
    double m_mutex_wait_time;            ///< Wait time for m_input_mutex
    bool m_mipused;                      ///< MIP level >0 accessed
    volatile bool m_validspec;           ///< If false, reread spec upon open
    mutable int m_errors_issued;         ///< Errors issued for this file
    std::vector<size_t> m_mipreadcount;  ///< Tile reads per mip level
    ImageCacheImpl& m_imagecache;        ///< Back pointer for ImageCache
    mutable recursive_mutex m_input_mutex;  ///< Mutex protecting the ImageInput
    std::time_t m_mod_time;                 ///< Time file was last updated
    ustring m_fingerprint;          ///< Optional cryptographic fingerprint
    ImageCacheFile* m_duplicate;    ///< Is this a duplicate?
    imagesize_t m_total_imagesize;  ///< Total size, uncompressed
    imagesize_t m_total_imagesize_ondisk;  ///< Total size, compressed on disk
    ImageInput::Creator m_inputcreator;    ///< Custom ImageInput-creator
    std::unique_ptr<ImageSpec> m_configspec;  // Optional configuration hints
    UdimLookupMap m_udim_lookup;              ///< Used for decoding udim tiles
                                              // protected by mutex elsewhere!

    // Thread-safe retrieve a shared pointer to the ImageInput (which may
    // not currently be open). The one returned is safe to use as long as
    // the caller is holding the shared_ptr.
    std::shared_ptr<ImageInput>
    get_imageinput(ImageCachePerThreadInfo* thread_info);

    // Safely replace the existing ImageInput shared pointer with the one in
    // newval. Ensure that the cache still knows how many open ImageInputs
    // there are in total.
    void set_imageinput(std::shared_ptr<ImageInput> newval);

    /// Retrieve a shared pointer to the file's open ImageInput (opening if
    /// necessary, and maintaining the limit on number of open files). For a
    /// broken file, return an empty shared ptr. This is thread-safe and
    /// requires no external lock.
    std::shared_ptr<ImageInput> open(ImageCachePerThreadInfo* thread_info);

    /// Release the ImageInput, if currently open. It will close and destroy
    /// when the last thread holding it is done with its shared ptr. This
    /// is thread-safe, no need to hold a lock to call it. It will close the
    /// open file, but it doesn't change the fact that this ImageCacheFile
    /// is a valid descriptor of the image file.
    void close(void);

    /// Load the requested tile, from a file that's not really tiled.
    /// Preconditions: the ImageInput is already opened, and we already did
    /// a seek_subimage to the right subimage and MIP level.
    bool read_untiled(ImageCachePerThreadInfo* thread_info, ImageInput* inp,
                      int subimage, int miplevel, int x, int y, int z,
                      int chbegin, int chend, TypeDesc format, void* data);

    /// Load the requested tile, from a file that's not really MIPmapped.
    /// Preconditions: the ImageInput is already opened, and we already did
    /// a seek_subimage to the right subimage.
    bool read_unmipped(ImageCachePerThreadInfo* thread_info, int subimage,
                       int miplevel, int x, int y, int z, int chbegin,
                       int chend, TypeDesc format, void* data);

    // Initialize a bunch of fields based on the ImageSpec.
    // FIXME -- this is actually deeply flawed, many of these things only
    // make sense if they are per subimage, not one value for the whole
    // file. But it will require a bigger refactor to fix that.
    void init_from_spec();

    friend class ImageCacheImpl;
    friend class TextureSystemImpl;
    friend struct SubimageInfo;
};



/// Reference-counted pointer to a ImageCacheFile
///
typedef intrusive_ptr<ImageCacheFile> ImageCacheFileRef;


/// Map file names to file references
typedef unordered_map_concurrent<
    ustring, ImageCacheFileRef, ustringHash, std::equal_to<ustring>,
    FILE_CACHE_SHARDS, tsl::robin_map<ustring, ImageCacheFileRef, ustringHash>>
    FilenameMap;
typedef tsl::robin_map<ustring, ImageCacheFileRef, ustringHash> FingerprintMap;



/// Compact identifier for a particular tile of a particular image
///
struct TileID {
    /// Default constructor
    ///
    TileID()
        : m_file(nullptr)
    {
    }

    /// Initialize a TileID based on full elaboration of image file,
    /// subimage, and tile x,y,z indices.
    TileID(ImageCacheFile& file, int subimage, int miplevel, int x, int y,
           int z = 0, int chbegin = 0, int chend = -1)
        : m_x(x)
        , m_y(y)
        , m_z(z)
        , m_subimage(subimage)
        , m_miplevel(miplevel)
        , m_chbegin(chbegin)
        , m_chend(chend)
        , m_file(&file)
    {
        int nc = file.spec(subimage, miplevel).nchannels;
        if (chend < chbegin || chend > nc)
            m_chend = nc;
    }

    /// Destructor is trivial, because we don't hold any resources
    /// of our own.  This is by design.
    ~TileID() {}

    ImageCacheFile& file(void) const { return *m_file; }
    ImageCacheFile* file_ptr(void) const { return m_file; }
    int subimage(void) const { return m_subimage; }
    int miplevel(void) const { return m_miplevel; }
    int x(void) const { return m_x; }
    int y(void) const { return m_y; }
    int z(void) const { return m_z; }
    int chbegin() const { return m_chbegin; }
    int chend() const { return m_chend; }
    int nchannels() const { return m_chend - m_chbegin; }

    void x(int v) { m_x = v; }
    void y(int v) { m_y = v; }
    void z(int v) { m_z = v; }
    void xy(int x, int y)
    {
        m_x = x;
        m_y = y;
    }
    void xyz(int x, int y, int z)
    {
        m_x = x;
        m_y = y;
        m_z = z;
    }

    /// Is this an uninitialized tileID?
    bool empty() const { return m_file == nullptr; }

    /// Do the two ID's refer to the same tile?
    ///
    friend bool equal(const TileID& a, const TileID& b)
    {
        // Try to speed up by comparing field by field in order of most
        // probable rejection if they really are unequal.
        return (a.m_x == b.m_x && a.m_y == b.m_y && a.m_z == b.m_z
                && a.m_subimage == b.m_subimage && a.m_miplevel == b.m_miplevel
                && (a.m_file == b.m_file) && a.m_chbegin == b.m_chbegin
                && a.m_chend == b.m_chend);
    }

    /// Do the two ID's refer to the same tile?
    ///
    bool operator==(const TileID& b) const { return equal(*this, b); }
    bool operator!=(const TileID& b) const { return !equal(*this, b); }

    /// Digest the TileID into a size_t to use as a hash key.
    size_t hash() const
    {
        const uint64_t a = (uint64_t(m_x) << 32) + uint64_t(m_y);
        const uint64_t b = (uint64_t(m_z) << 32) + uint64_t(m_subimage);
        const uint64_t c = (uint64_t(m_miplevel) << 32)
                           + (uint64_t(m_chbegin) << 16) + uint64_t(m_chend);
        const uint64_t d = m_file->filename().hash();
        return fasthash::fasthash64({ a, b, c, d });
    }

    /// Functor that hashes a TileID
    struct Hasher {
        size_t operator()(const TileID& a) const { return a.hash(); }
    };

    friend std::ostream& operator<<(std::ostream& o, const TileID& id)
    {
        return (o << "{xyz=" << id.m_x << ',' << id.m_y << ',' << id.m_z
                  << ", sub=" << id.m_subimage << ", mip=" << id.m_miplevel
                  << ", chans=[" << id.chbegin() << "," << id.chend() << ")"
                  << ' '
                  << (id.m_file ? ustring("nofile") : id.m_file->filename())
                  << '}');
    }

private:
    int m_x, m_y, m_z;         ///< x,y,z tile index within the subimage
    int m_subimage;            ///< subimage
    int m_miplevel;            ///< MIP-map level
    short m_chbegin, m_chend;  ///< Channel range
    ImageCacheFile* m_file;    ///< Which ImageCacheFile we refer to
};



/// Record for a single image tile.
///
class ImageCacheTile final : public RefCnt {
public:
    /// Construct a new tile, pixels will be read when calling read()
    ImageCacheTile(const TileID& id);

    /// Construct a new tile out of the pixels supplied.
    ///
    ImageCacheTile(const TileID& id, const void* pels, TypeDesc format,
                   stride_t xstride, stride_t ystride, stride_t zstride,
                   bool copy = true);

    ~ImageCacheTile();

    /// Actually read the pixels.  The caller had better be the thread
    /// that constructed the tile.
    void read(ImageCachePerThreadInfo* thread_info);

    /// Return pointer to the raw pixel data
    const void* data(void) const { return &m_pixels[0]; }

    /// Return pointer to the pixel data for a particular pixel.  Be
    /// extremely sure the pixel is within this tile!
    const void* data(int x, int y, int z, int c) const;

    /// Return pointer to the floating-point pixel data
    const float* floatdata(void) const { return (const float*)&m_pixels[0]; }

    /// Return a pointer to the character data
    const unsigned char* bytedata(void) const
    {
        return (unsigned char*)&m_pixels[0];
    }

    /// Return a pointer to unsigned short data
    const unsigned short* ushortdata(void) const
    {
        return (unsigned short*)&m_pixels[0];
    }

    /// Return a pointer to half data
    const half* halfdata(void) const { return (half*)&m_pixels[0]; }

    /// Return the id for this tile.
    ///
    const TileID& id(void) const { return m_id; }

    const ImageCacheFile& file() const { return m_id.file(); }

    /// Return the actual allocated memory size for this tile's pixels.
    ///
    size_t memsize() const { return m_pixels_size; }

    /// Return the space that will be needed for this tile's pixels.
    ///
    size_t memsize_needed() const;

    /// Mark the tile as recently used.
    ///
    void use() { m_used = 1; }

    /// Mark the tile as not recently used, return its previous value.
    ///
    bool release()
    {
        if (!pixels_ready() || !valid())
            return true;  // Don't really release invalid or unready tiles
        // If m_used is 1, set it to zero and return true.  If it was already
        // zero, it's fine and return false.
        int one = 1;
        return m_used.compare_exchange_strong(one, 0);
    }

    /// Has this tile been recently used?
    ///
    int used(void) const { return m_used; }

    bool valid(void) const { return m_valid; }

    /// Are the pixels ready for use?  If false, they're still being
    /// read from disk.
    bool pixels_ready() const { return m_pixels_ready; }

    /// Spin until the pixels have been read and are ready for use.
    ///
    void wait_pixels_ready() const;

    int channelsize() const { return m_channelsize; }
    int pixelsize() const { return m_pixelsize; }

private:
    TileID m_id;                       ///< ID of this tile
    std::unique_ptr<char[]> m_pixels;  ///< The pixel data
    size_t m_pixels_size { 0 };        ///< How much m_pixels has allocated
    int m_channelsize { 0 };           ///< How big is each channel (bytes)
    int m_pixelsize { 0 };             ///< How big is each pixel (bytes)
    bool m_valid { false };            ///< Valid pixels
    bool m_nofree { false };  ///< We do NOT own the pixels, do not free!
    volatile bool m_pixels_ready {
        false
    };                        ///< The pixels have been read from disk
    atomic_int m_used { 1 };  ///< Used recently
};



/// Reference-counted pointer to a ImageCacheTile
///
typedef intrusive_ptr<ImageCacheTile> ImageCacheTileRef;



/// Hash table that maps TileID to ImageCacheTileRef -- this is the type of the
/// main tile cache.
typedef unordered_map_concurrent<
    TileID, ImageCacheTileRef, TileID::Hasher, std::equal_to<TileID>,
    TILE_CACHE_SHARDS, tsl::robin_map<TileID, ImageCacheTileRef, TileID::Hasher>>
    TileCache;


/// A very small amount of per-thread data that saves us from locking
/// the mutex quite as often.  We store things here used by both
/// ImageCache and TextureSystem, so they don't each need a costly
/// thread_specific_ptr retrieval.  There's no real penalty for this,
/// even if you are using only ImageCache but not TextureSystem.
class ImageCachePerThreadInfo {
public:
    // Keep a per-thread unlocked map of filenames to ImageCacheFile*'s.
    // Fall back to the shared map only when not found locally.
    // This is safe because no ImageCacheFile is ever truly deleted from
    // the shared map, so this map isn't the owner.
    using ThreadFilenameMap
        = tsl::robin_map<ustring, ImageCacheFile*, ustringHash>;
    ThreadFilenameMap m_thread_files;

    // We have a two-tile "microcache", storing the last two tiles needed.
    ImageCacheTileRef tile, lasttile;
    atomic_int purge;  // If set, tile ptrs need purging!
    ImageCacheStatistics m_stats;
    bool shared = false;  // Pointed to by the IC and thread_specific_ptr

    ImageCachePerThreadInfo()
    {
        // std::cout << "Creating PerThreadInfo " << (void*)this << "\n";
        purge = 0;
    }

    ~ImageCachePerThreadInfo()
    {
        // std::cout << "Destroying PerThreadInfo " << (void*)this << "\n";
    }

    // Add a new filename/fileptr pair to our microcache
    void remember_filename(ustring n, ImageCacheFile* f)
    {
        m_thread_files.emplace(n, f);
    }

    // See if a filename has a fileptr in the microcache
    ImageCacheFile* find_file(ustring n) const
    {
        auto f = m_thread_files.find(n);
        return f == m_thread_files.end() ? nullptr : f->second;
    }
};



/// Working implementation of the abstract ImageCache class.
///
/// Some of the methods require a pointer to the thread-specific IC data
/// including microcache and statistics.
///
class ImageCacheImpl final : public ImageCache {
public:
    ImageCacheImpl();
    virtual ~ImageCacheImpl();

    virtual bool attribute(string_view name, TypeDesc type, const void* val);
    virtual bool attribute(string_view name, int val)
    {
        return attribute(name, TypeDesc::INT, &val);
    }
    virtual bool attribute(string_view name, float val)
    {
        return attribute(name, TypeDesc::FLOAT, &val);
    }
    virtual bool attribute(string_view name, double val)
    {
        float f = (float)val;
        return attribute(name, TypeDesc::FLOAT, &f);
    }
    virtual bool attribute(string_view name, string_view val)
    {
        const char* s = val.c_str();
        return attribute(name, TypeDesc::STRING, &s);
    }

    virtual bool getattribute(string_view name, TypeDesc type, void* val) const;
    virtual bool getattribute(string_view name, int& val) const
    {
        return getattribute(name, TypeDesc::INT, &val);
    }
    virtual bool getattribute(string_view name, float& val) const
    {
        return getattribute(name, TypeDesc::FLOAT, &val);
    }
    virtual bool getattribute(string_view name, double& val) const
    {
        float f;
        bool ok = getattribute(name, TypeDesc::FLOAT, &f);
        if (ok)
            val = f;
        return ok;
    }
    virtual bool getattribute(string_view name, char** val) const
    {
        return getattribute(name, TypeDesc::STRING, val);
    }
    virtual bool getattribute(string_view name, std::string& val) const
    {
        ustring s;
        bool ok = getattribute(name, TypeDesc::STRING, &s);
        if (ok)
            val = s.string();
        return ok;
    }


    // Retrieve options
    int max_open_files() const { return m_max_open_files; }
    const std::string& searchpath() const { return m_searchpath; }
    const std::string& plugin_searchpath() const { return m_plugin_searchpath; }
    int autotile() const { return m_autotile; }
    bool autoscanline() const { return m_autoscanline; }
    bool automip() const { return m_automip; }
    bool forcefloat() const { return m_forcefloat; }
    bool accept_untiled() const { return m_accept_untiled; }
    bool accept_unmipped() const { return m_accept_unmipped; }
    bool unassociatedalpha() const { return m_unassociatedalpha; }
    bool trust_file_extensions() const { return m_trust_file_extensions; }
    int failure_retries() const { return m_failure_retries; }
    bool latlong_y_up_default() const { return m_latlong_y_up_default; }
    void get_commontoworld(Imath::M44f& result) const { result = m_Mc2w; }
    int max_errors_per_file() const { return m_max_errors_per_file; }

    virtual std::string resolve_filename(const std::string& filename) const;

    // Set m_max_open_files, with logic to try to clamp reasonably.
    void set_max_open_files(int m);

    /// Get information about the given image.
    ///
    virtual bool get_image_info(ustring filename, int subimage, int miplevel,
                                ustring dataname, TypeDesc datatype,
                                void* data);
    virtual bool get_image_info(ImageCacheFile* file,
                                ImageCachePerThreadInfo* thread_info,
                                int subimage, int miplevel, ustring dataname,
                                TypeDesc datatype, void* data);

    /// Get the ImageSpec associated with the named image.  If the file
    /// is found and is an image format that can be read, store a copy
    /// of its specification in spec and return true.  Return false if
    /// the file was not found or could not be opened as an image file
    /// by any available ImageIO plugin.
    virtual bool get_imagespec(ustring filename, ImageSpec& spec,
                               int subimage = 0, int miplevel = 0,
                               bool native = false);
    virtual bool get_imagespec(ImageCacheFile* file,
                               ImageCachePerThreadInfo* thread_info,
                               ImageSpec& spec, int subimage = 0,
                               int miplevel = 0, bool native = false);

    virtual const ImageSpec* imagespec(ustring filename, int subimage = 0,
                                       int miplevel = 0, bool native = false);
    virtual const ImageSpec*
    imagespec(ImageCacheFile* file, ImageCachePerThreadInfo* thread_info = NULL,
              int subimage = 0, int miplevel = 0, bool native = false);

    virtual bool get_thumbnail(ustring filename, ImageBuf& thumbnail,
                               int subimage = 0);
    virtual bool get_thumbnail(ImageHandle* file, Perthread* thread_info,
                               ImageBuf& thumbnail, int subimage = 0);

    // Retrieve a rectangle of raw unfiltered pixels.
    virtual bool get_pixels(ustring filename, int subimage, int miplevel,
                            int xbegin, int xend, int ybegin, int yend,
                            int zbegin, int zend, TypeDesc format,
                            void* result);
    virtual bool get_pixels(ImageCacheFile* file,
                            ImageCachePerThreadInfo* thread_info, int subimage,
                            int miplevel, int xbegin, int xend, int ybegin,
                            int yend, int zbegin, int zend, TypeDesc format,
                            void* result);
    virtual bool get_pixels(ustring filename, int subimage, int miplevel,
                            int xbegin, int xend, int ybegin, int yend,
                            int zbegin, int zend, int chbegin, int chend,
                            TypeDesc format, void* result,
                            stride_t xstride  = AutoStride,
                            stride_t ystride  = AutoStride,
                            stride_t zstride  = AutoStride,
                            int cache_chbegin = 0, int cache_chend = -1);
    virtual bool
    get_pixels(ImageCacheFile* file, ImageCachePerThreadInfo* thread_info,
               int subimage, int miplevel, int xbegin, int xend, int ybegin,
               int yend, int zbegin, int zend, int chbegin, int chend,
               TypeDesc format, void* result, stride_t xstride = AutoStride,
               stride_t ystride = AutoStride, stride_t zstride = AutoStride,
               int cache_chbegin = 0, int cache_chend = -1);

    /// Find the ImageCacheFile record for the named image, or NULL if
    /// no such file can be found.  This returns a plain old pointer,
    /// which is ok because the file hash table has ref-counted pointers
    /// and those won't be freed until the texture system is destroyed.
    /// If header_only is true, we are finding the file only for the sake
    /// of header information (e.g., called by get_image_info).
    /// A call to verify_file() is still needed after find_file().
    ImageCacheFile* find_file(ustring filename,
                              ImageCachePerThreadInfo* thread_info,
                              ImageInput::Creator creator = nullptr,
                              const ImageSpec* config     = nullptr,
                              bool replace                = false);

    /// Verify & prep the ImageCacheFile record for the named image,
    /// return the pointer (which may have changed for deduplication),
    /// or NULL if no such file can be found. This returns a plain old
    /// pointer, which is ok because the file hash table has ref-counted
    /// pointers and those won't be freed until the texture system is
    /// destroyed.  If header_only is true, we are finding the file only
    /// for the sake of header information (e.g., called by
    /// get_image_info).
    ImageCacheFile* verify_file(ImageCacheFile* tf,
                                ImageCachePerThreadInfo* thread_info,
                                bool header_only = false);

    virtual ImageCacheFile*
    get_image_handle(ustring filename,
                     ImageCachePerThreadInfo* thread_info = NULL)
    {
        if (!thread_info)
            thread_info = get_perthread_info();
        ImageCacheFile* file = find_file(filename, thread_info);
        return verify_file(file, thread_info);
    }

    virtual bool good(ImageCacheFile* handle)
    {
        return handle && !handle->broken();
    }

    /// Is the tile specified by the TileID already in the cache?
    bool tile_in_cache(const TileID& id,
                       ImageCachePerThreadInfo* /*thread_info*/)
    {
        TileCache::iterator found = m_tilecache.find(id);
        return (found != m_tilecache.end());
    }

    /// Add the tile to the cache.  This will also enforce cache memory
    /// limits.
    void add_tile_to_cache(ImageCacheTileRef& tile,
                           ImageCachePerThreadInfo* thread_info);

    /// Find the tile specified by id.  If found, return true and place
    /// the tile ref in thread_info->tile; if not found, return false.
    /// Try to avoid looking to the big cache (and locking) most of the
    /// time for fairly coherent tile access patterns, by using the
    /// per-thread microcache to boost our hit rate over the big cache.
    /// Inlined for speed.  The tile is marked as 'used' if it wasn't the
    /// very last one used, or if it was the same as the last used and
    /// mark_same_tile_used is true.
    bool find_tile(const TileID& id, ImageCachePerThreadInfo* thread_info,
                   bool mark_same_tile_used)
    {
        ++thread_info->m_stats.find_tile_calls;
        ImageCacheTileRef& tile(thread_info->tile);
        if (tile) {
            if (tile->id() == id) {
                if (mark_same_tile_used)
                    tile->use();
                return true;  // already have the tile we want
            }
            // Tile didn't match, maybe lasttile will?  Swap tile
            // and last tile.  Then the new one will either match,
            // or we'll fall through and replace tile.
            tile.swap(thread_info->lasttile);
            if (tile && tile->id() == id) {
                tile->use();
                return true;
            }
        }
        return find_tile_main_cache(id, tile, thread_info);
        // N.B. find_tile_main_cache marks the tile as used
    }

    virtual Tile* get_tile(ustring filename, int subimage, int miplevel, int x,
                           int y, int z, int chbegin, int chend);
    virtual Tile* get_tile(ImageHandle* file, Perthread* thread_info,
                           int subimage, int miplevel, int x, int y, int z,
                           int chbegin, int chend);
    virtual void release_tile(Tile* tile) const;
    virtual TypeDesc tile_format(const Tile* tile) const;
    virtual ROI tile_roi(const Tile* tile) const;
    virtual const void* tile_pixels(Tile* tile, TypeDesc& format) const;
    virtual bool add_file(ustring filename, ImageInput::Creator creator,
                          const ImageSpec* config, bool replace);
    virtual bool add_tile(ustring filename, int subimage, int miplevel, int x,
                          int y, int z, int chbegin, int chend, TypeDesc format,
                          const void* buffer, stride_t xstride,
                          stride_t ystride, stride_t zstride, bool copy);

    /// Return the numerical subimage index for the given subimage name,
    /// as stored in the "oiio:subimagename" metadata.  Return -1 if no
    /// subimage matches its name.
    int subimage_from_name(ImageCacheFile* file, ustring subimagename);

    virtual bool has_error() const;
    virtual std::string geterror(bool clear = true) const;
    virtual std::string getstats(int level = 1) const;
    virtual void reset_stats();
    virtual void invalidate(ustring filename, bool force);
    virtual void invalidate_all(bool force = false);
    virtual void close(ustring filename);
    virtual void close_all();

    /// Merge all the per-thread statistics into one set of stats.
    ///
    void mergestats(ImageCacheStatistics& merged) const;

    void operator delete(void* todel) { ::delete ((char*)todel); }

    /// Called when a new file is opened, so that the system can track
    /// the number of simultaneously-opened files.
    void incr_open_files(void)
    {
        ++m_stat_open_files_created;
        atomic_max(m_stat_open_files_peak, ++m_stat_open_files_current);
    }

    /// Called when a file is closed, so that the system can track
    /// the number of simultaneously-opened files.
    void decr_open_files(void) { --m_stat_open_files_current; }

    /// Called when a new tile is created, to update all the stats.
    ///
    void incr_tiles(size_t size)
    {
        ++m_stat_tiles_created;
        atomic_max(m_stat_tiles_peak, ++m_stat_tiles_current);
        m_mem_used += size;
    }

    /// Called when a tile's pixel memory is allocated, but a new tile
    /// is not created.
    void incr_mem(size_t size) { m_mem_used += size; }

    /// Called when a tile is destroyed, to update all the stats.
    ///
    void decr_tiles(size_t size)
    {
        --m_stat_tiles_current;
        m_mem_used -= size;
        OIIO_DASSERT(m_mem_used >= 0);
    }

    /// Internal error reporting routine, with std::format-like arguments.
    template<typename... Args>
    void error(const char* fmt, const Args&... args) const
    {
        append_error(Strutil::fmt::format(fmt, args...));
    }
    void error(const char* msg) const { append_error(msg); }

    /// Append a string to the current error message
    void append_error(string_view message) const;

    virtual Perthread* get_perthread_info(Perthread* thread_info = NULL);
    virtual Perthread* create_thread_info();
    virtual void destroy_thread_info(Perthread* thread_info);

    /// Called when the IC is destroyed.  We have a list of all the
    /// perthread pointers -- go through and delete the ones for which we
    /// hold the only remaining pointer.
    void erase_perthread_info();

    /// This is called when the thread terminates.  If p->m_imagecache
    /// is non-NULL, there's still an imagecache alive that might want
    /// the per-thread info (say, for statistics, though it's safe to
    /// clear its tile microcache), so don't delete the perthread info
    /// (it will be owned thereafter by the IC).  If there is no IC still
    /// depending on it (signalled by m_imagecache == NULL), delete it.
    static void cleanup_perthread_info(Perthread* thread_info);

    /// Ensure that the max_memory_bytes is at least newsize bytes.
    /// Override the previous value if necessary, with thread-safety.
    void set_min_cache_size(long long newsize);

    /// Enforce the max number of open files.
    void check_max_files(ImageCachePerThreadInfo* thread_info);

    // For virtual UDIM-like files, adjust s and t and return the concrete
    // ImageCacheFile pointer for the tile it's on.
    ImageCacheFile* resolve_udim(ImageCacheFile* file, Perthread* thread_info,
                                 float& s, float& t);

    int max_mip_res() const noexcept { return m_max_mip_res; }

private:
    void init();

    /// Find a tile identified by 'id' in the tile cache, paging it in if
    /// needed, and store a reference to the tile.  Return true if ok,
    /// false if no such tile exists in the file or could not be read.
    bool find_tile_main_cache(const TileID& id, ImageCacheTileRef& tile,
                              ImageCachePerThreadInfo* thread_info);

    /// Enforce the max memory for tile data.
    void check_max_mem(ImageCachePerThreadInfo* thread_info);

    /// Internal statistics printing routine
    ///
    void printstats() const;

    // Helper function for printstats()
    std::string onefile_stat_line(const ImageCacheFileRef& file, int i,
                                  bool includestats = true) const;

    /// Search the fingerprint table for the given fingerprint.  If it
    /// doesn't already have an entry in the fingerprint map, then add
    /// one, mapping the it to file.  In either case, return the file it
    /// maps to (the caller can tell if it was newly added to the table
    /// by whether the return value is the same as the passed-in file).
    /// All the while, properly maintain thread safety on the
    /// fingerprint table.
    ImageCacheFile* find_fingerprint(ustring finger, ImageCacheFile* file);

    /// Clear all the per-thread microcaches.
    void purge_perthread_microcaches();

    /// Clear the fingerprint list, thread-safe.
    void clear_fingerprints();

    thread_specific_ptr<ImageCachePerThreadInfo> m_perthread_info;
    std::vector<ImageCachePerThreadInfo*> m_all_perthread_info;
    static spin_mutex m_perthread_info_mutex;  ///< Thread safety for perthread
    int m_max_open_files;
    atomic_ll m_max_memory_bytes;
    std::string m_searchpath;  ///< Colon-separated image directory list
    std::vector<std::string> m_searchdirs;  ///< Searchpath split into dirs
    std::string m_plugin_searchpath;  ///< Colon-separated plugin directory list
    int m_autotile;            ///< if nonzero, pretend tiles of this size
    bool m_autoscanline;       ///< autotile using full width tiles
    bool m_automip;            ///< auto-mipmap on demand?
    bool m_forcefloat;         ///< force all cache tiles to be float
    bool m_accept_untiled;     ///< Accept untiled images?
    bool m_accept_unmipped;    ///< Accept unmipped images?
    bool m_deduplicate;        ///< Detect duplicate files?
    bool m_unassociatedalpha;  ///< Keep unassociated alpha files as they are?
    bool m_latlong_y_up_default;  ///< Is +y the default "up" for latlong?
    bool m_trust_file_extensions = false;  ///< Assume file extensions don't lie?
    int m_failure_retries;                 ///< Times to re-try disk failures
    int m_max_mip_res = 1 << 30;  ///< Don't use MIP levels higher than this
    Imath::M44f m_Mw2c;           ///< world-to-"common" matrix
    Imath::M44f m_Mc2w;           ///< common-to-world matrix
    ustring m_substitute_image;   ///< Substitute this image for all others

    mutable FilenameMap m_files;    ///< Map file names to ImageCacheFile's
    ustring m_file_sweep_name;      ///< Sweeper for "clock" paging algorithm
    spin_mutex m_file_sweep_mutex;  ///< Ensure only one in check_max_files

    spin_mutex m_fingerprints_mutex;  ///< Protect m_fingerprints
    FingerprintMap m_fingerprints;    ///< Map fingerprints to files

    TileCache m_tilecache;          ///< Our in-memory tile cache
    TileID m_tile_sweep_id;         ///< Sweeper for "clock" paging algorithm
    spin_mutex m_tile_sweep_mutex;  ///< Ensure only one in check_max_mem

    atomic_ll m_mem_used;       ///< Memory being used for tiles
    int m_statslevel;           ///< Statistics level
    int m_max_errors_per_file;  ///< Max errors to print for each file.

    /// Saved error string, per-thread
    ///
    mutable thread_specific_ptr<std::string> m_errormessage;

    // For debugging -- keep track of who holds the tile and file mutex

private:
    // Statistics that are really hard to track per-thread
    atomic_int m_stat_tiles_created;
    atomic_int m_stat_tiles_current;
    atomic_int m_stat_tiles_peak;
    atomic_int m_stat_open_files_created;
    atomic_int m_stat_open_files_current;
    atomic_int m_stat_open_files_peak;

    // Simulate an atomic double with a long long!
    void incr_time_stat(double& stat, double incr)
    {
        stat += incr;
        return;
        OIIO_STATIC_ASSERT(sizeof(atomic_ll) == sizeof(double));
        double oldval, newval;
        long long* lloldval = (long long*)&oldval;
        long long* llnewval = (long long*)&newval;
        atomic_ll* llstat   = (atomic_ll*)&stat;
        // Make long long and atomic_ll pointers to the doubles in question.
        do {
            // Grab the double bits, shove into a long long
            *lloldval = *llstat;
            // increment
            newval = oldval + incr;
            // Now try to atomically swap it, and repeat until we've
            // done it with nobody else interfering.
        } while (llstat->compare_exchange_strong(*llnewval, *lloldval));
    }
};



}  // end namespace pvt

OIIO_NAMESPACE_END


#endif  // OPENIMAGEIO_IMAGECACHE_PVT_H
