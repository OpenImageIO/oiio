.. _chap-pythonbindings:

Python Bindings
###############


Overview
========

OpenImageIO provides Python language bindings for much of its functionality.

You must ensure that the environment variable `PYTHONPATH` includes the
:program:`python` subdirectory of the OpenImageIO installation.


A Python program must import the `OpenImageIO` package:

.. code-block:: python

    import OpenImageIO

In most of our examples below, we assume that for the sake
of brevity, we will alias the package name as follows:

.. code-block:: python

    import OpenImageIO as oiio
    from OIIO import ImageInput, ImageOutput
    from OIIO import ImageBuf, ImageSpec, ImageBufAlgo


.. _sec-pythontypedesc:

TypeDesc
========

The `TypeDesc` class that describes data types of pixels and metadata,
described in detail in Section :ref:`sec-typedesc`, is replicated for Python.

.. py:class:: BASETYPE

    The `BASETYPE` enum corresponds to the C++ `TypeDesc::BASETYPE` and
    contains the following values::

        UNKNOWN NONE UINT8 INT8 UINT16 INT16 UINT32 INT32 UINT64 INT64
        HALF FLOAT DOUBLE STRING PTR
    
    These names are also exported to the `OpenImageIO` namespace.


.. py:class:: AGGREGATE

    The `AGGREGATE` enum corresponds to the C++ `TypeDesc::AGGREGATE` and
    contains the following values::

        SCALAR VEC2 VEC3 VEC4 MATRIX33 MATRIX44
    
    These names are also exported to the `OpenImageIO` namespace.


.. py:class:: VECSEMANTICS

    The `VECSEMANTICS` enum corresponds to the C++ `TypeDesc::VECSEMANTICS` and
    contains the following values::

        NOSEMANTICS COLOR POINT VECTOR NORMAL TIMECODE KEYCODE RATIONAL
    
    These names are also exported to the `OpenImageIO` namespace.


.. py::method:: TypeDesc.TypeDesc(typename='unknown')

    Construct a `TypeDesc` object the easy way: from a string description.
    If the type name is omitted, it will default to`UNKNOWN`.

    Example:

    .. code-block:: python

        import OpenImageIO as oiio

        # make a default (UNKNOWN) TypeDesc
        t = TypeDesc()

        # make a TypeDesc describing an unsigned 8 bit int
        t = TypeDesc("uint8")

        # make a TypeDesc describing an array of 14 'float' values
        t = TypeDesc("float[14]")

        # make a TypeDesc describing 3-vector with point semantics
        t = TypeDesc("point")



.. py::method:: TypeDesc.TypeDesc(basetype=oiio.UNKNOWN, aggregate=oiio.SCALAR, vecsemantics=NOSEMANTICS, arraylen=0)

    Construct a `TypeDesc` object the hard way: from individual enum tokens
    describing the base type, aggregate class, semantic hints, and array length.

    Example:

    .. code-block:: python

        import OpenImageIO as oiio

        # make a default (UNKNOWN) TypeDesc
        t = TypeDesc()

        # make a TypeDesc describing an unsigned 8 bit int
        t = TypeDesc(oiio.UINT8)

        # make a TypeDesc describing an array of 14 'float' values
        t = TypeDesc(oiio.FLOAT, oiio.SCALAR, oiio.NOSEMANTICS, 14)

        # make a TypeDesc describing a float point
        t = TypeDesc(oiio.FLOAT, oiio.VEC3, oiio.POINT)



.. py:data:: TypeUnknown TypeString TypeFloat TypeHalf
             TypeInt TypeUInt TypeInt16 TypeUInt16
             TypeColor TypePoint TypeVector TypeNormal
             TypeFloat2 TypeVector2 TypeFloat4 TypeVector2i
             TypeMatrix TypeMatrix33
             TypeTimeCode TypeKeyCode TypeRational TypePointer

    Pre-constructed `TypeDesc` objects for some common types, available in the
    outer OpenImageIO scope.

    Example:

    .. code-block:: python

        t = TypeFloat



.. py:function:: str (typedesc)

    Returns a string that describes the `TypeDesc`.

    Example:

    .. code-block:: python

        print (str(TypeDesc(oiio.UINT16)))

        > int16



.. py:attribute:: TypeDesc.basetype
                  TypeDesc.aggregate
                  TypeDesc.vecsemantics
                  TypeDesc.arraylen

    Access to the raw fields in the `TypeDesc`.

    Example:

    .. code-block:: python

        t = TypeDesc(...)
        if t.basetype == oiio.FLOAT :
            print ("It's made of floats")



.. py:method:: int TypeDesc.size ()
               int TypeDesc.basesize ()
               TypeDesc TypeDesc.elementtype ()
               int TypeDesc.numelements ()
               int TypeDesc.elementsize ()

    The `size()` is the size in bytes, of the type described.  The
    `basesize()` is the size in bytes of the `BASETYPE`.

    The `elementtype()` is the type of each array element, if it is an
    array, or just the full type if it is not an array.  The `elementsize()`
    is the size, in bytes, of the `elementtype` (thus, returning the same
    value as `size()` if the type is not an array).  The `numelements()`
    method returns `arraylen` if it is an array, or 1 if it is not an array.

    Example:

    .. code-block:: python

        t = TypeDesc("point[2]")
        print "size =", t.size()
        print ("elementtype =", t.elementtype())
        print ("elementsize =", t.elementsize())

        > size = 24
        > elementtype = point
        > elementsize = 12



.. py:method:: typedesc == typedesc
               typedesc != typedesc
               TypeDesc.equivalent(typedesc)

    Test for equality or inequality.  The `equivalent()` method is more
    forgiving than `==`, in that it considers `POINT`, `VECTOR`, and
    `NORMAL` vector semantics to not constitute a difference from one
    another.

    Example:

    .. code-block:: python

        f = TypeDesc("float")
        p = TypeDesc("point")
        v = TypeDesc("vector")
        print ("float==point?", (f == p))
        print ("vector==point?", (v == p))
        print ("float.equivalent(point)?", f.equivalent(p))
        print ("vector.equivalent(point)?", v.equivalent(p))

        > float==point? False
        > vector==point? False
        > float.equivalent(point)? False
        > vector.equivalent(point)? True



.. _sec-pythonroi:

ROI
===

The ROI class that describes an image extent or region of interest,
explained in deail in Section :ref:`sec-ROI`, is replicated for Python.

.. py:method:: ROI()
               ROI(xbegin, xend, ybegin, yend, zbegin=0, zend=1, chbegin=0, chend=1000)

    Construct an ROI with the given bounds.  The constructor with no
    arguments makes an ROI that is "undefined."

    Example:

    .. code-block:: python

        roi = ROI (0, 640, 0, 480, 0, 1, 0, 4)   # video res RGBA



.. py:attribute:: ROI.xbegin
                  ROI.xend
                  ROI.ybegin
                  ROI.yend
                  ROI.zbegin
                  ROI.zend
                  ROI.chbegin
                  ROI.chend

    The basic fields of the ROI (all of type `int`).


.. py:attribute:: ROI.All

    A pre-constructed undefined ROI understood to mean unlimited ROI on
    an image.


.. py:attribute:: ROI.defined

    `True` if the ROI is defined, `False` if the ROI is undefined.


.. py:attribute:: ROI.width
                  ROI.height
                  ROI.depth
                  ROI.nchannels

    The number of pixels in each dimension, and the number of channels,
    as described by the ROI. (All of type `int`.)


.. py:attribute:: int ROI.npixels

    The total number of pixels in the region described by the ROI (as an
    `int`).


.. py:method:: ROI.contains (x, y, z=0, ch=0)

    Returns `True` if the ROI contains the coordinate.


.. py:method:: ROI.contains (other)

    Returns `True` if the ROI `other` is entirel contained within
    this ROI.


.. py:function:: ROI get_roi (imagespec)
                 ROI get_roi_full (imagespec)

    Returns an ROI corresponding to the pixel data window of the given
    ImageSpec, or the display/full window, respectively.

    Example:

    .. code-block:: python

        spec = ImageSpec(...)
        roi = oiio.get_roi(spec)



.. py:function:: set_roi (imagespec, roi)
                 set_roi_full (imagespec, roi)

    Alter the ImageSpec's resolution and offset to match the passed ROI.

    Example:

    .. code-block:: python

        # spec is an ImageSpec
        # The following sets the full (display) window to be equal to the
        # pixel data window:
        oiio.set_roi_full (spec, oiio.get_roi(spec))

|


.. _sec-pythonimagespec:

ImageSpec
=========

The ImageSpec class that describes an image, explained in deail in
Section :ref:`sec-ImageSpec`, is replicated for Python.

.. py:method:: ImageSpec ()
               ImageSpec (typedesc)
               ImageSpec (xres, yres, nchannels, typedesc)
               ImageSpec (roi, typedesc)

    Constructors of an ImageSpec. These correspond directly to the constructors
    in the C++ bindings.

    Example:

    .. code-block:: python

        import OpenImageIO as oiio
        ...

        # default ctr
        s = ImageSpec()

        # construct with known pixel type, unknown resolution
        s = ImageSpec(oiio.UINT8)

        # construct with known resolution, channels, pixel data type
        s = ImageSpec(640, 480, 4, "half")

        # construct from an ROI
        s = ImageSpec (ROI(0,640,0,480,0,1,0,3), TypeFloat)



.. py:attribute:: ImageSpec.width, ImageSpec.height, ImageSpec.depth
                  ImageSpec.x, ImageSpec.y, ImageSpec.z

    Resolution and offset of the image data (`int` values).

    Example:

    .. code-block:: python

        s = ImageSpec (...)
        print ("Data window is ({},{})-({},{})".format (s.x, s.x+s.width-1,
                                                        s.y, s.y+s.height-1))



.. py:attribute:: ImageSpec.full_width, ImageSpec.full_height, ImageSpec.full_depth
                  ImageSpec.full_x, ImageSpec.full_y, ImageSpec.full_z

    Resolution and offset of the "full" display window (`int` values).


.. py:attribute:: ImageSpec.tile_width, ImageSpec.tile_height, ImageSpec.tile_depth

    For tiled images, the resolution of the tiles (`int` values).  Will be
    0 for  untiled images.


.. py:attribute:: ImageSpec.format

    A `TypeDesc` describing the pixel data.


.. py:attribute:: ImageSpec.nchannels

    An `int` giving the number of color channels in the image.


.. py:attribute:: ImageSpec.channelnames

    A tuple of strings containing the names of each color channel.


.. py:attribute:: ImageSpec.channelformats

    If all color channels have the same format, that will be
    `ImageSpec.format`, and `channelformats` will be `None`.  However, if
    there are different formats per channel, they will be stored in
    `channelformats` as a tuple of `TypeDesc` objects.
    
    Example:

    .. code-block:: python

        if spec.channelformats == None:
            print ("All color channels are", str(spec.format))
        else:
            print ("Channel formats: ")
            for t in spec.channelformats:
                print ("\t", t)



.. py:attribute:: ImageSpec.alpha_channel
                  ImageSpec.z_channel

    The channel index containing the alpha or depth channel, respectively, or
    -1 if either one does not exist or cannot be identified.


.. py:attribute:: ImageSpec.deep

    `True` if the image is a *deep* (multiple samples per pixel) image, of
    `False` if it is an ordinary image.


.. py:attribute:: ImageSpec.extra_attribs

    Direct access to the `extra_attribs` named metadata, appropriate for
    iterating over the entire list rather than searching for a particular
    named value.


    - `len(extra_attribs)` : Returns the number of extra attributes.
    
    - `extra_attribs[i].name` : The name of the indexed attribute.
    
    - `extra_attribs[i].type` : The type of the indexed attribute, as a `TypeDesc`.
    
    - `extra_attribs[i].value` : The value of the indexed attribute.

    Example:

    .. code-block:: python

        s = ImageSpec(...)
        ...
        print ("extra_attribs size is", len(s.extra_attribs))
        for i in range(len(s.extra_attribs)) :
            print (i, s.extra_attribs[i].name, str(s.extra_attribs[i].type), " :")
            print ("\t", s.extra_attribs[i].value)
        print



.. py:attribute:: Imagespec.roi

    The ROI describing the pixel data window.


.. py:attribute:: ImageSpec.roi_full

    The ROI describing the "display window" (or "full size").


.. py:method:: ImageSpec.set_format (typedesc)

    Given a `TypeDesc`, sets the `format` field and clear any per-channel
    formats in `channelformats`.

    Example:

    .. code-block:: python

        s = ImageSpec ()
        s.set_format (TypeDesc("uint8"))



.. py:method:: ImageSpec.default_channel_names ()

    Sets `channel_names` to the default names given the value of
    the `nchannels` field.


.. py:method:: ImageSpec.channelindex (name)

    Return (as an int) the index of the channel with the given name, or -1
    if it does not exist.


.. py:method:: ImageSpec.channel_bytes ()

    ImageSpec.channel_bytes (channel, native=False)} Returns the size of a
    single channel value, in bytes (as an `int`). (Analogous to the C++
    member functions, see Section :ref:`sec-ImageSpec` for details.)


.. py:method:: ImageSpec.pixel_bytes ()
               ImageSpec.pixel_bytes (native=False)
               ImageSpec.pixel_bytes (chbegin, chend, native=False)

    Returns the size of a pixel, in bytes (as an `int`). (Analogous to the
    C++ member functions, see Section :ref:`sec-ImageSpec`  for details.)


.. py:method:: ImageSpec.scanline_bytes (native=False)
               ImageSpec.tile_bytes (native=False)
               ImageSpec.image_bytes (native=False)

    Returns the size of a scanline, tile, or the full image, in bytes (as an
    `int`). (Analogous to the C++ member functions, see Section
    :ref:`sec-ImageSpec`  for details.)


.. py:method:: ImageSpec.tile_pixels ()
               ImageSpec.image_pixels ()

    Returns the number of pixels in a tile or the full image, respectively
    (as an `int`). (Analogous to the C++ member functions, see Section
    :ref:`sec-ImageSpec`  for details.)


.. py:method:: ImageSpec.erase_attribute (name, searchtype=TypeUnknown, casesensitive=False)

    Remove any specified attributes matching the regular expression `name`
    from the list of extra_attribs.


.. py:method:: ImageSpec.attribute (name, int)
               ImageSpec.attribute (name, float)
               ImageSpec.attribute (name, string)
               ImageSpec.attribute (name, typedesc, data)

    Sets a metadata value in the `extra_attribs`.  If the metadata item
    is a single `int`, `float`, or `string`, you can pass it
    directly. For other types, you must pass the `TypeDesc` and then the
    data (for aggregate types or arrays, pass multiple values as a tuple).

    Example:

    .. code-block:: python

        s = ImageSpec (...)
        s.attribute ("foo_str", "blah")
        s.attribute ("foo_int", 14)
        s.attribute ("foo_float", 3.14)
        s.attribute ("foo_vector", TypeDesc.TypeVector, (1, 0, 11))
        s.attribute ("foo_matrix", TypeDesc.TypeMatrix,
                     (1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1))



.. py:method:: ImageSpec.getattribute (name)
               ImageSpec.getattribute (name, typedesc)

    Retrieves a named metadata value from `extra_attribs`.  The generic
    `getattribute()` function returns it regardless of type, or `None` if
    the attribute does not exist.  The typed variety will only succeed if
    the attribute is actually of that type specified.

    Example:

    .. code-block:: python

        foo = s.getattribute ("foo")   # None if not found
        foo = s.getattribute ("foo", oiio.FLOAT)  # None if not found AND float



.. py:method:: ImageSpec.get_int_attribute (name, defaultval=0)
                  ImageSpec.get_float_attribute (name, defaultval=0.0)
                  ImageSpec.get_string_attribute (name, defaultval="")

    Retrieves a named metadata value from `extra_attribs`, if it is
    found and is of the given type; returns the default value (or a passed
    value) if not found.

    Example:

    .. code-block:: python

        # If "foo" is not found, or if it's not an int, return 0
        foo = s.get_int_attribute ("foo")

        # If "foo" is not found, or if it's not a string, return "blah"
        foo = s.get_string_attribute ("foo", "blah")



.. py:attribute:: ImageSpec[name]

    *NEW in 2.1*

    Retrieve or set metadata using a dictionary-like syntax, rather than
    `attribute()` and `getattribute()`. This is best illustrated by
    example:

    .. code-block:: python

        comp = spec["Compression"]
        # Same as:  comp = spec.getattribute("Compression")

        spec["Compression"] = comp
        # Same as: spec.attribute("Compression", comp)



.. py:method:: ImageSpec.metadata_val (paramval, human=False)

    For a ParamValue, format its value as a string.


.. py:method:: ImageSpec.serialize (format="text", verbose="Detailed")

    Return a string containing the serialization of the ImageSpec. The
    `format` may be either "text" or "XML". The `verbose` may be one of
    "brief", "detailed", or "detailedhuman".


.. py:method:: ImageSpec.to_xml ()

    Equivalent to `serialize("xml", "detailedhuman")`.


.. py:method:: ImageSpec.from_xml (xml)

    Initializes the ImageSpec from the information in the string `xml`
    containing an XML-serialized ImageSpec.


.. py:method:: ImageSpec.channel_name (chan)

    Returns a string containing the name of the channel with index `chan`.


.. py:method:: ImageSpec.channelindex (name)

    Return the integer index of the channel with the given `name`, or -1 if
    the name is not a name of one of the channels.


.. py:method:: ImageSpec.channelformat (chan)

    Returns a `TypeDesc` of the channel with index `chan`.


.. py:method:: ImageSpec.get_channelformats ()

    Returns a tuple containing all the channel formats.


.. py:method:: ImageSpec.valid_tile_range (xbegin, xend, ybegin, yend, zbegin, zend)

    Returns `True` if the given tile range exactly covers a set of tiles, or
    `False` if it isn't (or if the image is not tiled).


.. py:method:: ImageSpec.copy_dimensions (other)

    Copies from ImageSpec `other` only the fields describing the size and
    data types, but not the arbitrary named metadata or channel names.


.. py:method:: ImageSpec.undefined ()

    Returns `True` for a newly initialized (undefined) ImageSpec.


|

Example: Header info
--------------------

Here is an illustrative example of the use of ImageSpec, a working Python
function that opens a file and prints all the relevant header information:

.. code-block:: python

    #!/usr/bin/env python
    import OpenImageIO as oiio

    # Print the contents of an ImageSpec
    def print_imagespec (spec, subimage=0, mip=0) :
        if spec.depth <= 1 :
            print ("  resolution %dx%d%+d%+d" % (spec.width, spec.height, spec.x, spec.y))
        else :
            print ("  resolution %dx%d%x%d+d%+d%+d" %
                   (spec.width, spec.height, spec.depth, spec.x, spec.y, spec.z))
        if (spec.width != spec.full_width or spec.height != spec.full_height
            or spec.depth != spec.full_depth) :
            if spec.full_depth <= 1 :
                print ("  full res   %dx%d%+d%+d" %
                       (spec.full_width, spec.full_height, spec.full_x, spec.full_y))
            else :
                print ("  full res   %dx%d%x%d+d%+d%+d" %
                       (spec.full_width, spec.full_height, spec.full_depth,
                        spec.full_x, spec.full_y, spec.full_z))
        if spec.tile_width :
            print ("  tile size  %dx%dx%d" %
                   (spec.tile_width, spec.tile_height, spec.tile_depth))
        else :
            print "  untiled"
        if mip >= 1 :
            return
        print "  " + str(spec.nchannels), "channels:", spec.channelnames
        print "  format = ", str(spec.format)
        if len(spec.channelformats) > 0 :
            print "  channelformats = ", spec.channelformats
        print "  alpha channel = ", spec.alpha_channel
        print "  z channel = ", spec.z_channel
        print "  deep = ", spec.deep
        for i in spec.extra_attribs) :
            if type(i.value) == str :
                print " ", i.name, "= \"" + i.value + "\""
            else :
                print " ", i.name, "=", i.value


    def poor_mans_iinfo (filename) :
        input = ImageInput.open (filename)
        if not input :
            print 'Could not open "' + filename + '"'
            print "\tError: ", oiio.geterror()
            return
        print 'Opened "' + filename + '" as a ' + input.format_name()
        sub = 0
        mip = 0
        while True :
            if sub > 0 or mip > 0 :
                print "Subimage", sub, "MIP level", mip, ":"
            print_imagespec (input.spec(), mip=mip)
            mip = mip + 1
            if input.seek_subimage (sub, mip) :
                continue    # proceed to next MIP level
            else :
                sub = sub + 1
                mip = 0
                if input.seek_subimage (sub, mip) :
                    continue    # proceed to next subimage
            break  # no more MIP levels or subimages
        input.close ()



.. _sec-pythondeepdata:

DeepData
========

The DeepData class describing "deep" image data (multiple depth
sample per pixel), which is explained in deail in
Section :ref:`sec-imageinput-deepdata`, is replicated for Python.

.. py:method:: DeepData ()

    Constructs a DeepData object. It needs to have its `init()` and
    `alloc()` methods called before it can hold any meaningful data.

.. py:method:: DeepData.init (npixels, nchannels, channeltypes, channelnames)

    Initializes this DeepData to hold `npixels` total pixels, with
    `nchannels` color channels. The data types of the channels are
    described by `channeltypes`, a tuple of `TypeDesc` values (one per
    channel), and the names are provided in a tuple of `string`s
    `channelnames`. After calling `init`, you still need to set the number of
    samples for each pixel (using `set_nsamples`) and then call `alloc()`
    to actually allocate the sample memory.

.. py:method:: DeepData.initialized ()

    Returns `True` if the DeepData is initialized at all.

.. py:method:: DeepData.allocated ()

    Returns `True` if the DeepData has already had pixel memory allocated.

.. py:attribute:: DeepData.pixels

    This `int` field constains the total number of pixels in this collection
    of deep data.

.. py:attribute:: DeepData.channels

    This `int` field constains the number of channels.

.. py:attribute:: DeepData.A_channel
                  DeepData.AR_channel
                  DeepData.AG_channel
                  DeepData.AB_channel
                  DeepData.Z_channel
                  DeepData.Zback_channel

    The channel index of certain named channels, or -1 if they don't exist.
    For `AR_channel`, `AG_channel`, `AB_channel`, if they don't exist, they
    will contain the value of `A_channel`, and `Zback_channel` will contain
    the value of `z_channel` if there is no actual `Zback`.

.. py:method:: DeepData.channelname (c)

    Retrieve the name of channel `C`, as a `string`.

.. py:method:: DeepData.channeltype (c)

    Retrieve the data type of channel `C`, as a `TypeDesc`.

.. py:method:: DeepData.channelsize (c)

    Retrieve the size (in bytes) of one datum of channel `C`.

.. py:method:: DeepData.samplesize ()

    Retrieve the packed size (in bytes) of all channels of one sample.

.. py:method:: DeepData.set_samples (pixel, nsamples)

    Set the number of samples for a given pixel (specified by integer
    index).

.. py:method:: DeepData.samples (pixel)

    Get the number of samples for a given pixel (specified by integer
    index).

.. py:method:: DeepData.insert_samples (pixel, samplepos, n)

    Insert *n* samples starting at the given position of an indexed pixel.

.. py:method:: DeepData.erase_samples (pixel, samplepos, n)

    Erase *n* samples starting at the given position of an indexed pixel.

.. py:method:: DeepData.set_deep_value (pixel, channel, sample, value)

    Set specific float value of a given pixel, channel, and sample index.

.. py:method:: DeepData.set_deep_value_uint (pixel, channel, sample, value)

    Set specific unsigned int value of a given pixel, channel, and sample
    index.

.. py:method:: DeepData.deep_value (pixel, channel, sample, value)

    Retrieve the specific value of a given pixel, channel, and sample index
    (for float channels.

.. py:method:: DeepData.deep_value_uint (pixel, channel, sample)

    Retrieve the specific value of a given pixel, channel, and sample index
    (for uint channels).

.. py:method:: DeepData.copy_deep_sample (pixel, sample, src, srcpixel, srcsample)

    Copy a deep sample from DeepData `src` into this DeepData.

.. py:method:: DeepData.copy_deep_pixel (pixel, src, srcpixel)

    Copy a deep pixel from DeepData `src` into this DeepData.

.. py:method:: DeepData.split (pixel, depth)

    Split any samples of the pixel that cross `depth`. Return `True` if any
    splits occurred, `False` if the pixel was unmodified.

.. py:method:: DeepData.sort (pixel)

    Sort the samples of the pixel by their Z depth.

.. py:method:: DeepData.merge_overlaps (pixel)

    Merge any adjacent samples in the pixel that exactly overlap in *z*
    range. This is only useful if the pixel has previously been split at
    all sample starts and ends, and sorted by depth.

.. py:method:: DeepData.merge_deep_pixels (pixel, src, srcpixel)

    Merge the samples of `src`'s pixel into this DeepData's pixel.

.. py:method:: DeepData.occlusion_cull (pixel)

    Eliminate any samples beyond an opaque sample.

.. py:method:: DeepData.opaque_z (pixel)

    For the given pixel index. return the *z* value at which the pixel reaches
    full opacity.




.. _sec-pythonimageinput:

ImageInput
==========

See Chapter :ref:`chap-imageinput` for detailed explanations of the C++
ImageInput class APIs. The Python APIs are very similar. The biggest
difference is that in C++, the various `read_*` functions write the pixel
values into an already-allocated array that belongs to the caller, whereas
the Python versions allocate and return an array holding the pixel values
(or `None` if the read failed).


.. py:method:: ImageInput.open (filename [, config_imagespec])

    Creates an ImageInput object and opens the named file.  Returns the open
    ImageInput upon success, or `None` if it failed to open the file (after
    which, `OpenImageIO.geterror()` will contain an error message).  In the
    second form, the optional ImageSpec argument `config` contains
    attributes that may set certain options when opening the file.
    
    Example:

    .. code-block:: python

        input = ImageInput.open ("tahoe.jpg")
        if input == None :
            print "Error:", oiio.geterror()
            return

.. py:method:: ImageInput.close ()

    Closes an open image file, returning `True` if successful, `False`
    otherwise.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        ...
        input.close ()

.. py:method:: ImageInput.format_name ()

    Returns the format name of the open file, as a string.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        if input :
            print filename, "was a", input.format_name(), "file."
            input.close ()


.. py:method:: ImageInput.spec ()

    Returns an ImageSpec corresponding to the currently open subimage and
    MIP level of the file.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        spec = input.spec()
        print "resolution ", spec.width, "x", spec.height

.. py:method:: ImageInput.spec (subimage, miplevel=0)

    Returns a full copy of the ImageSpec corresponding to the designated
    subimage and MIP level.

.. py:method:: ImageSpec ImageInput.spec_dimensions (subimage, miplevel=0)

    Returns a partial copy of the ImageSpec corresponding to the designated
    subimage and MIP level, only copying the dimension fields and not any of
    the arbitrary named metadata (and is thus much less expensive).

.. py:method:: ImageInput.current_subimage ()

    Returns the current subimage of the file.

.. py:method:: ImageInput.current_miplevel ()

    Returns the current MIP level of the file.

.. py:method:: ImageInput.seek_subimage (subimage, miplevel)

    Repositions the file pointer to the given subimage and MIP level within
    the file (starting with 0).  This function returns `True` upon success,
    `False` upon failure (which may include the file not having the
    specified subimage or MIP level).
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        mip = 0
        while True :
            ok = input.seek_subimage (0, mip)
            if not ok :
                break
            spec = input.spec()
            print "MIP level", mip, "is", spec.width, "x", spec.height



.. py:method:: ImageInput.read_image (format='float')
               ImageInput.read_image (chbegin, chend, format='float')
               ImageInput.read_image (subimage, miplevel, chbegin, chend, format='float')

    Read the entire image and return the pixels as a NumPy array of values
    of the given `format` (described by a `TypeDesc` or a string, float by
    default). If the `format` is `unknown`, the pixels will be returned in
    the native format of the file. If an error occurs, `None` will be
    returned.
    
    For a normal (2D) image, the array returned will be 3D indexed as
    `[y][x][channel]`. For 3D volumetric images, the array returned will be
    4D with shape indexed as `[z][y][x][channel]`.

    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        spec = input.spec ()
        pixels = input.read_image ()
        print "The first pixel is", pixels[0][0]
        print "The second pixel is", pixels[0][1]
        input.close ()


.. py:method:: ImageInput.read_scanline (y, z, format="float")

    Read scanline number `y` from depth plane `z` from the open file,
    returning the pixels as a NumPy array of values of the given `type`
    (described by a `TypeDesc` or a string, float by default). If the `type`
    is `TypeUnknown`, the pixels will be returned in the native format of
    the file. If an error occurs, `None` will be returned.
    
    The pixel array returned will be a 2D `ndarray`, indexed as `[x][channel]`.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        spec = input.spec ()
        if spec.tile_width == 0 :
            for y in range(spec.y, spec.y+spec.height) :
                pixels = input.read_scanline (y, spec.z, "float")
                # process the scanline
        else :
            print "It's a tiled file"
        input.close ()


.. py:method:: ImageInput.read_tile (x, y, z, format="float")

    Read the tile whose upper left corner is pixel (x,y,z) from the open
    file, returning the pixels as a NumPy array of values of the given
    `type` (described by a `TypeDesc` or a string, float by default). If the
    `type` is `TypeUnknown`, the pixels will be returned in the native
    format of the file. If an error occurs, `None` will be returned.
    
    For a normal (2D) image, the array of tile pixels returned will be a 3D
    `ndarray` indexed as `[y][x][channel]`. For 3D volumetric images, the
    array returned will be 4D with shape indexed as `[z][y][x][channel]`.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        spec = input.spec ()
        if spec.tile_width > 0 :
            for z in range(spec.z, spec.z+spec.depth, spec.tile_depth) :
                for y in range(spec.y, spec.y+spec.height, spec.tile_height) :
                    for x in range(spec.x, spec.x+spec.width, spec.tile_width) :
                        pixels = input.read_tile (x, y, z, oiio.FLOAT)
                        # process the tile
        else :
            print "It's a scanline file"
        input.close ()



.. py:method:: ImageInput.read_scanlines(subimage, miplevel, ybegin, yend, z, chbegin, chend, format="float")
               ImageInput.read_scanlines(ybegin, yend, z, chbegin, chend, format="float")
               ImageInput.read_tiles(xbegin, xend, ybegin, yend, zbegin, zend, chbegin, chend, format="float")
               ImageInput.read_tiles(subimage, miplevel, xbegin, xend, ybegin, yend, zbegin, zend, format="float")

    Similar to the C++ routines, these functions read multiple scanlines or
    tiles at once, which in some cases may be more efficient than reading
    each scanline or tile separately.  Additionally, they allow you to read only
    a subset of channels.
    
    For normal 2D images, both `read_scanlines` and `read_tiles` will
    return a 3D array indexed as `[z][y][x][channel]`.
    
    For 3D volumetric images, both `read_scanlines` will return a 3D array
    indexed as `[y][x][channel]`, and `read_tiles` will return a 4D
    array indexed as `[z][y][x][channel]`,
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        spec = input.spec ()
    
        # Read the whole image, the equivalent of
        #     pixels = input.read_image (type)
        # but do it using read_scanlines or read_tiles:
        if spec.tile_width == 0 :
            pixels = input.read_scanlines (spec.y, spec.y+spec.height, 0,
                                           0, spec.nchannels)
        else :
            pixels = input.read_tiles (spec.x, spec.x+spec.width,
                                       spec.y, spec.y+spec.height,
                                       spec.z, spec.z+spec.depth,
                                       0, spec.nchannels)
    

.. py:method:: ImageInput.read_native_deep_scanlines (subimage, miplevel, ybegin, yend, z, chbegin, chend)
               ImageInput.read_native_deep_tiles (subimage, miplevel, xbegin, xend, ybegin, yend, zbegin, zend, chbegin, chend)
               ImageInput.read_native_deep_image (subimage=0, miplevel=0)

    Read a collection of scanlines, tiles, or an entire image of "deep"
    pixel data from the specified subimage and MIP level. The begin/end
    coordinates are all integer values. The value returned will be a
    DeepData if the read succeeds, or `None` if the read fails.

    These methods are guaranteed to be thread-safe against simultaneous
    calls to any of the other other `read_native` calls that take an
    explicit subimage/miplevel.


.. py:method:: ImageOutput.get_thumbnail(subimage: int = 0) -> ImageBuf

    Retrieve an ImageBuf containing reduced-resolution("thumbnail") version
    of the image. If no thumbnail could be retrieved, an empty ImageBuf
    will be returned.


.. py:method:: ImageInput.geterror (clear = True)

    Retrieves the error message from the latest failed operation on an
    ImageInput.
    
    Example:

    .. code-block:: python

        input = ImageInput.open (filename)
        if not input :
            print "Open error:", oiio.geterror()
            # N.B. error on open must be retrieved with the global geterror(),
            # since there is no ImageInput object!
        else :
            pixels = input.read_image (oiio.FLOAT)
            if not pixels :
                print "Read_image error:", input.geterror()
            input.close ()


|

Example: Reading pixel values from a file to find min/max
---------------------------------------------------------

.. code-block:: python

    #!/usr/bin/env python 
    import OpenImageIO as oiio
    
    def find_min_max (filename) :
        input = ImageInput.open (filename)
        if not input :
            print 'Could not open "' + filename + '"'
            print "\tError: ", oiio.geterror()
            return
        spec = input.spec()
        nchans = spec.nchannels
        pixels = input.read_image()
        if not pixels :
            print "Could not read:", input.geterror()
            return
        input.close()    # we're done with the file at this point
        minval = pixels[0][0]   # initialize to the first pixel value
        maxval = pixels[0][0]
        for y in range(spec.height) :
            for x in range(spec.width) :
                p = pixels[y][x]
                for c in range(nchans) :
                    if p[c] < minval[c] :
                        minval[c] = p[c]
                    if p[c] > maxval[c] :
                        maxval[c] = p[c]
        print "Min values per channel were", minval
        print "Max values per channel were", maxval

|



.. _sec-pythonimageoutput:

ImageOutput
===========

See Chapter :ref:`chap-imageoutput` for detailed explanations of the C++
ImageOutput class APIs. The Python APIs are very similar.

.. py:method:: ImageOutput.create (name, plugin_searchpath="")

    Create a new ImageOutput capable of writing the named file format (which
    may also be a file name, with the type deduced from the extension).
    There is an optional parameter giving an colon-separated search path for
    finding ImageOutput plugins.  The function returns an ImageOutput
    object, or `None` upon error (in which case, `OpenImageIO.geterror()`
    may be used to retrieve the error message).
    
    Example:

    .. code-block:: python

        import OpenImageIO as oiio
        output = ImageOutput.create ("myfile.tif")
        if not output :
            print "Error:", oiio.geterror()



.. py:method:: ImageOutput.format_name ()

    The file format name of a created ImageOutput, as a string.
    
    Example:

    .. code-block:: python

        output = ImageOutput.create (filename)
        if output :
            print "Created output", filename, "as a", output.format_name()



.. py:method:: ImageOutput.supports (feature)

    For a created ImageOutput, returns `True` if the file format supports
    the named feature (such as "tiles", "mipmap", etc., see Section
    :ref:`sec-imageoutput-class-reference` for the full list), or `False` if
    this file format does not support the feature.

    Example:

    .. code-block:: python

        output = ImageOutput.create (filename)
        if output :
            print output.format_name(), "supports..."
            print "tiles?", output.supports("tiles")
            print "multi-image?", output.supports("multiimage")
            print "MIP maps?", output.supports("mipmap")
            print "per-channel formats?", output.supports("channelformats")


.. py:method:: ImageOutput.open (filename, spec, mode="Create")

    Opens the named output file, with an ImageSpec describing the image to
    be output.  The `mode` may be one of "Create", "AppendSubimage", or
    "AppendMIPLevel". See Section :ref:`sec-imageoutput-class-reference` for
    details. Returns `True` upon success, `False` upon failure (error
    messages retrieved via `ImageOutput.geterror()`.)

    :return: `True` for success, `False` for failure.

    Example:

    .. code-block:: python

        output = ImageOutput.create (filename)
        if not output :
            print "Error:", oiio.geterror()
        spec = ImageSpec (640, 480, 3, "uint8")
        ok = output.open (filename, spec)
        if not ok :
            print "Could not open", filename, ":", output.geterror()


.. py:method:: ImageOutput.open (filename, (imagespec, ...))

    This variety of `open()` is used specifically for multi-subimage files. A
    tuple of ImageSpec objects is passed, one for each subimage that will be
    written to the file.  After each subimage is written, then a regular call to
    `open(name, newspec, AppendSubimage)` moves on to the next subimage.

    :return: `True` for success, `False` for failure.

.. py:method:: ImageOutput.close ()

    Closes an open output.

    :return: `True` for success, `False` for failure.

.. py:method:: ImageOutput.spec ()

    Returns the ImageSpec of the currently-open output image.


.. py:method:: ImageOutput.write_image (pixels)

    Write the currently opened image all at once.  The `pixels` parameter
    should be a Numpy `ndarray` containing data elements indexed as
    `[y][x][channel]` for normal 2D images, or for 3D volumetric images,
    as `[z][y][x][channel]`, in other words, exactly matching the shape of
    array returned by `ImageInput.read_image()`. (It will also work fine if
    the array is 1D "flattened" version, as long as it contains the correct
    total number of values.) The data type is deduced from the contents of the
    array itself. Returns `True` upon success, `False` upon failure.
    
    Example:

    .. code-block:: python

        # This example reads a scanline file, then converts it to tiled
        # and writes to the same name.
    
        input = ImageInput.open (filename)
        spec = input.spec ()
        pixels = input.read_image ()
        input.close ()
    
        output = ImageOutput.create (filename)
        if output.supports("tiles") :
            spec.tile_width = 64
            spec.tile_height = 64
            output.open (filename, spec)
            output.write_image (pixels)
            output.close ()


.. py:method:: ImageOutput.write_scanline (y, z, pixels)
               ImageOutput.write_scanlines (ybegin, yend, z, pixels)

    Write one or many scanlines to the currently open file. Returns `True`
    upon success, `False` upon failure.
    
    The `pixels` parameter should be a Numpy `ndarray` containing data
    elements indexed as `[x][channel]` for `write_scanline` or as
    `[y][x][channels` for `write_scanlines`, exactly matching the shape
    returned by `ImageInput.read_scanline` or `ImageInput.read_scanlines`.
    (It will also work fine if the array is 1D "flattened" version, as long
    as it contains the correct total number of values.)
    
    Example:

    .. code-block:: python

        # Copy a TIFF image to JPEG by copying scanline by scanline.
        input = ImageInput.open ("in.tif")
        spec = input.spec ()
        output = ImageOutput.create ("out.jpg")
        output.open (filename, spec)
        for z in range(spec.z, spec.z+spec.depth) :
            for y in range(spec.y, spec.y+spec.height) :
                pixels = input.read_scanline (y, z)
                output.write_scanline (y, z, pixels)
        output.close ()
        input.close ()
    
        # The same example, but copying a whole "plane" of scanlines at a time:
        ...
        for z in range(spec.z, spec.z+spec.depth) :
            pixels = input.read_scanlines (spec.y, spec.y+spec.height, z)
            output.write_scanlines (spec.y, spec.y+spec.height, z, pixels)
        ...



.. py:method:: ImageOutput.write_tile(x, y, z, pixels)
               ImageOutput.write_tiles(xbegin, xend, ybegin, yend, zbegin, zend, pixels)

    Write one or many tiles to the currently open file. Returns `True` upon
    success, `False` upon failure.
    
    The `pixels` parameter should be a Numpy `ndarray` containing data
    elements indexed as `[y][x][channel]` for normal 2D images, or as
    `[z][y][x][channels` 3D volumetric images, exactly matching the shape
    returned by `ImageInput.read_tile` or `ImageInput.read_tiles`. (It will
    also work fine if the array is 1D "flattened" version, as long as it
    contains the correct total number of values.)
    
    Example:

    .. code-block:: python

        input = ImageInput.open (in_filename)
        spec = input.spec ()
        output = ImageOutput.create (out_filename)
        output.open (out_filename, spec)
        for z in range(spec.z, spec.z+spec.depth, spec.tile_depth) :
            for y in range(spec.y, spec.y+spec.height, spec.tile_height) :
                for x in range(spec.x, spec.x+spec.width, spec.tile_width) :
                    pixels = input.read_tile (x, y, z)
                    output.write_tile (x, y, z, pixels)
        output.close ()
        input.close ()
    
        # The same example, but copying a whole row of of tiles at a time:
        ...
        for z in range(spec.z, spec.z+spec.depth, spec.tile_depth) :
            for y in range(spec.y, spec.y+spec.height, spec.tile_height) :
                pixels = input.read_tiles (spec.x, spec.x+spec.width,
                                           y, y+tile_width, z, z+tile_width)
                output.write_tiles (spec.x, spec.x+spec.width,
                                    y, y+tile_width, z, z+tile_width, pixels)
        ...



.. py:method:: ImageOutput.write_deep_scanlinesa(ybegin, yend, z, deepdata)
               ImageOutput.write_deep_tiles(xbegin, xend, ybegin, yend, zbegin, zend, deepdata)
               ImageOutput.write_deep_image(deepdata)

    Write a collection of scanlines, tiles, or an entire image of "deep"
    pixel data. The begin/end coordinates are all integer values, and
    `deepdata` should be a DeepData.


.. py:method:: ImageOutput.set_thumbnail(thumb : ImageBuf)

    Specify a reduced-resolution ("thumbnail") version of the image, as an
    ImageBuf. Note that many image formats may require the thumbnail to be
    specified prior to writing the pixels. Return `True` for success, or
    `False` if a thumbnail could not be set.

.. py:method:: ImageOutput.copy_image (imageinput)

    Copy the current image of the open input to the open output. (The reason
    this may be preferred in some circumstances is that, if input and
    output were the same kind of input file format, they may have a special
    efficient technique to copy pixels unaltered, for example by avoiding the 
    decompression/recompression round trip.)
    
    Example:

    .. code-block:: python

        input = ImageInput.open (in_filename)
        spec = input.spec ()
        output = ImageOutput.create (out_filename)
        output.open (filename, spec)
        output.copy_image (input)
        output.close ()
        input.close ()


.. py:method:: ImageOuput.geterror (clear = True)

    Retrieves the error message from the latest failed operation on an open
    file.
    
    Example:

    .. code-block:: python

        output = ImageOutput.create (filename)
        if not output :
            print "Create error:", oiio.geterror()
            # N.B. error on create must be retrieved with the global geterror(),
            # since there is no ImageOutput object!
        else :
            ok = output.open (filename, spec)
            if not ok :
                print "Open error:", output.geterror()
            ok = output.write_image (pixels)
            if not ok :
                print "Write error:", output.geterror()
            output.close ()




.. _sec-pythonimagebuf:

ImageBuf
========

See Chapter :ref:`chap-imagebuf` for detailed explanations of the C++
ImageBuf class APIs. The Python APIs are very similar.

.. py:method:: ImageBuf ()

Construct a new, empty ImageBuf. The ImageBuf is uninitialized and is
awaiting a call to `reset()` or `copy()` before it is useful.


.. py:method:: ImageBuf (filename [, subimage, miplevel])

    Construct a read-only ImageBuf that will read from the named file.
    Optionally, a specific subimage or MIP level may be specified
    (defaulting to 0).
    
    Example:

    .. code-block:: python

        import OpenImageIO as oiio
        ...
        buf = ImageBuf ("grid.tif")



.. py:method:: ImageBuf (filename, subimage, miplevel, config)

    Construct a read-only ImageBuf that will read from the named file,
    with an ImageSpec `config` giving configuration hints.
    
    Example:

    .. code-block:: python

        import OpenImageIO as oiio
        ...
        config = ImageSpec()
        config.attribute("oiio:RawColor", 1)
        buf = ImageBuf ("grid.tif", 0, 0, config)



.. py:method:: ImageBuf (imagespec, zero = True)

    Construct a writable ImageBuf of the dimensions and data format
    specified by an ImageSpec. The pixels will be initialized to black/empty
    values if `zero` is True, otherwise the pixel values will remain
    uninitialized.
    
    Example:

    .. code-block:: python

        spec = ImageSpec (640, 480, 3, "float")
        buf = ImageBuf (spec)



.. py:method:: ImageBuf.clear ()

    Resets the ImageBuf to a pristine state identical to that of a freshly
    constructed ImageBuf using the default constructor.
    
    Example:

    .. code-block:: python

        buf = ImageBuf (...)
    
        # The following two commands are equivalent:
        buf = ImageBuf()     # 1 - assign a new blank ImageBuf
        buf.clear()          # 2 - clear the existing ImageBuf



.. py:method:: ImageBuf.reset (filename, subimage=0, miplevel=0, config=ImageSpec())

    Restore the ImageBuf to a newly-constructed state, to read from a
    filename (optionally specifying a subimage, MIP level, and/or a
    "configuration" ImageSpec).


.. py:method:: ImageBuf.reset (imagespec, zero = True)

    Restore the ImageBuf to the newly-constructed state of a writable
    ImageBuf specified by an ImageSpec. The pixels will be iniialized to
    black/empty if `zero` is True, otherwise the pixel values will remain
    uninitialized.


.. py:method:: ImageBuf.read(subimage=0, miplevel=0, force=False, convert=oiio.UNKNOWN)
               ImageBuf.read(subimage, miplevel, chbegin, chend, force, convert)

    Explicitly read the image from the file (of a file-reading ImageBuf),
    optionally specifying a particular subimage, MIP level, and channel
    range.  If `force` is `True`, will force an allocation of memory and a
    full read (versus the default of relying on an underlying ImageCache).
    If `convert` is not the default of`UNKNOWN`, it will force the ImageBuf
    to convert the image to the specified data format (versus keeping it in
    the native format or relying on the ImageCache to make a data formatting
    decision).
    
    Note that a call to `read()` is not necessary --- any ImageBuf API call
    that accesses pixel values will trigger a file read if it has not yet
    been done. An explicit `read()` is generally only needed to change the
    subimage or miplevel, or to force an in-buffer read or format
    conversion.
    
    The `read()` method will return `True` for success, or `False` if the
    read could not be performed (in which case, a `geterror()` call will
    retrieve the specific error message).
    
    Example:

    .. code-block:: python

        buf = ImageBuf ("mytexture.exr")
        buf.read (0, 2, True)
        # That forces an allocation and read of MIP level 2



.. py:method:: ImageBuf.init_spec (filename, subimage=0, miplevel=0)

    Explicitly read just the header from a file-reading ImageBuf (if the
    header has not yet been read), optionally specifying a particular
    subimage and MIP level. The `init_spec()` method will return `True` for
    success, or `False` if the read could not be performed (in which case, a
    `geterror()` call will retrieve the specific error message).

    Note that a call to `init_spec()` is not necessary --- any ImageBuf API
    call that accesses the spec will read it automatically it has not yet
    been done.


.. py:method:: ImageBuf.write (filename, dtype="", fileformat="")

    Write the contents of the ImageBuf to the named file.  Optionally,
    `dtype` can override the pixel data type (by default, the pixel data
    type of the buffer), and  `fileformat` can specify a particular file
    format to use (by default, it will infer it from the extension of the
    file name).
    
    Example:

    .. code-block:: python

        # No-frills conversion of a TIFF file to JPEG
        buf = ImageBuf ("in.tif")
        buf.write ("out.jpg")
    
        # Convert to uint16 TIFF
        buf = ImageBuf ("in.exr")
        buf.write ("out.tif", "uint16")



.. py:method:: ImageBuf.write (imageoutput)

    Write the contents of the ImageBuf as the next subimage to an open
    ImageOutput.

    Example:

    .. code-block:: python

        buf = ImageBuf (...)   # Existing ImageBuf

        out = ImageOutput.create("out.exr")
        out.open ("out.exr", buf.spec())

        buf.write (out)
        out.close()



.. py:method:: ImageBuf.make_writable (keep_cache_type = False)

    Force the ImageBuf to be writable. That means that if it was previously
    backed by an ImageCache (storage was `IMAGECACHE`), it will force a full
    read so that the whole image is in local memory.


.. py:method:: ImageBuf.set_write_format (format=oiio.UNKNOWN)
               ImageBuf.set_write_tiles (width=0, height=0, depth=0)

    Override the data format or tile size in a subsequent call to `write()`.
    The `format` argument to `set_write_format` may be either a single
    data type description for all channels, or a tuple giving the data type for
    each channel in order.
    
    Example:

    .. code-block:: python

        # Conversion to a tiled unsigned 16 bit integer file
        buf = ImageBuf ("in.tif")
        buf.set_write_format ("uint16")
        buf.set_write_tiles (64, 64)
        buf.write ("out.tif")



.. py:method:: ImageBuf.spec()
               ImageBuf.nativespec()

    `ImageBuf.spec()` returns the ImageSpec that describes the contents of
    the ImageBuf.  `ImageBuf.nativespec()` returns an ImageSpec that
    describes the contents of the file that the ImageBuf was read from (this
    may differ from `ImageBuf.spec()` due to format conversions, or any
    changes made to the ImageBuf after the file was read, such as adding
    metadata).
    
    Handy rule of thumb: `spec()` describes the buffer, `nativespec()`
    describes the original file it came from.
    
    Example:

    .. code-block:: python

        buf = ImageBuf ("in.tif")
        print "Resolution is", buf.spec().width, "x", buf.spec().height



.. py:method:: ImageBuf.specmod()

    `ImageBuf.specmod()` provides a reference to the writable ImageSpec
    inside the ImageBuf.  Be very careful!  It is safe to modify certain
    metadata, but if you change the data format or resolution fields, you
    will get the chaos you deserve.
    
    Example:

    .. code-block:: python

        # Read an image, add a caption metadata, write it back out in place
        buf = ImageBuf ("file.tif")
        buf.specmod().attribute ("ImageDescription", "my photo")
        buf.write ("file.tif")


.. py:attribute:: ImageBuf.has_thumbnail

    Contains `True` if the ImageBuf contains thumbnail data, otherwise
    `False`.

.. py:method:: ImageBuf.clear_thumbnail()

    Remove any associated thumbnail from this buffer.

.. py:method:: ImageBuf.set_thumbnail(thumb: ImageBuf)

    Set the thumbnai associated with this buffer to `thumb`.

.. py:method:: ImageBuf.thumbnail() -> ImageBuf

    Return an ImageBuf holding any thumbnail associated with this buffer.

.. py:attribute:: ImageBuf.name

    The file name of the image (as a string).

.. py:attribute::  ImageBuf.file_format_name

    The file format of the image (as a string).


.. py:attribute:: ImageBuf.subimage
                  ImageBuf.miplevel
                  ImageBuf.nsubimages
                  ImageBuf.nmiplevels

    Several fields giving information about the current subimage and MIP
    level, and the total numbers thereof in the file.


.. py:attribute:: ImageBuf.xbegin
               ImageBuf.xend
               ImageBuf.ybegin
               ImageBuf.yend
               ImageBuf.zbegin
               ImageBuf.zend

    The range of valid pixel data window. Remember that the `end` is *one
    past* the last pixel.


.. py:attribute:: ImageBuf.xmin
                  ImageBuf.xmax
                  ImageBuf.ymin
                  ImageBuf.ymax
                  ImageBuf.zmin
                  ImageBuf.zmax

    The minimum and maximum (inclusive) coordinates of the pixel data window.


.. py:attribute:: ImageBuf.orientation
                  ImageBuf.oriented_width
                  ImageBuf.oriented_height
                  ImageBuf.oriented_x
                  ImageBuf.oriented_y
                  ImageBuf.oriented_full_width
                  ImageBuf.oriented_full_height
                  ImageBuf.oriented_full_x
                  ImageBuf.oriented_full_y

    The `Orientation` field gives the suggested display oriententation of
    the image (see Section :ref:`sec-metadata-orientation`).

    The other fields are helpers that give the width, height, and origin
    (as well as "full" or "display" resolution and origin), taking the
    intended orientation into consideration.


.. py:attribute:: ImageBuf.roi
                  ImageBuf.roi_full

    These fields hold an ROI description of the pixel data window
    (`roi`) and the full (a.k.a. "display") window (`roi_full`).

    Example:

    .. code-block:: python

        buf = ImageBuf ("tahoe.jpg")
        print "Resolution is", buf.roi.width, "x", buf.roi.height



.. py:method:: ImageBuf.set_origin (x, y, z=0)

    Changes the "origin" of the data pixel data window to the specified
    coordinates.
    
    Example:

    .. code-block:: python

        # Shift the pixel data so the upper left is at pixel (10, 10)
        buf.set_origin (10, 10)



.. py:method:: ImageBuf.set_full (roi)

    Changes the "full" (a.k.a. "display") window to the specified ROI.
    
    Example:

    .. code-block:: python

        newroi = ROI (0, 1024, 0, 768)
        buf.set_full (newroi)



.. py:attribute:: ImageBuf.pixels_valid

    Will be `True` if the file has already been read and the pixels are
    valid. (It is always `True` for writable ImageBuf's.) There should be
    few good reasons to access these, since the spec and pixels will be
    automatically be read when they are needed.


.. py:method:: ImageBuf.pixeltype

    Returns a TypeDesc describing the data type of the pixels stored within
    the ImageBuf.


.. py:method:: ImageBuf.copy_metadata (other_imagebuf)

    Replaces the metadata (all ImageSpec items, except for the data format
    and pixel data window size) with the corresponding metadata from the
    other ImageBuf.


.. py:method:: ImageBuf.copy_pixels (other_imagebuf)

    Replace the pixels in this ImageBuf with the values from the other
    ImageBuf.


.. py:method:: ImageBuf ImageBuf.copy (format=TypeUnknown)

    Return a full copy of this ImageBuf (with optional data format
    conversion, if `format` is supplied).

    Example:

    .. code-block:: python

        A = ImageBuf("A.tif")
    
        # Make a separate, duplicate copy of A
        B = A.copy()
    
        # Make another copy of A, but converting to float pixels
        C = A.copy ("float")



.. py:method:: ImageBuf.copy (other_imagebuf, format=TypeUnknown)

    Make this ImageBuf a complete copy of the other ImageBuf.
    If a `format` is provided, `this` will get the specified pixel
    data type rather than using the same pixel format as the source ImageBuf.
    
    Example:

    .. code-block:: python

        A = ImageBuf("A.tif")
    
        # Make a separate, duplicate copy of A
        B = ImageBuf()
        B.copy (A)
    
        # Make another copy of A, but converting to float pixels
        C = ImageBuf()
        C.copy (A, oiio.FLOAT)



.. py:method:: ImageBuf.swap (other_imagebuf)

    Swaps the content of this ImageBuf and the other ImageBuf.

    Example:

    .. code-block:: python

        A = ImageBuf("A.tif")
        B = ImageBuf("B.tif")
        A.swap (B)
        # Now B contains the "A.tif" image and A contains the "B.tif" image



.. py:method:: tuple ImageBuf.getpixel (x, y, z=0, wrap="black")

    Retrieves pixel (x,y,z) from the buffer and return it as a tuple of
    `float` values, one for each color channel.  The `x`, `y`, `z` values
    are `int` pixel coordinates.  The optional `wrap` parameter
    describes what should happen if the coordinates are outside the pixel data
    window (and may be: "black", "clamp", "periodic", "mirror").
    
    Example:
    
    .. code-block:: python
    
        buf = ImageBuf ("tahoe.jpg")
        p = buf.getpixel (50, 50)
        print p
    
        > (0.37, 0.615, 0.97)



.. py:method:: mageBuf.getchannel (x, y, z, channel, wrap="black")

    Retrieves just a single channel value from pixel (x,y,z) from the buffer
    and returns it as a `float` value.  The optional `wrap` parameter
    describes what should happen if the coordinates are outside the pixel data
    window (and may be: "black", "clamp", "periodic", "mirror").
    
    Example:

    .. code-block:: python

        buf = ImageBuf ("tahoe.jpg")
        green = buf.getchannel (50, 50, 0, 1)



.. py:method:: ImageBuf.interppixel (x, y, wrap="black")
               ImageBuf.interppixel_bicubic (x, y, wrap="black")

    Interpolates the image value (bilinearly or bicubically) at coordinates
    $(x,y)$ and return it as a tuple of `float` values, one for each color
    channel.  The `x`, `y` values are continuous `float` coordinates in
    "pixel space."   The optional `wrap` parameter describes what should
    happen if the coordinates are outside the pixel data window (and may be:
    "black", "clamp", "periodic", "mirror").

    Example:

    .. code-block:: python

        buf = ImageBuf ("tahoe.jpg")
        midx = float(buf.xbegin + buf.xend) / 2.0
        midy = float(buf.ybegin + buf.yend) / 2.0
        p = buf.interpixel (midx, midy)
        # Now p is the interpolated value from right in the center of
        # the data window



.. py:method:: ImageBuf.interppixel_NDC (x, y, wrap="black")
               ImageBuf.interppixel_bicubic_NDC (x, y, wrap="black")

    Interpolates the image value (bilinearly or bicubically) at coordinates
    (x,y) and return it as a tuple of `float` values, one for each color
    channel.  The `x`, `y` values are continuous, normalized `float`
    coordinates in "NDC space,"" where (0,0) is the upper left corner of the
    full (a.k.a. "display") window, and (1,1) is the lower right corner of
    the full/display window. The  `wrap` parameter describes what should
    happen if the coordinates are outside the pixel data window (and may be:
    "black", "clamp", "periodic", "mirror").

    Example:

    .. code-block:: python

        buf = ImageBuf ("tahoe.jpg")
        p = buf.interpixel_NDC (0.5, 0.5)
        # Now p is the interpolated value from right in the center of
        # the display window



.. py:method:: ImageBuf.setpixel (x, y, pixel_value)
               ImageBuf.setpixel (x, y, z, pixel_value)

    Sets pixel (x,y,z) to be the `pixel_value`, expressed as a tuple of
    `float` (one for each color channel).

    Example:

    .. code-block:: python

        buf = ImageBuf (ImageSpec (640, 480, 3, oiio.UINT8))
    
        # Set the whole image to red (the dumb slow way, but it works):
        for y in range(buf.ybegin, buf.yend) :
            for x in range(buf.xbegin, buf.xend) :
                buf.setpixel (x, y, (1.0, 0.0, 0.0))



.. py:method:: ImageBuf.get_pixels (format=TypeFloat, roi=ROI.All)

    Retrieves the rectangle of pixels (and channels) specified by `roi` from
    the image and returns them as an array of values with type specified by
    `format`.
    
    As with the ImageInput read functions, the return value is a NumPy
    `ndarray` containing data elements indexed as `[y][x][channel]` for
    normal 2D images, or for 3D volumetric images, as `[z][y][x][channel]`).
    Returns `True` upon success, `False` upon failure.

    Example:

    .. code-block:: python

        buf = ImageBuf ("tahoe.jpg")
        pixels = buf.get_pixels (oiio.FLOAT)  # no ROI means the whole image



.. py:method:: ImageBuf.set_pixels (roi, data)

    Sets the rectangle of pixels (and channels) specified by `roi` with
    values in the `data`, which is a NumPy `ndarray` of values indexed as
    `[y][x][channel]` for normal 2D images, or for 3D volumetric images, as
    `[z][y][x][channel]`. (It will also work fine if the array is 1D
    "flattened" version, as long as it contains the correct total number of
    values.) The data type is deduced from the contents of the array itself.

    Example:

    .. code-block:: python

        buf = ImageBuf (...)
        pixels = (....)
        buf.set_pixels (ROI(), pixels)



.. py:attribute:: ImageBuf.has_error

    This field will be `True` if an error has occurred in the ImageBuf.

.. py:method::  ImageBuf.geterror (clear = True)

    Retrieve the error message (and clear the `has_error` flag).

    Example:

    .. code-block:: python

        buf = ImageBuf ("in.tif")
        buf.read ()   # force a read
        if buf.has_error :
            print "Error reading the file:", buf.geterror()
        buf.write ("out.jpg")
        if buf.has_error :
            print "Could not convert the file:", buf.geterror()



.. py:method:: ImageBuf.pixelindex (x, y, z, check_range=False)

    Return the index of pixel (x,y,z).


.. py:attribute:: ImageBuf.deep

    Will be `True` if the file contains "deep" pixel data, or `False` for an
    ordinary images.


.. py:method:: ImageBuf.deep_samples (x, y, z=0)

    Return the number of deep samples for pixel (x,y,z).


.. py:method:: ImageBuf.set_deep_samples (x, y, z, nsamples)

    Set the number of deep samples for pixel (x,y,z).


.. py:method:: ImageBuf.deep_insert_samples (x, y, z, samplepos, nsamples)
               ImageBuf.deep_erase_samples (x, y, z, samplepos, nsamples)

    Insert or erase `nsamples` samples starting at the given position of
    pixel (x,y,z).


.. py:method::  ImageBuf.deep_value (x, y, z, channel, sample)
                ImageBuf.deep_value_uint (x, y, z, channel, sample)

    Return the value of the given deep sample (particular pixel, channel,
    and sample number) for a channel that is a float or an unsigned integer
    type, respectively.


.. py:method:: ImageBuf.set_deep_value (x, y, z, channel, sample, value)
               ImageBuf.set_deep_value_uint (x, y, z, channel, sample, value)

    Set the value of the given deep sample (particular pixel, channel, and
    sample number) for a channel that is a float or an unsigned integer
    type, respectively.


.. py:attribute:: DeepData ImageBuf.deepdata

    A reference to the underlying `DeepData` of the image.





|

.. _sec-pythonimagebufalgo:

ImageBufAlgo
============

The C++ ImageBufAlgo functions are described in detail in Chapter
:ref:`chap-imagebufalgo`.  They are also exposed to Python. For the
majority of ImageBufAlgo functions, their use in Python is identical to C++;
in those cases, we will keep our descriptions of the Python bindings minimal
and refer you to Chapter :ref:`chap-imagebufalgo`, saving the extended
descriptions for those functions that differ from the C++ counterparts.

A few things about the paramters of the ImageBufAlgo function calls are
identical among the functions, so we will explain once here rather than
separately for each function:

* `dst` is an existing ImageBuf, which will be modified (it may be an
  uninitialized ImageBuf, but it must be an ImageBuf).
* `src` parameter is an initialized ImageBuf, which will not be modified
  (unless it happens to refer to the same image as `dst`.
* `roi`, if supplied, is an `roi` specifying a region of interst over which
  to operate. If omitted, the region will be the entire size of the source
  image(s).
* `nthreads` is the maximum number of threads to use. If not supplied, it
  defaults to 0, meaning to use as many threads as hardware cores available.

Just as with the C++ ImageBufAlgo functions, if `dst` is an uninitialized
ImageBuf, it will be sized to reflect the roi (which, in turn, if undefined,
will be sized to be the union of the ROI's of the source images).

.. _sec-iba-py-patterns:

Pattern generation
------------------

.. py:method:: ImageBuf ImageBufAlgo.zero (roi, nthreads=0)
               ImageBufAlgo.zero (dst, roi=ROI.All, nthreads=0)

    Zero out the destination buffer (or a specific region of it).
    
    Example:

    .. code-block:: python

        # Initialize buf to a 640x480 3-channel FLOAT buffer of 0 values
        buf = ImageBufAlgo.zero (ROI(0, 640, 0, 480, 0, 1, 0, 3))



.. py:method:: ImageBuf ImageBufAlgo.fill (values, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.fill (top, bottom, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.fill (topleft, topright, bottomleft, bottomright, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fill (dst, values, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fill (dst, top, bottom, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fill (dst, topleft, topright, bottomleft, bottomright, roi=ROI.All, nthreads=0)

    Return a filled float image of size `roi`, or set the the pixels of
    image `dst` within the ROI to a color or gradient.
    
    Three fill optins are available: (a) if one color tuple is supplied, the
    whole ROI will be filled with that constant value, (b) if two color
    tuples are supplied, a linear gradient will be applied from top to
    bottom, (c) if four color cuples are supplied, the ROI will be be filled
    with values bilinearly interpolated from the four corner colors
    supplied.

    Example:

    .. code-block:: python

        # Draw a red rectangle into buf
        buf = ImageBuf (ImageSpec(640, 480, 3, TypeDesc.FLOAT)
        ImageBufAlgo.fill (buf, (1,0,0), ROI(50, 100, 75, 85))




.. py:method:: ImageBuf ImageBufAlgo.checker(width, height, depth, color1, color2,  xoffset=0, yoffset=0, zoffset=0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.checker(dst, width, height, depth, color1, color2,  xoffset=0, yoffset=0, zoffset=0, roi=ROI.All, nthreads=0)

    Return (or copy into `dst`) a checkerboard pattern. The colors are specified as
    tuples giving the values for each color channel.

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec(640, 480, 3, oiio.UINT8))
        ImageBufAlgo.checker (buf, 64, 64, 1, (0.1,0.1,0.1), (0.4,0.4,0.4))


.. py:method:: ImageBuf ImageBufAlgo.noise (noisetype, A=0.0, B=0.1, mono=False, seed=0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.noise (dst, noisetype, A=0.0, B=0.1, mono=False, seed=0, roi=ROI.All, nthreads=0)

    Return an image of pseudorandom noise, or add pseudorandom noise
    to the specified region of existing region `dst`.
    
    For noise type "uniform", the noise is uniformly distributed on the
    range `[A,B)`. For noise "gaussian", the noise will have a normal
    distribution with mean A and standard deviation B. For noise "salt", the
    value A will be stored in a random set of pixels whose proportion (of
    the overall image) is B. For all noise types, choosing different `seed`
    values will result in a different pattern. If the `mono` flag is `True`,
    a single noise value will be applied to all channels specified by `roi`,
    but if `mono` is `False`, a separate noise value will be computed for
    each channel in the region.

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec(640, 480, 3, oiio.UINT8))
        ImageBufAlgo.zero (buf)
        ImageBufAlgo.noise (buf, 'uniform', 0.25, 0.75)



.. py:method:: ImageBufAlgo.render_point (dst, x, y, color=(1,1,1,1))

    Render a point at pixel (x,y) of `dst`.  The `color` (if supplied)
    is a tuple giving the per-channel colors. Return `True` for success,
    `False` for failure.

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec (640, 480, 4, oiio.FLOAT))
        ImageBufAlgo.render_point (buf, 10, 20, (1,0,0,1))



.. py:method:: bool ImageBufAlgo.render_line (dst, x1, y1, x2, y2, color=(1,1,1,1), skip_first_point=False)

    Render a line from pixel $(x_1,y_1)$ to $(x_2,y_2)$ into `dst`.  The
    `color` (if supplied) is a tuple giving the per-channel colors.

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec (640, 480, 4, oiio.FLOAT))
        ImageBufAlgo.render_line (buf, 10, 10, 500, 20, (1,0,0,1))



.. py:method:: bool ImageBufAlgo.render_box (dst, x1, y1, x2, y2, color=(1,1,1,1), filled=False)

    Render a filled or unfilled box with corners at pixels $(x_1,y_1)$ and
    $(x_2,y_2)$ into `dst`.  The `color` (if supplied) is a tuple giving
    the per-channel colors.

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec (640, 480, 4, oiio.FLOAT))
        ImageBufAlgo.render_box (buf, 150, 100, 240, 180, (0,1,1,1))
        ImageBufAlgo.render_box (buf, 100, 50, 180, 140, (0.5, 0.5, 0, 0.5), True)



.. py:method:: bool ImageBufAlgo.render_text (dst, x, y, text, fontsize=16, fontname="", textcolor=(1,1,1,1), alignx="left", aligny="baseline", shadow=0, roi=ROI.All, nthreads=0)

    Render antialiased text into `dst`.  The `textcolor` (if supplied)
    is a tuple giving the per-channel colors. Choices for `alignx` are
    "left", "right", and "center", and choices for `aligny` are
    "baseline", "top", "bottom", and "center".

    Example:

    .. code-block:: python

        buf = ImageBuf(ImageSpec (640, 480, 4, oiio.FLOAT))
        ImageBufAlgo.render_text (buf, 50, 100, "Hello, world")
        ImageBufAlgo.render_text (buf, 100, 200, "Go Big Red!",
                                  60, "Arial Bold", (1,0,0,1))



.. py:method:: ROI ImageBufAlgo.text_size (text, fontsize=16, fontname="")

    Compute the size that will be needed for the text as an ROI and return it.
    The size will not be `defined` if an error occurred (such as not being a
    valid font name).

    Example:

    .. code-block:: python

        A = ImageBuf(ImageSpec (640, 480, 4, oiio.FLOAT))
        Aroi = A.roi
        size = ImageBufAlgo.text_size ("Centered", 40, "Courier New")
        if size.defined :
            x = Aroi.xbegin + Aroi.width/2  - (size.xbegin + size.width/2)
            y = Aroi.ybegin + Aroi.height/2 - (size.ybegin + size.height/2)
            ImageBufAlgo.render_text (A, x, y, "Centered", 40, "Courier New")
    
        # Note: this was for illustration. An easier way to do this is:
        #   render_text (A, x, y, "Centered", 40, "Courier New", alignx="center")




.. _sec-iba-py-transforms:

Image transformations and data movement
---------------------------------------

.. py:method:: ImageBuf ImageBufAlgo.channels(src, channelorder, newchannelnames=(), shuffle_channel_names=False, nthreads=0)
               bool ImageBufAlgo.channels(dst, src, channelorder, newchannelnames=(), shuffle_channel_names=False, nthreads=0)

    Return (or store in `dst`) shuffled channels of `src`, with channels in the
    order specified by the tuple `channelorder`. The length of `channelorder`
    specifies the number of channels to copy. Each element in the tuple
    `channelorder` may be one of the following:
    
    * `int` : specifies the index (beginning at 0) of the channel to copy.
    * `str` : specifies the name of the channel to copy.
    * `float` : specifies a constant value to use for that channel.
    
    
    If `newchannelnames` is supplied, it is a tuple of new channel names. (See
    the C++ version for more full explanation.)

    Example:

    .. code-block:: python

        # Copy the first 3 channels of an RGBA, drop the alpha
        RGBA = ImageBuf("rgba.tif")
        RGB = ImageBufAlgo.channels (RGBA, (0,1,2))
    
        # Copy just the alpha channel, making a 1-channel image
        Alpha = ImageBufAlgo.channels (RGBA, ("A",))
    
        # Swap the R and B channels
        BGRA = ImageBufAlgo.channels (RGBA, (2, 1, 0, 3))
    
        # Add an alpha channel with value 1.0 everywhere to an RGB image
        RGBA = ImageBufAlgo.channels (RGB, ("R", "G", "B", 1.0),
                                      ("R", "G", "B", "A"))


.. py:method:: ImageBuf ImageBufAlgo.channel_append (A, B, roi=ROI.All, nthreads=0) bool ImageBufAlgo.channel_append (dst, A, B, roi=ROI.All, nthreads=0)

    Append the channels of images `A` and `B` together into one image.

    Example:

    .. code-block:: python

        RGBA = ImageBuf ("rgba.exr")
        Z = ImageBuf ("z.exr")
        RGBAZ = ImageBufAlgo.channel_append (RGBA, Z)



.. py:method:: ImageBuf ImageBufAlgo.copy (src, convert=TypeDesc.UNKNOWN, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.copy (dst, src, convert=TypeDesc.UNKNOWN, roi=ROI.All, nthreads=0)

    Copy the specified region of pixels of `src` at the same locations,
    optionally with the pixel type overridden by `convert` (if it is not
    `UNKNOWN`).

    Example:

    .. code-block:: python

        # Copy A's upper left 200x100 region into B
        B = ImageBufAlgo.copy (A, ROI(0,200,0,100))



.. py:method:: ImageBuf ImageBufAlgo.crop (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.crop (dst, src, roi=ROI.All, nthreads=0)

    Reset `dst` to be the specified region of `src`.

    Example:

    .. code-block:: python

        # Set B to be the upper left 200x100 region of A
        A = ImageBuf ("a.tif")
        B = ImageBufAlgo.crop (A, ROI(0,200,0,100))



.. py:method:: ImageBuf ImageBufAlgo.cut (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.cut (dst, src, roi=ROI.All, nthreads=0)

    Reset `dst` to be the specified region of `src`, but moved so that the
    resulting new image has its pixel data at the image plane origin.

    Example:

    .. code-block:: python

        # Set B to be the lower left 200x100 region of A, moved to the origin
        A = ImageBuf ("a.tif")
        B = ImageBufAlgo.cut (A, ROI(0,200,380,480))



.. py:method:: bool ImageBufAlgo.paste (dst, xbegin, ybegin, zbegin, chbegin, src, srcroi=ROI.All, nthreads=0)

    Copy the specified region of `src` into `dst` with the given offset
    (`xbegin`, `ybegin`, `zbegin`).

    Example:

    .. code-block:: python

        # Paste small.exr on top of big.exr at offset (100,100)
        Big = ImageBuf ("big.exr")
        Small = ImageBuf ("small.exr")
        ImageBufAlgo.paste (Big, 100, 100, 0, 0, Small)



.. py:method:: ImageBuf ImageBufAlgo.rotate90 (src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.rotate180 (src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.rotate270 (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rotate90 (dst, src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rotate180 (dst, src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rotate270 (dst, src, roi=ROI.All, nthreads=0)

    Copy while rotating the image by a multiple of 90 degrees.

    Example:

    .. code-block:: python

        A = ImageBuf ("tahoe.exr")
        B = ImageBufAlgo.rotate90 (A)




.. py:method:: ImageBuf ImageBufAlgo.flip (src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.flop (src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.transpose (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.flip (dst, src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.flop (dst, src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.transpose (dst, src, roi=ROI.All, nthreads=0)

    Copy while reversing orientation vertically (flip) or horizontally (flop),
    or diagonally (transpose).

    Example:

    .. code-block:: python

        A = ImageBuf ("tahoe.exr")
        B = ImageBufAlgo.flip (A)



.. py:method:: ImageBuf ImageBufAlgo.reorient (src, nthreads=0)
               bool ImageBufAlgo.reorient (dst, src, nthreads=0)

    Copy `src`, applying whatever seties of rotations, flips,
    or flops are necessary to transform the pixels into the configuration
    suggested by the `"Orientation"` metadata of the image (and the
    `"Orientation"` metadata is then set to 1, ordinary orientation).

    Example:

    .. code-block:: python

        A = ImageBuf ("tahoe.jpg")
        ImageBufAlgo.reorient (A, A)




.. py:method:: ImageBuf ImageBufAlgo.circular_shift (src, xshift, yshift, zshift=0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.circular_shift (dst, src, xshift, yshift, zshift=0, roi=ROI.All, nthreads=0)

    Copy while circularly shifting by the given amount. 

    Example:

    .. code-block:: python

        A = ImageBuf ("tahoe.exr")
        B = ImageBufAlgo.circular_shift (A, 200, 100)



.. py:method:: ImageBuf ImageBufAlgo.rotate (src, angle, filtername="", filtersize=0.0, recompute_roi=False, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.rotate (src, angle, center_x, center_y, filtername="", filtersize=0.0, recompute_roi=False, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rotate (dst, src, angle, filtername="", filtersize=0.0, recompute_roi=False, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rotate (dst, src, angle, center_x, center_y, filtername="", filtersize=0.0, recompute_roi=False, roi=ROI.All, nthreads=0)

    Copy arotated version of the corresponding portion of `src`.  The angle
    is in radians, with positive values indicating clockwise rotation. If
    the filter and size are not specified, an appropriate default will be
    chosen.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.rotate (Src, math.radians(45.0))



.. py:method:: ImageBuf ImageBufAlgo.warp (src, M, filtername="", filtersize=0.0, wrap="default", recompute_roi=False, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.warp (dst, src, M, filtername="", filtersize=0.0, wrap="default", recompute_roi=False, roi=ROI.All, nthreads=0)

    Compute a warped (transformed) copy of `src`, with the warp specified by
    `M` consisting of 9 floating-point numbers representing a 3x3
    transformation matrix.  If the filter and size are not specified, an
    appropriate default will be chosen.

    Example:

    .. code-block:: python

        M = (0.7071068, 0.7071068, 0, -0.7071068, 0.7071068, 0, 20, -8.284271, 1)
        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.warp (Src, M)



.. py:method:: ImageBuf ImageBufAlgo.resize (src, filtername="", filtersize=0.0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.resize (dst, src, filtername="", filtersize=0.0, roi=ROI.All, nthreads=0)

    Compute a high-quality resized version of the corresponding portion of
    `src`.  If the filter and size are not specified, an appropriate default
    will be chosen.

    Example:

    .. code-block:: python

        # Resize the image to 640x480, using the default filter
        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.resize (Src, roi=ROI(0,640,0,480,0,1,0,3))



.. py:method:: ImageBuf ImageBufAlgo.resample (src, interpolate=True, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.resample (dst, src, interpolate=True, roi=ROI.All, nthreads=0)

    Set `dst`, over the ROI, to be a low-quality (but fast) resized version
    of the corresponding portion of `src`, either using a simple "closest
    pixel" choice or by bilinaerly interpolating (depending on
    `interpolate`).

    Example:

    .. code-block:: python

        # Resample quickly to 320x240 to make a low-quality thumbnail
        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.resample (Src, roi=ROI(0,640,0,480,0,1,0,3))



.. py:method:: ImageBuf ImageBufAlgo.fit (src, filtername="", filtersize=0.0, fillmode="letterbox", exact=false, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fit (dst, src, filtername="", filtersize=0.0, fillmode="letterbox", exact=false, roi=ROI.All, nthreads=0)

    Fit `src` into the `roi` while preserving the original aspect ratio,
    without stretching.  If the filter and size are not specified, an
    appropriate default will be chosen.

    Example:

    .. code-block:: python

        # Resize to fit into a max of 640x480, preserving the aspect ratio
        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.fit (Src, roi=ROI(0,640,0,480,0,1,0,3))




.. _sec-iba-py-arith:

Image arithmetic
----------------

.. py:method:: ImageBuf ImageBufAlgo.add (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.add (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `A + B`.  `A` and `B` each may be an ImageBuf, a `float` value
    (for all channels) or a tuple giving a `float` for each color channel.

    Example:

    .. code-block:: python

        # Add two images
        buf = ImageBufAlgo.add (ImageBuf("a.exr"), ImageBuf("b.exr"))
    
        # Add 0.2 to channels 0-2 
        ImageBufAlgo.add (buf, buf, (0.2,0.2,0.2,0))



.. py:method:: ImageBuf ImageBufAlgo.sub (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.sub (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `A - B`.  `A` and `B` each may
    be an ImageBuf, a `float` value (for all channels) or a tuple giving a
    `float` for each color channel.

    Example:

    .. code-block:: python

        buf = ImageBufAlgo.sub (ImageBuf("a.exr"), ImageBuf("b.exr"))



.. py:method:: ImageBuf ImageBufAlgo.absdiff (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.absdiff (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `abs(A - B)`.  `A` and `B` each may be an ImageBuf, a `float` value
    (for all channels) or a tuple giving a `float` for each color channel.

    Example:

    .. code-block:: python

        buf = ImageBufAlgo.absdiff (ImageBuf("a.exr"), ImageBuf("b.exr"))



.. py:method:: ImageBuf ImageBufAlgo.abs (A, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.abs (dst, A, roi=ROI.All, nthreads=0)

    Compute `abs(A)`.  `A` is an ImageBuf.

    Example:

    .. code-block:: python

        buf = ImageBufAlgo.abs (ImageBuf("a.exr"))



.. py:method:: ImageBuf ImageBufAlgo.mul (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.mul (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `A * B` (channel-by-channel multiplication). `A` and `B` each
    may be an ImageBuf, a `float` value (for all channels) or a tuple giving
    a `float` for each color channel.

    Example:

    .. code-block:: python

        # Multiply the two images
        buf = ImageBufAlgo.mul (ImageBuf("a.exr"), ImageBuf("b.exr"))
    
        # Reduce intensity of buf's channels 0-2 by 50%, in place
        ImageBufAlgo.mul (buf, buf, (0.5, 0.5, 0.5, 1.0))



.. py:method:: ImageBuf ImageBufAlgo.div (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.div (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `A / B` (channel-by-channel division), where x/0 is defined to
    be 0.  `A` and `B` each may be an ImageBuf, a `float` value (for all
    channels) or a tuple giving a `float` for each color channel.

    Example:

    .. code-block:: python

        # Divide a.exr by b.exr
        buf = ImageBufAlgo.div (ImageBuf("a.exr"), ImageBuf("b.exr"))
    
        # Reduce intensity of buf's channels 0-2 by 50%, in place
        ImageBufAlgo.div (buf, buf, (2.0, 2.0, 2.0, 1.0))



.. py:method:: ImageBuf ImageBufAlgo.mad (A, B, C, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.mad (dst, A, B, C, roi=ROI.All, nthreads=0)

    Compute `A * B + C` (channel-by-channel multiplication and addition).
    `A`, `B`, and `C` each may be an ImageBuf, a `float` value (for all
    channels) or a tuple giving a `float` for each color channel.

    Example:

    .. code-block:: python

        # Multiply a and b, then add c
        buf = ImageBufAlgo.mad (ImageBuf("a.exr"),
                                (1.0f, 0.5f, 0.25f), ImageBuf("c.exr"))




.. py:method:: ImageBuf ImageBufAlgo.invert (A, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.invert (dst, A, roi=ROI.All, nthreads=0)

    Compute `1-A` (channel by channel color inverse). `A` is an ImageBuf.

    Example:

    .. code-block:: python

        buf = ImageBufAlgo.invert (ImageBuf("a.exr"))



.. py:method:: ImageBuf ImageBufAlgo.pow (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.pow (dst, A, B, roi=ROI.All, nthreads=0)

    Compute `pow (A, B)` (channel-by-channel exponentiation).
    `A` is an ImageBuf, and `B` may be a `float` (a single power
    for all channels) or a tuple giving a `float` for each color channel.

    Example:

    .. code-block:: python

        # Linearize a 2.2 gamma-corrected image (channels 0-2 only)
        img = ImageBuf ("a.exr")
        buf = ImageBufAlgo.pow (img, (2.2, 2.2, 2.2, 1.0))



.. py:method:: ImageBuf ImageBufAlgo.channel_sum (src, weights=(), roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.channel_sum (dst, src, weights=(), roi=ROI.All, nthreads=0)

    Converts a multi-channel image into a 1-channel image via a weighted sum
    of channels. The `weights` is a tuple providing the weight for each
    channel (if not supplied, all channels will have weight 1.0).

    Example:

    .. code-block:: python

        # Compute luminance via a weighted sum of R,G,B
        # (assuming Rec709 primaries and a linear scale)
        ImageBuf()
        weights = (.2126, .7152, .0722)
        luma = ImageBufAlgo.channel_sum (ImageBuf("a.exr"), weights)



.. py:method:: ImageBuf ImageBufAlgo.contrast_remap (src, black=0.0, white=1.0, min=0.0, max=1.0, sthresh=0.0, scontrast=1.0, ROI roi={}, int nthreads=0)
               bool ImageBufAlgo.contrast_remap (ImageBuf &dst, src, black=0.0, white=1.0, min=0.0, max=1.0, sthresh=0.0, scontrast=1.0, ROI roi={}, int nthreads=0)

    Return (or copy into `dst`) pixel values that are a contrast-remap
    of the corresponding values of the `src` image, transforming pixel
    value domain [black, white] to range [min, max], either linearly or with
    optional application of a smooth sigmoidal remapping (if scontrast != 1.0).

    Example:

    .. code-block:: python

        A = ImageBuf('tahoe.tif');
    
        # Simple linear remap that stretches input 0.1 to black, and input
        # 0.75 to white.
        linstretch = ImageBufAlgo.contrast_remap (A, black=0.1, white=0.75)
    
        # Remapping 0->1 and 1->0 inverts the colors of the image,
        # equivalent to ImageBufAlgo.invert().
        inverse = ImageBufAlgo.contrast_remap (A, black=1.0, white=0.0)
    
        # Use a sigmoid curve to add contrast but without any hard cutoffs.
        # Use a contrast parameter of 5.0.
        sigmoid = ImageBufAlgo.contrast_remap (a, contrast=5.0)



.. py:method:: ImageBuf ImageBufAlgo.color_map (src, srcchannel, nknots, channels, knots, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.color_map (src, srcchannel, mapname, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.color_map (dst, src, srcchannel, nknots, channels, knots, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.color_map (dst, src, srcchannel, mapname, roi=ROI.All, nthreads=0)

    Return an image (or copy into `dst`) pixel values determined by applying
    the color map to the values of `src`, using either the channel specified
    by `srcchannel`, or the luminance of `src`'s RGB if `srcchannel` is -1.
    
    In the first variant, the values linearly-interpolated color map are
    given by the tuple `knots[nknots*channels]`.
    
    In the second variant, just the name of a color map is specified.
    Recognized map names include: "inferno", "viridis", "magma", "plasma",
    all of which are perceptually uniform, strictly increasing in luminance,
    look good when converted to grayscale, and work for people with all
    types of colorblindness. The "turbo" color map is also nice in most of
    these ways (except for being strictly increasing in luminance). Also
    supported are the following color maps that do not have those desirable
    qualities (and are this not recommended): "blue-red", "spectrum", and
    "heat". In all cases, the implied `channels` is 3.

    Example:

    .. code-block:: python

        heatmap = ImageBufAlgo.color_map (ImageBuf("a.jpg"), -1, "inferno")
    
        heatmap = ImageBufAlgo.color_map (ImageBuf("a.jpg"), -1, 3, 3,
                                (0.25, 0.25, 0.25,  0, 0.5, 0,  1, 0, 0))
    


.. py:method:: ImageBuf ImageBufAlgo.clamp (src, min, max, bool clampalpha01=False,  roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.clamp (dst, src, min, max, bool clampalpha01=False,  roi=ROI.All, nthreads=0)

    Copy pixels while clamping between the `min` and `max` values.  The
    `min` and `max` may either be tuples (one min and max value per
    channel), or single floats (same value for all channels).  Additionally,
    if `clampalpha01` is `True`, then any alpha channel is clamped to the
    0--1 range.

    Example:

    .. code-block:: python

        # Clamp image buffer A in-place to the [0,1] range for all channels.
        ImageBufAlgo.clamp (A, A, 0.0, 1.0)



.. py:method:: ImageBuf ImageBufAlgo.rangecompress (src, useluma=False, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rangecompress (dst, src, useluma=False, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.rangeexpand (src, useluma=False, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.rangeexpand (dst, src, useluma=False, roi=ROI.All, nthreads=0)

    Copy from `src`, compressing (logarithmically) or expanding
    (by the inverse of the compressive transformation) the range of pixel
    values.  Alpha and z channels are copied but not transformed.
    
    If `useluma` is `True`, the luma of the first three channels (presumed
    to be R, G, and B) are used to compute a single scale factor for all
    color channels, rather than scaling all channels individually (which
    could result in a big color shift when performing `rangecompress`
    and `rangeexpand`).

    Example:

    .. code-block:: python

        # Resize the image to 640x480, using a Lanczos3 filter, which
        # has negative lobes. To prevent those negative lobes from
        # producing ringing or negative pixel values for HDR data,
        # do range compression, then resize, then re-expand the range.
    
        # 1. Read the original image
        Src = ImageBuf ("tahoeHDR.exr")
    
        # 2. Range compress to a logarithmic scale
        Compressed = ImageBufAlgo.rangecompress (Src)
    
        # 3. Now do the resize
        roi = ROI (0, 640, 0, 480, 0, 1, 0, Compressed.nchannels)
        Dst = ImageBufAlgo.resize (Compressed, "lanczos3", 6.0, roi)
    
        # 4. Expand range to be linear again (operate in-place)
        ImageBufAlgo.rangeexpand (Dst, Dst)



.. py:method:: ImageBuf ImageBufAlgo.over (A, B, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.over (dst, A, B, roi=ROI.All, nthreads=0)

    Composite ImageBuf `A` *over* ImageBuf `B`.

    Example:

    .. code-block:: python

        Comp = ImageBufAlgo.over (ImageBuf("fg.exr"), ImageBuf("bg.exr"))



.. py:method:: ImageBuf ImageBufAlgo.zover (A, B, bool z_zeroisinf=False, roi=ROI.All, nthreads=0
               bool ImageBufAlgo.zover (dst, A, B, bool z_zeroisinf=False, roi=ROI.All, nthreads=0)

    Composite ImageBuf `A` and ImageBuf `B` using their respective
    *Z* channels to decide which is in front on a pixel-by-pixel basis.

    Example:

    .. code-block:: python

        Comp = ImageBufAlgo.zover (ImageBuf("fg.exr"), ImageBuf("bg.exr"))




.. _sec-iba-py-stats:

Image comparison and statistics
-------------------------------


.. py:method:: PixelStats ImageBufAlgo.computePixelStats (src, roi=ROI.All, nthreads=0)

    Compute statistics about the ROI of the image `src`. The `PixelStats`
    structure is defined as contining the following data fields: `min`,
    `max`, `avg`, `stddev`, `nancount`, `infcount`, `finitecount`, `sum`,
    `sum2`, each of which is a tuple with one value for each channel of the
    image.

    Example:

    .. code-block:: python

        A = ImageBuf("a.exr")
        stats = ImageBufAlgo.computePixelStats (A)
        print "   min = ", stats.min
        print "   max = ", stats.max
        print "   average = ", stats.avg
        print "   standard deviation  = ", stats.stddev
        print "   # NaN values    = ", stats.nancount
        print "   # Inf values    = ", stats.infcount
        print "   # finite values = ", stats.finitecount



.. py:method:: CompareResults ImageBufAlgo.compare (A, B, failthresh, warnthresh, roi=ROI.All, nthreads=0)

    Numerically compare two ImageBuf's, `A` and `B`. The `failthresh` and
    `warnthresh` supply failure and warning difference thresholds. The
    return value is a `CompareResults` object, which is defined as a class
    having the following members:

    .. code-block:: python

        meanerror, rms_error, PSNR, maxerror  # error statistics
        maxx, maxy, maxz, maxc                # pixel of biggest difference
        nwarn, nfail                          # number of warnings and failures
        error                                 # True if there was an error


    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        B = ImageBuf ("b.exr")
        comp = ImageBufAlgo.compare (A, B, 1.0/255.0, 0.0)
        if comp.nwarn == 0 and comp.nfail == 0 :
            print "Images match within tolerance"
        else :
            print comp.nfail, "failures,", comp.nwarn, " warnings."
            print "Average error was " , comp.meanerror
            print "RMS error was" , comp.rms_error
            print "PSNR was" , comp.PSNR
            print "largest error was ", comp.maxerror
            print "  on pixel", (comp.maxx, comp.maxy, comp.maxz)
            print "  channel", comp.maxc



.. py:method:: tuple ImageBufAlgo.isConstantColor (src, threshold=0.0, roi=ROI.All, nthreads=0)

    If all pixels of `src` within the ROI have the same values (for the
    subset of channels described by `roi`), return a tuple giving that color
    (one `float` for each channel), otherwise return `None`.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        color = ImageBufAlgo.isConstantColor (A)
        if color != None :
            print "The image has the same value in all pixels:", color
        else :
            print "The image is not a solid color."



.. py:method:: bool ImageBufAlgo.isConstantChannel (src, channel, val, threshold=0.0, roi=ROI.All, nthreads=0)

    Returns `True` if all pixels of `src` within the ROI have the given
    `channel` value `val`.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        alpha = A.spec.alpha_channel
        if alpha < 0 :
            print "The image does not have an alpha channel"
        elif ImageBufAlgo.isConstantChannel (A, alpha, 1.0) :
            print "The image has alpha = 1.0 everywhere"
        else :
            print "The image has alpha < 1 in at least one pixel"



.. py:method:: bool ImageBufAlgo.isMonochrome (src, threshold=0.0, roi=ROI.All, nthreads=0)

    Returns `True` if the image is monochrome within the ROI.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        roi = A.roi
        roi.chend = min (3, roi.chend)  # only test RGB, not alpha
        if ImageBufAlgo.isMonochrome (A, roi) :
            print "a.exr is really grayscale"



.. py:method:: bool ImageBufAlgo.color_range_check (src, low, high, roi=ROI.All, nthreads=0)

    Count how many pixels in the `src` image (within the `roi`) are outside
    the value range described by `low` and `hi` (which each may be either
    one value or a tuple with per-channel values for each of `roi.chbegin
    ... roi.chend`. The result returned is a tuple containing three values:
    the number of values less than `low`, the number of values greater then
    `hi`, and the number of values within the range.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        counts = ImageBufAlgo.color_range_check (A, 0.5, 0.75)
        print ('{} values < 0.5, {} values > 0.75'.format(counts[0], counts[1])



.. py:method:: ROI ImageBufAlgo.nonzero_region (src, roi=ROI.All, nthreads=0)

    Returns an ROI that tightly encloses the minimal region within `roi`
    that contains all pixels with nonzero values.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        nonzero_roi = ImageBufAlgo.nonzero_region(A)



.. py:method:: std::string ImageBufAlgo.computePixelHashSHA1 (src, extrainfo = "", roi=ROI.All, blocksize=0, nthreads=0)

    Compute the SHA-1 byte hash for all the pixels in the ROI of `src`.

    Example:

    .. code-block:: python

        A = ImageBuf ("a.exr")
        hash = ImageBufAlgo.computePixelHashSHA1 (A, blocksize=64)



.. py:method:: tuple histogram (src, channel=0, bins=256, min=0.0, max=1.0, ignore_empty=False, roi=ROI.All, nthreads=0)
    
    Computes a histogram of the given `channel` of image `src`, within the
    ROI, returning a tuple of length `bins` containing count of pixels whose
    value was in each of the equally-sized range bins between `min` and
    `max`. If `ignore_empty` is `True`, pixels that are empty (all channels
    0 including alpha) will not be counted in the total.



.. _sec-iba-py-convolutions:

Convolutions
------------

.. py:method:: ImageBuf ImageBufAlgo.make_kernel (name, width, height, depth=1.0, normalize=True)}

    Create a 1-channel `float` image of the named kernel and dimensions.  If
    `normalize` is `True`, the values will be normalized so that they sum to
    1.0.
    
    If `depth` > 1, a volumetric kernel will be created.  Use with caution!
    
    Kernel names can be: "gaussian", "sharp-gaussian", "box", "triangle",
    "mitchell", "blackman-harris", "b-spline", "catmull-rom", "lanczos3",
    "cubic", "keys", "simon", "rifman", "disk", "binomial", "laplacian".
    Note that "catmull-rom" and "lanczos3" are fixed-size kernels that don't
    scale with the width, and are therefore probably less useful in most
    cases.

    Example:

    .. code-block:: python

        K = ImageBufAlgo.make_kernel ("gaussian", 5.0, 5.0)



.. py:method:: ImageBuf ImageBufAlgo.convolve (src, kernel, normalize=True, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.convolve (dst, src, kernel, normalize=True, roi=ROI.All, nthreads=0)

    Replace the given ROI of `dst` with the convolution of `src` and
    a kernel (also an ImageBuf).

    Example:

    .. code-block:: python

        # Blur an image with a 5x5 Gaussian kernel
        Src = ImageBuf ("tahoe.exr")
        K = ImageBufAlgo.make_kernel (K, "gaussian", 5.0, 5.0)
        Blurred = ImageBufAlgo.convolve (Src, K)



.. py:method:: ImageBuf ImageBufAlgo.laplacian (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.laplacian (dst, src, roi=ROI.All, nthreads=0)

    Replace the given ROI of `dst` with the Laplacian of the corresponding
    part of `src`.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
        L = ImageBufAlgo.laplacian (Src)



.. py:method:: ImageBuf ImageBufAlgo.fft (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fft (dst, src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.ifft (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.ifft (dst, src, roi=ROI.All, nthreads=0)

    Compute the forward or inverse discrete Fourier Transform.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
    
        # Take the DFT of the first channel of Src
        Freq = ImageBufAlgo.fft (Src)
    
        # At this point, Freq is a 2-channel float image (real, imag)
        # Convert it back from frequency domain to a spatial iamge
        Spatial = ImageBufAlgo.ifft (Freq)



.. py:method:: ImageBuf ImageBufAlgo.complex_to_polar (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.complex_to_polar (dst, src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.polar_to_complex (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.polar_to_complex (dst, src, roi=ROI.All, nthreads=0)

    Transform a 2-channel image from complex (real, imaginary) representation
    to polar (amplitude, phase), or vice versa.

    Example:

    .. code-block:: python

        Polar = ImageBuf ("polar.exr")
    
        Complex = ImageBufAlgo.polar_to_complex (Polar)
    
        # At this point, Complex is a 2-channel complex image (real, imag)
        # Convert it back from frequency domain to a spatial iamge
        Spatial = ImageBufAlgo.ifft (Complex)




.. _sec-iba-py-enhance:

Image Enhancement / Restoration
-------------------------------

.. py:method:: ImageBuf ImageBufAlgo.fixNonFinite (src, mode=NONFINITE_BOX3, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fixNonFinite (dst, src, mode=NONFINITE_BOX3, roi=ROI.All, nthreads=0)

    Copy pixel values from `src` and repair any non-finite (`NaN` or `Inf`)
    pixels.
    
    How the non-finite values are repaired is specified by one of the
    following modes::

        OpenImageIO.NONFINITE_NONE
        OpenImageIO.NONFINITE_BLACK
        OpenImageIO.NONFINITE_BOX3

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
        ImageBufAlgo.fixNonFinite (Src, Src, OpenImageIO.NONFINITE_BOX3)



.. py:method:: ImageBuf ImageBufAlgo.fillholes_pushpull (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.fillholes_pushpull (dst, src, roi=ROI.All, nthreads=0)

    Copy the specified ROI of `src` and fill any holes (pixels where alpha <
    1) with plausible values using a push-pull technique.  The `src` image
    must have an alpha channel.  The `dst` image will end up with a copy of
    src, but will have an alpha of 1.0 everywhere, and any place where the
    alpha of src was < 1, dst will have a pixel color that is a plausible
    "filling" of the original alpha hole.

    Example:

    .. code-block:: python

        Src = ImageBuf ("holes.exr")
        Filled = ImageBufAlgo.fillholes_pushpull (Src)



.. py:method:: bool ImageBufAlgo.median_filter (dst, src, width=3, height=-1, roi=ROI.All, nthreads=0)

    Replace the given ROI of `dst` with the `width` x `height` median filter
    of the corresponding region of `src` using the "unsharp mask" technique.

    Example:

    .. code-block:: python

        Noisy = ImageBuf ("tahoe.exr")
        Clean = ImageBuf ()
        ImageBufAlgo.median_filter (Clean, Noisy, 3, 3)



.. py:method:: ImageBuf ImageBufAlgo.dilate (src, width=3, height=-1, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.dilate (dst, src, width=3, height=-1, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.erode (src, width=3, height=-1, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.erode (dst, src, width=3, height=-1, roi=ROI.All, nthreads=0) }

    Compute a dilated or eroded version of the corresponding region of `src`.

    Example:

    .. code-block:: python

        Source = ImageBuf ("source.tif")
        Dilated = ImageBufAlgo.dilate (Source, 3, 3)



.. py:method:: ImageBuf ImageBufAlgo.unsharp_mask (src, kernel="gaussian", width=3.0, contrast=1.0, threshold=0.0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.unsharp_mask (dst, src, kernel="gaussian", width=3.0, contrast=1.0, threshold=0.0, roi=ROI.All, nthreads=0)

    Compute a sharpened version of the corresponding region of `src` using
    the "unsharp mask" technique.

    Example:

    .. code-block:: python

        Blurry = ImageBuf ("tahoe.exr")
        Sharp = ImageBufAlgo.unsharp_mask (Blurry, "gaussian", 5.0)




.. _sec-iba-py-color:

Color manipulation
------------------

.. py:method:: ImageBuf ImageBufAlgo.colorconvert (src, fromspace, tospace, unpremult=True, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.colorconvert (dst, src, fromspace, tospace, unpremult=True, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)

    Apply a color transform to the pixel values.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.jpg")
        Dst = ImageBufAlgo.colorconvert (Src, "sRGB", "linear")



.. py:method:: ImageBuf ImageBufAlgo.colormatrixtransform (src, M, unpremult=True, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.colormatrixtransform (dst, src, M, unpremult=True, roi=ROI.All, nthreads=0)

    *NEW in 2.1*

    Apply a 4x4 matrix color transform to the pixel values. The matrix can
    be any tuple of 16 float values.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.jpg")
        M = ( .8047379,  .5058794, -.3106172, 0,
             -.3106172,  .8047379,  .5058794, 0,
              .5058794, -.3106172,  .8047379, 0,
              0,         0,         0,       1)
        Dst = ImageBufAlgo.colormatrixtransform (Src, M)



.. py:method:: ImageBuf ImageBufAlgo.ociolook (src, looks, fromspace, tospace, unpremult=True, inverse=False, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.ociolook (dst, src, looks, fromspace, tospace, unpremult=True, inverse=False, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)

    Apply an OpenColorIO "look" transform to the pixel values.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.jpg")
        Dst = ImageBufAlgo.ociolook (Src, "look", "vd8", "lnf",
                                context_key="SHOT", context_value="pe0012")



.. py:method:: ImageBuf ImageBufAlgo.ociodisplay (src, display, view, fromspace="", looks="", unpremult=True, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.ociodisplay (dst, src, display, view, fromspace="", looks="", unpremult=True, context_key="", context_value="", colorconfig="", roi=ROI.All, nthreads=0)

    Apply an OpenColorIO "display" transform to the pixel values.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.ociodisplay (Src, "sRGB", "Film", "lnf",
                                  context_key="SHOT", context_value="pe0012")



.. py:method:: ImageBuf ImageBufAlgo.ociofiletransform (src, name, unpremult=True, inverse=False, colorconfig="", roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.ociofiletransform (dst, src, name, unpremult=True, inverse=False, colorconfig="", roi=ROI.All, nthreads=0)

    Apply an OpenColorIO "file" transform to the pixel values.
    In-place operations (`dst` and `src` being the same image)
    are supported.

    Example:

    .. code-block:: python

        Src = ImageBuf ("tahoe.exr")
        Dst = ImageBufAlgo.ociofiletransform (Src, "foottransform.csp")



.. py:method:: ImageBuf ImageBufAlgo.unpremult (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.unpremult (dst, src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.premult (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.premult (dst, src, roi=ROI.All, nthreads=0)
               ImageBuf ImageBufAlgo.repremult (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.repremult (dst, src, roi=ROI.All, nthreads=0)

    Copy pixels from `src` to `dst`, and un-premultiply, premultiply, or
    re-premultiply the colors by alpha.

    `unpremult` divides colors by alpha, but preserves original color if
    alpha is 0. `premult` multiplies colors by alpha (even if alpha is 0).
    `repreumlt` is the true inverse of `unpremult`, multiplying color by
    alpha, but preserving color values in the alpha = 0 case.

    Example:

    .. code-block:: python

        # Convert in-place from associated alpha to unassociated alpha
        A = ImageBuf ("a.exr")
        ImageBufAlgo.unpremult (A, A)




.. _sec-iba-py-importexport:

Import / export
---------------

.. py:method:: bool ImageBufAlgo.make_texture (mode, input, outputfilename, config=ImageSpec())

    Turn an input image (either an ImageBuf or a string giving a filename)
    into a tiled, MIP-mapped, texture file and write to the
    file named by (`outputfilename`).  The `mode` describes what type of texture file we
    are creating and may be one of the following::

        OpenImageIO.MakeTxTexture
        OpenImageIO.MakeTxEnvLatl
        OpenImageIO.MakeTxEnvLatlFromLightProbe

    The `config`, if supplied, is an ImageSpec that contains all the
    information and special instructions for making the texture. The full list
    of supported configuration options is given in
    Section :ref:`sec-iba-importexport`.

    The return value is True for success, False if errors occurred, in which
    case the error message will be retrievable from the global
    `oiio.geterror()`.

    Example:

    .. code-block:: python

        # This command line:
        #    maketx in.exr --hicomp --filter lanczos3 --opaque-detect \
        #             -o texture.exr
        # performs the same operations as:

        import OpenImageIO as oiio

        Input = oiio.ImageBuf ("in.exr")
        config = oiio.ImageSpec()
        config.attribute ("maketx:highlightcomp", 1)
        config.attribute ("maketx:filtername", "lanczos3")
        config.attribute ("maketx:opaque_detect", 1)
        ok = oiio.ImageBufAlgo.make_texture (oiio.MakeTxTexture, Input,
                                        "texture.exr", config)
        if not ok :
            print("error:", oiio.geterror())



.. py:method:: ImageBuf ImageBufAlgo::capture_image (cameranum, convert = OpenImageIO.UNKNOWN)

    Capture a still image from a designated camera.

    Example:

    .. code-block:: python

        WebcamImage = ImageBufAlgo.capture_image (0, OpenImageIO.UINT8)
        WebcamImage.write ("webcam.jpg")



Functions specific to deep images
---------------------------------

.. py:method:: ImageBuf ImageBufAlgo.deepen (src, zvalue=1.0, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.deepen (dst, src, zvalue=1.0, roi=ROI.All, nthreads=0)

    Convert a flat image to a deep one that has one depth sample per pixel
    (but no depth samples for the pixels corresponding to those in the
    source image that have infinite "Z" or that had 0 for all color channels
    and no "Z" channel).

    Example:

    .. code-block:: python

        Deep = ImageBufAlgo.deepen (ImageBuf("az.exr"))



.. py:method:: ImageBuf ImageBufAlgo.flatten (src, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.flatten (dst, src, roi=ROI.All, nthreads=0)

    Composite the depth samples within each pixel of "deep" ImageBuf `src`
    to produce a "flat" ImageBuf.

    Example:

    .. code-block:: python

        Flat = ImageBufAlgo.flatten (ImageBuf("deepalpha.exr"))



.. py:method:: ImageBuf ImageBufAlgo.deep_merge (A, B, occlusion_cull, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.deep_merge (dst, A, B, occlusion_cull, roi=ROI.All, nthreads=0)

    Merge the samples of two deep images `A` and `B` into a deep result. If
    `occlusion_cull` is `True`, samples beyond the first opaque sample will
    be discarded, otherwise they will be kept.

    Example:

    .. code-block:: python

        DeepA = ImageBuf("hardsurf.exr")
        DeepB = ImageBuf("volume.exr")
        Merged = ImageBufAlgo.deep_merge (DeepA, DeepB)



.. py:method:: ImageBuf ImageBufAlgo.deep_holdout (src, holdout, roi=ROI.All, nthreads=0)
               bool ImageBufAlgo.deep_holdout (dst, src, holdout, roi=ROI.All, nthreads=0)

    Return the pixels of `src`, but only copying the samples that are closer
    than the opaque frontier of image `holdout`. That is, `holdout` will
    serve as a depth holdout mask, but no samples from `holdout` will
    actually be copied to `dst`.

    Example:

    .. code-block:: python

        Img = ImageBuf("image.exr")
        Mask = ImageBuf("mask.exr")
        Thresholded = ImageBufAlgo.deep_holdout (Img, Mask)



Other ImageBufAlgo methods that understand deep images
------------------------------------------------------

In addition to the previously described methods that are specific to deep
images, the following ImageBufAlgo methods (described in their respective
sections) work with deep inputs::

    ImageBufAlgo.add
    ImageBufAlgo.channels
    ImageBufAlgo.compare
    ImageBufAlgo.computePixelStats
    ImageBufAlgo.crop
    ImageBufAlgo.div
    ImageBufAlgo.fixNonFinite
    ImageBufAlgo.mul
    ImageBufAlgo.nonzero_region
    ImageBufAlgo.resample
    ImageBufAlgo.sub


|

.. _sec-pythonmiscapi:

Miscellaneous Utilities
=======================

In the main ``OpenImageIO`` module, there are a number of values and
functions that are useful.  These correspond to the C++ API functions
explained in Section :ref:`sec-globalattribs`, please refer there for
details.

.. py:attribute:: openimageio_version

    The OpenImageIO version number is an `int`, 10000 for each major
    version, 100 for each minor version, 1 for each patch.  For example,
    OpenImageIO 1.2.3 would return a value of 10203.


.. py:method:: geterror(clear = True)

    Retrieves the latest global error, as a string.


.. py:method:: attribute (name, typedesc, value)
               attribute (name, int_value)
               attribute (name, float_value)
               attribute (name, str_value)

    Sets a global attribute (see Section :ref:`sec-globalattribs` for details),
    returning `True` upon success, or `False` if it was not a recognized
    attribute.

    Example:

    .. code-block:: python

        oiio.attribute ("threads", 0)



.. py:method:: getattribute (name, typedesc)
               get_int_attribute (name, defaultval=0)
               get_float_attribute (name, defaultval=0.0)
               get_string_attribute (name, defaultval="")

    Retrieves an attribute value from the named set of global OIIO options.
    (See Section :ref:`sec-globalattribs`.) The `getattribute()` function
    returns the value regardless of type, or `None` if the attribute does
    not exist.  The typed variety will only succeed if the attribute is
    actually of that type specified. Type varity with the type in the name
    also takes a default value.

    Example:

    .. code-block:: python

        formats = oiio.get_string_attribute ("format_list")


.. py:method:: is_imageio_format_name (name)

    Returns True if `name` is the name of a known and supported file format,
    `False` if it is not.

    Example:

    .. code-block:: python

        >>> print (oiio.is_imageio_format_name('tiff'))
        True
        >>> print (oiio.is_imageio_format_name('Bob'))
        False


.. _sec-pythonrecipes:

Python Recipes
==============

This section illustrates the Python syntax for doing many common image
operations from Python scripts, but that aren't already given as examples in
the earlier function descriptions.  All example code fragments assume the
following boilerplate:

.. code-block:: python

    #!/usr/bin/env python

    import OpenImageIO as oiio
    from OpenImageIO import ImageBuf, ImageSpec, ImageBufAlgo



|

**Subroutine to create a constant-colored image**

.. code-block:: python

    # Create an ImageBuf holding a n image of constant color, given the
    # resolution, data format (defaulting to UINT8), fill value, and image
    # origin.
    def make_constimage (xres, yres, chans=3, format=oiio.UINT8, value=(0,0,0),
                         xoffset=0, yoffset=0) :
        spec = ImageSpec (xres,yres,chans,format)
        spec.x = xoffset
        spec.y = yoffset
        b = ImageBuf (spec)
        oiio.ImageBufAlgo.fill (b, value)
        return b


The image is returned as an ImageBuf, then up to the caller 
what to do with it next.

|

**Subroutine to save an image to disk, printing errors**

.. code-block:: python

    # Save an ImageBuf to a given file name, with optional forced image format
    # and error handling.
    def write_image (image, filename, format=oiio.UNKNOWN) :
        if not image.has_error :
            image.write (filename, format)
        if image.has_error :
            print "Error writing", filename, ":", image.geterror()



|

**Converting between file formats**

.. code-block:: python

    img = ImageBuf ("input.png")
    write_image (img, "output.tif")



|

**Comparing two images and writing a difference image**

.. code-block:: python

    A = ImageBuf ("A.tif")
    B = ImageBuf ("B.tif")
    compresults = ImageBufAlgo.compare (A, B, 1.0e-6, 1.0e-6)
    if compresults.nfail > 0 :
        print "Images did not match, writing difference image diff.tif"
        diff = ImageBufAlgo.absdiff (A, B)
        image_write (diff, "diff.tif")



|

**Changing the data format or bit depth**

.. code-block:: python

    img = ImageBuf ("input.exr")
    # presume that it's a "half" OpenEXR file
    # write it back out as a "float" file:
    write_image (img, "output.exr", oiio.FLOAT)



|

**Changing the compression**

The following command converts writes a TIFF file, specifically using
LZW compression:

.. code-block:: python

    img = ImageBuf ("in.tif")
    img.specmod().attribute ("compression", "lzw")
    write_image (img, "compressed.tif")


The following command writes its results as a JPEG file at a compression
quality of 50 (pretty severe compression):

.. code-block:: python

    img = ImageBuf ("big.jpg")
    img.specmod().attribute ("quality", 50)
    write_image (img, "small.jpg")



|

**Converting between scanline and tiled images**

.. code-block:: python

    img = ImageBuf ("scan.tif")
    img.set_write_tiles (16, 16)
    write_image (img, "tile.tif")

    img = ImageBuf ("tile.tif")
    img.set_write_tiles (0, 0)
    write_image (img, "scan.tif")



|

**Adding captions or metadata**

.. code-block:: python

    img = ImageBuf ("foo.jpg")
    # Add a caption:
    img.specmod().attribute ("ImageDescription", "Hawaii vacation")
    # Add keywords:
    img.specmod().attribute ("keywords", "volcano,lava")
    write_image (img, "foo.jpg")



|

**Changing image boundaries**

Change the origin of the pixel data window:

.. code-block:: python

    img = ImageBuf ("in.exr")
    img.set_origin (256, 80)
    write_image (img, "offset.exr")


Change the display window:

.. code-block:: python

    img = ImageBuf ("in.exr")
    img.set_full (16, 1040, 16, 784)
    write_image (img, "out.exr")


Change the display window to match the data window:

.. code-block:: python

    img = ImageBuf ("in.exr")
    img.set_full (img.roi())
    write_image (img, "out.exr")


Cut (trim and extract) a 128x128 region whose upper left corner is at
location (900,300), moving the result to the origin (0,0) of the image plane
and setting the display window to the new pixel data window:

.. code-block:: python

    img = ImageBuf ("in.exr")
    b = ImageBufAlgo.cut (img, oiio.ROI(900,1028,300,428))
    write_image (b, "out.exr")



|

**Extract just the named channels from a complicted many-channel image, and
add an alpha channel that is 1 everywhere**

.. code-block:: python

    img = ImageBuf ("allmyaovs.exr")
    b = ImageBufAlgo.channels (img, ("spec.R", "spec.G", "spec.B", 1.0))
    write_image (b, "spec.tif")



|

**Fade 30% of the way between two images**


.. code-block:: python

    a = ImageBufAlgo.mul (ImageBuf("A.exr"), 0.7)
    b = ImageBufAlgo.mul (ImageBuf("B.exr"), 0.3)
    fade = ImageBufAlgo.add (a, b)
    write_image (fade, "fade.exr")



|

**Composite of small foreground over background, with offset**


.. code-block:: python

    fg = ImageBuf ("fg.exr")
    fg.set_origin (512, 89)
    bg = ImageBuf ("bg.exr")
    comp = ImageBufAlgo.over (fg, bg)
    write_image (comp, "composite.exr")


|

**Write multiple ImageBufs into one multi-subimage file**

.. code-block:: python

    bufs = (...)   # Suppose that bufs is a tuple of ImageBuf
    specs = (...)  # specs is a tuple of the specs that describe them

    # Open with intent to write the subimages
    out = ImageOutput.create ("multipart.exr")
    out.open ("multipart.exr", specs)
    for s in range(len(bufs)) :
        if s > 0 :
            out.open ("multipart.exr", specs[s], "AppendSubimage")
        bufs[s].write (out)
    out.close ()
