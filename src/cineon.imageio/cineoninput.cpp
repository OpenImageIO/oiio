// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include "libcineon/Cineon.h"

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/strutil.h>
#include <OpenImageIO/typedesc.h>

using namespace cineon;

OIIO_PLUGIN_NAMESPACE_BEGIN


class CineonInput final : public ImageInput {
public:
    CineonInput() { init(); }
    virtual ~CineonInput() { close(); }
    virtual const char* format_name(void) const override { return "cineon"; }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    InStream* m_stream = nullptr;
    cineon::Reader m_cin;
    std::vector<unsigned char> m_userBuf;

    /// Reset everything to initial state
    ///
    void init()
    {
        if (m_stream) {
            m_stream->Close();
            delete m_stream;
            m_stream = nullptr;
        }
        m_userBuf.clear();
    }

    /// Helper function - retrieve string for libcineon descriptor
    ///
    char* get_descriptor_string(cineon::Descriptor c);
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
cineon_input_imageio_create()
{
    return new CineonInput;
}

OIIO_EXPORT int cineon_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
cineon_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT const char* cineon_input_extensions[] = { "cin", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
CineonInput::open(const std::string& name, ImageSpec& newspec)
{
    // open the image
    m_stream = new InStream();
    if (!m_stream->Open(name.c_str())) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    m_cin.SetInStream(m_stream);
    if (!m_cin.ReadHeader()) {
        errorf("Could not read header");
        close();
        return false;
    }

    // create imagespec
    TypeDesc typedesc;
    int maxbits = 0;
    for (int i = 0; i < m_cin.header.NumberOfElements(); i++) {
        if (maxbits < m_cin.header.BitDepth(i))
            maxbits = m_cin.header.BitDepth(i);
    }
    switch ((maxbits + 7) / 8) {
    case 1: typedesc = TypeDesc::UINT8; break;
    case 2: typedesc = TypeDesc::UINT16; break;
    case 3:
    case 4: typedesc = TypeDesc::UINT32; break;
    default:
        errorf("Unsupported bit depth %d", maxbits);
        close();
        return false;
    }
    m_spec = ImageSpec(m_cin.header.Width(), m_cin.header.Height(),
                       m_cin.header.NumberOfElements(), typedesc);
    // fill channel names
    m_spec.channelnames.clear();
    int gscount = 0, rcount = 0, gcount = 0, bcount = 0;
    char buf[3];
    for (int i = 0; i < m_cin.header.NumberOfElements(); i++) {
        switch (m_cin.header.ImageDescriptor(i)) {
        case cineon::kGrayscale:
            if (++gscount > 1) {
                std::string ch = Strutil::sprintf("I%d", gscount);
                m_spec.channelnames.push_back(ch);
            } else
                m_spec.channelnames.emplace_back("I");
            break;
        case cineon::kPrintingDensityRed:
        case cineon::kRec709Red:
            if (++gscount > 1) {
                std::string ch = Strutil::sprintf("R%d", rcount);
                m_spec.channelnames.push_back(ch);
            } else
                m_spec.channelnames.emplace_back("R");
            break;
        case cineon::kPrintingDensityGreen:
        case cineon::kRec709Green:
            if (++gcount > 1) {
                std::string ch = Strutil::sprintf("G%d", gcount);
                m_spec.channelnames.push_back(ch);
            } else
                m_spec.channelnames.emplace_back("G");
            break;
        case cineon::kPrintingDensityBlue:
        case cineon::kRec709Blue:
            if (++bcount > 1) {
                std::string ch = Strutil::sprintf("B%d", bcount);
                m_spec.channelnames.push_back(ch);
            } else
                m_spec.channelnames.emplace_back("B");
            break;
        default:
            std::string ch = Strutil::sprintf("channel%d",
                                              m_spec.channelnames.size());
            m_spec.channelnames.push_back(ch);
            break;
        }
    }
    // bits per sample
    m_spec.attribute("oiio:BitsPerSample", maxbits);
    // image orientation - see appendix B.2 of the OIIO documentation
    int orientation;
    switch (m_cin.header.ImageOrientation()) {
    case cineon::kLeftToRightTopToBottom: orientation = 1; break;
    case cineon::kRightToLeftTopToBottom: orientation = 2; break;
    case cineon::kLeftToRightBottomToTop: orientation = 4; break;
    case cineon::kRightToLeftBottomToTop: orientation = 3; break;
    case cineon::kTopToBottomLeftToRight: orientation = 5; break;
    case cineon::kTopToBottomRightToLeft: orientation = 6; break;
    case cineon::kBottomToTopLeftToRight: orientation = 8; break;
    case cineon::kBottomToTopRightToLeft: orientation = 7; break;
    default: orientation = 0; break;
    }
    m_spec.attribute("Orientation", orientation);

#if 1
    // This is not very smart, but it seems that as a practical matter,
    // all Cineon files are log. So ignore the gamma field and just set
    // the color space to KodakLog.
    m_spec.attribute("oiio:ColorSpace", "KodakLog");
#else
    // image linearity
    // FIXME: making this more robust would require the per-channel transfer
    // function functionality which isn't yet in OIIO
    switch (m_cin.header.ImageDescriptor(0)) {
    case cineon::kRec709Red:
    case cineon::kRec709Green:
    case cineon::kRec709Blue: m_spec.attribute("oiio:ColorSpace", "Rec709");
    default:
        // either grayscale or printing density
        if (!isinf(m_cin.header.Gamma()) && m_cin.header.Gamma() != 0.0f)
            // actual gamma value is read later on
            m_spec.attribute("oiio:ColorSpace",
                             Strutil::sprintf("GammaCorrected%.2g", g));
        break;
    }

    // gamma exponent
    if (!isinf(m_cin.header.Gamma()) && m_cin.header.Gamma() != 0.0f)
        m_spec.attribute("oiio:Gamma", (float)m_cin.header.Gamma());
#endif

    // general metadata
    // some non-compliant writers will dump a field filled with 0xFF rather
    // than a NULL string termination on the first character, so take that
    // into account, too
    if (m_cin.header.creationDate[0] && m_cin.header.creationTime[0]) {
        // libcineon's date/time format is pretty close to OIIO's (libcineon
        // uses %Y:%m:%d:%H:%M:%S%Z)
        m_spec.attribute("DateTime",
                         Strutil::sprintf("%s %s", m_cin.header.creationDate,
                                          m_cin.header.creationTime));
        // FIXME: do something about the time zone
    }

    // cineon-specific metadata
    char* strings[8];
    int ints[8];
    float floats[8];

    // image descriptor
    for (int i = 0; i < m_cin.header.NumberOfElements(); i++)
        strings[i] = get_descriptor_string(m_cin.header.ImageDescriptor(i));
    m_spec.attribute("cineon:ImageDescriptor",
                     TypeDesc(TypeDesc::STRING, m_cin.header.NumberOfElements()),
                     &strings);

    // save some typing by using macros
    // "internal" macros
// per-file attribs
#define CINEON_SET_ATTRIB_S(x, n, s) m_spec.attribute(s, m_cin.header.x(n))
#define CINEON_SET_ATTRIB(x, n) CINEON_SET_ATTRIB_S(x, n, "cineon:" #x)
#define CINEON_SET_ATTRIB_BYTE(x)                                              \
    if (m_cin.header.x() != 0xFF)                                              \
    CINEON_SET_ATTRIB(x, )
#define CINEON_SET_ATTRIB_INT(x)                                               \
    if (static_cast<unsigned int>(m_cin.header.x()) != 0xFFFFFFFF)             \
    CINEON_SET_ATTRIB(x, )
#define CINEON_SET_ATTRIB_FLOAT(x)                                             \
    if (!isinf(m_cin.header.x()))                                              \
    CINEON_SET_ATTRIB(x, )
#define CINEON_SET_ATTRIB_COORDS(x)                                            \
    m_cin.header.x(floats);                                                    \
    if (!isinf(floats[0]) && !isinf(floats[1])                                 \
        && !(floats[0] == 0. && floats[1] == 0.))                              \
    m_spec.attribute("cineon:" #x, TypeDesc(TypeDesc::FLOAT, 2), &floats[0])
#define CINEON_SET_ATTRIB_STR(X, x)                                            \
    if (m_cin.header.x[0] && m_cin.header.x[0] != char(-1))                    \
    m_spec.attribute("cineon:" #X, m_cin.header.x)

// per-element attribs
#define CINEON_SET_ATTRIB_N(x, a, t, c)                                        \
    for (int i = 0; i < m_cin.header.NumberOfElements(); i++) {                \
        c(x) a[i] = m_cin.header.x(i);                                         \
    }                                                                          \
    m_spec.attribute("cineon:" #x,                                             \
                     TypeDesc(TypeDesc::t, m_cin.header.NumberOfElements()),   \
                     &a)
#define CINEON_CHECK_ATTRIB_FLOAT(x) if (!isinf(m_cin.header.x(i)))
#define CINEON_SET_ATTRIB_FLOAT_N(x)                                           \
    CINEON_SET_ATTRIB_N(x, floats, FLOAT, CINEON_CHECK_ATTRIB_FLOAT)
#define CINEON_CHECK_ATTRIB_INT(x) if (m_cin.header.x(i) != 0xFFFFFFFF)
#define CINEON_SET_ATTRIB_INT_N(x)                                             \
    CINEON_SET_ATTRIB_N(x, ints, UINT32, CINEON_CHECK_ATTRIB_INT)
#define CINEON_CHECK_ATTRIB_BYTE(x) if (m_cin.header.x(i) != 0xFF)
#define CINEON_SET_ATTRIB_BYTE_N(x)                                            \
    CINEON_SET_ATTRIB_N(x, ints, UINT32, CINEON_CHECK_ATTRIB_BYTE)

    CINEON_SET_ATTRIB_STR(Version, version);

    // per-element data
    CINEON_SET_ATTRIB_BYTE_N(Metric);
    CINEON_SET_ATTRIB_BYTE_N(BitDepth);
    CINEON_SET_ATTRIB_INT_N(PixelsPerLine);
    CINEON_SET_ATTRIB_INT_N(LinesPerElement);
    CINEON_SET_ATTRIB_FLOAT_N(LowData);
    CINEON_SET_ATTRIB_FLOAT_N(LowQuantity);
    CINEON_SET_ATTRIB_FLOAT_N(HighData);
    CINEON_SET_ATTRIB_FLOAT_N(HighQuantity);

    CINEON_SET_ATTRIB_COORDS(WhitePoint);
    CINEON_SET_ATTRIB_COORDS(RedPrimary);
    CINEON_SET_ATTRIB_COORDS(GreenPrimary);
    CINEON_SET_ATTRIB_COORDS(BluePrimary);
    CINEON_SET_ATTRIB_STR(LabelText, labelText);

    CINEON_SET_ATTRIB_INT(XOffset);
    CINEON_SET_ATTRIB_INT(YOffset);
    CINEON_SET_ATTRIB_STR(SourceImageFileName, sourceImageFileName);
    CINEON_SET_ATTRIB_STR(InputDevice, inputDevice);
    CINEON_SET_ATTRIB_STR(InputDeviceModelNumber, inputDeviceModelNumber);
    CINEON_SET_ATTRIB_STR(InputDeviceSerialNumber, inputDeviceSerialNumber);
    CINEON_SET_ATTRIB_FLOAT(XDevicePitch);
    CINEON_SET_ATTRIB_FLOAT(YDevicePitch);

    CINEON_SET_ATTRIB_INT(FramePosition);
    CINEON_SET_ATTRIB_FLOAT(FrameRate);
    CINEON_SET_ATTRIB_STR(Format, format);
    CINEON_SET_ATTRIB_STR(FrameId, frameId);
    CINEON_SET_ATTRIB_STR(SlateInfo, slateInfo);

#undef CINEON_SET_ATTRIB_BYTE_N
#undef CINEON_CHECK_ATTRIB_BYTE
#undef CINEON_SET_ATTRIB_INT_N
#undef CINEON_CHECK_ATTRIB_INT
#undef CINEON_SET_ATTRIB_FLOAT_N
#undef CINEON_CHECK_ATTRIB_FLOAT
#undef CINEON_SET_ATTRIB_N
#undef CINEON_SET_ATTRIB_STR
#undef CINEON_SET_ATTRIB_COORDS
#undef CINEON_SET_ATTRIB_FLOAT
#undef CINEON_SET_ATTRIB_INT
#undef CINEON_SET_ATTRIB
#undef CINEON_SET_ATTRIB_S

    std::string tmpstr;
    switch (m_cin.header.ImagePacking() & ~cineon::kPackAsManyAsPossible) {
    case cineon::kPacked: tmpstr = "Packed"; break;
    case cineon::kByteLeft: tmpstr = "8-bit boundary, left justified"; break;
    case cineon::kByteRight: tmpstr = "8-bit boundary, right justified"; break;
    case cineon::kWordLeft: tmpstr = "16-bit boundary, left justified"; break;
    case cineon::kWordRight: tmpstr = "16-bit boundary, right justified"; break;
    case cineon::kLongWordLeft:
        tmpstr = "32-bit boundary, left justified";
        break;
    case cineon::kLongWordRight:
        tmpstr = "32-bit boundary, right justified";
        break;
    }
    if (m_cin.header.ImagePacking() & cineon::kPackAsManyAsPossible)
        tmpstr += ", as many fields as possible per cell";
    else
        tmpstr += ", at most one pixel per cell";

    if (!tmpstr.empty())
        m_spec.attribute("cineon:Packing", tmpstr);

    if (m_cin.header.sourceDate[0] && m_cin.header.sourceTime[0]) {
        // libcineon's date/time format is pretty close to OIIO's (libcineon
        // uses %Y:%m:%d:%H:%M:%S%Z)
        m_spec.attribute("DateTime",
                         Strutil::sprintf("%s %s", m_cin.header.sourceDate,
                                          m_cin.header.sourceTime));
        // FIXME: do something about the time zone
    }
    m_cin.header.FilmEdgeCode(buf);
    if (buf[0])
        m_spec.attribute("cineon:FilmEdgeCode", buf);

    // read in user data
    if (m_cin.header.UserSize() != 0 && m_cin.header.UserSize() != 0xFFFFFFFF) {
        m_userBuf.resize(m_cin.header.UserSize());
        m_cin.ReadUserData(&m_userBuf[0]);
    }
    if (!m_userBuf.empty())
        m_spec.attribute("cineon:UserData",
                         TypeDesc(TypeDesc::UCHAR, m_cin.header.UserSize()),
                         &m_userBuf[0]);

    newspec = spec();
    return true;
}



bool
CineonInput::close()
{
    init();  // Reset to initial state
    return true;
}



bool
CineonInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                  void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    cineon::Block block(0, y, m_cin.header.Width() - 1, y);

    // FIXME: un-hardcode the channel from 0
    if (!m_cin.ReadBlock(data, m_cin.header.ComponentDataSize(0), block))
        return false;

    return true;
}



char*
CineonInput::get_descriptor_string(cineon::Descriptor c)
{
    switch (c) {
    case cineon::kGrayscale: return (char*)"Grayscale";
    case cineon::kPrintingDensityRed: return (char*)"Red, printing density";
    case cineon::kRec709Red: return (char*)"Red, Rec709";
    case cineon::kPrintingDensityGreen: return (char*)"Green, printing density";
    case cineon::kRec709Green: return (char*)"Green, Rec709";
    case cineon::kPrintingDensityBlue: return (char*)"Blue, printing density";
    case cineon::kRec709Blue: return (char*)"Blue, Rec709";
    //case cineon::kUndefinedDescriptor:
    default: return (char*)"Undefined";
    }
}

OIIO_PLUGIN_NAMESPACE_END
