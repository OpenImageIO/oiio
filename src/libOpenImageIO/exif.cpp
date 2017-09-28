/*
  Copyright 2008 Larry Gritz and the other authors and contributors.
  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:
  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of the software's owners nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  (This is the Modified BSD License)
*/


#include <cctype>
#include <cstdio>
#include <iostream>
#include <vector>
#include <set>
#include <algorithm>

#include <boost/container/flat_map.hpp>

#include <OpenImageIO/fmath.h>
#include <OpenImageIO/strutil.h>

#include "exif.h"

#include <OpenImageIO/imageio.h>


OIIO_NAMESPACE_BEGIN

using namespace pvt;



int
pvt::tiff_data_size (const TIFFDirEntry &dir)
{
    // Sizes of TIFFDataType members
    static size_t sizes[] = { 0, 1, 1, 2, 4, 8, 1, 1, 2, 4, 8, 4, 8, 4 };
    const int num_data_sizes = sizeof(sizes) / sizeof(*sizes);
    int dir_index = (int)dir.tdir_type;
    if (dir_index < 0 || dir_index >= num_data_sizes) {
        // Inform caller about corrupted entry.
        return -1;
    }
    return sizes[dir_index] * dir.tdir_count;
}



TypeDesc
pvt::tiff_datatype_to_typedesc (int tifftype, int tiffcount)
{
    if (tiffcount == 1)
        tiffcount = 0;    // length 1 == not an array
    switch (tifftype) {
    case TIFF_NOTYPE :
        return TypeUnknown;
    case TIFF_BYTE :
        return TypeDesc(TypeDesc::UINT8, tiffcount);
    case TIFF_ASCII :
        return TypeString;
    case TIFF_SHORT :
        return TypeDesc(TypeDesc::UINT16, tiffcount);
    case TIFF_LONG :
        return TypeDesc(TypeDesc::UINT32, tiffcount);
    case TIFF_RATIONAL :
        return tiffcount <= 1 ? TypeRational : TypeUnknown;
    case TIFF_SBYTE :
        return TypeDesc(TypeDesc::INT8, tiffcount);
    case TIFF_UNDEFINED :
        return TypeDesc(TypeDesc::UINT8, tiffcount); // 8-bit untyped data
    case TIFF_SSHORT :
        return TypeDesc(TypeDesc::INT16, tiffcount);
    case TIFF_SLONG :
        return TypeDesc(TypeDesc::INT32, tiffcount);
    case TIFF_SRATIONAL :
        return tiffcount <= 1 ? TypeRational : TypeUnknown;
    case TIFF_FLOAT :
        return TypeDesc(TypeDesc::FLOAT, tiffcount);
    case TIFF_DOUBLE :
        return TypeDesc(TypeDesc::DOUBLE, tiffcount);
    case TIFF_IFD :
        return TypeUnknown;
    case TIFF_LONG8 :
        return TypeDesc(TypeDesc::UINT64, tiffcount);
    case TIFF_SLONG8 :
        return TypeDesc(TypeDesc::INT64, tiffcount);
    case TIFF_IFD8 :
        return TypeUnknown;
    }
    ASSERT (0 && "Unknown TIFF type");
    return TypeUnknown;
}



namespace {

void
version4char_handler (const TagInfo& taginfo, const TIFFDirEntry& dir,
                     string_view buf, ImageSpec& spec)
{
    if (tiff_data_size(dir) == 4)   // sanity check
        spec.attribute (taginfo.name,
                        string_view((const char*)dataptr(dir,buf), 4));
}


void
version4uint8_handler (const TagInfo& taginfo, const TIFFDirEntry& dir,
                       string_view buf, ImageSpec& spec)
{
    if (tiff_data_size(dir) == 4)  // sanity check
        spec.attribute (taginfo.name, TypeDesc(TypeDesc::UINT8,4),
                        dataptr(dir,buf));
}


void
makernote_handler (const TagInfo& taginfo, const TIFFDirEntry& dir,
                   string_view buf, ImageSpec& spec)
{
    // Maker notes are tricky. We'd like to process the maker note here and
    // now, but we may not yet have come to the metadata that tells us what
    // kind of camera is it, and thus how to interpret the maker note blob
    // which is a different layout for each camera brand. So we punt by
    // shoving the makernote offset into the metadata, and then at the very
    // end of decode_exif(), we will extract it and parse the maker note.
    if (tiff_data_size(dir) > 4)   // sanity check
        spec.attribute ("oiio:MakerNoteOffset", int(dir.tdir_offset));
}



// Define EXIFTAG constants that don't seem to be in tiff.h
#define EXIFTAG_PHOTOGRAPHICSENSITIVITY     34855
#define EXIFTAG_SENSITIVITYTYPE             34864
#define EXIFTAG_STANDARDOUTPUTSENSITIVITY   34865
#define EXIFTAG_RECOMMENDEDEXPOSUREINDEX    34866
#define EXIFTAG_ISOSPEED                    34867
#define EXIFTAG_ISOSPEEDLATITUDEYYY         34868
#define EXIFTAG_ISOSPEEDLATITUDEZZZ         34869
#define EXIFTAG_OFFSETTIME                  36880
#define EXIFTAG_OFFSETTIMEORIGINAL          36881
#define EXIFTAG_OFFSETTIMEDIGITIZED         36882
#define EXIFTAG_TEMPERATURE                 37888
#define EXIFTAG_HUMIDITY                    37889
#define EXIFTAG_PRESSURE                    37890
#define EXIFTAG_WATERDEPTH                  37891
#define EXIFTAG_ACCELERATION                37892
#define EXIFTAG_CAMERAELEVATIONANGLE        37893
#define EXIFTAG_CAMERAOWNERNAME             42032
#define EXIFTAG_BODYSERIALNUMBER            42033
#define EXIFTAG_LENSSPECIFICATION           42034
#define EXIFTAG_LENSMAKE                    42035
#define EXIFTAG_LENSMODEL                   42036
#define EXIFTAG_LENSSERIALNUMBER            42037
#define EXIFTAG_GAMMA                       42240



static const TagInfo exif_tag_table[] = {
    // Skip ones handled by the usual JPEG code
    { TIFFTAG_IMAGEWIDTH,	"Exif:ImageWidth",	TIFF_NOTYPE, 1 },
    { TIFFTAG_IMAGELENGTH,	"Exif:ImageLength",	TIFF_NOTYPE, 1 },
    { TIFFTAG_BITSPERSAMPLE,	"Exif:BitsPerSample",	TIFF_NOTYPE, 1 },
    { TIFFTAG_COMPRESSION,	"Exif:Compression",	TIFF_NOTYPE, 1 },
    { TIFFTAG_PHOTOMETRIC,	"Exif:Photometric",	TIFF_NOTYPE, 1 },
    { TIFFTAG_SAMPLESPERPIXEL,	"Exif:SamplesPerPixel",	TIFF_NOTYPE, 1 },
    { TIFFTAG_PLANARCONFIG,	"Exif:PlanarConfig",	TIFF_NOTYPE, 1 },
    { TIFFTAG_YCBCRSUBSAMPLING,	"Exif:YCbCrSubsampling",TIFF_SHORT, 1 },
    { TIFFTAG_YCBCRPOSITIONING,	"Exif:YCbCrPositioning",TIFF_SHORT, 1 },
    // TIFF tags we may come across
    { TIFFTAG_ORIENTATION,	"Orientation",	TIFF_SHORT, 1 },
    { TIFFTAG_XRESOLUTION,	"XResolution",	TIFF_RATIONAL, 1 },
    { TIFFTAG_YRESOLUTION,	"YResolution",	TIFF_RATIONAL, 1 },
    { TIFFTAG_RESOLUTIONUNIT,	"ResolutionUnit",TIFF_SHORT, 1 },
    { TIFFTAG_IMAGEDESCRIPTION,	"ImageDescription",	TIFF_ASCII, 0 },
    { TIFFTAG_MAKE,	        "Make",	        TIFF_ASCII, 0 },
    { TIFFTAG_MODEL,	        "Model",	TIFF_ASCII, 0 },
    { TIFFTAG_SOFTWARE,	        "Software",	TIFF_ASCII, 0 },
    { TIFFTAG_ARTIST,	        "Artist",	TIFF_ASCII, 0 },
    { TIFFTAG_COPYRIGHT,	"Copyright",	TIFF_ASCII, 0 },
    { TIFFTAG_DATETIME,	        "DateTime",	TIFF_ASCII, 0 },
    { TIFFTAG_EXIFIFD,          "Exif:ExifIFD", TIFF_NOTYPE, 1 },
    { TIFFTAG_INTEROPERABILITYIFD, "Exif:InteroperabilityIFD", TIFF_NOTYPE, 1 },
    { TIFFTAG_GPSIFD,           "Exif:GPSIFD",  TIFF_NOTYPE, 1 },

    // EXIF tags we may come across
    { EXIFTAG_EXPOSURETIME,	"ExposureTime",	TIFF_RATIONAL, 1 },
    { EXIFTAG_FNUMBER,	        "FNumber",	TIFF_RATIONAL, 1 },
    { EXIFTAG_EXPOSUREPROGRAM,	"Exif:ExposureProgram",	TIFF_SHORT, 1 }, // ?? translate to ascii names?
    { EXIFTAG_SPECTRALSENSITIVITY,"Exif:SpectralSensitivity",	TIFF_ASCII, 0 },
    { EXIFTAG_ISOSPEEDRATINGS,	"Exif:ISOSpeedRatings",	TIFF_SHORT, 1 },
    { EXIFTAG_OECF,	        "Exif:OECF",	TIFF_NOTYPE, 1 },	 // skip it
    { EXIFTAG_EXIFVERSION,	"Exif:ExifVersion",	TIFF_UNDEFINED, 1, version4char_handler },	 // skip it
    { EXIFTAG_DATETIMEORIGINAL,	"Exif:DateTimeOriginal",	TIFF_ASCII, 0 },
    { EXIFTAG_DATETIMEDIGITIZED,"Exif:DateTimeDigitized",   TIFF_ASCII, 0 },
    { EXIFTAG_OFFSETTIME,"Exif:OffsetTime",   TIFF_ASCII, 0 },
    { EXIFTAG_OFFSETTIMEORIGINAL,"Exif:OffsetTimeOriginal",   TIFF_ASCII, 0 },
    { EXIFTAG_OFFSETTIMEDIGITIZED,"Exif:OffsetTimeDigitized",	TIFF_ASCII, 0 },
    { EXIFTAG_COMPONENTSCONFIGURATION, "Exif:ComponentsConfiguration",	TIFF_UNDEFINED, 1 },
    { EXIFTAG_COMPRESSEDBITSPERPIXEL,  "Exif:CompressedBitsPerPixel",	TIFF_RATIONAL, 1 },
    { EXIFTAG_SHUTTERSPEEDVALUE,"Exif:ShutterSpeedValue",	TIFF_SRATIONAL, 1 }, // APEX units
    { EXIFTAG_APERTUREVALUE,	"Exif:ApertureValue",	TIFF_RATIONAL, 1 },	// APEX units
    { EXIFTAG_BRIGHTNESSVALUE,	"Exif:BrightnessValue",	TIFF_SRATIONAL, 1 },
    { EXIFTAG_EXPOSUREBIASVALUE,"Exif:ExposureBiasValue",	TIFF_SRATIONAL, 1 },
    { EXIFTAG_MAXAPERTUREVALUE,	"Exif:MaxApertureValue",TIFF_RATIONAL, 1 },
    { EXIFTAG_SUBJECTDISTANCE,	"Exif:SubjectDistance",	TIFF_RATIONAL, 1 },
    { EXIFTAG_METERINGMODE,	"Exif:MeteringMode",	TIFF_SHORT, 1 },
    { EXIFTAG_LIGHTSOURCE,	"Exif:LightSource",	TIFF_SHORT, 1 },
    { EXIFTAG_FLASH,	        "Exif:Flash",	        TIFF_SHORT, 1 },
    { EXIFTAG_FOCALLENGTH,	"Exif:FocalLength",	TIFF_RATIONAL, 1 }, // mm
    { EXIFTAG_SECURITYCLASSIFICATION, "Exif:SecurityClassification", TIFF_ASCII, 1 },
    { EXIFTAG_IMAGEHISTORY,     "Exif:ImageHistory",    TIFF_ASCII, 1 },
    { EXIFTAG_SUBJECTAREA,	"Exif:SubjectArea",	TIFF_NOTYPE, 1 }, // FIXME
    { EXIFTAG_MAKERNOTE,	"Exif:MakerNote",	TIFF_BYTE, 0, makernote_handler },
    { EXIFTAG_USERCOMMENT,	"Exif:UserComment",	TIFF_BYTE, 0 },
    { EXIFTAG_SUBSECTIME,	"Exif:SubsecTime",	        TIFF_ASCII, 0 },
    { EXIFTAG_SUBSECTIMEORIGINAL,"Exif:SubsecTimeOriginal",	TIFF_ASCII, 0 },
    { EXIFTAG_SUBSECTIMEDIGITIZED,"Exif:SubsecTimeDigitized",	TIFF_ASCII, 0 },
    { EXIFTAG_FLASHPIXVERSION,	"Exif:FlashPixVersion",	TIFF_UNDEFINED, 1, version4char_handler },	// skip "Exif:FlashPixVesion",	TIFF_NOTYPE, 1 },
    { EXIFTAG_COLORSPACE,	"Exif:ColorSpace",	TIFF_SHORT, 1 },
    { EXIFTAG_PIXELXDIMENSION,	"Exif:PixelXDimension",	TIFF_LONG, 1 },
    { EXIFTAG_PIXELYDIMENSION,	"Exif:PixelYDimension",	TIFF_LONG, 1 },
    { EXIFTAG_RELATEDSOUNDFILE,	"Exif:RelatedSoundFile", TIFF_ASCII, 0 },
    { EXIFTAG_FLASHENERGY,	"Exif:FlashEnergy",	TIFF_RATIONAL, 1 },
    { EXIFTAG_SPATIALFREQUENCYRESPONSE,	"Exif:SpatialFrequencyResponse",	TIFF_NOTYPE, 1 },
    { EXIFTAG_FOCALPLANEXRESOLUTION,	"Exif:FocalPlaneXResolution",	TIFF_RATIONAL, 1 },
    { EXIFTAG_FOCALPLANEYRESOLUTION,	"Exif:FocalPlaneYResolution",	TIFF_RATIONAL, 1 },
    { EXIFTAG_FOCALPLANERESOLUTIONUNIT,	"Exif:FocalPlaneResolutionUnit",	TIFF_SHORT, 1 }, // Symbolic?
    { EXIFTAG_SUBJECTLOCATION,	"Exif:SubjectLocation",	TIFF_SHORT, 2 },
    { EXIFTAG_EXPOSUREINDEX,	"Exif:ExposureIndex",	TIFF_RATIONAL, 1 },
    { EXIFTAG_SENSINGMETHOD,	"Exif:SensingMethod",	TIFF_SHORT, 1 },
    { EXIFTAG_FILESOURCE,	"Exif:FileSource",	TIFF_UNDEFINED, 1 },
    { EXIFTAG_SCENETYPE,	"Exif:SceneType",	TIFF_UNDEFINED, 1 },
    { EXIFTAG_CFAPATTERN,	"Exif:CFAPattern",	TIFF_NOTYPE, 1 }, // FIXME
    { EXIFTAG_CUSTOMRENDERED,	"Exif:CustomRendered",	TIFF_SHORT, 1 },
    { EXIFTAG_EXPOSUREMODE,	"Exif:ExposureMode",	TIFF_SHORT, 1 },
    { EXIFTAG_WHITEBALANCE,	"Exif:WhiteBalance",	TIFF_SHORT, 1 },
    { EXIFTAG_DIGITALZOOMRATIO,	"Exif:DigitalZoomRatio", TIFF_RATIONAL, 1 },
    { EXIFTAG_FOCALLENGTHIN35MMFILM, "Exif:FocalLengthIn35mmFilm",	TIFF_SHORT, 1 },
    { EXIFTAG_SCENECAPTURETYPE,	"Exif:SceneCaptureType", TIFF_SHORT, 1 },
    { EXIFTAG_GAINCONTROL,	"Exif:GainControl",	TIFF_RATIONAL, 1 },
    { EXIFTAG_CONTRAST,	        "Exif:Contrast",	TIFF_SHORT, 1 },
    { EXIFTAG_SATURATION,	"Exif:Saturation",	TIFF_SHORT, 1 },
    { EXIFTAG_SHARPNESS,	"Exif:Sharpness",	TIFF_SHORT, 1 },
    { EXIFTAG_DEVICESETTINGDESCRIPTION,	"Exif:DeviceSettingDescription",	TIFF_NOTYPE, 1 }, // FIXME
    { EXIFTAG_SUBJECTDISTANCERANGE,	"Exif:SubjectDistanceRange",	TIFF_SHORT, 1 },
    { EXIFTAG_IMAGEUNIQUEID,	"Exif:ImageUniqueID",   TIFF_ASCII, 0 },
    { EXIFTAG_PHOTOGRAPHICSENSITIVITY,  "Exif:PhotographicSensitivity",  TIFF_SHORT, 1 },
    { EXIFTAG_SENSITIVITYTYPE,  "Exif:SensitivityType",  TIFF_SHORT, 1 },
    { EXIFTAG_STANDARDOUTPUTSENSITIVITY,  "Exif:StandardOutputSensitivity", TIFF_LONG, 1 },
    { EXIFTAG_RECOMMENDEDEXPOSUREINDEX,  "Exif:RecommendedExposureIndex", TIFF_LONG, 1 },
    { EXIFTAG_ISOSPEED,  "Exif:ISOSpeed", TIFF_LONG, 1 },
    { EXIFTAG_ISOSPEEDLATITUDEYYY,  "Exif:ISOSpeedLatitudeyyy", TIFF_LONG, 1 },
    { EXIFTAG_ISOSPEEDLATITUDEZZZ,  "Exif:ISOSpeedLatitudezzz", TIFF_LONG, 1 },
    { EXIFTAG_TEMPERATURE,  "Exif:Temperature", TIFF_SRATIONAL, 1 },
    { EXIFTAG_HUMIDITY,  "Exif:Humidity", TIFF_RATIONAL, 1 },
    { EXIFTAG_PRESSURE,  "Exif:Pressure", TIFF_RATIONAL, 1 },
    { EXIFTAG_WATERDEPTH,  "Exif:WaterDepth", TIFF_SRATIONAL, 1 },
    { EXIFTAG_ACCELERATION,  "Exif:Acceleration", TIFF_RATIONAL, 1 },
    { EXIFTAG_CAMERAELEVATIONANGLE,  "Exif:CameraElevationAngle", TIFF_SRATIONAL, 1 },
    { EXIFTAG_CAMERAOWNERNAME,  "Exif:CameraOwnerName",  TIFF_ASCII, 0 },
    { EXIFTAG_BODYSERIALNUMBER,  "Exif:BodySerialNumber", TIFF_ASCII, 0 },
    { EXIFTAG_LENSSPECIFICATION,  "Exif:LensSpecification", TIFF_RATIONAL, 4 },
    { EXIFTAG_LENSMAKE,  "Exif:LensMake",         TIFF_ASCII, 0 },
    { EXIFTAG_LENSMODEL,  "Exif:LensModel",        TIFF_ASCII, 0 },
    { EXIFTAG_LENSSERIALNUMBER,  "Exif:LensSerialNumber", TIFF_ASCII, 0 },
    { EXIFTAG_GAMMA,  "Exif:Gamma", TIFF_RATIONAL, 0 }
};

static TagMap& exif_tagmap_ref () {
    static TagMap T ("EXIF", exif_tag_table);
    return T;
}



enum GPSTag {
    GPSTAG_VERSIONID = 0, 
    GPSTAG_LATITUDEREF = 1,  GPSTAG_LATITUDE = 2,
    GPSTAG_LONGITUDEREF = 3, GPSTAG_LONGITUDE = 4, 
    GPSTAG_ALTITUDEREF = 5,  GPSTAG_ALTITUDE = 6,
    GPSTAG_TIMESTAMP = 7,
    GPSTAG_SATELLITES = 8,
    GPSTAG_STATUS = 9,
    GPSTAG_MEASUREMODE = 10,
    GPSTAG_DOP = 11,
    GPSTAG_SPEEDREF = 12, GPSTAG_SPEED = 13,
    GPSTAG_TRACKREF = 14, GPSTAG_TRACK = 15,
    GPSTAG_IMGDIRECTIONREF = 16,  GPSTAG_IMGDIRECTION = 17,
    GPSTAG_MAPDATUM = 18,
    GPSTAG_DESTLATITUDEREF = 19,  GPSTAG_DESTLATITUDE = 20,
    GPSTAG_DESTLONGITUDEREF = 21, GPSTAG_DESTLONGITUDE = 22, 
    GPSTAG_DESTBEARINGREF = 23,   GPSTAG_DESTBEARING = 24,
    GPSTAG_DESTDISTANCEREF = 25,  GPSTAG_DESTDISTANCE = 26,
    GPSTAG_PROCESSINGMETHOD = 27,
    GPSTAG_AREAINFORMATION = 28,
    GPSTAG_DATESTAMP = 29,
    GPSTAG_DIFFERENTIAL = 30,
    GPSTAG_HPOSITIONINGERROR = 31
};

static const TagInfo gps_tag_table[] = {
    { GPSTAG_VERSIONID,		"GPS:VersionID",	TIFF_BYTE, 4, version4uint8_handler }, 
    { GPSTAG_LATITUDEREF,	"GPS:LatitudeRef",	TIFF_ASCII, 2 },
    { GPSTAG_LATITUDE,		"GPS:Latitude",		TIFF_RATIONAL, 3 },
    { GPSTAG_LONGITUDEREF,	"GPS:LongitudeRef",	TIFF_ASCII, 2 },
    { GPSTAG_LONGITUDE,		"GPS:Longitude",	TIFF_RATIONAL, 3 }, 
    { GPSTAG_ALTITUDEREF,	"GPS:AltitudeRef",	TIFF_BYTE, 1 },
    { GPSTAG_ALTITUDE,		"GPS:Altitude",		TIFF_RATIONAL, 1 },
    { GPSTAG_TIMESTAMP,		"GPS:TimeStamp",	TIFF_RATIONAL, 3 },
    { GPSTAG_SATELLITES,	"GPS:Satellites",	TIFF_ASCII, 0 },
    { GPSTAG_STATUS,		"GPS:Status",		TIFF_ASCII, 2 },
    { GPSTAG_MEASUREMODE,	"GPS:MeasureMode",	TIFF_ASCII, 2 },
    { GPSTAG_DOP,		"GPS:DOP",		TIFF_RATIONAL, 1 },
    { GPSTAG_SPEEDREF,		"GPS:SpeedRef",		TIFF_ASCII, 2 },
    { GPSTAG_SPEED,		"GPS:Speed",		TIFF_RATIONAL, 1 },
    { GPSTAG_TRACKREF,		"GPS:TrackRef",		TIFF_ASCII, 2 },
    { GPSTAG_TRACK,		"GPS:Track",		TIFF_RATIONAL, 1 },
    { GPSTAG_IMGDIRECTIONREF,	"GPS:ImgDirectionRef",	TIFF_ASCII, 2 },
    { GPSTAG_IMGDIRECTION,	"GPS:ImgDirection",	TIFF_RATIONAL, 1 },
    { GPSTAG_MAPDATUM,		"GPS:MapDatum",		TIFF_ASCII, 0 },
    { GPSTAG_DESTLATITUDEREF,	"GPS:DestLatitudeRef",	TIFF_ASCII, 2 },
    { GPSTAG_DESTLATITUDE,	"GPS:DestLatitude",	TIFF_RATIONAL, 3 },
    { GPSTAG_DESTLONGITUDEREF,	"GPS:DestLongitudeRef",	TIFF_ASCII, 2 },
    { GPSTAG_DESTLONGITUDE,	"GPS:DestLongitude",	TIFF_RATIONAL, 3 }, 
    { GPSTAG_DESTBEARINGREF,	"GPS:DestBearingRef",	TIFF_ASCII, 2 },
    { GPSTAG_DESTBEARING,	"GPS:DestBearing",	TIFF_RATIONAL, 1 },
    { GPSTAG_DESTDISTANCEREF,	"GPS:DestDistanceRef",	TIFF_ASCII, 2 },
    { GPSTAG_DESTDISTANCE,	"GPS:DestDistance",	TIFF_RATIONAL, 1 },
    { GPSTAG_PROCESSINGMETHOD,	"GPS:ProcessingMethod",	TIFF_UNDEFINED, 1 },
    { GPSTAG_AREAINFORMATION,	"GPS:AreaInformation",	TIFF_UNDEFINED, 1 },
    { GPSTAG_DATESTAMP,		"GPS:DateStamp",	TIFF_ASCII, 0 },
    { GPSTAG_DIFFERENTIAL,	"GPS:Differential",	TIFF_SHORT, 1 },
    { GPSTAG_HPOSITIONINGERROR,	"GPS:HPositioningError",TIFF_RATIONAL, 1 }
};


static TagMap& gps_tagmap_ref () {
    static TagMap T ("GPS", gps_tag_table);
    return T;
}




#if (DEBUG_EXIF_WRITE || DEBUG_EXIF_READ)
static bool
print_dir_entry (const TagMap &tagmap,
                 const TIFFDirEntry &dir, string_view buf)
{
    int len = tiff_data_size (dir);
    if (len < 0) {
        std::cerr << "Ignoring bad directory entry\n";
        return false;
    }
    const char *mydata = NULL;
    if (len <= 4) {  // short data is stored in the offset field
        mydata = (const char *)&dir.tdir_offset;
    } else {
        if (dir.tdir_offset >= buf.size() ||
           (dir.tdir_offset+tiff_data_size(dir)) >= buf.size())
            return false;    // bogus! overruns the buffer
        mydata = buf.data() + dir.tdir_offset;
    }
    const char *name = tagmap.name(dir.tdir_tag);
    std::cerr << "tag=" << dir.tdir_tag
              << " (" << (name ? name : "unknown") << ")"
              << ", type=" << dir.tdir_type
              << ", count=" << dir.tdir_count
              << ", offset=" << dir.tdir_offset << " = " ;
    switch (dir.tdir_type) {
    case TIFF_ASCII :
        std::cerr << "'" << (char *)mydata << "'";
        break;
    case TIFF_RATIONAL :
        {
            const unsigned int *u = (unsigned int *)mydata;
            for (size_t i = 0; i < dir.tdir_count;  ++i)
                std::cerr << u[2*i] << "/" << u[2*i+1] << " = "
                          << (double)u[2*i]/(double)u[2*i+1] << " ";
        }
        break;
    case TIFF_SRATIONAL :
        {
            const int *u = (int *)mydata;
            for (size_t i = 0; i < dir.tdir_count;  ++i)
                std::cerr << u[2*i] << "/" << u[2*i+1] << " = "
                          << (double)u[2*i]/(double)u[2*i+1] << " ";
        }
        break;
    case TIFF_SHORT :
        std::cerr << ((unsigned short *)mydata)[0];
        break;
    case TIFF_LONG :
        std::cerr << ((unsigned int *)mydata)[0];
        break;
    case TIFF_BYTE :
    case TIFF_UNDEFINED :
    case TIFF_NOTYPE :
    default:
        if (len <= 4 && dir.tdir_count > 4) {
            // Request more data than is stored.
            std::cerr << "Ignoring buffer with too much count of short data.\n";
            return false;
        }
        for (size_t i = 0;  i < dir.tdir_count;  ++i)
            std::cerr << (int)((unsigned char *)mydata)[i] << ' ';
        break;
    }
    std::cerr << "\n";
    return true;
}
#endif



/// Add one EXIF directory entry's data to spec under the given 'name'.
/// The directory entry is in *dirp, buf points to the beginning of the
/// TIFF "file", i.e. all TIFF tag offsets are relative to buf.  If swab
/// is true, the endianness of the file doesn't match the endianness of
/// the host CPU, therefore all integer and float data embedded in buf
/// needs to be byte-swapped.  Note that *dirp HAS already been swapped,
/// if necessary, so no byte swapping on *dirp is necessary.
static void
add_exif_item_to_spec (ImageSpec &spec, const char *name,
                       const TIFFDirEntry *dirp, string_view buf, bool swab)
{
    if (dirp->tdir_type == TIFF_SHORT && dirp->tdir_count == 1) {
        union { uint32_t i32; uint16_t i16[2]; } convert;
        convert.i32 = dirp->tdir_offset;
        unsigned short d = convert.i16[0];
        // N.B. The Exif spec says that for a 16 bit value, it's stored in
        // the *first* 16 bits of the offset area.
        if (swab)
            swap_endian (&d);
        spec.attribute (name, (unsigned int)d);
        return;
    }
    if (dirp->tdir_type == TIFF_LONG && dirp->tdir_count == 1) {
        unsigned int d;
        d = * (const unsigned int *) &dirp->tdir_offset;  // int stored in offset itself
        if (swab)
            swap_endian (&d);
        spec.attribute (name, (unsigned int)d);
        return;
    }
    if (dirp->tdir_type == TIFF_RATIONAL) {
        int n = dirp->tdir_count;  // How many
        float *f = (float *) alloca (n * sizeof(float));
        for (int i = 0;  i < n;  ++i) {
            unsigned int num, den;
            num = ((const unsigned int *) &(buf[dirp->tdir_offset]))[2*i+0];
            den = ((const unsigned int *) &(buf[dirp->tdir_offset]))[2*i+1];
            if (swab) {
                swap_endian (&num);
                swap_endian (&den);
            }
            f[i] = (float) ((double)num / (double)den);
        }
        if (dirp->tdir_count == 1)
            spec.attribute (name, *f);
        else
            spec.attribute (name, TypeDesc(TypeDesc::FLOAT, n), f);
        return;
    }
    if (dirp->tdir_type == TIFF_SRATIONAL) {
        int n = dirp->tdir_count;  // How many
        float *f = (float *) alloca (n * sizeof(float));
        for (int i = 0;  i < n;  ++i) {
            int num, den;
            num = ((const int *) &(buf[dirp->tdir_offset]))[2*i+0];
            den = ((const int *) &(buf[dirp->tdir_offset]))[2*i+1];
            if (swab) {
                swap_endian (&num);
                swap_endian (&den);
            }
            f[i] = (float) ((double)num / (double)den);
        }
        if (dirp->tdir_count == 1)
            spec.attribute (name, *f);
        else
            spec.attribute (name, TypeDesc(TypeDesc::FLOAT, n), f);
        return;
    }
    if (dirp->tdir_type == TIFF_ASCII) {
        int len = tiff_data_size (*dirp);
        const char *ptr = (len <= 4) ? (const char *)&dirp->tdir_offset 
                                     : (buf.data() + dirp->tdir_offset);
        while (len && ptr[len-1] == 0)  // Don't grab the terminating null
            --len;
        std::string str (ptr, len);
        if (strlen(str.c_str()) < str.length())  // Stray \0 in the middle
            str = std::string (str.c_str());
        spec.attribute (name, str);
        return;
    }
    if (dirp->tdir_type == TIFF_BYTE && dirp->tdir_count == 1) {
        // Not sure how to handle "bytes" generally, but certainly for just
        // one, add it as an int.
        unsigned char d;
        d = * (const unsigned char *) &dirp->tdir_offset;  // byte stored in offset itself
        spec.attribute (name, (int)d);
        return;
    }

#if 0
    if (dirp->tdir_type == TIFF_UNDEFINED || dirp->tdir_type == TIFF_BYTE) {
        // Add it as bytes
        const void *addr = dirp->tdir_count <= 4 ? (const void *) &dirp->tdir_offset 
                                                 : (const void *) &buf[dirp->tdir_offset];
        spec.attribute (name, TypeDesc(TypeDesc::UINT8, dirp->tdir_count), addr);
    }
#endif

#if !defined(NDEBUG) || DEBUG_EXIF_UNHANDLED
    std::cerr << "add_exif_item_to_spec: didn't know how to process " << name << ", type "
              << dirp->tdir_type << " x " << dirp->tdir_count << "\n";
#endif
}



/// Process a single TIFF directory entry embedded in the JPEG 'APP1'
/// data.  The directory entry is in *dirp, buf points to the beginning
/// of the TIFF "file", i.e. all TIFF tag offsets are relative to buf.
/// The goal is to decode the tag and put the data into appropriate
/// attribute slots of spec.  If swab is true, the endianness of the
/// file doesn't match the endianness of the host CPU, therefore all
/// integer and float data embedded in buf needs to be byte-swapped.
/// Note that *dirp has not been swapped, and so is still in the native
/// endianness of the file.
static void
read_exif_tag (ImageSpec &spec, const TIFFDirEntry *dirp,
               string_view buf, bool swab,
               std::set<size_t> &ifd_offsets_seen,
               const TagMap &tagmap)
{
    if ((char*)dirp < buf.data() || (char*)dirp >= buf.data() + buf.size()) {
#if DEBUG_EXIF_READ
        std::cerr << "Ignoring directory outside of the buffer.\n";
#endif
        return;
    }

    TagMap& exif_tagmap (exif_tagmap_ref());
    TagMap& gps_tagmap (gps_tagmap_ref());

    // Make a copy of the pointed-to TIFF directory, swab the components
    // if necessary.
    TIFFDirEntry dir = *dirp;
    if (swab) {
        swap_endian (&dir.tdir_tag);
        swap_endian (&dir.tdir_type);
        swap_endian (&dir.tdir_count);
        // only swab true offsets, not data embedded in the offset field
        if (tiff_data_size (dir) > 4)
            swap_endian (&dir.tdir_offset);
    }

#if DEBUG_EXIF_READ
    std::cerr << "Read " << tagmap.mapname() << " ";
    print_dir_entry (tagmap, dir, buf);
#endif

    if (dir.tdir_tag == TIFFTAG_EXIFIFD || dir.tdir_tag == TIFFTAG_GPSIFD) {
        // Special case: It's a pointer to a private EXIF directory.
        // Handle the whole thing recursively.
        unsigned int offset = dirp->tdir_offset;  // int stored in offset itself
        if (swab)
            swap_endian (&offset);
        if (offset >= buf.size()) {
#if DEBUG_EXIF_READ
            unsigned int off2 = offset;
            swap_endian (&off2);
            std::cerr << "Bad Exif block? ExifIFD has offset " << offset
                      << " inexplicably greater than exif buffer length "
                      << buf.size() << " (byte swapped = " << off2 << ")\n";
#endif
            return;
        }
        // Don't recurse if we've already visited this IFD
        if (ifd_offsets_seen.find (offset) != ifd_offsets_seen.end()) {
#if DEBUG_EXIF_READ
            std::cerr << "Early ifd exit\n";
#endif
            return;
        }
        ifd_offsets_seen.insert (offset);
#if DEBUG_EXIF_READ
        std::cerr << "Now we've seen offset " << offset << "\n";
#endif
        const unsigned char *ifd = ((const unsigned char *)buf.data() + offset);
        unsigned short ndirs = *(const unsigned short *)ifd;
        if (swab)
            swap_endian (&ndirs);
        if (dir.tdir_tag == TIFFTAG_GPSIFD && ndirs > 32) {
            // We have encountered JPEG files that inexplicably have the
            // directory count for the GPS data using the wrong byte order.
            // In this case, since there are only 32 possible GPS related
            // tags, we use that as a sanity check and skip the corrupted
            // data block. This isn't a very general solution, but it's a
            // rare case and clearly a broken file. We're just trying not to
            // crash in this case.
            return;
        }

#if DEBUG_EXIF_READ
        std::cerr << "exifid has type " << dir.tdir_type << ", offset " << dir.tdir_offset << "\n";
        std::cerr << "EXIF Number of directory entries = " << ndirs << "\n";
#endif
        for (int d = 0;  d < ndirs;  ++d)
            read_exif_tag (spec, (const TIFFDirEntry *)(ifd+2+d*sizeof(TIFFDirEntry)),
                           buf, swab, ifd_offsets_seen,
                           dir.tdir_tag == TIFFTAG_EXIFIFD ? exif_tagmap : gps_tagmap);
#if DEBUG_EXIF_READ
        std::cerr << "> End EXIF\n";
#endif
    } else if (dir.tdir_tag == TIFFTAG_INTEROPERABILITYIFD) {
        // Special case: It's a pointer to a private EXIF directory.
        // Handle the whole thing recursively.
        unsigned int offset = dirp->tdir_offset;  // int stored in offset itself
        if (swab)
            swap_endian (&offset);
        // Don't recurse if we've already visited this IFD
        if (ifd_offsets_seen.find (offset) != ifd_offsets_seen.end())
            return;
        ifd_offsets_seen.insert (offset);
#if DEBUG_EXIF_READ
        std::cerr << "Now we've seen offset " << offset << "\n";
#endif
        const unsigned char *ifd = ((const unsigned char *)buf.data() + offset);
        unsigned short ndirs = *(const unsigned short *)ifd;
        if (swab)
            swap_endian (&ndirs);
#if DEBUG_EXIF_READ
        std::cerr << "\n\nInteroperability has type " << dir.tdir_type << ", offset " << dir.tdir_offset << "\n";
        std::cerr << "Interoperability Number of directory entries = " << ndirs << "\n";
#endif
        for (int d = 0;  d < ndirs;  ++d)
            read_exif_tag (spec, (const TIFFDirEntry *)(ifd+2+d*sizeof(TIFFDirEntry)),
                           buf, swab, ifd_offsets_seen, exif_tagmap);
#if DEBUG_EXIF_READ
        std::cerr << "> End Interoperability\n\n";
#endif
    } else {
        // Everything else -- use our table to handle the general case
        const TagInfo* taginfo = tagmap.find (dir.tdir_tag);
        if (taginfo) {
            if (taginfo->handler)
                taginfo->handler (*taginfo, dir, buf, spec);
            else
                add_exif_item_to_spec (spec, taginfo->name, &dir, buf, swab);
        } else {
#if DEBUG_EXIF_READ || DEBUG_EXIF_UNHANDLED
            Strutil::fprintf (stderr, "read_exif_tag: Unhandled %s tag=%d (0x%x), type=%d count=%d (%s), offset=%d\n",
                              tagmap.mapname(), dir.tdir_tag, dir.tdir_tag,
                              dir.tdir_type, dir.tdir_count,
                              tiff_datatype_to_typedesc(dir), dir.tdir_offset);
#endif
        }
    }
}



class tagcompare {
public:
    int operator() (const TIFFDirEntry &a, const TIFFDirEntry &b) {
        return (a.tdir_tag < b.tdir_tag);
    }
};




/// Convert to the desired integer type and then append_tiff_dir_entry it.
///
template <class T>
bool
append_tiff_dir_entry_integer (const ParamValue &p,
                               std::vector<TIFFDirEntry> &dirs,
                               std::vector<char> &data, int tag,
                               TIFFDataType type, size_t offset_correction)
{
    T i;
    switch (p.type().basetype) {
    case TypeDesc::UINT:
        i = (T) *(unsigned int *)p.data();
        break;
    case TypeDesc::INT:
        i = (T) *(int *)p.data();
        break;
    case TypeDesc::UINT16:
        i = (T) *(unsigned short *)p.data();
        break;
    case TypeDesc::INT16:
        i = (T) *(short *)p.data();
        break;
    default:
        return false;
    }
    append_tiff_dir_entry (dirs, data, tag, type, 1, &i, offset_correction);
    return true;
}



/// Helper: For param that needs to be added as a tag, create a TIFF
/// directory entry for it in dirs and add its data in data.  Set the
/// directory's offset just to the position within data where it will
/// reside.  Don't worry about it being relative to the start of some
/// TIFF structure.
static void
encode_exif_entry (const ParamValue &p, int tag,
                   std::vector<TIFFDirEntry> &dirs,
                   std::vector<char> &data, const TagMap &tagmap,
                   size_t offset_correction)
{
    if (tag < 0)
        return;
    TIFFDataType type = tagmap.tifftype (tag);
    size_t count = (size_t) tagmap.tiffcount (tag);
    TypeDesc element = p.type().elementtype();

    switch (type) {
    case TIFF_ASCII :
        if (p.type() == TypeDesc::STRING) {
            const char *s = *(const char **) p.data();
            int len = strlen(s) + 1;
            append_tiff_dir_entry (dirs, data, tag, type, len, s, offset_correction);
            return;
        }
        break;
    case TIFF_RATIONAL :
        if (element == TypeDesc::FLOAT) {
            unsigned int *rat = (unsigned int *) alloca (2*count*sizeof(unsigned int));
            const float *f = (const float *)p.data();
            for (size_t i = 0;  i < count;  ++i)
                float_to_rational (f[i], rat[2*i], rat[2*i+1]);
            append_tiff_dir_entry (dirs, data, tag, type, count, rat, offset_correction);
            return;
        }
        break;
    case TIFF_SRATIONAL :
        if (element == TypeDesc::FLOAT) {
            int *rat = (int *) alloca (2*count*sizeof(int));
            const float *f = (const float *)p.data();
            for (size_t i = 0;  i < count;  ++i)
                float_to_rational (f[i], rat[2*i], rat[2*i+1]);
            append_tiff_dir_entry (dirs, data, tag, type, count, rat, offset_correction);
            return;
        }
        break;
    case TIFF_SHORT :
        if (append_tiff_dir_entry_integer<unsigned short> (p, dirs, data, tag, type, offset_correction))
            return;
        break;
    case TIFF_LONG :
        if (append_tiff_dir_entry_integer<unsigned int> (p, dirs, data, tag, type, offset_correction))
            return;
        break;
    case TIFF_BYTE :
        if (append_tiff_dir_entry_integer<unsigned char> (p, dirs, data, tag, type, offset_correction))
            return;
        break;
    default:
        break;
    }
#if DEBUG_EXIF_WRITE || DEBUG_EXIF_UNHANDLED
    std::cerr << "encode_exif_entry: Don't know how to add " << p.name() << ", tag " << tag << ", type " << type << ' ' << p.type().c_str() << "\n";
#endif
}



// Decode a raw Exif data block and save all the metadata in an
// ImageSpec.  Return true if all is ok, false if the exif block was
// somehow malformed.
void
decode_ifd (const unsigned char *ifd,
            string_view buf, ImageSpec &spec, const TagMap& tag_map,
            std::set<size_t>& ifd_offsets_seen, bool swab)
{
    // Read the directory that the header pointed to.  It should contain
    // some number of directory entries containing tags to process.
    unsigned short ndirs = *(const unsigned short *)ifd;
    if (swab)
        swap_endian (&ndirs);
    for (int d = 0;  d < ndirs;  ++d)
        read_exif_tag (spec, (const TIFFDirEntry *) (ifd+2+d*sizeof(TIFFDirEntry)),
                       buf, swab, ifd_offsets_seen, tag_map);
}

}  // anon namespace



void
pvt::append_tiff_dir_entry (std::vector<TIFFDirEntry> &dirs,
                            std::vector<char> &data,
                            int tag, TIFFDataType type, size_t count,
                            const void *mydata, size_t offset_correction,
                            size_t offset_override)
{
    TIFFDirEntry dir;
    dir.tdir_tag = tag;
    dir.tdir_type = type;
    dir.tdir_count = count;
    size_t len = tiff_data_size (dir);
    if (len <= 4) {
        dir.tdir_offset = 0;
        memcpy (&dir.tdir_offset, mydata, len);
    } else {
        if (mydata) {
            // Add to the data vector and use its offset
            dir.tdir_offset = data.size() - offset_correction;
            data.insert (data.end(), (char *)mydata, (char *)mydata + len);
        } else {
            // An offset override was given, use that, it means that data
            // ALREADY contains what we want.
            dir.tdir_offset = uint32_t(offset_override);
        }
    }
    // Don't double-add
    for (TIFFDirEntry &d : dirs) {
        if (d.tdir_tag == tag) {
            d = dir;
            return;
        }
    }
    dirs.push_back (dir);
}




bool
decode_exif (string_view exif, ImageSpec &spec)
{
#if DEBUG_EXIF_READ
    std::cerr << "Exif dump:\n";
    for (size_t i = 0;  i < exif.size();  ++i) {
        if (exif[i] >= ' ')
            std::cerr << (char)exif[i] << ' ';
        std::cerr << "(" << (int)(unsigned char)exif[i] << ") ";
    }
    std::cerr << "\n";
#endif

    // The first item should be a standard TIFF header.  Note that HERE,
    // not the start of the Exif blob, is where all TIFF offsets are
    // relative to.  The header should have the right magic number (which
    // also tells us the endianness of the data) and an offset to the
    // first TIFF directory.
    //
    // N.B. Just read libtiff's "tiff.h" for info on the structure 
    // layout of TIFF headers and directory entries.  The TIFF spec
    // itself is also helpful in this area.
    TIFFHeader head = *(const TIFFHeader *)exif.data();
    if (head.tiff_magic != 0x4949 && head.tiff_magic != 0x4d4d)
        return false;
    bool host_little = littleendian();
    bool file_little = (head.tiff_magic == 0x4949);
    bool swab = (host_little != file_little);
    if (swab)
        swap_endian (&head.tiff_diroff);

    const unsigned char *ifd = ((const unsigned char *)exif.data() + head.tiff_diroff);
    // keep track of IFD offsets we've already seen to avoid infinite
    // recursion if there are circular references.
    std::set<size_t> ifd_offsets_seen;
    decode_ifd (ifd, exif, spec, exif_tagmap_ref(), ifd_offsets_seen, swab);

    // A few tidbits to look for
    ParamValue *p;
    if ((p = spec.find_attribute ("Exif:ColorSpace")) ||
        (p = spec.find_attribute ("ColorSpace"))) {
        int cs = -1;
        if (p->type() == TypeDesc::UINT) 
            cs = *(const unsigned int *)p->data();
        else if (p->type() == TypeDesc::INT) 
            cs = *(const int *)p->data();
        else if (p->type() == TypeDesc::UINT16) 
            cs = *(const unsigned short *)p->data();
        else if (p->type() == TypeDesc::INT16) 
            cs = *(const short *)p->data();
        // Exif spec says that anything other than 0xffff==uncalibrated
        // should be interpreted to be sRGB.
        if (cs != 0xffff)
            spec.attribute ("oiio:ColorSpace", "sRGB");
    }

    // Look for a maker note offset, now that we have seen all the metadata
    // and therefore are sure we know the camera Make. See the comments in
    // makernote_handler for why this needs to come at the end.
    int makernote_offset = spec.get_int_attribute ("oiio:MakerNoteOffset");
    if (makernote_offset > 0) {
        if (spec.get_string_attribute("Make") == "Canon") {
            decode_ifd ((unsigned char *)exif.data() + makernote_offset, exif,
                        spec, pvt::canon_maker_tagmap_ref(), ifd_offsets_seen, swab);
        }
        // Now we can erase the attrib we used to pass the message about
        // the maker note offset.
        spec.erase_attribute ("oiio:MakerNoteOffset");
    }

    return true;
}



// DEPRECATED (1.8)
bool
decode_exif (const void *exif, int length, ImageSpec &spec)
{
    return decode_exif (string_view ((const char *)exif, length), spec);
}



template<class T>
inline void append (std::vector<char>& blob, const T& v)
{
    blob.insert (blob.end(), (const char *)&v, (const char*)&v + sizeof(T));
}

template<class T>
inline void appendvec (std::vector<char>& blob, const std::vector<T>& v)
{
    blob.insert (blob.end(), (const char *)v.data(), (const char*)(v.data()+v.size()));
}



// Construct an Exif data block from the ImageSpec, appending the Exif 
// data as a big blob to the char vector.
void
encode_exif (const ImageSpec &spec, std::vector<char> &blob)
{
    const TagMap& exif_tagmap (exif_tagmap_ref());
    const TagMap& gps_tagmap (gps_tagmap_ref());
    // const TagMap& canon_tagmap (pvt::canon_maker_tagmap_ref());

    // Reserve maximum space that an APP1 can take in a JPEG file, so
    // we can push_back to our heart's content and know that no offsets
    // or pointers to the exif vector's memory will change due to
    // reallocation.
    blob.reserve (0xffff);

    // Layout:
    //                     .-----------------------------------------
    //    (tiffstart) ---->|  TIFFHeader
    //                     |    magic
    //                     |    version
    //                  .--+--  diroff
    //                  |  |-----------------------------------------
    //            .-----+->|  d
    //            |     |  |   a
    //            |  .--+->|    t
    //            |  |  |  |     a
    //        .---+--+--+->|  d
    //        |   |  |  |  |   a
    //      .-+---+--+--+->|    t
    //      | |   |  |  |  |     a
    //      | |   |  |  |  +-----------------------------------------
    //      | |   |  |  `->|  number of top dir entries
    //      | |   `--+-----+- top dir entry 0
    //      | |      |     |  ...
    //      | |      | .---+- top dir Exif entry (point to Exif IFD)
    //      | |      | |   |  ...
    //      | |      | |   +------------------------------------------
    //      | |      | `-->|  number of Exif IFD dir entries (n)
    //      | |      `-----+- Exif IFD entry 0
    //      | |            |  ...
    //      | |        .---+- Exif entry for maker note
    //      | |        |   |  ...
    //      | `--------+---+- Exif IFD entry n-1
    //      |          |   +------------------------------------------
    //      |           `->|  number of makernote IFD dir entries
    //      `--------------+- Makernote IFD entry 0
    //                     |  ...
    //                     `------------------------------------------

    // Put a TIFF header
    size_t tiffstart = blob.size();   // store initial size
    TIFFHeader head;
    head.tiff_magic = littleendian() ? 0x4949 : 0x4d4d;
    head.tiff_version = 42;
    // head.tiff_diroff -- fix below, once we know the sizes
    append (blob, head);

    // Accumulate separate tag directories for TIFF, Exif, GPS, and Interop.
    std::vector<TIFFDirEntry> tiffdirs, exifdirs, gpsdirs;
    std::vector<TIFFDirEntry> makerdirs;

    // Go through all spec attribs, add them to the appropriate tag
    // directory (tiff, gps, or exif), adding their data to the main blob.
    for (const ParamValue &p : spec.extra_attribs) {
        // Which tag domain are we using?
        if (Strutil::starts_with (p.name(), "GPS:")) {
            int tag = gps_tagmap.tag (p.name());
            if (tag >= 0)
                encode_exif_entry (p, tag, gpsdirs, blob, gps_tagmap, tiffstart);
        } else {
            // Not GPS
            int tag = exif_tagmap.tag (p.name());
            if (tag < EXIFTAG_EXPOSURETIME || tag > EXIFTAG_IMAGEUNIQUEID) {
                // This range of Exif tags go in the main TIFF directories,
                // not the Exif IFD. Whatever.
                encode_exif_entry (p, tag, tiffdirs, blob, exif_tagmap, tiffstart);
            } else {
                encode_exif_entry (p, tag, exifdirs, blob, exif_tagmap, tiffstart);
            }
        }
    }

    // If we're a canon camera, construct the dirs for the Makernote,
    // with the data adding to the main blob.
    if (Strutil::iequals (spec.get_string_attribute("Make"), "Canon"))
        pvt::encode_canon_makernote (blob, makerdirs, spec, tiffstart);

#if DEBUG_EXIF_WRITE
    std::cerr << "Blob header size " << blob.size() << "\n";
    std::cerr << "tiff tags: " << tiffdirs.size() << "\n";
    std::cerr << "exif tags: " << exifdirs.size() << "\n";
    std::cerr << "gps tags: " << gpsdirs.size() << "\n";
    std::cerr << "canon makernote tags: " << makerdirs.size() << "\n";
#endif

    // If any legit Exif info was found (including if there's a maker note),
    // add some extra required Exif fields.
    if (exifdirs.size() || makerdirs.size()) {
        // Add some required Exif tags that wouldn't be in the spec
        append_tiff_dir_entry (exifdirs, blob,
                               EXIFTAG_EXIFVERSION, TIFF_UNDEFINED, 4, "0230", tiffstart);
        append_tiff_dir_entry (exifdirs, blob,
                               EXIFTAG_FLASHPIXVERSION, TIFF_UNDEFINED, 4, "0100", tiffstart);
        static char componentsconfig[] = { 1, 2, 3, 0 };
        append_tiff_dir_entry (exifdirs, blob,
                          EXIFTAG_COMPONENTSCONFIGURATION, TIFF_UNDEFINED, 4, componentsconfig, tiffstart);
    }

    // If any GPS info was found, add a version tag to the GPS fields.
    if (gpsdirs.size()) {
        // Add some required Exif tags that wouldn't be in the spec
        static char ver[] = { 2, 2, 0, 0 };
        append_tiff_dir_entry (gpsdirs, blob, GPSTAG_VERSIONID, TIFF_BYTE, 4, &ver, tiffstart);
    }

    // Compute offsets:
    // TIFF dirs will start after the data
    size_t tiffdirs_offset = blob.size() - tiffstart;
    size_t tiffdirs_size = sizeof(uint16_t)  // ndirs
                         + sizeof(TIFFDirEntry) * tiffdirs.size()
                         + (exifdirs.size() ? sizeof(TIFFDirEntry) : 0)
                         + (gpsdirs.size() ? sizeof(TIFFDirEntry) : 0)
                         + sizeof(uint32_t);  // zero pad for next IFD offset
    // Exif dirs will start after the TIFF dirs.
    size_t exifdirs_offset = tiffdirs_offset + tiffdirs_size;
    size_t exifdirs_size = exifdirs.empty() ? 0 :
                           ( sizeof(uint16_t)  // ndirs
                           + sizeof(TIFFDirEntry) * exifdirs.size()
                           + (makerdirs.size() ? sizeof(TIFFDirEntry) : 0)
                           + sizeof(uint32_t));  // zero pad for next IFD offset
    // GPS dirs will start after Exif
    size_t gpsdirs_offset = exifdirs_offset + exifdirs_size;
    size_t gpsdirs_size = gpsdirs.empty() ? 0 :
                          ( sizeof(uint16_t)  // ndirs
                          + sizeof(TIFFDirEntry) * gpsdirs.size()
                          + sizeof(uint32_t));  // zero pad for next IFD offset
    // MakerNote is after GPS
    size_t makerdirs_offset = gpsdirs_offset + gpsdirs_size;
    size_t makerdirs_size = makerdirs.empty() ? 0 :
                          ( sizeof(uint16_t)  // ndirs
                          + sizeof(TIFFDirEntry) * makerdirs.size()
                          + sizeof(uint32_t));  // zero pad for next IFD offset

    // If any Maker info was found, add a MakerNote tag to the Exif dirs
    if (makerdirs.size()) {
        ASSERT (exifdirs.size());
        // unsigned int size = (unsigned int) makerdirs_offset;
        append_tiff_dir_entry (exifdirs, blob, EXIFTAG_MAKERNOTE, TIFF_BYTE,
                               makerdirs_size, nullptr, 0, makerdirs_offset);
    }

    // If any Exif info was found, add a Exif IFD tag to the TIFF dirs
    if (exifdirs.size()) {
        unsigned int size = (unsigned int) exifdirs_offset;
        append_tiff_dir_entry (tiffdirs, blob, TIFFTAG_EXIFIFD, TIFF_LONG, 1, &size, tiffstart);
    }

    // If any GPS info was found, add a GPS IFD tag to the TIFF dirs
    if (gpsdirs.size()) {
        unsigned int size = (unsigned int) gpsdirs_offset;
        append_tiff_dir_entry (tiffdirs, blob, TIFFTAG_GPSIFD, TIFF_LONG, 1, &size, tiffstart);
    }

    // All the tag dirs need to be sorted
    std::sort (exifdirs.begin(), exifdirs.end(), tagcompare());
    std::sort (gpsdirs.begin(), gpsdirs.end(), tagcompare());
    std::sort (makerdirs.begin(), makerdirs.end(), tagcompare());

    // Now mash everything together
    size_t tiffdirstart = blob.size();
    append (blob, uint16_t(tiffdirs.size()));      // ndirs for tiff
    appendvec (blob, tiffdirs);                    // tiff dirs
    append (blob, uint32_t(0));                    // addr of next IFD (none)
    if (exifdirs.size()) {
        ASSERT (blob.size() == exifdirs_offset + tiffstart);
        append (blob, uint16_t(exifdirs.size()));  // ndirs for exif
        appendvec (blob, exifdirs);                // exif dirs
        append (blob, uint32_t(0));                // addr of next IFD (none)
    }
    if (gpsdirs.size()) {
        ASSERT (blob.size() == gpsdirs_offset + tiffstart);
        append (blob, uint16_t(gpsdirs.size()));   // ndirs for gps
        appendvec (blob, gpsdirs);                 // gps dirs
        append (blob, uint32_t(0));                // addr of next IFD (none)
    }
    if (makerdirs.size()) {
        ASSERT (blob.size() == makerdirs_offset + tiffstart);
        append (blob, uint16_t(makerdirs.size())); // ndirs for canon
        appendvec (blob, makerdirs);               // canon dirs
        append (blob, uint32_t(0));                // addr of next IFD (none)
    }

    // Now go back and patch the header with the offset of the first TIFF
    // directory.
    ((TIFFHeader *)(blob.data()+tiffstart))->tiff_diroff = tiffdirstart - tiffstart;

#if DEBUG_EXIF_WRITE
    std::cerr << "resulting exif block is a total of " << blob.size() << "\n";
#if 1
    std::cerr << "APP1 dump:";
    for (size_t pos = 0; pos < blob.size(); ++pos) {
        bool at_ifd = (pos == tiffdirs_offset+tiffstart ||
                       pos == exifdirs_offset+tiffstart ||
                       pos == gpsdirs_offset+tiffstart ||
                       pos == makerdirs_offset+tiffstart);
        if (pos == 0 || pos == tiffstart || at_ifd || (pos % 10) == 0) {
            std::cerr << "\n@" << pos << ": ";
            if (at_ifd) {
                uint16_t n = *(uint16_t *)&blob[pos];
                std::cerr << "\nNew IFD: " << n << " tags:\n";
                TIFFDirEntry *td = (TIFFDirEntry *) &blob[pos+2];
                for (int i = 0; i < n; ++i, ++td)
                    std::cerr << "  Tag " << td->tdir_tag
                              << " type=" << td->tdir_type
                              << " (" << tiff_datatype_to_typedesc(td->tdir_type) << ")"
                              << " count=" << td->tdir_count
                              << " offset=" << td->tdir_offset
                              << "   post-tiff offset=" << (td->tdir_offset+tiffstart)
                              << "\n";
            }
        }
        unsigned char c = (unsigned char) blob[pos];
        if (c >= ' ' && c < 127)
            std::cerr << c << ' ';
        std::cerr << "(" << (int)c << ") ";
    }
    std::cerr << "\n";
#endif
#endif
}



bool
exif_tag_lookup (string_view name, int &tag, int &tifftype, int &count)
{
    const TagInfo *e = exif_tagmap_ref().find (name);
    if (! e)
        return false;  // not found

    tag = e->tifftag;
    tifftype = e->tifftype;
    count = e->tiffcount;
    return true;
}


OIIO_NAMESPACE_END

