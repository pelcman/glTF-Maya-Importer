#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif

#include "GltfDocument.h"

#include <picojson/picojson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace gltf
{
    //---------------------------------------------------------------------
    // small helpers
    //---------------------------------------------------------------------

    static bool ReadFileBytes(const std::string& path, std::vector<unsigned char>& out)
    {
        std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return false;
        }
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        out.resize((size_t)size);
        if (size > 0)
        {
            if (!file.read((char*)&out[0], size))
            {
                return false;
            }
        }
        return true;
    }

    static double JGetNumber(const picojson::object& o, const char* key, double def)
    {
        picojson::object::const_iterator it = o.find(key);
        if (it != o.end() && it->second.is<double>())
        {
            return it->second.get<double>();
        }
        return def;
    }

    static int JGetInt(const picojson::object& o, const char* key, int def)
    {
        return (int)JGetNumber(o, key, (double)def);
    }

    static std::string JGetString(const picojson::object& o, const char* key, const std::string& def = "")
    {
        picojson::object::const_iterator it = o.find(key);
        if (it != o.end() && it->second.is<std::string>())
        {
            return it->second.get<std::string>();
        }
        return def;
    }

    static bool JGetBool(const picojson::object& o, const char* key, bool def)
    {
        picojson::object::const_iterator it = o.find(key);
        if (it != o.end() && it->second.is<bool>())
        {
            return it->second.get<bool>();
        }
        return def;
    }

    static const picojson::array* JGetArray(const picojson::object& o, const char* key)
    {
        picojson::object::const_iterator it = o.find(key);
        if (it != o.end() && it->second.is<picojson::array>())
        {
            return &it->second.get<picojson::array>();
        }
        return 0;
    }

    static const picojson::object* JGetObject(const picojson::object& o, const char* key)
    {
        picojson::object::const_iterator it = o.find(key);
        if (it != o.end() && it->second.is<picojson::object>())
        {
            return &it->second.get<picojson::object>();
        }
        return 0;
    }

    static void JGetNumberArray(const picojson::object& o, const char* key, double* dst, int n)
    {
        const picojson::array* a = JGetArray(o, key);
        if (!a)
        {
            return;
        }
        for (int i = 0; i < n && i < (int)a->size(); i++)
        {
            if ((*a)[i].is<double>())
            {
                dst[i] = (*a)[i].get<double>();
            }
        }
    }

    static void JGetFloatArray(const picojson::object& o, const char* key, float* dst, int n)
    {
        const picojson::array* a = JGetArray(o, key);
        if (!a)
        {
            return;
        }
        for (int i = 0; i < n && i < (int)a->size(); i++)
        {
            if ((*a)[i].is<double>())
            {
                dst[i] = (float)(*a)[i].get<double>();
            }
        }
    }

    static void JGetIntVector(const picojson::object& o, const char* key, std::vector<int>& dst)
    {
        const picojson::array* a = JGetArray(o, key);
        if (!a)
        {
            return;
        }
        dst.reserve(a->size());
        for (size_t i = 0; i < a->size(); i++)
        {
            if ((*a)[i].is<double>())
            {
                dst.push_back((int)(*a)[i].get<double>());
            }
        }
    }

    static TextureRef ParseTextureRef(const picojson::object* o)
    {
        TextureRef ref;
        if (o)
        {
            ref.index = JGetInt(*o, "index", -1);
            ref.texCoord = JGetInt(*o, "texCoord", 0);
            // normalTexture.scale / occlusionTexture.strength
            double s = JGetNumber(*o, "scale", JGetNumber(*o, "strength", 1.0));
            ref.scale = (float)s;
        }
        return ref;
    }

    //---------------------------------------------------------------------
    // base64 / URI decoding
    //---------------------------------------------------------------------

    static int Base64Value(char c)
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    }

    static bool Base64Decode(const std::string& src, std::vector<unsigned char>& out)
    {
        out.clear();
        out.reserve(src.size() * 3 / 4 + 3);
        int val = 0;
        int bits = 0;
        for (size_t i = 0; i < src.size(); i++)
        {
            char c = src[i];
            if (c == '=' || c == '\r' || c == '\n' || c == ' ')
            {
                continue;
            }
            int v = Base64Value(c);
            if (v < 0)
            {
                return false;
            }
            val = (val << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back((unsigned char)((val >> bits) & 0xFF));
            }
        }
        return true;
    }

    static std::string PercentDecode(const std::string& src)
    {
        std::string dst;
        dst.reserve(src.size());
        for (size_t i = 0; i < src.size(); i++)
        {
            if (src[i] == '%' && i + 2 < src.size())
            {
                int hi = -1, lo = -1;
                char a = src[i + 1], b = src[i + 2];
                if (a >= '0' && a <= '9') hi = a - '0';
                else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
                else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
                if (b >= '0' && b <= '9') lo = b - '0';
                else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
                else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
                if (hi >= 0 && lo >= 0)
                {
                    dst += (char)(hi * 16 + lo);
                    i += 2;
                    continue;
                }
            }
            dst += src[i];
        }
        return dst;
    }

    bool Document::IsDataUri(const std::string& uri)
    {
        return uri.compare(0, 5, "data:") == 0;
    }

    bool Document::DecodeDataUri(const std::string& uri, std::vector<unsigned char>& out, std::string* outMime)
    {
        if (!IsDataUri(uri))
        {
            return false;
        }
        size_t comma = uri.find(',');
        if (comma == std::string::npos)
        {
            return false;
        }
        std::string header = uri.substr(5, comma - 5); // e.g. "image/png;base64"
        if (outMime)
        {
            size_t semi = header.find(';');
            *outMime = header.substr(0, semi);
        }
        if (header.find("base64") == std::string::npos)
        {
            return false;
        }
        return Base64Decode(uri.substr(comma + 1), out);
    }

    std::string Document::ResolveUri(const std::string& uri) const
    {
        std::string decoded = PercentDecode(uri);
        // absolute path? (drive letter or leading separator)
        if (decoded.size() >= 2 && decoded[1] == ':')
        {
            return decoded;
        }
        if (!decoded.empty() && (decoded[0] == '/' || decoded[0] == '\\'))
        {
            return decoded;
        }
        return baseDir + decoded;
    }

    //---------------------------------------------------------------------
    // component helpers
    //---------------------------------------------------------------------

    int Document::GetComponentCount(const std::string& type)
    {
        if (type == "SCALAR") return 1;
        if (type == "VEC2") return 2;
        if (type == "VEC3") return 3;
        if (type == "VEC4") return 4;
        if (type == "MAT2") return 4;
        if (type == "MAT3") return 9;
        if (type == "MAT4") return 16;
        return 0;
    }

    int Document::GetComponentSize(int componentType)
    {
        switch (componentType)
        {
        case CT_BYTE:
        case CT_UNSIGNED_BYTE:
            return 1;
        case CT_SHORT:
        case CT_UNSIGNED_SHORT:
            return 2;
        case CT_UNSIGNED_INT:
        case CT_FLOAT:
            return 4;
        }
        return 0;
    }

    static double ReadComponent(const unsigned char* p, int componentType)
    {
        switch (componentType)
        {
        case CT_BYTE:
        {
            signed char v;
            std::memcpy(&v, p, 1);
            return (double)v;
        }
        case CT_UNSIGNED_BYTE:
        {
            unsigned char v;
            std::memcpy(&v, p, 1);
            return (double)v;
        }
        case CT_SHORT:
        {
            short v;
            std::memcpy(&v, p, 2);
            return (double)v;
        }
        case CT_UNSIGNED_SHORT:
        {
            unsigned short v;
            std::memcpy(&v, p, 2);
            return (double)v;
        }
        case CT_UNSIGNED_INT:
        {
            unsigned int v;
            std::memcpy(&v, p, 4);
            return (double)v;
        }
        case CT_FLOAT:
        {
            float v;
            std::memcpy(&v, p, 4);
            return (double)v;
        }
        }
        return 0.0;
    }

    static double NormalizeComponent(double v, int componentType)
    {
        switch (componentType)
        {
        case CT_BYTE:
            return std::max(v / 127.0, -1.0);
        case CT_UNSIGNED_BYTE:
            return v / 255.0;
        case CT_SHORT:
            return std::max(v / 32767.0, -1.0);
        case CT_UNSIGNED_SHORT:
            return v / 65535.0;
        }
        return v;
    }

    //---------------------------------------------------------------------
    // loading
    //---------------------------------------------------------------------

    bool Document::Load(const std::string& path, std::string* errorMessage)
    {
        // split dir / base name
        size_t sep = path.find_last_of("/\\");
        baseDir = (sep == std::string::npos) ? "" : path.substr(0, sep + 1);
        std::string fname = (sep == std::string::npos) ? path : path.substr(sep + 1);
        size_t dot = fname.find_last_of('.');
        baseName = (dot == std::string::npos) ? fname : fname.substr(0, dot);

        std::vector<unsigned char> bytes;
        if (!ReadFileBytes(path, bytes))
        {
            if (errorMessage) *errorMessage = "Failed to open file: " + path;
            return false;
        }
        if (bytes.size() < 4)
        {
            if (errorMessage) *errorMessage = "File is too small: " + path;
            return false;
        }

        std::string jsonText;
        if (std::memcmp(&bytes[0], "glTF", 4) == 0)
        {
            // ---- GLB container ----
            isGLB_ = true;
            if (bytes.size() < 12)
            {
                if (errorMessage) *errorMessage = "Broken GLB header";
                return false;
            }
            unsigned int version = 0, length = 0;
            std::memcpy(&version, &bytes[4], 4);
            std::memcpy(&length, &bytes[8], 4);
            if (version != 2)
            {
                if (errorMessage)
                {
                    std::ostringstream ss;
                    ss << "Unsupported GLB version: " << version;
                    *errorMessage = ss.str();
                }
                return false;
            }
            size_t offset = 12;
            while (offset + 8 <= bytes.size())
            {
                unsigned int chunkLength = 0, chunkType = 0;
                std::memcpy(&chunkLength, &bytes[offset], 4);
                std::memcpy(&chunkType, &bytes[offset + 4], 4);
                offset += 8;
                if (offset + chunkLength > bytes.size())
                {
                    if (errorMessage) *errorMessage = "Broken GLB chunk";
                    return false;
                }
                if (chunkType == 0x4E4F534A) // 'JSON'
                {
                    jsonText.assign((const char*)&bytes[offset], chunkLength);
                }
                else if (chunkType == 0x004E4942) // 'BIN'
                {
                    glbBin_.assign(bytes.begin() + offset, bytes.begin() + offset + chunkLength);
                }
                offset += chunkLength;
            }
            if (jsonText.empty())
            {
                if (errorMessage) *errorMessage = "GLB has no JSON chunk";
                return false;
            }
        }
        else
        {
            jsonText.assign((const char*)&bytes[0], bytes.size());
        }

        if (!ParseJson(jsonText, errorMessage))
        {
            return false;
        }
        return ResolveBuffers(errorMessage);
    }

    bool Document::ParseJson(const std::string& jsonText, std::string* errorMessage)
    {
        picojson::value root;
        std::string err = picojson::parse(root, jsonText);
        if (!err.empty())
        {
            if (errorMessage) *errorMessage = "JSON parse error: " + err;
            return false;
        }
        if (!root.is<picojson::object>())
        {
            if (errorMessage) *errorMessage = "JSON root is not an object";
            return false;
        }
        const picojson::object& jroot = root.get<picojson::object>();

        // asset.version check (accept 2.x)
        const picojson::object* jasset = JGetObject(jroot, "asset");
        if (jasset)
        {
            std::string version = JGetString(*jasset, "version", "2.0");
            if (!version.empty() && version[0] != '2')
            {
                if (errorMessage) *errorMessage = "Unsupported glTF version: " + version;
                return false;
            }
        }

        // ---- buffers ----
        if (const picojson::array* ja = JGetArray(jroot, "buffers"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Buffer b;
                b.uri = JGetString(jo, "uri");
                b.byteLength = (size_t)JGetNumber(jo, "byteLength", 0);
                buffers.push_back(b);
            }
        }

        // ---- bufferViews ----
        if (const picojson::array* ja = JGetArray(jroot, "bufferViews"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                BufferView bv;
                bv.buffer = JGetInt(jo, "buffer", -1);
                bv.byteOffset = (size_t)JGetNumber(jo, "byteOffset", 0);
                bv.byteLength = (size_t)JGetNumber(jo, "byteLength", 0);
                bv.byteStride = JGetInt(jo, "byteStride", 0);
                bufferViews.push_back(bv);
            }
        }

        // ---- accessors ----
        if (const picojson::array* ja = JGetArray(jroot, "accessors"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Accessor acc;
                acc.bufferView = JGetInt(jo, "bufferView", -1);
                acc.byteOffset = (size_t)JGetNumber(jo, "byteOffset", 0);
                acc.componentType = JGetInt(jo, "componentType", 0);
                acc.normalized = JGetBool(jo, "normalized", false);
                acc.count = JGetInt(jo, "count", 0);
                acc.type = JGetString(jo, "type", "SCALAR");
                if (const picojson::object* jsparse = JGetObject(jo, "sparse"))
                {
                    acc.hasSparse = true;
                    acc.sparse.count = JGetInt(*jsparse, "count", 0);
                    if (const picojson::object* ji = JGetObject(*jsparse, "indices"))
                    {
                        acc.sparse.indicesBufferView = JGetInt(*ji, "bufferView", -1);
                        acc.sparse.indicesByteOffset = (size_t)JGetNumber(*ji, "byteOffset", 0);
                        acc.sparse.indicesComponentType = JGetInt(*ji, "componentType", 0);
                    }
                    if (const picojson::object* jv = JGetObject(*jsparse, "values"))
                    {
                        acc.sparse.valuesBufferView = JGetInt(*jv, "bufferView", -1);
                        acc.sparse.valuesByteOffset = (size_t)JGetNumber(*jv, "byteOffset", 0);
                    }
                }
                accessors.push_back(acc);
            }
        }

        // ---- images ----
        if (const picojson::array* ja = JGetArray(jroot, "images"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Image img;
                img.name = JGetString(jo, "name");
                img.uri = JGetString(jo, "uri");
                img.bufferView = JGetInt(jo, "bufferView", -1);
                img.mimeType = JGetString(jo, "mimeType");
                images.push_back(img);
            }
        }

        // ---- samplers ----
        if (const picojson::array* ja = JGetArray(jroot, "samplers"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Sampler s;
                s.wrapS = JGetInt(jo, "wrapS", WRAP_REPEAT);
                s.wrapT = JGetInt(jo, "wrapT", WRAP_REPEAT);
                samplers.push_back(s);
            }
        }

        // ---- textures ----
        if (const picojson::array* ja = JGetArray(jroot, "textures"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Texture t;
                t.source = JGetInt(jo, "source", -1);
                t.sampler = JGetInt(jo, "sampler", -1);
                // KHR_texture_basisu etc. keep source == -1; not supported
                textures.push_back(t);
            }
        }

        // ---- materials ----
        if (const picojson::array* ja = JGetArray(jroot, "materials"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Material m;
                m.name = JGetString(jo, "name");
                if (const picojson::object* jpbr = JGetObject(jo, "pbrMetallicRoughness"))
                {
                    JGetFloatArray(*jpbr, "baseColorFactor", m.baseColorFactor, 4);
                    m.baseColorTexture = ParseTextureRef(JGetObject(*jpbr, "baseColorTexture"));
                    m.metallicFactor = (float)JGetNumber(*jpbr, "metallicFactor", 1.0);
                    m.roughnessFactor = (float)JGetNumber(*jpbr, "roughnessFactor", 1.0);
                    m.metallicRoughnessTexture = ParseTextureRef(JGetObject(*jpbr, "metallicRoughnessTexture"));
                }
                m.normalTexture = ParseTextureRef(JGetObject(jo, "normalTexture"));
                m.occlusionTexture = ParseTextureRef(JGetObject(jo, "occlusionTexture"));
                m.emissiveTexture = ParseTextureRef(JGetObject(jo, "emissiveTexture"));
                JGetFloatArray(jo, "emissiveFactor", m.emissiveFactor, 3);
                m.alphaMode = JGetString(jo, "alphaMode", "OPAQUE");
                m.alphaCutoff = (float)JGetNumber(jo, "alphaCutoff", 0.5);
                m.doubleSided = JGetBool(jo, "doubleSided", false);
                materials.push_back(m);
            }
        }

        // ---- meshes ----
        if (const picojson::array* ja = JGetArray(jroot, "meshes"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Mesh mesh;
                mesh.name = JGetString(jo, "name");
                if (const picojson::array* jw = JGetArray(jo, "weights"))
                {
                    for (size_t k = 0; k < jw->size(); k++)
                    {
                        mesh.weights.push_back((float)(*jw)[k].get<double>());
                    }
                }
                if (const picojson::object* jextras = JGetObject(jo, "extras"))
                {
                    if (const picojson::array* jn = JGetArray(*jextras, "targetNames"))
                    {
                        for (size_t k = 0; k < jn->size(); k++)
                        {
                            if ((*jn)[k].is<std::string>())
                            {
                                mesh.targetNames.push_back((*jn)[k].get<std::string>());
                            }
                        }
                    }
                }
                if (const picojson::array* jprims = JGetArray(jo, "primitives"))
                {
                    for (size_t p = 0; p < jprims->size(); p++)
                    {
                        const picojson::object& jp = (*jprims)[p].get<picojson::object>();
                        Primitive prim;
                        prim.indices = JGetInt(jp, "indices", -1);
                        prim.material = JGetInt(jp, "material", -1);
                        prim.mode = JGetInt(jp, "mode", MODE_TRIANGLES);
                        if (const picojson::object* jattr = JGetObject(jp, "attributes"))
                        {
                            for (picojson::object::const_iterator it = jattr->begin(); it != jattr->end(); ++it)
                            {
                                if (it->second.is<double>())
                                {
                                    prim.attributes[it->first] = (int)it->second.get<double>();
                                }
                            }
                        }
                        if (const picojson::array* jtargets = JGetArray(jp, "targets"))
                        {
                            for (size_t t = 0; t < jtargets->size(); t++)
                            {
                                const picojson::object& jt = (*jtargets)[t].get<picojson::object>();
                                MorphTarget target;
                                for (picojson::object::const_iterator it = jt.begin(); it != jt.end(); ++it)
                                {
                                    if (it->second.is<double>())
                                    {
                                        target.attributes[it->first] = (int)it->second.get<double>();
                                    }
                                }
                                prim.targets.push_back(target);
                            }
                        }
                        if (const picojson::object* jext = JGetObject(jp, "extensions"))
                        {
                            if (const picojson::object* jdraco = JGetObject(*jext, "KHR_draco_mesh_compression"))
                            {
                                prim.hasDraco = true;
                                prim.dracoBufferView = JGetInt(*jdraco, "bufferView", -1);
                                if (const picojson::object* jda = JGetObject(*jdraco, "attributes"))
                                {
                                    for (picojson::object::const_iterator it = jda->begin(); it != jda->end(); ++it)
                                    {
                                        if (it->second.is<double>())
                                        {
                                            prim.dracoAttributes[it->first] = (int)it->second.get<double>();
                                        }
                                    }
                                }
                            }
                        }
                        mesh.primitives.push_back(prim);
                    }
                }
                meshes.push_back(mesh);
            }
        }

        // ---- nodes ----
        if (const picojson::array* ja = JGetArray(jroot, "nodes"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Node n;
                n.name = JGetString(jo, "name");
                JGetIntVector(jo, "children", n.children);
                n.mesh = JGetInt(jo, "mesh", -1);
                n.skin = JGetInt(jo, "skin", -1);
                if (JGetArray(jo, "matrix"))
                {
                    n.hasMatrix = true;
                    static const double identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                    std::memcpy(n.matrix, identity, sizeof(identity));
                    JGetNumberArray(jo, "matrix", n.matrix, 16);
                }
                JGetNumberArray(jo, "translation", n.translation, 3);
                JGetNumberArray(jo, "rotation", n.rotation, 4);
                JGetNumberArray(jo, "scale", n.scale, 3);
                nodes.push_back(n);
            }
        }

        // ---- skins ----
        if (const picojson::array* ja = JGetArray(jroot, "skins"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Skin s;
                s.name = JGetString(jo, "name");
                s.inverseBindMatrices = JGetInt(jo, "inverseBindMatrices", -1);
                s.skeleton = JGetInt(jo, "skeleton", -1);
                JGetIntVector(jo, "joints", s.joints);
                skins.push_back(s);
            }
        }

        // ---- animations ----
        if (const picojson::array* ja = JGetArray(jroot, "animations"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Animation anim;
                anim.name = JGetString(jo, "name");
                if (const picojson::array* js = JGetArray(jo, "samplers"))
                {
                    for (size_t k = 0; k < js->size(); k++)
                    {
                        const picojson::object& jsamp = (*js)[k].get<picojson::object>();
                        AnimationSampler samp;
                        samp.input = JGetInt(jsamp, "input", -1);
                        samp.output = JGetInt(jsamp, "output", -1);
                        samp.interpolation = JGetString(jsamp, "interpolation", "LINEAR");
                        anim.samplers.push_back(samp);
                    }
                }
                if (const picojson::array* jc = JGetArray(jo, "channels"))
                {
                    for (size_t k = 0; k < jc->size(); k++)
                    {
                        const picojson::object& jchan = (*jc)[k].get<picojson::object>();
                        AnimationChannel chan;
                        chan.sampler = JGetInt(jchan, "sampler", -1);
                        if (const picojson::object* jt = JGetObject(jchan, "target"))
                        {
                            chan.targetNode = JGetInt(*jt, "node", -1);
                            chan.targetPath = JGetString(*jt, "path");
                        }
                        anim.channels.push_back(chan);
                    }
                }
                animations.push_back(anim);
            }
        }

        // ---- scenes ----
        if (const picojson::array* ja = JGetArray(jroot, "scenes"))
        {
            for (size_t i = 0; i < ja->size(); i++)
            {
                const picojson::object& jo = (*ja)[i].get<picojson::object>();
                Scene s;
                s.name = JGetString(jo, "name");
                JGetIntVector(jo, "nodes", s.nodes);
                scenes.push_back(s);
            }
        }
        defaultScene = JGetInt(jroot, "scene", -1);

        return true;
    }

    bool Document::ResolveBuffers(std::string* errorMessage)
    {
        for (size_t i = 0; i < buffers.size(); i++)
        {
            Buffer& b = buffers[i];
            if (b.uri.empty())
            {
                // GLB BIN chunk
                b.data = glbBin_;
            }
            else if (IsDataUri(b.uri))
            {
                if (!DecodeDataUri(b.uri, b.data, 0))
                {
                    if (errorMessage) *errorMessage = "Failed to decode base64 buffer";
                    return false;
                }
            }
            else
            {
                std::string path = ResolveUri(b.uri);
                if (!ReadFileBytes(path, b.data))
                {
                    if (errorMessage) *errorMessage = "Failed to open buffer file: " + path;
                    return false;
                }
            }
            if (b.data.size() < b.byteLength)
            {
                if (errorMessage) *errorMessage = "Buffer is shorter than declared byteLength";
                return false;
            }
        }
        return true;
    }

    //---------------------------------------------------------------------
    // accessor decoding
    //---------------------------------------------------------------------

    bool Document::GetBufferViewBytes(int bufferViewIndex, std::vector<unsigned char>& out) const
    {
        if (bufferViewIndex < 0 || bufferViewIndex >= (int)bufferViews.size())
        {
            return false;
        }
        const BufferView& bv = bufferViews[bufferViewIndex];
        if (bv.buffer < 0 || bv.buffer >= (int)buffers.size())
        {
            return false;
        }
        const Buffer& b = buffers[bv.buffer];
        if (bv.byteOffset + bv.byteLength > b.data.size())
        {
            return false;
        }
        out.assign(b.data.begin() + bv.byteOffset, b.data.begin() + bv.byteOffset + bv.byteLength);
        return true;
    }

    // Read raw (pre-normalization) values of an accessor.
    bool Document::ReadElements(const Accessor& acc, std::vector<double>& out) const
    {
        const int components = GetComponentCount(acc.type);
        const int compSize = GetComponentSize(acc.componentType);
        if (components == 0 || compSize == 0 || acc.count < 0)
        {
            return false;
        }
        out.assign((size_t)acc.count * components, 0.0);

        if (acc.bufferView >= 0)
        {
            if (acc.bufferView >= (int)bufferViews.size())
            {
                return false;
            }
            const BufferView& bv = bufferViews[acc.bufferView];
            if (bv.buffer < 0 || bv.buffer >= (int)buffers.size())
            {
                return false;
            }
            const Buffer& buf = buffers[bv.buffer];
            const size_t elementSize = (size_t)components * compSize;
            const size_t stride = (bv.byteStride > 0) ? (size_t)bv.byteStride : elementSize;
            const size_t start = bv.byteOffset + acc.byteOffset;
            if (acc.count > 0)
            {
                const size_t needed = start + stride * (acc.count - 1) + elementSize;
                if (needed > buf.data.size())
                {
                    return false;
                }
                const unsigned char* base = &buf.data[0];
                for (int e = 0; e < acc.count; e++)
                {
                    const unsigned char* p = base + start + stride * e;
                    for (int c = 0; c < components; c++)
                    {
                        out[(size_t)e * components + c] = ReadComponent(p + (size_t)c * compSize, acc.componentType);
                    }
                }
            }
        }

        // ---- sparse substitution ----
        if (acc.hasSparse && acc.sparse.count > 0)
        {
            std::vector<unsigned char> idxBytes, valBytes;
            if (!GetBufferViewBytes(acc.sparse.indicesBufferView, idxBytes) ||
                !GetBufferViewBytes(acc.sparse.valuesBufferView, valBytes))
            {
                return false;
            }
            const int idxCompSize = GetComponentSize(acc.sparse.indicesComponentType);
            const size_t elementSize = (size_t)components * compSize;
            if (idxCompSize == 0)
            {
                return false;
            }
            if (acc.sparse.indicesByteOffset + (size_t)acc.sparse.count * idxCompSize > idxBytes.size() ||
                acc.sparse.valuesByteOffset + (size_t)acc.sparse.count * elementSize > valBytes.size())
            {
                return false;
            }
            for (int s = 0; s < acc.sparse.count; s++)
            {
                const unsigned char* ip = &idxBytes[acc.sparse.indicesByteOffset + (size_t)s * idxCompSize];
                const size_t index = (size_t)ReadComponent(ip, acc.sparse.indicesComponentType);
                if (index >= (size_t)acc.count)
                {
                    continue;
                }
                const unsigned char* vp = &valBytes[acc.sparse.valuesByteOffset + (size_t)s * elementSize];
                for (int c = 0; c < components; c++)
                {
                    out[index * components + c] = ReadComponent(vp + (size_t)c * compSize, acc.componentType);
                }
            }
        }
        return true;
    }

    bool Document::GetFloats(int accessorIndex, std::vector<float>& out, int* outComponents) const
    {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)accessors.size())
        {
            return false;
        }
        const Accessor& acc = accessors[accessorIndex];
        std::vector<double> raw;
        if (!ReadElements(acc, raw))
        {
            return false;
        }
        out.resize(raw.size());
        if (acc.normalized)
        {
            for (size_t i = 0; i < raw.size(); i++)
            {
                out[i] = (float)NormalizeComponent(raw[i], acc.componentType);
            }
        }
        else
        {
            for (size_t i = 0; i < raw.size(); i++)
            {
                out[i] = (float)raw[i];
            }
        }
        if (outComponents)
        {
            *outComponents = GetComponentCount(acc.type);
        }
        return true;
    }

    bool Document::GetUInts(int accessorIndex, std::vector<unsigned int>& out) const
    {
        out.clear();
        if (accessorIndex < 0 || accessorIndex >= (int)accessors.size())
        {
            return false;
        }
        const Accessor& acc = accessors[accessorIndex];
        std::vector<double> raw;
        if (!ReadElements(acc, raw))
        {
            return false;
        }
        out.resize(raw.size());
        for (size_t i = 0; i < raw.size(); i++)
        {
            out[i] = (unsigned int)raw[i];
        }
        return true;
    }

} // namespace gltf
