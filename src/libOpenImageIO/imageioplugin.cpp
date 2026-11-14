// Copyright Contributors to the OpenImageIO project.
// SPDX-License-Identifier: Apache-2.0
// https://github.com/AcademySoftwareFoundation/OpenImageIO

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/plugin.h>
#include <OpenImageIO/strutil.h>

#include "imageio_pvt.h"


OIIO_NAMESPACE_BEGIN
using namespace pvt;

typedef std::map<std::string, ImageInput::Creator> InputPluginMap;
typedef std::map<std::string, ImageOutput::Creator> OutputPluginMap;
typedef const char* (*PluginLibVersionFunc)();

namespace {

// Map format name (and extension) to ImageInput creation
static InputPluginMap input_formats;
// Map format name (and extensions) to ImageOutput creation
static OutputPluginMap output_formats;
// Map format name to plugin handle
static std::map<std::string, Plugin::Handle> plugin_handles;
// Map format name to full path
static std::map<std::string, std::string> plugin_filepaths;
// Map format name to underlying implementation library
static std::map<std::string, std::string> format_library_versions;
// Map extension name to format name
static std::map<std::string, std::string> extension_to_format_map;


// Vector of all format names, in "priority" order (most common formats first).
// This should be guarded by imageio_mutex.
static std::vector<ustring> format_list_vector;

// Which format names and extensions are procedural (not reading from files)
static std::set<std::string> procedural_plugins;

static std::string pattern = Strutil::fmt::format(".imageio.{}",
                                                  Plugin::plugin_extension());


inline void
add_if_missing(std::vector<std::string>& vec, const std::string& val)
{
    if (std::find(vec.begin(), vec.end(), val) == vec.end())
        vec.push_back(val);
}

}  // namespace



// Same as declare_imageio_format except that ownership of imageio_mutex is implied
void
declare_imageio_format_locked(const std::string& format_name,
                              ImageInput::Creator input_creator,
                              const char** input_extensions,
                              ImageOutput::Creator output_creator,
                              const char** output_extensions,
                              const char* lib_version)
{
    std::vector<std::string> all_extensions;
    // Look for input creator and list of supported extensions
    if (input_creator) {
        for (const char** e = input_extensions; e && *e; ++e) {
            std::string ext = Strutil::lower(*e);
            if (input_formats.find(ext) == input_formats.end()) {
                input_formats[ext] = input_creator;
                add_if_missing(all_extensions, ext);
            }
        }
        if (input_formats.find(format_name) == input_formats.end())
            input_formats[format_name] = input_creator;
    }

    // Look for output creator and list of supported extensions
    if (output_creator) {
        for (const char** e = output_extensions; e && *e; ++e) {
            std::string ext = Strutil::lower(*e);
            if (output_formats.find(ext) == output_formats.end()) {
                output_formats[ext] = output_creator;
                add_if_missing(all_extensions, ext);
            }
        }
        if (output_formats.find(format_name) == output_formats.end())
            output_formats[format_name] = output_creator;
    }

    // Populate the extension -> format name map
    for (const char** e = input_extensions; e && *e; ++e) {
        std::string ext = Strutil::lower(*e);
        if (extension_to_format_map.find(ext) == extension_to_format_map.end())
            extension_to_format_map[ext] = format_name;
    }
    for (const char** e = output_extensions; e && *e; ++e) {
        std::string ext = Strutil::lower(*e);
        if (extension_to_format_map.find(ext) == extension_to_format_map.end())
            extension_to_format_map[ext] = format_name;
    }

    // Add the name to the master list of format_names, and extensions to
    // their master list.
    format_list_vector.emplace_back(Strutil::lower(format_name));
    if (format_list.length())
        format_list += std::string(",");
    format_list += format_name;
    if (input_creator) {
        if (input_format_list.length())
            input_format_list += std::string(",");
        input_format_list += format_name;
    }
    if (output_creator) {
        if (output_format_list.length())
            output_format_list += std::string(",");
        output_format_list += format_name;
    }
    if (extension_list.length())
        extension_list += std::string(";");
    extension_list += format_name + std::string(":");
    extension_list += Strutil::join(all_extensions, ",");
    if (lib_version) {
        format_library_versions[format_name] = lib_version;
        if (library_list.length())
            library_list += std::string(";");
        library_list += Strutil::fmt::format("{}:{}", format_name, lib_version);
        // std::cout << format_name << ": " << lib_version << "\n";
    }
}

OIIO_NAMESPACE_END


OIIO_NAMESPACE_3_1_BEGIN

/// Register the input and output 'create' routine and list of file
/// extensions for a particular format.
void
declare_imageio_format(const std::string& format_name,
                       ImageInput::Creator input_creator,
                       const char** input_extensions,
                       ImageOutput::Creator output_creator,
                       const char** output_extensions, const char* lib_version)
{
    std::lock_guard<std::recursive_mutex> lock(OIIO::pvt::imageio_mutex);
    declare_imageio_format_locked(format_name, input_creator, input_extensions,
                                  output_creator, output_extensions,
                                  lib_version);
}



bool
is_imageio_format_name(string_view name)
{
    ustring namelower(Strutil::lower(name));

    std::unique_lock<std::recursive_mutex> lock(imageio_mutex);
    // If we were called before any plugins were loaded at all, catalog now
    if (!format_list_vector.size()) {
        lock.unlock();
        // catalog_all_plugins() will lock imageio_mutex.
        OIIO::pvt::catalog_all_plugins(OIIO::pvt::plugin_searchpath.string());
        lock.lock();
    }
    for (const auto& n : format_list_vector)
        if (namelower == n)
            return true;
    return false;
}

OIIO_NAMESPACE_3_1_END



OIIO_NAMESPACE_BEGIN

static void
catalog_plugin(const std::string& format_name,
               const std::string& plugin_fullpath)
{
    // Remember the plugin
    std::map<std::string, std::string>::const_iterator found_path;
    found_path = plugin_filepaths.find(format_name);
    if (found_path != plugin_filepaths.end()) {
        // Hey, we already have an entry for this format
        if (found_path->second == plugin_fullpath) {
            // It's ok if they're both the same file; just skip it.
            return;
        }
        OIIO::debugfmt("OpenImageIO WARNING: {} had multiple plugins:\n"
                       "\t\"{}\"\n    as well as\n\t\"{}\"\n"
                       "    Ignoring all but the first one.\n",
                       format_name, found_path->second, plugin_fullpath);
        return;
    }

    Plugin::Handle handle = Plugin::open(plugin_fullpath);
    if (!handle) {
        return;
    }

    std::string version_function = format_name + "_imageio_version";
    int* plugin_version = (int*)Plugin::getsym(handle,
                                               version_function.c_str());
    if (!plugin_version || *plugin_version != OIIO_PLUGIN_VERSION) {
        Plugin::close(handle);
        return;
    }

    std::string lib_version_function = format_name + "_imageio_library_version";
    PluginLibVersionFunc plugin_lib_version = (PluginLibVersionFunc)
        Plugin::getsym(handle, lib_version_function.c_str());

    // Add the filepath and handle to the master lists
    plugin_filepaths[format_name] = plugin_fullpath;
    plugin_handles[format_name]   = handle;

    ImageInput::Creator input_creator = (ImageInput::Creator)
        Plugin::getsym(handle, format_name + "_input_imageio_create");
    const char** input_extensions = (const char**)
        Plugin::getsym(handle, format_name + "_input_extensions");
    ImageOutput::Creator output_creator = (ImageOutput::Creator)
        Plugin::getsym(handle, format_name + "_output_imageio_create");
    const char** output_extensions = (const char**)
        Plugin::getsym(handle, format_name + "_output_extensions");

    if (input_creator || output_creator)
        declare_imageio_format_locked(format_name, input_creator,
                                      input_extensions, output_creator,
                                      output_extensions,
                                      plugin_lib_version ? plugin_lib_version()
                                                         : NULL);
    else
        Plugin::close(handle);  // not useful
}



#ifdef EMBED_PLUGINS

// Make extern declarations for the input and output create routines and
// list of file extensions, for the standard plugins that come with OIIO.
// These won't be used unless EMBED_PLUGINS is defined.  Use the PLUGENTRY
// macro to make the declaration compact and easy to read.
#    define PLUGENTRY(name)                            \
        ImageInput* name##_input_imageio_create();     \
        ImageOutput* name##_output_imageio_create();   \
        extern const char* name##_output_extensions[]; \
        extern const char* name##_input_extensions[];  \
        extern const char* name##_imageio_library_version();
#    define PLUGENTRY_RO(name)                        \
        ImageInput* name##_input_imageio_create();    \
        extern const char* name##_input_extensions[]; \
        extern const char* name##_imageio_library_version();
#    define PLUGENTRY_WO(name)                         \
        ImageOutput* name##_output_imageio_create();   \
        extern const char* name##_output_extensions[]; \
        extern const char* name##_imageio_library_version();

PLUGENTRY(bmp);
PLUGENTRY(cineon);
PLUGENTRY(dds);
PLUGENTRY_RO(dicom);
PLUGENTRY(dpx);
PLUGENTRY(ffmpeg);
PLUGENTRY(fits);
PLUGENTRY(gif);
PLUGENTRY(heif);
PLUGENTRY(hdr);
PLUGENTRY(ico);
PLUGENTRY(iff);
PLUGENTRY(jpeg);
PLUGENTRY(jpeg2000);
PLUGENTRY(jpegxl);
PLUGENTRY(ktx);
PLUGENTRY(null);
PLUGENTRY(openexr);
PLUGENTRY(openvdb);
PLUGENTRY(png);
PLUGENTRY(pnm);
PLUGENTRY_RO(psd);
PLUGENTRY_RO(ptex);
PLUGENTRY_RO(r3d);
PLUGENTRY_RO(raw);
PLUGENTRY(rla);
PLUGENTRY(sgi);
PLUGENTRY_RO(softimage);
PLUGENTRY_WO(term);
PLUGENTRY(tiff);
PLUGENTRY(targa);
PLUGENTRY(webp);
PLUGENTRY(zfile);


#endif  // defined(EMBED_PLUGINS)


namespace {
// clang-format off

// Add all the built-in plugins, those compiled right into libOpenImageIO, to
// the catalogs.  This does nothing if EMBED_PLUGINS is not defined, in which
// case they'll be registered only when read from external DSO/DLL's.
static void
catalog_builtin_plugins()
{
#ifdef EMBED_PLUGINS
    // Use DECLAREPLUG macro to make this more compact and easy to read.
#define DECLAREPLUG(name)                                                \
        declare_imageio_format(                                          \
            #name, (ImageInput::Creator)name##_input_imageio_create,     \
            name##_input_extensions,                                     \
            (ImageOutput::Creator)name##_output_imageio_create,          \
            name##_output_extensions, name##_imageio_library_version())
#define DECLAREPLUG_RO(name)                                             \
        declare_imageio_format(                                          \
            #name, (ImageInput::Creator)name##_input_imageio_create,     \
            name##_input_extensions, nullptr, nullptr,                   \
            name##_imageio_library_version())
#define DECLAREPLUG_WO(name)                                             \
        declare_imageio_format(                                          \
            #name, nullptr, nullptr,                                     \
            (ImageOutput::Creator)name##_output_imageio_create,          \
            name##_output_extensions,                                    \
            name##_imageio_library_version())

// Declare the most commonly used formats we encounter first, so that they are
// tried right away any time we have to try each format in turn.
#if !defined(DISABLE_OPENEXR)
    DECLAREPLUG (openexr);
#endif
#if !defined(DISABLE_TIFF)
    DECLAREPLUG (tiff);
#endif
#if !defined(DISABLE_JPEG)
    DECLAREPLUG (jpeg);
#endif

// Now all the less common formats, in alphabetical order.
#if !defined(DISABLE_BMP)
    DECLAREPLUG (bmp);
#endif
#if !defined(DISABLE_CINEON)
    DECLAREPLUG_RO (cineon);
#endif
#if !defined(DISABLE_DDS)
    DECLAREPLUG_RO (dds);
#endif
#if defined(USE_DCMTK) && !defined(DISABLE_DICOM)
    DECLAREPLUG_RO (dicom);
#endif
#if !defined(DISABLE_DPX)
    DECLAREPLUG (dpx);
#endif
#if defined(USE_FFMPEG) && !defined(DISABLE_FFMPEG)
    DECLAREPLUG_RO (ffmpeg);
#endif
#if !defined(DISABLE_FITS)
    DECLAREPLUG (fits);
#endif
#if defined(USE_GIF) && !defined(DISABLE_GIF)
    DECLAREPLUG (gif);
#endif
#if defined(USE_HEIF) && !defined(DISABLE_HEIF)
    DECLAREPLUG (heif);
#endif
#if !defined(DISABLE_HDR)
    DECLAREPLUG (hdr);
#endif
#if !defined(DISABLE_ICO)
    DECLAREPLUG (ico);
#endif
#if !defined(DISABLE_IFF)
    DECLAREPLUG (iff);
#endif
#if defined(USE_OPENJPEG) && !defined(DISABLE_JPEG2000)
    DECLAREPLUG (jpeg2000);
#endif
#if defined(USE_JXL)
    DECLAREPLUG (jpegxl);
#endif
#if !defined(DISABLE_KTX)
    DECLAREPLUG (ktx);
#endif
#if !defined(DISABLE_NULL)
    DECLAREPLUG (null);
#endif
#if defined(USE_OPENVDB) && !defined(DISABLE_OPENVDB)
    DECLAREPLUG_RO (openvdb);
#endif
#if !defined(DISABLE_PNG)
    DECLAREPLUG (png);
#endif
#if !defined(DISABLE_PNM)
    DECLAREPLUG (pnm);
#endif
#if !defined(DISABLE_PSD)
    DECLAREPLUG_RO (psd);
#endif
#if defined(USE_PTEX) && !defined(DISABLE_PTEX)
    DECLAREPLUG_RO (ptex);
#endif
#if defined(USE_R3DSDK) && !defined(DISABLE_R3D)
    DECLAREPLUG_RO (r3d);
#endif
#if defined(USE_LIBRAW) && !defined(DISABLE_RAW)
    DECLAREPLUG_RO (raw);
#endif
#if !defined(DISABLE_RLA)
    DECLAREPLUG (rla);
#endif
#if !defined(DISABLE_SGI)
    DECLAREPLUG (sgi);
#endif
#if !defined(DISABLE_SOFTIMAGE)
    DECLAREPLUG_RO (softimage);
#endif
#if !defined(DISABLE_TARGA)
    DECLAREPLUG (targa);
#endif
#if !defined(DISABLE_TERM)
    DECLAREPLUG_WO (term);
#endif
#if defined(USE_WEBP) && !defined(DISABLE_WEBP)
    DECLAREPLUG (webp);
#endif
#if !defined(DISABLE_ZFILE)
    DECLAREPLUG (zfile);
#endif
#endif
}
// clang-format on

}  // namespace



static void
append_if_env_exists(std::string& searchpath, const char* env,
                     bool prepend = false)
{
    const char* path = getenv(env);
    if (path && *path) {
        std::string newpath = path;
        if (searchpath.length()) {
            if (prepend)
                newpath = newpath + ':' + searchpath;
            else
                newpath = searchpath + ':' + newpath;
        }
        searchpath = newpath;
    }
}



/// Look at ALL imageio plugins in the searchpath and add them to the
/// catalog.
void
pvt::catalog_all_plugins(std::string searchpath)
{
    static std::once_flag builtin_flag;
    std::call_once(builtin_flag, catalog_builtin_plugins);

    std::unique_lock<std::recursive_mutex> lock(imageio_mutex);
    append_if_env_exists(searchpath, "OPENIMAGEIO_PLUGIN_PATH", true);
    // obsolete name:
    append_if_env_exists(searchpath, "OIIO_LIBRARY_PATH", true);

    size_t patlen = pattern.length();
    std::vector<std::string> dirs;
    Filesystem::searchpath_split(searchpath, dirs, true);
    for (const auto& dir : dirs) {
        std::vector<std::string> dir_entries;
        Filesystem::get_directory_entries(dir, dir_entries);
        for (const auto& full_filename : dir_entries) {
            std::string leaf = Filesystem::filename(full_filename);
            size_t found     = leaf.find(pattern);
            if (found != std::string::npos
                && (found == leaf.length() - patlen)) {
                std::string pluginname(leaf.begin(),
                                       leaf.begin() + leaf.length() - patlen);
                catalog_plugin(pluginname, full_filename);
            }
        }
    }

    // Inventory the procedural plugins
    auto current_input_formats = input_formats;  // do a copy
    for (auto&& f : current_input_formats) {
        lock.unlock();
        // ImageInput::create will take a lock of imageio_mutex
        auto inp = ImageInput::create(f.first);
        lock.lock();
        if (inp && inp->supports("procedural"))
            procedural_plugins.insert(f.first);
    }
}



bool
pvt::is_procedural_plugin(const std::string& name)
{
    std::unique_lock<std::recursive_mutex> lock(imageio_mutex);

    if (!format_list_vector.size()) {
        lock.unlock();
        // catalog_all_plugins() will lock imageio_mutex.
        pvt::catalog_all_plugins(pvt::plugin_searchpath.string());
        lock.lock();
    }
    return procedural_plugins.find(name) != procedural_plugins.end();
}

OIIO_NAMESPACE_END


OIIO_NAMESPACE_3_1_BEGIN


std::unique_ptr<ImageOutput>
ImageOutput::create(string_view filename, Filesystem::IOProxy* ioproxy,
                    string_view plugin_searchpath)
{
    std::unique_ptr<ImageOutput> out;
    if (filename.empty()) {  // Can't even guess if no filename given
        OIIO::errorfmt("ImageOutput::create() called with no filename");
        return out;
    }

    // Extract the file extension from the filename (without the leading dot)
    std::string format = Filesystem::extension(filename, false);
    if (format.empty()) {
        // If the file had no extension, maybe it was itself the format name
        format = filename;
    }

    ImageOutput::Creator create_function = nullptr;
    {  // scope the lock:
        std::unique_lock<std::recursive_mutex> lock(imageio_mutex);

        // See if it's already in the table.  If not, scan all plugins we can
        // find to populate the table.
        Strutil::to_lower(format);
        OutputPluginMap::const_iterator found = output_formats.find(format);
        if (found == output_formats.end()) {
            lock.unlock();
            // catalog_all_plugins() will lock imageio_mutex
            catalog_all_plugins(
                plugin_searchpath.size()
                    ? plugin_searchpath
                    : string_view(OIIO::pvt::plugin_searchpath));
            lock.lock();
            found = output_formats.find(format);
        }
        if (found != output_formats.end()) {
            create_function = found->second;
        } else {
            if (output_formats.empty()) {
                // This error is so fundamental, we echo it to stderr in
                // case the app is too dumb to do so.
                const char* msg
                    = "ImageOutput::create() could not find any ImageOutput plugins!  Perhaps you need to set OIIO_LIBRARY_PATH.\n";
                Strutil::print(stderr, "{}", msg);
                OIIO::errorfmt("{}", msg);
            } else
                OIIO::errorfmt(
                    "OpenImageIO could not find a format writer for \"{}\". "
                    "Is it a file format that OpenImageIO doesn't know about?\n",
                    filename);
            return out;
        }
    }

    OIIO_ASSERT(create_function != nullptr);
    try {
        out = std::unique_ptr<ImageOutput>(create_function());
    } catch (...) {
        // Safety in case the ctr throws an exception
        out.reset();
    }
    if (out && ioproxy) {
        if (!out->supports("ioproxy")) {
            OIIO::errorfmt(
                "ImageOutput::create called with IOProxy, but format {} does not support IOProxy",
                out->format_name());
            out.reset();
        } else {
            out->set_ioproxy(ioproxy);
        }
    }
    return out;
}



std::unique_ptr<ImageInput>
ImageInput::create(string_view filename, bool do_open, const ImageSpec* config,
                   Filesystem::IOProxy* ioproxy, string_view plugin_searchpath)
{
    // In case the 'filename' was really a REST-ful URI with query/config
    // details tacked on to the end, strip them off so we can correctly
    // extract the file extension.
    std::unique_ptr<ImageInput> in;
    std::map<std::string, std::string> args;
    std::string filename_stripped;

    // Only check REST arguments if the file does not exist
    if (!Filesystem::exists(filename)) {
        if (!Strutil::get_rest_arguments(filename, filename_stripped, args)) {
            OIIO::errorfmt(
                "ImageInput::create() called with malformed filename");
            return in;
        }
    }

    if (filename_stripped.empty())
        filename_stripped = filename;

    if (filename_stripped.empty()) {  // Can't even guess if no filename given
        OIIO::errorfmt("ImageInput::create() called with no filename");
        return in;
    }

    // Extract the file extension from the filename (without the leading dot)
    std::string format = Filesystem::extension(filename_stripped, false);
    if (format.empty()) {
        // If the file had no extension, maybe it was itself the format name
        format = filename;
    }

    // Mostly for backward compatibility, if we were not given an ioproxy,
    // check if one was passed via the configuration hints.
    if (!ioproxy && config) {
        if (auto p = config->find_attribute("oiio:ioproxy", TypeDesc::PTR))
            ioproxy = p->get<Filesystem::IOProxy*>();
    }

    ImageInput::Creator create_function = nullptr;
    {  // scope the lock:
        std::unique_lock<std::recursive_mutex> lock(imageio_mutex);

        // See if it's already in the table.  If not, scan all plugins we can
        // find to populate the table.
        Strutil::to_lower(format);
        InputPluginMap::const_iterator found = input_formats.find(format);
        if (found == input_formats.end()) {
            if (plugin_searchpath.empty())
                plugin_searchpath = OIIO::pvt::plugin_searchpath;
            lock.unlock();
            // catalog_all_plugins() will lock imageio_mutex.
            catalog_all_plugins(plugin_searchpath);
            lock.lock();
            found = input_formats.find(format);
        }
        if (found != input_formats.end())
            create_function = found->second;
    }

    // Remember which prototypes we've already tried, so we don't double dip.
    std::vector<ImageInput::Creator> formats_tried;

    std::string specific_error;
    if (create_function && filename != format) {
        // If given a full filename, double-check that our guess
        // based on the extension actually works.  You never know
        // when somebody will have an incorrectly-named file, let's
        // deal with it robustly.
        formats_tried.push_back(create_function);
        in = std::unique_ptr<ImageInput>(create_function());
        if (!do_open && in) {
            // We created the ImageInput but we don't need a full open yet
            if ((ioproxy && in->supports("ioproxy") && in->valid_file(ioproxy))
                || (!ioproxy && !filename.empty() && in->valid_file(filename))) {
                // Special case: we don't need to return the file
                // already opened, and this ImageInput says that the
                // file is the right type with a fast valid_file test.
                return in;
            }
        }
        ImageSpec tmpspec;
        bool ok = false;
        if (in) {
            in->set_ioproxy(ioproxy);
            if (config)
                ok = in->open(filename, tmpspec, *config);
            else
                ok = in->open(filename, tmpspec);
        }
        if (ok) {
            // It worked
            if (!do_open)
                in->close();
            return in;
        } else {
            // Oops, it failed.  Apparently, this file can't be
            // opened with this II.  Clear create_function to force
            // the code below to check every plugin we know.
            create_function = nullptr;
            if (in) {
                specific_error = in->geterror();
                if (OIIO::pvt::oiio_print_debug > 1)
                    OIIO::debugfmt(
                        "ImageInput::create: \"{}\" did not open using format \"{}\".\n",
                        filename, in->format_name());
            }
            in.reset();
        }
    }

    if (!create_function && OIIO::pvt::oiio_try_all_readers) {
        // If a plugin can't be found that was explicitly designated for
        // this extension, then just try every one we find and see if
        // any will open the file.  Add a configuration request that
        // includes a "nowait" option so that it returns immediately if
        // it's a plugin that might wait for an event, like a socket that
        // doesn't yet exist).
        ImageSpec myconfig;
        if (config)
            myconfig = *config;
        myconfig.attribute("nowait", (int)1);
        std::lock_guard<std::recursive_mutex> lock(imageio_mutex);
        for (auto f : format_list_vector) {
            auto plugin = input_formats.find(f.string());
            if (plugin == input_formats.end() || !plugin->second)
                continue;  // format that's output only
            // If we already tried this create function, don't do it again
            if (std::find(formats_tried.begin(), formats_tried.end(),
                          plugin->second)
                != formats_tried.end())
                continue;
            formats_tried.push_back(plugin->second);  // remember

            ImageSpec tmpspec;
            try {
                ImageInput::Creator create_function = plugin->second;
                in = std::unique_ptr<ImageInput>(create_function());
            } catch (...) {
                // Safety in case the ctr throws an exception
            }
            if (!in)
                continue;

            // It can be quite expensive to do a full open() on each file
            // type. So if the obvious one failed already, as we're checking
            // the rest, try their valid_file method first, and if it fails,
            // don't proceed to the full open. (If it succeeds, though, we
            // will try a full open, so that should really incentivize us to
            // ensure that every format class has a valid_file method, or this
            // will be expensive since it will open twice.)
            if ((ioproxy && in->supports("ioproxy") && !in->valid_file(ioproxy))
                || (!ioproxy && !filename.empty()
                    && !in->valid_file(filename))) {
                // Since we didn't need to open it, we just checked whether
                // it was a valid file, and it's not.  Try the next one.
                if (OIIO::pvt::oiio_print_debug > 1)
                    OIIO::debugfmt(
                        "ImageInput::create: \"{}\" did not open using format \"{}\" {} [valid_file was false].\n",
                        filename, plugin->first, in->format_name());
                in.reset();
                continue;
            }

            // We either need to open it, or we already know it appears
            // to be a file of the right type.
            in->set_ioproxy(ioproxy);
            bool ok = in->open(filename, tmpspec, myconfig);
            if (ok) {
                if (!do_open)
                    in->close();
                if (OIIO::pvt::oiio_print_debug > 1)
                    OIIO::debugfmt(
                        "ImageInput::create: \"{}\" succeeded using format \"{}\".\n",
                        filename, plugin->first);
                return in;
            }
            if (OIIO::pvt::oiio_print_debug > 1)
                OIIO::debugfmt(
                    "ImageInput::create: \"{}\" did not open using format \"{}\" {}.\n",
                    filename, plugin->first, in->format_name());
            in.reset();
        }
    }

    if (!create_function) {
        std::lock_guard<std::recursive_mutex> lock(imageio_mutex);
        if (input_formats.empty()) {
            // This error is so fundamental, we echo it to stderr in
            // case the app is too dumb to do so.
            const char* msg
                = "ImageInput::create() could not find any ImageInput plugins!\n"
                  "    Perhaps you need to set OIIO_LIBRARY_PATH.\n";
            Strutil::print(stderr, "{}", msg);
            OIIO::errorfmt("{}", msg);
        } else if (!specific_error.empty()) {
            // Pass along any specific error message we got from our
            // best guess of the format.
            OIIO::errorfmt("{}", specific_error);
        } else if (Filesystem::exists(filename))
            OIIO::errorfmt(
                "OpenImageIO could not find a format reader for \"{}\". "
                "Is it a file format that OpenImageIO doesn't know about?\n",
                filename);
        else
            OIIO::errorfmt(
                "Image \"{}\" does not exist. Also, it is not the name of an image format that OpenImageIO recognizes.\n",
                filename);
        OIIO_DASSERT(!in);
        return in;
    }

    return std::unique_ptr<ImageInput>(create_function());
}


OIIO_NAMESPACE_3_1_END
