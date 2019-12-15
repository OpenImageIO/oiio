// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <cstdlib>
#include <fstream>
#include <string>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/fmath.h>
#include <OpenImageIO/imageio.h>

OIIO_PLUGIN_NAMESPACE_BEGIN

class PNMInput final : public ImageInput {
public:
    PNMInput() {}
    virtual ~PNMInput() { close(); }
    virtual const char* format_name(void) const override { return "pnm"; }
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override { return 0; }
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;

private:
    enum PNMType { P1, P2, P3, P4, P5, P6, Pf, PF };

    OIIO::ifstream m_file;
    std::streampos m_header_end_pos;  // file position after the header
    std::string m_current_line;       ///< Buffer the image pixels
    const char* m_pos;
    PNMType m_pnm_type;
    unsigned int m_max_val;
    float m_scaling_factor;

    bool read_file_scanline(void* data, int y);
    bool read_file_header();
};



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
pnm_input_imageio_create()
{
    return new PNMInput;
}

OIIO_EXPORT int pnm_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
pnm_imageio_library_version()
{
    return nullptr;
}

OIIO_EXPORT const char* pnm_input_extensions[] = { "ppm", "pgm", "pbm",
                                                   "pnm", "pfm", nullptr };

OIIO_PLUGIN_EXPORTS_END


inline bool
nextLine(std::istream& file, std::string& current_line, const char*& pos)
{
    if (!file.good())
        return false;
    getline(file, current_line);
    if (file.fail())
        return false;
    pos = current_line.c_str();
    return true;
}



inline const char*
nextToken(std::istream& file, std::string& current_line, const char*& pos)
{
    while (1) {
        while (isspace(*pos))
            pos++;
        if (*pos)
            break;
        else
            nextLine(file, current_line, pos);
    }
    return pos;
}



inline const char*
skipComments(std::istream& file, std::string& current_line, const char*& pos,
             char comment = '#')
{
    while (1) {
        nextToken(file, current_line, pos);
        if (*pos == comment)
            nextLine(file, current_line, pos);
        else
            break;
    }
    return pos;
}



inline bool
nextVal(std::istream& file, std::string& current_line, const char*& pos,
        int& val, char comment = '#')
{
    skipComments(file, current_line, pos, comment);
    if (!isdigit(*pos))
        return false;
    val = strtol(pos, (char**)&pos, 10);
    return true;
}



template<class T>
inline void
invert(const T* read, T* write, imagesize_t nvals)
{
    for (imagesize_t i = 0; i < nvals; i++)
        write[i] = std::numeric_limits<T>::max() - read[i];
}



template<class T>
inline bool
ascii_to_raw(std::istream& file, std::string& current_line, const char*& pos,
             T* write, imagesize_t nvals, T max)
{
    if (max)
        for (imagesize_t i = 0; i < nvals; i++) {
            int tmp;
            if (!nextVal(file, current_line, pos, tmp))
                return false;
            write[i] = std::min((int)max, tmp) * std::numeric_limits<T>::max()
                       / max;
        }
    else
        for (imagesize_t i = 0; i < nvals; i++)
            write[i] = std::numeric_limits<T>::max();
    return true;
}



template<class T>
inline void
raw_to_raw(const T* read, T* write, imagesize_t nvals, T max)
{
    if (max)
        for (imagesize_t i = 0; i < nvals; i++) {
            int tmp  = read[i];
            write[i] = std::min((int)max, tmp) * std::numeric_limits<T>::max()
                       / max;
        }
    else
        for (imagesize_t i = 0; i < nvals; i++)
            write[i] = std::numeric_limits<T>::max();
}



inline void
unpack(const unsigned char* read, unsigned char* write, imagesize_t size)
{
    imagesize_t w = 0, r = 0;
    unsigned char bit = 0x7, byte = 0;
    for (imagesize_t x = 0; x < size; x++) {
        if (bit == 0x7)
            byte = ~read[r++];
        write[w++] = 0 - ((byte & (1 << bit)) >> bit);  //assign expanded bit
        bit        = (bit - 1) & 0x7;                   // limit bit to [0; 8[
    }
}

inline void
unpack_floats(const unsigned char* read, float* write, imagesize_t numsamples,
              float scaling_factor)
{
    float* read_floats = (float*)read;

    if ((scaling_factor < 0 && bigendian())
        || (scaling_factor > 0 && littleendian())) {
        swap_endian(read_floats, numsamples);
    }

    float absfactor = fabs(scaling_factor);
    for (imagesize_t i = 0; i < numsamples; i++) {
        write[i] = absfactor * read_floats[i];
    }
}



template<class T>
inline bool
read_int(std::istream& in, T& dest, char comment = '#')
{
    T ret;
    char c;
    while (!in.eof()) {
        in >> ret;
        if (!in.good()) {
            in.clear();
            in >> c;
            if (c == comment)
                in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            else
                return false;
        } else {
            dest = ret;
            return true;
        }
    }
    return false;
}



bool
PNMInput::read_file_scanline(void* data, int y)
{
    try {
        std::vector<unsigned char> buf;
        bool good = true;
        if (!m_file)
            return false;
        int nsamples = m_spec.width * m_spec.nchannels;

        // PFM files are bottom-to-top, so we need to seek to the right spot
        if (m_pnm_type == PF || m_pnm_type == Pf) {
            int file_scanline     = m_spec.height - 1 - (y - m_spec.y);
            std::streampos offset = file_scanline * m_spec.scanline_bytes();
            m_file.seekg(m_header_end_pos + offset, std::ios_base::beg);
        }

        if ((m_pnm_type >= P4 && m_pnm_type <= P6) || m_pnm_type == PF
            || m_pnm_type == Pf) {
            int numbytes;
            if (m_pnm_type == P4)
                numbytes = (m_spec.width + 7) / 8;
            else if (m_pnm_type == PF || m_pnm_type == Pf)
                numbytes = m_spec.nchannels * 4 * m_spec.width;
            else
                numbytes = m_spec.scanline_bytes();
            buf.resize(numbytes);
            m_file.read((char*)&buf[0], numbytes);
            if (!m_file.good())
                return false;
        }

        switch (m_pnm_type) {
        //Ascii
        case P1:
            good &= ascii_to_raw(m_file, m_current_line, m_pos,
                                 (unsigned char*)data, nsamples,
                                 (unsigned char)m_max_val);
            invert((unsigned char*)data, (unsigned char*)data, nsamples);
            break;
        case P2:
        case P3:
            if (m_max_val > std::numeric_limits<unsigned char>::max())
                good &= ascii_to_raw(m_file, m_current_line, m_pos,
                                     (unsigned short*)data, nsamples,
                                     (unsigned short)m_max_val);
            else
                good &= ascii_to_raw(m_file, m_current_line, m_pos,
                                     (unsigned char*)data, nsamples,
                                     (unsigned char)m_max_val);
            break;
        //Raw
        case P4: unpack(&buf[0], (unsigned char*)data, nsamples); break;
        case P5:
        case P6:
            if (m_max_val > std::numeric_limits<unsigned char>::max()) {
                if (littleendian())
                    swap_endian((unsigned short*)&buf[0], nsamples);
                raw_to_raw((unsigned short*)&buf[0], (unsigned short*)data,
                           nsamples, (unsigned short)m_max_val);
            } else {
                raw_to_raw((unsigned char*)&buf[0], (unsigned char*)data,
                           nsamples, (unsigned char)m_max_val);
            }
            break;
        //Floating point
        case Pf:
        case PF:
            unpack_floats(&buf[0], (float*)data, nsamples, m_scaling_factor);
            break;
        default: return false;
        }
        return good;

    } catch (const std::exception& e) {
        errorf("PNM exception: %s", e.what());
        return false;
    }
}



bool
PNMInput::read_file_header()
{
    try {
        unsigned int width, height;
        char c;
        if (!m_file)
            return false;

        //MagicNumber
        m_file >> c;
        if (c != 'P')
            return false;

        m_file >> c;
        switch (c) {
        case '1': m_pnm_type = P1; break;
        case '2': m_pnm_type = P2; break;
        case '3': m_pnm_type = P3; break;
        case '4': m_pnm_type = P4; break;
        case '5': m_pnm_type = P5; break;
        case '6': m_pnm_type = P6; break;
        case 'f': m_pnm_type = Pf; break;
        case 'F': m_pnm_type = PF; break;
        default: return false;
        }

        //Size
        if (!read_int(m_file, width))
            return false;
        if (!read_int(m_file, height))
            return false;

        if (m_pnm_type != PF && m_pnm_type != Pf) {
            //Max Val
            if (m_pnm_type != P1 && m_pnm_type != P4) {
                if (!read_int(m_file, m_max_val))
                    return false;
            } else
                m_max_val = 1;

            //Space before content
            if (!(isspace(m_file.get()) && m_file.good()))
                return false;
            m_header_end_pos = m_file.tellg();  // remember file pos

            if (m_pnm_type == P3 || m_pnm_type == P6)
                m_spec = ImageSpec(width, height, 3,
                                   (m_max_val > 255) ? TypeDesc::UINT16
                                                     : TypeDesc::UINT8);
            else
                m_spec = ImageSpec(width, height, 1,
                                   (m_max_val > 255) ? TypeDesc::UINT16
                                                     : TypeDesc::UINT8);

            if (m_spec.nchannels == 1)
                m_spec.channelnames[0] = "I";
            else
                m_spec.default_channel_names();

            if (m_pnm_type >= P1 && m_pnm_type <= P3)
                m_spec.attribute("pnm:binary", 0);
            else
                m_spec.attribute("pnm:binary", 1);

            m_spec.attribute("oiio:BitsPerSample",
                             ceilf(logf(m_max_val + 1) / logf(2)));
            return true;
        } else {
            //Read scaling factor
            if (!read_int(m_file, m_scaling_factor)) {
                return false;
            }

            //Space before content
            if (!(isspace(m_file.get()) && m_file.good()))
                return false;
            m_header_end_pos = m_file.tellg();  // remember file pos

            if (m_pnm_type == PF) {
                m_spec = ImageSpec(width, height, 3, TypeDesc::FLOAT);
                m_spec.default_channel_names();
            } else {
                m_spec = ImageSpec(width, height, 1, TypeDesc::FLOAT);
                m_spec.channelnames[0] = "I";
            }

            if (m_scaling_factor < 0) {
                m_spec.attribute("pnm:bigendian", 0);
            } else {
                m_spec.attribute("pnm:bigendian", 1);
            }

            return true;
        }
    } catch (const std::exception& e) {
        errorf("PNM exception: %s", e.what());
        return false;
    }
}



bool
PNMInput::open(const std::string& name, ImageSpec& newspec)
{
    close();  //close previously opened file

    Filesystem::open(m_file, name, std::ios::in | std::ios::binary);

    m_current_line = "";
    m_pos          = m_current_line.c_str();

    if (!read_file_header()) {
        close();
        return false;
    }

    newspec = m_spec;
    return true;
}



bool
PNMInput::close()
{
    m_file.close();
    return true;
}



bool
PNMInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                               void* data)
{
    lock_guard lock(m_mutex);
    if (!seek_subimage(subimage, miplevel))
        return false;

    if (z)
        return false;
    if (!read_file_scanline(data, y))
        return false;
    return true;
}

OIIO_PLUGIN_NAMESPACE_END
