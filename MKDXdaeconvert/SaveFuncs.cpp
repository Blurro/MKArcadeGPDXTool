#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <iostream>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <bitset>
#include <assimp/mesh.h>
#include <assimp/Importer.hpp>
#include <assimp/anim.h>
#include <assimp/postprocess.h>
#include <vector>
#include <regex>
#include <assimp/DefaultLogger.hpp>
#include <Windows.h>

#include "SaveFuncs.h"

aiNode* BuildAiNode(uint32_t index, const std::vector<NodeNames>& allNodeNames,
    const std::vector<FullNodeData>& fullNodeDataList,
    std::unordered_map<uint32_t, aiNode*>& nodeMap)
{
    aiNode* node = new aiNode();
    node->mName = allNodeNames[index].Name;
    nodeMap[index] = node; // <-- this line right here is the missing piece

    const auto& nodeData = fullNodeDataList[index];
    node->mNumChildren = static_cast<unsigned int>(nodeData.childrenIndexList.size());
    node->mChildren = new aiNode * [node->mNumChildren];

    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        uint32_t childIdx = nodeData.childrenIndexList[i];
        node->mChildren[i] = BuildAiNode(childIdx, allNodeNames, fullNodeDataList, nodeMap);
        node->mChildren[i]->mParent = node;
    }

    return node;
}

void EraseWholeLineContaining(std::string& str, const std::string& target, size_t startPos = 0) {
    size_t tagPos = str.find(target, startPos);
    if (tagPos == std::string::npos) return;

    size_t lineStart = str.rfind('\n', tagPos);
    size_t lineEnd = str.find('\n', tagPos);

    if (lineStart == std::string::npos) lineStart = 0;
    else lineStart++; // move to start of the line after \n

    if (lineEnd != std::string::npos) lineEnd++; // include the newline
    else lineEnd = str.size(); // in case it's the last line

    str.erase(lineStart, lineEnd - lineStart);
}

void SerializeAllMatToIndices(const std::string& path, const std::vector<std::vector<std::pair<unsigned int, std::vector<unsigned int>>>>& allMaterialToIndices) {
    std::ofstream file(path);
    if (!file.is_open()) return;

    for (const auto& mesh : allMaterialToIndices) {
        file << "mesh\n";
        for (const auto& mat : mesh) {
            file << mat.first << ":";
            for (auto idx : mat.second)
                file << idx << ",";
            file << "\n";
        }
        file << "endmesh\n";
    }
    file.close();
}

typedef void(__cdecl* PatchDaeFileFunc)(const char*, const char*);
void CallPatchDaeFileDLL(const std::string& outFile, const std::vector<std::vector<std::pair<unsigned int, std::vector<unsigned int>>>>& allMaterialToIndices) {
    std::string tempPath = "allmatinfo.txt";
    SerializeAllMatToIndices(tempPath, allMaterialToIndices);

    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "Failed to load tinyxml2patcher.dll" << std::endl;
        return;
    }
    PatchDaeFileFunc patchFunc = (PatchDaeFileFunc)GetProcAddress(dll, "PatchDaeFile_C");
    if (!patchFunc) {
        std::cerr << "Failed to find PatchDaeFile_C in DLL" << std::endl;
        FreeLibrary(dll);
        return;
    }
    patchFunc(outFile.c_str(), tempPath.c_str());
    std::remove(tempPath.c_str());
    FreeLibrary(dll);
}

typedef void(__cdecl* GetNormalsFunc)(const char*);
void CallGetNormalsFromDLL(const std::string& daePath) {
    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "couldn't load tinyxml2patcher.dll\n";
        return;
    }

    GetNormalsFunc func = (GetNormalsFunc)GetProcAddress(dll, "GetDaeNormals_C");
    if (!func) {
        std::cerr << "couldn't find GetDaeNormals_C\n";
        FreeLibrary(dll);
        return;
    }

    func(daePath.c_str());
    FreeLibrary(dll);
}

void RenameNode(uint32_t root, uint32_t current, std::vector<NodeNames>& allNodeNames, const std::vector<FullNodeData>& fullNodeDataList)
{
    auto& rootName = allNodeNames[root].Name;
    std::string prefix = rootName + ".";

    // if current name starts with prefix, strip it
    auto& currentName = allNodeNames[current].Name;
    if (currentName.find(prefix) == 0) {
        currentName = currentName.substr(prefix.size());
    }

    // recurse into children of current
    for (auto childIdx : fullNodeDataList[current].childrenIndexList) {
        RenameNode(root, childIdx, allNodeNames, fullNodeDataList);
    }
}

aiMatrix4x4 GetOffsetMatrix(aiNode* boneNode, aiNode* meshNode) {
    aiMatrix4x4 boneTransform = boneNode->mTransformation;
    for (aiNode* p = boneNode->mParent; p; p = p->mParent)
        boneTransform = p->mTransformation * boneTransform;

    aiMatrix4x4 meshTransform = meshNode->mTransformation;
    for (aiNode* p = meshNode->mParent; p; p = p->mParent)
        meshTransform = p->mTransformation * meshTransform;

    return boneTransform.Inverse() * meshTransform;
}

// stuff for writing preset file for imports

bool floatsEqual(float a, float b, float epsilon = 1e-5f) {
    return std::abs(a - b) < epsilon;
}

bool vecEquals(const std::vector<float>& v, const float* def, size_t count, float epsilon = 1e-5f) {
    if (v.size() < count) return false;
    for (size_t i = 0; i < count; ++i) {
        if (!floatsEqual(v[i], def[i], epsilon)) return false;
    }
    return true;
}

int WritePresetFile(const std::string& path, const std::vector<Material>& materialsData,
    const std::vector<TextureName>& textureNames, const std::vector<NodeNames>& allNodeNames,
    const std::vector<FullNodeData>& fullNodeDataList)
{
    std::ofstream out(path);
    if (!out) {
        std::cerr << "failed to open " << path << " for writing\n";
        return 1;
    }

    MaterialPreset def;

    for (size_t i = 0; i < materialsData.size(); ++i) {
        const Material& m = materialsData[i];
        MaterialPreset def;

        bool printedHeader = false;

        auto printHeader = [&]() {
            if (!printedHeader) {
                if (i != 0) out << "\n";
                out << "#Material\n";
                printedHeader = true;
            }
            };

        bool diffDiffuse = false;
        for (int j = 0; j < 4; ++j)
            if (!floatsEqual(m.Diffuse[j], def.diffuse[j])) diffDiffuse = true;

        bool diffSpecular = false;
        for (int j = 0; j < 4; ++j)
            if (!floatsEqual(m.Specular[j], def.specular[j])) diffSpecular = true;

        bool diffAmbience = false;
        for (int j = 0; j < 4; ++j)
            if (!floatsEqual(m.Ambience[j], def.ambience[j])) diffAmbience = true;

        bool diffShiny = !floatsEqual(m.Shiny, def.shiny);

        bool diffUnknown0 = (!m.Unknowns.empty() && m.Unknowns[0] != def.unknownVal);

        bool diffUnknown2 = (m.Unknowns2.size() > 3) && !floatsEqual(m.Unknowns2[3], def.unknownVal2);

        bool diffTexAlbedo = (m.TextureIndices.size() > 0 && m.TextureIndices[0] != def.texAlbedo);
        bool diffTexSpecular = (m.TextureIndices.size() > 1 && m.TextureIndices[1] != def.texSpecular);
        bool diffTexReflective = (m.TextureIndices.size() > 2 && m.TextureIndices[2] != def.texReflective);
        bool diffTexEnvironment = (m.TextureIndices.size() > 3 && m.TextureIndices[3] != def.texEnvironment);
        bool diffTexNormal = (m.TextureIndices.size() > 4 && m.TextureIndices[4] != def.texNormal);

        if (diffDiffuse) {
            printHeader();
            out << "#DIFFUSE " << m.Diffuse[0] << " " << m.Diffuse[1] << " " << m.Diffuse[2] << " " << m.Diffuse[3] << "\n";
        }
        if (diffSpecular) {
            printHeader();
            out << "#SPECULAR " << m.Specular[0] << " " << m.Specular[1] << " " << m.Specular[2] << " " << m.Specular[3] << "\n";
        }
        if (diffAmbience) {
            printHeader();
            out << "#AMBIENCE " << m.Ambience[0] << " " << m.Ambience[1] << " " << m.Ambience[2] << " " << m.Ambience[3] << "\n";
        }
        if (diffShiny) {
            printHeader();
            out << "#SHINY " << m.Shiny << "\n";
        }
        if (diffTexAlbedo && m.TextureIndices[0] != -1) {
            printHeader();
            out << "#TEXALBEDO " << m.TextureIndices[0] << "\n";
        }
        if (diffTexSpecular && m.TextureIndices[1] != -1) {
            printHeader();
            out << "#TEXSPECULAR " << m.TextureIndices[1] << "\n";
        }
        if (diffTexReflective && m.TextureIndices[2] != -1) {
            printHeader();
            out << "#TEXREFLECTIVE " << m.TextureIndices[2] << "\n";
        }
        if (diffTexEnvironment && m.TextureIndices[3] != -1) {
            printHeader();
            out << "#TEXENVIRONMENT " << m.TextureIndices[3] << "\n";
        }
        if (diffTexNormal && m.TextureIndices[4] != -1) {
            printHeader();
            out << "#TEXNORMAL " << m.TextureIndices[4] << "\n";
        }
        if (diffUnknown0) {
            printHeader();
            out << "#UNKNOWN " << m.Unknowns[0] << "\n";
        }
        if (diffUnknown2) {
            printHeader();
            out << "#UNKNOWN2 " << m.Unknowns2[3] << "\n";
        }
    }

    bool wroteAnimFloats = false;
    for (size_t i = 0; i < fullNodeDataList.size(); ++i) {
        const BoneData& b = fullNodeDataList[i].boneData;
        bool hasAnimData = false;
        for (int j = 0; j < 6; ++j) {
            if (b.AnimationVals[j] != 0.0f) {
                hasAnimData = true;
                break;
            }
        }
        if (hasAnimData) {
            if (!wroteAnimFloats) {
                out << "\n";
                wroteAnimFloats = true;
            }
            out << "#AnimFloats " << allNodeNames[i].Name << " "
                << b.AnimationVals[0] << " " << b.AnimationVals[1] << " " << b.AnimationVals[2] << " "
                << b.AnimationVals[3] << " " << b.AnimationVals[4] << " " << b.AnimationVals[5] << "\n";
        }
    }

    if (!textureNames.empty()) {
        out << "\n#Textures\n";
        for (size_t i = 0; i < textureNames.size(); ++i) {
            out << textureNames[i].Name << "\n";
        }
    }

    if (!allNodeNames.empty() && allNodeNames.size() == fullNodeDataList.size()) {
        out << "\n#Meshes\n";
        for (size_t i = 0; i < fullNodeDataList.size(); ++i) {
            if (!fullNodeDataList[i].subMeshes.empty()) {
                out << allNodeNames[i].Name << "\n";
            }
        }
    }

    std::cout << std::endl << "Written material/extra data preset to " << path << "\nUse this for importing!" << std::endl;

    return 0;
}

std::string MakeOutFilePath(const std::string& path, const std::string& outDir)
{
    size_t slashPos = path.find_last_of("/\\");
    std::string filename = (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);

    std::string dir = outDir;
    if (!dir.empty() && dir.back() != '\\')
        dir += "\\";

    std::string fullPath = dir + filename;

    // convert all forward slashes to backslashes
    for (size_t i = 0; i < fullPath.size(); ++i)
    {
        if (fullPath[i] == '/')
            fullPath[i] = '\\';
    }

    return fullPath;
}

void SaveDaeFile(const std::string& path, const std::string& outDir, Header& headerData, std::vector<Material>& materialsData, std::vector<TextureName>& textureNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList, const bool mergeSubmeshes)
{
    // rename children of root to remove the root name prefix
    for (auto root : rootNodes) {
        // removed because not all files follow the same pattern, and not an issue in blender to have periods in names
        //RenameNode(root, root, allNodeNames, fullNodeDataList);
    }
    aiScene* scene = new aiScene();
    scene->mRootNode = new aiNode();
    scene->mRootNode->mName = "Scene";

    // create armature node
    aiNode* armatureNode = new aiNode();
    armatureNode->mName = "Armature";

    // attach armature node as child of Scene
    scene->mRootNode->mNumChildren = 1;
    scene->mRootNode->mChildren = new aiNode * [1] { armatureNode };
    armatureNode->mParent = scene->mRootNode;

    // build all skeleton nodes as children of armatureNode

    std::unordered_map<uint32_t, aiNode*> nodeMap;
    size_t numSkeletonRoots = rootNodes.size();
    armatureNode->mNumChildren = static_cast<unsigned int>(numSkeletonRoots);
    armatureNode->mChildren = new aiNode * [numSkeletonRoots]();

    for (size_t i = 0; i < numSkeletonRoots; ++i) {
        uint32_t rootIdx = rootNodes[i];
        aiNode* node = BuildAiNode(rootIdx, allNodeNames, fullNodeDataList, nodeMap);
        armatureNode->mChildren[i] = node;
        node->mParent = armatureNode;
    }

    // create materials
    scene->mNumMaterials = headerData.MaterialCount;
    scene->mMaterials = new aiMaterial * [headerData.MaterialCount];
    for (size_t i = 0; i < headerData.MaterialCount; i++) {
        scene->mMaterials[i] = new aiMaterial();
        if (materialsData[i].TextureIndices[0] >= 0 && materialsData[i].TextureIndices[0] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[0]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_DIFFUSE(0));
        }
        if (materialsData[i].TextureIndices[1] >= 0 && materialsData[i].TextureIndices[1] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[1]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_SPECULAR(0));
        }
        if (materialsData[i].TextureIndices[2] >= 0 && materialsData[i].TextureIndices[2] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[2]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_REFLECTION(0));
        }
        if (materialsData[i].TextureIndices[3] >= 0 && materialsData[i].TextureIndices[3] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[3]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_EMISSIVE(0)); // unsure but dont think it matters
        }
        if (materialsData[i].TextureIndices[4] >= 0 && materialsData[i].TextureIndices[4] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[4]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_NORMALS(0));
        }
        if (materialsData[i].TextureIndices[5] >= 0 && materialsData[i].TextureIndices[5] < (int)textureNames.size()) {
            aiString texPath(textureNames[materialsData[i].TextureIndices[5]].Name.c_str());
            scene->mMaterials[i]->AddProperty(&texPath, AI_MATKEY_TEXTURE_AMBIENT(0)); // unsure
        }
        float ambientColor[3] = { 0.5f, 0.5f, 0.5f };
        scene->mMaterials[i]->AddProperty(ambientColor, 3, AI_MATKEY_COLOR_AMBIENT);
    }

    // used for post processing dae patching to fix materials on a single mesh, assimp can only export 1 mat per mesh
    std::vector<std::vector<std::pair<unsigned int, std::vector<unsigned int>>>> allMaterialToIndices;

    // big loop that merges submeshes
    for (size_t nodeIndex = 0; nodeIndex < fullNodeDataList.size(); nodeIndex++) {
        const auto& nodeData = fullNodeDataList[nodeIndex];
        // set up transforms
        aiNode* node = nodeMap[static_cast<unsigned int>(nodeIndex)];
        // create scale matrix
        aiMatrix4x4 scaleMat;
        aiVector3D scaleVec(nodeData.boneData.Scale[0], nodeData.boneData.Scale[1], nodeData.boneData.Scale[2]);
        aiMatrix4x4::Scaling(scaleVec, scaleMat);

        // create rotation matrix from euler angles (in radians)
        aiMatrix4x4 rotX, rotY, rotZ;
        aiMatrix4x4::RotationX(nodeData.boneData.Rotation[0], rotX);
        aiMatrix4x4::RotationY(nodeData.boneData.Rotation[1], rotY);
        aiMatrix4x4::RotationZ(nodeData.boneData.Rotation[2], rotZ);
        aiMatrix4x4 rotMat = rotZ * rotY * rotX;

        // create translation matrix
        aiMatrix4x4 transMat;
        aiVector3D transVec(nodeData.boneData.Translation[0], nodeData.boneData.Translation[1], nodeData.boneData.Translation[2]);
        aiMatrix4x4::Translation(transVec, transMat);

        // combine all: local = translation * rotation * scale
        node->mTransformation = transMat * rotMat * scaleMat;

        // skip next part if not mesh
        if (nodeData.subMeshes.empty()) continue;
        aiNode* parentNode = nodeMap[static_cast<unsigned int>(nodeIndex)];

        if (mergeSubmeshes)
        {
            std::vector<aiVector3D> mergedVerts;
            std::vector<aiVector3D> mergedNorms;
            std::vector<aiColor4D> mergedColors;
            std::vector<aiVector3D> mergedUVs[4];
            std::vector<aiFace> mergedFaces;

            unsigned int vertexOffset = 0;

            // used for post processing dae patching to fix materials on a single mesh (this var is per mesh, and is to be added to the 'all' var containing data for all meshes)
            std::vector<std::pair<unsigned int, std::vector<unsigned int>>> materialToIndicesOrdered;
            std::unordered_map<unsigned int, size_t> matIndexToOrder;

            for (size_t s = 0; s < nodeData.subMeshes.size(); s++) {
                std::vector<float> vertsFlat = nodeData.verticesList[s];
                std::vector<aiVector3D> verts;
                for (size_t i = 0; i + 2 < vertsFlat.size(); i += 3)
                    verts.emplace_back(vertsFlat[i], vertsFlat[i + 1], vertsFlat[i + 2]);

                std::vector<aiVector3D> norms;
                if (nodeData.normalsList.size() > s) {
                    const auto& normsFlat = nodeData.normalsList[s];
                    for (size_t i = 0; i + 2 < normsFlat.size(); i += 3)
                        norms.emplace_back(normsFlat[i], normsFlat[i + 1], normsFlat[i + 2]);
                }
                else {
                    norms = verts;
                }

                std::vector<aiColor4D> colors;
                if (nodeData.colorsList.size() > s) {
                    const auto& colorsFlat = nodeData.colorsList[s];
                    for (size_t i = 0; i + 3 < colorsFlat.size(); i += 4)
                        colors.emplace_back(colorsFlat[i], colorsFlat[i + 1], colorsFlat[i + 2], colorsFlat[i + 3]);
                }
                else {
                    colors.resize(verts.size(), aiColor4D(1.0f, 1.0f, 1.0f, 1.0f)); // default to white
                }

                std::vector<aiVector3D> uvs[4];
                const std::vector<float>* uvsLists[4] = {
                    (nodeData.uvs0List.size() > s) ? &nodeData.uvs0List[s] : nullptr,
                    (nodeData.uvs1List.size() > s) ? &nodeData.uvs1List[s] : nullptr,
                    (nodeData.uvs2List.size() > s) ? &nodeData.uvs2List[s] : nullptr,
                    (nodeData.uvs3List.size() > s) ? &nodeData.uvs3List[s] : nullptr
                };
                for (int uvIdx = 0; uvIdx < 4; uvIdx++) {
                    if (uvsLists[uvIdx]) {
                        const auto& uvsFlat = *uvsLists[uvIdx];
                        for (size_t i = 0; i + 1 < uvsFlat.size(); i += 2)
                            uvs[uvIdx].emplace_back(uvsFlat[i], uvsFlat[i + 1], 0.0f);
                    }
                }

                std::vector<uint16_t> rawIndices = (nodeData.polygonsList.size() > s) ? nodeData.polygonsList[s] : std::vector<uint16_t>{};

                // add verts, norms, uvs
                mergedVerts.insert(mergedVerts.end(), verts.begin(), verts.end());

                // remap indices for this submesh
                std::vector<unsigned int> remappedIndices;
                remappedIndices.reserve(verts.size());
                for (size_t i = 0; i < verts.size(); ++i)
                    remappedIndices.push_back(vertexOffset + static_cast<unsigned int>(i));

                mergedNorms.insert(mergedNorms.end(), norms.begin(), norms.end());
                mergedColors.insert(mergedColors.end(), colors.begin(), colors.end());
                for (int uvIdx = 0; uvIdx < 4; uvIdx++)
                    mergedUVs[uvIdx].insert(mergedUVs[uvIdx].end(), uvs[uvIdx].begin(), uvs[uvIdx].end());

                // adjust indices and add faces
                for (size_t i = 0; i + 2 < rawIndices.size(); i += 3) {
                    aiFace face;
                    face.mNumIndices = 3;
                    face.mIndices = new unsigned int[3] {
                        static_cast<unsigned int>(rawIndices[i]) + vertexOffset,
                            static_cast<unsigned int>(rawIndices[i + 1]) + vertexOffset,
                            static_cast<unsigned int>(rawIndices[i + 2]) + vertexOffset
                        };
                    mergedFaces.push_back(face);

                    // store in materialToIndices map for later dae post processing
                    unsigned int matIndex = nodeData.subMeshes[s].MaterialIndex;
                    if (!matIndexToOrder.count(matIndex)) {
                        matIndexToOrder[matIndex] = materialToIndicesOrdered.size();
                        materialToIndicesOrdered.emplace_back(matIndex, std::vector<unsigned int>());
                    }
                    auto& indicesVec = materialToIndicesOrdered[matIndexToOrder[matIndex]].second;
                    indicesVec.push_back(face.mIndices[0]);
                    indicesVec.push_back(face.mIndices[1]);
                    indicesVec.push_back(face.mIndices[2]);
                }

                vertexOffset += static_cast<unsigned int>(verts.size());
            }
            // store material to indices mapping for this mesh
            allMaterialToIndices.push_back(std::move(materialToIndicesOrdered));

            // create merged mesh
            auto* mergedMesh = new aiMesh();
            mergedMesh->mName = aiString(std::string(parentNode->mName.C_Str())); // keep original node name
            mergedMesh->mNumVertices = static_cast<unsigned int>(mergedVerts.size());
            mergedMesh->mVertices = new aiVector3D[mergedVerts.size()];
            std::copy(mergedVerts.begin(), mergedVerts.end(), mergedMesh->mVertices);

            // apply normals
            if (!mergedNorms.empty() && mergedNorms.size() == mergedMesh->mNumVertices) {
                mergedMesh->mNormals = new aiVector3D[mergedNorms.size()];
                std::copy(mergedNorms.begin(), mergedNorms.end(), mergedMesh->mNormals);
            }

            // apply vertex colors
            if (!mergedColors.empty() && mergedColors.size() == mergedMesh->mNumVertices) {
                mergedMesh->mColors[0] = new aiColor4D[mergedColors.size()];
                std::copy(mergedColors.begin(), mergedColors.end(), mergedMesh->mColors[0]);
            }

            // apply uvs
            for (int uvIdx = 0; uvIdx < 4; uvIdx++) {
                if (!mergedUVs[uvIdx].empty()) {
                    mergedMesh->mTextureCoords[uvIdx] = new aiVector3D[mergedUVs[uvIdx].size()];
                    std::copy(mergedUVs[uvIdx].begin(), mergedUVs[uvIdx].end(), mergedMesh->mTextureCoords[uvIdx]);
                    mergedMesh->mNumUVComponents[uvIdx] = 2;
                }
            }

            mergedMesh->mNumFaces = static_cast<unsigned int>(mergedFaces.size());
            mergedMesh->mFaces = new aiFace[mergedFaces.size()];
            std::copy(mergedFaces.begin(), mergedFaces.end(), mergedMesh->mFaces);

            mergedMesh->mMaterialIndex = nodeData.subMeshes[0].MaterialIndex;

            // add merged mesh to scene
            aiMesh** newMeshes = new aiMesh * [scene->mNumMeshes + 1];
            if (scene->mNumMeshes > 0)
                std::copy(scene->mMeshes, scene->mMeshes + scene->mNumMeshes, newMeshes);
            newMeshes[scene->mNumMeshes] = mergedMesh;
            delete[] scene->mMeshes;
            scene->mMeshes = newMeshes;
            unsigned int mergedMeshIndex = scene->mNumMeshes;
            scene->mNumMeshes++;

            // assign merged mesh index to parent node, delete old mesh indices if any
            if (parentNode->mMeshes)
                delete[] parentNode->mMeshes;
            parentNode->mNumMeshes = 1;
            parentNode->mMeshes = new unsigned int[1] { mergedMeshIndex };
        }
        else {
            std::vector<unsigned int> meshIndices;

            for (size_t s = 0; s < nodeData.subMeshes.size(); s++) {
                std::vector<float> vertsFlat = nodeData.verticesList[s];
                std::vector<aiVector3D> verts;
                for (size_t i = 0; i + 2 < vertsFlat.size(); i += 3)
                    verts.emplace_back(vertsFlat[i], vertsFlat[i + 1], vertsFlat[i + 2]);

                std::vector<aiVector3D> norms;
                if (nodeData.normalsList.size() > s) {
                    auto& normsFlat = nodeData.normalsList[s];
                    for (size_t i = 0; i + 2 < normsFlat.size(); i += 3)
                        norms.emplace_back(normsFlat[i], normsFlat[i + 1], normsFlat[i + 2]);
                }
                else {
                    norms = verts;
                }

                std::vector<aiColor4D> colors;
                if (nodeData.colorsList.size() > s) {
                    auto& colorsFlat = nodeData.colorsList[s];
                    for (size_t i = 0; i + 3 < colorsFlat.size(); i += 4)
                        colors.emplace_back(colorsFlat[i], colorsFlat[i + 1], colorsFlat[i + 2], colorsFlat[i + 3]);
                }
                else {
                    colors.resize(verts.size(), aiColor4D(1.0f, 1.0f, 1.0f, 1.0f));
                }

                std::vector<aiVector3D> uvs[4];
                const std::vector<float>* uvsLists[4] = {
                    (nodeData.uvs0List.size() > s) ? &nodeData.uvs0List[s] : nullptr,
                    (nodeData.uvs1List.size() > s) ? &nodeData.uvs1List[s] : nullptr,
                    (nodeData.uvs2List.size() > s) ? &nodeData.uvs2List[s] : nullptr,
                    (nodeData.uvs3List.size() > s) ? &nodeData.uvs3List[s] : nullptr
                };
                for (int uvIdx = 0; uvIdx < 4; uvIdx++) {
                    if (uvsLists[uvIdx]) {
                        auto& uvsFlat = *uvsLists[uvIdx];
                        for (size_t i = 0; i + 1 < uvsFlat.size(); i += 2)
                            uvs[uvIdx].emplace_back(uvsFlat[i], uvsFlat[i + 1], 0.0f);
                    }
                }

                std::vector<uint16_t> rawIndices = (nodeData.polygonsList.size() > s) ? nodeData.polygonsList[s] : std::vector<uint16_t>{};

                auto* mesh = new aiMesh();
                mesh->mName = aiString(std::string(parentNode->mName.C_Str())); // still give it the node name

                mesh->mNumVertices = static_cast<unsigned int>(verts.size());
                mesh->mVertices = new aiVector3D[verts.size()];
                std::copy(verts.begin(), verts.end(), mesh->mVertices);

                if (!norms.empty() && norms.size() == verts.size()) {
                    mesh->mNormals = new aiVector3D[norms.size()];
                    std::copy(norms.begin(), norms.end(), mesh->mNormals);
                }

                if (!colors.empty() && colors.size() == verts.size()) {
                    mesh->mColors[0] = new aiColor4D[colors.size()];
                    std::copy(colors.begin(), colors.end(), mesh->mColors[0]);
                }

                for (int uvIdx = 0; uvIdx < 4; uvIdx++) {
                    if (!uvs[uvIdx].empty()) {
                        mesh->mTextureCoords[uvIdx] = new aiVector3D[uvs[uvIdx].size()];
                        std::copy(uvs[uvIdx].begin(), uvs[uvIdx].end(), mesh->mTextureCoords[uvIdx]);
                        mesh->mNumUVComponents[uvIdx] = 2;
                    }
                }

                mesh->mNumFaces = static_cast<unsigned int>(rawIndices.size() / 3);
                mesh->mFaces = new aiFace[mesh->mNumFaces];
                for (size_t i = 0; i + 2 < rawIndices.size(); i += 3) {
                    aiFace& face = mesh->mFaces[i / 3];
                    face.mNumIndices = 3;
                    face.mIndices = new unsigned int[3] {
                        static_cast<unsigned int>(rawIndices[i]),
                            static_cast<unsigned int>(rawIndices[i + 1]),
                            static_cast<unsigned int>(rawIndices[i + 2])
                        };
                }

                mesh->mMaterialIndex = nodeData.subMeshes[s].MaterialIndex;

                aiMesh** newMeshes = new aiMesh * [scene->mNumMeshes + 1];
                if (scene->mNumMeshes > 0)
                    std::copy(scene->mMeshes, scene->mMeshes + scene->mNumMeshes, newMeshes);
                newMeshes[scene->mNumMeshes] = mesh;
                delete[] scene->mMeshes;
                scene->mMeshes = newMeshes;
                unsigned int newMeshIndex = scene->mNumMeshes;
                scene->mNumMeshes++;

                meshIndices.push_back(newMeshIndex);
            }

            if (parentNode->mMeshes)
                delete[] parentNode->mMeshes;

            parentNode->mNumMeshes = static_cast<unsigned int>(meshIndices.size());
            parentNode->mMeshes = new unsigned int[meshIndices.size()];
            std::copy(meshIndices.begin(), meshIndices.end(), parentNode->mMeshes);
        }
    }

    // create bones with weights on new mesh(es)
    bool anyWeights = false;
    std::vector<std::vector<uint32_t>> bonesToAddPerMesh(scene->mNumMeshes);

    for (size_t nodeIndex = 0; nodeIndex < fullNodeDataList.size(); nodeIndex++) {
        const auto& nodeData = fullNodeDataList[nodeIndex];
        if (nodeData.subMeshes.empty()) continue;
        aiNode* parentNode = nodeMap[static_cast<unsigned int>(nodeIndex)];
        auto linkIt = std::find_if(nodeLinks.begin(), nodeLinks.end(), [&](const NodeLinks& l) { return l.MeshOffset == nodeIndex; });

        size_t meshBase = (parentNode->mMeshes && parentNode->mNumMeshes > 0) ? parentNode->mMeshes[0] : 0;
        size_t vertexOffset = 0;
        size_t subCount = nodeData.subMeshes.size();
        size_t meshLoop = mergeSubmeshes ? 1 : subCount;

        for (size_t m = 0; m < meshLoop; m++) {
            aiMesh* mesh = scene->mMeshes[mergeSubmeshes ? parentNode->mMeshes[0] : meshBase + m];
            std::map<uint32_t, std::vector<aiVertexWeight>> boneWeightsMap;

            for (size_t s = (mergeSubmeshes ? 0 : m); s < (mergeSubmeshes ? subCount : m + 1); ++s) {
                const auto& sub = nodeData.subMeshes[s];
                if (!sub.SkinnedBonesCount) { vertexOffset += sub.VertexCount; continue; }

                uint32_t mask = sub.BonesIndexMask;
                std::vector<uint32_t> filtered;
                for (uint32_t i = 0; i < linkIt->BoneOffsets.size(); ++i)
                    if (mask & (1 << i)) filtered.push_back(linkIt->BoneOffsets[i]);

                size_t vertexCount = sub.VertexCount;
                const auto& weightsFlat = nodeData.weightsList[s];
                for (size_t b = 0; b < filtered.size(); b++) {
                    uint32_t boneNodeIndex = filtered[b];
                    for (size_t v = 0; v < vertexCount; v++) {
                        float weight = weightsFlat[b * vertexCount + v];
                        if (weight > 0.0f)
                            boneWeightsMap[boneNodeIndex].push_back(aiVertexWeight(static_cast<unsigned int>((mergeSubmeshes ? vertexOffset : 0) + v), weight));
                    }
                }

                vertexOffset += vertexCount;
            }

            // collect all filtered bones first, keep order, even if they have 0 weights
            std::vector<uint32_t> orderedBones;
            std::unordered_set<uint32_t> written;
            if (linkIt != nodeLinks.end()) {
                for (uint32_t boneIdx : linkIt->BoneOffsets) {
                    if (written.insert(boneIdx).second)
                        orderedBones.push_back(boneIdx);
                }
            }
            if (!orderedBones.empty()) anyWeights = true;

            // stash extra bones to add later if anyWeights true
            for (const auto& kv : nodeMap) {
                uint32_t nodeIdx = kv.first;
                if (kv.second->mNumMeshes > 0 || written.count(nodeIdx)) continue;
                bonesToAddPerMesh[mergeSubmeshes ? parentNode->mMeshes[0] : meshBase + m].push_back(nodeIdx);
            }

            // allocate bones with weights only (no extras yet)
            mesh->mNumBones = static_cast<unsigned int>(orderedBones.size());
            mesh->mBones = new aiBone * [mesh->mNumBones];
            size_t boneIdx = 0;

            for (uint32_t nodeIdx : orderedBones) {
                aiBone* bone = new aiBone();
                bone->mName = aiString(allNodeNames[nodeIdx].Name.c_str());
                bone->mOffsetMatrix = GetOffsetMatrix(nodeMap[nodeIdx], parentNode);

                const auto it = boneWeightsMap.find(nodeIdx);
                if (it != boneWeightsMap.end()) {
                    bone->mNumWeights = static_cast<unsigned int>(it->second.size());
                    bone->mWeights = new aiVertexWeight[it->second.size()];
                    std::copy(it->second.begin(), it->second.end(), bone->mWeights);
                }
                else {
                    bone->mNumWeights = 0;
                    bone->mWeights = nullptr;
                }

                mesh->mBones[boneIdx++] = bone;
            }
        }

        if (mergeSubmeshes && subCount > 1) {
            std::cout << "\nProcessed merged mesh " << allNodeNames[nodeIndex].Name;
        }
        else if (subCount > 1) {
            std::cout << "\nProcessed submeshes of mesh " << allNodeNames[nodeIndex].Name;
        }
        else {
            std::cout << "\nProcessed mesh " << allNodeNames[nodeIndex].Name;
        }
    }

    // add all extra bones that have 0 weights if any weights exist in file
    if (anyWeights) {
        for (size_t meshIndex = 0; meshIndex < bonesToAddPerMesh.size(); meshIndex++) {
            auto& meshBones = bonesToAddPerMesh[meshIndex];
            if (meshBones.empty()) continue;

            aiMesh* mesh = scene->mMeshes[meshIndex];

            size_t oldNumBones = mesh->mNumBones;
            size_t extraCount = meshBones.size();
            size_t newNumBones = oldNumBones + extraCount;

            aiBone** newBones = new aiBone * [newNumBones];

            // copy existing bones first
            for (size_t i = 0; i < oldNumBones; i++) {
                newBones[i] = mesh->mBones[i];
            }

            // replace pointer after copying
            delete[] mesh->mBones;
            mesh->mBones = newBones;
            mesh->mNumBones = static_cast<unsigned int>(newNumBones);

            size_t boneIdx = oldNumBones;
            aiNode* parentNode = nullptr;
            for (const auto& kv : nodeMap) {
                aiNode* n = kv.second;
                if (n->mNumMeshes > 0) {
                    for (unsigned int mi = 0; mi < n->mNumMeshes; mi++) {
                        if (scene->mMeshes[n->mMeshes[mi]] == mesh) {
                            parentNode = n;
                            break;
                        }
                    }
                    if (parentNode) break;
                }
            }

            for (uint32_t nodeIdx : meshBones) {
                aiBone* bone = new aiBone();
                bone->mName = aiString(allNodeNames[nodeIdx].Name.c_str());
                bone->mOffsetMatrix = GetOffsetMatrix(nodeMap[nodeIdx], parentNode);

                bone->mNumWeights = 0;
                bone->mWeights = nullptr;

                mesh->mBones[boneIdx++] = bone;
            }
        }
    }

    std::cout << std::endl << "Writing preset..." << std::endl;

    std::string presetFilename = path.substr(path.find_last_of("/\\") + 1);
    presetFilename = presetFilename.substr(0, presetFilename.find_last_of('.') == std::string::npos ? presetFilename.size() : presetFilename.find_last_of('.'));
    presetFilename = presetFilename.substr(0, presetFilename.find('_') == std::string::npos ? presetFilename.size() : presetFilename.find('_'));
    if (!presetFilename.empty()) presetFilename[0] = (char)toupper(presetFilename[0]);
    std::string presetPath = MakeOutFilePath(presetFilename + "Preset.txt", outDir);
    WritePresetFile(presetPath, materialsData, textureNames, allNodeNames, fullNodeDataList);

    std::cout << std::endl << "Writing collada .dae..." << std::endl;

    Assimp::Exporter exporter;
    std::string outFile = path.substr(0, path.find_last_of('.')) + "_out.dae";
    outFile = MakeOutFilePath(outFile, outDir);
    aiReturn rc;
    {
        Assimp::Exporter exporter;
        rc = exporter.Export(scene, "collada", outFile);
    }

    CallPatchDaeFileDLL(outFile, allMaterialToIndices);
    CallGetNormalsFromDLL(outFile);
    std::cout << std::endl << "Saved file as " << outFile << std::endl;
	std::ofstream(logPath.c_str(), std::ios::trunc) << "Saved collada file to " << outFile << "\n\nAlong with Maya py script to import normals\n(Blender can skip this)\n\nCreated " << presetFilename + "Preset.txt" << " file for MKDX importing" << std::endl;
}

void SaveMKDXFile(const std::string& path, const std::string& outDir, Header& header, std::vector<Material>& materialsData,
    std::vector<TextureName>& textureNames, std::vector<NodeNames>& boneNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList)
{
    std::string outFile = path.substr(0, path.find_last_of('.')) + "_out.bin";
    outFile = MakeOutFilePath(outFile, outDir);
    std::ofstream writer(outFile, std::ios::binary);

    // Write the header with all offsets as 0 for now
    writer.write("BIKE", 4);
    writer.write(reinterpret_cast<char*>(&header.Type), sizeof(header.Type));
    writer.write(reinterpret_cast<char*>(&header.Unknown), sizeof(header.Unknown));
    writer.write(reinterpret_cast<char*>(&header.Alignment), sizeof(header.Alignment));
    writer.write(reinterpret_cast<char*>(&header.Padding), sizeof(header.Padding));

    writer.write(reinterpret_cast<char*>(&header.MaterialCount), sizeof(header.MaterialCount));
    std::streampos posMaterialArrayOffset = writer.tellp();
    uint32_t zero32 = 0;
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // Placeholder for MaterialArrayOffset

    writer.write(reinterpret_cast<char*>(&header.TextureMapsCount), sizeof(header.TextureMapsCount));
    std::streampos posTextureNameArrayOffset = writer.tellp();
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // TextureNameArrayOffset

    writer.write(reinterpret_cast<char*>(&header.BoneCount), sizeof(header.BoneCount));
    std::streampos posBoneNameArrayOffset = writer.tellp();
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // BoneNameArrayOffset

    std::streampos posRootNodeArrayOffset = writer.tellp();
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // RootNodeArrayOffset

    writer.write(reinterpret_cast<char*>(&header.LinkNodeCount), sizeof(header.LinkNodeCount));
    std::streampos posLinkNodeOffset = writer.tellp();
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // LinkNodeOffset

    writer.write(reinterpret_cast<char*>(&header.TotalNodeCount), sizeof(header.TotalNodeCount));
    std::streampos posTotalNodeArrayOffset = writer.tellp();
    writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // TotalNodeArrayOffset

    writer.write(reinterpret_cast<char*>(&header.Padding2), sizeof(header.Padding2));

    // Write each material in the same order/format as ReadMaterial
    std::streampos materialArrayOffset = writer.tellp(); // for pointer add marathon at the end
    for (const auto& mat : materialsData)
    {
        for (uint32_t f : mat.Unknowns)
            writer.write(reinterpret_cast<char*>(&f), sizeof(uint32_t));
        for (uint32_t f : mat.UnknownValues)
            writer.write(reinterpret_cast<char*>(&f), sizeof(uint32_t));
        for (float f : mat.Diffuse)
            writer.write(reinterpret_cast<char*>(&f), sizeof(float));
        for (float f : mat.Specular)
            writer.write(reinterpret_cast<char*>(&f), sizeof(float));
        for (float f : mat.Ambience)
            writer.write(reinterpret_cast<char*>(&f), sizeof(float));

        writer.write(reinterpret_cast<const char*>(&mat.Shiny), sizeof(float));

        for (float f : mat.Unknowns2)
            writer.write(reinterpret_cast<char*>(&f), sizeof(float));
        for (uint16_t s : mat.TextureIndices)
            writer.write(reinterpret_cast<char*>(&s), sizeof(uint16_t));
    }

    // write 00 padding til at the next line
    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // write all texture name pointers (as blanks for now)
    std::streampos posTextureNameArray = writer.tellp();
    for (size_t i = 0; i < textureNames.size(); i++)
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t)); // write pair.key later after updating them to new values

    // pad to next line again
    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // the boneNames chunk is entirely pointers so just all 0s for now
    std::streampos posBoneNamesArray = writer.tellp();
    for (uint32_t i = 0; i < header.BoneCount; i++)
    {
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
    }
    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // write root node pointers as blanks
    std::streampos posRootNodeArray = writer.tellp();
    for (size_t i = 0; i < rootNodes.size(); i++)
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));

    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // write links data (two blank pointers then uint index of bone on mesh)
    std::streampos posLinkNodeArray = writer.tellp();
    for (const auto& link : nodeLinks)
    {
        for (size_t i = 0; i < link.BoneOffsets.size(); i++)
        {
            writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
            writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
            uint32_t index = static_cast<uint32_t>(i);
            writer.write(reinterpret_cast<char*>(&index), sizeof(uint32_t)); // index of bone on mesh
        }
    }
    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // the allNodeNames chunk is also entirely pointers so just all 0s for now x2
    std::streampos posAllNodeNamesArray = writer.tellp();
    for (uint32_t i = 0; i < header.TotalNodeCount; i++)
    {
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
        writer.write(reinterpret_cast<char*>(&zero32), sizeof(uint32_t));
    }
    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);


    // start writing children of first bone (appears first in data)
    std::vector<std::pair<int, uint32_t>> nodeUpdates, boneUpdates;
    std::vector<uint32_t> subMeshOffsetsList;
    int j = 0;

    for (auto& fullNodeData : fullNodeDataList) {
        subMeshOffsetsList.clear();
        // write submesh data in order by looping through each submesh, writing all data it points to in order of appearance (all 9 buffers if current offset value > 0) then write the submesh data itself
        for (size_t i = 0; i < fullNodeData.subMeshes.size(); i++) {
            auto& submesh = fullNodeData.subMeshes[i];

            if (submesh.VertexPositionOffset > 0) {
                submesh.VertexPositionOffset = writer.tellp();
                for (float val : fullNodeData.verticesList[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16); // new row
            }

            if (submesh.VertexNormalOffset > 0) {
                submesh.VertexNormalOffset = writer.tellp();
                for (float val : fullNodeData.normalsList[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16); // new row
            }

            if (submesh.ColorBufferOffset > 0) {
                submesh.ColorBufferOffset = writer.tellp();
                for (float val : fullNodeData.colorsList[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.TexCoord0Offset > 0) {
                submesh.TexCoord0Offset = writer.tellp();
                for (float val : fullNodeData.uvs0List[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.TexCoord1Offset > 0) {
                submesh.TexCoord1Offset = writer.tellp();
                for (float val : fullNodeData.uvs1List[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.TexCoord2Offset > 0) {
                submesh.TexCoord2Offset = writer.tellp();
                for (float val : fullNodeData.uvs2List[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.TexCoord3Offset > 0) {
                submesh.TexCoord3Offset = writer.tellp();
                for (float val : fullNodeData.uvs3List[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.FaceOffset > 0) {
                submesh.FaceOffset = writer.tellp();
                for (int16_t val : fullNodeData.polygonsList[i]) writer.write((char*)&val, sizeof(int16_t));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

            if (submesh.WeightOffset > 0) {
                submesh.WeightOffset = writer.tellp();
                for (float val : fullNodeData.weightsList[i]) writer.write((char*)&val, sizeof(float));
                writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
            }

			// write submesh data block (the pointers etc not the buffers)
            subMeshOffsetsList.push_back(writer.tellp());
            writer.write((char*)&submesh.Padding, sizeof(uint32_t));
            writer.write((char*)&submesh.TriangleCount, sizeof(uint32_t));
            writer.write((char*)&submesh.MaterialIndex, sizeof(uint32_t));
            for (float f : submesh.BoundingBox) writer.write((char*)&f, sizeof(float));
            writer.write((char*)&submesh.VertexCount, sizeof(uint32_t));
            writer.write((char*)&submesh.VertexPositionOffset, sizeof(uint32_t));
            writer.write((char*)&submesh.VertexNormalOffset, sizeof(uint32_t));
            writer.write((char*)&submesh.ColorBufferOffset, sizeof(uint32_t));
            writer.write((char*)&submesh.TexCoord0Offset, sizeof(uint32_t));
            writer.write((char*)&submesh.TexCoord1Offset, sizeof(uint32_t));
            writer.write((char*)&submesh.TexCoord2Offset, sizeof(uint32_t));
            writer.write((char*)&submesh.TexCoord3Offset, sizeof(uint32_t));
            writer.write((char*)&submesh.FaceOffset, sizeof(uint32_t));
            writer.write((char*)&submesh.SkinnedBonesCount, sizeof(uint32_t));
            writer.write((char*)&submesh.BonesIndexMask, sizeof(uint32_t));
            writer.write((char*)&submesh.WeightOffset, sizeof(uint32_t));
            for (float f : submesh.BoundingBoxMaxMin) writer.write((char*)&f, sizeof(float));
            writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
        }

        // write pointers for submeshes, but empty pointers for node children

        uint32_t subMeshesOffset = 0;
        if (!fullNodeData.subMeshes.empty()) {
            subMeshesOffset = writer.tellp();
            for (uint32_t offset : subMeshOffsetsList) writer.write((char*)&offset, sizeof(uint32_t));
            writer.write((char*)&zero32, sizeof(uint32_t));
            writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
        }

        // even tho we write all submesh offsets, write blanks for children node offsets because each of these are added later

        uint32_t childNodesOffset = 0;
        if (!fullNodeData.childrenIndexList.empty()) {
            childNodesOffset = writer.tellp();
            for (size_t i = 0; i < fullNodeData.childrenIndexList.size(); i++) {
                writer.write((char*)&zero32, sizeof(uint32_t));
            }
            writer.write((char*)&zero32, sizeof(uint32_t)); // pointer array must end with 0
            writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);
        }

        // update node data pointers since data is written last
        fullNodeData.boneData.ModelObjectArrayOffset = subMeshesOffset;
        fullNodeData.boneData.ChildrenArrayOffset = childNodesOffset;

        // update the position of this node data in nodenames and bonenames (add to lists and update both after so that we arent changing the list as we check it nd cause errors)
        uint32_t newOffset = writer.tellp();
        nodeUpdates.emplace_back(j, newOffset);
        auto boneMatch = std::find_if(boneNames.begin(), boneNames.end(), [&](const NodeNames& b) {
            return b.DataOffset == allNodeNames[j].DataOffset;
            });
        if (boneMatch != boneNames.end()) {
            int boneIndex = std::distance(boneNames.begin(), boneMatch);
            boneUpdates.emplace_back(boneIndex, newOffset);
        }

        allNodeNames[j].DataOffset = newOffset; // update the node data offset in allNodeNames list

        // write all node data
        auto& bone = fullNodeData.boneData;
        writer.write((char*)&bone.Visibility, sizeof(uint32_t));
        for (float f : bone.Scale) writer.write((char*)&f, sizeof(float));
        for (float f : bone.Rotation) writer.write((char*)&f, sizeof(float));
        for (float f : bone.Translation) writer.write((char*)&f, sizeof(float));
        for (float u : bone.BoundingBox) writer.write((char*)&u, sizeof(float));
        writer.write((char*)&bone.ModelObjectArrayOffset, sizeof(uint32_t));
        writer.write((char*)&bone.ChildrenArrayOffset, sizeof(uint32_t));
        for (float f : bone.MoreFloats) writer.write((char*)&f, sizeof(float));
        for (float u : bone.AnimationVals) writer.write((char*)&u, sizeof(float));
        for (float u : bone.BoundingBoxMaxMin) writer.write((char*)&u, sizeof(float));
        uint32_t pad = 0;
        writer.write((char*)&pad, sizeof(uint32_t)); // pad

        j++;
    }

    writer.write(std::vector<char>((16 - writer.tellp() % 16) % 16, 0).data(), (16 - writer.tellp() % 16) % 16);

    // update node and bone names lists with the pointers created in last loop
    for (const auto& pair : nodeUpdates) {
        int index = pair.first;
        uint32_t newOffset = pair.second;
        auto& old = allNodeNames[index];
        allNodeNames[index] = NodeNames{ newOffset, old.Name, old.NamePointer };
    }
    for (const auto& pair : boneUpdates) {
        int index = pair.first;
        uint32_t newOffset = pair.second;
        auto& old = boneNames[index];
        boneNames[index] = NodeNames{ newOffset, old.Name, old.NamePointer };
    }

    // Update all texture name pointers (in memory)
    for (size_t i = 0; i < textureNames.size(); i++) {
        uint32_t offset = static_cast<uint32_t>(writer.tellp());
        writer.write(textureNames[i].Name.c_str(), textureNames[i].Name.size());
        writer.put(0); // null terminator

        textureNames[i].NamePointer = offset; // use offsets after seeking to posTextureNameArray (not posTextureNameArrayOffset, thats the pointer TO the array)
    }

    // Update all bone name pointers (in memory)
    for (size_t i = 0; i < boneNames.size(); i++) {
        uint32_t offset = static_cast<uint32_t>(writer.tellp());
        writer.write(boneNames[i].Name.c_str(), boneNames[i].Name.size());
        writer.put(0); // null terminator

        boneNames[i].NamePointer = offset;
    }

    // Update all node name pointers (in memory)
    for (size_t i = 0; i < allNodeNames.size(); i++) {
        uint32_t offset = static_cast<uint32_t>(writer.tellp());
        writer.write(allNodeNames[i].Name.c_str(), allNodeNames[i].Name.size());
        writer.put(0); // null terminator

        allNodeNames[i].NamePointer = offset;
    }

    // Update all pointers in out file (seek to variables that start with 'pos')
    if (header.MaterialCount)
        writer.seekp(posMaterialArrayOffset), writer.write(reinterpret_cast<char*>(&materialArrayOffset), sizeof(uint32_t));

    if (header.TextureMapsCount)
        writer.seekp(posTextureNameArrayOffset), writer.write(reinterpret_cast<char*>(&posTextureNameArray), sizeof(uint32_t));

    if (header.BoneCount)
        writer.seekp(posBoneNameArrayOffset), writer.write(reinterpret_cast<char*>(&posBoneNamesArray), sizeof(uint32_t));

    writer.seekp(posRootNodeArrayOffset), writer.write(reinterpret_cast<char*>(&posRootNodeArray), sizeof(uint32_t));

    if (header.LinkNodeCount)
        writer.seekp(posLinkNodeOffset), writer.write(reinterpret_cast<char*>(&posLinkNodeArray), sizeof(uint32_t));

    if (header.TotalNodeCount)
        writer.seekp(posTotalNodeArrayOffset), writer.write(reinterpret_cast<char*>(&posAllNodeNamesArray), sizeof(uint32_t));

    writer.seekp(posTextureNameArray);
    for (size_t i = 0; i < textureNames.size(); i++)
        writer.write(reinterpret_cast<char*>(&textureNames[i].NamePointer), sizeof(uint32_t));

    writer.seekp(posBoneNamesArray);
    for (uint32_t i = 0; i < header.BoneCount; i++) {
        writer.write(reinterpret_cast<char*>(&boneNames[i].NamePointer), sizeof(uint32_t));
        writer.write(reinterpret_cast<char*>(&boneNames[i].DataOffset), sizeof(uint32_t));
    }

    writer.seekp(posRootNodeArray);
    for (size_t i = 0; i < rootNodes.size(); i++) {
        int index = static_cast<int>(rootNodes[i]);
        writer.write(reinterpret_cast<char*>(&allNodeNames[index].DataOffset), sizeof(uint32_t));
    }

    for (const auto& nodeData : fullNodeDataList) {
        writer.seekp(nodeData.boneData.ChildrenArrayOffset);
        for (const auto& childIndex : nodeData.childrenIndexList) {
            uint32_t childOffset = allNodeNames[static_cast<int>(childIndex)].DataOffset;
            writer.write(reinterpret_cast<char*>(&childOffset), sizeof(uint32_t));
        }
    }

    writer.seekp(posLinkNodeArray);
    for (const auto& link : nodeLinks) {
        uint32_t meshDataOffset = allNodeNames[static_cast<int>(link.MeshOffset)].DataOffset; // mesh offset index to actual offset
        for (size_t i = 0; i < link.BoneOffsets.size(); i++) {
            uint32_t boneDataOffset = allNodeNames[static_cast<int>(link.BoneOffsets[i])].DataOffset; // bone offset index to actual offset
            writer.write(reinterpret_cast<char*>(&meshDataOffset), sizeof(uint32_t));
            writer.write(reinterpret_cast<char*>(&boneDataOffset), sizeof(uint32_t));
            uint32_t index = static_cast<uint32_t>(i);
            writer.write(reinterpret_cast<char*>(&index), sizeof(uint32_t));
        }
    }

    writer.seekp(posAllNodeNamesArray);
    for (uint32_t i = 0; i < header.TotalNodeCount; i++) {
        writer.write(reinterpret_cast<char*>(&allNodeNames[i].NamePointer), sizeof(uint32_t));
        writer.write(reinterpret_cast<char*>(&allNodeNames[i].DataOffset), sizeof(uint32_t));
    }

    writer.close();
    std::cout << "\nSaved binary MKDX file to " << outFile << std::endl;
    std::ofstream(logPath.c_str(), std::ios::trunc) << "Saved binary MKDX file to " << outFile << std::endl;
}