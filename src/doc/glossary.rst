.. _chap-glossary:

Glossary
########


.. glossary::

  Channel

    One of several data values persent in each pixel. Examples include red,
    green, blue, alpha, etc.  The data in one channel of a pixel may be
    represented by a single number, whereas the pixel as a whole requires
    one number for each channel.

  Client

    A client (as in "client application") is a program or library that uses
    OpenImageIO or any of its constituent libraries.

  Data format

    The representation used to store a piece of data. Examples include 8-bit
    unsigned integers, 32-bit floating-point numbers, etc.

  Image File Format

    The specification and data layout of an image on disk.  For example,
    TIFF, JPEG/JFIF, OpenEXR, etc.

  Image Format Plugin

    A DSO/DLL that implements the ImageInput and ImageOutput classes for a
    particular image file format.

  Format Plugin

    See :term:`image format plugin`.

  Metadata

    Data about data.  As used in OpenImageIO, this means Information about
    an image, beyond describing the values of the pixels themselves.
    Examples include the name of the artist that created the image, the date
    that an image was scanned, the camera settings used when a photograph
    was taken, etc.

  Multi-part image / multi-part file

    This is what OpenEXR calls a file containing multiple subimages.

  Native data format

    The *data format* used in the disk file representing an image. Note that
    with OpenImageIO, this may be different than the data format used by an
    application to store the image in the computer's RAM.

  Pixel

    One pixel element of an image, consisting of one number describing each
    :term:`channel` of data at a particular location in an image.

  Plugin

    See :term:`image format plugin`.

  Scanline

    A single horizontal row of pixels of an image.  See also :term:`tile`.

  Scanline Image

    An image whose data layout on disk is organized by breaking the image up
    into horizontal scanlines, typically with the ability to read or write
    an entire scanline at once.  See also :term:`tiled image`.

  Subimage

    Some image file formats allow the storage of multiple images in each
    file. These are called *subimages* in OpenImageIO. Note that in OpenEXR,
    these are called :term:`multi-part` files.

  Thumbnail

    A small, lower-resolution preview image that is stored in an image file,
    in addition to the main image that the file represents. Not all image
    file formats support thumbnails.

  Tile

    A rectangular region of pixels of an image.  A rectangular tile is more
    spatially coherent than a scanline that stretches across the entire
    image --- that is, a pixel's neighbors are most likely in the same tile,
    whereas a pixel in a scanline image will typically have most of its
    immediate neighbors on different scanlines (requiring additional
    scanline reads in order to access them).

  Tiled Image

    An image whose data layout on disk is organized by breaking the image up
    into rectangular regions of pixels called tiles.  All the pixels
    in a tile can be read or written at once, and individual tiles may be
    read or written separately from other tiles.

  Volume Image

    A 3-D set of pixels that has not only horizontal and vertical
    dimensions, but also a "depth" dimension.
