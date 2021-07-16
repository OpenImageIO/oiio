// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <boost/container/flat_map.hpp>

#include <OpenImageIO/color.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>

#include "imageio_pvt.h"

#define MAKE_OCIO_VERSION_HEX(maj, min, patch) \
    (((maj) << 24) | ((min) << 16) | (patch))

#ifdef USE_OCIO
#    include <OpenColorIO/OpenColorIO.h>
#    if OCIO_VERSION_HEX >= MAKE_OCIO_VERSION_HEX(2, 0, 0)
#        define OCIO_v2 1
// NOTE: these 3 header files were part of the pre-release OCIO 2.0 but
// eventually were removed. After OCIO v2 is released and we are fairly
// confident nobody will encounter them, we can remove these includes
// entirely.
#        if __has_include(<OpenColorIO/apphelpers/ColorSpaceHelpers.h>)
#            include <OpenColorIO/apphelpers/ColorSpaceHelpers.h>
#        endif
#        if __has_include(<OpenColorIO/apphelpers/DisplayViewHelpers.h>)
#            include <OpenColorIO/apphelpers/DisplayViewHelpers.h>
#        endif
#        if __has_include(<OpenColorIO/apphelpers/ViewingPipeline.h>)
#            include <OpenColorIO/apphelpers/ViewingPipeline.h>
#        endif
#    endif
namespace OCIO = OCIO_NAMESPACE;
#endif

OIIO_NAMESPACE_BEGIN


static std::shared_ptr<ColorConfig> default_colorconfig;  // default color config
static spin_mutex colorconfig_mutex;
#ifdef USE_OCIO
static OCIO::ConstConfigRcPtr ocio_current_config;
#endif



// Class used as the key to index color processors in the cache.
class ColorProcCacheKey {
public:
    ColorProcCacheKey(ustring in, ustring out, ustring key = ustring(),
                      ustring val = ustring(), ustring looks = ustring(),
                      ustring display = ustring(), ustring view = ustring(),
                      ustring file = ustring(), bool inverse = false)
        : inputColorSpace(in)
        , outputColorSpace(out)
        , context_key(key)
        , context_value(val)
        , looks(looks)
        , file(file)
        , inverse(inverse)
    {
        hash = inputColorSpace.hash() + 14033ul * outputColorSpace.hash()
               + 823ul * context_key.hash() + 28411ul * context_value.hash()
               + 1741ul
                     * (looks.hash() + display.hash() + view.hash()
                        + file.hash())
               + (inverse ? 6421 : 0);
        // N.B. no separate multipliers for looks, display, view, file
        // because they're never used for the same lookup.
    }

    friend bool operator<(const ColorProcCacheKey& a,
                          const ColorProcCacheKey& b)
    {
        if (a.hash < b.hash)
            return true;
        if (b.hash < a.hash)
            return false;
        // They hash the same, so now compare for real. Note that we just to
        // impose an order, any order -- does not need to be alphabetical --
        // so we just compare the pointers.
        if (a.inputColorSpace.c_str() < b.inputColorSpace.c_str())
            return true;
        if (b.inputColorSpace.c_str() < a.inputColorSpace.c_str())
            return false;
        if (a.outputColorSpace.c_str() < b.outputColorSpace.c_str())
            return true;
        if (b.outputColorSpace.c_str() < a.outputColorSpace.c_str())
            return false;
        if (a.context_key.c_str() < b.context_key.c_str())
            return true;
        if (b.context_key.c_str() < a.context_key.c_str())
            return false;
        if (a.looks.c_str() < b.looks.c_str())
            return true;
        if (b.looks.c_str() < a.looks.c_str())
            return false;
        if (a.display.c_str() < b.display.c_str())
            return true;
        if (b.display.c_str() < a.display.c_str())
            return false;
        if (a.view.c_str() < b.view.c_str())
            return true;
        if (b.view.c_str() < a.view.c_str())
            return false;
        if (a.file.c_str() < b.view.c_str())
            return true;
        if (b.view.c_str() < a.view.c_str())
            return false;
        return int(a.inverse) < int(b.inverse);
    }

    ustring inputColorSpace;
    ustring outputColorSpace;
    ustring context_key;
    ustring context_value;
    ustring looks;
    ustring display;
    ustring view;
    ustring file;
    bool inverse;
    size_t hash;
};



typedef boost::container::flat_map<ColorProcCacheKey, ColorProcessorHandle>
    ColorProcessorMap;



bool
ColorConfig::supportsOpenColorIO()
{
#ifdef USE_OCIO
    return true;
#else
    return false;
#endif
}



int
ColorConfig::OpenColorIO_version_hex()
{
#ifdef USE_OCIO
    return OCIO_VERSION_HEX;
#else
    return 0;
#endif
}



// Hidden implementation of ColorConfig
class ColorConfig::Impl {
public:
#ifdef USE_OCIO
    OCIO::ConstConfigRcPtr config_;
#endif
    std::vector<std::pair<std::string, int>> colorspaces;
    std::string linear_alias;  // Alias for a scene-linear color space
private:
    mutable spin_rw_mutex m_mutex;
    mutable std::string m_error;
    ColorProcessorMap colorprocmap;  // cache of ColorProcessors
    atomic_int colorprocs_requested;
    atomic_int colorprocs_created;
    std::string m_configname;

public:
    Impl() {}

    ~Impl()
    {
#if 0
        // Debugging the cache -- make sure we're creating a small number
        // compared to repeated requests.
        if (colorprocs_requested)
            Strutil::printf ("ColorConfig::Impl : color procs requested: %d, created: %d\n",
                             colorprocs_requested, colorprocs_created);
#endif
    }

    void inventory();
    void add(const std::string& name, int index)
    {
        colorspaces.emplace_back(name, index);
    }

    // Search for a matching ColorProcessor, return it if found (otherwise
    // return an empty handle).
    ColorProcessorHandle findproc(const ColorProcCacheKey& key)
    {
        ++colorprocs_requested;
        spin_rw_read_lock lock(m_mutex);
        auto found = colorprocmap.find(key);
        return (found == colorprocmap.end()) ? ColorProcessorHandle()
                                             : found->second;
    }

    // Add the given color processor. Be careful -- if a matching one is
    // already in the table, just return the existing one. If they pass
    // in an empty handle, just return it.
    ColorProcessorHandle addproc(const ColorProcCacheKey& key,
                                 ColorProcessorHandle handle)
    {
        if (!handle)
            return handle;
        ++colorprocs_created;
        spin_rw_write_lock lock(m_mutex);
        auto found = colorprocmap.find(key);
        if (found == colorprocmap.end()) {
            // No equivalent item in the map. Add this one.
            colorprocmap[key] = handle;
        } else {
            // There's already an equivalent one. Oops. Discard this one and
            // return the one already in the map.
            handle = found->second;
        }
        return handle;
    }

    void error(const std::string& err)
    {
        spin_rw_write_lock lock(m_mutex);
        m_error = err;
    }
    std::string geterror(bool clear = true)
    {
        std::string err;
        if (clear) {
            spin_rw_write_lock lock(m_mutex);
            err = m_error;
            m_error.clear();
        } else {
            spin_rw_read_lock lock(m_mutex);
            err = m_error;
        }
        return err;
    }
    bool haserror() const
    {
        spin_rw_read_lock lock(m_mutex);
        return !m_error.empty();
    }
    void clear_error()
    {
        spin_rw_write_lock lock(m_mutex);
        m_error.clear();
    }

    const std::string& configname() const { return m_configname; }
    void configname(string_view name) { m_configname = name; }
};



// ColorConfig utility to take inventory of the color spaces available.
// It sets up knowledge of "linear", "sRGB", "Rec709",
// even if the underlying OCIO configuration lacks them.
void
ColorConfig::Impl::inventory()
{
#ifdef USE_OCIO
    if (config_) {
        bool nonraw = false;
        for (int i = 0, e = config_->getNumColorSpaces(); i < e; ++i)
            nonraw |= !Strutil::iequals(config_->getColorSpaceNameByIndex(i),
                                        "raw");
        if (nonraw) {
            for (int i = 0, e = config_->getNumColorSpaces(); i < e; ++i)
                add(config_->getColorSpaceNameByIndex(i), i);
            OCIO::ConstColorSpaceRcPtr lin = config_->getColorSpace(
                "scene_linear");
            if (lin)
                linear_alias = lin->getName();
            return;  // If any non-"raw" spaces were defined, we're done
        }
    }
    // If we had some kind of bogus configuration that seemed to define
    // only a "raw" color space and nothing else, that's useless, so
    // figure out our own way to move forward.
    config_.reset();
#endif

    // If there was no configuration, or we didn't compile with OCIO
    // support at all, register a few basic names we know about.
    add("linear", 0);
    add("default", 0);
    add("rgb", 0);
    add("RGB", 0);
    add("sRGB", 1);
    add("Rec709", 2);
}



ColorConfig::ColorConfig(string_view filename) { reset(filename); }



ColorConfig::~ColorConfig() {}



bool
ColorConfig::reset(string_view filename)
{
    bool ok = true;

    m_impl.reset(new ColorConfig::Impl);
#ifdef USE_OCIO
    OCIO::SetLoggingLevel(OCIO::LOGGING_LEVEL_NONE);
    if (!ocio_current_config)
        ocio_current_config = OCIO::GetCurrentConfig();
    try {
        if (filename.empty()) {
            getImpl()->config_  = ocio_current_config;
            string_view ocioenv = Sysutil::getenv("OCIO");
            if (ocioenv.size())
                getImpl()->configname(ocioenv);
        } else {
            getImpl()->config_ = OCIO::Config::CreateFromFile(filename.c_str());
            getImpl()->configname(filename);
        }
    } catch (OCIO::Exception& e) {
        getImpl()->error(e.what());
        ok = false;
    } catch (...) {
        getImpl()->error(
            "An unknown error occurred in OpenColorIO creating the config");
        ok = false;
    }
#endif

    getImpl()->inventory();

    // If we populated our own, remove any errors.
    if (getNumColorSpaces() && !getImpl()->haserror())
        getImpl()->clear_error();

    return ok;
}



bool
ColorConfig::error() const
{
    return (getImpl()->haserror());
}



std::string
ColorConfig::geterror(bool clear)
{
    return getImpl()->geterror(clear);
}



int
ColorConfig::getNumColorSpaces() const
{
    return (int)getImpl()->colorspaces.size();
}



const char*
ColorConfig::getColorSpaceNameByIndex(int index) const
{
    return getImpl()->colorspaces[index].first.c_str();
}



const char*
ColorConfig::getColorSpaceFamilyByName(string_view name) const
{
#ifdef USE_OCIO
    if (getImpl()->config_) {
        OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
            name.c_str());
        if (c)
            return c->getFamily();
    }
#endif
    return NULL;
}



std::vector<std::string>
ColorConfig::getColorSpaceNames() const
{
    std::vector<std::string> result;
    result.reserve(getImpl()->colorspaces.size());
    for (auto& c : getImpl()->colorspaces)
        result.push_back(c.first);
    return result;
}

int
ColorConfig::getNumRoles() const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getNumRoles();
#endif
    return 0;
}

const char*
ColorConfig::getRoleByIndex(int index) const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getRoleName(index);
#endif
    return NULL;
}


std::vector<std::string>
ColorConfig::getRoles() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumRoles(); i != e; ++i)
        result.emplace_back(getRoleByIndex(i));
    return result;
}



int
ColorConfig::getNumLooks() const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getNumLooks();
#endif
    return 0;
}



const char*
ColorConfig::getLookNameByIndex(int index) const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getLookNameByIndex(index);
#endif
    return NULL;
}



std::vector<std::string>
ColorConfig::getLookNames() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumLooks(); i != e; ++i)
        result.emplace_back(getLookNameByIndex(i));
    return result;
}



const char*
ColorConfig::getColorSpaceNameByRole(string_view role) const
{
#ifdef USE_OCIO
    if (getImpl()->config_) {
        OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
            role.c_str());
        // Catch special case of obvious name synonyms
        if (!c
            && (Strutil::iequals(role, "RGB")
                || Strutil::iequals(role, "default")))
            role = string_view("linear");
        if (!c && Strutil::iequals(role, "linear"))
            c = getImpl()->config_->getColorSpace("scene_linear");
        if (!c && Strutil::iequals(role, "scene_linear"))
            c = getImpl()->config_->getColorSpace("linear");
        if (c)
            return c->getName();
    }
#endif

    // No OCIO at build time, or no OCIO configuration at run time
    if (Strutil::iequals(role, "linear")
        || Strutil::iequals(role, "scene_linear"))
        return "linear";

    return NULL;  // Dunno what role
}



TypeDesc
ColorConfig::getColorSpaceDataType(string_view name, int* bits) const
{
#ifdef USE_OCIO
    OCIO::ConstColorSpaceRcPtr c = getImpl()->config_->getColorSpace(
        name.c_str());
    if (c) {
        OCIO::BitDepth b = c->getBitDepth();
        switch (b) {
        case OCIO::BIT_DEPTH_UNKNOWN: return TypeDesc::UNKNOWN;
        case OCIO::BIT_DEPTH_UINT8: *bits = 8; return TypeDesc::UINT8;
        case OCIO::BIT_DEPTH_UINT10: *bits = 10; return TypeDesc::UINT16;
        case OCIO::BIT_DEPTH_UINT12: *bits = 12; return TypeDesc::UINT16;
        case OCIO::BIT_DEPTH_UINT14: *bits = 14; return TypeDesc::UINT16;
        case OCIO::BIT_DEPTH_UINT16: *bits = 16; return TypeDesc::UINT16;
        case OCIO::BIT_DEPTH_UINT32: *bits = 32; return TypeDesc::UINT32;
        case OCIO::BIT_DEPTH_F16: *bits = 16; return TypeDesc::HALF;
        case OCIO::BIT_DEPTH_F32: *bits = 32; return TypeDesc::FLOAT;
        }
    }
#endif
    return TypeDesc::UNKNOWN;
}



int
ColorConfig::getNumDisplays() const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getNumDisplays();
#endif
    return 0;
}



const char*
ColorConfig::getDisplayNameByIndex(int index) const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getDisplay(index);
#endif
    return NULL;
}



std::vector<std::string>
ColorConfig::getDisplayNames() const
{
    std::vector<std::string> result;
    for (int i = 0, e = getNumDisplays(); i != e; ++i)
        result.emplace_back(getDisplayNameByIndex(i));
    return result;
}



int
ColorConfig::getNumViews(string_view display) const
{
#ifdef USE_OCIO
    if (display.empty())
        display = getDefaultDisplayName();
    if (getImpl()->config_)
        return getImpl()->config_->getNumViews(display.c_str());
#endif
    return 0;
}



const char*
ColorConfig::getViewNameByIndex(string_view display, int index) const
{
#ifdef USE_OCIO
    if (display.empty())
        display = getDefaultDisplayName();
    if (getImpl()->config_)
        return getImpl()->config_->getView(display.c_str(), index);
#endif
    return NULL;
}



std::vector<std::string>
ColorConfig::getViewNames(string_view display) const
{
    std::vector<std::string> result;
    if (display.empty())
        display = getDefaultDisplayName();
    for (int i = 0, e = getNumViews(display); i != e; ++i)
        result.emplace_back(getViewNameByIndex(display, i));
    return result;
}



const char*
ColorConfig::getDefaultDisplayName() const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getDefaultDisplay();
#endif
    return NULL;
}



const char*
ColorConfig::getDefaultViewName(string_view display) const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->config_->getDefaultView(display.c_str());
#endif
    return NULL;
}



std::string
ColorConfig::configname() const
{
#ifdef USE_OCIO
    if (getImpl()->config_)
        return getImpl()->configname();
#endif
    return "built-in";
}



#ifdef USE_OCIO

#    if OCIO_VERSION_HEX >= 0x02000000
inline OCIO::BitDepth
ocio_bitdepth(TypeDesc type)
{
    if (type == TypeDesc::UINT8)
        return OCIO::BIT_DEPTH_UINT8;
    if (type == TypeDesc::UINT16)
        return OCIO::BIT_DEPTH_UINT16;
    if (type == TypeDesc::UINT32)
        return OCIO::BIT_DEPTH_UINT32;
    // N.B.: OCIOv2 also supports 10, 12, and 14 bit int, but we won't
    // ever have data in that format at this stage.
    if (type == TypeDesc::HALF)
        return OCIO::BIT_DEPTH_F16;
    if (type == TypeDesc::FLOAT)
        return OCIO::BIT_DEPTH_F32;
    return OCIO::BIT_DEPTH_UNKNOWN;
}
#    endif


// Custom ColorProcessor that wraps an OpenColorIO Processor.
class ColorProcessor_OCIO final : public ColorProcessor {
public:
    ColorProcessor_OCIO(OCIO::ConstProcessorRcPtr p)
        : m_p(p)
#    if OCIO_VERSION_HEX >= 0x02000000
        , m_cpuproc(p->getDefaultCPUProcessor())
#    endif
    {
    }
    virtual ~ColorProcessor_OCIO(void) {}

    virtual bool isNoOp() const { return m_p->isNoOp(); }
    virtual bool hasChannelCrosstalk() const
    {
        return m_p->hasChannelCrosstalk();
    }
    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
#    if OCIO_VERSION_HEX >= 0x02000000
        OCIO::PackedImageDesc pid(data, width, height, channels,
                                  OCIO::BIT_DEPTH_F32,  // For now, only float
                                  chanstride, xstride, ystride);
        m_cpuproc->apply(pid);
#    else
        OCIO::PackedImageDesc pid(data, width, height, channels, chanstride,
                                  xstride, ystride);
        m_p->apply(pid);
#    endif
    }

private:
    OCIO::ConstProcessorRcPtr m_p;
#    if OCIO_VERSION_HEX >= 0x02000000
    OCIO::ConstCPUProcessorRcPtr m_cpuproc;
#    endif
};
#endif



// ColorProcessor that hard-codes sRGB-to-linear
class ColorProcessor_sRGB_to_linear final : public ColorProcessor {
public:
    ColorProcessor_sRGB_to_linear()
        : ColorProcessor() {};
    ~ColorProcessor_sRGB_to_linear() {};

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        if (channels > 3)
            channels = 3;
        if (channels == 3 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    simd::vfloat4 r;
                    r.load((float*)d, 3);
                    r = sRGB_to_linear(r);
                    r.store((float*)d, 3);
                }
            }
        } else {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    char* dc = d;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        ((float*)dc)[c] = sRGB_to_linear(((float*)dc)[c]);
                }
            }
        }
    }
};


// ColorProcessor that hard-codes linear-to-sRGB
class ColorProcessor_linear_to_sRGB final : public ColorProcessor {
public:
    ColorProcessor_linear_to_sRGB()
        : ColorProcessor() {};
    ~ColorProcessor_linear_to_sRGB() {};

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        if (channels > 3)
            channels = 3;
        if (channels == 3 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    simd::vfloat4 r;
                    r.load((float*)d, 3);
                    r = linear_to_sRGB(r);
                    r.store((float*)d, 3);
                }
            }
        } else {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    char* dc = d;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        ((float*)dc)[c] = linear_to_sRGB(((float*)dc)[c]);
                }
            }
        }
    }
};



// ColorProcessor that hard-codes Rec709-to-linear
class ColorProcessor_Rec709_to_linear final : public ColorProcessor {
public:
    ColorProcessor_Rec709_to_linear()
        : ColorProcessor() {};
    ~ColorProcessor_Rec709_to_linear() {};

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        if (channels > 3)
            channels = 3;
        for (int y = 0; y < height; ++y) {
            char* d = (char*)data + y * ystride;
            for (int x = 0; x < width; ++x, d += xstride) {
                char* dc = d;
                for (int c = 0; c < channels; ++c, dc += chanstride)
                    ((float*)d)[c] = Rec709_to_linear(((float*)d)[c]);
            }
        }
    }
};


// ColorProcessor that hard-codes linear-to-Rec709
class ColorProcessor_linear_to_Rec709 final : public ColorProcessor {
public:
    ColorProcessor_linear_to_Rec709()
        : ColorProcessor() {};
    ~ColorProcessor_linear_to_Rec709() {};

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        if (channels > 3)
            channels = 3;
        for (int y = 0; y < height; ++y) {
            char* d = (char*)data + y * ystride;
            for (int x = 0; x < width; ++x, d += xstride) {
                char* dc = d;
                for (int c = 0; c < channels; ++c, dc += chanstride)
                    ((float*)d)[c] = linear_to_Rec709(((float*)d)[c]);
            }
        }
    }
};



// ColorProcessor that performs gamma correction
class ColorProcessor_gamma final : public ColorProcessor {
public:
    ColorProcessor_gamma(float gamma)
        : ColorProcessor()
        , m_gamma(gamma) {};
    ~ColorProcessor_gamma() {};

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        if (channels > 3)
            channels = 3;
        if (channels == 3 && chanstride == sizeof(float)) {
            simd::vfloat4 g = m_gamma;
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    simd::vfloat4 r;
                    r.load((float*)d, 3);
                    r = fast_pow_pos(r, g);
                    r.store((float*)d, 3);
                }
            }
        } else {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    char* dc = d;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        ((float*)d)[c] = powf(((float*)d)[c], m_gamma);
                }
            }
        }
    }

private:
    float m_gamma;
};


// ColorProcessor that does nothing (identity transform)
class ColorProcessor_Ident final : public ColorProcessor {
public:
    ColorProcessor_Ident()
        : ColorProcessor()
    {
    }
    ~ColorProcessor_Ident() {}
    virtual void apply(float* /*data*/, int /*width*/, int /*height*/,
                       int /*channels*/, stride_t /*chanstride*/,
                       stride_t /*xstride*/, stride_t /*ystride*/) const
    {
    }
};



// ColorProcessor that implements a matrix multiply color transformation.
class ColorProcessor_Matrix final : public ColorProcessor {
public:
    ColorProcessor_Matrix(const Imath::M44f& Matrix, bool inverse)
        : ColorProcessor()
        , m_M(Matrix)
    {
        if (inverse)
            m_M = m_M.inverse();
    }
    ~ColorProcessor_Matrix() {}

    virtual void apply(float* data, int width, int height, int channels,
                       stride_t chanstride, stride_t xstride,
                       stride_t ystride) const
    {
        using namespace simd;
        if (channels == 3 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    color.load((float*)d, 3);
                    vfloat4 xcolor = color * m_M;
                    xcolor.store((float*)d, 3);
                }
            }
        } else if (channels >= 4 && chanstride == sizeof(float)) {
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    color.load((float*)d);
                    vfloat4 xcolor = color * m_M;
                    xcolor.store((float*)d);
                }
            }
        } else {
            channels = std::min(channels, 4);
            for (int y = 0; y < height; ++y) {
                char* d = (char*)data + y * ystride;
                for (int x = 0; x < width; ++x, d += xstride) {
                    vfloat4 color;
                    char* dc = d;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        color[c] = *(float*)dc;
                    vfloat4 xcolor = color * m_M;
                    for (int c = 0; c < channels; ++c, dc += chanstride)
                        *(float*)dc = xcolor[c];
                }
            }
        }
    }

private:
    simd::matrix44 m_M;
};



ColorProcessorHandle
ColorConfig::createColorProcessor(string_view inputColorSpace,
                                  string_view outputColorSpace,
                                  string_view context_key,
                                  string_view context_value) const
{
    return createColorProcessor(ustring(inputColorSpace),
                                ustring(outputColorSpace), ustring(context_key),
                                ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createColorProcessor(ustring inputColorSpace,
                                  ustring outputColorSpace, ustring context_key,
                                  ustring context_value) const
{
    ustring inputrole, outputrole;
    std::string pending_error;

    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, outputColorSpace, context_key,
                              context_value);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

#ifdef USE_OCIO
    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    OCIO::ConstProcessorRcPtr p;
    if (getImpl()->config_) {
        // If the names are roles, convert them to color space names
        string_view name;
        name = getColorSpaceNameByRole(inputColorSpace);
        if (!name.empty()) {
            inputrole       = inputColorSpace;
            inputColorSpace = name;
        }
        name = getColorSpaceNameByRole(outputColorSpace);
        if (!name.empty()) {
            outputrole       = outputColorSpace;
            outputColorSpace = name;
        }

        OCIO::ConstConfigRcPtr config   = getImpl()->config_;
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        std::vector<string_view> keys, values;
        Strutil::split(context_key, keys, ",");
        Strutil::split(context_value, values, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        try {
            // Get the processor corresponding to this transform.
            p = getImpl()->config_->getProcessor(context,
                                                 inputColorSpace.c_str(),
                                                 outputColorSpace.c_str());
            getImpl()->clear_error();
        } catch (OCIO::Exception& e) {
            // Don't quit yet, remember the error and see if any of our
            // built-in knowledge of some generic spaces will save us.
            p.reset();
            pending_error = e.what();
        } catch (...) {
            p.reset();
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }

        if (p && !p->isNoOp()) {
            // If we got a valid processor that does something useful,
            // return it now. If it boils down to a no-op, give a second
            // chance below to recognize it as a special case.
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        }
    }
#endif

    if (!handle) {
        // Either not compiled with OCIO support, or no OCIO configuration
        // was found at all.  There are a few color conversions we know
        // about even in such dire conditions.
        using namespace Strutil;
        if (iequals(inputColorSpace, outputColorSpace)) {
            handle = ColorProcessorHandle(new ColorProcessor_Ident);
        } else if ((iequals(inputColorSpace, "linear")
                    || iequals(inputrole, "linear")
                    || iequals(inputColorSpace, "lnf")
                    || iequals(inputColorSpace, "lnh"))
                   && iequals(outputColorSpace, "sRGB")) {
            handle = ColorProcessorHandle(new ColorProcessor_linear_to_sRGB);
        } else if (iequals(inputColorSpace, "sRGB")
                   && (iequals(outputColorSpace, "linear")
                       || iequals(outputrole, "linear")
                       || iequals(outputColorSpace, "lnf")
                       || iequals(outputColorSpace, "lnh"))) {
            handle = ColorProcessorHandle(new ColorProcessor_sRGB_to_linear);
        } else if ((iequals(inputColorSpace, "linear")
                    || iequals(inputrole, "linear")
                    || iequals(inputColorSpace, "lnf")
                    || iequals(inputColorSpace, "lnh"))
                   && iequals(outputColorSpace, "Rec709")) {
            handle = ColorProcessorHandle(new ColorProcessor_linear_to_Rec709);
        } else if (iequals(inputColorSpace, "Rec709")
                   && (iequals(outputColorSpace, "linear")
                       || iequals(outputrole, "linear")
                       || iequals(outputColorSpace, "lnf")
                       || iequals(outputColorSpace, "lnh"))) {
            handle = ColorProcessorHandle(new ColorProcessor_Rec709_to_linear);
        } else if ((iequals(inputColorSpace, "linear")
                    || iequals(inputrole, "linear")
                    || iequals(inputColorSpace, "lnf")
                    || iequals(inputColorSpace, "lnh"))
                   && istarts_with(outputColorSpace, "GammaCorrected")) {
            string_view gamstr = outputColorSpace;
            Strutil::parse_prefix(gamstr, "GammaCorrected");
            float g = from_string<float>(gamstr);
            handle  = ColorProcessorHandle(new ColorProcessor_gamma(1.0f / g));
        } else if (istarts_with(inputColorSpace, "GammaCorrected")
                   && (iequals(outputColorSpace, "linear")
                       || iequals(outputrole, "linear")
                       || iequals(outputColorSpace, "lnf")
                       || iequals(outputColorSpace, "lnh"))) {
            string_view gamstr = inputColorSpace;
            Strutil::parse_prefix(gamstr, "GammaCorrected");
            float g = from_string<float>(gamstr);
            handle  = ColorProcessorHandle(new ColorProcessor_gamma(g));
        }
    }

#ifdef USE_OCIO
    if (!handle && p) {
        // If we found a processor from OCIO, even if it was a NoOp, and we
        // still don't have a better idea, return it.
        handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
    }
#endif

    if (pending_error.size())
        getImpl()->error(pending_error);

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createLookTransform(string_view looks, string_view inputColorSpace,
                                 string_view outputColorSpace, bool inverse,
                                 string_view context_key,
                                 string_view context_value) const
{
    return createLookTransform(ustring(looks), ustring(inputColorSpace),
                               ustring(outputColorSpace), inverse,
                               ustring(context_key), ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createLookTransform(ustring looks, ustring inputColorSpace,
                                 ustring outputColorSpace, bool inverse,
                                 ustring context_key,
                                 ustring context_value) const
{
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, outputColorSpace, context_key,
                              context_value, looks, ustring() /*display*/,
                              ustring() /*view*/, ustring() /*file*/, inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

#ifdef USE_OCIO
    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    if (getImpl()->config_) {
        OCIO::ConstConfigRcPtr config      = getImpl()->config_;
        OCIO::LookTransformRcPtr transform = OCIO::LookTransform::Create();
        transform->setLooks(looks.c_str());
        OCIO::TransformDirection dir;
        if (inverse) {
            // The TRANSFORM_DIR_INVERSE applies an inverse for the
            // end-to-end transform, which would otherwise do dst->inv
            // look -> src.  This is an unintuitive result for the
            // artist (who would expect in, out to remain unchanged), so
            // we account for that here by flipping src/dst
            transform->setSrc(outputColorSpace.c_str());
            transform->setDst(inputColorSpace.c_str());
            dir = OCIO::TRANSFORM_DIR_INVERSE;
        } else {  // forward
            transform->setSrc(inputColorSpace.c_str());
            transform->setDst(outputColorSpace.c_str());
            dir = OCIO::TRANSFORM_DIR_FORWARD;
        }
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        std::vector<string_view> keys, values;
        Strutil::split(context_key, keys, ",");
        Strutil::split(context_value, values, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = getImpl()->config_->getProcessor(context, transform, dir);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }
#endif

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createDisplayTransform(string_view display, string_view view,
                                    string_view inputColorSpace,
                                    string_view looks, string_view context_key,
                                    string_view context_value) const
{
    return createDisplayTransform(ustring(display), ustring(view),
                                  ustring(inputColorSpace), ustring(looks),
                                  ustring(context_key), ustring(context_value));
}



ColorProcessorHandle
ColorConfig::createDisplayTransform(ustring display, ustring view,
                                    ustring inputColorSpace, ustring looks,
                                    ustring context_key,
                                    ustring context_value) const
{
    if (display.empty())
        display = getDefaultDisplayName();
    if (view.empty())
        view = getDefaultViewName();
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(inputColorSpace, ustring() /*outputColorSpace*/,
                              context_key, context_value, looks, display, view);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

#ifdef USE_OCIO
    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    if (getImpl()->config_) {
        OCIO::ConstConfigRcPtr config = getImpl()->config_;
#    ifdef OCIO_v2
        auto transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc(inputColorSpace.c_str());
        if (looks.size()) {
            getImpl()->error(
                "createDisplayTransform: looks overrides are not allowed in OpenColorIO v2");
        }
#    else
        auto transform = OCIO::DisplayTransform::Create();
        transform->setInputColorSpaceName(inputColorSpace.c_str());
        if (looks.size()) {
            transform->setLooksOverride(looks.c_str());
            transform->setLooksOverrideEnabled(true);
        } else {
            transform->setLooksOverrideEnabled(false);
        }
#    endif
        transform->setDisplay(display.c_str());
        transform->setView(view.c_str());
        auto context = config->getCurrentContext();
        auto keys    = Strutil::splits(context_key, ",");
        auto values  = Strutil::splits(context_value, ",");
        if (keys.size() && values.size() && keys.size() == values.size()) {
            OCIO::ContextRcPtr ctx = context->createEditableCopy();
            for (size_t i = 0; i < keys.size(); ++i)
                ctx->setStringVar(keys[i].c_str(), values[i].c_str());
            context = ctx;
        }

        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = getImpl()->config_->getProcessor(context, transform,
                                                 OCIO::TRANSFORM_DIR_FORWARD);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }
#endif

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createFileTransform(string_view name, bool inverse) const
{
    return createFileTransform(ustring(name), inverse);
}



ColorProcessorHandle
ColorConfig::createFileTransform(ustring name, bool inverse) const
{
    // First, look up the requested processor in the cache. If it already
    // exists, just return it.
    ColorProcCacheKey prockey(ustring() /*inputColorSpace*/,
                              ustring() /*outputColorSpace*/,
                              ustring() /*context_key*/,
                              ustring() /*context_value*/, ustring() /*looks*/,
                              ustring() /*display*/, ustring() /*view*/, name,
                              inverse);
    ColorProcessorHandle handle = getImpl()->findproc(prockey);
    if (handle)
        return handle;

#ifdef USE_OCIO
    // Ask OCIO to make a Processor that can handle the requested
    // transformation.
    OCIO::ConstConfigRcPtr config = getImpl()->config_;
    // If no config was found, config_ will be null. But that shouldn't
    // stop us for a filetransform, which doesn't need color spaces anyway.
    // Just use the default current config, it'll be freed when we exit.
    if (!config)
        config = ocio_current_config;
    if (config) {
        OCIO::FileTransformRcPtr transform = OCIO::FileTransform::Create();
        transform->setSrc(name.c_str());
        transform->setInterpolation(OCIO::INTERP_BEST);
        OCIO::TransformDirection dir    = inverse ? OCIO::TRANSFORM_DIR_INVERSE
                                                  : OCIO::TRANSFORM_DIR_FORWARD;
        OCIO::ConstContextRcPtr context = config->getCurrentContext();
        OCIO::ConstProcessorRcPtr p;
        try {
            // Get the processor corresponding to this transform.
            p = getImpl()->config_->getProcessor(context, transform, dir);
            getImpl()->clear_error();
            handle = ColorProcessorHandle(new ColorProcessor_OCIO(p));
        } catch (OCIO::Exception& e) {
            getImpl()->error(e.what());
        } catch (...) {
            getImpl()->error(
                "An unknown error occurred in OpenColorIO, getProcessor");
        }
    }
#endif

    return getImpl()->addproc(prockey, handle);
}



ColorProcessorHandle
ColorConfig::createMatrixTransform(const Imath::M44f& M, bool inverse) const
{
    return ColorProcessorHandle(new ColorProcessor_Matrix(M, inverse));
}



string_view
ColorConfig::getColorSpaceFromFilepath(string_view str) const
{
#if defined(USE_OCIO) && (OCIO_VERSION_HEX >= MAKE_OCIO_VERSION_HEX(2, 1, 0))
    if (getImpl() && getImpl()->config_) {
        string_view r = getImpl()->config_->getColorSpaceFromFilepath(
            str.c_str());
        if (!getImpl()->config_->filepathOnlyMatchesDefaultRule(str.c_str()))
            return r;
    }
#endif
    // Fall back on parseColorSpaceFromString
    return parseColorSpaceFromString(str);
}



string_view
ColorConfig::parseColorSpaceFromString(string_view str) const
{
#if defined(USE_OCIO) && (OCIO_VERSION_HEX < MAKE_OCIO_VERSION_HEX(2, 1, 0))
    if (getImpl() && getImpl()->config_)
        return getImpl()->config_->parseColorSpaceFromString(str.c_str());
#endif

    // Reproduce the logic in OCIO v1 parseColorSpaceFromSring

    if (str.empty())
        return "";

    // Get the colorspace names, sorted shortest-to-longest
    auto names = getColorSpaceNames();
    std::sort(names.begin(), names.end(),
              [](const std::string& a, const std::string& b) {
                  return a.length() < b.length();
              });

    // See if it matches a LUT name.
    // This is the position of the RIGHT end of the colorspace substring,
    // not the left
    size_t rightMostColorPos = std::string::npos;
    std::string rightMostColorspace;

    // Find the right-most occcurance within the string for each colorspace.
    for (auto&& csname : names) {
        // find right-most extension matched in filename
        size_t pos = Strutil::irfind(str, csname);
        if (pos == std::string::npos)
            continue;

        // If we have found a match, move the pointer over to the right end
        // of the substring.  This will allow us to find the longest name
        // that matches the rightmost colorspace
        pos += csname.size();

        if (rightMostColorPos == std::string::npos
            || pos >= rightMostColorPos) {
            rightMostColorPos   = pos;
            rightMostColorspace = csname;
        }
    }
    return string_view(ustring(rightMostColorspace));
}



//////////////////////////////////////////////////////////////////////////
//
// Image Processing Implementations


bool
ImageBufAlgo::colorconvert(ImageBuf& dst, const ImageBuf& src, string_view from,
                           string_view to, bool unpremult,
                           string_view context_key, string_view context_value,
                           ColorConfig* colorconfig, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::colorconvert");
    if (from.empty() || from == "current") {
        from = src.spec().get_string_attribute("oiio:Colorspace", "Linear");
    }
    if (from.empty() || to.empty()) {
        dst.errorfmt("Unknown color space name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        spin_lock lock(colorconfig_mutex);
        if (!colorconfig)
            colorconfig = default_colorconfig.get();
        if (!colorconfig)
            default_colorconfig.reset(colorconfig = new ColorConfig);
        processor = colorconfig->createColorProcessor(from, to, context_key,
                                                      context_value);
        if (!processor) {
            if (colorconfig->error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt("Could not construct the color transform {} -> {}",
                             from, to);
            return false;
        }
    }

    logtime.stop();  // transition to other colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok)
        dst.specmod().attribute("oiio:ColorSpace", to);
    return ok;
}



ImageBuf
ImageBufAlgo::colorconvert(const ImageBuf& src, string_view from,
                           string_view to, bool unpremult,
                           string_view context_key, string_view context_value,
                           ColorConfig* colorconfig, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = colorconvert(result, src, from, to, unpremult, context_key,
                           context_value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colorconvert() error");
    return result;
}



bool
ImageBufAlgo::colormatrixtransform(ImageBuf& dst, const ImageBuf& src,
                                   const Imath::M44f& M, bool unpremult,
                                   ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::colormatrixtransform");
    ColorProcessorHandle processor;
    {
        spin_lock lock(colorconfig_mutex);
        auto colorconfig = default_colorconfig.get();
        if (!colorconfig)
            default_colorconfig.reset(colorconfig = new ColorConfig);
        processor = colorconfig->createMatrixTransform(M);
    }

    logtime.stop();  // transition to other colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::colormatrixtransform(const ImageBuf& src, const Imath::M44f& M,
                                   bool unpremult, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = colormatrixtransform(result, src, M, unpremult, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colormatrixtransform() error");
    return result;
}



template<class Rtype, class Atype>
static bool
colorconvert_impl(ImageBuf& R, const ImageBuf& A,
                  const ColorProcessor* processor, bool unpremult, ROI roi,
                  int nthreads)
{
    using namespace ImageBufAlgo;
    using namespace simd;
    // Only process up to, and including, the first 4 channels.  This
    // does let us process images with fewer than 4 channels, which is
    // the intent.
    int channelsToCopy = std::min(4, roi.nchannels());
    if (channelsToCopy < 4)
        unpremult = false;
    // clang-format off
    parallel_image(
        roi, parallel_options(nthreads),
        [&, unpremult, channelsToCopy, processor](ROI roi) {
            int width = roi.width();
            // Temporary space to hold one RGBA scanline
            vfloat4* scanline;
            OIIO_ALLOCATE_STACK_OR_HEAP(scanline, vfloat4, width);
            float* alpha;
            OIIO_ALLOCATE_STACK_OR_HEAP(alpha, float, width);
            const float fltmin = std::numeric_limits<float>::min();
            ImageBuf::ConstIterator<Atype> a(A, roi);
            ImageBuf::Iterator<Rtype> r(R, roi);
            for (int k = roi.zbegin; k < roi.zend; ++k) {
                for (int j = roi.ybegin; j < roi.yend; ++j) {
                    // Load the scanline
                    a.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                    for (int i = 0; !a.done(); ++a, ++i) {
                        vfloat4 v(0.0f);
                        for (int c = 0; c < channelsToCopy; ++c)
                            v[c] = a[c];
                        if (channelsToCopy == 1)
                            v[2] = v[1] = v[0];
                        scanline[i] = v;
                    }

                    // Optionally unpremult. Be careful of alpha==0 pixels,
                    // preserve their color rather than div-by-zero.
                    if (unpremult) {
                        for (int i = 0; i < width; ++i) {
                            float a  = extract<3>(scanline[i]);
                            alpha[i] = a;
                            a        = a >= fltmin ? a : 1.0f;
                            scanline[i] /= vfloat4(a,a,a,1.0f);
                        }
                    }

                    // Apply the color transformation in place
                    processor->apply((float*)&scanline[0], width, 1, 4,
                                     sizeof(float), 4 * sizeof(float),
                                     width * 4 * sizeof(float));

                    // Optionally re-premult. Be careful of alpha==0 pixels,
                    // preserve their value rather than crushing to black.
                    if (unpremult) {
                        for (int i = 0; i < width; ++i) {
                            float a  = alpha[i];
                            a        = a >= fltmin ? a : 1.0f;
                            scanline[i] *= vfloat4(a,a,a,1.0f);
                        }
                    }

                    // Store the scanline
                    float* dstPtr = (float*)&scanline[0];
                    r.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                    for (; !r.done(); ++r, dstPtr += 4)
                        for (int c = 0; c < channelsToCopy; ++c)
                            r[c] = dstPtr[c];
                    if (channelsToCopy < roi.chend && (&R != &A)) {
                        // If there are "leftover" channels, just copy them
                        // unaltered from the source.
                        a.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                        r.rerange(roi.xbegin, roi.xend, j, j + 1, k, k + 1);
                        for (; !r.done(); ++r, ++a)
                            for (int c = channelsToCopy; c < roi.chend; ++c)
                                r[c] = 0.5 + 10 * a[c];
                    }
                }
            }
        });
    // clang-format on
    return true;
}



// Specialized version where both buffers are in memory (not cache based),
// float data, and we are dealing with 4 channels.
static bool
colorconvert_impl_float_rgba(ImageBuf& R, const ImageBuf& A,
                             const ColorProcessor* processor, bool unpremult,
                             ROI roi, int nthreads)
{
    using namespace ImageBufAlgo;
    using namespace simd;
    OIIO_ASSERT(R.localpixels() && A.localpixels()
                && R.spec().format == TypeFloat && A.spec().format == TypeFloat
                && R.nchannels() == 4 && A.nchannels() == 4);
    parallel_image(roi, parallel_options(nthreads), [&](ROI roi) {
        int width = roi.width();
        // Temporary space to hold one RGBA scanline
        vfloat4* scanline;
        OIIO_ALLOCATE_STACK_OR_HEAP(scanline, vfloat4, width);
        float* alpha;
        OIIO_ALLOCATE_STACK_OR_HEAP(alpha, float, width);
        const float fltmin = std::numeric_limits<float>::min();
        for (int k = roi.zbegin; k < roi.zend; ++k) {
            for (int j = roi.ybegin; j < roi.yend; ++j) {
                // Load the scanline
                memcpy((void*)scanline, A.pixeladdr(roi.xbegin, j, k),
                       width * 4 * sizeof(float));
                // Optionally unpremult
                if (unpremult) {
                    for (int i = 0; i < width; ++i) {
                        vfloat4 p(scanline[i]);
                        float a  = extract<3>(p);
                        alpha[i] = a;
                        a        = a >= fltmin ? a : 1.0f;
                        if (a == 1.0f)
                            scanline[i] = p;
                        else
                            scanline[i] = p / vfloat4(a, a, a, 1.0f);
                    }
                }

                // Apply the color transformation in place
                processor->apply((float*)&scanline[0], width, 1, 4,
                                 sizeof(float), 4 * sizeof(float),
                                 width * 4 * sizeof(float));

                // Optionally premult
                if (unpremult) {
                    for (int i = 0; i < width; ++i) {
                        vfloat4 p(scanline[i]);
                        float a = alpha[i];
                        a       = a >= fltmin ? a : 1.0f;
                        p *= vfloat4(a, a, a, 1.0f);
                        scanline[i] = p;
                    }
                }
                memcpy(R.pixeladdr(roi.xbegin, j, k), scanline,
                       width * 4 * sizeof(float));
            }
        }
    });
    return true;
}



bool
ImageBufAlgo::colorconvert(ImageBuf& dst, const ImageBuf& src,
                           const ColorProcessor* processor, bool unpremult,
                           ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::colorconvert");
    // If the processor is NULL, return false (error)
    if (!processor) {
        dst.errorfmt(
            "Passed NULL ColorProcessor to colorconvert() [probable application bug]");
        return false;
    }

    // If the processor is a no-op and the conversion is being done
    // in place, no work needs to be done. Early exit.
    if (processor->isNoOp() && (&dst == &src))
        return true;

    if (!IBAprep(roi, &dst, &src))
        return false;

    // If the processor is a no-op (and it's not an in-place conversion),
    // use copy() to simplify the operation.
    if (processor->isNoOp()) {
        logtime.stop();  // transition to copy
        return ImageBufAlgo::copy(dst, src, TypeUnknown, roi, nthreads);
    }

    if (unpremult && src.spec().alpha_channel >= 0
        && src.spec().get_int_attribute("oiio:UnassociatedAlpha") != 0) {
        // If we appear to be operating on an image that already has
        // unassociated alpha, don't do a redundant unpremult step.
        unpremult = false;
    }

    if (dst.localpixels() && src.localpixels() && dst.spec().format == TypeFloat
        && src.spec().format == TypeFloat && dst.nchannels() == 4
        && src.nchannels() == 4) {
        return colorconvert_impl_float_rgba(dst, src, processor, unpremult, roi,
                                            nthreads);
    }

    bool ok = true;
    OIIO_DISPATCH_COMMON_TYPES2(ok, "colorconvert", colorconvert_impl,
                                dst.spec().format, src.spec().format, dst, src,
                                processor, unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::colorconvert(const ImageBuf& src, const ColorProcessor* processor,
                           bool unpremult, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = colorconvert(result, src, processor, unpremult, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::colorconvert() error");
    return result;
}



bool
ImageBufAlgo::ociolook(ImageBuf& dst, const ImageBuf& src, string_view looks,
                       string_view from, string_view to, bool unpremult,
                       bool inverse, string_view key, string_view value,
                       ColorConfig* colorconfig, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::ociolook");
    if (from.empty() || from == "current") {
        from = src.spec().get_string_attribute("oiio:Colorspace", "Linear");
    }
    if (to.empty() || to == "current") {
        to = src.spec().get_string_attribute("oiio:Colorspace", "Linear");
    }
    if (from.empty() || to.empty()) {
        dst.errorfmt("Unknown color space name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        spin_lock lock(colorconfig_mutex);
        if (!colorconfig)
            colorconfig = default_colorconfig.get();
        if (!colorconfig)
            default_colorconfig.reset(colorconfig = new ColorConfig);
        processor = colorconfig->createLookTransform(looks, from, to, inverse,
                                                     key, value);
        if (!processor) {
            if (colorconfig->error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt("Could not construct the color transform");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok)
        dst.specmod().attribute("oiio:ColorSpace", to);
    return ok;
}



ImageBuf
ImageBufAlgo::ociolook(const ImageBuf& src, string_view looks, string_view from,
                       string_view to, bool unpremult, bool inverse,
                       string_view key, string_view value,
                       ColorConfig* colorconfig, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = ociolook(result, src, looks, from, to, unpremult, inverse, key,
                       value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociolook() error");
    return result;
}



bool
ImageBufAlgo::ociodisplay(ImageBuf& dst, const ImageBuf& src,
                          string_view display, string_view view,
                          string_view from, string_view looks, bool unpremult,
                          string_view key, string_view value,
                          ColorConfig* colorconfig, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::ociodisplay");
    ColorProcessorHandle processor;
    {
        spin_lock lock(colorconfig_mutex);
        if (!colorconfig)
            colorconfig = default_colorconfig.get();
        if (!colorconfig)
            default_colorconfig.reset(colorconfig = new ColorConfig);
        if (from.empty() || from == "current") {
            auto linearspace = colorconfig->getColorSpaceNameByRole("linear");
            from = src.spec().get_string_attribute("oiio:Colorspace",
                                                   linearspace);
        }
        if (from.empty()) {
            dst.errorfmt("Unknown color space name");
            return false;
        }
        processor = colorconfig->createDisplayTransform(display, view, from,
                                                        looks, key, value);
        if (!processor) {
            if (colorconfig->error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt("Could not construct the color transform");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    return ok;
}



ImageBuf
ImageBufAlgo::ociodisplay(const ImageBuf& src, string_view display,
                          string_view view, string_view from, string_view looks,
                          bool unpremult, string_view key, string_view value,
                          ColorConfig* colorconfig, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = ociodisplay(result, src, display, view, from, looks, unpremult,
                          key, value, colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociodisplay() error");
    return result;
}



bool
ImageBufAlgo::ociofiletransform(ImageBuf& dst, const ImageBuf& src,
                                string_view name, bool unpremult, bool inverse,
                                ColorConfig* colorconfig, ROI roi, int nthreads)
{
    pvt::LoggedTimer logtime("IBA::ociofiletransform");
    if (name.empty()) {
        dst.errorfmt("Unknown filetransform name");
        return false;
    }
    ColorProcessorHandle processor;
    {
        spin_lock lock(colorconfig_mutex);
        if (!colorconfig)
            colorconfig = default_colorconfig.get();
        if (!colorconfig)
            default_colorconfig.reset(colorconfig = new ColorConfig);
        processor = colorconfig->createFileTransform(name, inverse);
        if (!processor) {
            if (colorconfig->error())
                dst.errorfmt("{}", colorconfig->geterror());
            else
                dst.errorfmt("Could not construct the color transform");
            return false;
        }
    }

    logtime.stop();  // transition to colorconvert
    bool ok = colorconvert(dst, src, processor.get(), unpremult, roi, nthreads);
    if (ok)
        dst.specmod().attribute("oiio:ColorSpace", name);
    return ok;
}



ImageBuf
ImageBufAlgo::ociofiletransform(const ImageBuf& src, string_view name,
                                bool unpremult, bool inverse,
                                ColorConfig* colorconfig, ROI roi, int nthreads)
{
    ImageBuf result;
    bool ok = ociofiletransform(result, src, name, unpremult, inverse,
                                colorconfig, roi, nthreads);
    if (!ok && !result.has_error())
        result.errorfmt("ImageBufAlgo::ociofiletransform() error");
    return result;
}



bool
ImageBufAlgo::colorconvert(span<float> color, const ColorProcessor* processor,
                           bool unpremult)
{
    // If the processor is NULL, return false (error)
    if (!processor) {
        return false;
    }

    // If the processor is a no-op, no work needs to be done. Early exit.
    if (processor->isNoOp())
        return true;

    // Load the pixel
    float rgba[4]      = { 0.0f, 0.0f, 0.0f, 0.0f };
    int channelsToCopy = std::min(4, (int)color.size());
    memcpy(rgba, color.data(), channelsToCopy * sizeof(float));

    const float fltmin = std::numeric_limits<float>::min();

    // Optionally unpremult
    if ((channelsToCopy >= 4) && unpremult) {
        float alpha = rgba[3];
        if (alpha > fltmin) {
            rgba[0] /= alpha;
            rgba[1] /= alpha;
            rgba[2] /= alpha;
        }
    }

    // Apply the color transformation
    processor->apply(rgba, 1, 1, 4, sizeof(float), 4 * sizeof(float),
                     4 * sizeof(float));

    // Optionally premult
    if ((channelsToCopy >= 4) && unpremult) {
        float alpha = rgba[3];
        if (alpha > fltmin) {
            rgba[0] *= alpha;
            rgba[1] *= alpha;
            rgba[2] *= alpha;
        }
    }

    // Store the scanline
    memcpy(color.data(), rgba, channelsToCopy * sizeof(float));

    return true;
}



OIIO_NAMESPACE_END
