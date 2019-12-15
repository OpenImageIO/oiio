// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <algorithm>
#include <cassert>
#include <cstdio>

#include <OpenImageIO/color.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/tiffutils.h>

#include "jpeg_pvt.h"

OIIO_PLUGIN_NAMESPACE_BEGIN


// N.B. The class definition for JpgInput is in jpeg_pvt.h.


// Export version number and create function symbols
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int jpeg_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
jpeg_imageio_library_version()
{
#define STRINGIZE2(a) #a
#define STRINGIZE(a) STRINGIZE2(a)
#ifdef LIBJPEG_TURBO_VERSION
    return "jpeg-turbo " STRINGIZE(LIBJPEG_TURBO_VERSION) "/jp" STRINGIZE(
        JPEG_LIB_VERSION);
#else
    return "jpeglib " STRINGIZE(JPEG_LIB_VERSION_MAJOR) "." STRINGIZE(
        JPEG_LIB_VERSION_MINOR);
#endif
}

OIIO_EXPORT ImageInput*
jpeg_input_imageio_create()
{
    return new JpgInput;
}

OIIO_EXPORT const char* jpeg_input_extensions[]
    = { "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", nullptr };

OIIO_PLUGIN_EXPORTS_END


static const uint8_t JPEG_MAGIC1 = 0xff;
static const uint8_t JPEG_MAGIC2 = 0xd8;


// For explanations of the error handling, see the "example.c" in the
// libjpeg distribution.


static void
my_error_exit(j_common_ptr cinfo)
{
    /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
    JpgInput::my_error_ptr myerr = (JpgInput::my_error_ptr)cinfo->err;

    /* Always display the message. */
    /* We could postpone this until after returning, if we chose. */
    //  (*cinfo->err->output_message) (cinfo);
    myerr->jpginput->jpegerror(myerr, true);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}



static void
my_output_message(j_common_ptr cinfo)
{
    JpgInput::my_error_ptr myerr = (JpgInput::my_error_ptr)cinfo->err;

    // Create the message
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    myerr->jpginput->jpegerror(myerr, true);

    /* Return control to the setjmp point */
    longjmp(myerr->setjmp_buffer, 1);
}



static std::string
comp_info_to_attr(const jpeg_decompress_struct& cinfo)
{
    // Compare the current 6 samples with our known definitions
    // to determine the corresponding subsampling attr
    std::vector<int> comp;
    comp.push_back(cinfo.comp_info[0].h_samp_factor);
    comp.push_back(cinfo.comp_info[0].v_samp_factor);
    comp.push_back(cinfo.comp_info[1].h_samp_factor);
    comp.push_back(cinfo.comp_info[1].v_samp_factor);
    comp.push_back(cinfo.comp_info[2].h_samp_factor);
    comp.push_back(cinfo.comp_info[2].v_samp_factor);
    size_t size = comp.size();

    if (std::equal(JPEG_444_COMP, JPEG_444_COMP + size, comp.begin()))
        return JPEG_444_STR;
    else if (std::equal(JPEG_422_COMP, JPEG_422_COMP + size, comp.begin()))
        return JPEG_422_STR;
    else if (std::equal(JPEG_420_COMP, JPEG_420_COMP + size, comp.begin()))
        return JPEG_420_STR;
    else if (std::equal(JPEG_411_COMP, JPEG_411_COMP + size, comp.begin()))
        return JPEG_411_STR;
    return "";
}



void
JpgInput::jpegerror(my_error_ptr myerr, bool fatal)
{
    // Send the error message to the ImageInput
    char errbuf[JMSG_LENGTH_MAX];
    (*m_cinfo.err->format_message)((j_common_ptr)&m_cinfo, errbuf);
    errorf("JPEG error: %s (\"%s\")", errbuf, filename());

    // Shut it down and clean it up
    if (fatal) {
        m_fatalerr = true;
        close();
        m_fatalerr = true;  // because close() will reset it
    }
}



bool
JpgInput::valid_file(const std::string& filename, Filesystem::IOProxy* io) const
{
    // Check magic number to assure this is a JPEG file
    uint8_t magic[2] = { 0, 0 };
    bool ok          = true;

    if (io) {
        ok = (io->pread(magic, sizeof(magic), 0) == sizeof(magic));
    } else {
        FILE* fd = Filesystem::fopen(filename, "rb");
        if (!fd)
            return false;
        ok = (fread(magic, sizeof(magic), 1, fd) == 1);
        fclose(fd);
    }

    if (magic[0] != JPEG_MAGIC1 || magic[1] != JPEG_MAGIC2) {
        ok = false;
    }
    return ok;
}



bool
JpgInput::open(const std::string& name, ImageSpec& newspec,
               const ImageSpec& config)
{
    auto p = config.find_attribute("_jpeg:raw", TypeInt);
    m_raw  = p && *(int*)p->data();
    p      = config.find_attribute("oiio:ioproxy", TypeDesc::PTR);
    m_io   = p ? p->get<Filesystem::IOProxy*>() : nullptr;
    return open(name, newspec);
}



bool
JpgInput::open(const std::string& name, ImageSpec& newspec)
{
    m_filename = name;

    if (m_io) {
        // If an IOProxy was passed, it had better be a File or a
        // MemReader, that's all we know how to use with jpeg.
        std::string proxytype = m_io->proxytype();
        if (proxytype != "file" && proxytype != "memreader") {
            errorf("JPEG reader can't handle proxy type %s", proxytype);
            return false;
        }
    } else {
        // If no proxy was supplied, create a file reader
        m_io = new Filesystem::IOFile(name, Filesystem::IOProxy::Mode::Read);
        m_local_io.reset(m_io);
    }
    if (!m_io || m_io->mode() != Filesystem::IOProxy::Mode::Read) {
        errorf("Could not open file \"%s\"", name);
        return false;
    }

    // Check magic number to assure this is a JPEG file
    uint8_t magic[2] = { 0, 0 };
    if (m_io->pread(magic, sizeof(magic), 0) != sizeof(magic)) {
        errorf("Empty file \"%s\"", name);
        close_file();
        return false;
    }

    if (magic[0] != JPEG_MAGIC1 || magic[1] != JPEG_MAGIC2) {
        errorf(
            "\"%s\" is not a JPEG file, magic number doesn't match (was 0x%x%x)",
            name, int(magic[0]), int(magic[1]));
        close_file();
        return false;
    }

    // Set up the normal JPEG error routines, then override error_exit and
    // output_message so we intercept all the errors.
    m_cinfo.err               = jpeg_std_error((jpeg_error_mgr*)&m_jerr);
    m_jerr.pub.error_exit     = my_error_exit;
    m_jerr.pub.output_message = my_output_message;
    if (setjmp(m_jerr.setjmp_buffer)) {
        // Jump to here if there's a libjpeg internal error
        // Prevent memory leaks, see example.c in jpeg distribution
        jpeg_destroy_decompress(&m_cinfo);
        close_file();
        return false;
    }

    // initialize decompressor
    jpeg_create_decompress(&m_cinfo);
    m_decomp_create = true;
    // specify the data source
    if (!strcmp(m_io->proxytype(), "file")) {
        auto fd = ((Filesystem::IOFile*)m_io)->handle();
        jpeg_stdio_src(&m_cinfo, fd);
    } else {
        auto buffer = ((Filesystem::IOMemReader*)m_io)->buffer();
        jpeg_mem_src(&m_cinfo, const_cast<unsigned char*>(buffer.data()),
                     buffer.size());
    }

    // Request saving of EXIF and other special tags for later spelunking
    for (int mark = 0; mark < 16; ++mark)
        jpeg_save_markers(&m_cinfo, JPEG_APP0 + mark, 0xffff);
    jpeg_save_markers(&m_cinfo, JPEG_COM, 0xffff);  // comment marker

    // read the file parameters
    if (jpeg_read_header(&m_cinfo, FALSE) != JPEG_HEADER_OK || m_fatalerr) {
        errorf("Bad JPEG header for \"%s\"", filename());
        close();
        return false;
    }

    int nchannels = m_cinfo.num_components;

    if (m_cinfo.jpeg_color_space == JCS_CMYK
        || m_cinfo.jpeg_color_space == JCS_YCCK) {
        // CMYK jpegs get converted by us to RGB
        m_cinfo.out_color_space = JCS_CMYK;  // pre-convert YCbCrK->CMYK
        nchannels               = 3;
        m_cmyk                  = true;
    }

    if (m_raw)
        m_coeffs = jpeg_read_coefficients(&m_cinfo);
    else
        jpeg_start_decompress(&m_cinfo);  // start working
    if (m_fatalerr) {
        close();
        return false;
    }
    m_next_scanline = 0;  // next scanline we'll read

    m_spec = ImageSpec(m_cinfo.output_width, m_cinfo.output_height, nchannels,
                       TypeDesc::UINT8);

    // Assume JPEG is in sRGB unless the Exif or XMP tags say otherwise.
    m_spec.attribute("oiio:ColorSpace", "sRGB");

    if (m_cinfo.jpeg_color_space == JCS_CMYK)
        m_spec.attribute("jpeg:ColorSpace", "CMYK");
    else if (m_cinfo.jpeg_color_space == JCS_YCCK)
        m_spec.attribute("jpeg:ColorSpace", "YCbCrK");

    // If the chroma subsampling is detected and matches something
    // we expect, then set an attribute so that it can be preserved
    // in future operations.
    std::string subsampling = comp_info_to_attr(m_cinfo);
    if (!subsampling.empty())
        m_spec.attribute(JPEG_SUBSAMPLING_ATTR, subsampling);

    for (jpeg_saved_marker_ptr m = m_cinfo.marker_list; m; m = m->next) {
        if (m->marker == (JPEG_APP0 + 1)
            && !strcmp((const char*)m->data, "Exif")) {
            // The block starts with "Exif\0\0", so skip 6 bytes to get
            // to the start of the actual Exif data TIFF directory
            decode_exif(string_view((char*)m->data + 6, m->data_length - 6),
                        m_spec);
        } else if (m->marker == (JPEG_APP0 + 1)
                   && !strcmp((const char*)m->data,
                              "http://ns.adobe.com/xap/1.0/")) {
#ifndef NDEBUG
            std::cerr << "Found APP1 XMP! length " << m->data_length << "\n";
#endif
            std::string xml((const char*)m->data, m->data_length);
            decode_xmp(xml, m_spec);
        } else if (m->marker == (JPEG_APP0 + 13)
                   && !strcmp((const char*)m->data, "Photoshop 3.0"))
            jpeg_decode_iptc((unsigned char*)m->data);
        else if (m->marker == JPEG_COM) {
            if (!m_spec.find_attribute("ImageDescription", TypeDesc::STRING))
                m_spec.attribute("ImageDescription",
                                 std::string((const char*)m->data,
                                             m->data_length));
        }
    }

    // Handle density/pixelaspect. We need to do this AFTER the exif is
    // decoded, in case it contains useful information.
    float xdensity = m_spec.get_float_attribute("XResolution");
    float ydensity = m_spec.get_float_attribute("YResolution");
    if (!xdensity || !ydensity) {
        xdensity = float(m_cinfo.X_density);
        ydensity = float(m_cinfo.Y_density);
        if (xdensity && ydensity) {
            m_spec.attribute("XResolution", xdensity);
            m_spec.attribute("YResolution", ydensity);
        }
    }
    if (xdensity && ydensity) {
        float aspect = ydensity / xdensity;
        if (aspect != 1.0f)
            m_spec.attribute("PixelAspectRatio", aspect);
        switch (m_cinfo.density_unit) {
        case 0: m_spec.attribute("ResolutionUnit", "none"); break;
        case 1: m_spec.attribute("ResolutionUnit", "in"); break;
        case 2: m_spec.attribute("ResolutionUnit", "cm"); break;
        }
    }

    read_icc_profile(&m_cinfo, m_spec);  /// try to read icc profile

    newspec = m_spec;
    return true;
}



bool
JpgInput::read_icc_profile(j_decompress_ptr cinfo, ImageSpec& spec)
{
    int num_markers = 0;
    std::vector<unsigned char> icc_buf;
    unsigned int total_length = 0;
    const int MAX_SEQ_NO      = 255;
    unsigned char marker_present
        [MAX_SEQ_NO
         + 1];  // one extra is used to store the flag if marker is found, set to one if marker is found
    unsigned int data_length[MAX_SEQ_NO + 1];  // store the size of each marker
    unsigned int data_offset[MAX_SEQ_NO + 1];  // store the offset of each marker
    memset(marker_present, 0, (MAX_SEQ_NO + 1));

    for (jpeg_saved_marker_ptr m = cinfo->marker_list; m; m = m->next) {
        if (m->marker == (JPEG_APP0 + 2)
            && !strcmp((const char*)m->data, "ICC_PROFILE")) {
            if (num_markers == 0)
                num_markers = GETJOCTET(m->data[13]);
            else if (num_markers != GETJOCTET(m->data[13]))
                return false;
            int seq_no = GETJOCTET(m->data[12]);
            if (seq_no <= 0 || seq_no > num_markers)
                return false;
            if (marker_present[seq_no])  // duplicate marker
                return false;
            marker_present[seq_no] = 1;  // flag found marker
            data_length[seq_no]    = m->data_length - ICC_HEADER_SIZE;
        }
    }
    if (num_markers == 0)
        return false;

    // checking for missing markers
    for (int seq_no = 1; seq_no <= num_markers; seq_no++) {
        if (marker_present[seq_no] == 0)
            return false;  // missing sequence number
        data_offset[seq_no] = total_length;
        total_length += data_length[seq_no];
    }

    if (total_length == 0)
        return false;  // found only empty markers

    icc_buf.resize(total_length * sizeof(JOCTET));

    // and fill it in
    for (jpeg_saved_marker_ptr m = cinfo->marker_list; m; m = m->next) {
        if (m->marker == (JPEG_APP0 + 2)
            && !strcmp((const char*)m->data, "ICC_PROFILE")) {
            int seq_no = GETJOCTET(m->data[12]);
            memcpy(&icc_buf[0] + data_offset[seq_no], m->data + ICC_HEADER_SIZE,
                   data_length[seq_no]);
        }
    }
    spec.attribute(ICC_PROFILE_ATTR, TypeDesc(TypeDesc::UINT8, total_length),
                   &icc_buf[0]);
    return true;
}



static void
cmyk_to_rgb(int n, const unsigned char* cmyk, size_t cmyk_stride,
            unsigned char* rgb, size_t rgb_stride)
{
    for (; n; --n, cmyk += cmyk_stride, rgb += rgb_stride) {
        // JPEG seems to store CMYK as 1-x
        float C = convert_type<unsigned char, float>(cmyk[0]);
        float M = convert_type<unsigned char, float>(cmyk[1]);
        float Y = convert_type<unsigned char, float>(cmyk[2]);
        float K = convert_type<unsigned char, float>(cmyk[3]);
        float R = C * K;
        float G = M * K;
        float B = Y * K;
        rgb[0]  = convert_type<float, unsigned char>(R);
        rgb[1]  = convert_type<float, unsigned char>(G);
        rgb[2]  = convert_type<float, unsigned char>(B);
    }
}



bool
JpgInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    if (!seek_subimage(subimage, miplevel))
        return false;
    if (m_raw)
        return false;
    if (y < 0 || y >= (int)m_cinfo.output_height)  // out of range scanline
        return false;
    if (m_next_scanline > y) {
        // User is trying to read an earlier scanline than the one we're
        // up to.  Easy fix: close the file and re-open.
        ImageSpec dummyspec;
        int subimage = current_subimage();
        if (!close() || !open(m_filename, dummyspec)
            || !seek_subimage(subimage, 0))
            return false;  // Somehow, the re-open failed
        assert(m_next_scanline == 0 && current_subimage() == subimage);
    }

    // Set up our custom error handler
    if (setjmp(m_jerr.setjmp_buffer)) {
        // Jump to here if there's a libjpeg internal error
        return false;
    }

    void* readdata = data;
    if (m_cmyk) {
        // If the file's data is CMYK, read into a 4-channel buffer, then
        // we'll have to convert.
        m_cmyk_buf.resize(m_spec.width * 4);
        readdata = &m_cmyk_buf[0];
        ASSERT(m_spec.nchannels == 3);
    }

    for (; m_next_scanline <= y; ++m_next_scanline) {
        // Keep reading until we've read the scanline we really need
        if (jpeg_read_scanlines(&m_cinfo, (JSAMPLE**)&readdata, 1) != 1
            || m_fatalerr) {
            errorf("JPEG failed scanline read (\"%s\")", filename());
            return false;
        }
    }

    if (m_cmyk)
        cmyk_to_rgb(m_spec.width, (unsigned char*)readdata, 4,
                    (unsigned char*)data, 3);

    return true;
}



bool
JpgInput::close()
{
    if (m_io) {
        // unnecessary?  jpeg_abort_decompress (&m_cinfo);
        if (m_decomp_create)
            jpeg_destroy_decompress(&m_cinfo);
        m_decomp_create = false;
        close_file();
    }
    init();  // Reset to initial state
    return true;
}



void
JpgInput::jpeg_decode_iptc(const unsigned char* buf)
{
    // APP13 blob doesn't have to be IPTC info.  Look for the IPTC marker,
    // which is the string "Photoshop 3.0" followed by a null character.
    if (strcmp((const char*)buf, "Photoshop 3.0"))
        return;
    buf += strlen("Photoshop 3.0") + 1;

    // Next are the 4 bytes "8BIM"
    if (strncmp((const char*)buf, "8BIM", 4))
        return;
    buf += 4;

    // Next two bytes are the segment type, in big endian.
    // We expect 1028 to indicate IPTC data block.
    if (((buf[0] << 8) + buf[1]) != 1028)
        return;
    buf += 2;

    // Next are 4 bytes of 0 padding, just skip it.
    buf += 4;

    // Next is 2 byte (big endian) giving the size of the segment
    int segmentsize = (buf[0] << 8) + buf[1];
    buf += 2;

    decode_iptc_iim(buf, segmentsize, m_spec);
}

OIIO_PLUGIN_NAMESPACE_END
