#include <cstdio>
#include <cstdlib>
#include<vector>
#include "gif_lib.h"
#include "imageio.h"


OIIO_PLUGIN_NAMESPACE_BEGIN


class GIFOutput : public ImageOutput {
public:
    GIFOutput ();
    virtual ~GIFOutput ();
    virtual const char * format_name (void) const { return "gif"; }
    virtual bool supports (const std::string &feature) const {
        // Support nothing nonstandard
        return false;
    }
    virtual bool open (const std::string &name, const ImageSpec &spec,
                     OpenMode mode=Create);
    virtual bool close ();
    virtual bool write_scanline (int y, int z, TypeDesc format,
                                 const void *data, stride_t xstride);

private:
    GifFileType * GifFile;
    std::string m_filename;           ///< Stash the filename
    FILE *m_file;                     ///< Open image handle
    int m_color_type;                 ///< PNG color model type
    ColorMapObject *ColorMap;
    std::vector<unsigned char> m_scratch;
    
    // Initialize private members to pre-opened state
    void init (void) {
        m_file = NULL;
        GifFile= NULL;
        ColorMap = NULL;
        }

    // Add a parameter to the output
    
};




// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

DLLEXPORT ImageOutput *gif_output_imageio_create () { return new GIFOutput; }

// DLLEXPORT int gif_imageio_version = OIIO_PLUGIN_VERSION;   // it's in gifinput.cpp

DLLEXPORT const char * gif_output_extensions[] = {
    "gif", NULL
};

GIFOutput::GIFOutput ()
{
    init ();
}



GIFOutput::~GIFOutput ()
{
    // Close, if not already done.
    close ();
}

bool
GIFOutput::open (const std::string &name, const ImageSpec &userspec,
                 OpenMode mode)
{
    printf("open1");
    if (mode != Create) {
        error ("%s does not support subimages or MIP levels", format_name());
        return false;
    }

    close ();  // Close any already-opened file
    m_filename = name;
    m_spec = userspec;  // Stash the spec
    GifFile = EGifOpenFileName(name.c_str(),1);
    //EGifSetGifVersion(const char *Version);
    if(EGifPutScreenDesc(GifFile,m_spec.full_width, m_spec.full_height, 0,0,NULL)!=1)
    {
      PrintGifError();
      return false;
    }
    printf("open 2");
    ColorMap->ColorCount = m_spec.nchannels;
    ColorMap->BitsPerPixel = (int)m_spec.channel_bytes ();
    /*ColorMap->Colors[0] = (char)m_spec.channelnames[0];
    ColorMap->Colors[1] = (char)m_spec.channelnames[1];
    ColorMap->Colors[2] = (char)m_spec.channelnames[2];
    */if(EGifPutImageDesc(GifFile, m_spec.x, m_spec.y,
                     m_spec.width, m_spec.height, 0,
                     ColorMap)!=1)
    {
      PrintGifError();
      return false;
    }
    printf("open 3"); 
    return true;                  
}

bool
GIFOutput::write_scanline (int y, int z, TypeDesc format,
                            const void *data, stride_t xstride)
{
    printf("write");
    y -= m_spec.y;
    m_spec.auto_stride (xstride, format, spec().nchannels);
    const void *origdata = data;
    data = to_native_scanline (format, data, xstride, m_scratch);
    if (data == origdata) {
        m_scratch.assign ((unsigned char *)data,
                          (unsigned char *)data+m_spec.scanline_bytes());
        data = &m_scratch[0];
    }
    if(EGifPutLine(GifFile, (GifPixelType *)data,z)!=1)
    {
     PrintGifError();
      return false;
    }
    printf("scan lines wrtn");
    return true;
}

bool
GIFOutput::close ()
{
    if(EGifCloseFile(GifFile)!=1)
    {
    PrintGifError();
    return false;
    }
    printf("close");
    return true;
}  
}
}  
