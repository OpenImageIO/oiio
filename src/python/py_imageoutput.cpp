// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

// Avoid a compiler warning from a duplication in tiffconf.h/pyconfig.h
#undef SIZEOF_LONG
#include "py_oiio.h"

namespace PyOpenImageIO {


bool
ImageOutput_open_specs(ImageOutput& self, const std::string& name,
                       py::tuple& specs)
{
    const size_t length = len(specs);
    if (length == 0)
        return false;
    std::vector<ImageSpec> Cspecs(length);
    for (size_t i = 0; i < length; ++i) {
        auto s = specs[i];
        if (py::isinstance<ImageSpec>(s))
            Cspecs[i] = s.cast<ImageSpec>();
        else
            return false;  // Tuple item was not an ImageSpec
    }
    return self.open(name, int(length), &Cspecs[0]);
}



bool
ImageOutput_write_scanline(ImageOutput& self, int y, int z, py::buffer& buffer)
{
    const ImageSpec& spec(self.spec());
    if (spec.tile_width != 0) {
        self.errorfmt("Cannot write scanlines to a filed file.");
        return false;
    }
    oiio_bufinfo buf(buffer.request(), spec.nchannels, spec.width, 1, 1, 1);
    if (!buf.data || buf.error.size()) {
        self.errorfmt("Pixel data array error: {}",
                      buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    if (static_cast<int>(buf.size)
        < self.spec().width * self.spec().nchannels) {
        self.errorfmt("write_scanlines was not passed a long enough array");
        return false;
    }
    py::gil_scoped_release gil;
    return self.write_scanline(y, z, buf.format, buf.data, buf.xstride);
}



bool
ImageOutput_write_scanlines(ImageOutput& self, int ybegin, int yend, int z,
                            py::buffer& buffer)
{
    const ImageSpec& spec(self.spec());
    if (spec.tile_width != 0) {
        self.errorfmt("Cannot write scanlines to a filed file.");
        return false;
    }
    oiio_bufinfo buf(buffer.request(), spec.nchannels, spec.width,
                     yend - ybegin, 1, 2);
    if (!buf.data || buf.error.size()) {
        self.errorfmt("Pixel data array error: {}",
                      buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    if (static_cast<int>(buf.size)
        < self.spec().width * self.spec().nchannels * (yend - ybegin)) {
        self.errorfmt("write_scanlines was not passed a long enough array");
        return false;
    }
    py::gil_scoped_release gil;
    return self.write_scanlines(ybegin, yend, z, buf.format, buf.data,
                                buf.xstride, buf.ystride);
}



bool
ImageOutput_write_tile(ImageOutput& self, int x, int y, int z,
                       py::buffer& buffer)
{
    const ImageSpec& spec(self.spec());
    if (spec.tile_width == 0) {
        self.errorf("Cannot write tiles to a scanline file.");
        return false;
    }
    oiio_bufinfo buf(buffer.request(), spec.nchannels, spec.tile_width,
                     spec.tile_height, spec.tile_depth,
                     spec.tile_depth > 1 ? 3 : 2);
    if (!buf.data || buf.error.size()) {
        self.errorf("Pixel data array error: %s",
                    buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    if (buf.size < self.spec().tile_pixels() * self.spec().nchannels) {
        self.errorfmt("write_tile was not passed a long enough array");
        return false;
    }
    py::gil_scoped_release gil;
    return self.write_tile(x, y, z, buf.format, buf.data, buf.xstride,
                           buf.ystride, buf.zstride);
}



bool
ImageOutput_write_tiles(ImageOutput& self, int xbegin, int xend, int ybegin,
                        int yend, int zbegin, int zend, py::buffer& buffer)
{
    const ImageSpec& spec(self.spec());
    if (spec.tile_width == 0) {
        self.errorf("Cannot write tiles to a scanline file.");
        return false;
    }
    oiio_bufinfo buf(buffer.request(), spec.nchannels, xend - xbegin,
                     yend - ybegin, zend - zbegin, spec.tile_depth > 1 ? 3 : 2);
    if (!buf.data || buf.error.size()) {
        self.errorf("Pixel data array error: %s",
                    buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    if (static_cast<int>(buf.size) < (xend - xbegin) * (yend - ybegin)
                                         * (zend - zbegin)
                                         * self.spec().nchannels) {
        self.errorfmt("write_tiles was not passed a long enough array");
        return false;
    }
    py::gil_scoped_release gil;
    return self.write_tiles(xbegin, xend, ybegin, yend, zbegin, zend,
                            buf.format, buf.data, buf.xstride, buf.ystride,
                            buf.zstride);
}



bool
ImageOutput_write_image(ImageOutput& self, py::buffer& buffer)
{
    const ImageSpec& spec(self.spec());
    oiio_bufinfo buf(buffer.request(), spec.nchannels, spec.width, spec.height,
                     spec.depth, spec.depth > 1 ? 3 : 2);
    if (!buf.data || buf.size < spec.image_pixels() * spec.nchannels
        || buf.error.size()) {
        self.errorf("Pixel data array error: %s",
                    buf.error.size() ? buf.error.c_str() : "unspecified");
        return false;  // failed sanity checks
    }
    py::gil_scoped_release gil;
    return self.write_image(buf.format, buf.data, buf.xstride, buf.ystride,
                            buf.zstride);
}



bool
ImageOutput_write_deep_scanlines(ImageOutput& self, int ybegin, int yend, int z,
                                 const DeepData& deepdata)
{
    py::gil_scoped_release gil;
    return self.write_deep_scanlines(ybegin, yend, z, deepdata);
}


bool
ImageOutput_write_deep_tiles(ImageOutput& self, int xbegin, int xend,
                             int ybegin, int yend, int zbegin, int zend,
                             const DeepData& deepdata)
{
    py::gil_scoped_release gil;
    return self.write_deep_tiles(xbegin, xend, ybegin, yend, zbegin, zend,
                                 deepdata);
}


bool
ImageOutput_write_deep_image(ImageOutput& self, const DeepData& deepdata)
{
    py::gil_scoped_release gil;
    return self.write_deep_image(deepdata);
}



void
declare_imageoutput(py::module& m)
{
    using namespace pybind11::literals;

    py::class_<ImageOutput>(m, "ImageOutput")
        .def_static(
            "create",
            [](const std::string& filename,
               const std::string& searchpath) -> py::object {
                auto out(ImageOutput::create(filename, searchpath));
                return out ? py::cast(out.release()) : py::none();
            },
            "filename"_a, "plugin_searchpath"_a = "")
        .def("format_name", &ImageOutput::format_name)
        .def("supports",
             [](const ImageOutput& self, const std::string& feature) {
                 return self.supports(feature);
             })
        .def("spec", &ImageOutput::spec)
        .def(
            "open",
            [](ImageOutput& self, const std::string& name,
               const ImageSpec& newspec, const std::string& modestr) {
                ImageOutput::OpenMode mode = ImageOutput::Create;
                if (Strutil::iequals(modestr, "AppendSubimage"))
                    mode = ImageOutput::AppendSubimage;
                else if (Strutil::iequals(modestr, "AppendMIPLevel"))
                    mode = ImageOutput::AppendMIPLevel;
                else if (!Strutil::iequals(modestr, "Create"))
                    throw std::invalid_argument(
                        Strutil::sprintf("Unknown open mode '%s'", modestr));
                return self.open(name, newspec, mode);
            },
            "filename"_a, "spec"_a, "mode"_a = "Create")
        .def(
            "open",
            [](ImageOutput& self, const std::string& name,
               const std::vector<ImageSpec>& specs) {
                return self.open(name, (int)specs.size(), &specs[0]);
            },
            "filename"_a, "specs"_a)
        .def("open", &ImageOutput_open_specs)
        .def("close", [](ImageOutput& self) { return self.close(); })
        .def("write_image", &ImageOutput_write_image)
        .def("write_scanline", &ImageOutput_write_scanline, "y"_a, "z"_a,
             "pixels"_a)
        .def("write_scanlines", &ImageOutput_write_scanlines, "ybegin"_a,
             "yend"_a, "z"_a, "pixels"_a)
        .def("write_tile", &ImageOutput_write_tile, "x"_a, "y"_a, "z"_a,
             "pixels"_a)
        .def("write_tiles", &ImageOutput_write_tiles, "xbegin"_a, "xend"_a,
             "ybegin"_a, "yend"_a, "zbegin"_a, "zend"_a, "pixels"_a)
        .def("write_deep_scanlines", &ImageOutput_write_deep_scanlines,
             "ybegin"_a, "yend"_a, "z"_a, "deepdata"_a)
        .def("write_deep_tiles", &ImageOutput_write_deep_tiles, "xbegin"_a,
             "xend"_a, "ybegin"_a, "yend"_a, "zbegin"_a, "zend"_a, "deepdata"_a)
        .def("write_deep_image", &ImageOutput_write_deep_image)
        .def("set_thumbnail",
             [](ImageOutput& self, const ImageBuf& thumb) {
                 return self.set_thumbnail(thumb);
             })
        .def("copy_image", [](ImageOutput& self,
                              ImageInput& in) { return self.copy_image(&in); })
        .def_property_readonly("has_error", &ImageOutput::has_error)
        .def(
            "geterror",
            [](ImageOutput& self, bool clear) {
                return PY_STR(self.geterror(clear));
            },
            "clear"_a = true);
}

}  // namespace PyOpenImageIO
