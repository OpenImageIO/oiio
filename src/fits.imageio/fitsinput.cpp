// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md


#include <cctype>
#include <cstdlib>

#include "fits_pvt.h"


OIIO_PLUGIN_NAMESPACE_BEGIN


// Obligatory material to make this a recognizeable imageio plugin
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT int fits_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
fits_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT ImageInput*
fits_input_imageio_create()
{
    return new FitsInput;
}

OIIO_EXPORT const char* fits_input_extensions[] = { "fits", nullptr };

OIIO_PLUGIN_EXPORTS_END



bool
FitsInput::valid_file(const std::string& filename) const
{
    FILE* fd = Filesystem::fopen(filename, "rb");
    if (!fd)
        return false;

    char magic[6] = { 0 };
    bool ok = (fread(magic, 1, 6, fd) == 6) && !strncmp(magic, "SIMPLE", 6);

    fclose(fd);
    return ok;
}



bool
FitsInput::open(const std::string& name, ImageSpec& spec)
{
    // saving 'name' for later use
    m_filename = name;

    // checking if the file exists and can be opened in READ mode
    m_fd = Filesystem::fopen(m_filename, "rb");
    if (!m_fd) {
        errorf("Could not open file \"%s\"", m_filename);
        return false;
    }

    // checking if the file is FITS file
    char magic[6] = { 0 };
    if (fread(magic, 1, 6, m_fd) != 6) {
        errorf("%s isn't a FITS file", m_filename);
        close();
        return false;  // Read failed
    }

    if (strncmp(magic, "SIMPLE", 6)) {
        errorf("%s isn't a FITS file", m_filename);
        close();
        return false;
    }
    // moving back to the start of the file
    fseek(m_fd, 0, SEEK_SET);

    subimage_search();

    if (!set_spec_info()) {
        close();
        return false;
    }

    spec = m_spec;
    return true;
};



bool
FitsInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    // we return true just to support 0x0 images
    if (!m_naxes)
        return true;

    std::vector<unsigned char> data_tmp(m_spec.scanline_bytes());
    long scanline_off = (m_spec.height - y) * m_spec.scanline_bytes();
    fseek(m_fd, scanline_off, SEEK_CUR);
    size_t n = fread(&data_tmp[0], 1, m_spec.scanline_bytes(), m_fd);
    if (n != m_spec.scanline_bytes()) {
        if (feof(m_fd))
            errorf("Hit end of file unexpectedly (offset=%d, scanline %d)",
                   ftell(m_fd), y);
        else
            errorf("read error");
        return false;  // Read failed
    }

    // in FITS image data is stored in big-endian so we have to switch to
    // little-endian on little-endian machines
    if (littleendian()) {
        if (m_spec.format == TypeDesc::USHORT
            || m_spec.format == TypeDesc::SHORT)
            swap_endian((unsigned short*)&data_tmp[0],
                        data_tmp.size() / sizeof(unsigned short));
        else if (m_spec.format == TypeDesc::UINT
                 || m_spec.format == TypeDesc::INT)
            swap_endian((unsigned int*)&data_tmp[0],
                        data_tmp.size() / sizeof(unsigned int));
        else if (m_spec.format == TypeDesc::FLOAT)
            swap_endian((float*)&data_tmp[0], data_tmp.size() / sizeof(float));
        else if (m_spec.format == TypeDesc::DOUBLE)
            swap_endian((double*)&data_tmp[0],
                        data_tmp.size() / sizeof(double));
    }

    memcpy(data, &data_tmp[0], data_tmp.size());

    // after reading scanline we set file pointer to the start of image data
    fsetpos(m_fd, &m_filepos);
    return true;
};



bool
FitsInput::seek_subimage(int subimage, int miplevel)
{
    if (miplevel != 0)
        return false;
    if (subimage < 0 || subimage >= (int)m_subimages.size())
        return false;

    if (subimage == m_cur_subimage) {
        return true;
    }

    // setting file pointer to the beginning of IMAGE extension
    m_cur_subimage = subimage;
    fseek(m_fd, m_subimages[m_cur_subimage].offset, SEEK_SET);

    if (!set_spec_info())
        return false;

    return true;
}



bool
FitsInput::set_spec_info()
{
    keys.clear();
    // FITS spec doesn't say anything about color space or
    // number of channels, so we read all images as if they
    // all were one-channel images
    m_spec = ImageSpec(0, 0, 1, TypeDesc::UNKNOWN);

    // reading info about current subimage
    if (!read_fits_header())
        return false;

    // now we can get the current position in the file
    // this is the start of the image data
    // we will need it in the read_native_scanline method
    fgetpos(m_fd, &m_filepos);

    if (m_bitpix == 8)
        m_spec.set_format(TypeDesc::UCHAR);
    else if (m_bitpix == 16)
        m_spec.set_format(TypeDesc::SHORT);
    else if (m_bitpix == 32)
        m_spec.set_format(TypeDesc::INT);
    else if (m_bitpix == -32)
        m_spec.set_format(TypeDesc::FLOAT);
    else if (m_bitpix == -64)
        m_spec.set_format(TypeDesc::DOUBLE);
    return true;
}



bool
FitsInput::close(void)
{
    if (m_fd)
        fclose(m_fd);
    init();
    return true;
}



bool
FitsInput::read_fits_header(void)
{
    std::string fits_header(HEADER_SIZE, 0);

    // we read whole header at once
    if (fread(&fits_header[0], 1, HEADER_SIZE, m_fd) != HEADER_SIZE) {
        if (feof(m_fd))
            errorf("Hit end of file unexpectedly (offset=%d)", ftell(m_fd));
        else
            errorf("read error");
        return false;  // Read failed
    }

    bool found_end = false;
    for (int i = 0; i < CARDS_PER_HEADER; ++i) {
        std::string card(CARD_SIZE, 0);
        // reading card number i
        memcpy(&card[0], &fits_header[i * CARD_SIZE], CARD_SIZE);

        std::string keyname, value;
        fits_pvt::unpack_card(card, keyname, value);

        // END means that this is end of the FITS header
        // we can now add to the ImageSpec COMMENT, HISTORY and HIERARCH keys
        if (keyname == "END") {
            // removing white spaces that we use to separate lines of comments
            // from the end ot the string
            m_comment  = m_comment.substr(0, m_comment.size() - m_sep.size());
            m_history  = m_history.substr(0, m_history.size() - m_sep.size());
            m_hierarch = m_hierarch.substr(0, m_hierarch.size() - m_sep.size());
            add_to_spec("Comment", m_comment);
            add_to_spec("History", m_history);
            add_to_spec("Hierarch", m_hierarch);
            found_end = true;
            break;
        }

        if (keyname == "SIMPLE" || keyname == "XTENSION")
            continue;

        // setting up some important fields
        // m_bitpix - format of the data (eg. bpp)
        // m_naxes - number of axes
        // width, height and depth of the image
        if (keyname == "BITPIX") {
            m_bitpix = Strutil::stoi(&card[10]);
            continue;
        }
        if (keyname == "NAXIS") {
            m_naxes = Strutil::stoi(&card[10]);
            m_naxis.resize(m_naxes);
            continue;
        }
        if (Strutil::starts_with(keyname, "NAXIS")) {
            int i = Strutil::stoi(keyname.substr(5));
            if (i > 0 && i <= m_naxes)
                m_naxis[i - 1] = Strutil::stoi(&card[10]);
            continue;
        }
        if (keyname == "ORIENTAT") {
            add_to_spec("Orientation", value);
            continue;
        }
        if (keyname == "DATE") {
            add_to_spec("DateTime", convert_date(value));
            continue;
        }
        if (keyname == "COMMENT") {
            m_comment += (value + m_sep);
            continue;
        }
        if (keyname == "HISTORY") {
            m_history += (value + m_sep);
            continue;
        }
        if (keyname == "HIERARCH") {
            m_hierarch += (value + m_sep);
            continue;
        }

        Strutil::to_lower(keyname);  // make lower case
        if (keyname.size() >= 1)
            keyname[0] = toupper(keyname[0]);
        add_to_spec(keyname, value);
    }

    // Fix up dimensions
    while (m_naxes > 1 && m_naxis[m_naxes - 1] == 1) {
        --m_naxes;
    }
    if (m_naxes < 0 || m_naxes > 4) {
        errorf("Number of data axes %d not supported", m_naxes);
        return false;
    }
    m_spec.nchannels = 1;
    m_spec.depth     = 1;
    if (m_naxes == 0 || m_naxis[0] == 0) {
        m_spec.width = m_spec.height = 0;
    } else if (m_naxes == 1) {
        m_spec.width  = m_naxis[0];
        m_spec.height = 1;
    } else if (m_naxes == 2) {
        m_spec.width  = m_naxis[0];
        m_spec.height = m_naxis[1];
    } else if (m_naxes == 3 && m_naxis[0] <= 4) {
        // 3D, small number of most-rapidly changing dimension: color image?
        m_spec.nchannels = m_naxis[0];
        m_spec.width     = m_naxis[1];
        m_spec.height    = m_naxis[2];
    } else if (m_naxes == 3) {
        // 3D, large number of most-rapidly changing dimension: volume?
        m_spec.width  = m_naxis[0];
        m_spec.height = m_naxis[1];
        m_spec.depth  = m_naxis[2];
    } else if (m_naxes == 4) {
        // 4D... volume + color?
        m_spec.nchannels = m_naxis[0];
        m_spec.width     = m_naxis[1];
        m_spec.height    = m_naxis[2];
        m_spec.depth     = m_naxis[3];
    } else {
        errorf("Don't know now to read %d-channel FITS image", m_naxes);
        return false;
    }
    m_spec.full_width  = m_spec.width;
    m_spec.full_height = m_spec.height;
    m_spec.full_depth  = m_spec.depth;

    // if (m_spec.width < 1 || m_spec.height < 1 || m_spec.depth < 1 ||
    //     m_spec.nchannels < 1) {
    //     errorf("Don't know now to read empty (0 pixel) FITS image");
    //     return false;
    // }

    // if we didn't found END keyword in current header, we read next one
    return found_end ? true : read_fits_header();
}



void
FitsInput::add_to_spec(const std::string& keyname, const std::string& value)
{
    // we don't add empty keys (or keys with empty values) to ImageSpec
    if (!keyname.size() || !value.size())
        return;

    // COMMENT, HISTORY, HIERARCH and DATE keywords we save AS-IS
    bool speckey = (keyname == "Comment" || keyname == "History"
                    || keyname == "Hierarch");
    if (speckey || keyname == "DateTime") {
        m_spec.attribute(keyname, value);
        return;
    }

    // converting string to float or integer
    bool isNumSign = (value[0] == '+' || value[0] == '-' || value[0] == '.');
    if (isdigit(value[0]) || isNumSign) {
        float val = Strutil::stof(value);
        if (val == (int)val)
            m_spec.attribute(keyname, (int)val);
        else
            m_spec.attribute(keyname, val);
    } else
        m_spec.attribute(keyname, value);
}



void
FitsInput::subimage_search()
{
    // saving position of the file, just for safe)
    fpos_t fpos;
    fgetpos(m_fd, &fpos);

    // starting reading headers from the beginning of the file
    fseek(m_fd, 0, SEEK_SET);

    // we search for subimages by reading whole header and checking if it
    // starts by "SIMPLE" keyword (primary header is always image header)
    // or by "XTENSION= 'IMAGE   '" (it is image extensions)
    std::string hdu(HEADER_SIZE, 0);
    size_t offset = 0;
    while (fread(&hdu[0], 1, HEADER_SIZE, m_fd) == HEADER_SIZE) {
        if (!strncmp(&hdu[0], "SIMPLE", 6)
            || !strncmp(&hdu[0], "XTENSION= 'IMAGE   '", 20)) {
            fits_pvt::Subimage newSub;
            newSub.number = m_subimages.size();
            newSub.offset = offset;
            m_subimages.push_back(newSub);
        }
        offset += HEADER_SIZE;
    }
    fsetpos(m_fd, &fpos);
}



std::string
FitsInput::convert_date(const std::string& date)
{
    using Strutil::stoi;
    std::string ndate;
    if (date[4] == '-') {
        // YYYY-MM-DDThh:mm:ss convention is used since 1 January 2000
        ndate = Strutil::sprintf("%04u:%02u:%02u", stoi(&date[0]),
                                 stoi(&date[5]), stoi(&date[8]));
        if (date.size() >= 11 && date[10] == 'T')
            ndate += Strutil::sprintf(" %02u:%02u:%02u", stoi(&date[11]),
                                      stoi(&date[14]), stoi(&date[17]));
        return ndate;
    }

    if (date[2] == '/') {
        // DD/MM/YY convention was used before 1 January 2000
        ndate = Strutil::sprintf("19%02u:%02u:%02u 00:00:00", stoi(&date[6]),
                                 stoi(&date[3]), stoi(&date[0]));
        return ndate;
    }
    // unrecognized format
    return date;
}

OIIO_PLUGIN_NAMESPACE_END
