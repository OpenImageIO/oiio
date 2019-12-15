// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <numeric>

#include <boost/version.hpp>
#if BOOST_VERSION >= 106900
#    include <boost/integer/common_factor_rt.hpp>
using boost::integer::gcd;
#else
#    include <boost/math/common_factor_rt.hpp>
using boost::math::gcd;
#endif

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfEnvmap.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfTestFile.h>
#include <OpenEXR/ImfTiledInputFile.h>

// The way that OpenEXR uses dynamic casting for attributes requires
// temporarily suspending "hidden" symbol visibility mode.
#ifdef __GNUC__
#    pragma GCC visibility push(default)
#endif
#include <OpenEXR/IexBaseExc.h>
#include <OpenEXR/IexThrowErrnoExc.h>
#include <OpenEXR/ImfBoxAttribute.h>
#include <OpenEXR/ImfChromaticitiesAttribute.h>
#include <OpenEXR/ImfCompressionAttribute.h>
#include <OpenEXR/ImfDeepFrameBuffer.h>
#include <OpenEXR/ImfDeepScanLineInputPart.h>
#include <OpenEXR/ImfDeepTiledInputPart.h>
#include <OpenEXR/ImfDoubleAttribute.h>
#include <OpenEXR/ImfEnvmapAttribute.h>
#include <OpenEXR/ImfFloatAttribute.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfIntAttribute.h>
#include <OpenEXR/ImfKeyCodeAttribute.h>
#include <OpenEXR/ImfMatrixAttribute.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfRationalAttribute.h>
#include <OpenEXR/ImfStringAttribute.h>
#include <OpenEXR/ImfStringVectorAttribute.h>
#include <OpenEXR/ImfTiledInputPart.h>
#include <OpenEXR/ImfTimeCodeAttribute.h>
#include <OpenEXR/ImfVecAttribute.h>

#ifdef __GNUC__
#    pragma GCC visibility pop
#endif

#include <OpenEXR/ImfCRgbaFile.h>

#include "imageio_pvt.h"
#include <OpenImageIO/dassert.h>
#include <OpenImageIO/deepdata.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imagebufalgo_util.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/sysutil.h>
#include <OpenImageIO/thread.h>


OIIO_PLUGIN_NAMESPACE_BEGIN


// Custom file input stream, copying code from the class StdIFStream in OpenEXR,
// which would have been used if we just provided a filename. The difference is
// that this can handle UTF-8 file paths on all platforms.
class OpenEXRInputStream : public Imf::IStream {
public:
    OpenEXRInputStream(const char* filename, Filesystem::IOProxy* io)
        : Imf::IStream(filename)
        , m_io(io)
    {
        if (!io || io->mode() != Filesystem::IOProxy::Read)
            throw Iex::IoExc("File intput failed.");
    }
    virtual bool read(char c[], int n)
    {
        ASSERT(m_io);
        if (m_io->read(c, n) != size_t(n))
            throw Iex::IoExc("Unexpected end of file.");
        return n;
    }
    virtual Imath::Int64 tellg() { return m_io->tell(); }
    virtual void seekg(Imath::Int64 pos)
    {
        if (!m_io->seek(pos))
            throw Iex::IoExc("File input failed.");
    }
    virtual void clear() {}

private:
    Filesystem::IOProxy* m_io = nullptr;
};



class OpenEXRInput final : public ImageInput {
public:
    OpenEXRInput();
    virtual ~OpenEXRInput() { close(); }
    virtual const char* format_name(void) const override { return "openexr"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "arbitrary_metadata"
                || feature == "exif"  // Because of arbitrary_metadata
                || feature == "iptc"  // Because of arbitrary_metadata
                || feature == "ioproxy");
    }
    virtual bool valid_file(const std::string& filename) const override;
    virtual bool open(const std::string& name, ImageSpec& newspec,
                      const ImageSpec& config) override;
    virtual bool open(const std::string& name, ImageSpec& newspec) override
    {
        return open(name, newspec, ImageSpec());
    }
    virtual bool close() override;
    virtual int current_subimage(void) const override { return m_subimage; }
    virtual int current_miplevel(void) const override { return m_miplevel; }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual ImageSpec spec(int subimage, int miplevel) override;
    virtual ImageSpec spec_dimensions(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool read_native_scanlines(int subimage, int miplevel, int ybegin,
                                       int yend, int z, void* data) override;
    virtual bool read_native_scanlines(int subimage, int miplevel, int ybegin,
                                       int yend, int z, int chbegin, int chend,
                                       void* data) override;
    virtual bool read_native_tile(int subimage, int miplevel, int x, int y,
                                  int z, void* data) override;
    virtual bool read_native_tiles(int subimage, int miplevel, int xbegin,
                                   int xend, int ybegin, int yend, int zbegin,
                                   int zend, void* data) override;
    virtual bool read_native_tiles(int subimage, int miplevel, int xbegin,
                                   int xend, int ybegin, int yend, int zbegin,
                                   int zend, int chbegin, int chend,
                                   void* data) override;
    virtual bool read_native_deep_scanlines(int subimage, int miplevel,
                                            int ybegin, int yend, int z,
                                            int chbegin, int chend,
                                            DeepData& deepdata) override;
    virtual bool read_native_deep_tiles(int subimage, int miplevel, int xbegin,
                                        int xend, int ybegin, int yend,
                                        int zbegin, int zend, int chbegin,
                                        int chend, DeepData& deepdata) override;

private:
    struct PartInfo {
        std::atomic_bool initialized;
        ImageSpec spec;
        int topwidth;      ///< Width of top mip level
        int topheight;     ///< Height of top mip level
        int levelmode;     ///< The level mode
        int roundingmode;  ///< Rounding mode
        bool cubeface;     ///< It's a cubeface environment map
        int nmiplevels;    ///< How many MIP levels are there?
        Imath::Box2i top_datawindow;
        Imath::Box2i top_displaywindow;
        std::vector<Imf::PixelType> pixeltype;  ///< Imf pixel type for each chan
        std::vector<int> chanbytes;  ///< Size (in bytes) of each channel

        PartInfo()
            : initialized(false)
        {
        }
        PartInfo(const PartInfo& p)
            : initialized((bool)p.initialized)
            , spec(p.spec)
            , topwidth(p.topwidth)
            , topheight(p.topheight)
            , levelmode(p.levelmode)
            , roundingmode(p.roundingmode)
            , cubeface(p.cubeface)
            , nmiplevels(p.nmiplevels)
            , top_datawindow(p.top_datawindow)
            , top_displaywindow(p.top_displaywindow)
            , pixeltype(p.pixeltype)
            , chanbytes(p.chanbytes)
        {
        }
        ~PartInfo() {}
        bool parse_header(OpenEXRInput* in, const Imf::Header* header);
        bool query_channels(OpenEXRInput* in, const Imf::Header* header);
        void compute_mipres(int miplevel, ImageSpec& spec) const;
    };
    friend struct PartInfo;

    std::vector<PartInfo> m_parts;               ///< Image parts
    OpenEXRInputStream* m_input_stream;          ///< Stream for input file
    Imf::MultiPartInputFile* m_input_multipart;  ///< Multipart input
    Imf::InputPart* m_scanline_input_part;
    Imf::TiledInputPart* m_tiled_input_part;
    Imf::DeepScanLineInputPart* m_deep_scanline_input_part;
    Imf::DeepTiledInputPart* m_deep_tiled_input_part;
    Imf::InputFile* m_input_scanline;    ///< Input for scanline files
    Imf::TiledInputFile* m_input_tiled;  ///< Input for tiled files
    Filesystem::IOProxy* m_io = nullptr;
    std::unique_ptr<Filesystem::IOProxy> m_local_io;
    int m_subimage;                     ///< What subimage are we looking at?
    int m_nsubimages;                   ///< How many subimages are there?
    int m_miplevel;                     ///< What MIP level are we looking at?
    std::vector<float> m_missingcolor;  ///< Color for missing tile/scanline

    void init()
    {
        m_input_stream             = NULL;
        m_input_multipart          = NULL;
        m_scanline_input_part      = NULL;
        m_tiled_input_part         = NULL;
        m_deep_scanline_input_part = NULL;
        m_deep_tiled_input_part    = NULL;
        m_input_scanline           = NULL;
        m_input_tiled              = NULL;
        m_subimage                 = -1;
        m_miplevel                 = -1;
        m_io                       = nullptr;
        m_local_io.reset();
        m_missingcolor.clear();
    }

    bool valid_file(const std::string& filename, Filesystem::IOProxy* io) const;

    bool read_native_tiles_individually(int subimage, int miplevel, int xbegin,
                                        int xend, int ybegin, int yend,
                                        int zbegin, int zend, int chbegin,
                                        int chend, void* data, stride_t xstride,
                                        stride_t ystride);

    // Fill in with 'missing' color/pattern.
    void fill_missing(int xbegin, int xend, int ybegin, int yend, int zbegin,
                      int zend, int chbegin, int chend, void* data,
                      stride_t xstride, stride_t ystride);
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
openexr_input_imageio_create()
{
    return new OpenEXRInput;
}

// OIIO_EXPORT int openexr_imageio_version = OIIO_PLUGIN_VERSION; // it's in exroutput.cpp

OIIO_EXPORT const char* openexr_input_extensions[] = { "exr", "sxr", "mxr",
                                                       nullptr };

OIIO_PLUGIN_EXPORTS_END



class StringMap {
    typedef std::map<std::string, std::string> map_t;

public:
    StringMap(void) { init(); }

    const std::string& operator[](const std::string& s) const
    {
        map_t::const_iterator i;
        i = m_map.find(s);
        return i == m_map.end() ? s : i->second;
    }

private:
    map_t m_map;

    void init(void)
    {
        // Ones whose name we change to our convention
        m_map["cameraTransform"]  = "worldtocamera";
        m_map["worldToCamera"]    = "worldtocamera";
        m_map["worldToNDC"]       = "worldtoscreen";
        m_map["capDate"]          = "DateTime";
        m_map["comments"]         = "ImageDescription";
        m_map["owner"]            = "Copyright";
        m_map["pixelAspectRatio"] = "PixelAspectRatio";
        m_map["xDensity"]         = "XResolution";
        m_map["expTime"]          = "ExposureTime";
        // Ones we don't rename -- OpenEXR convention matches ours
        m_map["wrapmodes"] = "wrapmodes";
        m_map["aperture"]  = "FNumber";
        // Ones to prefix with openexr:
        m_map["version"]             = "openexr:version";
        m_map["chunkCount"]          = "openexr:chunkCount";
        m_map["maxSamplesPerPixel"]  = "openexr:maxSamplesPerPixel";
        m_map["dwaCompressionLevel"] = "openexr:dwaCompressionLevel";
        // Ones to skip because we handle specially
        m_map["channels"]          = "";
        m_map["compression"]       = "";
        m_map["dataWindow"]        = "";
        m_map["displayWindow"]     = "";
        m_map["envmap"]            = "";
        m_map["tiledesc"]          = "";
        m_map["tiles"]             = "";
        m_map["openexr:lineOrder"] = "";
        m_map["type"]              = "";
        // Ones to skip because we consider them irrelevant

        //        m_map[""] = "";
        // FIXME: Things to consider in the future:
        // preview
        // screenWindowCenter
        // adoptedNeutral
        // renderingTransform, lookModTransform
        // utcOffset
        // longitude latitude altitude
        // focus isoSpeed
    }
};

static StringMap exr_tag_to_oiio_std;


namespace pvt {

void
set_exr_threads()
{
    static int exr_threads = 0;  // lives in exrinput.cpp
    static spin_mutex exr_threads_mutex;

    int oiio_threads = 1;
    OIIO::getattribute("exr_threads", oiio_threads);

    // 0 means all threads in OIIO, but single-threaded in OpenEXR
    // -1 means single-threaded in OIIO
    if (oiio_threads == 0) {
        oiio_threads = Sysutil::hardware_concurrency();
    } else if (oiio_threads == -1) {
        oiio_threads = 0;
    }
    spin_lock lock(exr_threads_mutex);
    if (exr_threads != oiio_threads) {
        exr_threads = oiio_threads;
        Imf::setGlobalThreadCount(exr_threads);
    }
}

}  // namespace pvt



OpenEXRInput::OpenEXRInput() { init(); }



bool
OpenEXRInput::valid_file(const std::string& filename) const
{
    return valid_file(filename, nullptr);
}



bool
OpenEXRInput::valid_file(const std::string& filename,
                         Filesystem::IOProxy* io) const
{
    try {
        std::unique_ptr<Filesystem::IOProxy> local_io;
        if (!io) {
            io = new Filesystem::IOFile(filename, Filesystem::IOProxy::Read);
            local_io.reset(io);
        }
        OpenEXRInputStream IStream(filename.c_str(), io);
        return Imf::isOpenExrFile(IStream);
    } catch (const std::exception& e) {
        return false;
    }
}



bool
OpenEXRInput::open(const std::string& name, ImageSpec& newspec,
                   const ImageSpec& config)
{
    const ParamValue* param = config.find_attribute("oiio:ioproxy",
                                                    TypeDesc::PTR);
    m_io = param ? param->get<Filesystem::IOProxy*>() : nullptr;

    // Quick check to reject non-exr files. Don't perform these tests for
    // the IOProxy case.
    if (!m_io && !Filesystem::is_regular(name)) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }
    if (!valid_file(name, m_io)) {
        errorf("\"%s\" is not an OpenEXR file", name);
        return false;
    }
    pvt::set_exr_threads();

    // "missingcolor" gives fill color for missing scanlines or tiles.
    if (const ParamValue* m = config.find_attribute("oiio:missingcolor")) {
        if (m->type().basetype == TypeDesc::STRING) {
            // missingcolor as string
            m_missingcolor = Strutil::extract_from_list_string<float>(
                m->get_string());
        } else {
            // missingcolor as numeric array
            int n = m->type().basevalues();
            m_missingcolor.clear();
            m_missingcolor.reserve(n);
            for (int i = 0; i < n; ++i)
                m_missingcolor[i] = m->get_float(i);
        }
    } else {
        // If not passed explicit, is there a global setting?
        std::string mc = OIIO::get_string_attribute("missingcolor");
        if (mc.size())
            m_missingcolor = Strutil::extract_from_list_string<float>(mc);
    }

    m_spec = ImageSpec();  // Clear everything with default constructor

    try {
        if (!m_io) {
            m_io = new Filesystem::IOFile(name, Filesystem::IOProxy::Read);
            m_local_io.reset(m_io);
        }
        m_io->seek(0);
        m_input_stream = new OpenEXRInputStream(name.c_str(), m_io);
    } catch (const std::exception& e) {
        m_input_stream = NULL;
        errorf("OpenEXR exception: %s", e.what());
        close();
        return false;
    } catch (...) {  // catch-all for edge cases or compiler bugs
        m_input_stream = NULL;
        errorf("OpenEXR exception: unknown");
        close();
        return false;
    }

    try {
        m_input_multipart = new Imf::MultiPartInputFile(*m_input_stream);
    } catch (const std::exception& e) {
        delete m_input_stream;
        m_input_stream = NULL;
        errorf("OpenEXR exception: %s", e.what());
        close();
        return false;
    } catch (...) {  // catch-all for edge cases or compiler bugs
        m_input_stream = NULL;
        errorf("OpenEXR exception: unknown");
        close();
        return false;
    }

    m_nsubimages = m_input_multipart->parts();
    m_parts.resize(m_nsubimages);
    m_subimage = -1;
    m_miplevel = -1;

    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = m_spec;
    else
        close();
    return ok;
}



// Count number of MIPmap levels
inline int
numlevels(int width, int roundingmode)
{
    int nlevels = 1;
    for (; width > 1; ++nlevels) {
        if (roundingmode == Imf::ROUND_DOWN)
            width = width / 2;
        else
            width = (width + 1) / 2;
    }
    return nlevels;
}



bool
OpenEXRInput::PartInfo::parse_header(OpenEXRInput* in,
                                     const Imf::Header* header)
{
    bool ok = true;
    if (initialized)
        return ok;

    ImageInput::lock_guard lock(in->m_mutex);
    ASSERT(header);
    spec = ImageSpec();

    top_datawindow    = header->dataWindow();
    top_displaywindow = header->displayWindow();
    spec.x            = top_datawindow.min.x;
    spec.y            = top_datawindow.min.y;
    spec.z            = 0;
    spec.width        = top_datawindow.max.x - top_datawindow.min.x + 1;
    spec.height       = top_datawindow.max.y - top_datawindow.min.y + 1;
    spec.depth        = 1;
    topwidth          = spec.width;  // Save top-level mipmap dimensions
    topheight         = spec.height;
    spec.full_x       = top_displaywindow.min.x;
    spec.full_y       = top_displaywindow.min.y;
    spec.full_z       = 0;
    spec.full_width   = top_displaywindow.max.x - top_displaywindow.min.x + 1;
    spec.full_height  = top_displaywindow.max.y - top_displaywindow.min.y + 1;
    spec.full_depth   = 1;
    spec.tile_depth   = 1;

    if (header->hasTileDescription()
        && Strutil::icontains(header->type(), "tile")) {
        const Imf::TileDescription& td(header->tileDescription());
        spec.tile_width  = td.xSize;
        spec.tile_height = td.ySize;
        levelmode        = td.mode;
        roundingmode     = td.roundingMode;
        if (levelmode == Imf::MIPMAP_LEVELS)
            nmiplevels = numlevels(std::max(topwidth, topheight), roundingmode);
        else if (levelmode == Imf::RIPMAP_LEVELS)
            nmiplevels = numlevels(std::max(topwidth, topheight), roundingmode);
        else
            nmiplevels = 1;
    } else {
        spec.tile_width  = 0;
        spec.tile_height = 0;
        levelmode        = Imf::ONE_LEVEL;
        nmiplevels       = 1;
    }
    if (!query_channels(in, header))  // also sets format
        return false;

    spec.deep = Strutil::istarts_with(header->type(), "deep");

    // Unless otherwise specified, exr files are assumed to be linear.
    spec.attribute("oiio:ColorSpace", "Linear");

    if (levelmode != Imf::ONE_LEVEL)
        spec.attribute("openexr:roundingmode", roundingmode);

    const Imf::EnvmapAttribute* envmap;
    envmap = header->findTypedAttribute<Imf::EnvmapAttribute>("envmap");
    if (envmap) {
        cubeface = (envmap->value() == Imf::ENVMAP_CUBE);
        spec.attribute("textureformat", cubeface ? "CubeFace Environment"
                                                 : "LatLong Environment");
        // OpenEXR conventions for env maps
        if (!cubeface)
            spec.attribute("oiio:updirection", "y");
        spec.attribute("oiio:sampleborder", 1);
        // FIXME - detect CubeFace Shadow?
    } else {
        cubeface = false;
        if (spec.tile_width && levelmode == Imf::MIPMAP_LEVELS)
            spec.attribute("textureformat", "Plain Texture");
        // FIXME - detect Shadow
    }

    const Imf::CompressionAttribute* compressattr;
    compressattr = header->findTypedAttribute<Imf::CompressionAttribute>(
        "compression");
    if (compressattr) {
        const char* comp = NULL;
        switch (compressattr->value()) {
        case Imf::NO_COMPRESSION: comp = "none"; break;
        case Imf::RLE_COMPRESSION: comp = "rle"; break;
        case Imf::ZIPS_COMPRESSION: comp = "zips"; break;
        case Imf::ZIP_COMPRESSION: comp = "zip"; break;
        case Imf::PIZ_COMPRESSION: comp = "piz"; break;
        case Imf::PXR24_COMPRESSION: comp = "pxr24"; break;
#ifdef IMF_B44_COMPRESSION
            // The enum Imf::B44_COMPRESSION is not defined in older versions
            // of OpenEXR, and there are no explicit version numbers in the
            // headers.  BUT this other related #define is present only in
            // the newer version.
        case Imf::B44_COMPRESSION: comp = "b44"; break;
        case Imf::B44A_COMPRESSION: comp = "b44a"; break;
#endif
#if defined(OPENEXR_VERSION_MAJOR)                                             \
    && (OPENEXR_VERSION_MAJOR * 10000 + OPENEXR_VERSION_MINOR * 100            \
        + OPENEXR_VERSION_PATCH)                                               \
           >= 20200
        case Imf::DWAA_COMPRESSION: comp = "dwaa"; break;
        case Imf::DWAB_COMPRESSION: comp = "dwab"; break;
#endif
        default: break;
        }
        if (comp)
            spec.attribute("compression", comp);
    }

    for (auto hit = header->begin(); hit != header->end(); ++hit) {
        const Imf::IntAttribute* iattr;
        const Imf::FloatAttribute* fattr;
        const Imf::StringAttribute* sattr;
        const Imf::M33fAttribute* m33fattr;
        const Imf::M44fAttribute* m44fattr;
        const Imf::V3fAttribute* v3fattr;
        const Imf::V3iAttribute* v3iattr;
        const Imf::V2fAttribute* v2fattr;
        const Imf::V2iAttribute* v2iattr;
        const Imf::Box2iAttribute* b2iattr;
        const Imf::Box2fAttribute* b2fattr;
        const Imf::TimeCodeAttribute* tattr;
        const Imf::KeyCodeAttribute* kcattr;
        const Imf::ChromaticitiesAttribute* crattr;
        const Imf::RationalAttribute* rattr;
        const Imf::StringVectorAttribute* svattr;
        const Imf::DoubleAttribute* dattr;
        const Imf::V2dAttribute* v2dattr;
        const Imf::V3dAttribute* v3dattr;
        const Imf::M33dAttribute* m33dattr;
        const Imf::M44dAttribute* m44dattr;
        const char* name  = hit.name();
        std::string oname = exr_tag_to_oiio_std[name];
        if (oname.empty())  // Empty string means skip this attrib
            continue;
        //        if (oname == name)
        //            oname = std::string(format_name()) + "_" + oname;
        const Imf::Attribute& attrib = hit.attribute();
        std::string type             = attrib.typeName();
        if (type == "string"
            && (sattr = header->findTypedAttribute<Imf::StringAttribute>(name)))
            spec.attribute(oname, sattr->value().c_str());
        else if (type == "int"
                 && (iattr = header->findTypedAttribute<Imf::IntAttribute>(
                         name)))
            spec.attribute(oname, iattr->value());
        else if (type == "float"
                 && (fattr = header->findTypedAttribute<Imf::FloatAttribute>(
                         name)))
            spec.attribute(oname, fattr->value());
        else if (type == "m33f"
                 && (m33fattr = header->findTypedAttribute<Imf::M33fAttribute>(
                         name)))
            spec.attribute(oname, TypeMatrix33, &(m33fattr->value()));
        else if (type == "m44f"
                 && (m44fattr = header->findTypedAttribute<Imf::M44fAttribute>(
                         name)))
            spec.attribute(oname, TypeMatrix44, &(m44fattr->value()));
        else if (type == "v3f"
                 && (v3fattr = header->findTypedAttribute<Imf::V3fAttribute>(
                         name)))
            spec.attribute(oname, TypeVector, &(v3fattr->value()));
        else if (type == "v3i"
                 && (v3iattr = header->findTypedAttribute<Imf::V3iAttribute>(
                         name))) {
            TypeDesc v3(TypeDesc::INT, TypeDesc::VEC3, TypeDesc::VECTOR);
            spec.attribute(oname, v3, &(v3iattr->value()));
        } else if (type == "v2f"
                   && (v2fattr = header->findTypedAttribute<Imf::V2fAttribute>(
                           name))) {
            TypeDesc v2(TypeDesc::FLOAT, TypeDesc::VEC2);
            spec.attribute(oname, v2, &(v2fattr->value()));
        } else if (type == "v2i"
                   && (v2iattr = header->findTypedAttribute<Imf::V2iAttribute>(
                           name))) {
            TypeDesc v2(TypeDesc::INT, TypeDesc::VEC2);
            spec.attribute(oname, v2, &(v2iattr->value()));
        } else if (type == "stringvector"
                   && (svattr
                       = header->findTypedAttribute<Imf::StringVectorAttribute>(
                           name))) {
            std::vector<std::string> strvec = svattr->value();
            std::vector<ustring> ustrvec(strvec.size());
            for (size_t i = 0, e = strvec.size(); i < e; ++i)
                ustrvec[i] = strvec[i];
            TypeDesc sv(TypeDesc::STRING, ustrvec.size());
            spec.attribute(oname, sv, &ustrvec[0]);
        } else if (type == "double"
                   && (dattr = header->findTypedAttribute<Imf::DoubleAttribute>(
                           name))) {
            TypeDesc d(TypeDesc::DOUBLE);
            spec.attribute(oname, d, &(dattr->value()));
        } else if (type == "v2d"
                   && (v2dattr = header->findTypedAttribute<Imf::V2dAttribute>(
                           name))) {
            TypeDesc v2(TypeDesc::DOUBLE, TypeDesc::VEC2);
            spec.attribute(oname, v2, &(v2dattr->value()));
        } else if (type == "v3d"
                   && (v3dattr = header->findTypedAttribute<Imf::V3dAttribute>(
                           name))) {
            TypeDesc v3(TypeDesc::DOUBLE, TypeDesc::VEC3, TypeDesc::VECTOR);
            spec.attribute(oname, v3, &(v3dattr->value()));
        } else if (type == "m33d"
                   && (m33dattr = header->findTypedAttribute<Imf::M33dAttribute>(
                           name))) {
            TypeDesc m33(TypeDesc::DOUBLE, TypeDesc::MATRIX33);
            spec.attribute(oname, m33, &(m33dattr->value()));
        } else if (type == "m44d"
                   && (m44dattr = header->findTypedAttribute<Imf::M44dAttribute>(
                           name))) {
            TypeDesc m44(TypeDesc::DOUBLE, TypeDesc::MATRIX44);
            spec.attribute(oname, m44, &(m44dattr->value()));
        } else if (type == "box2i"
                   && (b2iattr = header->findTypedAttribute<Imf::Box2iAttribute>(
                           name))) {
            TypeDesc bx(TypeDesc::INT, TypeDesc::VEC2, 2);
            spec.attribute(oname, bx, &b2iattr->value());
        } else if (type == "box2f"
                   && (b2fattr = header->findTypedAttribute<Imf::Box2fAttribute>(
                           name))) {
            TypeDesc bx(TypeDesc::FLOAT, TypeDesc::VEC2, 2);
            spec.attribute(oname, bx, &b2fattr->value());
        } else if (type == "timecode"
                   && (tattr
                       = header->findTypedAttribute<Imf::TimeCodeAttribute>(
                           name))) {
            unsigned int timecode[2];
            timecode[0] = tattr->value().timeAndFlags(
                Imf::TimeCode::TV60_PACKING);  //TV60 returns unchanged _time
            timecode[1] = tattr->value().userData();

            // Elevate "timeCode" to smpte:TimeCode
            if (oname == "timeCode")
                oname = "smpte:TimeCode";
            spec.attribute(oname, TypeTimeCode, timecode);
        } else if (type == "keycode"
                   && (kcattr
                       = header->findTypedAttribute<Imf::KeyCodeAttribute>(
                           name))) {
            const Imf::KeyCode* k = &kcattr->value();
            unsigned int keycode[7];
            keycode[0] = k->filmMfcCode();
            keycode[1] = k->filmType();
            keycode[2] = k->prefix();
            keycode[3] = k->count();
            keycode[4] = k->perfOffset();
            keycode[5] = k->perfsPerFrame();
            keycode[6] = k->perfsPerCount();

            // Elevate "keyCode" to smpte:KeyCode
            if (oname == "keyCode")
                oname = "smpte:KeyCode";
            spec.attribute(oname, TypeKeyCode, keycode);
        } else if (type == "chromaticities"
                   && (crattr = header->findTypedAttribute<
                                Imf::ChromaticitiesAttribute>(name))) {
            const Imf::Chromaticities* chroma = &crattr->value();
            spec.attribute(oname, TypeDesc(TypeDesc::FLOAT, 8),
                           (const float*)chroma);
        } else if (type == "rational"
                   && (rattr
                       = header->findTypedAttribute<Imf::RationalAttribute>(
                           name))) {
            const Imf::Rational* rational = &rattr->value();
            int n                         = rational->n;
            unsigned int d                = rational->d;
            if (d < (1UL << 31)) {
                int r[2];
                r[0] = n;
                r[1] = static_cast<int>(d);
                spec.attribute(oname, TypeRational, r);
            } else if (int f = static_cast<int>(
                                   gcd<long int>(rational[0], rational[1]))
                               > 1) {
                int r[2];
                r[0] = n / f;
                r[1] = static_cast<int>(d / f);
                spec.attribute(oname, TypeRational, r);
            } else {
                // TODO: find a way to allow the client to accept "close" rational values
                OIIO::debugf(
                    "Don't know what to do with OpenEXR Rational attribute %s with value %d / %u that we cannot represent exactly",
                    oname, n, d);
            }
        } else {
#if 0
            std::cerr << "  unknown attribute " << type << ' ' << name << "\n";
#endif
        }
    }

    float aspect   = spec.get_float_attribute("PixelAspectRatio", 0.0f);
    float xdensity = spec.get_float_attribute("XResolution", 0.0f);
    if (xdensity) {
        // If XResolution is found, supply the YResolution and unit.
        spec.attribute("YResolution", xdensity * (aspect ? aspect : 1.0f));
        spec.attribute("ResolutionUnit", "in");  // EXR is always pixels/inch
    }

    // EXR "name" also gets passed along as "oiio:subimagename".
    if (header->hasName() && header->name() != "")
        spec.attribute("oiio:subimagename", header->name());

    spec.attribute("oiio:subimages", in->m_nsubimages);

    // Squash some problematic texture metadata if we suspect it's wrong
    pvt::check_texture_metadata_sanity(spec);

    initialized = true;
    return ok;
}



namespace {


static TypeDesc
TypeDesc_from_ImfPixelType(Imf::PixelType ptype)
{
    switch (ptype) {
    case Imf::UINT: return TypeDesc::UINT; break;
    case Imf::HALF: return TypeDesc::HALF; break;
    case Imf::FLOAT: return TypeDesc::FLOAT; break;
    default: ASSERT_MSG(0, "Unknown Imf::PixelType %d", int(ptype));
    }
}



// Used to hold channel information for sorting into canonical order
struct ChanNameHolder {
    string_view fullname;
    int exr_channel_number;  // channel index in the exr (sorted by name)
    string_view layer;
    string_view suffix;
    int special_index;
    Imf::PixelType exr_data_type;
    TypeDesc datatype;
    int xSampling;
    int ySampling;

    ChanNameHolder(string_view fullname, int n, const Imf::Channel& exrchan)
        : fullname(fullname)
        , exr_channel_number(n)
        , exr_data_type(exrchan.type)
        , datatype(TypeDesc_from_ImfPixelType(exrchan.type))
        , xSampling(exrchan.xSampling)
        , ySampling(exrchan.ySampling)
    {
        size_t dot = fullname.find_last_of('.');
        if (dot == string_view::npos) {
            suffix = fullname;
        } else {
            layer  = string_view(fullname.data(), dot + 1);
            suffix = string_view(fullname.data() + dot + 1,
                                 fullname.size() - dot - 1);
        }
        static const char* special[]
            = { "R",    "Red",  "G",  "Green", "B",     "Blue",  "Y",
                "real", "imag", "A",  "Alpha", "AR",    "RA",    "AG",
                "GA",   "AB",   "BA", "Z",     "Depth", "Zback", nullptr };
        special_index = 10000;
        for (int i = 0; special[i]; ++i)
            if (Strutil::iequals(suffix, special[i])) {
                special_index = i;
                break;
            }
    }

    static bool compare_cnh(const ChanNameHolder& a, const ChanNameHolder& b)
    {
        if (a.layer < b.layer)
            return true;
        if (a.layer > b.layer)
            return false;
        // Within the same layer
        if (a.special_index < b.special_index)
            return true;
        if (a.special_index > b.special_index)
            return false;
        return a.suffix < b.suffix;
    }
};

}  // namespace



bool
OpenEXRInput::PartInfo::query_channels(OpenEXRInput* in,
                                       const Imf::Header* header)
{
    ASSERT(!initialized);
    bool ok        = true;
    spec.nchannels = 0;
    const Imf::ChannelList& channels(header->channels());
    std::vector<std::string> channelnames;  // Order of channels in file
    std::vector<ChanNameHolder> cnh;
    int c = 0;
    for (auto ci = channels.begin(); ci != channels.end(); ++c, ++ci)
        cnh.emplace_back(ci.name(), c, ci.channel());
    spec.nchannels = int(cnh.size());
    std::sort(cnh.begin(), cnh.end(), ChanNameHolder::compare_cnh);
    // Now we should have cnh sorted into the order that we want to present
    // to the OIIO client.
    spec.format         = TypeDesc::UNKNOWN;
    bool all_one_format = true;
    for (int c = 0; c < spec.nchannels; ++c) {
        spec.channelnames.push_back(cnh[c].fullname);
        spec.channelformats.push_back(cnh[c].datatype);
        spec.format = TypeDesc(ImageBufAlgo::type_merge(
            TypeDesc::BASETYPE(spec.format.basetype),
            TypeDesc::BASETYPE(cnh[c].datatype.basetype)));
        pixeltype.push_back(cnh[c].exr_data_type);
        chanbytes.push_back(cnh[c].datatype.size());
        all_one_format &= (cnh[c].datatype == cnh[0].datatype);
        if (spec.alpha_channel < 0
            && (Strutil::iequals(cnh[c].suffix, "A")
                || Strutil::iequals(cnh[c].suffix, "Alpha")))
            spec.alpha_channel = c;
        if (spec.z_channel < 0
            && (Strutil::iequals(cnh[c].suffix, "Z")
                || Strutil::iequals(cnh[c].suffix, "Depth")))
            spec.z_channel = c;
        if (cnh[c].xSampling != 1 || cnh[c].ySampling != 1) {
            ok = false;
            in->errorf(
                "Subsampled channels are not supported (channel \"%s\" has sampling %d,%d).",
                cnh[c].fullname, cnh[c].xSampling, cnh[c].ySampling);
            // FIXME: Some day, we should handle channel subsampling.
        }
    }
    ASSERT((int)spec.channelnames.size() == spec.nchannels);
    ASSERT(spec.format != TypeDesc::UNKNOWN);
    if (all_one_format)
        spec.channelformats.clear();
    return ok;
}



void
OpenEXRInput::PartInfo::compute_mipres(int miplevel, ImageSpec& spec) const
{
    // Compute the resolution of the requested mip level, and also adjust
    // the "full" size appropriately (based on the exr display window).

    if (levelmode == Imf::ONE_LEVEL)
        return;  // spec is already correct

    int w = topwidth;
    int h = topheight;
    if (levelmode == Imf::MIPMAP_LEVELS) {
        for (int m = miplevel; m; --m) {
            if (roundingmode == Imf::ROUND_DOWN) {
                w = w / 2;
                h = h / 2;
            } else {
                w = (w + 1) / 2;
                h = (h + 1) / 2;
            }
            w = std::max(1, w);
            h = std::max(1, h);
        }
    } else if (levelmode == Imf::RIPMAP_LEVELS) {
        // FIXME
    } else {
        ASSERT_MSG(0, "Unknown levelmode %d", int(levelmode));
    }

    spec.width  = w;
    spec.height = h;
    // N.B. OpenEXR doesn't support data and display windows per MIPmap
    // level.  So always take from the top level.
    Imath::Box2i datawindow    = top_datawindow;
    Imath::Box2i displaywindow = top_displaywindow;
    spec.x                     = datawindow.min.x;
    spec.y                     = datawindow.min.y;
    if (miplevel == 0) {
        spec.full_x      = displaywindow.min.x;
        spec.full_y      = displaywindow.min.y;
        spec.full_width  = displaywindow.max.x - displaywindow.min.x + 1;
        spec.full_height = displaywindow.max.y - displaywindow.min.y + 1;
    } else {
        spec.full_x      = spec.x;
        spec.full_y      = spec.y;
        spec.full_width  = spec.width;
        spec.full_height = spec.height;
    }
    if (cubeface) {
        spec.full_width  = w;
        spec.full_height = w;
    }
}



bool
OpenEXRInput::seek_subimage(int subimage, int miplevel)
{
    if (subimage < 0 || subimage >= m_nsubimages)  // out of range
        return false;

    if (subimage == m_subimage && miplevel == m_miplevel) {  // no change
        return true;
    }

    PartInfo& part(m_parts[subimage]);
    if (!part.initialized) {
        const Imf::Header* header = NULL;
        if (m_input_multipart)
            header = &(m_input_multipart->header(subimage));
        if (!part.parse_header(this, header))
            return false;
        part.initialized = true;
    }

    if (subimage != m_subimage) {
        delete m_scanline_input_part;
        m_scanline_input_part = NULL;
        delete m_tiled_input_part;
        m_tiled_input_part = NULL;
        delete m_deep_scanline_input_part;
        m_deep_scanline_input_part = NULL;
        delete m_deep_tiled_input_part;
        m_deep_tiled_input_part = NULL;
        try {
            if (part.spec.deep) {
                if (part.spec.tile_width)
                    m_deep_tiled_input_part
                        = new Imf::DeepTiledInputPart(*m_input_multipart,
                                                      subimage);
                else
                    m_deep_scanline_input_part
                        = new Imf::DeepScanLineInputPart(*m_input_multipart,
                                                         subimage);
            } else {
                if (part.spec.tile_width)
                    m_tiled_input_part
                        = new Imf::TiledInputPart(*m_input_multipart, subimage);
                else
                    m_scanline_input_part
                        = new Imf::InputPart(*m_input_multipart, subimage);
            }
        } catch (const std::exception& e) {
            errorf("OpenEXR exception: %s", e.what());
            m_scanline_input_part      = NULL;
            m_tiled_input_part         = NULL;
            m_deep_scanline_input_part = NULL;
            m_deep_tiled_input_part    = NULL;
            return false;
        } catch (...) {  // catch-all for edge cases or compiler bugs
            errorf("OpenEXR exception: unknown");
            m_scanline_input_part      = NULL;
            m_tiled_input_part         = NULL;
            m_deep_scanline_input_part = NULL;
            m_deep_tiled_input_part    = NULL;
            return false;
        }
    }

    m_subimage = subimage;

    if (miplevel < 0 || miplevel >= part.nmiplevels)  // out of range
        return false;

    m_miplevel = miplevel;
    m_spec     = part.spec;

    if (miplevel == 0 && part.levelmode == Imf::ONE_LEVEL) {
        return true;
    }

    // Compute the resolution of the requested mip level and adjust the
    // full size fields.
    part.compute_mipres(miplevel, m_spec);

    return true;
}



ImageSpec
OpenEXRInput::spec(int subimage, int miplevel)
{
    ImageSpec ret;
    if (subimage < 0 || subimage >= m_nsubimages)
        return ret;  // invalid
    const PartInfo& part(m_parts[subimage]);
    if (!part.initialized) {
        // Only if this subimage hasn't yet been inventoried do we need
        // to lock and seek.
        lock_guard lock(m_mutex);
        if (!part.initialized) {
            if (!seek_subimage(subimage, miplevel))
                return ret;
        }
    }
    if (miplevel < 0 || miplevel >= part.nmiplevels)
        return ret;  // invalid
    ret = part.spec;
    part.compute_mipres(miplevel, ret);
    return ret;
}



ImageSpec
OpenEXRInput::spec_dimensions(int subimage, int miplevel)
{
    ImageSpec ret;
    if (subimage < 0 || subimage >= m_nsubimages)
        return ret;  // invalid
    const PartInfo& part(m_parts[subimage]);
    if (!part.initialized) {
        // Only if this subimage hasn't yet been inventoried do we need
        // to lock and seek.
        lock_guard lock(m_mutex);
        if (!seek_subimage(subimage, miplevel))
            return ret;
    }
    if (miplevel < 0 || miplevel >= part.nmiplevels)
        return ret;  // invalid
    ret.copy_dimensions(part.spec);
    part.compute_mipres(miplevel, ret);
    return ret;
}



bool
OpenEXRInput::close()
{
    delete m_input_multipart;
    delete m_scanline_input_part;
    delete m_tiled_input_part;
    delete m_deep_scanline_input_part;
    delete m_deep_tiled_input_part;
    delete m_input_scanline;
    delete m_input_tiled;
    delete m_input_stream;
    init();  // Reset to initial state
    return true;
}



bool
OpenEXRInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                   void* data)
{
    return read_native_scanlines(subimage, miplevel, y, y + 1, z, 0,
                                 m_spec.nchannels, data);
}



bool
OpenEXRInput::read_native_scanlines(int subimage, int miplevel, int ybegin,
                                    int yend, int z, void* data)
{
    return read_native_scanlines(subimage, miplevel, ybegin, yend, z, 0,
                                 m_spec.nchannels, data);
}



bool
OpenEXRInput::read_native_scanlines(int subimage, int miplevel, int ybegin,
                                    int yend, int z, int chbegin, int chend,
                                    void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    chend = clamp(chend, chbegin + 1, m_spec.nchannels);
    //    std::cerr << "openexr rns " << ybegin << ' ' << yend << ", channels "
    //              << chbegin << "-" << (chend-1) << "\n";
    if (m_input_scanline == NULL && m_scanline_input_part == NULL) {
        errorf(
            "called OpenEXRInput::read_native_scanlines without an open file");
        return false;
    }

    // Compute where OpenEXR needs to think the full buffers starts.
    // OpenImageIO requires that 'data' points to where the client wants
    // to put the pixels being read, but OpenEXR's frameBuffer.insert()
    // wants where the address of the "virtual framebuffer" for the
    // whole image.
    const PartInfo& part(m_parts[m_subimage]);
    size_t pixelbytes    = m_spec.pixel_bytes(chbegin, chend, true);
    size_t scanlinebytes = (size_t)m_spec.width * pixelbytes;
    char* buf = (char*)data - m_spec.x * pixelbytes - ybegin * scanlinebytes;

    try {
        Imf::FrameBuffer frameBuffer;
        size_t chanoffset = 0;
        for (int c = chbegin; c < chend; ++c) {
            size_t chanbytes = m_spec.channelformat(c).size();
            frameBuffer.insert(m_spec.channelnames[c].c_str(),
                               Imf::Slice(part.pixeltype[c], buf + chanoffset,
                                          pixelbytes, scanlinebytes));
            chanoffset += chanbytes;
        }
        if (m_input_scanline) {
            m_input_scanline->setFrameBuffer(frameBuffer);
            m_input_scanline->readPixels(ybegin, yend - 1);
        } else if (m_scanline_input_part) {
            m_scanline_input_part->setFrameBuffer(frameBuffer);
            m_scanline_input_part->readPixels(ybegin, yend - 1);
        } else {
            errorf("Attempted to read scanline from a non-scanline file.");
            return false;
        }
    } catch (const std::exception& e) {
        errorf("Failed OpenEXR read: %s", e.what());
        return false;
    } catch (...) {  // catch-all for edge cases or compiler bugs
        errorf("Failed OpenEXR read: unknown exception");
        return false;
    }
    return true;
}



bool
OpenEXRInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    return read_native_tiles(subimage, miplevel, x, x + m_spec.tile_width, y,
                             y + m_spec.tile_height, z, z + m_spec.tile_depth,
                             0, m_spec.nchannels, data);
}



bool
OpenEXRInput::read_native_tiles(int subimage, int miplevel, int xbegin,
                                int xend, int ybegin, int yend, int zbegin,
                                int zend, void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    return read_native_tiles(subimage, miplevel, xbegin, xend, ybegin, yend,
                             zbegin, zend, 0, m_spec.nchannels, data);
}



bool
OpenEXRInput::read_native_tiles(int subimage, int miplevel, int xbegin,
                                int xend, int ybegin, int yend, int zbegin,
                                int zend, int chbegin, int chend, void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    chend = clamp(chend, chbegin + 1, m_spec.nchannels);
#if 0
    std::cerr << "openexr rnt " << xbegin << ' ' << xend << ' ' << ybegin 
              << ' ' << yend << ", chans " << chbegin
              << "-" << (chend-1) << "\n";
#endif
    if (!(m_input_tiled || m_tiled_input_part)
        || !m_spec.valid_tile_range(xbegin, xend, ybegin, yend, zbegin, zend)) {
        errorf("called OpenEXRInput::read_native_tiles without an open file");
        return false;
    }

    // Compute where OpenEXR needs to think the full buffers starts.
    // OpenImageIO requires that 'data' points to where the client wants
    // to put the pixels being read, but OpenEXR's frameBuffer.insert()
    // wants where the address of the "virtual framebuffer" for the
    // whole image.
    const PartInfo& part(m_parts[m_subimage]);
    size_t pixelbytes = m_spec.pixel_bytes(chbegin, chend, true);
    int firstxtile    = (xbegin - m_spec.x) / m_spec.tile_width;
    int firstytile    = (ybegin - m_spec.y) / m_spec.tile_height;
    // clamp to the image edge
    xend = std::min(xend, m_spec.x + m_spec.width);
    yend = std::min(yend, m_spec.y + m_spec.height);
    zend = std::min(zend, m_spec.z + m_spec.depth);
    // figure out how many tiles we need
    int nxtiles = (xend - xbegin + m_spec.tile_width - 1) / m_spec.tile_width;
    int nytiles = (yend - ybegin + m_spec.tile_height - 1) / m_spec.tile_height;
    int whole_width  = nxtiles * m_spec.tile_width;
    int whole_height = nytiles * m_spec.tile_height;

    std::unique_ptr<char[]> tmpbuf;
    void* origdata = data;
    if (whole_width != (xend - xbegin) || whole_height != (yend - ybegin)) {
        // Deal with the case of reading not a whole number of tiles --
        // OpenEXR will happily overwrite user memory in this case.
        tmpbuf.reset(new char[nxtiles * nytiles * m_spec.tile_bytes(true)]);
        data = &tmpbuf[0];
    }
    char* buf = (char*)data - xbegin * pixelbytes
                - ybegin * pixelbytes * m_spec.tile_width * nxtiles;

    try {
        Imf::FrameBuffer frameBuffer;
        size_t chanoffset = 0;
        for (int c = chbegin; c < chend; ++c) {
            size_t chanbytes = m_spec.channelformat(c).size();
            frameBuffer.insert(m_spec.channelnames[c].c_str(),
                               Imf::Slice(part.pixeltype[c], buf + chanoffset,
                                          pixelbytes,
                                          pixelbytes * m_spec.tile_width
                                              * nxtiles));
            chanoffset += chanbytes;
        }
        if (m_input_tiled) {
            m_input_tiled->setFrameBuffer(frameBuffer);
            m_input_tiled->readTiles(firstxtile, firstxtile + nxtiles - 1,
                                     firstytile, firstytile + nytiles - 1,
                                     m_miplevel, m_miplevel);
        } else if (m_tiled_input_part) {
            m_tiled_input_part->setFrameBuffer(frameBuffer);
            m_tiled_input_part->readTiles(firstxtile, firstxtile + nxtiles - 1,
                                          firstytile, firstytile + nytiles - 1,
                                          m_miplevel, m_miplevel);
        } else {
            errorf("Attempted to read tiles from a non-tiled file");
            return false;
        }
        if (data != origdata) {
            stride_t user_scanline_bytes = (xend - xbegin) * pixelbytes;
            stride_t scanline_stride = nxtiles * m_spec.tile_width * pixelbytes;
            for (int y = ybegin; y < yend; ++y)
                memcpy((char*)origdata + (y - ybegin) * scanline_stride,
                       (char*)data + (y - ybegin) * scanline_stride,
                       user_scanline_bytes);
        }
    } catch (const std::exception& e) {
        std::string err = e.what();
        if (m_missingcolor.size()) {
            // User said not to fail for bad or missing tiles. If we failed
            // reading a single tile, use the fill pattern. If we failed
            // reading many tiles, we don't know which ones, so go back and
            // read them individually for a second chance.
            stride_t xstride = pixelbytes;
            stride_t ystride = xstride * (xend - xbegin);
            if (nxtiles * nytiles == 1) {
                // Read of one tile -- use the fill pattern
                fill_missing(xbegin, xend, ybegin, yend, zbegin, zend, chbegin,
                             chend, data, xstride, ystride);
            } else {
                // Read of many tiles -- don't know which failed, so try
                // again to read them all individually.
                return read_native_tiles_individually(subimage, miplevel,
                                                      xbegin, xend, ybegin,
                                                      yend, zbegin, zend,
                                                      chbegin, chend, data,
                                                      xstride, ystride);
            }
        } else {
            errorf("Failed OpenEXR read: %s", err);
            return false;
        }
    } catch (...) {  // catch-all for edge cases or compiler bugs
        errorf("Failed OpenEXR read: unknown exception");
        return false;
    }

    return true;
}



bool
OpenEXRInput::read_native_tiles_individually(int subimage, int miplevel,
                                             int xbegin, int xend, int ybegin,
                                             int yend, int zbegin, int zend,
                                             int chbegin, int chend, void* data,
                                             stride_t xstride, stride_t ystride)
{
    // Note: this is only called by read_tiles, which still holds the
    // mutex, so it's safe to directly access m_spec.
    bool ok = true;
    for (int y = ybegin; y < yend; y += m_spec.tile_height) {
        // int ye = std::min(y + m_spec.tile_height, m_spec.y + m_spec.height);
        int ye = y + m_spec.tile_height;
        for (int x = xbegin; x < xend; x += m_spec.tile_width) {
            // int xe = std::min(x + m_spec.tile_width, m_spec.x + m_spec.width);
            int xe  = x + m_spec.tile_width;
            char* d = (char*)data + (y - ybegin) * ystride
                      + (x - xbegin) * xstride;
            ok &= read_tiles(subimage, miplevel, x, xe, y, ye, zbegin, zend,
                             chbegin, chend, TypeUnknown, d, xstride, ystride);
        }
    }
    return ok;
}



void
OpenEXRInput::fill_missing(int xbegin, int xend, int ybegin, int yend,
                           int zbegin, int zend, int chbegin, int chend,
                           void* data, stride_t xstride, stride_t ystride)
{
    std::vector<float> missingcolor = m_missingcolor;
    missingcolor.resize(chend, m_missingcolor.back());
    bool stripe = missingcolor[0] < 0.0f;
    if (stripe)
        missingcolor[0] = fabsf(missingcolor[0]);
    for (int y = ybegin; y < yend; ++y) {
        for (int x = xbegin; x < xend; ++x) {
            char* d = (char*)data + (y - ybegin) * ystride
                      + (x - xbegin) * xstride;
            for (int ch = chbegin; ch < chend; ++ch) {
                float v = missingcolor[ch];
                if (stripe && ((x - y) & 8))
                    v = 0.0f;
                TypeDesc cf = m_spec.channelformat(ch);
                if (cf == TypeFloat)
                    *(float*)d = v;
                else if (cf == TypeHalf)
                    *(half*)d = v;
                d += cf.size();
            }
        }
    }
}



bool
OpenEXRInput::read_native_deep_scanlines(int subimage, int miplevel, int ybegin,
                                         int yend, int z, int chbegin,
                                         int chend, DeepData& deepdata)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (m_deep_scanline_input_part == NULL) {
        errorf(
            "called OpenEXRInput::read_native_deep_scanlines without an open file");
        return false;
    }

    try {
        const PartInfo& part(m_parts[m_subimage]);
        size_t npixels = (yend - ybegin) * m_spec.width;
        chend          = clamp(chend, chbegin + 1, m_spec.nchannels);
        int nchans     = chend - chbegin;

        // Set up the count and pointers arrays and the Imf framebuffer
        std::vector<TypeDesc> channeltypes;
        m_spec.get_channelformats(channeltypes);
        deepdata.init(npixels, nchans,
                      cspan<TypeDesc>(&channeltypes[chbegin], nchans),
                      m_spec.channelnames);
        std::vector<unsigned int> all_samples(npixels);
        std::vector<void*> pointerbuf(npixels * nchans);
        Imf::DeepFrameBuffer frameBuffer;
        Imf::Slice countslice(Imf::UINT,
                              (char*)(&all_samples[0] - m_spec.x
                                      - ybegin * m_spec.width),
                              sizeof(unsigned int),
                              sizeof(unsigned int) * m_spec.width);
        frameBuffer.insertSampleCountSlice(countslice);

        for (int c = chbegin; c < chend; ++c) {
            Imf::DeepSlice slice(
                part.pixeltype[c],
                (char*)(&pointerbuf[0] + (c - chbegin) - m_spec.x * nchans
                        - ybegin * m_spec.width * nchans),
                sizeof(void*) * nchans,  // xstride of pointer array
                sizeof(void*) * nchans
                    * m_spec.width,      // ystride of pointer array
                deepdata.samplesize());  // stride of data sample
            frameBuffer.insert(m_spec.channelnames[c].c_str(), slice);
        }
        m_deep_scanline_input_part->setFrameBuffer(frameBuffer);

        // Get the sample counts for each pixel and compute the total
        // number of samples and resize the data area appropriately.
        m_deep_scanline_input_part->readPixelSampleCounts(ybegin, yend - 1);
        deepdata.set_all_samples(all_samples);
        deepdata.get_pointers(pointerbuf);

        // Read the pixels
        m_deep_scanline_input_part->readPixels(ybegin, yend - 1);
        // deepdata.import_chansamp (pointerbuf);
    } catch (const std::exception& e) {
        errorf("Failed OpenEXR read: %s", e.what());
        return false;
    } catch (...) {  // catch-all for edge cases or compiler bugs
        errorf("Failed OpenEXR read: unknown exception");
        return false;
    }

    return true;
}



bool
OpenEXRInput::read_native_deep_tiles(int subimage, int miplevel, int xbegin,
                                     int xend, int ybegin, int yend, int zbegin,
                                     int zend, int chbegin, int chend,
                                     DeepData& deepdata)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (m_deep_tiled_input_part == NULL) {
        errorf(
            "called OpenEXRInput::read_native_deep_tiles without an open file");
        return false;
    }

    try {
        const PartInfo& part(m_parts[m_subimage]);
        size_t width   = xend - xbegin;
        size_t height  = yend - ybegin;
        size_t npixels = width * height;
        chend          = clamp(chend, chbegin + 1, m_spec.nchannels);
        int nchans     = chend - chbegin;

        // Set up the count and pointers arrays and the Imf framebuffer
        std::vector<TypeDesc> channeltypes;
        m_spec.get_channelformats(channeltypes);
        deepdata.init(npixels, nchans,
                      cspan<TypeDesc>(&channeltypes[chbegin], nchans),
                      m_spec.channelnames);
        std::vector<unsigned int> all_samples(npixels);
        std::vector<void*> pointerbuf(npixels * nchans);
        Imf::DeepFrameBuffer frameBuffer;
        Imf::Slice countslice(
            Imf::UINT, (char*)(&all_samples[0] - xbegin - ybegin * width),
            sizeof(unsigned int), sizeof(unsigned int) * width);
        frameBuffer.insertSampleCountSlice(countslice);
        for (int c = chbegin; c < chend; ++c) {
            Imf::DeepSlice slice(
                part.pixeltype[c],
                (char*)(&pointerbuf[0] + (c - chbegin) - xbegin * nchans
                        - ybegin * width * nchans),
                sizeof(void*) * nchans,          // xstride of pointer array
                sizeof(void*) * nchans * width,  // ystride of pointer array
                deepdata.samplesize());          // stride of data sample
            frameBuffer.insert(m_spec.channelnames[c].c_str(), slice);
        }
        m_deep_tiled_input_part->setFrameBuffer(frameBuffer);

        int xtiles = round_to_multiple(width, m_spec.tile_width)
                     / m_spec.tile_width;
        int ytiles = round_to_multiple(height, m_spec.tile_height)
                     / m_spec.tile_height;

        int firstxtile = (xbegin - m_spec.x) / m_spec.tile_width;
        int firstytile = (ybegin - m_spec.y) / m_spec.tile_height;

        // Get the sample counts for each pixel and compute the total
        // number of samples and resize the data area appropriately.
        m_deep_tiled_input_part->readPixelSampleCounts(firstxtile,
                                                       firstxtile + xtiles - 1,
                                                       firstytile,
                                                       firstytile + ytiles - 1);
        deepdata.set_all_samples(all_samples);
        deepdata.get_pointers(pointerbuf);

        // Read the pixels
        m_deep_tiled_input_part->readTiles(firstxtile, firstxtile + xtiles - 1,
                                           firstytile, firstytile + ytiles - 1,
                                           m_miplevel, m_miplevel);
    } catch (const std::exception& e) {
        errorf("Failed OpenEXR read: %s", e.what());
        return false;
    } catch (...) {  // catch-all for edge cases or compiler bugs
        errorf("Failed OpenEXR read: unknown exception");
        return false;
    }

    return true;
}


OIIO_PLUGIN_NAMESPACE_END
