// Copyright 2008-present Contributors to the OpenImageIO project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/OpenImageIO/oiio/blob/master/LICENSE.md

#include <OpenImageIO/dassert.h>
#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>

#include <OpenEXR/ImathMatrix.h>

#include <openvdb/openvdb.h>
#include <openvdb/tools/Dense.h>

// Try to use the long form/abi version string introduced in 5.0
#if OPENVDB_LIBRARY_MAJOR_VERSION_NUMBER <= 4
#    define OIIO_OPENVDB_VERSION OPENVDB_LIBRARY_VERSION_STRING
#else
#    define OIIO_OPENVDB_VERSION OPENVDB_LIBRARY_ABI_VERSION_STRING
#endif


OIIO_PLUGIN_NAMESPACE_BEGIN

struct layerrecord {
    std::string name;
    std::string attribute;
    openvdb::CoordBBox bounds;
    ImageSpec spec;
    openvdb::GridBase::Ptr grid;

    layerrecord(std::string obj, std::string attr, openvdb::CoordBBox bx,
                ImageSpec is, openvdb::GridBase::Ptr grd)
        : name(std::move(obj))
        , attribute(std::move(attr))
        , bounds(std::move(bx))
        , spec(std::move(is))
        , grid(std::move(grd))
    {
    }
};



class OpenVDBInput final : public ImageInput {
    std::string m_name;
    int m_subimage;    ///< What subimage/field are we looking at?
    int m_nsubimages;  ///< How many fields in the file?
    std::vector<layerrecord> m_layers;

    void init()
    {
        std::string().swap(m_name);
        std::vector<layerrecord>().swap(m_layers);
        m_subimage   = -1;
        m_nsubimages = 0;
    }

    mutex& vdbMutex() { return m_mutex; }
    void readMetaData(const openvdb::GridBase& grid, const layerrecord& layer,
                      ImageSpec& spec);

public:
    OpenVDBInput() { init(); }
    virtual ~OpenVDBInput() { close(); }

    virtual const char* format_name(void) const override { return "openvdb"; }
    virtual int supports(string_view feature) const override
    {
        return (feature == "arbitrary_metadata");
    }
    virtual bool valid_file(const std::string& filename) const override;
    virtual bool open(const std::string& name, ImageSpec& newspec) override;
    virtual bool close() override;
    virtual int current_subimage(void) const override;
    virtual bool seek_subimage(int subimage, int miplevel) override;
    virtual bool seek_subimage_nolock(int subimage, int miplevel);
    virtual bool read_native_scanline(int subimage, int miplevel, int y, int z,
                                      void* data) override;
    virtual bool read_native_tile(int subimage, int miplevel, int x, int y,
                                  int z, void* data) override;

    ImageSpec spec(int subimage, int miplevel) override;
    ImageSpec spec_dimensions(int subimage, int miplevel) override;
};



using namespace openvdb;



bool
OpenVDBInput::close()
{
    init();  // Reset to initial state
    return true;
}



ImageSpec
OpenVDBInput::spec(int subimage, int miplevel)
{
    if (subimage < 0 || subimage >= m_nsubimages)  // out of range
        return ImageSpec();
    if (miplevel != 0)
        return ImageSpec();
    return m_layers[subimage].spec;
}



ImageSpec
OpenVDBInput::spec_dimensions(int subimage, int miplevel)
{
    if (subimage < 0 || subimage >= m_nsubimages)  // out of range
        return ImageSpec();
    if (miplevel != 0)
        return ImageSpec();
    ImageSpec spec;
    spec.copy_dimensions(m_layers[subimage].spec);
    return spec;
}



int
OpenVDBInput::current_subimage(void) const
{
    lock_guard lock(m_mutex);
    return m_subimage;
}



bool
OpenVDBInput::seek_subimage(int subimage, int miplevel)
{
    lock_guard lock(vdbMutex());
    return seek_subimage_nolock(subimage, miplevel);
}



bool
OpenVDBInput::seek_subimage_nolock(int subimage, int miplevel)
{
    if (subimage < 0 || subimage >= m_nsubimages)  // out of range
        return false;
    if (miplevel != 0)
        return false;
    if (subimage == m_subimage)
        return true;

    m_subimage = subimage;
    m_spec     = m_layers[subimage].spec;
    return true;
}



namespace {

CoordBBox
getBoundingBox(const GridBase& grid)
{
    auto bbMin = grid.getMetadata<TypedMetadata<Vec3i>>(
        GridBase::META_FILE_BBOX_MIN);
    if (bbMin) {
        auto bbMax = grid.getMetadata<TypedMetadata<Vec3i>>(
            GridBase::META_FILE_BBOX_MAX);
        if (bbMax)
            return CoordBBox(Coord(bbMin->value()), Coord(bbMax->value()));
    }
    return grid.evalActiveVoxelBoundingBox();
}



template<typename GridType> struct VDBReader {
    using TreeType  = typename GridType::TreeType;
    using RootType  = typename TreeType::RootNodeType;
    using Int1Type  = typename RootType::ChildNodeType;
    using Int2Type  = typename Int1Type::ChildNodeType;
    using ValueType = typename GridType::ValueType;
    using LeafType  = typename TreeType::LeafNodeType;
    typedef openvdb::tools::Dense<ValueType, openvdb::tools::LayoutXYZ> DenseT;

    static void setTile(ValueType* data, const ValueType value)
    {
        for (ValueType* end = data + LeafType::SIZE; data < end; ++data)
            *data = value;
    }

    static bool readTile(const GridType& grid, int x, int y, int z,
                         ValueType* values)
    {
        // Probe for a cell-centered voxel
        enum { kOffset = LeafType::DIM / 2 };
        const openvdb::Coord xyz(x + kOffset, y + kOffset, z + kOffset);
        const RootType& root = grid.tree().root();
        // Use the GridType::ConstAccessor so only one query needs to be done.
        // From that query, check the node type from 'most interesting' to least
        typename GridType::ConstAccessor cache = grid.getConstAccessor();
        if (auto* leaf = root.probeConstLeafAndCache(xyz, cache)) {
            CoordBBox bbox = leaf->getNodeBoundingBox();
            ASSERT((bbox.min().x() == x && bbox.min().y() == y
                    && bbox.min().z() == z)
                   && "Tile access unaligned");
            ASSERT((bbox.dim() == Coord(LeafType::DIM))
                   && "Unexpected tile dimensions");
            // Have OpenVDB fill the dense block, into the values pointer
            DenseT dense(bbox, values);
            leaf->copyToDense(bbox, dense);
        } else
            setTile(values, cache.getValue(xyz));
        return true;
    }

    static void fillSpec(const CoordBBox& bounds, const Coord& dim,
                         ImageSpec& spec)
    {
        Vec3i data_min, data_max;
        for (int i = 0; i < 3; ++i) {
            // Round the block_bounds up to encompass the leaf-node dimension (generally 8)
            // So a box spanning [-2, -2, -2] -> [2, 2, 2]
            // is expanded to    [-8, -8, -8] -> [8, 8, 8]
            data_min[i] = bounds.min()[i] - (bounds.min()[i] % LeafType::DIM);
            data_max[i] = bounds.max()[i]
                          + (LeafType::DIM - (bounds.max()[i] % LeafType::DIM));
        }
        spec.x = data_min.x();
        spec.y = data_min.y();
        spec.z = data_min.z();

        spec.width  = data_max.x() - data_min.x() + 1;
        spec.height = data_max.y() - data_min.y() + 1;
        spec.depth  = data_max.z() - data_min.z() + 1;

        spec.full_x = bounds.min().x();
        spec.full_y = bounds.min().y();
        spec.full_z = bounds.min().z();

        spec.full_width  = dim.x();
        spec.full_height = dim.y();
        spec.full_depth  = dim.z();

        spec.tile_width  = LeafType::DIM;
        spec.tile_height = LeafType::DIM;
        spec.tile_depth  = LeafType::DIM;
    }
};



// openvdb::io::File seems to not autoclose on destruct?
class VDBFile {
    std::unique_ptr<openvdb::io::File> m_file;

public:
    VDBFile(openvdb::io::File* f)
        : m_file(f)
    {
    }
    VDBFile(VDBFile&& rhs)
        : m_file(std::move(rhs.m_file))
    {
    }
    ~VDBFile()
    {
        if (m_file)
            m_file->close();
    }
    openvdb::io::File* operator->() { return m_file.get(); };
    operator bool() const { return m_file.get() != nullptr; }
    void reset() { m_file.reset(); }
};



VDBFile
openVDB(const std::string& filename, const ImageInput* errReport)
{
    if (!Filesystem::is_regular(filename))
        return nullptr;

    FILE* f = Filesystem::fopen(filename, "r");
    if (!f)
        return nullptr;

    // Endianess of OPENVDB_MAGIC isn't clear, so just leave as is
    int32_t magic;
    static_assert(sizeof(magic) == sizeof(OPENVDB_MAGIC),
                  "Magic type not the same size");

    if (fread(&magic, sizeof(magic), 1, f) != 1)
        magic = 0;
    fclose(f);
    if (magic != OPENVDB_MAGIC)
        return nullptr;

    const char* errhint = "Unknown error";
    try {
        static struct OpenVDBLib {
            OpenVDBLib() { openvdb::initialize(); }
            ~OpenVDBLib() { openvdb::uninitialize(); }
        } sVDBLib;

        VDBFile file(new io::File(filename));

        file->open();
        if (file->isOpen())
            return file;

    } catch (const std::exception& e) {
        errReport->errorf("Could not open '%s': %s", filename, e.what());
        return nullptr;
    } catch (...) {
        errhint = "Unknown exception thrown";
    }

    errReport->errorf("Could not open '%s': %s", filename, errhint);
    return nullptr;
}

}  // anonymous namespace



bool
OpenVDBInput::valid_file(const std::string& filename) const
{
    return openVDB(filename, this);
}



void
OpenVDBInput::readMetaData(const openvdb::GridBase& grid,
                           const layerrecord& layer, ImageSpec& spec)
{
    // If two grids of the same name exist in a VDB, then there will be an
    // object name & a grid name that get concated to make a unique name
    // "density[0].density", "density[1].density" for lookup.
    // Otherwise, just use the grid name; so one can do texture3d("Cd") instead
    // of texture3d("Cd.Cd")
    if (layer.name != layer.attribute)
        spec.attribute("oiio:subimagename", layer.name + "." + layer.attribute);
    else
        spec.attribute("oiio:subimagename", layer.attribute);

    auto mdPrefix = [](const openvdb::Name name) { return "openvdb:" + name; };

    const auto& transform   = grid.transform();
    const auto& map         = transform.baseMap()->getAffineMap();
    openvdb::math::Mat4d md = map->getConstMat4();

    static_assert(sizeof(openvdb::math::Mat4d) == sizeof(Imath::M44d),
                  "Matrix is not the right type / size!");

    spec.attribute(mdPrefix("indextoworld"),
                   TypeDesc(TypeDesc::DOUBLE, TypeDesc::MATRIX44), &md);

    // Invert to go from world to index
    md = md.inverse();

    spec.attribute(mdPrefix("worldtoindex"),
                   TypeDesc(TypeDesc::DOUBLE, TypeDesc::MATRIX44), &md);

    // Build the 'worldtolocal' matrix that OIIO wants
    Imath::M44f m((float)md[0][0], (float)md[0][1], (float)md[0][2],
                  (float)md[0][3], (float)md[1][0], (float)md[1][1],
                  (float)md[1][2], (float)md[1][3], (float)md[2][0],
                  (float)md[2][1], (float)md[2][2], (float)md[2][3],
                  (float)md[3][0], (float)md[3][1], (float)md[3][2],
                  (float)md[3][3]);

    // Map/scale the data window into a unit cube
    const Vec3f unitScale(1.0 / spec.full_width, 1.0 / spec.full_height,
                          1.0 / spec.full_depth);

    // Shift by min data window and half a voxel
    const Vec3f voxSize    = grid.voxelSize();
    const Vec3f dataOffset = (Vec3f(-spec.full_x, -spec.full_y, -spec.full_z)
                              * voxSize)
                             + (voxSize * 0.5);

    // Shift by the data offset
    m = Imath::M44f(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0,
                    dataOffset[0], dataOffset[1], dataOffset[2], 1.0)
        *
        // And scale to a unit cube
        Imath::M44f(unitScale[0], 0.0, 0.0, 0.0, 0.0, unitScale[1], 0.0, 0.0,
                    0.0, 0.0, unitScale[2], 0.0, 0.0, 0.0, 0.0, 1.0)
        * m;

    spec.attribute("worldtolocal", TypeMatrix, &m);

    for (auto metaItr = grid.beginMeta(), metaEnd = grid.endMeta();
         metaItr != metaEnd; ++metaItr) {
        const std::string& name = metaItr->first;
        const auto value        = metaItr->second;
        const auto&& type       = value->typeName();

        // Ordering below by amount from a default VDB from houdini
        if (type == StringMetadata::staticTypeName()) {
            spec.attribute(mdPrefix(name),
                           static_cast<StringMetadata&>(*value).value());
        } else if (type == Vec3SMetadata::staticTypeName()) {
            const auto v = static_cast<Vec3SMetadata&>(*value).value();
            spec.attribute(mdPrefix(name), TypeVector, &v);
        } else if (type == Int64Metadata::staticTypeName()) {
            const auto v = static_cast<Int64Metadata&>(*value).value();
            spec.attribute(mdPrefix(name), TypeDesc::INT64, &v);
        } else if (type == BoolMetadata::staticTypeName()) {
            spec.attribute(mdPrefix(name),
                           static_cast<BoolMetadata&>(*value).value());
        } else if (type == FloatMetadata::staticTypeName()) {
            spec.attribute(mdPrefix(name),
                           static_cast<FloatMetadata&>(*value).value());
        }

        else if (type == Int32Metadata::staticTypeName()) {
            spec.attribute(mdPrefix(name),
                           static_cast<Int32Metadata&>(*value).value());
        } else if (type == DoubleMetadata::staticTypeName()) {
            const auto v = static_cast<DoubleMetadata&>(*value).value();
            spec.attribute(mdPrefix(name), TypeDesc::DOUBLE, &v);
        }

        else if (type == Vec3IMetadata::staticTypeName()) {
            const auto v = static_cast<Vec3IMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::INT, TypeDesc::VEC3), &v);
        } else if (type == Vec3DMetadata::staticTypeName()) {
            const auto v = static_cast<Vec3DMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::DOUBLE, TypeDesc::VEC3), &v);
        }

        else if (type == Vec2SMetadata::staticTypeName()) {
            const auto v = static_cast<Vec2SMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::FLOAT, TypeDesc::VEC2), &v);
        } else if (type == Vec2IMetadata::staticTypeName()) {
            const auto v = static_cast<Vec2IMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::INT, TypeDesc::VEC2), &v);
        } else if (type == Vec2DMetadata::staticTypeName()) {
            const auto v = static_cast<Vec2DMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::DOUBLE, TypeDesc::VEC2), &v);
        }

        else if (type == Mat4SMetadata::staticTypeName()) {
            const auto v = static_cast<Mat4SMetadata&>(*value).value();
            spec.attribute(mdPrefix(name), TypeMatrix44, &v);
        } else if (type == Mat4DMetadata::staticTypeName()) {
            const auto v = static_cast<Mat4DMetadata&>(*value).value();
            spec.attribute(mdPrefix(name),
                           TypeDesc(TypeDesc::DOUBLE, TypeDesc::MATRIX44), &v);
        }
    }
}



bool
OpenVDBInput::open(const std::string& filename, ImageSpec& newspec)
{
    auto file = openVDB(filename, this);
    if (!file)
        return false;
    ASSERT(file->isOpen());
    // Note that the actual "open VDB file" stored in `file` will be
    // discarded when open() exits. That's ok, we still hang onto the data
    // with the pointers in the layerrecords.

    try {
        for (io::File::NameIterator name = file->beginName(),
                                    end  = file->endName();
             name != end; ++name) {
            std::string gridName   = name.gridName();
            GridBase::Ptr gridPtr  = file->readGrid(gridName, BBoxd());
            const CoordBBox bounds = getBoundingBox(*gridPtr);
            const Coord dim        = bounds.dim();

            ImageSpec spec;
            ScalarGrid::Ptr fPtr;
            Vec3fGrid::Ptr v3Ptr;
            if ((fPtr = gridPtrCast<ScalarGrid>(gridPtr))) {
                spec = ImageSpec(dim.x(), dim.y(), 1, TypeFloat);
                VDBReader<ScalarGrid>::fillSpec(bounds, dim, spec);
            } else if ((v3Ptr = gridPtrCast<Vec3fGrid>(gridPtr))) {
                spec = ImageSpec(dim.x(), dim.y(), 3, TypeFloat);
                VDBReader<Vec3fGrid>::fillSpec(bounds, dim, spec);
            } else
                continue;

            // gridName will now be moved/invalid
            m_layers.emplace_back(std::move(gridName), gridPtr->getName(),
                                  bounds, spec, std::move(gridPtr));

            auto& layer        = m_layers.back();
            auto& layerspec    = layer.spec;
            auto& channelnames = layerspec.channelnames;

            channelnames.resize(layerspec.nchannels);
            if (layerspec.nchannels > 1) {
                ASSERT(layerspec.nchannels <= 4);
                const bool iscolor = layer.name == "Cd"
                                     || layer.name == "color";
                const char kChanName[4]
                    = { iscolor ? 'r' : 'x', iscolor ? 'g' : 'y',
                        iscolor ? 'b' : 'z', iscolor ? 'a' : 'w' };
                for (int c = 0; c < layerspec.nchannels; ++c)
                    channelnames[c] = layer.name + "."
                                      + std::string(&kChanName[c], 1);
            } else
                channelnames.back() = layer.name;

            readMetaData(*layer.grid, layer, layerspec);
        }
    } catch (const std::exception& e) {
        init();  // Reset to initial state
        errorf("Could not open '%s': %s", filename, e.what());
        return false;
    }
    m_name       = filename;
    m_nsubimages = (int)m_layers.size();

    bool ok = seek_subimage(0, 0);
    newspec = ImageInput::spec();
    return ok;
}



bool
OpenVDBInput::read_native_scanline(int subimage, int miplevel, int y, int z,
                                   void* data)
{
    // scanlines not supported
    return false;
}



bool
OpenVDBInput::read_native_tile(int subimage, int miplevel, int x, int y, int z,
                               void* data)
{
    lock_guard lock(vdbMutex());
    if (!seek_subimage_nolock(subimage, miplevel))
        return false;

    const layerrecord& lay = m_layers[m_subimage];
    switch (lay.spec.nchannels) {
    case 1:
        return VDBReader<FloatGrid>::readTile(*gridPtrCast<ScalarGrid>(lay.grid),
                                              x, y, z,
                                              reinterpret_cast<float*>(data));
    case 3:
        return VDBReader<Vec3fGrid>::readTile(*gridPtrCast<Vec3fGrid>(lay.grid),
                                              x, y, z,
                                              reinterpret_cast<Vec3f*>(data));
    default: break;
    }
    return false;
}



// Obligatory material to make this a recognizeable imageio plugin:
OIIO_PLUGIN_EXPORTS_BEGIN

OIIO_EXPORT ImageInput*
openvdb_input_imageio_create()
{
    return new OpenVDBInput;
}

OIIO_EXPORT const char* openvdb_input_extensions[] = { "vdb", nullptr };

OIIO_EXPORT int openvdb_imageio_version = OIIO_PLUGIN_VERSION;

OIIO_EXPORT const char*
openvdb_imageio_library_version()
{
    return "OpenVDB " OIIO_OPENVDB_VERSION;
}

OIIO_PLUGIN_EXPORTS_END

OIIO_PLUGIN_NAMESPACE_END
