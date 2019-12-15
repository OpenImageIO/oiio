// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <Ptexture.h>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/typedesc.h>

OIIO_PLUGIN_NAMESPACE_BEGIN


class PtexInput final : public ImageInput {
public:
    PtexInput()
        : m_ptex(NULL)
    {
        init();
    }
    virtual ~PtexInput() { close(); }
    virtual const char* format_name(void) const override { return "ptex"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "arbitrary_metadata"
                || feature == "exif"    // Because of arbitrary_metadata
                || feature == "iptc");  // Because of arbitrary_metadata
    }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override
    {
        lock_guard lock(m_mutex);
        return m_subimage;
    }
    virtual int current_miplevel(void) const override
    {
        lock_guard lock(m_mutex);
        return m_miplevel;
    }
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool read_native_tile(int subimage, int miplevel, int x, int y,
                                  int z, void* data) override;

private:
    PtexTexture* m_ptex;
    int m_subimage;
    int m_miplevel;
    int m_numFaces;
    Ptex::Res m_faceres;
    Ptex::Res m_mipfaceres;
    Ptex::Res m_tileres;
    bool m_isTiled;
    bool m_hasMipMaps;
    int m_ntilesu;

    /// Reset everything to initial state
    ///
    void init()
    {
        if (m_ptex)
            m_ptex->release();
        m_ptex     = NULL;
        m_subimage = -1;
        m_miplevel = -1;
    }
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
ptex_input_imageio_create()
{
    return new PtexInput;
}

OIIO_EXPORT int ptex_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
ptex_imageio_library_version()
{
    return ustring::sprintf("Ptex %d.%d", PtexLibraryMajorVersion,
                            PtexLibraryMinorVersion)
        .c_str();
}

OIIO_EXPORT const char* ptex_input_extensions[] = { "ptex", "ptx", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
PtexInput::open(const std::string& name, ImageSpec& newspec)
{
    Ptex::String perr;
    m_ptex = PtexTexture::open(name.c_str(), perr, true /*premultiply*/);
    if (!perr.empty()) {
        errorf("%s", perr);
        close();
        return false;
    }

    m_numFaces   = m_ptex->numFaces();
    m_hasMipMaps = m_ptex->hasMipMaps();

    bool ok = seek_subimage(0, 0);
    if (ok)
        newspec = spec();
    else
        close();
    return ok;
}



bool
PtexInput::seek_subimage(int subimage, int miplevel)
{
    if (m_subimage == subimage && m_miplevel == miplevel)
        return true;  // Already fine

    if (subimage < 0 || subimage >= m_numFaces)
        return false;
    m_subimage                  = subimage;
    const Ptex::FaceInfo& pface = m_ptex->getFaceInfo(subimage);
    m_faceres                   = pface.res;

    int nmiplevels = std::max(m_faceres.ulog2, m_faceres.vlog2) + 1;
    if (miplevel < 0 || miplevel > nmiplevels - 1)
        return false;
    m_miplevel   = miplevel;
    m_mipfaceres = Ptex::Res(std::max(0, m_faceres.ulog2 - miplevel),
                             std::max(0, m_faceres.vlog2 - miplevel));

    TypeDesc format = TypeDesc::UNKNOWN;
    switch (m_ptex->dataType()) {
    case Ptex::dt_uint8: format = TypeDesc::UINT8; break;
    case Ptex::dt_uint16: format = TypeDesc::UINT16; break;
    case Ptex::dt_half: format = TypeDesc::HALF; break;
    case Ptex::dt_float: format = TypeDesc::FLOAT; break;
    default: errorf("Ptex with unknown data format"); return false;
    }

    m_spec = ImageSpec(std::max(1, m_faceres.u() >> miplevel),
                       std::max(1, m_faceres.v() >> miplevel),
                       m_ptex->numChannels(), format);

    m_spec.alpha_channel = m_ptex->alphaChannel();

    if (m_ptex->meshType() == Ptex::mt_triangle)
        m_spec.attribute("ptex:meshType", "triangle");
    else
        m_spec.attribute("ptex:meshType", "quad");

    if (m_ptex->hasEdits())
        m_spec.attribute("ptex:hasEdits", (int)1);

    PtexFaceData* facedata = m_ptex->getData(m_subimage, m_faceres);
    m_isTiled              = facedata->isTiled();
    if (m_isTiled) {
        m_tileres          = facedata->tileRes();
        m_spec.tile_width  = m_tileres.u();
        m_spec.tile_height = m_tileres.v();
        m_ntilesu          = m_faceres.ntilesu(m_tileres);
    } else {
        // Always make it look tiled
        m_spec.tile_width  = m_spec.width;
        m_spec.tile_height = m_spec.height;
    }

    std::string wrapmode;
    if (m_ptex->uBorderMode() == Ptex::m_clamp)
        wrapmode = "clamp";
    else if (m_ptex->uBorderMode() == Ptex::m_black)
        wrapmode = "black";
    else  // if (m_ptex->uBorderMode() == Ptex::m_periodic)
        wrapmode = "periodic";
    wrapmode += ",";
    if (m_ptex->uBorderMode() == Ptex::m_clamp)
        wrapmode += "clamp";
    else if (m_ptex->uBorderMode() == Ptex::m_black)
        wrapmode += "black";
    else  // if (m_ptex->uBorderMode() == Ptex::m_periodic)
        wrapmode += "periodic";
    m_spec.attribute("wrapmode", wrapmode);

#define GETMETA(pmeta, key, ptype, basetype, typedesc, value)                  \
    {                                                                          \
        const ptype* v;                                                        \
        int count;                                                             \
        pmeta->getValue(key, v, count);                                        \
        typedesc = TypeDesc(basetype, count);                                  \
        value    = (const void*)v;                                             \
    }

    PtexMetaData* pmeta = m_ptex->getMetaData();
    if (pmeta) {
        int n = pmeta->numKeys();
        for (int i = 0; i < n; ++i) {
            const char* key = NULL;
            Ptex::MetaDataType ptype;
            pmeta->getKey(i, key, ptype);
            ASSERT(key);
            const char* vchar;
            const void* value;
            TypeDesc typedesc;
            switch (ptype) {
            case Ptex::mdt_string:
                pmeta->getValue(key, vchar);
                value    = &vchar;
                typedesc = TypeDesc::STRING;
                break;
            case Ptex::mdt_int8:
                GETMETA(pmeta, key, int8_t, TypeDesc::INT8, typedesc, value);
                break;
            case Ptex::mdt_int16:
                GETMETA(pmeta, key, int16_t, TypeDesc::INT16, typedesc, value);
                break;
            case Ptex::mdt_int32:
                GETMETA(pmeta, key, int32_t, TypeDesc::INT32, typedesc, value);
                break;
            case Ptex::mdt_float:
                GETMETA(pmeta, key, float, TypeDesc::FLOAT, typedesc, value);
                break;
            case Ptex::mdt_double:
                GETMETA(pmeta, key, double, TypeDesc::DOUBLE, typedesc, value);
                break;
            default: continue;
            }
            m_spec.attribute(key, typedesc, value);
        }
        pmeta->release();
    }

    facedata->release();
    return true;
}



bool
PtexInput::close()
{
    init();  // Reset to initial state, including closing any open files
    return true;
}



bool
PtexInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                void* data)
{
    return false;  // Not scanline oriented
}



bool
PtexInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                            void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    PtexFaceData* facedata = m_ptex->getData(m_subimage, m_mipfaceres);

    PtexFaceData* f = facedata;
    if (m_isTiled) {
        int tileno = y / m_spec.tile_height * m_ntilesu + x / m_spec.tile_width;
        f          = facedata->getTile(tileno);
    }

    bool ok        = true;
    void* tiledata = f->getData();
    if (tiledata) {
        memcpy(data, tiledata, m_spec.tile_bytes());
    } else {
        ok = false;
    }

    if (m_isTiled)
        f->release();
    facedata->release();
    return ok;
}



OIIO_PLUGIN_NAMESPACE_END
