// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include "py_oiio.h"

#include <memory>

#include <OpenImageIO/platform.h>


namespace PyOpenImageIO {



py::tuple
ImageBuf_getpixel(const ImageBuf& buf, int x, int y, int z = 0,
                  const std::string& wrapname = "black")
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    int nchans              = buf.nchannels();
    float* pixel            = OIIO_ALLOCA(float, nchans);
    buf.getpixel(x, y, z, pixel, nchans, wrap);
    return C_to_tuple(pixel, nchans);
}



py::tuple
ImageBuf_interppixel(const ImageBuf& buf, float x, float y,
                     const std::string& wrapname = "black")
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    int nchans              = buf.nchannels();
    float* pixel            = OIIO_ALLOCA(float, nchans);
    buf.interppixel(x, y, pixel, wrap);
    return C_to_tuple(pixel, nchans);
}



py::tuple
ImageBuf_interppixel_NDC(const ImageBuf& buf, float x, float y,
                         const std::string& wrapname = "black")
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    int nchans              = buf.nchannels();
    float* pixel            = OIIO_ALLOCA(float, nchans);
    buf.interppixel_NDC(x, y, pixel, wrap);
    return C_to_tuple(pixel, nchans);
}



py::tuple
ImageBuf_interppixel_bicubic(const ImageBuf& buf, float x, float y,
                             const std::string& wrapname = "black")
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    int nchans              = buf.nchannels();
    float* pixel            = OIIO_ALLOCA(float, nchans);
    buf.interppixel_bicubic(x, y, pixel, wrap);
    return C_to_tuple(pixel, nchans);
}



py::tuple
ImageBuf_interppixel_bicubic_NDC(const ImageBuf& buf, float x, float y,
                                 const std::string& wrapname = "black")
{
    ImageBuf::WrapMode wrap = ImageBuf::WrapMode_from_string(wrapname);
    int nchans              = buf.nchannels();
    float* pixel            = OIIO_ALLOCA(float, nchans);
    buf.interppixel_bicubic_NDC(x, y, pixel, wrap);
    return C_to_tuple(pixel, nchans);
}



void
ImageBuf_setpixel(ImageBuf& buf, int x, int y, int z, py::object p)
{
    std::vector<float> pixel;
    py_to_stdvector(pixel, p);
    if (pixel.size())
        buf.setpixel(x, y, z, pixel);
}

void
ImageBuf_setpixel2(ImageBuf& buf, int x, int y, py::object p)
{
    ImageBuf_setpixel(buf, x, y, 0, p);
}


void
ImageBuf_setpixel1(ImageBuf& buf, int i, py::object p)
{
    std::vector<float> pixel;
    py_to_stdvector(pixel, p);
    if (pixel.size())
        buf.setpixel(i, pixel);
}



py::object
ImageBuf_get_pixels(const ImageBuf& buf, TypeDesc format, ROI roi = ROI::All())
{
    // Allocate our own temp buffer and try to read the image into it.
    // If the read fails, return None.
    if (!roi.defined())
        roi = buf.roi();
    roi.chend = std::min(roi.chend, buf.nchannels());

    size_t size = (size_t)roi.npixels() * roi.nchannels() * format.size();
    std::unique_ptr<char[]> data(new char[size]);
    if (buf.get_pixels(roi, format, &data[0]))
        return make_numpy_array(format, data.release(),
                                buf.spec().depth > 1 ? 4 : 3, roi.nchannels(),
                                roi.width(), roi.height(), roi.depth());
    else
        return py::none();
}



void
ImageBuf_set_deep_value(ImageBuf& buf, int x, int y, int z, int c, int s,
                        float value)
{
    buf.set_deep_value(x, y, z, c, s, value);
}

void
ImageBuf_set_deep_value_uint(ImageBuf& buf, int x, int y, int z, int c, int s,
                             uint32_t value)
{
    buf.set_deep_value(x, y, z, c, s, value);
}



bool
ImageBuf_set_pixels_buffer(ImageBuf& self, ROI roi, py::buffer& buffer)
{
    if (!roi.defined())
        roi = self.roi();
    roi.chend   = std::min(roi.chend, self.nchannels());
    size_t size = (size_t)roi.npixels() * roi.nchannels();
    if (size == 0) {
        return true;  // done
    }
    oiio_bufinfo buf(buffer.request(), roi.nchannels(), roi.width(),
                     roi.height(), roi.depth(), self.spec().depth > 1 ? 3 : 2);
    if (!buf.data || buf.error.size()) {
        self.errorfmt("set_pixels error: {}",
                      buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    if (!buf.data || buf.size != size) {
        self.errorfmt(
            "ImageBuf.set_pixels: array size ({}) did not match ROI size w={} h={} d={} ch={} (total {})",
            buf.size, roi.width(), roi.height(), roi.depth(), roi.nchannels(),
            size);
        return false;
    }

    py::gil_scoped_release gil;
    return self.set_pixels(roi, buf.format, buf.data, buf.xstride, buf.ystride,
                           buf.zstride);
}



void
ImageBuf_set_write_format(ImageBuf& self, const py::object& py_channelformats)
{
    std::vector<TypeDesc> formats;
    py_to_stdvector(formats, py_channelformats);
    self.set_write_format(formats);
}



void
declare_imagebuf(py::module& m)
{
    using namespace pybind11::literals;

    py::class_<ImageBuf>(m, "ImageBuf")
        .def(py::init<>())
        .def(py::init<const std::string&>())
        .def(py::init<const std::string&, int, int>())
        .def(py::init<const ImageSpec&>())
        .def(py::init([](const ImageSpec& spec, bool zero) {
            auto z = zero ? InitializePixels::Yes : InitializePixels::No;
            return ImageBuf(spec, z);
        }))
        .def(py::init([](const std::string& name, int subimage, int miplevel,
                         const ImageSpec& config) {
                 return ImageBuf(name, subimage, miplevel, nullptr, &config);
             }),
             "name"_a, "subimage"_a, "miplevel"_a, "config"_a)
        .def("clear", &ImageBuf::clear)
        .def(
            "reset",
            [](ImageBuf& self, const std::string& name, int subimage,
               int miplevel) { self.reset(name, subimage, miplevel); },
            "name"_a, "subimage"_a = 0, "miplevel"_a = 0)
        .def(
            "reset",
            [](ImageBuf& self, const std::string& name, int subimage,
               int miplevel, const ImageSpec& config) {
                self.reset(name, subimage, miplevel, nullptr, &config);
            },
            "name"_a, "subimage"_a = 0, "miplevel"_a = 0,
            "config"_a = ImageSpec())
        .def(
            "reset",
            [](ImageBuf& self, const ImageSpec& spec, bool zero) {
                auto z = zero ? InitializePixels::Yes : InitializePixels::No;
                self.reset(spec, z);
            },
            "spec"_a, "zero"_a = true)
        .def_property_readonly("initialized",
                               [](const ImageBuf& self) {
                                   return self.initialized();
                               })
        .def(
            "init_spec",
            [](ImageBuf& self, std::string filename, int subimage,
               int miplevel) {
                py::gil_scoped_release gil;
                self.init_spec(filename, subimage, miplevel);
            },
            "filename"_a, "subimage"_a = 0, "miplevel"_a = 0)
        .def(
            "read",
            [](ImageBuf& self, int subimage, int miplevel, int chbegin,
               int chend, bool force, TypeDesc convert) {
                py::gil_scoped_release gil;
                return self.read(subimage, miplevel, chbegin, chend, force,
                                 convert);
            },
            "subimage"_a, "miplevel"_a, "chbegin"_a, "chend"_a, "force"_a,
            "convert"_a)
        .def(
            "read",
            [](ImageBuf& self, int subimage, int miplevel, bool force,
               TypeDesc convert) {
                py::gil_scoped_release gil;
                return self.read(subimage, miplevel, force, convert);
            },
            "subimage"_a = 0, "miplevel"_a = 0, "force"_a = false,
            "convert"_a = TypeUnknown)

        .def(
            "write",
            [](ImageBuf& self, const std::string& filename, TypeDesc dtype,
               const std::string& fileformat) {
                py::gil_scoped_release gil;
                return self.write(filename, dtype, fileformat);
            },
            "filename"_a, "dtype"_a = TypeUnknown, "fileformat"_a = "")
        .def(
            "write",
            [](ImageBuf& self, ImageOutput& out) {
                py::gil_scoped_release gil;
                return self.write(&out);
            },
            "out"_a)
        .def(
            "make_writable",
            [](ImageBuf& self, bool keep_cache_type) {
                py::gil_scoped_release gil;
                return self.make_writable(keep_cache_type);
            },
            "keep_cache_type"_a = false)
        // DEPRECATED(2.2): nonstandard spelling
        .def(
            "make_writeable",
            [](ImageBuf& self, bool keep_cache_type) {
                py::gil_scoped_release gil;
                return self.make_writable(keep_cache_type);
            },
            "keep_cache_type"_a = false)
        .def("set_write_format", &ImageBuf_set_write_format)
        // FIXME -- write(ImageOut&)
        .def("set_write_tiles", &ImageBuf::set_write_tiles, "width"_a = 0,
             "height"_a = 0, "depth"_a = 0)
        .def("spec", &ImageBuf::spec,
             py::return_value_policy::reference_internal)
        .def("nativespec", &ImageBuf::nativespec,
             py::return_value_policy::reference_internal)
        .def("specmod", &ImageBuf::specmod,
             py::return_value_policy::reference_internal)
        .def_property_readonly("has_thumbnail",
                               [](const ImageBuf& self) {
                                   return self.has_thumbnail();
                               })
        .def("clear_thumbnail", &ImageBuf::clear_thumbnail)
        .def("set_thumbnail", &ImageBuf::set_thumbnail, "thumb"_a)
        .def("thumbnail",
             [](const ImageBuf& self) { return *self.thumbnail(); })
        .def_property_readonly("name",
                               [](const ImageBuf& self) {
                                   return PY_STR(self.name());
                               })
        .def_property_readonly("file_format_name",
                               [](const ImageBuf& self) {
                                   return PY_STR(self.file_format_name());
                               })
        .def_property_readonly("subimage", &ImageBuf::subimage)
        .def_property_readonly("nsubimages", &ImageBuf::nsubimages)
        .def_property_readonly("miplevel", &ImageBuf::miplevel)
        .def_property_readonly("nmiplevels", &ImageBuf::nmiplevels)
        .def_property_readonly("nchannels", &ImageBuf::nchannels)
        .def_property("orientation", &ImageBuf::orientation,
                      &ImageBuf::set_orientation)
        .def_property_readonly("oriented_width", &ImageBuf::oriented_width)
        .def_property_readonly("oriented_height", &ImageBuf::oriented_height)
        .def_property_readonly("oriented_x", &ImageBuf::oriented_x)
        .def_property_readonly("oriented_y", &ImageBuf::oriented_y)
        .def_property_readonly("oriented_full_width",
                               &ImageBuf::oriented_full_width)
        .def_property_readonly("oriented_full_height",
                               &ImageBuf::oriented_full_height)
        .def_property_readonly("oriented_full_x", &ImageBuf::oriented_full_x)
        .def_property_readonly("oriented_full_y", &ImageBuf::oriented_full_y)
        .def_property_readonly("xbegin", &ImageBuf::xbegin)
        .def_property_readonly("xend", &ImageBuf::xend)
        .def_property_readonly("ybegin", &ImageBuf::ybegin)
        .def_property_readonly("yend", &ImageBuf::yend)
        .def_property_readonly("zbegin", &ImageBuf::zbegin)
        .def_property_readonly("zend", &ImageBuf::zend)
        .def_property_readonly("xmin", &ImageBuf::xmin)
        .def_property_readonly("xmax", &ImageBuf::xmax)
        .def_property_readonly("ymin", &ImageBuf::ymin)
        .def_property_readonly("ymax", &ImageBuf::ymax)
        .def_property_readonly("zmin", &ImageBuf::zmin)
        .def_property_readonly("zmax", &ImageBuf::zmax)
        .def_property_readonly("roi", &ImageBuf::roi)
        .def_property("roi_full", &ImageBuf::roi_full, &ImageBuf::set_roi_full)
        .def("set_origin", &ImageBuf::set_origin, "x"_a, "y"_a, "z"_a = 0)
        .def("set_full", &ImageBuf::set_full)
        .def_property_readonly("pixels_valid", &ImageBuf::pixels_valid)
        .def_property_readonly("pixeltype", &ImageBuf::pixeltype)
        .def_property_readonly("has_error", &ImageBuf::has_error)
        .def(
            "geterror",
            [](const ImageBuf& self, bool clear) {
                return PY_STR(self.geterror(clear));
            },
            "clear"_a = true)

        .def("pixelindex", &ImageBuf::pixelindex, "x"_a, "y"_a, "z"_a,
             "check_range"_a = false)
        .def("copy_metadata", &ImageBuf::copy_metadata)
        .def("copy_pixels", &ImageBuf::copy_pixels)
        .def(
            "copy",
            [](ImageBuf& self, const ImageBuf& src, TypeDesc format) {
                py::gil_scoped_release gil;
                return self.copy(src, format);
            },
            "src"_a, "format"_a = TypeUnknown)
        .def(
            "copy",
            [](const ImageBuf& src, TypeDesc format) {
                py::gil_scoped_release gil;
                return src.copy(format);
            },
            "format"_a = TypeUnknown)
        .def("swap", &ImageBuf::swap)
        .def("getchannel", &ImageBuf::getchannel, "x"_a, "y"_a, "z"_a, "c"_a,
             "wrap"_a = "black")
        .def("getpixel", &ImageBuf_getpixel, "x"_a, "y"_a, "z"_a = 0,
             "wrap"_a = "black")

        .def("interppixel", &ImageBuf_interppixel, "x"_a, "y"_a,
             "wrap"_a = "black")
        .def("interppixel_NDC", &ImageBuf_interppixel_NDC, "x"_a, "y"_a,
             "wrap"_a = "black")
        .def("interppixel_NDC_full", &ImageBuf_interppixel_NDC, "x"_a, "y"_a,
             "wrap"_a = "black")
        .def("interppixel_bicubic", &ImageBuf_interppixel_bicubic, "x"_a, "y"_a,
             "wrap"_a = "black")
        .def("interppixel_bicubic_NDC", &ImageBuf_interppixel_bicubic_NDC,
             "x"_a, "y"_a, "wrap"_a = "black")
        .def("setpixel", &ImageBuf_setpixel, "x"_a, "y"_a, "z"_a, "pixel"_a)
        .def("setpixel", &ImageBuf_setpixel2, "x"_a, "y"_a, "pixel"_a)
        .def("setpixel", &ImageBuf_setpixel1, "i"_a, "pixel"_a)
        .def("get_pixels", &ImageBuf_get_pixels, "format"_a = TypeFloat,
             "roi"_a = ROI::All())
        .def("set_pixels", &ImageBuf_set_pixels_buffer, "roi"_a, "pixels"_a)

        .def_property_readonly("deep", &ImageBuf::deep)
        .def("deep_samples", &ImageBuf::deep_samples, "x"_a, "y"_a, "z"_a = 0)
        .def("set_deep_samples", &ImageBuf::set_deep_samples, "x"_a, "y"_a,
             "z"_a = 0, "nsamples"_a = 1)
        .def("deep_insert_samples", &ImageBuf::deep_insert_samples, "x"_a,
             "y"_a, "z"_a = 0, "samplepos"_a, "nsamples"_a = 1)
        .def("deep_erase_samples", &ImageBuf::deep_erase_samples, "x"_a, "y"_a,
             "z"_a = 0, "samplepos"_a, "nsamples"_a = 1)
        .def("deep_value", &ImageBuf::deep_value, "x"_a, "y"_a, "z"_a,
             "channel"_a, "sample"_a)
        .def("deep_value_uint", &ImageBuf::deep_value_uint, "x"_a, "y"_a, "z"_a,
             "channel"_a, "sample"_a)
        .def("set_deep_value", &ImageBuf_set_deep_value, "x"_a, "y"_a, "z"_a,
             "channel"_a, "sample"_a, "value"_a = 0.0f)
        .def("set_deep_value_uint", &ImageBuf_set_deep_value_uint, "x"_a, "y"_a,
             "z"_a, "channel"_a, "sample"_a, "value"_a = 0)
        .def(
            "deepdata", [](ImageBuf& self) { return *self.deepdata(); },
            py::return_value_policy::reference_internal)

        // FIXME -- do we want to provide pixel iterators?
        ;
}

}  // namespace PyOpenImageIO
