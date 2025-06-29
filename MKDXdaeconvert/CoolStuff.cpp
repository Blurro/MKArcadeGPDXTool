#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <assimp/mesh.h>
#include <assimp/anim.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
// above libs installed through vcpkg not nuget
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <bitset>
#include <format>
#include <windows.h>
#define _CRT_SECURE_NO_WARNINGS
// my headers
#include "CoolStructs.h"
#include "SaveFuncs.h"

void FireLogoPrint(int x, int y, int endx, int endy) {
    // if we detect regular cmd instead of terminal skip the logo stuff
    HWND consoleWindow = GetConsoleWindow();
    if (consoleWindow != NULL) {
        char className[256];
        GetClassNameA(consoleWindow, className, sizeof(className));
        if (std::string(className).find("ConsoleWindowClass") != std::string::npos) {
            return;
        }
    }
    const std::string logo[] = {
    "               ..::~:~:~.: . .               ",
    "           .^YY#5#J#55YJY7Y77!~!::.          ",
    "        .~PG5~7?#?B7B~B^G:5:5.Y:! ^.~        ",
    "       ?B#&G#G?~::57G!5!Y~J~?.! :.:^.:..     ",
    "     !G&#&?. .~G?P.^:7Y!J^? :.~.~..  .....   ",
    "   .G&&#&B      .~5^?:Y^P.!.~.^       . :    ",
    "  :&&@B&GB          : 7:Y.            .. .   ",
    " .&&@#&B&GJ           :~7.            : . .  ",
    " B&@#&G&G#P5~.      .!!!!:~        ..: : .   ",
    ".@#@B&57 . ^:7~Y~?!??~J:Y.!^:! . :.        . ",
    "!&###B.         .:.:^~^:^.~.                 ",
    "7&###P                                       ",
    "^&B&PB                                       ",
    " BG@J&~                                      ",
    " :#P&P#                                      ",
    "  !BP#5Y                                     ",
    "   ^PYGY~                                    ",
    "    .??PP~                                   ",
    "      :?YYJ.                                 ",
    "        .~7?Y7..                  .          ",
    "           .^~^?:! .                         ",
    "               . : : : . .                   "
    };
    for (int i = 0; i < 22; i++) {
        int leadingSpaces = 0;
        while (leadingSpaces < logo[i].size() && logo[i][leadingSpaces] == ' ') {
            leadingSpaces++;
        }
        std::string lineWithoutSpaces = logo[i].substr(leadingSpaces);
        std::cout << "\u001b[34m\033[" << y + i << ";" << x + leadingSpaces << "H";
        std::cout << lineWithoutSpaces;
    }
    std::cout << "\u001b[37m\033[" << endy << ";" << endx << "H";
}

// helper funcs
inline std::vector<int16_t> ReadUInt16s(std::istream& stream, size_t count) {
    std::vector<int16_t> arr(count);
    stream.read(reinterpret_cast<char*>(arr.data()), count * sizeof(uint16_t));
    return arr;
}

inline std::vector<uint32_t> ReadUInt32s(std::istream& stream, size_t count) {
    std::vector<uint32_t> arr(count);
    stream.read(reinterpret_cast<char*>(arr.data()), count * sizeof(uint32_t));
    return arr;
}

inline std::vector<float> ReadFloats(std::istream& stream, size_t count) {
    std::vector<float> arr(count);
    stream.read(reinterpret_cast<char*>(arr.data()), count * sizeof(float));
    return arr;
}

std::string ReadCStringAtOffset(std::istream& stream, uint32_t pointer) {
    auto currentPos = stream.tellg();
    stream.seekg(pointer, std::ios::beg);

    std::string result;
    char c;
    while (stream.get(c) && c != '\0')
        result.push_back(c);

    stream.seekg(currentPos, std::ios::beg);
    return result;
}

// all funcs to use the structs in coolstructs.h

Header ReadHeader(std::istream& stream) {
    char sig[4];
    stream.read(sig, 4);
    if (std::string(sig, 4) != "BIKE")
        throw std::runtime_error("Invalid file signature. Expected 'BIKE'.");

    Header h;
    h.Type = ReadUInt16s(stream, 1)[0];
    h.Unknown = ReadUInt16s(stream, 1)[0];
    h.Alignment = ReadUInt32s(stream, 1)[0];
    h.Padding = ReadUInt32s(stream, 1)[0];
    h.MaterialCount = ReadUInt32s(stream, 1)[0];
    h.MaterialArrayOffset = ReadUInt32s(stream, 1)[0];
    h.TextureMapsCount = ReadUInt32s(stream, 1)[0];
    h.TextureNameArrayOffset = ReadUInt32s(stream, 1)[0];
    h.BoneCount = ReadUInt32s(stream, 1)[0];
    h.BoneNameArrayOffset = ReadUInt32s(stream, 1)[0];
    h.RootNodeArrayOffset = ReadUInt32s(stream, 1)[0];
    h.LinkNodeCount = ReadUInt32s(stream, 1)[0];
    h.LinkNodeOffset = ReadUInt32s(stream, 1)[0];
    h.TotalNodeCount = ReadUInt32s(stream, 1)[0];
    h.TotalNodeArrayOffset = ReadUInt32s(stream, 1)[0];
    h.Padding2 = ReadUInt32s(stream, 1)[0];
    return h;
}

Material ReadMaterial(std::istream& stream) {
    Material m;
    m.Unknowns = ReadUInt32s(stream, 6);
    m.UnknownValues = ReadFloats(stream, 4);
    m.Diffuse = ReadFloats(stream, 4);
    m.Specular = ReadFloats(stream, 4);
    m.Ambience = ReadFloats(stream, 4);
    float shiny;
    stream.read(reinterpret_cast<char*>(&shiny), sizeof(float));
    m.Shiny = shiny;
    m.Unknowns2 = ReadFloats(stream, 19);

    m.TextureIndices.resize(6);
    for (int i = 0; i < 6; i++) {
        int16_t val;
        stream.read(reinterpret_cast<char*>(&val), sizeof(int16_t));
        m.TextureIndices[i] = val;
    }
    return m;
}

BoneData ReadBoneData(std::istream& stream) {
    BoneData b;
    b.Visibility = ReadUInt32s(stream, 1)[0];
    b.Scale = ReadFloats(stream, 3);
    b.Rotation = ReadFloats(stream, 3);
    b.Translation = ReadFloats(stream, 3);
    b.BoundingBox = ReadUInt32s(stream, 4);
    b.ModelObjectArrayOffset = ReadUInt32s(stream, 1)[0];
    b.ChildrenArrayOffset = ReadUInt32s(stream, 1)[0];
    b.MoreFloats = ReadFloats(stream, 3);
    b.Unknowns2 = ReadUInt32s(stream, 12);
    return b;
}

SubMesh ReadSubMesh(std::istream& stream) {
    SubMesh s;
    s.Padding = ReadUInt32s(stream, 1)[0];
    s.TriangleCount = ReadUInt32s(stream, 1)[0];
    s.MaterialIndex = ReadUInt32s(stream, 1)[0];
    s.BoundingBox = ReadFloats(stream, 4);
    s.VertexCount = ReadUInt32s(stream, 1)[0];
    s.VertexPositionOffset = ReadUInt32s(stream, 1)[0];
    s.VertexNormalOffset = ReadUInt32s(stream, 1)[0];
    s.ColorBufferOffset = ReadUInt32s(stream, 1)[0];
    s.TexCoord0Offset = ReadUInt32s(stream, 1)[0];
    s.TexCoord1Offset = ReadUInt32s(stream, 1)[0];
    s.TexCoord2Offset = ReadUInt32s(stream, 1)[0];
    s.TexCoord3Offset = ReadUInt32s(stream, 1)[0];
    s.FaceOffset = ReadUInt32s(stream, 1)[0];
    s.SkinnedBonesCount = ReadUInt32s(stream, 1)[0];
    s.BonesIndexMask = ReadUInt32s(stream, 1)[0];
    s.WeightOffset = ReadUInt32s(stream, 1)[0];
    s.BoundingBoxMaxMin = ReadFloats(stream, 6);
    return s;
}

// ========================================================
// ====================    MAIN    ========================
// ========================================================
int main(int argc, char* argv[])
{
    std::cout << "\033[34mVery epic mkagpdx dae tool\033[37m\n";
    std::cout << "Time to get some exporties donezies\n\n";

    // get exe directory
    std::string filePathInput;
    for (int i = 1; i < argc; ++i) {
        FILE* f = nullptr;
        if (fopen_s(&f, argv[i], "rb") == 0 && f != nullptr) {
            fclose(f);
            filePathInput = argv[i];
        }
    }
	// debug default file path
    //if (filePathInput.empty()) filePathInput = "./it_banana.bin";

    if (filePathInput.empty()) {
        std::cerr << "Usage for dae export: Drag and drop a .bin file onto the tool, or use command 'MKDXtool.exe <file path>'\n";
        system("pause");
        return 0;
    }

    std::ifstream fs(filePathInput, std::ios::binary);
    if (!fs) {
        std::cerr << "Failed to open file: " << filePathInput << "\n";
        system("pause");
        return 1;
    }

    // FIRE LOGO PRINT
    FireLogoPrint(56, 5, 0, 4);

    auto headerData = ReadHeader(fs);
    std::cout << "Read header: MaterialCount=" << headerData.MaterialCount << ", TextureMapsCount=" << headerData.TextureMapsCount << "\n";

    fs.seekg(headerData.MaterialArrayOffset, std::ios::beg);
    std::vector<Material> materialsData;
    for (uint32_t i = 0; i < headerData.MaterialCount; ++i)
        materialsData.push_back(ReadMaterial(fs));
    std::cout << "Read materials: " << materialsData.size() << " materials added\n";

    fs.seekg(headerData.TextureNameArrayOffset, std::ios::beg);
    std::vector<TextureName> textureNames;
    for (uint32_t i = 0; i < headerData.TextureMapsCount; ++i) {
        uint32_t ptr;
        fs.read(reinterpret_cast<char*>(&ptr), sizeof(ptr));
        auto texName = ReadCStringAtOffset(fs, ptr);
        textureNames.push_back(TextureName{ texName, ptr });
        std::cout << "[" << i << "] " << texName << "\n";
    }
    std::cout << "Read texture names: " << textureNames.size() << " names added\n";

    // seek to and read bone names
    fs.seekg(headerData.BoneNameArrayOffset, std::ios::beg);
    std::vector<NodeNames> boneNames;
    for (uint32_t i = 0; i < headerData.BoneCount; ++i) {
        uint32_t namePtr, dataOffset;
        fs.read(reinterpret_cast<char*>(&namePtr), sizeof(namePtr));
        fs.read(reinterpret_cast<char*>(&dataOffset), sizeof(dataOffset));
        auto boneName = ReadCStringAtOffset(fs, namePtr);
        boneNames.push_back(NodeNames{ dataOffset, boneName, namePtr });
    }

    // read node links
    fs.seekg(headerData.LinkNodeOffset, std::ios::beg);
    std::vector<NodeLinks> nodeLinks;
    for (uint32_t i = 0; i < headerData.LinkNodeCount; ++i) {
        uint32_t meshOffset, boneOffset, dummy;
        fs.read(reinterpret_cast<char*>(&meshOffset), sizeof(meshOffset));
        fs.read(reinterpret_cast<char*>(&boneOffset), sizeof(boneOffset));
        fs.read(reinterpret_cast<char*>(&dummy), sizeof(dummy)); // unused

        auto it = std::find_if(nodeLinks.begin(), nodeLinks.end(), [meshOffset](const NodeLinks& n) { return n.MeshOffset == meshOffset; });
        if (it == nodeLinks.end()) {
            nodeLinks.push_back(NodeLinks{ meshOffset });
            it = std::prev(nodeLinks.end());
        }
        it->BoneOffsets.push_back(boneOffset);

        auto boneIt = std::find_if(boneNames.begin(), boneNames.end(), [boneOffset](const NodeNames& n) { return n.DataOffset == boneOffset; });
        std::string boneName = (boneIt != boneNames.end()) ? boneIt->Name : "(unknown)";
        //std::cout << "Linked meshOffset " << std::hex << meshOffset << " to boneOffset " << boneOffset << " (" << boneName << ")\n";
    }

    // read all node names
    fs.seekg(headerData.TotalNodeArrayOffset, std::ios::beg);
    std::vector<NodeNames> allNodeNames;
    for (uint32_t i = 0; i < headerData.TotalNodeCount; ++i) {
        uint32_t namePtr, dataOffset;
        fs.read(reinterpret_cast<char*>(&namePtr), sizeof(namePtr));
        fs.read(reinterpret_cast<char*>(&dataOffset), sizeof(dataOffset));
        auto name = ReadCStringAtOffset(fs, namePtr);
        allNodeNames.push_back(NodeNames{ dataOffset, name, namePtr });
        std::cout << "Added node: offset " << std::hex << dataOffset << " = \"" << name << "\"\n";
    }

    // read root nodes (usually just 1)
    fs.seekg(headerData.RootNodeArrayOffset, std::ios::beg);
    std::vector<uint32_t> rootNodes;
    while (true) {
        uint32_t val;
        fs.read(reinterpret_cast<char*>(&val), sizeof(val));
        if (val == 0) break;
        rootNodes.push_back(val);

        auto nodeIt = std::find_if(allNodeNames.begin(), allNodeNames.end(), [val](const NodeNames& n) { return n.DataOffset == val; });
        std::string name = (nodeIt != allNodeNames.end()) ? nodeIt->Name : "(unknown)";
        std::cout << "Added root node offset: " << std::hex << val << " (" << name << ")\n";
    }

    std::vector<FullNodeData> fullNodeDataList;

    for (const auto& node : allNodeNames) {
        fs.seekg(node.DataOffset, std::ios::beg);
        BoneData boneData = ReadBoneData(fs);

        uint32_t meshy = boneData.ModelObjectArrayOffset;
        uint32_t childy = boneData.ChildrenArrayOffset;

        FullNodeData fullData;
        fullData.boneData = boneData;

        if (meshy > 0) {
            int j = 0;
            while (true) {
                fs.seekg(meshy + j * 4, std::ios::beg);
                uint32_t submeshOffset;
                fs.read(reinterpret_cast<char*>(&submeshOffset), sizeof(submeshOffset));
                if (submeshOffset == 0) break;

                fs.seekg(submeshOffset, std::ios::beg);
                SubMesh submeshData = ReadSubMesh(fs);
                fullData.subMeshes.push_back(submeshData);

                uint32_t vCount = submeshData.VertexCount;
                uint32_t pCount = submeshData.TriangleCount;
                uint32_t wCount = submeshData.SkinnedBonesCount;

                if (submeshData.VertexPositionOffset > 0) {
                    fs.seekg(submeshData.VertexPositionOffset, std::ios::beg);
                    std::vector<float> verts(vCount * 3);
                    fs.read(reinterpret_cast<char*>(verts.data()), verts.size() * sizeof(float));
                    fullData.verticesList.push_back(verts);
                }
                if (submeshData.VertexNormalOffset > 0) {
                    fs.seekg(submeshData.VertexNormalOffset, std::ios::beg);
                    std::vector<float> norms(vCount * 3);
                    fs.read(reinterpret_cast<char*>(norms.data()), norms.size() * sizeof(float));
                    fullData.normalsList.push_back(norms);
                }
                if (submeshData.ColorBufferOffset > 0) {
					fs.seekg(submeshData.ColorBufferOffset, std::ios::beg);
					std::vector<float> colors(vCount * 4);
					fs.read(reinterpret_cast<char*>(colors.data()), colors.size() * sizeof(float));
					fullData.colorsList.push_back(colors);
                }
                if (submeshData.TexCoord0Offset > 0) {
                    fs.seekg(submeshData.TexCoord0Offset, std::ios::beg);
                    std::vector<float> uvs0(vCount * 2);
                    fs.read(reinterpret_cast<char*>(uvs0.data()), uvs0.size() * sizeof(float));
                    fullData.uvs0List.push_back(uvs0);
                }
                if (submeshData.TexCoord1Offset > 0) {
                    fs.seekg(submeshData.TexCoord1Offset, std::ios::beg);
                    std::vector<float> uvs1(vCount * 2);
                    fs.read(reinterpret_cast<char*>(uvs1.data()), uvs1.size() * sizeof(float));
                    fullData.uvs1List.push_back(uvs1);
                }
                if (submeshData.TexCoord2Offset > 0) {
                    fs.seekg(submeshData.TexCoord2Offset, std::ios::beg);
                    std::vector<float> uvs2(vCount * 2);
                    fs.read(reinterpret_cast<char*>(uvs2.data()), uvs2.size() * sizeof(float));
                    fullData.uvs2List.push_back(uvs2);
                }
                if (submeshData.TexCoord3Offset > 0) {
                    fs.seekg(submeshData.TexCoord3Offset, std::ios::beg);
                    std::vector<float> uvs3(vCount * 2);
                    fs.read(reinterpret_cast<char*>(uvs3.data()), uvs3.size() * sizeof(float));
                    fullData.uvs3List.push_back(uvs3);
                }
                if (submeshData.FaceOffset > 0) {
                    fs.seekg(submeshData.FaceOffset, std::ios::beg);
                    std::vector<int16_t> polys(pCount * 3);
                    fs.read(reinterpret_cast<char*>(polys.data()), polys.size() * sizeof(int16_t));
                    fullData.polygonsList.push_back(polys);
                }
                if (submeshData.WeightOffset > 0) {
                    fs.seekg(submeshData.WeightOffset, std::ios::beg);
                    std::vector<float> weights(wCount * vCount);
                    fs.read(reinterpret_cast<char*>(weights.data()), weights.size() * sizeof(float));
                    fullData.weightsList.push_back(weights);
                }
                j++;
            }
        }

        if (childy > 0) {
            fs.seekg(childy, std::ios::beg);
            while (true) {
                uint32_t childOffset;
                fs.read(reinterpret_cast<char*>(&childOffset), sizeof(childOffset));
                if (childOffset == 0) break;
                fullData.childrenIndexList.push_back(childOffset);
            }
        }

        fullNodeDataList.push_back(fullData);
    }

    for (auto& node : fullNodeDataList) {
        for (size_t i = 0; i < node.childrenIndexList.size(); ++i)
            node.childrenIndexList[i] = static_cast<uint32_t>(
                std::find_if(allNodeNames.begin(), allNodeNames.end(),
                    [&](const NodeNames& n) { return n.DataOffset == node.childrenIndexList[i]; }) - allNodeNames.begin());
    }

    for (size_t i = 0; i < rootNodes.size(); ++i) {
        rootNodes[i] = static_cast<uint32_t>(
            std::find_if(allNodeNames.begin(), allNodeNames.end(),
                [&](const NodeNames& n) { return n.DataOffset == rootNodes[i]; }) - allNodeNames.begin());
    }

    for (auto& link : nodeLinks) {
        link.MeshOffset = static_cast<uint32_t>(
            std::find_if(allNodeNames.begin(), allNodeNames.end(),
                [&](const NodeNames& n) { return n.DataOffset == link.MeshOffset; }) - allNodeNames.begin());

        for (size_t i = 0; i < link.BoneOffsets.size(); ++i) {
            link.BoneOffsets[i] = static_cast<uint32_t>(
                std::find_if(allNodeNames.begin(), allNodeNames.end(),
                    [&](const NodeNames& n) { return n.DataOffset == link.BoneOffsets[i]; }) - allNodeNames.begin());
        }
    }

    fs.close();

    // start saving file
    std::string ext = filePathInput.substr(filePathInput.find_last_of('.') + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "bin") {
        //SaveGltfFile(filePathInput, headerData, materialsData, textureNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList);
        //SaveMKDXFile(filePathInput, headerData, materialsData, textureNames, boneNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList);
        SaveDaeFile(filePathInput, headerData, materialsData, textureNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList);
    }

    system("pause");
    return 0;


    auto scene = new aiScene();
    // first level parent named "scene" (collada visual_scene)
    auto sceneNode = new aiNode();
    sceneNode->mName = aiString("scene");
    sceneNode->mNumChildren = 1;
    sceneNode->mChildren = new aiNode * [1];

    // second level parent named "root"
    auto root = new aiNode();
    root->mName = aiString("root");
    root->mNumChildren = 2;
    root->mChildren = new aiNode * [2];
    root->mParent = sceneNode;
    sceneNode->mChildren[0] = root;

    // joint1 node
    auto joint1 = new aiNode();
    joint1->mName = aiString("joint1");
    joint1->mNumChildren = 2;
    joint1->mChildren = new aiNode * [2];
    joint1->mParent = root;

    // joint2 child of joint1
    auto joint2 = new aiNode();
    joint2->mName = aiString("joint2");
    joint2->mParent = joint1;
    joint1->mChildren[0] = joint2;


    auto joint3 = new aiNode();
    joint3->mName = aiString("joint3");
    joint3->mParent = joint1;
    joint1->mChildren[1] = joint3;

    // mesh node (cube)
    auto meshNode = new aiNode();
    meshNode->mName = aiString("mesh1");
    meshNode->mNumMeshes = 1;
    meshNode->mMeshes = new unsigned int[1] {0};
    meshNode->mParent = root;

    // add joint1 and meshNode as children of root
    root->mChildren[0] = joint1;
    root->mChildren[1] = meshNode;

    // set the scene root node to sceneNode (topmost)
    scene->mRootNode = sceneNode;

    // create mesh array
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh * [1];

    auto cube = new aiMesh();
    cube->mName = aiString("cube");

    cube->mNumVertices = 8;
    cube->mVertices = new aiVector3D[8]{
        {-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1},
        {-1,-1,1}, {1,-1,1}, {1,1,1}, {-1,1,1}
    };

    cube->mNumFaces = 12;
    cube->mFaces = new aiFace[12];
    int faces[12][3] = {
        {0,1,2}, {0,2,3},
        {4,6,5}, {4,7,6},
        {0,4,5}, {0,5,1},
        {1,5,6}, {1,6,2},
        {2,6,7}, {2,7,3},
        {3,7,4}, {3,4,0}
    };
    for (int i = 0; i < 12; i++) {
        cube->mFaces[i].mNumIndices = 3;
        cube->mFaces[i].mIndices = new unsigned int[3] {
            (unsigned int)faces[i][0], (unsigned int)faces[i][1], (unsigned int)faces[i][2]
            };
    }

    // bones
    cube->mNumBones = 3;
    cube->mBones = new aiBone * [3];

    // bone 1
    auto bone1 = new aiBone();
    bone1->mName = aiString("joint1");
    bone1->mNumWeights = 4;
    bone1->mWeights = new aiVertexWeight[4]{
        {0,1.0f}, {3,1.0f}, {4,1.0f}, {7,1.0f}
    };
    bone1->mOffsetMatrix = aiMatrix4x4();
    cube->mBones[0] = bone1;

    // bone 2
    auto bone2 = new aiBone();
    bone2->mName = aiString("joint2");
    bone2->mNumWeights = 4;
    bone2->mWeights = new aiVertexWeight[4]{
        {1,1.0f}, {2,1.0f}, {5,1.0f}, {6,1.0f}
    };
    bone2->mOffsetMatrix = aiMatrix4x4();
    cube->mBones[1] = bone2;

    // bone 3
    auto bone3 = new aiBone();
    bone3->mName = aiString("joint3");
    bone3->mNumWeights = 0;
    bone3->mOffsetMatrix = aiMatrix4x4();
    cube->mBones[2] = bone3;

    scene->mMeshes[0] = cube;

    // materials must exist (even dummy)
    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial * [1];
    scene->mMaterials[0] = new aiMaterial();

    // export
    Assimp::Exporter exporter;
    auto ret = exporter.Export(scene, "collada", "out.dae", 0);

    if (ret != AI_SUCCESS) {
        std::cerr << "Export failed: " << exporter.GetErrorString() << std::endl;
        return 1;
    }

    std::cout << "Export succeeded\n";
    return 0;
}