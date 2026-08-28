#pragma once
#ifndef __GLTF_DOCUMENT_H__
#define __GLTF_DOCUMENT_H__

//
// GltfDocument: Maya-independent glTF 2.0 parsing layer.
// Loads .gltf / .glb, resolves buffers (external files, data URIs, GLB BIN
// chunk) and decodes accessors into flat float / index arrays.
//

#include <map>
#include <string>
#include <vector>

namespace gltf
{
    // glTF componentType constants
    enum ComponentType
    {
        CT_BYTE = 5120,
        CT_UNSIGNED_BYTE = 5121,
        CT_SHORT = 5122,
        CT_UNSIGNED_SHORT = 5123,
        CT_UNSIGNED_INT = 5125,
        CT_FLOAT = 5126
    };

    // primitive.mode constants
    enum PrimitiveMode
    {
        MODE_POINTS = 0,
        MODE_LINES = 1,
        MODE_LINE_LOOP = 2,
        MODE_LINE_STRIP = 3,
        MODE_TRIANGLES = 4,
        MODE_TRIANGLE_STRIP = 5,
        MODE_TRIANGLE_FAN = 6
    };

    // sampler wrap constants
    enum WrapMode
    {
        WRAP_CLAMP_TO_EDGE = 33071,
        WRAP_MIRRORED_REPEAT = 33648,
        WRAP_REPEAT = 10497
    };

    struct Buffer
    {
        std::string uri;
        size_t byteLength = 0;
        std::vector<unsigned char> data;
    };

    struct BufferView
    {
        int buffer = -1;
        size_t byteOffset = 0;
        size_t byteLength = 0;
        int byteStride = 0; // 0 = tightly packed
    };

    struct AccessorSparse
    {
        int count = 0;
        int indicesBufferView = -1;
        size_t indicesByteOffset = 0;
        int indicesComponentType = 0;
        int valuesBufferView = -1;
        size_t valuesByteOffset = 0;
    };

    struct Accessor
    {
        int bufferView = -1; // -1 = all zeros (or sparse only)
        size_t byteOffset = 0;
        int componentType = 0;
        bool normalized = false;
        int count = 0;
        std::string type; // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT4", ...
        bool hasSparse = false;
        AccessorSparse sparse;
    };

    struct Image
    {
        std::string name;
        std::string uri; // file path or data URI
        int bufferView = -1;
        std::string mimeType;
    };

    struct Sampler
    {
        int wrapS = WRAP_REPEAT;
        int wrapT = WRAP_REPEAT;
    };

    struct Texture
    {
        int source = -1;
        int sampler = -1;
    };

    struct TextureRef
    {
        int index = -1; // texture index, -1 = not present
        int texCoord = 0;
        float scale = 1.0f; // normalTexture.scale / occlusionTexture.strength

        bool IsValid() const { return index >= 0; }
    };

    struct Material
    {
        std::string name;
        float baseColorFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        TextureRef baseColorTexture;
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        TextureRef metallicRoughnessTexture;
        TextureRef normalTexture;
        TextureRef occlusionTexture;
        TextureRef emissiveTexture;
        float emissiveFactor[3] = {0.0f, 0.0f, 0.0f};
        std::string alphaMode = "OPAQUE"; // OPAQUE / MASK / BLEND
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
    };

    struct MorphTarget
    {
        std::map<std::string, int> attributes; // POSITION / NORMAL / TANGENT -> accessor
    };

    struct Primitive
    {
        std::map<std::string, int> attributes; // POSITION / NORMAL / TEXCOORD_0 / ... -> accessor
        int indices = -1;
        int material = -1;
        int mode = MODE_TRIANGLES;
        std::vector<MorphTarget> targets;

        // KHR_draco_mesh_compression (parsed but only decoded when built with draco)
        bool hasDraco = false;
        int dracoBufferView = -1;
        std::map<std::string, int> dracoAttributes;

        int GetAttribute(const std::string& name) const
        {
            std::map<std::string, int>::const_iterator it = attributes.find(name);
            return (it != attributes.end()) ? it->second : -1;
        }
    };

    struct Mesh
    {
        std::string name;
        std::vector<Primitive> primitives;
        std::vector<float> weights;           // default morph weights
        std::vector<std::string> targetNames; // from extras.targetNames
    };

    struct Node
    {
        std::string name;
        std::vector<int> children;
        int mesh = -1;
        int skin = -1;
        bool hasMatrix = false;
        double matrix[16]; // column-major (as stored in glTF)
        double translation[3] = {0.0, 0.0, 0.0};
        double rotation[4] = {0.0, 0.0, 0.0, 1.0}; // x, y, z, w
        double scale[3] = {1.0, 1.0, 1.0};
    };

    struct Skin
    {
        std::string name;
        int inverseBindMatrices = -1;
        int skeleton = -1;
        std::vector<int> joints;
    };

    struct AnimationSampler
    {
        int input = -1;  // accessor (times, seconds)
        int output = -1; // accessor (values)
        std::string interpolation = "LINEAR"; // LINEAR / STEP / CUBICSPLINE
    };

    struct AnimationChannel
    {
        int sampler = -1;
        int targetNode = -1;
        std::string targetPath; // translation / rotation / scale / weights
    };

    struct Animation
    {
        std::string name;
        std::vector<AnimationSampler> samplers;
        std::vector<AnimationChannel> channels;
    };

    struct Scene
    {
        std::string name;
        std::vector<int> nodes;
    };

    class Document
    {
    public:
        // Load .gltf or .glb (auto-detected by magic). Resolves all buffers.
        // Returns false and sets errorMessage on failure.
        bool Load(const std::string& path, std::string* errorMessage);

        // Decode an accessor into floats. Normalized integers are converted to
        // [0,1] / [-1,1] per the glTF spec. outComponents receives the number
        // of components per element (1 for SCALAR, 3 for VEC3, 16 for MAT4...).
        bool GetFloats(int accessorIndex, std::vector<float>& out, int* outComponents = 0) const;

        // Decode an accessor (typically indices / JOINTS_0) into uint32.
        bool GetUInts(int accessorIndex, std::vector<unsigned int>& out) const;

        // Raw bytes of a bufferView (honoring byteOffset / byteLength).
        bool GetBufferViewBytes(int bufferViewIndex, std::vector<unsigned char>& out) const;

        // Resolve a (percent-encoded) relative URI against the source directory.
        std::string ResolveUri(const std::string& uri) const;

        static bool IsDataUri(const std::string& uri);
        // Decode "data:<mime>;base64,...." -> bytes. Returns mime type via outMime.
        static bool DecodeDataUri(const std::string& uri, std::vector<unsigned char>& out, std::string* outMime);

        static int GetComponentCount(const std::string& type);   // SCALAR->1 ...
        static int GetComponentSize(int componentType);          // bytes

    public:
        std::vector<Buffer> buffers;
        std::vector<BufferView> bufferViews;
        std::vector<Accessor> accessors;
        std::vector<Image> images;
        std::vector<Sampler> samplers;
        std::vector<Texture> textures;
        std::vector<Material> materials;
        std::vector<Mesh> meshes;
        std::vector<Node> nodes;
        std::vector<Skin> skins;
        std::vector<Animation> animations;
        std::vector<Scene> scenes;
        int defaultScene = -1;

        std::string baseDir;  // directory of the source file (with trailing separator)
        std::string baseName; // file name without extension

    protected:
        bool ParseJson(const std::string& jsonText, std::string* errorMessage);
        bool ResolveBuffers(std::string* errorMessage);
        bool ReadElements(const Accessor& acc, std::vector<double>& out) const; // raw (denormalized) values
        std::vector<unsigned char> glbBin_;
        bool isGLB_ = false;
    };

} // namespace gltf

#endif // __GLTF_DOCUMENT_H__
