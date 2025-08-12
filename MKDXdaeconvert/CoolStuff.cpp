#include <assimp/scene.h>
#include <assimp/Exporter.hpp>
#include <assimp/mesh.h>
#include <assimp/anim.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
// above lib assimp installed through vcpkg not nuget
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <bitset>
#include <windows.h>
#include <sstream>
#include <set>
#include <unordered_map>
#include <array>
#include <sys/stat.h>

#define _CRT_SECURE_NO_WARNINGS
// my headers
#include "CoolStructs.h"
#include "SaveFuncs.h"

void FireLogoPrint(int x) {
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
        while (leadingSpaces < logo[i].size() && logo[i][leadingSpaces] == ' ')
            leadingSpaces++;
        std::string lineWithoutSpaces = logo[i].substr(leadingSpaces);
        if (i > 0) std::cout << '\n';  // push screen down by printing newline after first line
        std::cout << "\033[" << (x + leadingSpaces) << "C";  // move right by x + leadingSpaces
        std::cout << "\u001b[34m" << lineWithoutSpaces;
        std::cout << "\033[" << (x + leadingSpaces + lineWithoutSpaces.size()) << "D";  // move cursor back left after printing
    }
    std::cout << "\033[" << (22) << "A\033[1G";
    std::cout << "\u001b[37m";  // reset color

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

void collectWorldVerts(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, std::vector<aiVector3D>& vertsOut) {
    aiMatrix4x4 localTransform = parentTransform * node->mTransformation;

    for (unsigned int m = 0; m < node->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[m]];
        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            aiVector3D vtx = mesh->mVertices[v];
            vtx *= localTransform;
            vertsOut.push_back(vtx);
        }
    }

    for (unsigned int c = 0; c < node->mNumChildren; ++c) {
        collectWorldVerts(node->mChildren[c], scene, localTransform, vertsOut);
    }
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

std::vector<float> QuaternionToEulerXYZ(const aiQuaterniont<float>& q) {
    float x = q.x;
    float y = q.y;
    float z = q.z;
    float w = q.w;

    float sinr_cosp = 2.f * (w * x + y * z);
    float cosr_cosp = 1.f - 2.f * (x * x + y * y);
    float roll = std::atan2(sinr_cosp, cosr_cosp);

    float sinp = 2.f * (w * y - z * x);
    float pitch = std::abs(sinp) >= 1.f ? std::copysign(AI_MATH_PI / 2.f, sinp) : std::asin(sinp);

    float siny_cosp = 2.f * (w * z + x * y);
    float cosy_cosp = 1.f - 2.f * (y * y + z * z);
    float yaw = std::atan2(siny_cosp, cosy_cosp);

    return { roll, pitch, yaw }; // in radians
}

std::string CloneAndFixFBXASC(const std::string& originalPath) {
    std::ifstream in(originalPath);
    if (!in) {
        std::cerr << "failed to open dae for fix: " << originalPath << "\n";
        return "";
    }

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string content = buffer.str();
    in.close();

    size_t pos = 0;
    const std::string needle = "FBXASC046";
    const std::string dot = ".";

    while ((pos = content.find(needle, pos)) != std::string::npos)
        content.replace(pos, needle.length(), dot);

    std::string tempPath = originalPath + "tmp";
    std::ofstream out(tempPath);
    if (!out) {
        std::cerr << "failed to write tmp dae\n";
        return "";
    }
    out << content;
    out.close();
    //system("pause");
    return tempPath;
}

typedef void(__cdecl* NodeToSubmeshFunc)(const char*, const char** meshList, int meshCount);
void CallNodeToSubmeshFromDLL(const std::string& daePath, const std::vector<std::string>& meshList) {
    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "couldn't load tinyxml2patcher.dll\n";
        return;
    }

    NodeToSubmeshFunc func = (NodeToSubmeshFunc)GetProcAddress(dll, "NodeToSubmesh_C");
    if (!func) {
        std::cerr << "couldn't find NodeToSubmesh_C\n";
        FreeLibrary(dll);
        return;
    }

    std::vector<const char*> meshNamesCStr;
    for (const std::string& meshName : meshList)
        meshNamesCStr.push_back(meshName.c_str());

    func(daePath.c_str(), meshNamesCStr.data(), (int)meshNamesCStr.size());

    FreeLibrary(dll);
}

typedef void(__cdecl* GetBonesFunc)(const char*, const char*, char**, int, int&);
std::vector<std::string> CallGetDaeBoneNamesFromDLL(const std::string& daePath, const std::string& meshName) {
    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "couldn't load tinyxml2patcher.dll\n";
        return {};
    }

    typedef void(__cdecl* GetBonesFunc)(const char*, const char*, char**, int, int*);
    GetBonesFunc func = (GetBonesFunc)GetProcAddress(dll, "GetDaeBoneNames_C");
    if (!func) {
        std::cerr << "couldn't find GetDaeBoneNames_C\n";
        FreeLibrary(dll);
        return {};
    }

    const int maxBones = 256;
    char* outputBones[maxBones];
    for (int i = 0; i < maxBones; ++i)
        outputBones[i] = new char[256];

    int count = 0;

    //printf("calling GetDaeBoneNames_C: mesh=%s\n", meshName.c_str());

    func(daePath.c_str(), meshName.c_str(), outputBones, maxBones, &count);

    //printf("returned bone count: %d\n", count);

    std::vector<std::string> result;
    for (int i = 0; i < count; ++i)
        result.push_back(outputBones[i]);

    for (int i = 0; i < maxBones; ++i)
        delete[] outputBones[i];

    FreeLibrary(dll);
    return result;
}

std::unordered_map<std::string, int> CallGetMaterialIndicesFromDLL(const std::string& daePath) {
    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "failed to load tinyxml2patcher.dll\n";
        return {};
    }

    typedef void(__cdecl* GetMapFunc)(const char*, char**, int*, int, int*);
    GetMapFunc func = (GetMapFunc)GetProcAddress(dll, "GetMaterialIndices_C");
    if (!func) {
        std::cerr << "couldn't find GetMaterialIndices_C\n";
        FreeLibrary(dll);
        return {};
    }

    const int maxEntries = 1024;
    char* meshNames[maxEntries];
    int materialIndices[maxEntries];
    int count = 0;

    for (int i = 0; i < maxEntries; ++i)
        meshNames[i] = new char[256];

    func(daePath.c_str(), meshNames, materialIndices, maxEntries, &count);

    std::unordered_map<std::string, int> meshToMaterial;
    for (int i = 0; i < count; ++i)
        meshToMaterial[meshNames[i]] = materialIndices[i];

    for (int i = 0; i < maxEntries; ++i)
        delete[] meshNames[i];

    FreeLibrary(dll);
    return meshToMaterial;
}

typedef void(__cdecl* PatchDaePreImportFunc)(const char*, const char*);

void CallPatchDaePreImportFromDLL(const std::string& path, const std::string& groupsFile)
{
    HMODULE dll = LoadLibraryA("tinyxml2patcher.dll");
    if (!dll) {
        std::cerr << "Failed to load tinyxml2patcher.dll" << std::endl;
        return;
    }
    PatchDaePreImportFunc patchFunc = (PatchDaePreImportFunc)GetProcAddress(dll, "PatchDaePreImport_C");
    if (!patchFunc) {
        std::cerr << "Failed to find PatchDaePreImport_C in DLL" << std::endl;
        FreeLibrary(dll);
        return;
    }
    patchFunc(path.c_str(), groupsFile.c_str());
    FreeLibrary(dll);
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
    m.UnknownValues = ReadUInt32s(stream, 4);
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
    b.BoundingBox = ReadFloats(stream, 4);
    b.ModelObjectArrayOffset = ReadUInt32s(stream, 1)[0];
    b.ChildrenArrayOffset = ReadUInt32s(stream, 1)[0];
    b.MoreFloats = ReadFloats(stream, 3);
	b.AnimationVals = ReadFloats(stream, 6);
    b.BoundingBoxMaxMin = ReadFloats(stream, 6);
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

MKDXData LoadMKDXFile(std::ifstream& fs)
{
    MKDXData data;

    auto headerData = ReadHeader(fs);
    std::cout << "\nRead header: MaterialCount=" << headerData.MaterialCount << ", TextureMapsCount=" << headerData.TextureMapsCount << "\n";

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
                    std::vector<uint16_t> polys(pCount * 3);
                    fs.read(reinterpret_cast<char*>(polys.data()), polys.size() * sizeof(uint16_t));
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

    data.headerData = headerData;
    data.materialsData = materialsData;
    data.textureNames = textureNames;
    data.nodeLinks = nodeLinks;
    data.allNodeNames = allNodeNames;
    data.rootNodes = rootNodes;
    data.fullNodeDataList = fullNodeDataList;
    data.boneNames = boneNames;

    return data;
}

bool dirExists(const std::string& path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && (info.st_mode & S_IFDIR);
}

std::string MakeAbsolutePath(const std::string& path)
{
#ifdef _WIN32
    char absPath[_MAX_PATH];
    if (_fullpath(absPath, path.c_str(), _MAX_PATH))
        return std::string(absPath);
    else
        return path; // fallback if fail
#else
    char absPath[PATH_MAX];
    if (realpath(path.c_str(), absPath))
        return std::string(absPath);
    else
        return path; // fallback if fail
#endif
}

// define global logPath
std::string logPath;

// ========================================================
// ========================================================
// ====================    MAIN    ========================
// ========================================================
// ========================================================
int main(int argc, char* argv[])
{
    std::cout << "\033[34mVery epic mkagpdx dae tool\033[37m\n";
    std::cout << "Cool tool for some exports and imports\n\n";

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string pathStr(exePath);
    size_t pos = pathStr.find_last_of("\\/");
    std::string exeDir = (pos == std::string::npos) ? "." : pathStr.substr(0, pos);
    logPath = exeDir + "\\message.log";
    std::ofstream(logPath.c_str(), std::ios::trunc) << "Unspecified error, contact @blurro on discord";

    // get args
    std::string filePathInput;
    std::string outDir;
    std::string txtFilePath;
    bool mergeOn = false;

    if (argc > 1) filePathInput = argv[1];

    bool outDirEmpty = true;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "m") == 0) {
            mergeOn = true;
        }
        else {
            // if multiple outDirs passed, last one wins
            outDir = argv[i];
			outDirEmpty = false;
        }
    }
    if (outDir.empty() || !dirExists(outDir)) {

        size_t pos = filePathInput.find_last_of("/\\");
        outDir = (pos == std::string::npos) ? "." : filePathInput.substr(0, pos);
    }

    outDir = MakeAbsolutePath(outDir);

	// debug default file path
    //if (filePathInput.empty()) filePathInput = "./KP_L_R_area3.bin";
    //if (filePathInput.empty()) filePathInput = "C:/Users/Blurro/Desktop/My Stuff/Games/Emulation/MarioKartArcadeGPDX/Mario Kart DX 1.18/Data/Model/driver/rosetta/rosetta_model og_out.dae";
    //if (filePathInput.empty()) { filePathInput = "C:/Users/Blurro/source/repos/MKDXdaeconvert/x64/Debug/brian/briannn.dae"; arg2 = "C:/Users/Blurro/source/repos/MKDXdaeconvert/x64/Debug/brian/BrianPreset.txt"; }

    if (filePathInput.empty()) {
        std::cout << "Usage for dae export: Drag and drop a .bin file onto the tool (in file explorer, not this window)\nOptional add \"m\" arg to merge submeshes into full meshes\nExample cmd command 'MKDXTool mario_model.bin m'\n";
		std::cout << "\nUsage for mkdx bin file creation: Drag and drop a .dae file onto the tool, then enter your material preset path.\nOptional add material preset path arg to skip the prompt\nExample cmd command 'MKDXTool mario_model.dae MarioPreset.txt'\n\n";
        system("pause");
        return 0;
    }

    std::ifstream fs(filePathInput, std::ios::binary);
    if (!fs) {
        std::cerr << "Failed to open file: " << filePathInput << "\n";
        std::ofstream(logPath.c_str(), std::ios::trunc) << "Error: failed to open input file";
        //system("pause");
        return 1;
    }

    std::string ext = filePathInput.substr(filePathInput.find_last_of('.'));
    if (ext == ".bin")
    {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "m") == 0) {
                mergeOn = true;
				std::cout << "Merging enabled\n";
            }
        }
        // FIRE LOGO PRINT
        FireLogoPrint(56);

        MKDXData data = LoadMKDXFile(fs);

        SaveDaeFile(filePathInput, outDir, data.headerData, data.materialsData, data.textureNames, data.nodeLinks, data.allNodeNames, data.rootNodes, data.fullNodeDataList, mergeOn);
        //SaveMKDXFile(filePathInput, data.headerData, data.materialsData, data.textureNames, data.boneNames, data.nodeLinks, data.allNodeNames, data.rootNodes, data.fullNodeDataList); // debug remake file
    }
    else if (ext == ".dae")
    {
        for (int i = 2; i < argc; i++) {
            std::string arg = argv[i];
            if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".txt") {
                txtFilePath = arg;
                break;
            }
        }
        // FIRE LOGO PRINT
        FireLogoPrint(56);

        filePathInput = CloneAndFixFBXASC(filePathInput);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filePathInput, aiProcess_Triangulate);

        if (!scene || !scene->HasMeshes()) {
            std::cerr << "failed to load scene or no meshes found\n";
            std::ofstream(logPath.c_str(), std::ios::trunc) << "Error: failed to load scene or no meshes found";
            //system("pause");
            return 1;
        }

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[i];
            std::cout << "\nmesh #" << i << "\n";
            std::cout << "  name: " << (mesh->mName.length > 0 ? mesh->mName.C_Str() : "(unnamed, this'll cause errors tell @blurro)") << "\n";
            std::cout << "  vertices: " << mesh->mNumVertices << "\n";
            std::cout << "  faces: " << mesh->mNumFaces << "\n";

            if (mesh->mMaterialIndex < scene->mNumMaterials) {
                aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
                aiString matName;
                mat->Get(AI_MATKEY_NAME, matName);
                std::cout << "  material: " << matName.C_Str() << "\n";
            }
            else {
                std::cout << "  material: (invalid index)\n";
            }
        }

        std::cout << "\nExpected number of materials in preset file: " << scene->mNumMaterials << "\n";
        std::string presetPath;
        if (!txtFilePath.empty() && txtFilePath.size() >= 4 && txtFilePath.substr(txtFilePath.size() - 4) == ".txt") {
            std::ifstream testFile(txtFilePath);
            if (testFile.good()) presetPath = txtFilePath;
        }
        if (presetPath.empty()) {
			if (outDirEmpty) {
                std::cout << "Enter material preset txt file path (leave blank to generate default): ";
                std::getline(std::cin, presetPath);
			}
            else {
                std::ofstream(logPath.c_str(), std::ios::trunc) << "Error: invalid material preset txt";
                return 1;
            }
        }

        //presetPath = "RosalinaPreset.txt"; // debug path

        // try opening the file to check if it exists
        presetPath.erase(std::remove(presetPath.begin(), presetPath.end(), '\"'), presetPath.end());
        std::ifstream check(presetPath);
        if (presetPath.empty() || !check.is_open()) {
            presetPath = "ExampleMatPreset.txt";
            std::ofstream out(presetPath);
            out << "//material 1, this is Mario's\n";
            out << "#Material\n";
            out << "#DIFFUSE 0.5 0.5 0.5 1\n";
            out << "#SPECULAR 0.7 0.7 0.7 1\n";
            out << "#AMBIENCE 1 1 1 1\n";
            out << "#SHINY 1\n";
            out << "#TEXALBEDO 0\n";
            out << "#TEXSPECULAR 1\n";
            out << "#TEXREFLECTIVE -1\n";
            out << "#TEXENVIRONMENT -1\n";
            out << "#TEXNORMAL -1\n\n";

            out << "//next material starts with the same tag, make sure the amount of '#Material's matches the .dae file!\n";
            out << "//anything not stated will use default values (same values as above, but any undefined '#TEX' is defaulted to '-1')\n";
            out << "#Material\n";
            out << "#TEXALBEDO 2\n\n";

            out << "//only have one '#Textures', all following lines will be the textures the file references\n";
            out << "#Textures\n";
            out << "mario_body01_col.dds\n";
            out << "mario_body01_spe.dds\n";
            out << "material_2_albedo_example.dds\n";
            out.close();

            std::cout << "No input, generated example preset file: " << presetPath << "\n";
        }
        else {
            std::cout << "Using material preset file: " << presetPath << "\n";

            std::ifstream presetFile(presetPath);
            if (!presetFile) {
                std::cerr << "failed to open preset file: " << presetPath << "\n";
                std::ofstream(logPath.c_str(), std::ios::trunc) << "Error: failed to open preset file";
                //system("pause");
                return 1;
            }

            std::vector<MaterialPreset> materials;
            std::vector<TextureName> textureNames;
            std::vector<std::string> meshList;
            std::string line;
            int lineNumber = 0;
            bool inTextures = false;
            bool inMeshes = false;

            auto readFloats = [&](std::istringstream& s, float* dst, int count) {
                for (int i = 0; i < count; ++i) {
                    if (!(s >> dst[i])) {
                        std::cerr << "line " << lineNumber << ": expected " << count << " floats\n";
                        return false;
                    }
                }
                return true;
                };

            auto readInt = [&](std::istringstream& s, int& dst) {
                if (!(s >> dst)) {
                    std::cerr << "line " << lineNumber << ": expected 1 int\n";
                    return false;
                }
                return true;
                };

            std::map<std::string, std::array<float, 6>> animFloatMap;
            MaterialPreset currentMat;
            bool haveMaterial = false;
            try {
                while (std::getline(presetFile, line)) {
                    ++lineNumber;
                    if (line.empty()) continue;
                    if (line[0] == '/' || line[0] == ' ') continue;

                    std::istringstream iss(line);
                    std::string tag;
                    iss >> tag;

                    if (tag == "#Material") {
                        if (!materials.empty() || lineNumber != 1) {
                            materials.push_back(currentMat);
                        }
                        currentMat = MaterialPreset();
                        haveMaterial = true;
                        inTextures = false;
                        inMeshes = false;
                        continue;
                    }

                    if (tag == "#AnimFloats") {
                        std::string boneName;
                        float v0, v1, v2, v3, v4, v5;
                        if (iss >> boneName >> v0 >> v1 >> v2 >> v3 >> v4 >> v5) {
                            animFloatMap[boneName] = { v0, v1, v2, v3, v4, v5 };
                        }
                        else {
                            printf("Invalid #AnimFloats line on %d\n", lineNumber);
                        }
                        continue;
                    }

                    if (tag == "#Textures") {
                        inTextures = true;
                        inMeshes = false;
                        continue;
                    }
                    if (tag == "#Meshes") {
                        inTextures = false;
                        inMeshes = true;
                        continue;
                    }

                    // if line starts with # then not reading names anymore
                    if (inTextures) {
                        if (tag[0] == '#') inTextures = false;
                        else textureNames.push_back({ line, 0 }); // 0 for NamePointer for now
                        continue;
                    }
                    if (inMeshes) {
                        if (tag[0] == '#') inMeshes = false;
                        else meshList.push_back(line);
                        continue;
                    }

                    if (tag == "#DIFFUSE") readFloats(iss, currentMat.diffuse, 4);
                    else if (tag == "#SPECULAR") readFloats(iss, currentMat.specular, 4);
                    else if (tag == "#AMBIENCE") readFloats(iss, currentMat.ambience, 4);
                    else if (tag == "#SHINY") readFloats(iss, &currentMat.shiny, 1);
                    else if (tag == "#TEXALBEDO") readInt(iss, currentMat.texAlbedo);
                    else if (tag == "#TEXSPECULAR") readInt(iss, currentMat.texSpecular);
                    else if (tag == "#TEXREFLECTIVE") readInt(iss, currentMat.texReflective);
                    else if (tag == "#TEXENVIRONMENT") readInt(iss, currentMat.texEnvironment);
                    else if (tag == "#TEXNORMAL") readInt(iss, currentMat.texNormal);
                    else if (tag == "#UNKNOWN") readInt(iss, currentMat.unknownVal);
                    else if (tag == "#UNKNOWN2") readFloats(iss, &currentMat.unknownVal2, 1);
                    else std::cerr << "line " << lineNumber << ": unknown tag: " << tag << "\n";
                }
                if (haveMaterial) {
                    materials.push_back(currentMat);
                }
            }
            catch (const std::exception& e) {
                std::string errMsg = std::string("Exception while reading preset file at line ") + std::to_string(lineNumber) + ": " + e.what() + "\n";
                std::cerr << errMsg;
                std::ofstream(logPath.c_str(), std::ios::app) << errMsg;
                return 1;
            }

            if (textureNames.size() == 0) {
                std::cerr << "no #Textures block found\n";
            }
            if (meshList.size() == 0) {
                std::cerr << "no #Meshes block found\n";
            }
            int dummyMat = 0;
            if (materials.size() == scene->mNumMaterials + 1) {
                const auto& first = materials[0];
                if (first.texAlbedo == -1 && first.texSpecular == -1 && first.texReflective == -1 &&
                    first.texEnvironment == -1 && first.texNormal == -1) {
                    dummyMat = 1;
                    std::cout << "\nDetected and adding absent-from-dae dummy material from preset\n\n";
                }
            }
            if (materials.size() != scene->mNumMaterials + dummyMat) {
                std::string errMsg = "Error: expected " + std::to_string(scene->mNumMaterials) +
                    " materials, but loaded " + std::to_string(materials.size()) + " from preset\n";
                std::cerr << errMsg;
                std::ofstream(logPath.c_str(), std::ios::trunc) << errMsg;
                //system("pause");
                return 1;
            }
            std::cout << "loaded " << materials.size() << " materials, " << textureNames.size() << " textures, and " << meshList.size() << " meshes" << "\n";

            // func that splits meshes into submeshes based on bone counts per triangle
            CallPatchDaePreImportFromDLL(filePathInput, "final_groups.t");

            // modify dae.tmp file to treat non-listed child mesh nodes of a listed mesh node as being submeshes of that listed mesh
            CallNodeToSubmeshFromDLL(filePathInput, meshList);
            
            // reload scene
            scene = importer.ReadFile(filePathInput, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

            // assign some header data
			Header headerData;
			headerData.MaterialCount = static_cast<uint32_t>(materials.size());
			headerData.TextureMapsCount = static_cast<uint32_t>(textureNames.size());

            // load node names and retrieve counts
            int totalNodeCount = 0;
            std::vector<std::string> nonMeshNodes;
            std::vector<NodeNames> allNodeNames;
            std::vector<FullNodeData> fullNodeDataList;
            std::vector<aiNode*> stack;

            aiNode* root = scene->mRootNode;

            // always skip the first node (scene), then skip "Armature" if present
            std::vector<aiNode*> nodesToProcess;
            if (root) {
                std::cout << "Top-level root node: \"" << root->mName.C_Str() << "\" with " << root->mNumChildren << " child(ren)\n";

                aiNode* armatureNode = nullptr;
                for (unsigned int i = 0; i < root->mNumChildren; ++i) {
                    aiNode* child = root->mChildren[i];
                    std::string name = child->mName.C_Str();
                    std::cout << "  child[" << i << "] = " << name << "\n";

                    if (name == "Armature") {
                        armatureNode = child;
                        break;
                    }
                }
                if (armatureNode) {
                    std::cout << "Found \"Armature\" node with " << armatureNode->mNumChildren << " child(ren). Processing those.\n";
                    for (unsigned int i = 0; i < armatureNode->mNumChildren; ++i)
                        nodesToProcess.push_back(armatureNode->mChildren[i]);
                }
                else {
                    std::cout << "No \"Armature\" found. Processing children of Scene root directly.\n";
                    for (unsigned int i = 0; i < root->mNumChildren; ++i)
                        nodesToProcess.push_back(root->mChildren[i]);
                }
            }

            for (aiNode* n : nodesToProcess)
                stack.push_back(n);

            std::vector<aiNode*> allAiNodes; // rearranged nodes to match the order of fullNodeDataList
            while (!stack.empty()) {
                aiNode* node = stack.back();
                allAiNodes.push_back(node);
                stack.pop_back();

                totalNodeCount++;

                NodeNames entry;
                entry.Name = node->mName.C_Str();
                entry.DataOffset = static_cast<uint32_t>(allNodeNames.size()); // gives unique id to the dataoffset, needed for linking nodenames to bonenames later in the save code
                entry.NamePointer = 0;
                allNodeNames.push_back(entry);

                if (node->mNumMeshes == 0)
                    nonMeshNodes.push_back(node->mName.C_Str());

                FullNodeData fnd;
                aiVector3t<float> scale, position;
                aiQuaterniont<float> rotationQuat;
                node->mTransformation.Decompose(scale, rotationQuat, position);

                std::vector<float> rotationEuler = QuaternionToEulerXYZ(rotationQuat);

                fnd.boneData.Scale = { scale.x, scale.y, scale.z };
                fnd.boneData.Translation = { position.x, position.y, position.z };
                fnd.boneData.Rotation = { rotationEuler[0], rotationEuler[1], rotationEuler[2]};

                fullNodeDataList.push_back(fnd);

                for (int i = node->mNumChildren - 1; i >= 0; --i)
                    stack.push_back(node->mChildren[i]);
            }

            std::cout << "\ntotal nodes (excluding dummy parents): " << totalNodeCount << "\n";
            std::cout << "non-mesh nodes: " << nonMeshNodes.size() << "\n";

			// add children indices to childrenIndexList in FullNodeData
            for (size_t parentIdx = 0; parentIdx < allAiNodes.size(); ++parentIdx) {
                aiNode* parentNode = allAiNodes[parentIdx];
                for (unsigned int i = 0; i < parentNode->mNumChildren; ++i) {
                    aiNode* child = parentNode->mChildren[i];

                    // find the index of this child in allAiNodes
                    auto it = std::find(allAiNodes.begin(), allAiNodes.end(), child);
                    if (it != allAiNodes.end()) {
                        size_t childIdx = std::distance(allAiNodes.begin(), it);
                        fullNodeDataList[parentIdx].childrenIndexList.push_back(static_cast<uint32_t>(childIdx));
                    }
                }
            }

			// pick root nodes out of all nodes by checking if they are not in any childIndices
            std::vector<uint32_t> rootNodes;
            std::unordered_set<uint32_t> childIndices;
            for (const auto& fnd : fullNodeDataList) {
                for (auto childIdx : fnd.childrenIndexList)
                    childIndices.insert(childIdx);
            }
            for (uint32_t i = 0; i < fullNodeDataList.size(); ++i) {
                if (childIndices.find(i) == childIndices.end()) {
                    rootNodes.push_back(i);
                }
            }

            // debug print
            //std::cout << "\nall nodes in order:\n"; for (const auto& node : allNodeNames) std::cout << "  " << node.Name << "\n";

            // create allBoneNames vector with struct NodeNames, keeping DataOffset same as original nodes
            std::vector<NodeNames> boneNames;
            boneNames.reserve(nonMeshNodes.size());

            // first find matching DataOffset from allNodeNames for each bone name
            for (const auto& boneNameStr : nonMeshNodes) {
                auto it = std::find_if(allNodeNames.begin(), allNodeNames.end(), [&](const NodeNames& n) {
                    return n.Name == boneNameStr;
                    });
                boneNames.push_back({ it->DataOffset, it->Name, 0 });
            }
            // sort allBoneNames by Name alphabetically with capitals first
            std::sort(boneNames.begin(), boneNames.end(), [](const NodeNames& a, const NodeNames& b) {
                size_t len = a.Name.size() < b.Name.size() ? a.Name.size() : b.Name.size();
                for (size_t i = 0; i < len; ++i) {
                    unsigned char c1 = a.Name[i];
                    unsigned char c2 = b.Name[i];
                    if (c1 != c2) {
                        if (std::isupper(c1) && std::islower(c2)) return true;
                        if (std::islower(c1) && std::isupper(c2)) return false;
                        return c1 < c2;
                    }
                }
                return a.Name.size() < b.Name.size();
             });

            // debug print the sorted bone names
            //std::cout << "\nnon-mesh nodes (bones) sorted alphabetically (caps first):\n"; for (const auto& bone : allBoneNames) { std::cout << "  " << bone.Name << " (DataOffset " << bone.DataOffset << ")\n"; }

            // assign more header stuffs
            headerData.BoneCount = static_cast<uint32_t>(nonMeshNodes.size());
            headerData.TotalNodeCount = static_cast<uint32_t>(totalNodeCount);

			// turn material presets into materialsData list
            std::vector<Material> materialsData;
            materialsData.reserve(materials.size());
            for (const auto& mat : materials) {
                Material m;
                m.Diffuse.assign(mat.diffuse, mat.diffuse + 4);
                m.Specular.assign(mat.specular, mat.specular + 4);
                m.Ambience.assign(mat.ambience, mat.ambience + 4);
                m.Shiny = mat.shiny;
                m.TextureIndices[0] = static_cast<int16_t>(mat.texAlbedo);
                m.TextureIndices[1] = static_cast<int16_t>(mat.texSpecular);
                m.TextureIndices[2] = static_cast<int16_t>(mat.texReflective);
                m.TextureIndices[3] = static_cast<int16_t>(mat.texEnvironment);
                m.TextureIndices[4] = static_cast<int16_t>(mat.texNormal);
				m.Unknowns[0] = mat.unknownVal; // will try find out about it another time
                m.Unknowns2[3] = mat.unknownVal2;
                materialsData.push_back(m);
            }

            std::vector<NodeLinks> nodeLinks;
            uint32_t totalLinksCount = 0;

            // create sorted index view
            std::vector<size_t> sortedIndices(allAiNodes.size());
            for (size_t i = 0; i < sortedIndices.size(); ++i)
                sortedIndices[i] = i;

            // sort the indices based on allAiNodes names
            for (size_t i = 0; i < sortedIndices.size(); ++i) {
                for (size_t j = i + 1; j < sortedIndices.size(); ++j) {
                    if (strcmp(allAiNodes[sortedIndices[i]]->mName.C_Str(), allAiNodes[sortedIndices[j]]->mName.C_Str()) > 0)
                        std::swap(sortedIndices[i], sortedIndices[j]);
                }
            }

            // fix material index on meshes (assimp loads in order of first used, not dae order)
            auto meshMaterialMap = CallGetMaterialIndicesFromDLL(filePathInput);
            for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
                aiMesh* mesh = scene->mMeshes[i];
                auto found = meshMaterialMap.find(mesh->mName.C_Str());
                if (found != meshMaterialMap.end()) {
                    std::cout << "  overriding material index of mesh " << mesh->mName.C_Str() << " to " << found->second << "\n";
                    scene->mMeshes[i]->mMaterialIndex = found->second;
                }
            }

            // loop in sorted order using the index indirection
            for (size_t sortedIndex = 0; sortedIndex < sortedIndices.size(); ++sortedIndex) {
                size_t originalIndex = sortedIndices[sortedIndex];
                aiNode* node = allAiNodes[originalIndex];

                auto& fullNode = fullNodeDataList[originalIndex];
                // find uniqueBoneIndices from all bones in meshes of this node (order from daeBoneList will reorder them later)
                std::vector<uint32_t> uniqueBoneIndices;
                if (node->mNumMeshes > 0) {
                    for (unsigned int meshIdx = 0; meshIdx < node->mNumMeshes; ++meshIdx) {
                        aiMesh* mesh = scene->mMeshes[node->mMeshes[meshIdx]];
                        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                            std::string boneName = mesh->mBones[b]->mName.C_Str();
                            for (size_t boneIdx = 0; boneIdx < allAiNodes.size(); ++boneIdx) {
                                if (boneName == allAiNodes[boneIdx]->mName.C_Str()) {
                                    if (std::find(uniqueBoneIndices.begin(), uniqueBoneIndices.end(), static_cast<uint32_t>(boneIdx)) == uniqueBoneIndices.end())
                                        uniqueBoneIndices.push_back(static_cast<uint32_t>(boneIdx));
                                    break;
                                }
                            }
                        }
                    }

                    // get daeBoneList order for this node’s submeshes (concatenate all daeBoneLists for submeshes)
                    std::vector<std::string> daeBoneListCombined;
                    for (unsigned int meshIdx = 0; meshIdx < node->mNumMeshes; ++meshIdx) {
                        uint32_t meshIndex = node->mMeshes[meshIdx];
                        aiMesh* mesh = scene->mMeshes[meshIndex];
                        auto daeBoneList = CallGetDaeBoneNamesFromDLL(filePathInput, mesh->mName.C_Str());

                        daeBoneListCombined.insert(daeBoneListCombined.end(), daeBoneList.begin(), daeBoneList.end());
                    }

                    // reorder uniqueBoneIndices to follow order in daeBoneListCombined
                    std::vector<uint32_t> reorderedUniqueBoneIndices;
                    for (const auto& boneName : daeBoneListCombined) {
                        for (auto it = uniqueBoneIndices.begin(); it != uniqueBoneIndices.end(); ++it) {
                            if (allAiNodes[*it]->mName.C_Str() == boneName) {
                                reorderedUniqueBoneIndices.push_back(*it);
                                uniqueBoneIndices.erase(it);
                                break;
                            }
                        }
                    }
                    uniqueBoneIndices = std::move(reorderedUniqueBoneIndices);

                    // cap skinnedCount at 6 max or total bones count
                    uint32_t skinnedCount = uniqueBoneIndices.size() > 6 ? 6 : static_cast<uint32_t>(uniqueBoneIndices.size());
                    // build default mask: first skinnedCount bits set to 1, rest 0
                    uint32_t defaultMask = (1u << skinnedCount) - 1;

                    NodeLinks link;
                    link.MeshOffset = originalIndex;
                    link.BoneOffsets = uniqueBoneIndices;
                    totalLinksCount += static_cast<uint32_t>(uniqueBoneIndices.size());

                    fullNode.subMeshes.resize(node->mNumMeshes);
                    for (size_t s = 0; s < fullNode.subMeshes.size(); ++s) {
                        uint32_t meshIndex = node->mMeshes[s];
                        aiMesh* mesh = scene->mMeshes[meshIndex];

                        uint32_t mask = defaultMask;
                        std::vector<uint32_t> availableDefaults;
                        for (uint32_t i = 0; i < skinnedCount; ++i)
                            availableDefaults.push_back(i);

                        std::cout << "processing mesh: " << mesh->mName.C_Str() << "\n";

                        for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                            std::string boneName = mesh->mBones[b]->mName.C_Str();

                            auto it = std::find_if(uniqueBoneIndices.begin(), uniqueBoneIndices.end(),
                                [&](uint32_t idx) { return allAiNodes[idx]->mName.C_Str() == boneName; });
                            if (it == uniqueBoneIndices.end())
                                continue;

                            uint32_t boneIdx = static_cast<uint32_t>(std::distance(uniqueBoneIndices.begin(), it));
                            uint32_t bit = 1u << boneIdx;

                            if (mask & bit) {
                                auto defIt = std::find(availableDefaults.begin(), availableDefaults.end(), boneIdx);
                                if (defIt != availableDefaults.end())
                                    availableDefaults.erase(defIt);
                                continue;
                            }

                            if (!availableDefaults.empty()) {
                                uint32_t defaultBitIdx = availableDefaults.back();
                                availableDefaults.pop_back();

                                mask &= ~(1u << defaultBitIdx);
                                mask |= bit;
                            }
                        }

                        //std::cout << "final mask bits: "; for (size_t i = 0; i < uniqueBoneIndices.size(); ++i) std::cout << ((mask & (1u << i)) ? '1' : '0') << "\n";

                        fullNode.subMeshes[s].BonesIndexMask = mask;
                        fullNode.subMeshes[s].SkinnedBonesCount = skinnedCount;
                        fullNode.subMeshes[s].TriangleCount = mesh->mNumFaces;
                        fullNode.subMeshes[s].MaterialIndex = mesh->mMaterialIndex + dummyMat;
                        fullNode.subMeshes[s].VertexCount = mesh->mNumVertices;

                        std::vector<float> verts, norms, cols, uv0, uv1, uv2, uv3;
                        std::vector<uint16_t> indices;

                        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                            verts.push_back(mesh->mVertices[v].x);
                            verts.push_back(mesh->mVertices[v].y);
                            verts.push_back(mesh->mVertices[v].z);
                        }
                        fullNode.subMeshes[s].VertexPositionOffset = 1;

                        if (mesh->HasNormals()) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                norms.push_back(mesh->mNormals[v].x);
                                norms.push_back(mesh->mNormals[v].y);
                                norms.push_back(mesh->mNormals[v].z);
                            }
                            fullNode.subMeshes[s].VertexNormalOffset = 1;
                        }

                        // if vertex colouring is just all 1, 1, 1, 1 skip it
                        bool allWhite = true;
                        if (mesh->HasVertexColors(0) && mesh->mColors[0]) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                auto& c = mesh->mColors[0][v];
                                if (c.r != 1.f || c.g != 1.f || c.b != 1.f || c.a != 1.f) {
                                    allWhite = false;
                                    break;
                                }
                            }
                        }
                        else {
                            allWhite = false; // or true depending on what you wanna assume if there's no color data
                        }
                        if (!allWhite && mesh->HasVertexColors(0) && mesh->mColors[0]) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                cols.push_back(mesh->mColors[0][v].r);
                                cols.push_back(mesh->mColors[0][v].g);
                                cols.push_back(mesh->mColors[0][v].b);
                                cols.push_back(mesh->mColors[0][v].a);
                            }
                            fullNode.subMeshes[s].ColorBufferOffset = 1;
                        }
                        else {
                            fullNode.subMeshes[s].ColorBufferOffset = 0;
                        }

                        if (mesh->HasTextureCoords(0)) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                uv0.push_back(mesh->mTextureCoords[0][v].x);
                                uv0.push_back(mesh->mTextureCoords[0][v].y);
                            }
                            fullNode.subMeshes[s].TexCoord0Offset = 1;
                        }
                        if (mesh->HasTextureCoords(1)) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                uv1.push_back(mesh->mTextureCoords[1][v].x);
                                uv1.push_back(mesh->mTextureCoords[1][v].y);
                            }
                            fullNode.subMeshes[s].TexCoord1Offset = 1;
                        }
                        if (mesh->HasTextureCoords(2)) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                uv2.push_back(mesh->mTextureCoords[2][v].x);
                                uv2.push_back(mesh->mTextureCoords[2][v].y);
                            }
                            fullNode.subMeshes[s].TexCoord2Offset = 1;
                        }
                        if (mesh->HasTextureCoords(3)) {
                            for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
                                uv3.push_back(mesh->mTextureCoords[3][v].x);
                                uv3.push_back(mesh->mTextureCoords[3][v].y);
                            }
                            fullNode.subMeshes[s].TexCoord3Offset = 1;
                        }

                        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                            const aiFace& face = mesh->mFaces[f];
                            for (unsigned int i = 0; i < face.mNumIndices; ++i)
                                indices.push_back(static_cast<uint16_t>(face.mIndices[i]));
                        }
                        fullNode.subMeshes[s].FaceOffset = 1;

                        // write all weights for this submesh
                        std::vector<float> weightsForThisMesh;
                        for (size_t i = 0; i < uniqueBoneIndices.size(); ++i) {
                            if (!(mask & (1u << i))) continue;

                            uint32_t boneNodeIndex = uniqueBoneIndices[i];
                            const char* targetBoneName = allAiNodes[boneNodeIndex]->mName.C_Str();

                            aiBone* aiBonePtr = nullptr;
                            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                                if (std::strcmp(mesh->mBones[b]->mName.C_Str(), targetBoneName) == 0) {
                                    aiBonePtr = mesh->mBones[b];
                                    break;
                                }
                            }

                            if (!aiBonePtr) {
                                weightsForThisMesh.insert(weightsForThisMesh.end(), mesh->mNumVertices, 0.0f);
                                continue;
                            }

                            std::vector<float> boneWeights(mesh->mNumVertices, 0.0f);
                            for (unsigned int w = 0; w < aiBonePtr->mNumWeights; ++w) {
                                unsigned int vertexId = aiBonePtr->mWeights[w].mVertexId;
                                if (vertexId < mesh->mNumVertices)
                                    boneWeights[vertexId] = aiBonePtr->mWeights[w].mWeight;
                            }
                            weightsForThisMesh.insert(weightsForThisMesh.end(), boneWeights.begin(), boneWeights.end());
                        }

                        fullNode.subMeshes[s].WeightOffset = weightsForThisMesh.empty() ? 0 : 1;
                        fullNode.weightsList.push_back(std::move(weightsForThisMesh));

                        // bounding box calc
                        aiMatrix4x4 nodeTransform = node->mTransformation;
                        aiNode* current = node->mParent;
                        while (current) {
                            nodeTransform = current->mTransformation * nodeTransform;
                            current = current->mParent;
                        }

                        std::vector<aiVector3D> worldVerts;
                        for (size_t v = 0; v < verts.size(); v += 3) {
                            aiVector3D worldPos(verts[v], verts[v + 1], verts[v + 2]);
                            worldPos *= nodeTransform;
                            worldVerts.push_back(worldPos);
                        }

                        float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
                        float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
                        for (const auto& v : worldVerts) {
                            if (v.x > maxX) maxX = v.x;
                            if (v.y > maxY) maxY = v.y;
                            if (v.z > maxZ) maxZ = v.z;
                            if (v.x < minX) minX = v.x;
                            if (v.y < minY) minY = v.y;
                            if (v.z < minZ) minZ = v.z;
                        }
                        fullNode.subMeshes[s].BoundingBoxMaxMin[0] = maxX;
                        fullNode.subMeshes[s].BoundingBoxMaxMin[1] = maxY;
                        fullNode.subMeshes[s].BoundingBoxMaxMin[2] = maxZ;
                        fullNode.subMeshes[s].BoundingBoxMaxMin[3] = minX;
                        fullNode.subMeshes[s].BoundingBoxMaxMin[4] = minY;
                        fullNode.subMeshes[s].BoundingBoxMaxMin[5] = minZ;

                        aiVector3D center(0, 0, 0);
                        for (const auto& v : worldVerts) center += v;
                        center /= (float)worldVerts.size();

                        float radius = 0.0f;
                        for (const auto& v : worldVerts) {
                            float distSq = (v - center).SquareLength();
                            if (distSq > radius) radius = distSq;
                        }
                        radius = std::sqrt(radius);

                        fullNode.subMeshes[s].BoundingBox[0] = center.x;
                        fullNode.subMeshes[s].BoundingBox[1] = center.y;
                        fullNode.subMeshes[s].BoundingBox[2] = center.z;
                        fullNode.subMeshes[s].BoundingBox[3] = radius;

                        fullNode.verticesList.push_back(std::move(verts));
                        fullNode.normalsList.push_back(std::move(norms));
                        fullNode.colorsList.push_back(std::move(cols));
                        fullNode.uvs0List.push_back(std::move(uv0));
                        fullNode.uvs1List.push_back(std::move(uv1));
                        fullNode.uvs2List.push_back(std::move(uv2));
                        fullNode.uvs3List.push_back(std::move(uv3));
                        fullNode.polygonsList.push_back(std::move(indices));
                    }
                    nodeLinks.push_back(link);
                }

                // write 'animfloatmap' stuff from the txt preset
                for (std::map<std::string, std::array<float, 6>>::const_iterator it = animFloatMap.begin(); it != animFloatMap.end(); ++it) {
                    const std::string& boneName = it->first;
                    const std::array<float, 6>& vals = it->second;

                    for (size_t i = 0; i < allNodeNames.size(); ++i) {
                        if (allNodeNames[i].Name == boneName) {
                            for (int j = 0; j < 6; ++j)
                                fullNodeDataList[i].boneData.AnimationVals[j] = vals[j];
                            break;
                        }
                    }
                }

				// get global bounding box for this node
                std::vector<aiVector3D> allWorldVerts;
                collectWorldVerts(node, scene, aiMatrix4x4(), allWorldVerts);

                if (!allWorldVerts.empty()) {
                    aiVector3D center(0, 0, 0);
                    for (const auto& v : allWorldVerts) center += v;
                    center /= (float)allWorldVerts.size();

                    float radius = 0.0f;
                    for (const auto& v : allWorldVerts) {
                        float distSq = (v - center).SquareLength();
                        if (distSq > radius) radius = distSq;
                    }
                    radius = std::sqrt(radius);

                    fullNode.boneData.BoundingBox[0] = center.x;
                    fullNode.boneData.BoundingBox[1] = center.y;
                    fullNode.boneData.BoundingBox[2] = center.z;
                    fullNode.boneData.BoundingBox[3] = radius;

                    //printf("full world center (node + children): %f %f %f\n", center.x, center.y, center.z);
                    //printf("full world radius (node + children): %f\n", radius);

                    float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
                    float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;

                    for (const auto& v : allWorldVerts) {
                        if (v.x > maxX) maxX = v.x;
                        if (v.y > maxY) maxY = v.y;
                        if (v.z > maxZ) maxZ = v.z;
                        if (v.x < minX) minX = v.x;
                        if (v.y < minY) minY = v.y;
                        if (v.z < minZ) minZ = v.z;
                    }

                    fullNode.boneData.BoundingBoxMaxMin[0] = maxX;
                    fullNode.boneData.BoundingBoxMaxMin[1] = maxY;
                    fullNode.boneData.BoundingBoxMaxMin[2] = maxZ;
                    fullNode.boneData.BoundingBoxMaxMin[3] = minX;
                    fullNode.boneData.BoundingBoxMaxMin[4] = minY;
                    fullNode.boneData.BoundingBoxMaxMin[5] = minZ;
                }
                else {
                    fullNode.boneData.BoundingBox[0] = 0.f;
                    fullNode.boneData.BoundingBox[1] = 0.f;
                    fullNode.boneData.BoundingBox[2] = 0.f;
                    fullNode.boneData.BoundingBox[3] = 0.f;
                    //printf("no vertices found in node subtree\n");
                }

                //std::cout << "node " << sortedIndex << " (" << node->mName.C_Str() << ") bones: ";
                //for (auto b : uniqueBoneIndices) std::cout << b << " " << "\n";
            }

            // set total links count in header
            headerData.LinkNodeCount = totalLinksCount;

            check.close();

			//std::cout << outDir << " is the output directory\n";
            SaveMKDXFile(filePathInput, outDir, headerData, materialsData, textureNames, boneNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList);
            std::remove(filePathInput.c_str()); // remove tmp file
        }
	}
    else {
	    std::cerr << "eyyooo mai bruther, '" << ext << "' this file idk wtf are this welcome to the thanos world\n";
		std::ofstream(logPath.c_str(), std::ios::trunc) << "Error: unsupported file extension '" + ext + "'";
	    //system("pause");
	    return 1;
	}

    //system("pause");
    return 0;
}