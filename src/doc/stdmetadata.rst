.. _chap-stdmetadata:

Metadata conventions
####################



The ImageSpec class, described thoroughly in :ref:`sec-ImageSpec`, provides
the basic description of an image that are essential across all formats ---
resolution, number of channels, pixel data format, etc.  Individual images
may have additional data, stored as name/value pairs in the `extra_attribs`
field. Though literally *anything* can be stored in `extra_attribs` --- it's
specifically designed for format- and user-extensibility --- this chapter
establishes some guidelines and lays out all of the field names that
OpenImageIO understands.


Description of the image
========================

.. option:: "ImageDescription" : string

    The image description, title, caption, or comments.

.. option:: "Keywords" : string

    Semicolon-separated keywords describing the contents of the image.
    (Semicolons are used rather than commas because of the common case of a
    comma being part of a keyword itself, e.g., "Kurt Vonnegut, Jr." or
    "Washington, DC."")

.. option:: "Artist" : string

    The artist, creator, or owner of the image.

.. option:: "Copyright" : string

    Any copyright notice or owner of the image.

.. option:: "DateTime" : string

    The creation date of the image, in the following format: `YYYY:MM:DD HH:MM:SS` (exactly 19 characters long, not including a terminating
    NULL).  For example, 7:30am on Dec 31, 2008 is encoded as
    `"2008:12:31 07:30:00"`.

    Usually this is simply the time that the image data was last modified.
    It may be wise to also store the `"Exif:DateTimeOriginal"` and
    `"Exif:DateTimeDigitized"` (see Section :ref:`sec-metadata-exif`) to
    further distinguish the original image and its conversion to digital
    media.

.. option:: "DocumentName" : string

    The name of an overall document that this image is a part of.

.. option:: "Software" : string

    The software that was used to create the image.

.. option:: "HostComputer" : string

    The name or identity of the computer that created the image.



.. _sec-metadata-displayhints:
.. _sec-metadata-orientation:

Display hints
=============


.. option:: "Orientation" : int

    y default, image pixels are ordered from the top of the display to the
    ottom, and within each scanline, from left to right (i.e., the same
    rdering as English text and scan progression on a CRT).  But the
    "Orientation"` field can suggest that it should be displayed with
    different orientation, according to the TIFF/EXIF conventions:

    ===  ==========================================================================
     0   normal (top to bottom, left to right)
     1   flipped horizontally (top to botom, right to left)
     2   rotated :math:`180^\circ` (bottom to top, right to left)
     3   flipped vertically (bottom to top, left to right)
     4   transposed (left to right, top to bottom)
     5   rotated :math:`90^\circ` clockwise (right to left, top to bottom)
     6   transverse (right to left, bottom to top)
     7   rotated :math:`90^\circ` counter-clockwise (left to right, bottom to top)
    ===  ==========================================================================

.. option:: "PixelAspectRatio" : float

    The aspect ratio (:math:`x/y`) of the size of individual pixels, with
    square pixels being 1.0 (the default).

.. option:: "XResolution" : float
            "YResolution" : float
            "ResolutionUnit" : string

    The number of horizontal (*x*) and vertical (*y*) pixels per resolution
    unit.  This ties the image to a physical size (where applicable, such as
    with a scanned image, or an image that will eventually be printed).

    Different file formats may dictate different resolution units. For
    example, the TIFF ImageIO plugin supports `none`, `in`, and `cm`.

.. option:: "oiio:Movie" : int

    If nonzero, a hint that a multi-image file is meant to be interpreted as
    an animation (i.e., that the subimages are a time sequence).

.. option:: "oiio:subimages" : int

    If nonzero, the number of subimages in the file. Not all image file
    formats can know this without reading the entire file, and in such
    cases, this attribute will not be set or will be 0. If the value is
    present and greater than zero, it can be trusted, but if not, nothing
    should be inferred and you will have to repeatedly seek to subimages
    to find out how many there are.

.. option:: "FramesPerSecond" : rational

    For a multi-image file intended to be played back as an animation, the
    frame refresh rate. (It's technically a rational, but it may be
    retrieved as a float also, if you are ok with imprecision.)



.. _sec-metadata-color:

Color information
=================

.. option:: "oiio:ColorSpace" : string

    The name of the color space of the color channels.  Values incude:
    
    - `"Linear"` :  Color pixel values are known to be scene-linear and
      using facility-default color primaries (presumed sRGB/Rec709 color
      primaries if otherwise unknown.
    - `"sRGB"` :  Using standard sRGB response and primaries.
    - `"Rec709"` :  Using standard Rec709 response and primaries.
    - `"ACES"` :  ACES color space encoding.
    - `"AdobeRGB"` :  Adobe RGB color space.
    - `"KodakLog"` :  Kodak logarithmic color space.
    - `"GammaCorrectedX.Y"` :  Color values have been gamma corrected
      (raised to the power :math:`1/\gamma`). The `X.Y` is the numeric value
      of the gamma exponent.
    - *arbitrary* :  The name of any color space known to OpenColorIO (if
      OCIO support is present).

.. option:: "oiio:Gamma" : float

    If the color space is "GammaCorrectedX.Y", this value is the gamma
    exponent. (Optional/deprecated; if present, it should match the suffix
    of the color space.)

.. option:: "oiio:BorderColor" : float[nchannels]

    The color presumed to be filling any parts of the display/full image
    window that are not overlapping the pixel data window.  If not supplied,
    the default is black (0 in all channels).

.. option:: "ICCProfile" : uint8[]

    The ICC color profile takes the form of an array of bytes (unsigned 8
    bit chars). The length of the array is the length of the profile blob.



Disk file format info/hints
===========================

.. option:: "oiio:BitsPerSample" : int

    Number of bits per sample *in the file*.
    
    Note that this may not match the reported `ImageSpec::format`, if the
    plugin is translating from an unsupported format.  For example, if a
    file stores 4 bit grayscale per channel, the `"oiio:BitsPerSample"` may
    be 4 but the `format` field may be `TypeDesc::UINT8` (because the
    OpenImageIO APIs do not support fewer than 8 bits per sample).

.. option:: "oiio:UnassociatedAlpha" : int

    Whether the data in the file stored alpha channels (if any) that were
    unassociated with the color (i.e., color not "premultiplied" by the
    alpha coverage value).

.. option:: "planarconfig" : string

    `contig` indicates that the file has contiguous pixels (RGB RGB RGB...),
    whereas `separate` indicate that the file stores each channel separately
    (RRR...GGG...BBB...).

    Note that only contiguous pixels are transmitted through the OpenImageIO
    APIs, but this metadata indicates how it is (or should be) stored in the
    file, if possible.

.. option:: "compression" : string

    Indicates the type of compression the file uses.  Supported compression
    modes will vary from file format to file format, and each reader/writer
    plugin should document the modes it supports.  If `ImageOutput::open` is
    called with an ImageSpec that specifies an compression mode not
    supported by that ImageOutput, it will choose a reasonable default. As
    an example, the OpenEXR writer supports `none`, `rle`, `zip`, `zips`,
    `piz`, `pxr24`, `b44`, `b44a`, `dwaa`, or `dwab`.

    he compression name is permitted to have a quality value to be appended
    fter a colon, for example `dwaa:60`.  The exact meaning and range of
    he quality value can vary between different file formats and compression
    odes, and some don't support quality values at all (it will be ignored if
    ot supported, or if out of range).

.. option:: "CompressionQuality" : int

    DEPRECATED(2.1)

    This is a deprecated methods of separately specifying the compression
    quality. Indicates the quality of compression to use (0--100), for those
    plugins and compression methods that allow a variable amount of
    compression, with higher numbers indicating higher image fidelity.




Substituting an `IOPRoxy` for custom I/O overrides
======================================================

Format readers and writers that respond positively to `supports("ioproxy")`
have the ability to read or write using an *I/O proxy* object. Among other
things, this lets an ImageOutput write the file to a memory buffer rather
than saving to disk, and for an ImageInput to read the file from a memory
buffer. This behavior is controlled by a special attributes

.. option:: "oiio:ioproxy" : pointer

    Pointer to a `Filesystem::IOProxy` that will handle the I/O.

An explanation of how this feature is used may be found in Sections
:ref:`sec-imageinput-readfilefrommemory` and
:ref:`sec-imageoutput-writefiletomemory`.

In addition to communicating IOProxy information via this attribute, it
is also allowed (and probably preferred) to directly supply the IOProxy
information via calls to `ImageInput::open()`, `create()`, and
`set_ioproxy()`, `ImageOutput::create()` and `set_ioproxy()`, and
`ImageBuf::ImageBuf()`, `reset()`, and `set_write_ioproxy()`.



Photographs or scanned images
=============================

The following metadata items are specific to photos or captured images.

.. option:: "Make" : string

    For captured or scanned image, the make of the camera or scanner.

.. option:: "Model" : string

    For captured or scanned image, the model of the camera or scanner.

.. option:: "ExposureTime" : float

    The exposure time (in seconds) of the captured image.

.. option:: "FNumber" : float

    The f/stop of the camera when it captured the image.


Thumbnails / postage stamps / preview images
============================================

Some image file formats allow for storing a `thumbnail` -- a lower-resolution
image suitable for an icon or other small preview. These are retrievable
separately via `ImageInput::get_thumbnail()`, `ImageBuf::thumbnail()`, or
`ImageCache::get_thumbnail()`. In addition, if a thumbnail is available,
the `ImageSpec` should also contain the following metadata:


.. option:: "thumbnail_width" : int

    The thumbnail width, in pixels.

.. option:: "thumbnail_height" : int

    The thumbnail height, in pixels.

.. option:: "thumbnail_nchannels" : int

    The number of color channels in the thumbnail image.



Texture Information
===================

Several standard metadata are very helpful for images that are intended
to be used as textures (especially for OpenImageIO's TextureSystem).

.. option:: "textureformat" : string

    The kind of texture that this image is intended to be.  We suggest the
    following names:

    =====================   ================================================
    Plain Texture           Ordinary 2D texture
    Volume Texture          3D volumetric texture
    Shadow                  Ordinary *z*-depth shadow map
    CubeFace Shadow         Cube-face shadow map
    Volume Shadow           Volumetric ("deep") shadow map
    LatLong Environment     Latitude-longitude (rectangular) environment map
    CubeFace Environment    Cube-face environment map
    =====================   ================================================

.. option:: "wrapmodes" : string

    Give the intended texture *wrap mode* indicating what happens with
    texture coordinates outside the :math:`[0...1]` range.  We suggest the
    following names: `black`, `periodic`, `clamp`, `mirror`. If the wrap
    mode is different in each direction, they should simply be separated by
    a comma.  For example, `black` means black wrap in both directions,
    whereas `clamp,periodic` means to clamp in :math:`u` and be periodic in
    :math:`v`.

.. option:: "fovcot" : float

    The cotangent (:math:`x/y`) of the field of view of the original image
    (which may not be the same as the aspect ratio of the pixels of the
    texture, which may have been resized).

.. option:: "worldtocamera" : matrix44

    For shadow maps or rendered images this item (of type
    `TypeDesc::PT_MATRIX`) is the world-to-camera matrix describing the
    camera position.

.. option:: "worldtoscreen" : matrix44

    For shadow maps or rendered images this item (of type
    `TypeDesc::PT_MATRIX`) is the world-to-screen matrix describing the full
    projection of the 3D view onto a :math:`[-1...1] \times [-1...1]` 2D
    domain.

.. option:: "worldtoNDC" : matrix44

    For shadow maps or rendered images this item (of type
    `TypeDesc::PT_MATRIX`) is the world-to-NDC matrix describing the full
    projection of the 3D view onto a :math:`[0...1] \times [0...1]` 2D
    domain.

.. option:: "oiio:updirection" : string

    For environment maps, indicates which direction is "up" (valid values
    are `y` or `z`), to disambiguate conventions for environment map
    orientation.

.. option:: "oiio:sampleborder" : int

    If not present or 0 (the default), the conversion from pixel integer
    coordinates :math:`(i,j)` to texture coordinates :math:`(s,t)` follows
    the usual convention of :math:`s = (i+0.5)/\mathit{xres}` and
    :math:`t = (j+0.5)/\mathit{yres}`. However, if this attribute is present
    and nonzero, the first and last row/column of pixels lie exactly at the
    :math:`s` or :math:`t = 0` or :math:`1` boundaries, i.e.,
    :math:`s = i/(\mathit{xres}-1)` and :math:`t = j/(\mathit{yres}-1)`.

.. option:: "oiio:ConstantColor" : string

    If present, is a hint that the texture has the same value in all pixels,
    and the metadata value is a string containing the channel values as a
    comma-separated list (no spaces, for example: `0.73,0.9,0.11,1.0`).

.. option:: "oiio:AverageColor" : string

    If present, is a hint giving the *average* value of all pixels in the
    texture, as a string containing a comma-separated list of the channel
    values (no spaces, for example: `0.73,0.9,0.11,1.0`).

.. option:: "oiio:SHA-1" : string

    f present, is a 40-byte SHA-1 hash of the input image (possibly salted
    with arious maketx options) that can serve to quickly compare two
    separate extures to know if they contain the same pixels. While it's
    not, echnically, 100% guaranteed that no separate textures will match,
    it's so stronomically unlikely that we discount the possibility (you'd
    be rendering ovies for centuries before finding a single match).



.. _sec-metadata-exif:

Exif metadata
=============

..
    % FIXME -- unsupported/undocumented: ExifVersion, FlashpixVersion,
    % ComponentsConfiguration, MakerNote, UserComment, RelatedSoundFile,
    % OECF, SubjectArea, SpatialFrequencyResponse, 
    % CFAPattern, DeviceSettingDescription
    %
    % SubjectLocation -- unsupported, but we could do it

The following Exif metadata tags correspond to items in the "standard"
set of metadata.


==============  ==================================================
Exif tag        OpenImageIO metadata convention
==============  ==================================================
ColorSpace      (reflected in "oiio:ColorSpace")
ExposureTime    `ExposureTime`
FNumber         `FNumber`
==============  ==================================================


The other remaining Exif metadata tags all include the ``Exif:`` prefix
to keep it from clashing with other names that may be used for other
purposes.

.. option:: "Exif:ExposureProgram" : int

    The exposure program used to set exposure when the picture was taken:

    ===  ==============================================================
     0   unknown
     1   manual
     2   normal program
     3   aperture priority
     4   shutter priority
     5   Creative program (biased toward depth of field)
     6   Action program (biased toward fast shutter speed)
     7   Portrait mode (closeup photo with background out of focus)
     8   Landscape mode (background in focus)
    ===  ==============================================================

.. option:: "Exif:SpectralSensitivity" : string

    The camera's spectral sensitivity, using the ASTM conventions.

.. option:: "Exif:ISOSpeedRatings" : int

    The ISO speed and ISO latitude of the camera as specified in ISO 12232.


.. option:: "Exif:DateTimeOriginal" : string
            "Exif:DateTimeDigitized" : string

    Date and time that the original image data was generated or captured,
    and the time/time that the image was stored as digital data. Both are in
    `YYYY:MM:DD HH:MM:SS` format.

    To clarify the role of these (and also with respect to the standard
    `DateTime` metadata), consider an analog photograph taken in 1960
    (`Exif:DateTimeOriginal`), which was scanned to a digital image in 2010
    (`Exif:DateTimeDigitized`), and had color corrections or other
    alterations performed in 2015 (`DateTime`).

.. option:: "Exif:CompressedBitsPerPixel" : float

    The compression mode used, measured in compressed bits per pixel.

.. option:: "Exif:ShutterSpeedValue" : float

    Shutter speed, in APEX units: :math:`-\log_2(\mathit{exposure time})`

.. option:: "Exif:ApertureValue" : float

    Aperture, in APEX units: :math:`2 \log_2 (\mathit{fnumber})`

.. option:: "Exif:BrightnessValue" : float

    Brightness value, assumed to be in the range of :math:`-99.99` -- :math:`99.99`.

.. option:: "Exif:ExposureBiasValue" : float

    Exposure bias, assumed to be in the range of :math:`-99.99` -- :math:`99.99`.

.. option:: "Exif:MaxApertureValue" : float

    Smallest F number of the lens, in APEX units: :math:`2 \log_2 (\mathit{fnumber})`

.. option:: "Exif:SubjectDistance" : float

    Distance to the subject, in meters.

.. option:: "Exif:MeteringMode" : int

    The metering mode:

    ===  ===============================================
    0    unknown
    1    average
    2    center-weighted average
    3    spot
    4    multi-spot
    5    pattern
    6    partial
    255  other
    ===  ===============================================

.. option:: "Exif:LightSource" : int

The kind of light source:

    ===  ===============================================
    0    unknown
    1    daylight
    2    tungsten (incandescent light)
    4    flash
    9    fine weather
    10   cloudy weather
    11   shade
    12   daylight fluorescent (D 5700-7100K)
    13   day white fluorescent (N 4600-5400K)
    14   cool white fuorescent (W 3900 - 4500K)
    15   white fluorescent (WW 3200 - 3700K)
    17   standard light A
    18   standard light B
    19   standard light C
    20   D55
    21   D65
    22   D75
    23   D50
    24   ISO studio tungsten
    255  other light source
    ===  ===============================================


.. option:: "Exif:Flash" int}

A sum of:

    ===  ==============================================================
    1    if the flash fired
    0    no strobe return detection function
    4    strobe return light was not detected
    6    strobe return light was detected
    8    compulsary flash firing
    16   compulsary flash suppression
    24   auto-flash mode
    32   no flash function (0 if flash function present)
    64   red-eye reduction supported (0 if no red-eye reduction mode)
    ===  ==============================================================


.. option:: "Exif:FocalLength" : float

    Actual focal length of the lens, in mm.

.. option:: "Exif:SecurityClassification" : string

    Security classification of the image: `C` = confidential, `R` =
    restricted, `S` = secret, `T` = top secret, `U` = unclassified.

.. option:: "Exif:ImageHistory" : string

    Image history.

.. option:: "Exif:SubsecTime" : string

    Fractions of a second to augment the `"DateTime"` (expressed as text of
    the digits to the right of the decimal).

.. option:: "Exif:SubsecTimeOriginal" : string

    Fractions of a second to augment the `Exif:DateTimeOriginal` (expressed
    as text of the digits to the right of the decimal).

.. option:: "Exif:SubsecTimeDigitized" : string

    Fractions of a second to augment the `Exif:DateTimeDigital` (expressed
    as text of the digits to the right of the decimal).


.. option:: "Exif:PixelXDimension" : int
            "Exif:PixelYDimension" : int

    The *x* and *y* dimensions of the valid pixel area.

.. option:: "Exif:FlashEnergy" : float

    Strobe energy when the image was captures, measured in Beam Candle Power
    Seconds (BCPS).

.. option:: "Exif:FocalPlaneXResolution" : float
            "Exif:FocalPlaneYResolution" : float
            "Exif:FocalPlaneResolutionUnit" : int

    The number of pixels in the *x* and *y* dimension, per resolution unit.
    The codes for resolution units are:


    ===  ==============================================================
    1    none
    2    inches
    3    cm
    4    mm
    5    :math:`\mu m`
    ===  ==============================================================


..
    option: "Exif:SubjectLocation" : int} // FIXME: short[2]


.. option:: "Exif:ExposureIndex" : float

    The exposure index selected on the camera.

.. option:: "Exif:SensingMethod" : int

    The image sensor type on the camra:

    ===  ==============================================================
    1    undefined
    2    one-chip color area sensor
    3    two-chip color area sensor
    4    three-chip color area sensor
    5    color sequential area sensor
    7    trilinear sensor
    8    color trilinear sensor 
    ===  ==============================================================

.. option:: "Exif:FileSource" : int

    The source type of the scanned image, if known:

    ===  ==============================================================
    1    film scanner
    2    reflection print scanner
    3    digital camera
    ===  ==============================================================

.. option:: "Exif:SceneType" : int

    Set to 1, if a directly-photographed image, otherwise it should not be
    present.

.. option:: "Exif:CustomRendered" : int

    Set to 0 for a normal process, 1 if some custom processing has been
    performed on the image data.

.. option:: "Exif:ExposureMode" : int

    The exposure mode:

    ===  ==============================================================
     0   auto
     1   manual
     2   auto-bracket
    ===  ==============================================================

.. option:: "Exif:WhiteBalance" : int

    Set to 0 for auto white balance, 1 for manual white balance.

.. option:: "Exif:DigitalZoomRatio" : float

    The digital zoom ratio used when the image was shot.

.. option:: "Exif:FocalLengthIn35mmFilm" : int

    The equivalent focal length of a 35mm camera, in mm.

.. option:: "Exif:SceneCaptureType" : int

    The type of scene that was shot:

    ===  ==============================================================
     0   standard
     1   landscape
     2   portrait
     3   night scene
    ===  ==============================================================

.. option:: "Exif:GainControl" : float

    The degree of overall gain adjustment:

    ===  ==============================================================
     0   none
     1   low gain up
     2   high gain up
     3   low gain down
     4   high gain down
    ===  ==============================================================

.. option:: "Exif:Contrast" : int

    The direction of contrast processing applied by the camera:

    ===  ==============================================================
     0   normal
     1   soft
     2   hard
    ===  ==============================================================

.. option:: "Exif:Saturation" : int

    The direction of saturation processing applied by the camera:

    ===  ==============================================================
     0   normal
     1   low saturation
     2   high saturation
    ===  ==============================================================

.. option:: "Exif:Sharpness" : int

    The direction of sharpness processing applied by the camera:

    ===  ==============================================================
     0   normal
     1   soft
     2   hard
    ===  ==============================================================

.. option:: "Exif:SubjectDistanceRange" : int

    The distance to the subject:

    ===  ==============================================================
     0   unknown
     1   macro
     2   close
     3   distant
    ===  ==============================================================

.. option:: "Exif:ImageUniqueID" : string

    A unique identifier for the image, as 16 ASCII hexidecimal digits
    representing a 128-bit number.



GPS Exif metadata
=================

The following GPS-related Exif metadata tags correspond to items in the
"standard" set of metadata.

.. option:: "GPS:LatitudeRef" : string

    Whether the `GPS:Latitude` tag refers to north or south: `N` or `S`.

.. option:: "GPS:Latitude" : float[3]

    The degrees, minutes, and seconds of latitude (see also
    `GPS:LatitudeRef`).

.. option:: "GPS:LongitudeRef" : string

    Whether the `GPS:Longitude` tag refers to east or west: `E` or a `W`.

.. option:: "GPS:Longitude" : float[3]

    The degrees, minutes, and seconds of longitude (see also
    `GPS:LongitudeRef`).

.. option:: "GPS:AltitudeRef" : string

    A value of 0 indicates that the altitude is above sea level, 1 indicates
    below sea level.

.. option:: "GPS:Altitude" : float

    Absolute value of the altitude, in meters, relative to sea level (see
    `GPS:AltitudeRef` for whether it's above or below sea level).

.. option:: "GPS:TimeStamp" : float[3]

    Gives the hours, minutes, and seconds, in UTC.

.. option:: "GPS:Satellites" : string

    Information about what satellites were visible.

.. option:: "GPS:Status" : string

    `A` indicates a measurement in progress, `V` indicates
    measurement interoperability.

.. option:: "GPS:MeasureMode" : string

    `2` indicates a 2D measurement, `3` indicates a 3D measurement.

.. option:: "GPS:DOP" : float

    Data degree of precision.

.. option:: "GPS:SpeedRef" : string

    Indicates the units of the related `GPS:Speed` tag: `K` for km/h, `M`
    for miles/h, `N` for knots.

.. option:: "GPS:Speed" : float

    Speed of the GPS receiver (see `GPS:SpeedRef` for the units).

.. option:: "GPS:TrackRef" : string

    Describes the meaning of the `GPS:Track` field: `T` for true
    direction, `M` for magnetic direction.

.. option:: "GPS:Track" : float

    Direction of the GPS receiver movement (from 0--359.99).  The
    related `GPS:TrackRef` indicate whether it's true or magnetic.

.. option:: "GPS:ImgDirectionRef" : string

    Describes the meaning of the `GPS:ImgDirection` field: `T` for true
    direction, `M` for magnetic direction.

.. option:: "GPS:ImgDirection" : float

    Direction of the image when captured (from 0--359.99).  The
    related `GPS:ImgDirectionRef` indicate whether it's true or magnetic.

.. option:: "GPS:MapDatum" : string

    The geodetic survey data used by the GPS receiver.

.. option:: "GPS:DestLatitudeRef" : string

    Whether the `GPS:DestLatitude` tag refers to north or south: `N` or `S`.

.. option:: "GPS:DestLatitude" : float[3]

    The degrees, minutes, and seconds of latitude of the destination (see
    also `GPS:DestLatitudeRef`).

.. option:: "GPS:DestLongitudeRef" : string

    Whether the `GPS:DestLongitude` tag refers to east or west: `E` or `W`.

.. option:: "GPS:DestLongitude" : float[3]

    The degrees, minutes, and seconds of longitude of the destination (see
    also `GPS:DestLongitudeRef`).

.. option:: "GPS:DestBearingRef" : string

    Describes the meaning of the `GPS:DestBearing` field: `T` for true
    direction, `M` for magnetic direction.

.. option:: "GPS:DestBearing" : float

    Bearing to the destination point (from 0--359.99).  The
    related `GPS:DestBearingRef` indicate whether it's true or magnetic.

.. option:: "GPS:DestDistanceRef" : string

    Indicates the units of the related `GPS:DestDistance` tag: `K` for
    km, `M` for miles, `N` for knots.

.. option:: "GPS:DestDistance" : float

    Distance to the destination (see `GPS:DestDistanceRef` for the units).

.. option:: "GPS:ProcessingMethod" : string

    Processing method information.

.. option:: "GPS:AreaInformation" : string

    Name of the GPS area.

.. option:: "GPS:DateStamp" : string

    Date according to the GPS device, in format `YYYY:MM:DD`.

.. option:: "GPS:Differential" : int

    If 1, indicates that differential correction was applied.

.. option:: "GPS:HPositioningError" : float

    Positioning error.



IPTC metadata
=============

The IPTC (International Press Telecommunications Council) publishes
conventions for storing image metadata, and this standard is growing in
popularity and is commonly used in photo-browsing programs to record
captions and keywords.

The following IPTC metadata items correspond exactly to metadata in the
OpenImageIO conventions, so it is recommended that you use the standards and
that plugins supporting IPTC metadata respond likewise:

    ===============  =========================================================================================================
    IPTC tag         OpenImageIO metadata convention
    ===============  =========================================================================================================
    Caption          `"ImageDescription"`
    Keyword          IPTC keywords should be concatenated, separated by semicolons (`;`), and stored as the `Keywords` attribute.
    ExposureTime     `ExposureTime`
    CopyrightNotice  `Copyright`
    Creator          `Artist`
    ===============  =========================================================================================================


The remainder of IPTC metadata fields should use the following names,
prefixed with `IPTC:` to avoid conflicts with other plugins or standards.

.. option:: "IPTC:ObjecTypeReference" : string

    Object type reference.

.. option:: "IPTC:ObjectAttributeReference" : string

    Object attribute reference.

.. option:: "IPTC:ObjectName" : string

    The name of the object in the picture.

.. option:: "IPTC:EditStatus" : string

    Edit status.

.. option:: "IPTC:SubjectReference" : string

    Subject reference.

.. option:: "IPTC:Category" : string

    Category.

.. option:: "IPTC:ContentLocationCode" : string

    Code for content location.

.. option:: "IPTC:ContentLocationName" : string

    Name of content location.

.. option:: "IPTC:ReleaseDate" : string
            "IPTC:ReleaseTime" : string

    Release date and time.

.. option:: "IPTC:ExpirationDate" : string
            "IPTC:ExpirationTime" : string

    Expiration date and time.

.. option:: "IPTC:Instructions" : string

    Special instructions for handling the image.

.. option:: "IPTC:ReferenceService" : string
            "IPTC:ReferenceDate" : string
            "IPTC:ReferenceNumber" : string

    Reference date, service, and number.

.. option:: "IPTC:DateCreated" : string
            "IPTC:TimeCreated" : string

    Date and time that the image was created.

.. option:: "IPTC:DigitalCreationDate" : string
            "IPTC:DigitalCreationTime" : string

    Date and time that the image was digitized.

.. option:: "IPTC:ProgramVersion" : string

    The version number of the creation software.

.. option:: "IPTC:AuthorsPosition" : string

    The job title or position of the creator of the image.

.. option:: "IPTC:City" : string
            "IPTC:Sublocation" : string
            "IPTC:State" : string
            "IPTC:Country" : string
            "IPTC:CountryCode" : string

    The city, sublocation within the city, state, country, and country code
    of the location of the image.

.. option:: "IPTC:Headline" : string

    Any headline that is meant to accompany the image.

.. option:: "IPTC:Provider" : string

    The provider of the image, or credit line.

.. option:: "IPTC:Source" : string

    The source of the image.

.. option:: "IPTC:Contact" : string

    The contact information for the image (possibly including name, address,
    email, etc.).

.. option:: "IPTC:CaptionWriter" : string

    The name of the person who wrote the caption or description of the
    image.

.. option:: "IPTC:JobID" : string
            "IPTC:MasterDocumentID" : string
            "IPTC:ShortDocumentID" : string
            "IPTC:UniqueDocumentID" : string
            "IPTC:OwnerID" : string

    Various identification tags.

.. option:: "IPTC:Prefs" : string
            "IPTC:ClassifyState" : string
            "IPTC:SimilarityIndex" : string

    Who knows what the heck these are?

.. option:: "IPTC:DocumentNotes" : string

    Notes about the image or document.

.. option:: "IPTC:DocumentHistory" : string

    The history of the image or document.


SMPTE metadata
==============

.. option:: "smpte:TimeCode" : int[2]

    SMPTE time code, encoded as an array of 2 32-bit integers (as a
    `TypeDesc`, it will be tagged with vecsemantics `TIMECODE`).

.. option:: "smpte:KeyCode" : int[7]

    SMPTE key code, encoded as an array of 7 32-bit integers (as a
    `TypeDesc`, it will be tagged with vecsemantics `KEYCODE`).




Extension conventions
=====================

To avoid conflicts with other plugins, or with any additional standard
metadata names that may be added in future verions of OpenImageIO, it is
strongly advised that writers of new plugins should prefix their metadata
with the name of the format, much like the `"Exif:"` and `"IPTC:"` metadata.

