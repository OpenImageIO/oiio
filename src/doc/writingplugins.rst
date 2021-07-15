Writing ImageIO Plugins
#######################


Plugin Introduction
========================

As explained in Chapters :ref:`chap-imageinput` and :ref:`chap-imageoutput`,
the ImageIO library does not know how to read or write any particular image
formats, but rather relies on plugins located and loaded dynamically at
run-time.  This set of plugins, and therefore the set of image file formats
that OpenImageIO or its clients can read and write, is extensible without
needing to modify OpenImageIO itself.

This chapter explains how to write your own OpenImageIO plugins.  We will
first explain separately how to write image file readers and writers, then
tie up the loose ends of how to build the plugins themselves.

Image Reader Plugins
========================

A plugin that reads a particular image file format must implement a
*subclass* of ImageInput (described in Chapter :ref:`chap-imageinput`).
This is actually very straightforward and consists of the following steps,
which we will illustrate with a real-world example of writing a JPEG/JFIF
plug-in.

#. Read the base class definition from :file:`imageio.h`.  It may also be
   helpful to enclose the contents of your plugin in the same namespace that
   the OpenImageIO library uses::

       #include <OpenImageIO/imageio.h>
       OIIO_PLUGIN_NAMESPACE_BEGIN

       // ... everything else ...

       OIIO_PLUGIN_NAMESPACE_END

#. Declare these public items:

    a. An integer called ``name_imageio_version`` that identifies
       the version of the ImageIO protocol implemented by the plugin,
       defined in :file:`imageio.h` as the constant ``OIIO_PLUGIN_VERSION``.
       This allows the library to be sure it is not loading a plugin
       that was compiled against an incompatible version of OpenImageIO.
    #. An function named ``name_imageio_library_version`` that identifies
       the underlying dependent library that is responsible for reading or
       writing the format (it may return ``nullptr`` to indicate that there is
       no dependent library being used for this format).
    #. A function named ``name_input_imageio_create`` that
       takes no arguments and returns an ``ImageInput *``
       constructed from a new instance of your ImageInput subclass and a
       deleter. (Note that *name* is the name of your format,
       and must match the name of the plugin itself.)
    #. An array of ``char *`` called ``name_input_extensions``
       that contains the list of file extensions that are likely to indicate
       a file of the right format.  The list is terminated by a ``nullptr``.

    All of these items must be inside an ``extern "C"`` block in order to
    avoid name mangling by the C++ compiler, and we provide handy macros
    ``OIIO_PLUGIN_EXPORTS_BEGIN`` and ``OIIO_PLUGIN_EXPORTS_END`` to make
    this easy.  Depending on your compiler, you may need to use special
    commands to dictate that the symbols will be exported in the DSO; we
    provide a special ``OIIO_EXPORT`` macro for this purpose, defined in
    :file:`export.h`.

    Putting this all together, we get the following for our JPEG example::

        OIIO_PLUGIN_EXPORTS_BEGIN
            OIIO_EXPORT int jpeg_imageio_version = OIIO_PLUGIN_VERSION;
            OIIO_EXPORT ImageInput *jpeg_input_imageio_create () {
                return new JpgInput;
            }
            OIIO_EXPORT const char *jpeg_input_extensions[] = {
                "jpg", "jpe", "jpeg", "jif", "jfif", "jfi", nullptr
            };
            OIIO_EXPORT const char* jpeg_imageio_library_version () {
              #define STRINGIZE2(a) #a
              #define STRINGIZE(a) STRINGIZE2(a)
              #ifdef LIBJPEG_TURBO_VERSION
                return "jpeg-turbo " STRINGIZE(LIBJPEG_TURBO_VERSION);
              #else
                return "jpeglib " STRINGIZE(JPEG_LIB_VERSION_MAJOR) "."
                        STRINGIZE(JPEG_LIB_VERSION_MINOR);
              #endif
            }
        OIIO_PLUGIN_EXPORTS_END

#. The definition and implementation of an ImageInput subclass for this file
   format.  It must publicly inherit ImageInput, and must overload the
   following methods which are "pure virtual" in the ImageInput base class:

   a. ``format_name()`` should return the name of the format, which ought to
      match the name of the plugin and by convention is strictly lower-case
      and contains no whitespace.
   #. ``open()`` should open the file and return true, or should return false
      if unable to do so (including if the file was found but turned out not
      to be in the format that your plugin is trying to implement).
   #. ``close()`` should close the file, if open.
   #. ``read_native_scanline()`` should read a single scanline from the file
      into the address provided, uncompressing it but keeping it in its naive
      data format without any translation.
   #. The virtual destructor, which should ``close()`` if the file is still
      open, addition to performing any other tear-down activities.

   Additionally, your ImageInput subclass may optionally choose to overload
   any of the following methods, which are defined in the ImageInput base
   class and only need to be overloaded if the default behavior is not
   appropriate for your plugin:

   f. ``supports()``, only if your format supports any of the optional
      features described in the section describing `ImageInput::supports`.
   #. ``valid_file()``, if your format has a way to determine if a file is
      of the given format in a way that is less expensive than a full
      ``open()``.
   #. ``seek_subimage()``, only if your format supports reading multiple
      subimages within a single file.
   #. ``read_native_scanlines()``, only if your format has a speed advantage
      when reading multiple scanlines at once.  If you do not supply this
      function, the default implementation will simply call
      ``read_scanline()`` for each scanline in the range.
   #. ``read_native_tile()``, only if your format supports reading tiled
      images.
   #. ``read_native_tiles()``, only if your format supports reading tiled
      images and there is a speed advantage when reading multiple tiles at
      once.  If you do not supply this function, the default implementation
      will simply call ``read_native_tile()`` for each tile in the range.
   #. ``Channel subset'' versions of ``read_native_scanlines()`` and/or
      ``read_native_tiles()``, only if your format has a more efficient
      means of reading a subset of channels.  If you do not supply these
      methods, the default implementation will simply use
      ``read_native_scanlines()`` or ``read_native_tiles()`` to read into a
      temporary all-channel buffer and then copy the channel subset into the
      user's buffer.
   #. ``read_native_deep_scanlines()`` and/or ``read_native_deep_tiles()``,
      only if your format supports "deep" data images.

   Here is how the class definition looks for our JPEG example.  Note that
   the JPEG/JFIF file format does not support multiple subimages or tiled
   images.
 
   .. code-block::

        class JpgInput final : public ImageInput {
         public:
            JpgInput () { init(); }
            virtual ~JpgInput () { close(); }
            virtual const char * format_name (void) const override { return "jpeg"; }
            virtual bool open (const std::string &name, ImageSpec &spec) override;
            virtual bool read_native_scanline (int y, int z, void *data) override;
            virtual bool close () override;
         private:
            FILE *m_fd;
            bool m_first_scanline;
            struct jpeg_decompress_struct m_cinfo;
            struct jpeg_error_mgr m_jerr;

            void init () { m_fd = NULL; }
        };


Your subclass implementation of ``open()``, ``close()``, and
``read_native_scanline()`` are the heart of an ImageInput implementation.
(Also ``read_native_tile()`` and ``seek_subimage()``, for those image
formats that support them.)

The remainder of this section simply lists the full implementation of our
JPEG reader, which relies heavily on the open source :file:`jpeg-6b` library
to perform the actual JPEG decoding.

.. include:: ../jpeg.imageio/jpeginput.cpp
    :code: cpp




Image Writers
========================

A plugin that writes a particular image file format must implement a
*subclass* of ImageOutput (described in Chapter :ref:`chap-imageoutput`).
This is actually very straightforward and consists of the following steps,
which we will illustrate with a real-world example of writing a JPEG/JFIF
plug-in.

#. Read the base class definition from :file:`imageio.h`, just as
   with an image reader (see Section `Image Reader Plugins`_).

#. Declare four public items:

   a. An integer called ``name_imageio_version`` that identifies
      the version of the ImageIO protocol implemented by the plugin, defined
      in :file:`imageio.h` as the constant ``OIIO_PLUGIN_VERSION``. This
      allows the library to be sure it is not loading a plugin that was
      compiled against an incompatible version of OpenImageIO. Note that if
      your plugin has both a reader and writer and they are compiled as
      separate modules (C++ source files), you don't want to declare this in
      *both* modules; either one is fine.
   #. A function named ``name_output_imageio_create`` that takes no
      arguments and returns an ``ImageOutput *`` constructed from a new
      instance of your ImageOutput subclass and a deleter.  (Note that
      *name* is the name of your format, and must match the name of the
      plugin itself.)
   #. An array of ``char *`` called ``name_output_extensions``
      that contains the list of file extensions that are likely to indicate
      a file of the right format.  The list is terminated by a ``nullptr``
      pointer.

   All of these items must be inside an ``extern "C"`` block in order to
   avoid name mangling by the C++ compiler, and we provide handy macros
   ``OIIO_PLUGIN_EXPORTS_BEGIN`` and ``OIIO_PLUGIN_EXPORTS_END`` to mamke
   this easy.  Depending on your compiler, you may need to use special
   commands to dictate that the symbols will be exported in the DSO; we
   provide a special ``OIIO_EXPORT`` macro for this purpose, defined in
   :file:`export.h`.

   Putting this all together, we get the following for our JPEG example::

       OIIO_PLUGIN_EXPORTS_BEGIN
           OIIO_EXPORT int jpeg_imageio_version = OIIO_PLUGIN_VERSION;
           OIIO_EXPORT ImageOutput *jpeg_output_imageio_create () {
               return new JpgOutput;
           }
           OIIO_EXPORT const char *jpeg_input_extensions[] = {
               "jpg", "jpe", "jpeg", nullptr
           };
       OIIO_PLUGIN_EXPORTS_END

#. The definition and implementation of an ImageOutput subclass for
   this file format.  It must publicly inherit ImageOutput, and must
   overload the following methods which are "pure virtual" in the
   ImageOutput base class:

   a. ``format_name()`` should return the name of the format, which ought
      to match the name of the plugin and by convention is strictly
      lower-case and contains no whitespace.
   #. ``supports()`` should return `true` if its argument names a feature
      supported by your format plugin, `false` if it names a feature not
      supported by your plugin.  See the description of
      `ImageOutput::supports()` for the list of feature names.
   #. ``open()`` should open the file and return true, or should return
      false if unable to do so (including if the file was found but turned
      out not to be in the format that your plugin is trying to implement).
   #. ``close()`` should close the file, if open.
   #. ``write_scanline()`` should write a single scanline to the file,
      translating from internal to native data format and handling strides
      properly.
   #. The virtual destructor, which should ``close()`` if the file is
      still open, addition to performing any other tear-down activities.
  
   Additionally, your ImageOutput subclass may optionally choose to
   overload any of the following methods, which are defined in the
   ImageOutput base class and only need to be overloaded if the default
   behavior is not appropriate for your plugin:

   g. ``write_scanlines()``, only if your format supports writing scanlines
      and you can get a performance improvement when outputting multiple
      scanlines at once.  If you don't supply ``write_scanlines()``, the
      default implementation will simply call ``write_scanline()``
      separately for each scanline in the range.
   #. ``write_tile()``, only if your format supports writing tiled images.
   #. ``write_tiles()``, only if your format supports writing tiled images
      and you can get a performance improvement when outputting multiple
      tiles at once.  If you don't supply ``write_tiles()``, the default
      implementation will simply call ``write_tile()`` separately for each
      tile in the range.
   #. ``write_rectangle()``, only if your format supports
      writing arbitrary rectangles.
   #. ``write_image()``, only if you have a more clever method of doing so
      than the default implementation that calls ``write_scanline()`` or
      ``write_tile()`` repeatedly.
   #. ``write_deep_scanlines()`` and/or ``write_deep_tiles()``, only if
      your format supports "deep" data images.

  It is not strictly required, but certainly appreciated, if a file format
  does not support tiles, to nonetheless accept an ImageSpec that specifies
  tile sizes by allocating a full-image buffer in ``open()``, providing an
  implementation of ``write_tile()`` that copies the tile of data to the
  right spots in the buffer, and having ``close()`` then call
  ``write_scanlines()`` to process the buffer now that the image has been
  fully sent.

  Here is how the class definition looks for our JPEG example.  Note that
  the JPEG/JFIF file format does not support multiple subimages or tiled
  images.

  .. code-block::

      class JpgOutput final : public ImageOutput {
       public:
          JpgOutput () { init(); }
          virtual ~JpgOutput () { close(); }
          virtual const char * format_name (void) const override { return "jpeg"; }
          virtual int supports (string_view property) const override { return false; }
          virtual bool open (const std::string &name, const ImageSpec &spec,
                             bool append=false) override;
          virtual bool write_scanline (int y, int z, TypeDesc format,
                                       const void *data, stride_t xstride) override;
          bool close ();
       private:
          FILE *m_fd;
          std::vector<unsigned char> m_scratch;
          struct jpeg_compress_struct m_cinfo;
          struct jpeg_error_mgr m_jerr;
  
          void init () { m_fd = NULL; }
      };

Your subclass implementation of ``open()``, ``close()``, and 
``write_scanline()`` are the heart of an ImageOutput implementation.
(Also ``write_tile()``, for those image formats that support tiled
output.)

An ImageOutput implementation must properly handle all data formats and
strides passed to ``write_scanline()`` or ``write_tile()``, unlike
an ImageInput implementation, which only needs to read scanlines or
tiles in their native format and then have the super-class handle the
translation.  But don't worry, all the heavy lifting can be accomplished
with the following helper functions provided as protected member
functions of ImageOutput that convert a scanline, tile, or rectangular
array of values from one format to the native format(s) of the file.

.. cpp:function:: const void* to_native_scanline (TypeDesc format, const void *data, stride_t xstride, std::vector<unsigned char>& scratch, unsigned int dither=0, int yorigin=0, int zorigin=0)

    Convert a full scanline of pixels (pointed to by ``data`` with the given
    ``format`` and strides into contiguous pixels in the native format
    (described by the ImageSpec returned by the ``spec()`` member function).
    The location of the newly converted data is returned, which may either
    be the original ``data`` itself if no data conversion was necessary and
    the requested layout was contiguous (thereby avoiding unnecessary memory
    copies), or may point into memory allocated within the ``scratch``
    vector passed by the user.  In either case, the caller doesn't need to
    worry about thread safety or freeing any allocated memory (other than
    eventually destroying the scratch vector).

.. cpp:function:: const void *  to_native_tile (TypeDesc format, const void *data, stride_t xstride, stride_t ystride, stride_t zstride, std::vector<unsigned char>& scratch, unsigned int dither=0, int xorigin=0, int yorigin=0, int zorigin=0)

    Convert a full tile of pixels (pointed to by ``data`` with the given
    ``format`` and strides into contiguous pixels in the native format
    (described by the ImageSpec returned by the ``spec()`` member function).
    The location of the newly converted data is returned, which may either
    be the original ``data`` itself if no data conversion was necessary and
    the requested layout was contiguous (thereby avoiding unnecessary memory
    copies), or may point into memory allocated within the ``scratch``
    vector passed by the user.  In either case, the caller doesn't need to
    worry about thread safety or freeing any allocated memory (other than
    eventually destroying the scratch vector).


.. cpp:function::  const void* to_native_rectangle (int xbegin, int xend, int ybegin, int yend, int zbegin, int zend, TypeDesc format, const void* data, stride_t xstride, stride_t ystride, stride_t zstride, std::vector<unsigned char>& scratch, unsigned int dither=0,   int xorigin=0, int yorigin=0, int zorigin=0)

    Convert a rectangle of pixels (pointed to by ``data`` with the given
    ``format``, dimensions, and strides into contiguous pixels in the
    native format (described by the ImageSpec returned by the ``spec()``
    member function).  The location of the newly converted data is returned,
    which may either be the original ``data`` itself if no data
    conversion was necessary and the requested layout was contiguous
    (thereby avoiding unnecessary memory copies), or may point into memory
    allocated within the ``scratch`` vector passed by the user.  In
    either case, the caller doesn't need to worry about thread safety or
    freeing any allocated memory (other than eventually destroying the
    scratch vector).


For `float` to 8 bit integer conversions only, if ``dither`` parameter is
nonzero, random dither will be added to reduce quantization banding
artifacts; in this case, the specific nonzero ``dither`` value is used as a
seed for the hash function that produces the per-pixel dither amounts, and
the optional ``origin`` parameters help it to align the pixels to the right
position in the dither pattern.


|

The remainder of this section simply lists the full implementation of
our JPEG writer, which relies heavily on the open source ``jpeg-6b``
library to perform the actual JPEG encoding.

.. include:: ../jpeg.imageio/jpegoutput.cpp
    :code: cpp


Tips and Conventions
========================

OpenImageIO's main goal is to hide all the pesky details of individual file
formats from the client application.  This inevitably leads to various
mismatches between a file format's true capabilities and requests that may
be made through the OpenImageIO APIs.  This section outlines conventions,
tips, and rules of thumb that we recommend for image file support.

**Readers**

* If the file format stores images in a non-spectral color space (for
  example, YUV), the reader should automatically convert to RGB to pass
  through the OIIO APIs.  In such a case, the reader should signal the
  file's true color space via a ``"Foo:colorspace"`` attribute in the
  ImageSpec.
* "Palette" images should be automatically converted by the reader to RGB.
* If the file supports thumbnail images in its header, the reader should
  provide an ``ImageInput::get_thumbnail()`` method, as well as store the
  thumbnail dimensions in the ImageSpec as attributes ``"thumbnail_width"``,
  ``"thumbnail_height"``, and ``"thumbnail_nchannels"`` (all of which should
  be `int`).

**Writers**

The overall rule of thumb is: try to always "succeed" at writing the file,
outputting the closest approximation of the user's data as possible.  But it
is permissible to fail the ``open()`` call if it is clearly nonsensical or
there is no possible way to output a decent approximation of the user's
data.  Some tips:

* If the client application requests a data format not directly supported by
  the file type, silently write the supported data format that will result
  in the least precision or range loss.
* It is customary to fail a call to ``open()`` if the ImageSpec requested a
  number of color channels plainly not supported by the file format.  As an
  exception to this rule, it is permissible for a file format that does not
  support alpha channels to silently drop the fourth (alpha) channel of a
  4-channel output request.
* If the app requests a ``"Compression"`` not supported by the file format,
  you may choose as a default any lossless compression supported.  Do not
  use a lossy compression unless you are fairly certain that the app wanted
  a lossy compression.
* If the file format is able to store images in a non-spectral color space
  (for example, YUV), the writer may accept a ``"Foo:colorspace"`` attribute
  in the ImageSpec as a request to automatically convert and store the data
  in that format (but it will always be passed as RGB through the OIIO
  APIs).
* If the file format can support thumbnail images in its header, and the
  ImageSpec contain attributes ``"thumbnail_width"``,
  ``"thumbnail_height"``, ``"thumbnail_nchannels"``, and
  ``"thumbnail_image"``, the writer should attempt to store the thumbnail if
  possible.

