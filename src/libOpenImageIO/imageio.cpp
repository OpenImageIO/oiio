// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cstdio>
#include <cstdlib>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/hash.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/optparser.h>
#include <OpenImageIO/parallel.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/thread.h>
#include <OpenImageIO/timer.h>
#include <OpenImageIO/typedesc.h>

#include "imageio_pvt.h"

OIIO_NAMESPACE_BEGIN

static int
threads_default()
{
    int n = Strutil::from_string<int>(Sysutil::getenv("OPENIMAGEIO_THREADS"));
    if (n < 1)
        n = Sysutil::hardware_concurrency();
    return n;
}

// Global private data
namespace pvt {
recursive_mutex imageio_mutex;
atomic_int oiio_threads(threads_default());
atomic_int oiio_exr_threads(threads_default());
atomic_int oiio_read_chunk(256);
int tiff_half(0);
int tiff_multithread(1);
ustring plugin_searchpath(OIIO_DEFAULT_PLUGIN_SEARCHPATH);
std::string format_list;         // comma-separated list of all formats
std::string input_format_list;   // comma-separated list of readable formats
std::string output_format_list;  // comma-separated list of writable formats
std::string extension_list;      // list of all extensions for all formats
std::string library_list;        // list of all libraries for all formats
static const char* oiio_debug_env = getenv("OPENIMAGEIO_DEBUG");
#ifdef NDEBUG
int oiio_print_debug(oiio_debug_env ? Strutil::stoi(oiio_debug_env) : 0);
#else
int oiio_print_debug(oiio_debug_env ? Strutil::stoi(oiio_debug_env) : 1);
#endif
int oiio_log_times = Strutil::from_string<int>(
    Sysutil::getenv("OPENIMAGEIO_LOG_TIMES"));
std::vector<float> oiio_missingcolor;
bool plugins_override = false;  // plugins override builtins
}  // namespace pvt

using namespace pvt;


namespace {
// Hidden global OIIO data.
static spin_mutex attrib_mutex;
static const int maxthreads  = 256;  // reasonable maximum for sanity check
static FILE* oiio_debug_file = NULL;

class TimingLog {
public:
    spin_mutex mutex;
    std::map<std::string, std::pair<double, size_t>> timing_map;

    TimingLog() noexcept {}

    // Destructor prints the timing report if oiio_log_times >= 2
    ~TimingLog()
    {
        if (oiio_log_times >= 2)
            std::cout << report();
    }

    // Call like a function to record times (but only if oiio_log_times > 0)
    void operator()(string_view key, const Timer& timer)
    {
        if (oiio_log_times) {
            auto t = timer();
            spin_lock lock(mutex);
            auto entry = timing_map.find(key);
            if (entry == timing_map.end())
                timing_map[key] = std::make_pair(t, size_t(1));
            else {
                entry->second.first += t;
                entry->second.second += 1;
            }
        }
    }

    // Retrieve the report as a big string
    std::string report()
    {
        std::stringstream out;
        spin_lock lock(mutex);
        for (const auto& item : timing_map) {
            size_t ncalls       = item.second.second;
            double time         = item.second.first;
            double percall      = time / ncalls;
            bool use_ms_percall = (percall < 0.1);
            out << Strutil::sprintf("%-25s%6d %7.3fs  (avg %6.2f%s)\n",
                                    item.first, ncalls, time,
                                    percall * (use_ms_percall ? 1000.0 : 1.0),
                                    use_ms_percall ? "ms" : "s");
        }
        return out.str();
    }
};
static TimingLog timing_log;



// Pipe-fitting class to set global options, for the sake of optparser.
struct GlobalOptSetter {
public:
    template<typename T> bool attribute(string_view name, T val) const
    {
        return OIIO::attribute(name, val);
    }
};


// Trick to force get the "OPENIMAGEIO_OPTIONS" env var on startup.
bool force_global_opts = []() {
    auto options = Sysutil::getenv("OPENIMAGEIO_OPTIONS");
    // std::cout << "OPTIONS: '" << options << "'\n";
    if (!options.empty())
        attribute("options", options);
    return true;
}();

}  // namespace



// Return a comma-separated list of all the important SIMD/capabilities
// supported by the hardware we're running on right now.
static std::string
hw_simd_caps()
{
    // clang-format off
    std::vector<string_view> caps;
    if (cpu_has_sse2())        caps.emplace_back ("sse2");
    if (cpu_has_sse3())        caps.emplace_back ("sse3");
    if (cpu_has_ssse3())       caps.emplace_back ("ssse3");
    if (cpu_has_sse41())       caps.emplace_back ("sse41");
    if (cpu_has_sse42())       caps.emplace_back ("sse42");
    if (cpu_has_avx())         caps.emplace_back ("avx");
    if (cpu_has_avx2())        caps.emplace_back ("avx2");
    if (cpu_has_avx512f())     caps.emplace_back ("avx512f");
    if (cpu_has_avx512dq())    caps.emplace_back ("avx512dq");
    if (cpu_has_avx512ifma())  caps.emplace_back ("avx512ifma");
    if (cpu_has_avx512pf())    caps.emplace_back ("avx512pf");
    if (cpu_has_avx512er())    caps.emplace_back ("avx512er");
    if (cpu_has_avx512cd())    caps.emplace_back ("avx512cd");
    if (cpu_has_avx512bw())    caps.emplace_back ("avx512bw");
    if (cpu_has_avx512vl())    caps.emplace_back ("avx512vl");
    if (cpu_has_fma())         caps.emplace_back ("fma");
    if (cpu_has_f16c())        caps.emplace_back ("f16c");
    if (cpu_has_popcnt())      caps.emplace_back ("popcnt");
    if (cpu_has_rdrand())      caps.emplace_back ("rdrand");
    return Strutil::join (caps, ",");
    // clang-format on
}



// Return a comma-separated list of all the important SIMD/capabilities
// that were enabled as a compile-time option when OIIO was built.
static std::string
oiio_simd_caps()
{
    // clang-format off
    std::vector<string_view> caps;
    if (OIIO_SIMD_SSE >= 2)      caps.emplace_back ("sse2");
    if (OIIO_SIMD_SSE >= 3)      caps.emplace_back ("sse3");
    if (OIIO_SIMD_SSE >= 3)      caps.emplace_back ("ssse3");
    if (OIIO_SIMD_SSE >= 4)      caps.emplace_back ("sse41");
    if (OIIO_SIMD_SSE >= 4)      caps.emplace_back ("sse42");
    if (OIIO_SIMD_AVX)           caps.emplace_back ("avx");
    if (OIIO_SIMD_AVX >= 2)      caps.emplace_back ("avx2");
    if (OIIO_SIMD_AVX >= 512)    caps.emplace_back ("avx512f");
    if (OIIO_AVX512DQ_ENABLED)   caps.emplace_back ("avx512dq");
    if (OIIO_AVX512IFMA_ENABLED) caps.emplace_back ("avx512ifma");
    if (OIIO_AVX512PF_ENABLED)   caps.emplace_back ("avx512pf");
    if (OIIO_AVX512ER_ENABLED)   caps.emplace_back ("avx512er");
    if (OIIO_AVX512CD_ENABLED)   caps.emplace_back ("avx512cd");
    if (OIIO_AVX512BW_ENABLED)   caps.emplace_back ("avx512bw");
    if (OIIO_AVX512VL_ENABLED)   caps.emplace_back ("avx512vl");
    if (OIIO_FMA_ENABLED)        caps.emplace_back ("fma");
    if (OIIO_F16C_ENABLED)       caps.emplace_back ("f16c");
    // if (OIIO_POPCOUNT_ENABLED)   caps.emplace_back ("popcnt");
    return Strutil::join (caps, ",");
    // clang-format on
}



int
openimageio_version()
{
    return OIIO_VERSION;
}



// To avoid thread oddities, we have the storage area buffering error
// messages for append_error()/geterror() be thread-specific.
static thread_local std::string error_msg;


void
pvt::append_error(string_view message)
{
    // Remove a single trailing newline
    if (message.size() && message.back() == '\n')
        message.remove_suffix(1);
    OIIO_ASSERT(
        error_msg.size() < 1024 * 1024 * 16
        && "Accumulated error messages > 16MB. Try checking return codes!");
    // If we are appending to existing error messages, separate them with
    // a single newline.
    if (error_msg.size() && error_msg.back() != '\n')
        error_msg += '\n';
    error_msg += message;

    // Remove a single trailing newline
    if (message.size() && message.back() == '\n')
        message.remove_suffix(1);
    error_msg = message;
}



bool
has_error()
{
    return !error_msg.empty();
}



std::string
geterror(bool clear)
{
    std::string e = error_msg;
    if (clear)
        error_msg.clear();
    return e;
}



void
debug(string_view message)
{
    recursive_lock_guard lock(pvt::imageio_mutex);
    if (oiio_print_debug) {
        if (!oiio_debug_file) {
            const char* filename = getenv("OPENIMAGEIO_DEBUG_FILE");
            oiio_debug_file = filename && filename[0] ? fopen(filename, "a")
                                                      : stderr;
            OIIO_ASSERT(oiio_debug_file);
            if (!oiio_debug_file)
                return;
        }
        Strutil::fprintf(oiio_debug_file, "OIIO DEBUG: %s", message);
    }
}



void
pvt::log_time(string_view key, const Timer& timer)
{
    timing_log(key, timer);
}



bool
attribute(string_view name, TypeDesc type, const void* val)
{
    if (name == "options" && type == TypeDesc::STRING) {
        GlobalOptSetter gos;
        return optparser(gos, *(const char**)val);
    }
    if (name == "threads" && type == TypeInt) {
        int ot = OIIO::clamp(*(const int*)val, 0, maxthreads);
        if (ot == 0)
            ot = threads_default();
        oiio_threads = ot;
        default_thread_pool()->resize(ot - 1);
        return true;
    }
    spin_lock lock(attrib_mutex);
    if (name == "read_chunk" && type == TypeInt) {
        oiio_read_chunk = *(const int*)val;
        return true;
    }
    if (name == "plugin_searchpath" && type == TypeString) {
        plugin_searchpath = ustring(*(const char**)val);
        return true;
    }
    if (name == "exr_threads" && type == TypeInt) {
        oiio_exr_threads = OIIO::clamp(*(const int*)val, -1, maxthreads);
        return true;
    }
    if (name == "tiff:half" && type == TypeInt) {
        tiff_half = *(const int*)val;
        return true;
    }
    if (name == "tiff:multithread" && type == TypeInt) {
        tiff_multithread = *(const int*)val;
        return true;
    }
    if (name == "debug" && type == TypeInt) {
        oiio_print_debug = *(const int*)val;
        return true;
    }
    if (name == "log_times" && type == TypeInt) {
        oiio_log_times = *(const int*)val;
        return true;
    }
    if (name == "missingcolor" && type.basetype == TypeDesc::FLOAT) {
        // missingcolor as float array
        oiio_missingcolor.clear();
        oiio_missingcolor.reserve(type.basevalues());
        int n = type.basevalues();
        for (int i = 0; i < n; ++i)
            oiio_missingcolor[i] = ((const float*)val)[i];
        return true;
    }
    if (name == "missingcolor" && type == TypeString) {
        // missingcolor as string
        oiio_missingcolor = Strutil::extract_from_list_string<float>(
            *(const char**)val);
        return true;
    }
    if (name == "plugins_override" && type == TypeDesc::TypeInt) {
        plugins_override = *(const int*)val;
        return true;
    }

    return false;
}



bool
getattribute(string_view name, TypeDesc type, void* val)
{
    if (name == "threads" && type == TypeInt) {
        *(int*)val = oiio_threads;
        return true;
    }
    spin_lock lock(attrib_mutex);
    if (name == "read_chunk" && type == TypeInt) {
        *(int*)val = oiio_read_chunk;
        return true;
    }
    if (name == "plugin_searchpath" && type == TypeString) {
        *(ustring*)val = plugin_searchpath;
        return true;
    }
    if (name == "plugins_override" && type == TypeInt) {
        *(int*)val = plugins_override;
        return true;
    }
    if (name == "format_list" && type == TypeString) {
        if (format_list.empty())
            pvt::catalog_all_plugins(plugin_searchpath.string());
        *(ustring*)val = ustring(format_list);
        return true;
    }
    if (name == "input_format_list" && type == TypeString) {
        if (input_format_list.empty())
            pvt::catalog_all_plugins(plugin_searchpath.string());
        *(ustring*)val = ustring(input_format_list);
        return true;
    }
    if (name == "output_format_list" && type == TypeString) {
        if (output_format_list.empty())
            pvt::catalog_all_plugins(plugin_searchpath.string());
        *(ustring*)val = ustring(output_format_list);
        return true;
    }
    if (name == "extension_list" && type == TypeString) {
        if (extension_list.empty())
            pvt::catalog_all_plugins(plugin_searchpath.string());
        *(ustring*)val = ustring(extension_list);
        return true;
    }
    if (name == "library_list" && type == TypeString) {
        if (library_list.empty())
            pvt::catalog_all_plugins(plugin_searchpath.string());
        *(ustring*)val = ustring(library_list);
        return true;
    }
    if (name == "exr_threads" && type == TypeInt) {
        *(int*)val = oiio_exr_threads;
        return true;
    }
    if (name == "tiff:half" && type == TypeInt) {
        *(int*)val = tiff_half;
        return true;
    }
    if (name == "tiff:multithread" && type == TypeInt) {
        *(int*)val = tiff_multithread;
        return true;
    }
    if (name == "debug" && type == TypeInt) {
        *(int*)val = oiio_print_debug;
        return true;
    }
    if (name == "log_times" && type == TypeInt) {
        *(int*)val = oiio_log_times;
        return true;
    }
    if (name == "timing_report" && type == TypeString) {
        *(ustring*)val = ustring(timing_log.report());
        return true;
    }
    if (name == "hw:simd" && type == TypeString) {
        *(ustring*)val = ustring(hw_simd_caps());
        return true;
    }
    if (name == "oiio:simd" && type == TypeString) {
        *(ustring*)val = ustring(oiio_simd_caps());
        return true;
    }
    if (name == "resident_memory_used_MB" && type == TypeInt) {
        *(int*)val = int(Sysutil::memory_used(true) >> 20);
        return true;
    }
    if (name == "missingcolor" && type.basetype == TypeDesc::FLOAT
        && oiio_missingcolor.size()) {
        // missingcolor as float array
        int n  = type.basevalues();
        int nm = std::min(int(oiio_missingcolor.size()), n);
        for (int i = 0; i < nm; ++i)
            ((float*)val)[i] = oiio_missingcolor[i];
        for (int i = nm; i < n; ++i)
            ((float*)val)[i] = 0.0f;
        return true;
    }
    if (name == "missingcolor" && type == TypeString) {
        // missingcolor as string
        *(ustring*)val = ustring(Strutil::join(oiio_missingcolor, ","));
        return true;
    }
    return false;
}



namespace {

/// Type-independent template for turning potentially
/// non-contiguous-stride data (e.g. "RGB RGB ") into contiguous-stride
/// ("RGBRGB").  Caller must pass in a dst pointing to enough memory to
/// hold the contiguous rectangle.  Return a ptr to where the contiguous
/// data ended up, which is either dst or src (if the strides indicated
/// that data were already contiguous).
template<typename T>
const T*
_contiguize(const T* src, int nchannels, stride_t xstride, stride_t ystride,
            stride_t zstride, T* dst, int width, int height, int depth)
{
    int datasize = sizeof(T);
    if (xstride == nchannels * datasize && ystride == xstride * width
        && (zstride == ystride * height || !zstride))
        return src;

    if (depth < 1)  // Safeguard against volume-unaware clients
        depth = 1;

    T* dstsave = dst;
    if (xstride == nchannels * datasize) {
        // Optimize for contiguous scanlines, but not from scanline to scanline
        for (int z = 0; z < depth;
             ++z, src = (const T*)((char*)src + zstride)) {
            const T* scanline = src;
            for (int y        = 0; y < height; ++y, dst += nchannels * width,
                     scanline = (const T*)((char*)scanline + ystride))
                memcpy(dst, scanline, xstride * width);
        }
    } else {
        for (int z = 0; z < depth;
             ++z, src = (const T*)((char*)src + zstride)) {
            const T* scanline = src;
            for (int y = 0; y < height;
                 ++y, scanline = (const T*)((char*)scanline + ystride)) {
                const T* pixel = scanline;
                for (int x = 0; x < width;
                     ++x, pixel = (const T*)((char*)pixel + xstride))
                    for (int c = 0; c < nchannels; ++c)
                        *dst++ = pixel[c];
            }
        }
    }
    return dstsave;
}

}  // namespace

const void*
pvt::contiguize(const void* src, int nchannels, stride_t xstride,
                stride_t ystride, stride_t zstride, void* dst, int width,
                int height, int depth, TypeDesc format)
{
    switch (format.basetype) {
    case TypeDesc::FLOAT:
        return _contiguize((const float*)src, nchannels, xstride, ystride,
                           zstride, (float*)dst, width, height, depth);
    case TypeDesc::INT8:
    case TypeDesc::UINT8:
        return _contiguize((const char*)src, nchannels, xstride, ystride,
                           zstride, (char*)dst, width, height, depth);
    case TypeDesc::HALF: OIIO_DASSERT(sizeof(half) == sizeof(short));
    case TypeDesc::INT16:
    case TypeDesc::UINT16:
        return _contiguize((const short*)src, nchannels, xstride, ystride,
                           zstride, (short*)dst, width, height, depth);
    case TypeDesc::INT:
    case TypeDesc::UINT:
        return _contiguize((const int*)src, nchannels, xstride, ystride,
                           zstride, (int*)dst, width, height, depth);
    case TypeDesc::INT64:
    case TypeDesc::UINT64:
        return _contiguize((const long long*)src, nchannels, xstride, ystride,
                           zstride, (long long*)dst, width, height, depth);
    case TypeDesc::DOUBLE:
        return _contiguize((const double*)src, nchannels, xstride, ystride,
                           zstride, (double*)dst, width, height, depth);
    default:
        OIIO_ASSERT(0 && "OpenImageIO::contiguize : bad format");
        return NULL;
    }
}



const float*
pvt::convert_to_float(const void* src, float* dst, int nvals, TypeDesc format)
{
    switch (format.basetype) {
    case TypeDesc::FLOAT: return (float*)src;
    case TypeDesc::UINT8:
        convert_type((const unsigned char*)src, dst, nvals);
        break;
    case TypeDesc::HALF: convert_type((const half*)src, dst, nvals); break;
    case TypeDesc::UINT16:
        convert_type((const unsigned short*)src, dst, nvals);
        break;
    case TypeDesc::INT8: convert_type((const char*)src, dst, nvals); break;
    case TypeDesc::INT16: convert_type((const short*)src, dst, nvals); break;
    case TypeDesc::INT: convert_type((const int*)src, dst, nvals); break;
    case TypeDesc::UINT:
        convert_type((const unsigned int*)src, dst, nvals);
        break;
    case TypeDesc::INT64:
        convert_type((const long long*)src, dst, nvals);
        break;
    case TypeDesc::UINT64:
        convert_type((const unsigned long long*)src, dst, nvals);
        break;
    case TypeDesc::DOUBLE: convert_type((const double*)src, dst, nvals); break;
    default: OIIO_ASSERT(0 && "ERROR to_float: bad format"); return NULL;
    }
    return dst;
}



const void*
pvt::convert_from_float(const float* src, void* dst, size_t nvals,
                        TypeDesc format)
{
    // If no source pixels, assume zeroes
    if (!src) {
        memset(dst, 0, nvals * format.size());
        return dst;
    }

    // clang-format off
    switch (format.basetype) {
    case TypeDesc::FLOAT:
        // If it's already float, return the source itself
        return src;
    case TypeDesc::HALF:   convert_type(src, (half*)    dst, nvals); break;
    case TypeDesc::UINT8:  convert_type(src, (uint8_t*) dst, nvals); break;
    case TypeDesc::UINT16: convert_type(src, (uint16_t*)dst, nvals); break;
    case TypeDesc::UINT:   convert_type(src, (uint32_t*)dst, nvals); break;
    case TypeDesc::INT8:   convert_type(src, (int8_t*)  dst, nvals); break;
    case TypeDesc::INT16:  convert_type(src, (int16_t*) dst, nvals); break;
    case TypeDesc::INT:    convert_type(src, (int32_t*) dst, nvals); break;
    case TypeDesc::DOUBLE: convert_type(src, (double*)  dst, nvals); break;
    case TypeDesc::INT64:  convert_type(src, (int64_t*) dst, nvals); break;
    case TypeDesc::UINT64: convert_type(src, (uint64_t*)dst, nvals); break;
    default: OIIO_ASSERT(0 && "ERROR from_float: bad format"); dst = nullptr;
    }
    // clang-format on
    return dst;
}


const void*
pvt::parallel_convert_from_float(const float* src, void* dst, size_t nvals,
                                 TypeDesc format)
{
    if (format.basetype == TypeDesc::FLOAT)
        return src;

    parallel_for_chunked(0, int64_t(nvals), 0, [=](int64_t b, int64_t e) {
        convert_from_float(src + b, (char*)dst + b * format.size(), e - b,
                           format);
    });
    return dst;
}



bool
convert_pixel_values(TypeDesc src_type, const void* src, TypeDesc dst_type,
                     void* dst, int n)
{
    // If no conversion is necessary, just memcpy
    if ((src_type == dst_type || dst_type.basetype == TypeDesc::UNKNOWN)) {
        memcpy(dst, src, n * src_type.size());
        return true;
    }

    if (dst_type == TypeFloat) {
        // Special case -- converting non-float to float
        pvt::convert_to_float(src, (float*)dst, n, src_type);
        return true;
    }

    // Conversion is to a non-float type

    std::unique_ptr<float[]> tmp;  // In case we need a lot of temp space
    float* buf = (float*)src;
    if (src_type != TypeFloat) {
        // If src is also not float, convert through an intermediate buffer
        if (n <= 4096)  // If < 16k, use the stack
            buf = OIIO_ALLOCA(float, n);
        else {
            tmp.reset(new float[n]);  // Freed when tmp exists its scope
            buf = tmp.get();
        }
        pvt::convert_to_float(src, buf, n, src_type);
    }

    // Convert float to 'dst_type'
    switch (dst_type.basetype) {
    case TypeDesc::UINT8: convert_type(buf, (unsigned char*)dst, n); break;
    case TypeDesc::UINT16: convert_type(buf, (unsigned short*)dst, n); break;
    case TypeDesc::HALF: convert_type(buf, (half*)dst, n); break;
    case TypeDesc::INT8: convert_type(buf, (char*)dst, n); break;
    case TypeDesc::INT16: convert_type(buf, (short*)dst, n); break;
    case TypeDesc::INT: convert_type(buf, (int*)dst, n); break;
    case TypeDesc::UINT: convert_type(buf, (unsigned int*)dst, n); break;
    case TypeDesc::INT64: convert_type(buf, (long long*)dst, n); break;
    case TypeDesc::UINT64:
        convert_type(buf, (unsigned long long*)dst, n);
        break;
    case TypeDesc::DOUBLE: convert_type(buf, (double*)dst, n); break;
    default: return false;  // unknown format
    }

    return true;
}



bool
convert_image(int nchannels, int width, int height, int depth, const void* src,
              TypeDesc src_type, stride_t src_xstride, stride_t src_ystride,
              stride_t src_zstride, void* dst, TypeDesc dst_type,
              stride_t dst_xstride, stride_t dst_ystride, stride_t dst_zstride)
{
    // If no format conversion is taking place, use the simplified
    // copy_image.
    if (src_type == dst_type)
        return copy_image(nchannels, width, height, depth, src,
                          src_type.size() * nchannels, src_xstride, src_ystride,
                          src_zstride, dst, dst_xstride, dst_ystride,
                          dst_zstride);

    ImageSpec::auto_stride(src_xstride, src_ystride, src_zstride, src_type,
                           nchannels, width, height);
    ImageSpec::auto_stride(dst_xstride, dst_ystride, dst_zstride, dst_type,
                           nchannels, width, height);
    bool result = true;
    bool contig = (src_xstride == stride_t(nchannels * src_type.size())
                   && dst_xstride == stride_t(nchannels * dst_type.size()));
    for (int z = 0; z < depth; ++z) {
        for (int y = 0; y < height; ++y) {
            const char* f = (const char*)src
                            + (z * src_zstride + y * src_ystride);
            char* t = (char*)dst + (z * dst_zstride + y * dst_ystride);
            if (contig) {
                // Special case: pixels within each row are contiguous
                // in both src and dst and we're copying all channels.
                // Be efficient by converting each scanline as a single
                // unit.  (Note that within convert_pixel_values, a memcpy
                // will be used if the formats are identical.)
                result &= convert_pixel_values(src_type, f, dst_type, t,
                                               nchannels * width);
            } else {
                // General case -- anything goes with strides.
                for (int x = 0; x < width; ++x) {
                    result &= convert_pixel_values(src_type, f, dst_type, t,
                                                   nchannels);
                    f += src_xstride;
                    t += dst_xstride;
                }
            }
        }
    }
    return result;
}



bool
parallel_convert_image(int nchannels, int width, int height, int depth,
                       const void* src, TypeDesc src_type, stride_t src_xstride,
                       stride_t src_ystride, stride_t src_zstride, void* dst,
                       TypeDesc dst_type, stride_t dst_xstride,
                       stride_t dst_ystride, stride_t dst_zstride, int nthreads)
{
    if (nthreads <= 0)
        nthreads = oiio_threads;
    nthreads
        = clamp(int((int64_t(width) * height * depth * nchannels) / 100000), 1,
                nthreads);
    if (nthreads <= 1)
        return convert_image(nchannels, width, height, depth, src, src_type,
                             src_xstride, src_ystride, src_zstride, dst,
                             dst_type, dst_xstride, dst_ystride, dst_zstride);

    ImageSpec::auto_stride(src_xstride, src_ystride, src_zstride, src_type,
                           nchannels, width, height);
    ImageSpec::auto_stride(dst_xstride, dst_ystride, dst_zstride, dst_type,
                           nchannels, width, height);

    int blocksize = std::max(1, height / nthreads);
    parallel_for_chunked(
        0, height, blocksize, [=](int /*id*/, int64_t ybegin, int64_t yend) {
            convert_image(nchannels, width, yend - ybegin, depth,
                          (const char*)src + src_ystride * ybegin, src_type,
                          src_xstride, src_ystride, src_zstride,
                          (char*)dst + dst_ystride * ybegin, dst_type,
                          dst_xstride, dst_ystride, dst_zstride);
        });
    return true;
}



bool
copy_image(int nchannels, int width, int height, int depth, const void* src,
           stride_t pixelsize, stride_t src_xstride, stride_t src_ystride,
           stride_t src_zstride, void* dst, stride_t dst_xstride,
           stride_t dst_ystride, stride_t dst_zstride)
{
    stride_t channelsize = pixelsize / nchannels;
    ImageSpec::auto_stride(src_xstride, src_ystride, src_zstride, channelsize,
                           nchannels, width, height);
    ImageSpec::auto_stride(dst_xstride, dst_ystride, dst_zstride, channelsize,
                           nchannels, width, height);
    bool contig = (src_xstride == dst_xstride
                   && src_xstride == (stride_t)pixelsize);
    for (int z = 0; z < depth; ++z) {
        for (int y = 0; y < height; ++y) {
            const char* f = (const char*)src
                            + (z * src_zstride + y * src_ystride);
            char* t = (char*)dst + (z * dst_zstride + y * dst_ystride);
            if (contig) {
                // Special case: pixels within each row are contiguous
                // in both src and dst and we're copying all channels.
                // Be efficient by converting each scanline as a single
                // unit.
                memcpy(t, f, width * pixelsize);
            } else {
                // General case -- anything goes with strides.
                for (int x = 0; x < width; ++x) {
                    memcpy(t, f, pixelsize);
                    f += src_xstride;
                    t += dst_xstride;
                }
            }
        }
    }
    return true;
}



void
add_dither(int nchannels, int width, int height, int depth, float* data,
           stride_t xstride, stride_t ystride, stride_t zstride,
           float ditheramplitude, int alpha_channel, int z_channel,
           unsigned int ditherseed, int chorigin, int xorigin, int yorigin,
           int zorigin)
{
    ImageSpec::auto_stride(xstride, ystride, zstride, sizeof(float), nchannels,
                           width, height);
    char* plane = (char*)data;
    for (int z = 0; z < depth; ++z, plane += zstride) {
        char* scanline = plane;
        for (int y = 0; y < height; ++y, scanline += ystride) {
            char* pixel = scanline;
            uint32_t ba = (z + zorigin) * 1311 + yorigin + y;
            uint32_t bb = ditherseed + (chorigin << 24);
            uint32_t bc = xorigin;
            for (int x = 0; x < width; ++x, pixel += xstride) {
                float* val = (float*)pixel;
                for (int c = 0; c < nchannels; ++c, ++val, ++bc) {
                    bjhash::bjmix(ba, bb, bc);
                    int channel = c + chorigin;
                    if (channel == alpha_channel || channel == z_channel)
                        continue;
                    float dither
                        = bc / float(std::numeric_limits<uint32_t>::max());
                    *val += ditheramplitude * (dither - 0.5f);
                }
            }
        }
    }
}



template<typename T>
static void
premult_impl(int width, int height, int depth, int chbegin, int chend, T* data,
             stride_t xstride, stride_t ystride, stride_t zstride,
             int alpha_channel, int z_channel)
{
    char* plane = (char*)data;
    for (int z = 0; z < depth; ++z, plane += zstride) {
        char* scanline = plane;
        for (int y = 0; y < height; ++y, scanline += ystride) {
            char* pixel = scanline;
            for (int x = 0; x < width; ++x, pixel += xstride) {
                DataArrayProxy<T, float> val((T*)pixel);
                float alpha = val[alpha_channel];
                for (int c = chbegin; c < chend; ++c) {
                    if (c == alpha_channel || c == z_channel)
                        continue;
                    val[c] = alpha * val[c];
                }
            }
        }
    }
}



void
premult(int nchannels, int width, int height, int depth, int chbegin, int chend,
        TypeDesc datatype, void* data, stride_t xstride, stride_t ystride,
        stride_t zstride, int alpha_channel, int z_channel)
{
    if (alpha_channel < 0 || alpha_channel > nchannels)
        return;  // nothing to do
    ImageSpec::auto_stride(xstride, ystride, zstride, datatype.size(),
                           nchannels, width, height);
    switch (datatype.basetype) {
    case TypeDesc::FLOAT:
        premult_impl(width, height, depth, chbegin, chend, (float*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::UINT8:
        premult_impl(width, height, depth, chbegin, chend, (unsigned char*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::UINT16:
        premult_impl(width, height, depth, chbegin, chend,
                     (unsigned short*)data, xstride, ystride, zstride,
                     alpha_channel, z_channel);
        break;
    case TypeDesc::HALF:
        premult_impl(width, height, depth, chbegin, chend, (half*)data, xstride,
                     ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::INT8:
        premult_impl(width, height, depth, chbegin, chend, (char*)data, xstride,
                     ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::INT16:
        premult_impl(width, height, depth, chbegin, chend, (short*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::INT:
        premult_impl(width, height, depth, chbegin, chend, (int*)data, xstride,
                     ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::UINT:
        premult_impl(width, height, depth, chbegin, chend, (unsigned int*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::INT64:
        premult_impl(width, height, depth, chbegin, chend, (int64_t*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::UINT64:
        premult_impl(width, height, depth, chbegin, chend, (uint64_t*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    case TypeDesc::DOUBLE:
        premult_impl(width, height, depth, chbegin, chend, (double*)data,
                     xstride, ystride, zstride, alpha_channel, z_channel);
        break;
    default: OIIO_ASSERT(0 && "OIIO::premult() of an unsupported type"); break;
    }
}



bool
wrap_black(int& coord, int origin, int width)
{
    return (coord >= origin && coord < (width + origin));
}


bool
wrap_clamp(int& coord, int origin, int width)
{
    if (coord < origin)
        coord = origin;
    else if (coord >= origin + width)
        coord = origin + width - 1;
    return true;
}


bool
wrap_periodic(int& coord, int origin, int width)
{
    coord -= origin;
    coord %= width;
    if (coord < 0)  // Fix negative values
        coord += width;
    coord += origin;
    return true;
}


bool
wrap_periodic_pow2(int& coord, int origin, int width)
{
    OIIO_DASSERT(ispow2(width));
    coord -= origin;
    coord &= (width - 1);  // Shortcut periodic if we're sure it's a pow of 2
    coord += origin;
    return true;
}


bool
wrap_mirror(int& coord, int origin, int width)
{
    coord -= origin;
    if (coord < 0)
        coord = -coord - 1;
    int iter = coord / width;  // Which iteration of the pattern?
    coord -= iter * width;
    if (iter & 1)  // Odd iterations -- flip the sense
        coord = width - 1 - coord;
    OIIO_DASSERT_MSG(coord >= 0 && coord < width,
                     "width=%d, origin=%d, result=%d", width, origin, coord);
    coord += origin;
    return true;
}


OIIO_NAMESPACE_END
