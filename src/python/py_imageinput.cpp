// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include "py_oiio.h"

namespace PyOpenImageIO {

static py::object
ImageInput_read_image(ImageInput& self, int subimage, int miplevel, int chbegin,
                      int chend, TypeDesc format)
{
    // Allocate our own temp buffer and try to read the into into it.
    // If the read fails, return None.
    self.lock();
    self.seek_subimage(subimage, miplevel);
    ImageSpec spec;
    spec.copy_dimensions(self.spec());
    self.unlock();

    if (format == TypeUnknown)
        format = spec.format;
    chend            = clamp(chend, chbegin + 1, spec.nchannels);
    size_t nchans    = size_t(chend - chbegin);
    size_t pixelsize = size_t(nchans * format.size());
    size_t size      = spec.image_pixels() * pixelsize;
    int dims         = spec.depth > 1 ? 4 : 3;
    std::unique_ptr<char[]> data(new char[size]);
    bool ok;
    {
        py::gil_scoped_release gil;
        ok = self.read_image(subimage, miplevel, chbegin, chend, format,
                             data.get());
    }
    if (ok)
        return make_numpy_array(format, data.release(), dims, nchans,
                                spec.width, spec.height, spec.depth);
    else
        return py::none();
}



static py::object
ImageInput_read_scanlines(ImageInput& self, int subimage, int miplevel,
                          int ybegin, int yend, int z, int chbegin, int chend,
                          TypeDesc format, int dims)
{
    // Allocate our own temp buffer and try to read the scanline into it.
    // If the read fails, return None.
    self.lock();
    self.seek_subimage(subimage, miplevel);
    ImageSpec spec;
    spec.copy_dimensions(self.spec());
    self.unlock();

    if (format == TypeUnknown)
        format = spec.format;
    int width        = spec.width;
    chend            = clamp(chend, chbegin + 1, spec.nchannels);
    size_t nchans    = size_t(chend - chbegin);
    size_t pixelsize = size_t(nchans * format.size());
    size_t size      = size_t((yend - ybegin) * width) * pixelsize;
    std::unique_ptr<char[]> data(new char[size]);
    bool ok;
    {
        py::gil_scoped_release gil;
        ok = self.read_scanlines(subimage, miplevel, ybegin, yend, z, chbegin,
                                 chend, format, data.get());
    }
    if (ok)
        return make_numpy_array(format, data.release(), dims, nchans, width,
                                yend - ybegin, 1);
    else
        return py::none();
}



static py::object
ImageInput_read_tiles(ImageInput& self, int subimage, int miplevel, int xbegin,
                      int xend, int ybegin, int yend, int zbegin, int zend,
                      int chbegin, int chend, TypeDesc format)
{
    // Allocate our own temp buffer and try to read the scanline into it.
    // If the read fails, return None.
    self.lock();
    self.seek_subimage(subimage, miplevel);
    ImageSpec spec;
    spec.copy_dimensions(self.spec());
    self.unlock();

    if (format == TypeUnknown)
        format = spec.format;
    chend            = clamp(chend, chbegin + 1, spec.nchannels);
    size_t nchans    = size_t(chend - chbegin);
    size_t pixelsize = size_t(nchans * format.size());
    size_t size      = (xend - xbegin) * (yend - ybegin) * (zend - zbegin)
                  * pixelsize;
    int dims = spec.tile_depth > 1 ? 4 : 3;
    std::unique_ptr<char[]> data(new char[size]);
    bool ok;
    {
        py::gil_scoped_release gil;
        ok = self.read_tiles(subimage, miplevel, xbegin, xend, ybegin, yend,
                             zbegin, zend, chbegin, chend, format, data.get());
    }
    if (ok)
        return make_numpy_array(format, data.release(), dims, nchans,
                                xend - xbegin, yend - ybegin, zend - zbegin);
    else
        return py::none();
}



py::object
ImageInput_read_native_deep_scanlines_old(ImageInput& self, int ybegin,
                                          int yend, int z, int chbegin,
                                          int chend)
{
    std::unique_ptr<DeepData> dd;
    bool ok = true;
    {
        py::gil_scoped_release gil;
        dd.reset(new DeepData);
        ok = self.read_native_deep_scanlines(ybegin, yend, z, chbegin, chend,
                                             *dd);
    }
    return ok ? py::cast(dd.release()) : py::none();
}



py::object
ImageInput_read_native_deep_scanlines(ImageInput& self, int subimage,
                                      int miplevel, int ybegin, int yend, int z,
                                      int chbegin, int chend)
{
    std::unique_ptr<DeepData> dd;
    bool ok = true;
    {
        py::gil_scoped_release gil;
        dd.reset(new DeepData);
        ok = self.read_native_deep_scanlines(subimage, miplevel, ybegin, yend,
                                             z, chbegin, chend, *dd);
    }
    return ok ? py::cast(dd.release()) : py::none();
}



py::object
ImageInput_read_native_deep_tiles(ImageInput& self, int subimage, int miplevel,
                                  int xbegin, int xend, int ybegin, int yend,
                                  int zbegin, int zend, int chbegin, int chend)
{
    std::unique_ptr<DeepData> dd;
    bool ok = true;
    {
        py::gil_scoped_release gil;
        dd.reset(new DeepData);
        ok = self.read_native_deep_tiles(subimage, miplevel, xbegin, xend,
                                         ybegin, yend, zbegin, zend, chbegin,
                                         chend, *dd);
    }
    return ok ? py::cast(dd.release()) : py::none();
}



py::object
ImageInput_read_native_deep_image(ImageInput& self, int subimage, int miplevel)
{
    std::unique_ptr<DeepData> dd;
    bool ok = true;
    {
        py::gil_scoped_release gil;
        dd.reset(new DeepData);
        ok = self.read_native_deep_image(subimage, miplevel, *dd);
    }
    return ok ? py::cast(dd.release()) : py::none();
}



void
declare_imageinput(py::module& m)
{
    using namespace pybind11::literals;

    py::class_<ImageInput>(m, "ImageInput")
        .def_static(
            "create",
            [](const std::string& filename,
               const std::string& searchpath) -> py::object {
                auto in = ImageInput::create(filename, searchpath);
                return in ? py::cast(in.release()) : py::none();
            },
            "filename"_a, "plugin_searchpath"_a = "")
        .def_static(
            "open",
            [](const std::string& filename) -> py::object {
                auto in = ImageInput::open(filename);
                return in ? py::cast(in.release()) : py::none();
            },
            "filename"_a)
        .def_static(
            "open",
            [](const std::string& filename,
               const ImageSpec& config) -> py::object {
                auto in = ImageInput::open(filename, &config);
                return in ? py::cast(in.release()) : py::none();
            },
            "filename"_a, "config"_a)
        .def("format_name", &ImageInput::format_name)
        .def("valid_file", &ImageInput::valid_file)
        .def("spec", [](ImageInput& self) { return self.spec(); })
        .def(
            "spec",
            [](ImageInput& self, int subimage, int miplevel) {
                return self.spec(subimage, miplevel);
            },
            "subimage"_a, "miplevel"_a = 0)
        .def(
            "spec_dimensions",
            [](ImageInput& self, int subimage, int miplevel) {
                return self.spec_dimensions(subimage, miplevel);
            },
            "subimage"_a, "miplevel"_a = 0)
        .def("supports",
             [](const ImageInput& self, const std::string& feature) {
                 return self.supports(feature);
             })
        .def("close", &ImageInput::close)
        .def("current_subimage", &ImageInput::current_subimage)
        .def("current_miplevel", &ImageInput::current_miplevel)
        .def("seek_subimage",
             [](ImageInput& self, int subimage, int miplevel) {
                 py::gil_scoped_release gil;
                 return self.seek_subimage(subimage, miplevel);
             })
        .def(
            "read_image",
            [](ImageInput& self, int subimage, int miplevel, int chbegin,
               int chend, TypeDesc format) -> py::object {
                return ImageInput_read_image(self, subimage, miplevel, chbegin,
                                             chend, format);
            },
            "subimage"_a, "miplevel"_a, "chbegin"_a, "chend"_a,
            "format"_a = TypeFloat)
        .def(
            "read_image",
            [](ImageInput& self, int chbegin, int chend,
               TypeDesc format) -> py::object {
                return ImageInput_read_image(self, 0, 0, chbegin, chend,
                                             format);
            },
            "chbegin"_a, "chend"_a, "format"_a = TypeFloat)
        .def(
            "read_image",
            [](ImageInput& self, TypeDesc format) -> py::object {
                return ImageInput_read_image(self, self.current_subimage(),
                                             self.current_miplevel(), 0, 10000,
                                             format);
            },
            "format"_a = TypeFloat)
        .def(
            "read_scanline",
            [](ImageInput& self, int y, int z, TypeDesc format) -> py::object {
                return ImageInput_read_scanlines(self, self.current_subimage(),
                                                 self.current_miplevel(), y,
                                                 y + 1, z, 0, 10000, format, 2);
            },
            "y"_a, "z"_a = 0, "format"_a = TypeFloat)
        .def(
            "read_scanlines",
            [](ImageInput& self, int subimage, int miplevel, int ybegin,
               int yend, int z, int chbegin, int chend,
               TypeDesc format) -> py::object {
                return ImageInput_read_scanlines(self, subimage, miplevel,
                                                 ybegin, yend, z, chbegin,
                                                 chend, format, 3);
            },
            "subimage"_a, "miplevel"_a, "ybegin"_a, "yend"_a, "z"_a,
            "chbegin"_a, "chend"_a, "format"_a = TypeFloat)
        .def(
            "read_scanlines",
            [](ImageInput& self, int ybegin, int yend, int z, int chbegin,
               int chend, TypeDesc format) -> py::object {
                return ImageInput_read_scanlines(self, self.current_subimage(),
                                                 self.current_miplevel(),
                                                 ybegin, yend, z, chbegin,
                                                 chend, format, 3);
            },
            "ybegin"_a, "yend"_a, "z"_a, "chbegin"_a, "chend"_a,
            "format"_a = TypeFloat)
        .def(
            "read_tiles",
            [](ImageInput& self, int subimage, int miplevel, int xbegin,
               int xend, int ybegin, int yend, int zbegin, int zend,
               int chbegin, int chend, TypeDesc format) -> py::object {
                return ImageInput_read_tiles(self, subimage, miplevel, xbegin,
                                             xend, ybegin, yend, zbegin, zend,
                                             chbegin, chend, format);
            },
            "subimage"_a, "miplevel"_a, "xbegin"_a, "xend"_a, "ybegin"_a,
            "yend"_a, "zbegin"_a, "zend"_a, "chbegin"_a, "chend"_a,
            "format"_a = TypeFloat)
        .def(
            "read_tiles",
            [](ImageInput& self, int xbegin, int xend, int ybegin, int yend,
               int zbegin, int zend, int chbegin, int chend,
               TypeDesc format) -> py::object {
                return ImageInput_read_tiles(self, self.current_subimage(),
                                             self.current_miplevel(), xbegin,
                                             xend, ybegin, yend, zbegin, zend,
                                             chbegin, chend, format);
            },
            "xbegin"_a, "xend"_a, "ybegin"_a, "yend"_a, "zbegin"_a, "zend"_a,
            "chbegin"_a, "chend"_a, "format"_a = TypeFloat)
        .def(
            "read_tile",
            [](ImageInput& self, int x, int y, int z,
               TypeDesc format) -> py::object {
                const ImageSpec& spec(self.spec());
                return ImageInput_read_tiles(self, self.current_subimage(),
                                             self.current_miplevel(), x,
                                             x + spec.tile_width, y,
                                             y + spec.tile_height, z,
                                             z + std::max(1, spec.tile_depth),
                                             0, spec.nchannels, format);
            },
            "x"_a, "y"_a, "z"_a, "format"_a = TypeFloat)
        .def("read_native_deep_scanlines",
             &ImageInput_read_native_deep_scanlines, "subimage"_a, "miplevel"_a,
             "ybegin"_a, "yend"_a, "z"_a, "chbegin"_a, "chend"_a)
        .def(
            "read_native_deep_scanlines",  // DEPRECATED(1.9), keep for back compatibility
            [](ImageInput& self, int ybegin, int yend, int z, int chbegin,
               int chend) {
                return ImageInput_read_native_deep_scanlines_old(self, ybegin,
                                                                 yend, z,
                                                                 chbegin,
                                                                 chend);
            },
            "ybegin"_a, "yend"_a, "z"_a, "chbegin"_a, "chend"_a)
        .def("read_native_deep_tiles", &ImageInput_read_native_deep_tiles,
             "subimage"_a, "miplevel"_a, "xbegin"_a, "xend"_a, "ybegin"_a,
             "yend"_a, "zbegin"_a, "zend"_a, "chbegin"_a, "chend"_a)
        .def(
            "read_native_deep_tiles",  // DEPRECATED(1.9), keep for back compatibility
            [](ImageInput& self, int xbegin, int xend, int ybegin, int yend,
               int zbegin, int zend, int chbegin, int chend) {
                return ImageInput_read_native_deep_tiles(self, 0, 0, xbegin,
                                                         xend, ybegin, yend,
                                                         zbegin, zend, chbegin,
                                                         chend);
            },
            "xbegin"_a, "xend"_a, "ybegin"_a, "yend"_a, "zbegin"_a, "zend"_a,
            "chbegin"_a, "chend"_a)
        .def("read_native_deep_image", &ImageInput_read_native_deep_image,
             "subimage"_a = 0, "miplevel"_a = 0)
        .def(
            "get_thumbnail",
            [](ImageInput& self, int subimage) {
                ImageBuf buf;
                self.get_thumbnail(buf, subimage);
                return buf;
            },
            "subimage"_a = 0)
        .def_property_readonly("has_error", &ImageInput::has_error)
        .def(
            "geterror",
            [](ImageInput& self, bool clear) {
                return PY_STR(self.geterror(clear));
            },
            "clear"_a = true);
}

}  // namespace PyOpenImageIO
