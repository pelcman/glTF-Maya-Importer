#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif

#include "DracoDecoder.h"
#include "GltfDocument.h"

#ifdef ENABLE_BUILD_WITH_DRACO
#include <draco/compression/decode.h>
#endif

namespace gltf
{
#ifdef ENABLE_BUILD_WITH_DRACO

    static const draco::PointAttribute* FindAttribute(const draco::Mesh& mesh, int uniqueId)
    {
        return mesh.GetAttributeByUniqueId((unsigned int)uniqueId);
    }

    template <typename T>
    static bool ReadAttribute(const draco::Mesh& mesh, const draco::PointAttribute* att,
                              int numComponents, std::vector<T>& out)
    {
        if (!att)
        {
            return false;
        }
        const unsigned int numPoints = mesh.num_points();
        out.resize((size_t)numPoints * numComponents);
        std::vector<T> value(numComponents);
        for (unsigned int p = 0; p < numPoints; p++)
        {
            const draco::AttributeValueIndex avi = att->mapped_index(draco::PointIndex(p));
            if (!att->ConvertValue<T>(avi, &value[0]))
            {
                return false;
            }
            for (int c = 0; c < numComponents; c++)
            {
                out[(size_t)p * numComponents + c] = value[c];
            }
        }
        return true;
    }

    bool DecodeDracoPrimitive(const Document& doc, const Primitive& prim,
                              PrimitiveData& out, std::string* errorMessage)
    {
        std::vector<unsigned char> bytes;
        if (!doc.GetBufferViewBytes(prim.dracoBufferView, bytes) || bytes.empty())
        {
            if (errorMessage) *errorMessage = "failed to read draco bufferView";
            return false;
        }

        draco::DecoderBuffer buffer;
        buffer.Init((const char*)&bytes[0], bytes.size());
        draco::Decoder decoder;
        draco::StatusOr<std::unique_ptr<draco::Mesh> > statusor =
            decoder.DecodeMeshFromBuffer(&buffer);
        if (!statusor.ok())
        {
            if (errorMessage) *errorMessage = statusor.status().error_msg();
            return false;
        }
        std::unique_ptr<draco::Mesh> mesh = std::move(statusor).value();
        if (!mesh)
        {
            if (errorMessage) *errorMessage = "draco decode returned no mesh";
            return false;
        }

        // indices
        out.indices.clear();
        out.indices.reserve((size_t)mesh->num_faces() * 3);
        for (draco::FaceIndex f(0); f < mesh->num_faces(); ++f)
        {
            const draco::Mesh::Face& face = mesh->face(f);
            out.indices.push_back(face[0].value());
            out.indices.push_back(face[1].value());
            out.indices.push_back(face[2].value());
        }

        // attributes (by unique id from the extension's attribute map)
        for (std::map<std::string, int>::const_iterator it = prim.dracoAttributes.begin();
             it != prim.dracoAttributes.end(); ++it)
        {
            const std::string& name = it->first;
            const draco::PointAttribute* att = FindAttribute(*mesh, it->second);
            if (!att)
            {
                continue;
            }
            if (name == "POSITION")
            {
                if (!ReadAttribute(*mesh, att, 3, out.positions))
                {
                    if (errorMessage) *errorMessage = "failed to read draco POSITION";
                    return false;
                }
            }
            else if (name == "NORMAL")
            {
                ReadAttribute(*mesh, att, 3, out.normals);
            }
            else if (name == "TEXCOORD_0")
            {
                ReadAttribute(*mesh, att, 2, out.uv0);
            }
            else if (name == "TEXCOORD_1")
            {
                ReadAttribute(*mesh, att, 2, out.uv1);
            }
            else if (name == "COLOR_0")
            {
                const int nc = att->num_components();
                if (nc == 3 || nc == 4)
                {
                    if (ReadAttribute(*mesh, att, nc, out.color0))
                    {
                        out.colorComps = nc;
                    }
                }
            }
            else if (name == "JOINTS_0")
            {
                ReadAttribute(*mesh, att, 4, out.joints0);
            }
            else if (name == "WEIGHTS_0")
            {
                ReadAttribute(*mesh, att, 4, out.weights0);
            }
        }

        if (out.positions.empty())
        {
            if (errorMessage) *errorMessage = "draco mesh has no POSITION attribute";
            return false;
        }
        return true;
    }

#else // !ENABLE_BUILD_WITH_DRACO

    bool DecodeDracoPrimitive(const Document& doc, const Primitive& prim,
                              PrimitiveData& out, std::string* errorMessage)
    {
        (void)doc;
        (void)prim;
        (void)out;
        if (errorMessage)
        {
            *errorMessage = "this build does not include draco support";
        }
        return false;
    }

#endif // ENABLE_BUILD_WITH_DRACO

} // namespace gltf
