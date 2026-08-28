#pragma once
#ifndef __DRACO_DECODER_H__
#define __DRACO_DECODER_H__

//
// Decoded per-primitive vertex data. Filled either from standard glTF
// accessors or from a KHR_draco_mesh_compression bufferView.
// Values are in raw glTF space (no unit conversion, no UV flip).
//

#include <string>
#include <vector>

namespace gltf
{
    class Document;
    struct Primitive;

    struct PrimitiveData
    {
        std::vector<float> positions; // xyz per vertex
        std::vector<float> normals;   // xyz per vertex (empty if none)
        std::vector<float> uv0;       // uv per vertex (empty if none)
        std::vector<float> uv1;
        std::vector<float> color0;    // colorComps per vertex (empty if none)
        int colorComps = 0;           // 3 or 4
        std::vector<unsigned int> joints0; // 4 per vertex (empty if none)
        std::vector<float> weights0;       // 4 per vertex (empty if none)
        std::vector<unsigned int> indices; // triangle-list source indices

        size_t VertexCount() const { return positions.size() / 3; }
    };

    // Decode a KHR_draco_mesh_compression primitive.
    // Returns false (with errorMessage) when draco support is not built in or
    // the stream cannot be decoded.
    bool DecodeDracoPrimitive(const Document& doc, const Primitive& prim,
                              PrimitiveData& out, std::string* errorMessage);

} // namespace gltf

#endif // __DRACO_DECODER_H__
