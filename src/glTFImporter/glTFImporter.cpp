#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif

#include "glTFImporter.h"
#include "DracoDecoder.h"
#include "GltfDocument.h"

#include <maya/MAnimControl.h>
#include <maya/MColor.h>
#include <maya/MColorArray.h>
#include <maya/MDagPath.h>
#include <maya/MDagPathArray.h>
#include <maya/MDistance.h>
#include <maya/MDoubleArray.h>
#include <maya/MEulerRotation.h>
#include <maya/MFileObject.h>
#include <maya/MFloatArray.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnIkJoint.h>
#include <maya/MFnMatrixData.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFnTransform.h>
#include <maya/MGlobal.h>
#include <maya/MIntArray.h>
#include <maya/MMatrix.h>
#include <maya/MPlug.h>
#include <maya/MPoint.h>
#include <maya/MPointArray.h>
#include <maya/MQuaternion.h>
#include <maya/MSelectionList.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MTime.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MVector.h>
#include <maya/MVectorArray.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    //=====================================================================
    // helpers
    //=====================================================================

    struct ImportOptions
    {
        bool importAnimations = true;
        bool importBlendShapes = true;
        bool importSkins = true;
    };

    static void ParseOptions(const MString& optionsString, ImportOptions& opts)
    {
        MStringArray items;
        optionsString.split(';', items);
        for (unsigned int i = 0; i < items.length(); i++)
        {
            MStringArray kv;
            items[i].split('=', kv);
            if (kv.length() != 2)
            {
                continue;
            }
            const MString key = kv[0];
            const bool value = (kv[1].asInt() != 0);
            if (key == "import_animations")
            {
                opts.importAnimations = value;
            }
            else if (key == "import_blendshapes")
            {
                opts.importBlendShapes = value;
            }
            else if (key == "import_skins")
            {
                opts.importSkins = value;
            }
        }
    }

    static std::string SanitizeName(const std::string& name, const std::string& fallback)
    {
        std::string src = name.empty() ? fallback : name;
        std::string dst;
        dst.reserve(src.size());
        for (size_t i = 0; i < src.size(); i++)
        {
            char c = src[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || (c == '_');
            dst += ok ? c : '_';
        }
        if (dst.empty())
        {
            dst = fallback;
        }
        if (dst[0] >= '0' && dst[0] <= '9')
        {
            dst = "_" + dst;
        }
        return dst;
    }

    static std::string EscapeMelPath(const std::string& path)
    {
        std::string dst;
        dst.reserve(path.size());
        for (size_t i = 0; i < path.size(); i++)
        {
            char c = path[i];
            if (c == '\\')
            {
                dst += '/';
            }
            else if (c == '"')
            {
                dst += "\\\"";
            }
            else
            {
                dst += c;
            }
        }
        return dst;
    }

    // numeric -> MString helpers for building MEL commands
    static MString IS(int v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", v);
        return MString(buf);
    }

    static MString FS(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return MString(buf);
    }

    static bool MakeDirectory(const std::string& path)
    {
#ifdef _WIN32
        int ret = _mkdir(path.c_str());
#else
        int ret = mkdir(path.c_str(), 0755);
#endif
        return (ret == 0) || (errno == EEXIST);
    }

    static bool FileExists(const std::string& path)
    {
        std::ifstream f(path.c_str(), std::ios::binary);
        return f.is_open();
    }

    // Reinterpret a glTF column-major matrix as a Maya matrix.
    // glTF uses column vectors (translation at elements 12..14); Maya uses row
    // vectors (translation in row 3), so the flat layout maps 1:1.
    static MMatrix GltfMatrixToMaya(const double* m, double distScale)
    {
        double rows[4][4];
        for (int r = 0; r < 4; r++)
        {
            for (int c = 0; c < 4; c++)
            {
                rows[r][c] = m[r * 4 + c];
            }
        }
        rows[3][0] *= distScale;
        rows[3][1] *= distScale;
        rows[3][2] *= distScale;
        return MMatrix(rows);
    }

    static MMatrix GltfMatrixToMaya(const float* m, double distScale)
    {
        double d[16];
        for (int i = 0; i < 16; i++)
        {
            d[i] = (double)m[i];
        }
        return GltfMatrixToMaya(d, distScale);
    }

    //=====================================================================
    // scene builder
    //=====================================================================

    class SceneBuilder
    {
    public:
        SceneBuilder(const gltf::Document& doc, const ImportOptions& opts)
            : doc_(doc), opts_(opts)
        {
            distScale_ = MDistance::uiToInternal(1.0);
        }

        MStatus Build();

    protected:
        // nodes
        void CollectJointNodes();
        void CreateNodeRecursive(int nodeIndex, const MObject& parent);
        void SetNodeTransform(int nodeIndex);
        std::string NodeName(int nodeIndex) const;
        MString NodeFullPath(int nodeIndex) const;

        // meshes
        MStatus BuildMeshForNode(int nodeIndex);
        bool LoadPrimitiveData(const gltf::Primitive& prim, gltf::PrimitiveData& data);

        // materials / textures
        MString GetShadingGroup(int materialIndex);
        MString CreateFileTexture(const gltf::TextureRef& ref, bool rawColorSpace);
        std::string GetImageFilePath(int imageIndex);

        // skinning / morphs
        MStatus BindSkin(int nodeIndex, const MDagPath& shapePath, const MDagPath& transformPath,
                         const std::vector<unsigned int>& joints0,
                         const std::vector<float>& weights0,
                         int vertexCount);

        // animation
        MStatus ImportAnimations();
        void ImportChannel(const gltf::Animation& anim, const gltf::AnimationChannel& channel);

    protected:
        const gltf::Document& doc_;
        ImportOptions opts_;
        double distScale_;

        std::vector<MObject> nodeObjects_;
        std::set<int> jointNodeSet_;
        std::map<int, MString> materialSG_;              // material index -> SG name
        std::map<int, std::string> extractedImages_;     // image index -> file path
        std::map<int, MObject> nodeBlendShape_;          // node index -> blendShape node
        std::map<int, int> nodeTargetCount_;             // node index -> morph target count
        std::string textureDir_;
        bool textureDirCreated_ = false;

        MTime animMinTime_ = MTime(0.0, MTime::kSeconds);
        MTime animMaxTime_ = MTime(0.0, MTime::kSeconds);
        bool hasAnimKeys_ = false;
    };

    //---------------------------------------------------------------------

    MStatus SceneBuilder::Build()
    {
        nodeObjects_.assign(doc_.nodes.size(), MObject::kNullObj);
        CollectJointNodes();

        // determine root node set
        std::vector<int> roots;
        if (doc_.defaultScene >= 0 && doc_.defaultScene < (int)doc_.scenes.size())
        {
            roots = doc_.scenes[doc_.defaultScene].nodes;
        }
        else if (!doc_.scenes.empty())
        {
            roots = doc_.scenes[0].nodes;
        }
        else
        {
            // no scene: import every node that is not a child of another node
            std::set<int> childSet;
            for (size_t i = 0; i < doc_.nodes.size(); i++)
            {
                for (size_t c = 0; c < doc_.nodes[i].children.size(); c++)
                {
                    childSet.insert(doc_.nodes[i].children[c]);
                }
            }
            for (int i = 0; i < (int)doc_.nodes.size(); i++)
            {
                if (childSet.find(i) == childSet.end())
                {
                    roots.push_back(i);
                }
            }
        }

        for (size_t i = 0; i < roots.size(); i++)
        {
            CreateNodeRecursive(roots[i], MObject::kNullObj);
        }

        // joints referenced by skins but not reachable from the scene
        for (size_t s = 0; s < doc_.skins.size(); s++)
        {
            const std::vector<int>& joints = doc_.skins[s].joints;
            for (size_t j = 0; j < joints.size(); j++)
            {
                int idx = joints[j];
                if (idx >= 0 && idx < (int)nodeObjects_.size() && nodeObjects_[idx].isNull())
                {
                    CreateNodeRecursive(idx, MObject::kNullObj);
                }
            }
        }

        // meshes (materials are created lazily on demand)
        for (int i = 0; i < (int)doc_.nodes.size(); i++)
        {
            if (!nodeObjects_[i].isNull() && doc_.nodes[i].mesh >= 0)
            {
                BuildMeshForNode(i);
            }
        }

        // animations
        if (opts_.importAnimations && !doc_.animations.empty())
        {
            ImportAnimations();
        }

        return MS::kSuccess;
    }

    //---------------------------------------------------------------------
    // nodes
    //---------------------------------------------------------------------

    void SceneBuilder::CollectJointNodes()
    {
        for (size_t s = 0; s < doc_.skins.size(); s++)
        {
            const std::vector<int>& joints = doc_.skins[s].joints;
            for (size_t j = 0; j < joints.size(); j++)
            {
                jointNodeSet_.insert(joints[j]);
            }
        }
    }

    std::string SceneBuilder::NodeName(int nodeIndex) const
    {
        std::ostringstream fallback;
        fallback << "node_" << nodeIndex;
        return SanitizeName(doc_.nodes[nodeIndex].name, fallback.str());
    }

    MString SceneBuilder::NodeFullPath(int nodeIndex) const
    {
        MDagPath path;
        MDagPath::getAPathTo(nodeObjects_[nodeIndex], path);
        return path.fullPathName();
    }

    void SceneBuilder::CreateNodeRecursive(int nodeIndex, const MObject& parent)
    {
        if (nodeIndex < 0 || nodeIndex >= (int)doc_.nodes.size())
        {
            return;
        }
        if (!nodeObjects_[nodeIndex].isNull())
        {
            return; // already created
        }
        const gltf::Node& node = doc_.nodes[nodeIndex];

        // Per the glTF spec, the transform of a skinned mesh node (and of its
        // ancestors) must be ignored: only the joint transforms apply. So a
        // skinned mesh node is created at the world root with an identity
        // transform.
        const bool skinnedMeshNode = (node.skin >= 0 && node.mesh >= 0);
        const MObject actualParent = skinnedMeshNode ? MObject::kNullObj : parent;

        MStatus status;
        MObject obj;
        if (jointNodeSet_.find(nodeIndex) != jointNodeSet_.end())
        {
            MFnIkJoint fnJoint;
            obj = fnJoint.create(actualParent, &status);
        }
        else
        {
            MFnTransform fnTransform;
            obj = fnTransform.create(actualParent, &status);
        }
        if (!status)
        {
            MGlobal::displayWarning(MString("glTFImporter: failed to create node ") + IS(nodeIndex));
            return;
        }
        MFnDagNode fnDag(obj);
        fnDag.setName(NodeName(nodeIndex).c_str());
        nodeObjects_[nodeIndex] = obj;

        if (!skinnedMeshNode)
        {
            SetNodeTransform(nodeIndex);
        }

        for (size_t c = 0; c < node.children.size(); c++)
        {
            CreateNodeRecursive(node.children[c], obj);
        }
    }

    void SceneBuilder::SetNodeTransform(int nodeIndex)
    {
        const gltf::Node& node = doc_.nodes[nodeIndex];
        MFnTransform fnTransform(nodeObjects_[nodeIndex]);

        if (node.hasMatrix)
        {
            MTransformationMatrix tm(GltfMatrixToMaya(node.matrix, distScale_));
            fnTransform.set(tm);
        }
        else
        {
            MTransformationMatrix tm;
            tm.setTranslation(MVector(node.translation[0] * distScale_,
                                      node.translation[1] * distScale_,
                                      node.translation[2] * distScale_),
                              MSpace::kTransform);
            tm.setRotationQuaternion(node.rotation[0], node.rotation[1],
                                     node.rotation[2], node.rotation[3],
                                     MSpace::kTransform);
            const double scale[3] = {node.scale[0], node.scale[1], node.scale[2]};
            tm.setScale(scale, MSpace::kTransform);
            fnTransform.set(tm);
        }
    }

    //---------------------------------------------------------------------
    // meshes
    //---------------------------------------------------------------------

    // convert one primitive's index list to a triangle list
    static bool ToTriangleList(int mode, const std::vector<unsigned int>& indices,
                               std::vector<unsigned int>& tris)
    {
        tris.clear();
        if (mode == gltf::MODE_TRIANGLES)
        {
            size_t n = indices.size() / 3 * 3;
            tris.assign(indices.begin(), indices.begin() + n);
            return true;
        }
        else if (mode == gltf::MODE_TRIANGLE_STRIP)
        {
            if (indices.size() < 3)
            {
                return false;
            }
            for (size_t i = 0; i + 2 < indices.size(); i++)
            {
                if (i % 2 == 0)
                {
                    tris.push_back(indices[i]);
                    tris.push_back(indices[i + 1]);
                    tris.push_back(indices[i + 2]);
                }
                else
                {
                    tris.push_back(indices[i + 1]);
                    tris.push_back(indices[i]);
                    tris.push_back(indices[i + 2]);
                }
            }
            return true;
        }
        else if (mode == gltf::MODE_TRIANGLE_FAN)
        {
            if (indices.size() < 3)
            {
                return false;
            }
            for (size_t i = 1; i + 1 < indices.size(); i++)
            {
                tris.push_back(indices[0]);
                tris.push_back(indices[i]);
                tris.push_back(indices[i + 1]);
            }
            return true;
        }
        return false; // points / lines are not supported
    }

    struct PrimRange
    {
        int faceStart = 0;
        int faceCount = 0;
        int material = -1;
    };

    // Load one primitive's vertex data, from draco or from standard accessors.
    bool SceneBuilder::LoadPrimitiveData(const gltf::Primitive& prim, gltf::PrimitiveData& data)
    {
        if (prim.hasDraco)
        {
            std::string errorMessage;
            if (!gltf::DecodeDracoPrimitive(doc_, prim, data, &errorMessage))
            {
                MGlobal::displayWarning(MString("glTFImporter: draco decode failed (") +
                                        errorMessage.c_str() + "). Primitive skipped.");
                return false;
            }
            const size_t vc = data.VertexCount();
            if (data.normals.size() < vc * 3)
            {
                data.normals.clear();
            }
            if (data.uv0.size() < vc * 2)
            {
                data.uv0.clear();
            }
            if (data.uv1.size() < vc * 2)
            {
                data.uv1.clear();
            }
            if (data.colorComps == 0 || data.color0.size() < vc * (size_t)data.colorComps)
            {
                data.color0.clear();
                data.colorComps = 0;
            }
            if (data.joints0.size() < vc * 4 || data.weights0.size() < vc * 4)
            {
                data.joints0.clear();
                data.weights0.clear();
            }
            return true;
        }

        int comps = 0;
        if (!doc_.GetFloats(prim.GetAttribute("POSITION"), data.positions, &comps) || comps != 3)
        {
            MGlobal::displayWarning("glTFImporter: primitive without POSITION skipped.");
            return false;
        }
        const size_t vc = data.positions.size() / 3;

        if (prim.indices >= 0)
        {
            if (!doc_.GetUInts(prim.indices, data.indices))
            {
                MGlobal::displayWarning("glTFImporter: failed to read indices. Primitive skipped.");
                return false;
            }
        }
        else
        {
            data.indices.resize(vc);
            for (size_t i = 0; i < vc; i++)
            {
                data.indices[i] = (unsigned int)i;
            }
        }

        std::vector<float> attr;
        if (doc_.GetFloats(prim.GetAttribute("NORMAL"), attr, &comps) && comps == 3 &&
            attr.size() >= vc * 3)
        {
            data.normals.assign(attr.begin(), attr.begin() + vc * 3);
        }
        if (doc_.GetFloats(prim.GetAttribute("TEXCOORD_0"), attr, &comps) && comps == 2 &&
            attr.size() >= vc * 2)
        {
            data.uv0.assign(attr.begin(), attr.begin() + vc * 2);
        }
        if (doc_.GetFloats(prim.GetAttribute("TEXCOORD_1"), attr, &comps) && comps == 2 &&
            attr.size() >= vc * 2)
        {
            data.uv1.assign(attr.begin(), attr.begin() + vc * 2);
        }
        if (doc_.GetFloats(prim.GetAttribute("COLOR_0"), attr, &comps) &&
            (comps == 3 || comps == 4) && attr.size() >= vc * comps)
        {
            data.color0.assign(attr.begin(), attr.begin() + vc * comps);
            data.colorComps = comps;
        }
        std::vector<unsigned int> jtmp;
        std::vector<float> wtmp;
        int wc = 0;
        if (doc_.GetUInts(prim.GetAttribute("JOINTS_0"), jtmp) &&
            doc_.GetFloats(prim.GetAttribute("WEIGHTS_0"), wtmp, &wc) && wc == 4 &&
            jtmp.size() >= vc * 4 && wtmp.size() >= vc * 4)
        {
            data.joints0.assign(jtmp.begin(), jtmp.begin() + vc * 4);
            data.weights0.assign(wtmp.begin(), wtmp.begin() + vc * 4);
        }
        return true;
    }

    MStatus SceneBuilder::BuildMeshForNode(int nodeIndex)
    {
        const gltf::Node& node = doc_.nodes[nodeIndex];
        const gltf::Mesh& mesh = doc_.meshes[node.mesh];

        MPointArray points;
        MIntArray polyCounts;
        MIntArray polyConnects;
        MVectorArray normals;
        MIntArray normalIds;
        MColorArray colors;
        MIntArray colorIds;
        MFloatArray uArr0, vArr0;
        MFloatArray uArr1, vArr1;
        bool hasUV0 = false;
        bool hasUV1 = false;
        std::vector<PrimRange> ranges;

        // skinning source data, 4 influences per merged vertex
        std::vector<unsigned int> joints0;
        std::vector<float> weights0;
        bool hasSkinData = false;

        // morph targets: deltas per merged vertex
        size_t numTargets = 0;
        if (!mesh.primitives.empty())
        {
            numTargets = mesh.primitives[0].targets.size();
        }
        std::vector<std::vector<MVector> > targetDeltas(numTargets);

        for (size_t p = 0; p < mesh.primitives.size(); p++)
        {
            const gltf::Primitive& prim = mesh.primitives[p];
            if (prim.mode != gltf::MODE_TRIANGLES &&
                prim.mode != gltf::MODE_TRIANGLE_STRIP &&
                prim.mode != gltf::MODE_TRIANGLE_FAN)
            {
                MGlobal::displayWarning(
                    "glTFImporter: points / lines primitives are not supported. Primitive skipped.");
                continue;
            }

            gltf::PrimitiveData data;
            if (!LoadPrimitiveData(prim, data))
            {
                continue;
            }
            const unsigned int vertexCount = (unsigned int)data.VertexCount();
            if (vertexCount == 0)
            {
                continue;
            }
            const unsigned int vertexOffset = points.length();

            std::vector<unsigned int> tris;
            if (!ToTriangleList(prim.mode, data.indices, tris))
            {
                continue;
            }
            bool outOfRange = false;
            for (size_t i = 0; i < tris.size(); i++)
            {
                if (tris[i] >= vertexCount)
                {
                    outOfRange = true;
                    break;
                }
            }
            if (outOfRange)
            {
                MGlobal::displayWarning("glTFImporter: index out of range. Primitive skipped.");
                continue;
            }

            // positions
            for (unsigned int i = 0; i < vertexCount; i++)
            {
                points.append(MPoint(data.positions[i * 3 + 0] * distScale_,
                                     data.positions[i * 3 + 1] * distScale_,
                                     data.positions[i * 3 + 2] * distScale_));
            }

            // faces
            PrimRange range;
            range.faceStart = (int)polyCounts.length();
            range.faceCount = (int)(tris.size() / 3);
            range.material = prim.material;
            ranges.push_back(range);
            for (size_t i = 0; i + 2 < tris.size(); i += 3)
            {
                polyCounts.append(3);
                polyConnects.append((int)(vertexOffset + tris[i + 0]));
                polyConnects.append((int)(vertexOffset + tris[i + 1]));
                polyConnects.append((int)(vertexOffset + tris[i + 2]));
            }

            // normals
            if (!data.normals.empty())
            {
                for (unsigned int i = 0; i < vertexCount; i++)
                {
                    normals.append(MVector(data.normals[i * 3 + 0], data.normals[i * 3 + 1],
                                           data.normals[i * 3 + 2]));
                    normalIds.append((int)(vertexOffset + i));
                }
            }

            // UV sets (pad earlier vertices with zero so uv id == vertex id)
            if (!data.uv0.empty())
            {
                hasUV0 = true;
                while (uArr0.length() < vertexOffset)
                {
                    uArr0.append(0.0f);
                    vArr0.append(0.0f);
                }
                for (unsigned int i = 0; i < vertexCount; i++)
                {
                    uArr0.append(data.uv0[i * 2 + 0]);
                    vArr0.append(1.0f - data.uv0[i * 2 + 1]); // glTF V is top-down
                }
            }
            if (!data.uv1.empty())
            {
                hasUV1 = true;
                while (uArr1.length() < vertexOffset)
                {
                    uArr1.append(0.0f);
                    vArr1.append(0.0f);
                }
                for (unsigned int i = 0; i < vertexCount; i++)
                {
                    uArr1.append(data.uv1[i * 2 + 0]);
                    vArr1.append(1.0f - data.uv1[i * 2 + 1]);
                }
            }

            // vertex colors
            if (data.colorComps == 3 || data.colorComps == 4)
            {
                const int cc = data.colorComps;
                for (unsigned int i = 0; i < vertexCount; i++)
                {
                    const float r = data.color0[i * cc + 0];
                    const float g = data.color0[i * cc + 1];
                    const float b = data.color0[i * cc + 2];
                    const float a = (cc == 4) ? data.color0[i * cc + 3] : 1.0f;
                    colors.append(MColor(r, g, b, a));
                    colorIds.append((int)(vertexOffset + i));
                }
            }

            // skin weights
            if (!data.joints0.empty() && !data.weights0.empty())
            {
                hasSkinData = true;
                joints0.resize((size_t)vertexOffset * 4, 0);
                weights0.resize((size_t)vertexOffset * 4, 0.0f);
                joints0.insert(joints0.end(), data.joints0.begin(),
                               data.joints0.begin() + (size_t)vertexCount * 4);
                weights0.insert(weights0.end(), data.weights0.begin(),
                                data.weights0.begin() + (size_t)vertexCount * 4);
            }
            else
            {
                // keep arrays aligned with the merged vertex list
                joints0.resize(((size_t)vertexOffset + vertexCount) * 4, 0);
                weights0.resize(((size_t)vertexOffset + vertexCount) * 4, 0.0f);
            }

            // morph target deltas
            for (size_t t = 0; t < numTargets; t++)
            {
                std::vector<MVector>& deltas = targetDeltas[t];
                deltas.resize(vertexOffset, MVector::zero);
                std::vector<float> dpos;
                int comps = 0;
                bool ok = false;
                if (t < prim.targets.size())
                {
                    std::map<std::string, int>::const_iterator it = prim.targets[t].attributes.find("POSITION");
                    if (it != prim.targets[t].attributes.end())
                    {
                        ok = doc_.GetFloats(it->second, dpos, &comps) && comps == 3 &&
                             dpos.size() >= (size_t)vertexCount * 3;
                    }
                }
                for (unsigned int i = 0; i < vertexCount; i++)
                {
                    if (ok)
                    {
                        deltas.push_back(MVector(dpos[i * 3 + 0] * distScale_,
                                                 dpos[i * 3 + 1] * distScale_,
                                                 dpos[i * 3 + 2] * distScale_));
                    }
                    else
                    {
                        deltas.push_back(MVector::zero);
                    }
                }
            }
        }

        if (points.length() == 0 || polyCounts.length() == 0)
        {
            return MS::kFailure;
        }

        // ---- create the mesh ----
        MStatus status;
        MFnMesh fnMesh;
        MObject created = fnMesh.create((int)points.length(), (int)polyCounts.length(),
                                        points, polyCounts, polyConnects,
                                        nodeObjects_[nodeIndex], &status);
        if (!status)
        {
            MGlobal::displayWarning(MString("glTFImporter: failed to create mesh for node ") + IS(nodeIndex));
            return status;
        }
        MObject shapeObj = created;
        if (created.hasFn(MFn::kTransform))
        {
            MFnDagNode fnDag(created);
            if (fnDag.childCount() > 0)
            {
                shapeObj = fnDag.child(0);
            }
        }
        {
            MFnDagNode fnShape(shapeObj);
            fnShape.setName((NodeName(nodeIndex) + "Shape").c_str());
        }
        fnMesh.setObject(shapeObj);

        // ---- UVs ----
        if (hasUV0)
        {
            while (uArr0.length() < points.length())
            {
                uArr0.append(0.0f);
                vArr0.append(0.0f);
            }
            fnMesh.setUVs(uArr0, vArr0);
            fnMesh.assignUVs(polyCounts, polyConnects);
        }
        if (hasUV1)
        {
            while (uArr1.length() < points.length())
            {
                uArr1.append(0.0f);
                vArr1.append(0.0f);
            }
            MString uvSetName = fnMesh.createUVSetWithName("map2");
            fnMesh.setUVs(uArr1, vArr1, &uvSetName);
            fnMesh.assignUVs(polyCounts, polyConnects, &uvSetName);
        }

        // ---- normals ----
        if (normalIds.length() == points.length())
        {
            fnMesh.setVertexNormals(normals, normalIds);
        }
        else if (normalIds.length() > 0)
        {
            fnMesh.setVertexNormals(normals, normalIds);
        }

        // ---- vertex colors ----
        if (colorIds.length() > 0)
        {
            MString colorSetName = fnMesh.createColorSetWithName("colorSet1");
            fnMesh.setCurrentColorSetName(colorSetName);
            fnMesh.setVertexColors(colors, colorIds);
        }

        // ---- shading assignment ----
        MDagPath shapePath;
        MDagPath::getAPathTo(shapeObj, shapePath);
        const MString shapeFullPath = shapePath.fullPathName();
        for (size_t r = 0; r < ranges.size(); r++)
        {
            MString sg = GetShadingGroup(ranges[r].material);
            MString cmd;
            if (ranges.size() == 1)
            {
                cmd = MString("sets -e -forceElement ") + sg + " " + shapeFullPath + ";";
            }
            else
            {
                MString faceRange = shapeFullPath + ".f[" + IS(ranges[r].faceStart) + ":" +
                                    IS(ranges[r].faceStart + ranges[r].faceCount - 1) + "]";
                cmd = MString("sets -e -forceElement ") + sg + " " + faceRange + ";";
            }
            MGlobal::executeCommand(cmd);
        }

        MDagPath transformPath;
        MDagPath::getAPathTo(nodeObjects_[nodeIndex], transformPath);

        // ---- morph targets (blendShape) ----
        if (numTargets > 0 && opts_.importBlendShapes)
        {
            MStringArray targetPaths;
            std::vector<MObject> targetTransforms;
            for (size_t t = 0; t < numTargets; t++)
            {
                MPointArray tpoints(points);
                const std::vector<MVector>& deltas = targetDeltas[t];
                for (unsigned int i = 0; i < tpoints.length() && i < (unsigned int)deltas.size(); i++)
                {
                    tpoints[i] += deltas[i];
                }
                MFnMesh tfn;
                MObject ttrans = tfn.create((int)tpoints.length(), (int)polyCounts.length(),
                                            tpoints, polyCounts, polyConnects,
                                            MObject::kNullObj, &status);
                if (!status)
                {
                    continue;
                }
                std::ostringstream fallback;
                fallback << "morphTarget_" << t;
                std::string tname = (t < mesh.targetNames.size())
                                        ? SanitizeName(mesh.targetNames[t], fallback.str())
                                        : fallback.str();
                MFnDagNode tdag(ttrans);
                tdag.setName(tname.c_str());
                MDagPath tpath;
                MDagPath::getAPathTo(ttrans, tpath);
                targetPaths.append(tpath.fullPathName());
                targetTransforms.push_back(ttrans);
            }

            if (targetPaths.length() > 0)
            {
                MString cmd = "blendShape -frontOfChain";
                for (unsigned int t = 0; t < targetPaths.length(); t++)
                {
                    cmd += " ";
                    cmd += targetPaths[t];
                }
                cmd += " ";
                cmd += transformPath.fullPathName();
                cmd += ";";
                MStringArray result;
                if (MGlobal::executeCommand(cmd, result) && result.length() > 0)
                {
                    MSelectionList sel;
                    sel.add(result[0]);
                    MObject bsObj;
                    if (sel.getDependNode(0, bsObj) == MS::kSuccess)
                    {
                        nodeBlendShape_[nodeIndex] = bsObj;
                        nodeTargetCount_[nodeIndex] = (int)targetPaths.length();

                        // default weights
                        MFnDependencyNode fnBS(bsObj);
                        MPlug weightArray = fnBS.findPlug("weight", false);
                        for (size_t t = 0; t < mesh.weights.size() && t < numTargets; t++)
                        {
                            MPlug w = weightArray.elementByLogicalIndex((unsigned int)t);
                            w.setValue((double)mesh.weights[t]);
                        }
                    }
                }
                // delete the target geometry (deltas are kept inside the blendShape)
                MString delCmd = "delete";
                for (unsigned int t = 0; t < targetPaths.length(); t++)
                {
                    delCmd += " ";
                    delCmd += targetPaths[t];
                }
                delCmd += ";";
                MGlobal::executeCommand(delCmd);
            }
        }

        // ---- skinning ----
        if (node.skin >= 0 && node.skin < (int)doc_.skins.size() && hasSkinData && opts_.importSkins)
        {
            BindSkin(nodeIndex, shapePath, transformPath, joints0, weights0, (int)points.length());
        }

        return MS::kSuccess;
    }

    //---------------------------------------------------------------------
    // skinning
    //---------------------------------------------------------------------

    MStatus SceneBuilder::BindSkin(int nodeIndex, const MDagPath& shapePath,
                                   const MDagPath& transformPath,
                                   const std::vector<unsigned int>& joints0,
                                   const std::vector<float>& weights0,
                                   int vertexCount)
    {
        const gltf::Skin& skin = doc_.skins[doc_.nodes[nodeIndex].skin];
        if (skin.joints.empty())
        {
            return MS::kFailure;
        }

        // resolve joint paths
        MDagPathArray jointPaths;
        for (size_t j = 0; j < skin.joints.size(); j++)
        {
            const int idx = skin.joints[j];
            if (idx < 0 || idx >= (int)nodeObjects_.size() || nodeObjects_[idx].isNull())
            {
                MGlobal::displayWarning("glTFImporter: skin references a missing joint. Skin skipped.");
                return MS::kFailure;
            }
            MDagPath path;
            MDagPath::getAPathTo(nodeObjects_[idx], path);
            jointPaths.append(path);
        }

        // bind
        MString cmd = "skinCluster -toSelectedBones -normalizeWeights 1";
        for (unsigned int j = 0; j < jointPaths.length(); j++)
        {
            cmd += " ";
            cmd += jointPaths[j].fullPathName();
        }
        cmd += " ";
        cmd += transformPath.fullPathName();
        cmd += ";";
        MStringArray result;
        MStatus status = MGlobal::executeCommand(cmd, result);
        if (!status || result.length() == 0)
        {
            MGlobal::displayWarning("glTFImporter: skinCluster command failed.");
            return MS::kFailure;
        }

        MSelectionList sel;
        sel.add(result[0]);
        MObject skinObj;
        if (sel.getDependNode(0, skinObj) != MS::kSuccess)
        {
            return MS::kFailure;
        }
        MFnSkinCluster fnSkin(skinObj, &status);
        if (!status)
        {
            return MS::kFailure;
        }

        // physical influence index for each glTF joint
        std::vector<unsigned int> influenceIndex(jointPaths.length(), 0);
        for (unsigned int j = 0; j < jointPaths.length(); j++)
        {
            influenceIndex[j] = fnSkin.indexForInfluenceObject(jointPaths[j], &status);
            if (!status)
            {
                MGlobal::displayWarning("glTFImporter: failed to find influence index.");
                return MS::kFailure;
            }
        }
        MDagPathArray allInfluences;
        const unsigned int numInfluences = fnSkin.influenceObjects(allInfluences, &status);

        // dense weight table
        MDoubleArray weightValues((unsigned int)vertexCount * numInfluences, 0.0);
        for (int v = 0; v < vertexCount; v++)
        {
            double sum = 0.0;
            for (int k = 0; k < 4; k++)
            {
                const size_t src = (size_t)v * 4 + k;
                const float w = weights0[src];
                if (w <= 0.0f)
                {
                    continue;
                }
                const unsigned int j = joints0[src];
                if (j >= influenceIndex.size())
                {
                    continue;
                }
                weightValues[(unsigned int)v * numInfluences + influenceIndex[j]] += (double)w;
                sum += (double)w;
            }
            if (sum > 0.0)
            {
                for (unsigned int i = 0; i < numInfluences; i++)
                {
                    weightValues[(unsigned int)v * numInfluences + i] /= sum;
                }
            }
        }

        MIntArray influenceIndices(numInfluences);
        for (unsigned int i = 0; i < numInfluences; i++)
        {
            influenceIndices[i] = (int)i;
        }

        MFnSingleIndexedComponent fnComp;
        MObject compObj = fnComp.create(MFn::kMeshVertComponent);
        fnComp.setCompleteData(vertexCount);

        MDagPath deformedPath(shapePath);
        status = fnSkin.setWeights(deformedPath, compObj, influenceIndices, weightValues, false);
        if (!status)
        {
            MGlobal::displayWarning("glTFImporter: failed to set skin weights.");
        }

        // inverse bind matrices -> bindPreMatrix
        std::vector<float> ibms;
        int comps = 0;
        if (skin.inverseBindMatrices >= 0 &&
            doc_.GetFloats(skin.inverseBindMatrices, ibms, &comps) && comps == 16 &&
            ibms.size() >= skin.joints.size() * 16)
        {
            MPlug bindPreMatrixArray = fnSkin.findPlug("bindPreMatrix", false, &status);
            if (status)
            {
                for (unsigned int j = 0; j < jointPaths.length(); j++)
                {
                    MMatrix ibm = GltfMatrixToMaya(&ibms[(size_t)j * 16], distScale_);
                    MPlug elem = bindPreMatrixArray.elementByLogicalIndex(influenceIndex[j], &status);
                    if (!status)
                    {
                        continue;
                    }
                    MFnMatrixData mdata;
                    MObject mobj = mdata.create(ibm);
                    elem.setValue(mobj);
                }
            }
        }

        return MS::kSuccess;
    }

    //---------------------------------------------------------------------
    // materials / textures
    //---------------------------------------------------------------------

    std::string SceneBuilder::GetImageFilePath(int imageIndex)
    {
        std::map<int, std::string>::const_iterator found = extractedImages_.find(imageIndex);
        if (found != extractedImages_.end())
        {
            return found->second;
        }
        if (imageIndex < 0 || imageIndex >= (int)doc_.images.size())
        {
            return "";
        }
        const gltf::Image& image = doc_.images[imageIndex];

        std::string path;
        if (!image.uri.empty() && !gltf::Document::IsDataUri(image.uri))
        {
            // external file: reference in place
            path = doc_.ResolveUri(image.uri);
            if (!FileExists(path))
            {
                MGlobal::displayWarning(MString("glTFImporter: image file not found: ") + path.c_str());
            }
        }
        else
        {
            // embedded: extract to <basename>_textures/ next to the source file
            std::vector<unsigned char> bytes;
            std::string mime = image.mimeType;
            bool ok = false;
            if (!image.uri.empty())
            {
                ok = gltf::Document::DecodeDataUri(image.uri, bytes, &mime);
            }
            else if (image.bufferView >= 0)
            {
                ok = doc_.GetBufferViewBytes(image.bufferView, bytes);
            }
            if (ok && !bytes.empty())
            {
                if (!textureDirCreated_)
                {
                    textureDir_ = doc_.baseDir + doc_.baseName + "_textures";
                    MakeDirectory(textureDir_);
                    textureDirCreated_ = true;
                }
                std::string ext = ".png";
                if (mime == "image/jpeg")
                {
                    ext = ".jpg";
                }
                else if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8)
                {
                    ext = ".jpg";
                }
                std::ostringstream nameStream;
                if (!image.name.empty())
                {
                    nameStream << SanitizeName(image.name, "image") << "_" << imageIndex << ext;
                }
                else
                {
                    nameStream << "image_" << imageIndex << ext;
                }
                path = textureDir_ + "/" + nameStream.str();
                std::ofstream out(path.c_str(), std::ios::binary);
                if (out.is_open())
                {
                    out.write((const char*)&bytes[0], bytes.size());
                }
                else
                {
                    MGlobal::displayWarning(MString("glTFImporter: failed to write image: ") + path.c_str());
                    path = "";
                }
            }
        }
        extractedImages_[imageIndex] = path;
        return path;
    }

    MString SceneBuilder::CreateFileTexture(const gltf::TextureRef& ref, bool rawColorSpace)
    {
        if (!ref.IsValid() || ref.index >= (int)doc_.textures.size())
        {
            return "";
        }
        const gltf::Texture& tex = doc_.textures[ref.index];
        std::string imagePath = GetImageFilePath(tex.source);
        if (imagePath.empty())
        {
            return "";
        }

        MStatus status;
        MString fileNode;
        status = MGlobal::executeCommand("shadingNode -asTexture -isColorManaged file", fileNode);
        if (!status || fileNode.length() == 0)
        {
            return "";
        }
        MString place2d;
        MGlobal::executeCommand("shadingNode -asUtility place2dTexture", place2d);
        if (place2d.length() > 0)
        {
            static const char* kLinks[][2] = {
                {"coverage", "coverage"},
                {"translateFrame", "translateFrame"},
                {"rotateFrame", "rotateFrame"},
                {"mirrorU", "mirrorU"},
                {"mirrorV", "mirrorV"},
                {"stagger", "stagger"},
                {"wrapU", "wrapU"},
                {"wrapV", "wrapV"},
                {"repeatUV", "repeatUV"},
                {"offset", "offset"},
                {"rotateUV", "rotateUV"},
                {"noiseUV", "noiseUV"},
                {"vertexUvOne", "vertexUvOne"},
                {"vertexUvTwo", "vertexUvTwo"},
                {"vertexUvThree", "vertexUvThree"},
                {"vertexCameraOne", "vertexCameraOne"},
                {"outUV", "uvCoord"},
                {"outUvFilterSize", "uvFilterSize"},
            };
            MString cmd;
            for (size_t i = 0; i < sizeof(kLinks) / sizeof(kLinks[0]); i++)
            {
                cmd += MString("connectAttr -f ") + place2d + "." + kLinks[i][0] + " " +
                       fileNode + "." + kLinks[i][1] + ";";
            }
            MGlobal::executeCommand(cmd);
        }

        // file path
        {
            MString cmd = MString("setAttr -type \"string\" ") + fileNode + ".fileTextureName \"" +
                          EscapeMelPath(imagePath).c_str() + "\";";
            MGlobal::executeCommand(cmd);
        }

        // color space
        if (rawColorSpace)
        {
            MString cmd = MString("setAttr -type \"string\" ") + fileNode + ".colorSpace \"Raw\";";
            cmd += MString("setAttr ") + fileNode + ".ignoreColorSpaceFileRules 1;";
            MGlobal::executeCommand(cmd);
        }

        // sampler wrap modes
        if (tex.sampler >= 0 && tex.sampler < (int)doc_.samplers.size() && place2d.length() > 0)
        {
            const gltf::Sampler& sampler = doc_.samplers[tex.sampler];
            MString cmd;
            if (sampler.wrapS == gltf::WRAP_CLAMP_TO_EDGE)
            {
                cmd += MString("setAttr ") + place2d + ".wrapU 0;";
            }
            else if (sampler.wrapS == gltf::WRAP_MIRRORED_REPEAT)
            {
                cmd += MString("setAttr ") + place2d + ".mirrorU 1;";
            }
            if (sampler.wrapT == gltf::WRAP_CLAMP_TO_EDGE)
            {
                cmd += MString("setAttr ") + place2d + ".wrapV 0;";
            }
            else if (sampler.wrapT == gltf::WRAP_MIRRORED_REPEAT)
            {
                cmd += MString("setAttr ") + place2d + ".mirrorV 1;";
            }
            if (cmd.length() > 0)
            {
                MGlobal::executeCommand(cmd);
            }
        }

        // texCoord == 1 -> use the second UV set via uvChooser (Maya links UV sets
        // per shape; keep it simple and warn)
        if (ref.texCoord != 0)
        {
            MGlobal::displayWarning(
                "glTFImporter: texCoord != 0 is not linked automatically (use the UV linking editor).");
        }

        return fileNode;
    }

    MString SceneBuilder::GetShadingGroup(int materialIndex)
    {
        std::map<int, MString>::const_iterator found = materialSG_.find(materialIndex);
        if (found != materialSG_.end())
        {
            return found->second;
        }
        if (materialIndex < 0 || materialIndex >= (int)doc_.materials.size())
        {
            materialSG_[materialIndex] = "initialShadingGroup";
            return "initialShadingGroup";
        }
        const gltf::Material& mat = doc_.materials[materialIndex];

        std::ostringstream fallback;
        fallback << "material_" << materialIndex;
        const std::string matName = SanitizeName(mat.name, fallback.str());

#if MAYA_API_VERSION >= 20200000
        const char* shaderType = "standardSurface";
#else
        const char* shaderType = "lambert";
#endif

        MStatus status;
        MString shader;
        {
            MString cmd = MString("shadingNode -asShader -name \"") + matName.c_str() + "\" " + shaderType;
            status = MGlobal::executeCommand(cmd, shader);
            if (!status || shader.length() == 0)
            {
                materialSG_[materialIndex] = "initialShadingGroup";
                return "initialShadingGroup";
            }
        }
        MString sg;
        {
            MString cmd = MString("sets -renderable true -noSurfaceShader true -empty -name \"") +
                          matName.c_str() + "SG\"";
            MGlobal::executeCommand(cmd, sg);
            MString cmd2 = MString("connectAttr -f ") + shader + ".outColor " + sg + ".surfaceShader;";
            MGlobal::executeCommand(cmd2);
        }

#if MAYA_API_VERSION >= 20200000
        // ---- standardSurface ----
        {
            MString cmd;
            cmd += MString("setAttr ") + shader + ".baseColor -type double3 " +
                   FS(mat.baseColorFactor[0]) + " " + FS(mat.baseColorFactor[1]) + " " +
                   FS(mat.baseColorFactor[2]) + ";";
            cmd += MString("setAttr ") + shader + ".metalness " + FS(mat.metallicFactor) + ";";
            cmd += MString("setAttr ") + shader + ".specularRoughness " + FS(mat.roughnessFactor) + ";";
            MGlobal::executeCommand(cmd);
        }

        // base color texture
        MString baseTex = CreateFileTexture(mat.baseColorTexture, false);
        if (baseTex.length() > 0)
        {
            MString cmd;
            cmd += MString("connectAttr -f ") + baseTex + ".outColor " + shader + ".baseColor;";
            cmd += MString("setAttr ") + baseTex + ".colorGain -type double3 " +
                   FS(mat.baseColorFactor[0]) + " " + FS(mat.baseColorFactor[1]) + " " +
                   FS(mat.baseColorFactor[2]) + ";";
            MGlobal::executeCommand(cmd);
        }

        // alpha
        if (mat.alphaMode == "BLEND" || mat.alphaMode == "MASK")
        {
            if (baseTex.length() > 0)
            {
                MString cmd;
                cmd += MString("connectAttr -f ") + baseTex + ".outAlpha " + shader + ".opacityR;";
                cmd += MString("connectAttr -f ") + baseTex + ".outAlpha " + shader + ".opacityG;";
                cmd += MString("connectAttr -f ") + baseTex + ".outAlpha " + shader + ".opacityB;";
                MGlobal::executeCommand(cmd);
            }
            else
            {
                const MString a = FS(mat.baseColorFactor[3]);
                MString cmd = MString("setAttr ") + shader + ".opacity -type double3 " +
                              a + " " + a + " " + a + ";";
                MGlobal::executeCommand(cmd);
            }
        }

        // metallic / roughness texture (B = metallic, G = roughness)
        MString mrTex = CreateFileTexture(mat.metallicRoughnessTexture, true);
        if (mrTex.length() > 0)
        {
            MString cmd;
            cmd += MString("connectAttr -f ") + mrTex + ".outColorB " + shader + ".metalness;";
            cmd += MString("connectAttr -f ") + mrTex + ".outColorG " + shader + ".specularRoughness;";
            cmd += MString("setAttr ") + mrTex + ".colorGain -type double3 1 " +
                   FS(mat.roughnessFactor) + " " + FS(mat.metallicFactor) + ";";
            cmd += MString("setAttr ") + mrTex + ".alphaIsLuminance 0;";
            MGlobal::executeCommand(cmd);
        }

        // normal map
        MString normalTex = CreateFileTexture(mat.normalTexture, true);
        if (normalTex.length() > 0)
        {
            MString bump;
            MGlobal::executeCommand("shadingNode -asUtility bump2d", bump);
            if (bump.length() > 0)
            {
                MString cmd;
                cmd += MString("setAttr ") + bump + ".bumpInterp 1;"; // tangent space normals
                cmd += MString("setAttr ") + bump + ".bumpDepth " + FS(mat.normalTexture.scale) + ";";
                cmd += MString("setAttr ") + normalTex + ".alphaIsLuminance 0;";
                cmd += MString("connectAttr -f ") + normalTex + ".outAlpha " + bump + ".bumpValue;";
                cmd += MString("connectAttr -f ") + bump + ".outNormal " + shader + ".normalCamera;";
                MGlobal::executeCommand(cmd);
            }
        }

        // emissive
        const bool hasEmissiveFactor =
            (mat.emissiveFactor[0] > 0.0f || mat.emissiveFactor[1] > 0.0f || mat.emissiveFactor[2] > 0.0f);
        MString emissiveTex = CreateFileTexture(mat.emissiveTexture, false);
        if (emissiveTex.length() > 0 || hasEmissiveFactor)
        {
            MString cmd;
            cmd += MString("setAttr ") + shader + ".emission 1;";
            cmd += MString("setAttr ") + shader + ".emissionColor -type double3 " +
                   FS(mat.emissiveFactor[0]) + " " + FS(mat.emissiveFactor[1]) + " " +
                   FS(mat.emissiveFactor[2]) + ";";
            MGlobal::executeCommand(cmd);
            if (emissiveTex.length() > 0)
            {
                MString cmd2;
                cmd2 += MString("connectAttr -f ") + emissiveTex + ".outColor " + shader + ".emissionColor;";
                if (hasEmissiveFactor)
                {
                    cmd2 += MString("setAttr ") + emissiveTex + ".colorGain -type double3 " +
                            FS(mat.emissiveFactor[0]) + " " + FS(mat.emissiveFactor[1]) + " " +
                            FS(mat.emissiveFactor[2]) + ";";
                }
                MGlobal::executeCommand(cmd2);
            }
        }
#else
        // ---- lambert fallback (Maya < 2020) ----
        {
            MString cmd;
            cmd += MString("setAttr ") + shader + ".color -type double3 " +
                   FS(mat.baseColorFactor[0]) + " " + FS(mat.baseColorFactor[1]) + " " +
                   FS(mat.baseColorFactor[2]) + ";";
            MGlobal::executeCommand(cmd);
        }
        MString baseTex = CreateFileTexture(mat.baseColorTexture, false);
        if (baseTex.length() > 0)
        {
            MString cmd = MString("connectAttr -f ") + baseTex + ".outColor " + shader + ".color;";
            MGlobal::executeCommand(cmd);
        }
#endif

        materialSG_[materialIndex] = sg;
        return sg;
    }

    //---------------------------------------------------------------------
    // animation
    //---------------------------------------------------------------------

    // Extract per-key values; for CUBICSPLINE take the value element out of the
    // (inTangent, value, outTangent) triples.
    static void ExtractSamplerValues(const std::vector<float>& raw, size_t keyCount,
                                     size_t elementSize, bool cubicSpline,
                                     std::vector<float>& out)
    {
        out.clear();
        if (cubicSpline)
        {
            out.reserve(keyCount * elementSize);
            for (size_t k = 0; k < keyCount; k++)
            {
                const size_t base = k * elementSize * 3 + elementSize; // skip inTangent block
                for (size_t c = 0; c < elementSize; c++)
                {
                    if (base + c < raw.size())
                    {
                        out.push_back(raw[base + c]);
                    }
                    else
                    {
                        out.push_back(0.0f);
                    }
                }
            }
        }
        else
        {
            out.assign(raw.begin(), raw.end());
        }
    }

    void SceneBuilder::ImportChannel(const gltf::Animation& anim, const gltf::AnimationChannel& channel)
    {
        if (channel.sampler < 0 || channel.sampler >= (int)anim.samplers.size())
        {
            return;
        }
        if (channel.targetNode < 0 || channel.targetNode >= (int)nodeObjects_.size() ||
            nodeObjects_[channel.targetNode].isNull())
        {
            return;
        }
        const gltf::AnimationSampler& sampler = anim.samplers[channel.sampler];

        std::vector<float> times;
        if (!doc_.GetFloats(sampler.input, times) || times.empty())
        {
            return;
        }
        std::vector<float> rawValues;
        int comps = 0;
        if (!doc_.GetFloats(sampler.output, rawValues, &comps) || comps == 0)
        {
            return;
        }
        const bool cubicSpline = (sampler.interpolation == "CUBICSPLINE");

        MFnAnimCurve::TangentType tangentIn = MFnAnimCurve::kTangentLinear;
        MFnAnimCurve::TangentType tangentOut = MFnAnimCurve::kTangentLinear;
        if (sampler.interpolation == "STEP")
        {
            tangentOut = MFnAnimCurve::kTangentStep;
        }
        else if (cubicSpline)
        {
            tangentIn = MFnAnimCurve::kTangentSmooth;
            tangentOut = MFnAnimCurve::kTangentSmooth;
        }

        const size_t keyCount = times.size();
        MFnDependencyNode fnNode(nodeObjects_[channel.targetNode]);

        // track the animation range
        for (size_t k = 0; k < keyCount; k++)
        {
            MTime t(times[k], MTime::kSeconds);
            if (!hasAnimKeys_)
            {
                animMinTime_ = t;
                animMaxTime_ = t;
                hasAnimKeys_ = true;
            }
            else
            {
                if (t < animMinTime_) animMinTime_ = t;
                if (t > animMaxTime_) animMaxTime_ = t;
            }
        }

        if (channel.targetPath == "translation" || channel.targetPath == "scale")
        {
            const bool isTranslation = (channel.targetPath == "translation");
            std::vector<float> values;
            ExtractSamplerValues(rawValues, keyCount, 3, cubicSpline, values);
            if (values.size() < keyCount * 3)
            {
                return;
            }
            const char* attrNames[3] = {isTranslation ? "translateX" : "scaleX",
                                        isTranslation ? "translateY" : "scaleY",
                                        isTranslation ? "translateZ" : "scaleZ"};
            const double valueScale = isTranslation ? distScale_ : 1.0;
            for (int axis = 0; axis < 3; axis++)
            {
                MStatus status;
                MPlug plug = fnNode.findPlug(attrNames[axis], false, &status);
                if (!status)
                {
                    continue;
                }
                MFnAnimCurve curve;
                curve.create(plug, MFnAnimCurve::kAnimCurveUnknown, 0, &status);
                if (!status)
                {
                    continue;
                }
                for (size_t k = 0; k < keyCount; k++)
                {
                    curve.addKey(MTime(times[k], MTime::kSeconds),
                                 (double)values[k * 3 + axis] * valueScale,
                                 tangentIn, tangentOut);
                }
            }
        }
        else if (channel.targetPath == "rotation")
        {
            std::vector<float> values;
            ExtractSamplerValues(rawValues, keyCount, 4, cubicSpline, values);
            if (values.size() < keyCount * 4)
            {
                return;
            }
            // quaternion -> continuous euler
            std::vector<MEulerRotation> eulers(keyCount);
            MQuaternion prevQ;
            for (size_t k = 0; k < keyCount; k++)
            {
                MQuaternion q(values[k * 4 + 0], values[k * 4 + 1],
                              values[k * 4 + 2], values[k * 4 + 3]);
                q.normalizeIt();
                if (k > 0)
                {
                    // hemisphere correction
                    const double dot = q.x * prevQ.x + q.y * prevQ.y + q.z * prevQ.z + q.w * prevQ.w;
                    if (dot < 0.0)
                    {
                        q = MQuaternion(-q.x, -q.y, -q.z, -q.w);
                    }
                }
                prevQ = q;
                MEulerRotation e = q.asEulerRotation();
                if (k > 0)
                {
                    // unroll each axis to stay close to the previous key
                    const MEulerRotation& pe = eulers[k - 1];
                    const double twoPi = 2.0 * 3.14159265358979323846;
                    double* cur[3] = {&e.x, &e.y, &e.z};
                    const double prev[3] = {pe.x, pe.y, pe.z};
                    for (int a = 0; a < 3; a++)
                    {
                        while (*cur[a] - prev[a] > 3.14159265358979323846)
                        {
                            *cur[a] -= twoPi;
                        }
                        while (prev[a] - *cur[a] > 3.14159265358979323846)
                        {
                            *cur[a] += twoPi;
                        }
                    }
                }
                eulers[k] = e;
            }
            const char* attrNames[3] = {"rotateX", "rotateY", "rotateZ"};
            for (int axis = 0; axis < 3; axis++)
            {
                MStatus status;
                MPlug plug = fnNode.findPlug(attrNames[axis], false, &status);
                if (!status)
                {
                    continue;
                }
                MFnAnimCurve curve;
                curve.create(plug, MFnAnimCurve::kAnimCurveUnknown, 0, &status);
                if (!status)
                {
                    continue;
                }
                for (size_t k = 0; k < keyCount; k++)
                {
                    const double value = (axis == 0) ? eulers[k].x : (axis == 1) ? eulers[k].y : eulers[k].z;
                    curve.addKey(MTime(times[k], MTime::kSeconds), value, tangentIn, tangentOut);
                }
            }
        }
        else if (channel.targetPath == "weights")
        {
            std::map<int, MObject>::const_iterator found = nodeBlendShape_.find(channel.targetNode);
            if (found == nodeBlendShape_.end())
            {
                return;
            }
            const int numTargets = nodeTargetCount_[channel.targetNode];
            if (numTargets <= 0)
            {
                return;
            }
            std::vector<float> values;
            ExtractSamplerValues(rawValues, keyCount, (size_t)numTargets, cubicSpline, values);
            if (values.size() < keyCount * (size_t)numTargets)
            {
                return;
            }
            MFnDependencyNode fnBS(found->second);
            MStatus status;
            MPlug weightArray = fnBS.findPlug("weight", false, &status);
            if (!status)
            {
                return;
            }
            for (int t = 0; t < numTargets; t++)
            {
                MPlug plug = weightArray.elementByLogicalIndex((unsigned int)t, &status);
                if (!status)
                {
                    continue;
                }
                MFnAnimCurve curve;
                curve.create(plug, MFnAnimCurve::kAnimCurveUnknown, 0, &status);
                if (!status)
                {
                    continue;
                }
                for (size_t k = 0; k < keyCount; k++)
                {
                    curve.addKey(MTime(times[k], MTime::kSeconds),
                                 (double)values[k * numTargets + t],
                                 tangentIn, tangentOut);
                }
            }
        }
    }

    MStatus SceneBuilder::ImportAnimations()
    {
        for (size_t a = 0; a < doc_.animations.size(); a++)
        {
            const gltf::Animation& anim = doc_.animations[a];
            for (size_t c = 0; c < anim.channels.size(); c++)
            {
                ImportChannel(anim, anim.channels[c]);
            }
        }
        if (doc_.animations.size() > 1)
        {
            MGlobal::displayWarning(
                "glTFImporter: multiple animations were imported onto the same timeline; "
                "channels targeted by more than one animation keep only the last one.");
        }
        if (hasAnimKeys_)
        {
            MTime minT = animMinTime_;
            if (MTime(0.0, MTime::kSeconds) < minT)
            {
                minT = MTime(0.0, MTime::kSeconds);
            }
            MAnimControl::setAnimationStartEndTime(minT, animMaxTime_);
            MAnimControl::setMinMaxTime(minT, animMaxTime_);
        }
        return MS::kSuccess;
    }

} // anonymous namespace

//=====================================================================
// glTFImporter (MPxFileTranslator)
//=====================================================================

glTFImporter::glTFImporter()
{
}

glTFImporter::~glTFImporter()
{
}

void* glTFImporter::creator()
{
    return new glTFImporter();
}

MStatus glTFImporter::reader(const MFileObject& file,
                             const MString& optionsString,
                             FileAccessMode mode)
{
    (void)mode;

    const MString fileName = file.fullName();

    gltf::Document doc;
    std::string errorMessage;
    if (!doc.Load(fileName.asChar(), &errorMessage))
    {
        MGlobal::displayError(MString("glTFImporter: ") + errorMessage.c_str());
        return MS::kFailure;
    }

    ImportOptions opts;
    ParseOptions(optionsString, opts);

    SceneBuilder builder(doc, opts);
    return builder.Build();
}

bool glTFImporter::haveReadMethod() const
{
    return true;
}

bool glTFImporter::haveWriteMethod() const
{
    return false;
}

bool glTFImporter::canBeOpened() const
{
    return true;
}

MString glTFImporter::defaultExtension() const
{
    return "gltf";
}

MPxFileTranslator::MFileKind glTFImporter::identifyFile(const MFileObject& fileName,
                                                        const char* buffer,
                                                        short size) const
{
    // GLB magic
    if (size >= 4 && buffer[0] == 'g' && buffer[1] == 'l' && buffer[2] == 'T' && buffer[3] == 'F')
    {
        return kIsMyFileType;
    }
    // extension check
    const MString name = fileName.resolvedName();
    const int len = (int)name.numChars();
    if (len > 5)
    {
        MString ext = name.substringW(len - 5, len - 1);
        ext.toLowerCase();
        if (ext == ".gltf")
        {
            return kIsMyFileType;
        }
    }
    if (len > 4)
    {
        MString ext = name.substringW(len - 4, len - 1);
        ext.toLowerCase();
        if (ext == ".glb")
        {
            return kIsMyFileType;
        }
    }
    return kNotMyFileType;
}

MString glTFImporter::filter() const
{
    return "*.gltf;*.glb";
}
