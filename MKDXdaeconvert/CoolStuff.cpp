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
// ========================================================
// ====================    MAIN    ========================
// ========================================================
// ========================================================
int main(int argc, char* argv[])
{
    std::cout << "\033[34mVery epic mkagpdx dae tool\033[37m\n";
    std::cout << "Cool tool for some exports and soon some imports too\n\n";

    // get exe directory
    std::string filePathInput;
    std::string arg2;
    if (argc > 1) filePathInput = argv[1];
    if (argc > 2) arg2 = argv[2];

	// debug default file path
    //if (filePathInput.empty()) filePathInput = "./rosetta_model.bin";
	//if (filePathInput.empty()) filePathInput = "./rosetta_model_out_nomerge.dae"; arg2 = "./RosalinaPreset.txt";

    if (filePathInput.empty()) {
        std::cout << "Usage for dae export: Drag and drop a .bin file onto the tool (in file explorer, not this window)\nOptional add \"n\" arg to skip merging the submeshes into full meshes\nExample cmd command 'MKDXTool mario_model.bin n'\n";
		std::cout << "\nUsage for mkdx bin file creation: Drag and drop a .dae file onto the tool, then enter your material preset path.\nOptional add material preset path arg to skip the prompt\nExample cmd command 'MKDXTool mario_model.dae mario_materials.txt'\n\n";
        system("pause");
        return 0;
    }

    std::ifstream fs(filePathInput, std::ios::binary);
    if (!fs) {
        std::cerr << "Failed to open file: " << filePathInput << "\n";
        system("pause");
        return 1;
    }

    std::string ext = filePathInput.substr(filePathInput.find_last_of('.'));
    if (ext == ".bin")
    {
        // FIRE LOGO PRINT
        FireLogoPrint(56);

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

        SaveDaeFile(filePathInput, headerData, materialsData, textureNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList, arg2 != "n");
        //SaveMKDXFile(filePathInput, headerData, materialsData, textureNames, boneNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList); // debug remake file
    }
    else if (ext == ".dae")
    {
        // FIRE LOGO PRINT
        FireLogoPrint(56);

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filePathInput, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);

        if (!scene || !scene->HasMeshes()) {
            std::cerr << "failed to load scene or no meshes found\n";
            system("pause");
            return 1;
        }

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[i];
            std::cout << "\nmesh #" << i << "\n";
            std::cout << "  name: " << (mesh->mName.length > 0 ? mesh->mName.C_Str() : "(unnamed)") << "\n";
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
        if (!arg2.empty() && arg2.size() >= 4 && arg2.substr(arg2.size() - 4) == ".txt") {
            std::ifstream testFile(arg2);
            if (testFile.good()) presetPath = arg2;
        }
        if (presetPath.empty()) {
            std::cout << "Enter material preset txt file path (leave blank to generate default): ";
            std::getline(std::cin, presetPath);
        }

        //presetPath = "RosalinaPreset.txt"; // debug path

        // try opening the file to check if it exists
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
                system("pause");
                return 1;
            }

            std::vector<MaterialPreset> materials;
            std::vector<TextureName> textureNames;

            std::string line;
            int lineNumber = 0;
            bool inTextures = false;

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

            MaterialPreset currentMat;
            while (std::getline(presetFile, line)) {
                ++lineNumber;
                if (line.empty()) continue;
                if (line[0] == '/' || line.substr(0, 2) == "//") continue;

                std::istringstream iss(line);
                std::string tag;
                iss >> tag;

                if (tag == "#Material") {
                    if (!materials.empty() || lineNumber != 1) {
                        materials.push_back(currentMat);
                    }
                    currentMat = MaterialPreset();
                    continue;
                }

                if (tag == "#Textures") {
                    materials.push_back(currentMat);
                    inTextures = true;
                    continue;
                }

                if (inTextures) {
                    if (tag[0] == '#') break;
                    textureNames.push_back({ line, 0 }); // 0 for NamePointer for now
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
                else std::cerr << "line " << lineNumber << ": unknown tag: " << tag << "\n";
            }
            if (!inTextures) {
                std::cerr << "no #Textures block found\n";
            }
            else {
                if (materials.size() != scene->mNumMaterials) {
                    std::cerr << "error: expected " << scene->mNumMaterials << " materials, but loaded " << materials.size() << " from preset\n";
                    system("pause");
                    return 1;
                }
                std::cout << "loaded " << materials.size() << " materials and " << textureNames.size() << " textures\n";
            }

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

            // skip dummy parents named "Scene" or "Armature"
            if (root && (std::string(root->mName.C_Str()) == "Scene" || std::string(root->mName.C_Str()) == "Armature")) {
                if (root->mNumChildren > 0)
                    root = root->mChildren[0];
                if (root && (std::string(root->mName.C_Str()) == "Scene" || std::string(root->mName.C_Str()) == "Armature")) {
                    if (root->mNumChildren > 0)
                        root = root->mChildren[0];
                }
            }

			std::vector<aiNode*> allAiNodes; // rearranged nodes to match the order of fullNodeDataList
            stack.push_back(root);

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
                materialsData.push_back(m);
            }



            // for every mesh, we will look at every submesh, get all bones that influence it, from this get the full list of unique bones influencing the mesh overall, the count of this list is the amount of bones that every submesh will be selecting from the start of its own list of bones (whether they contain weights or not)

            // split meshes into submeshes
            std::vector<NodeLinks> nodeLinks;

            for (size_t nodeIndex = 0; nodeIndex < allAiNodes.size(); ++nodeIndex) {
                aiNode* node = allAiNodes[nodeIndex];
                if (node->mNumMeshes == 0) continue;

                std::cout << "node " << nodeIndex << " has " << node->mNumMeshes << " submeshes\n";

                for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
                    uint32_t meshIndex = node->mMeshes[i];
                    aiMesh* mesh = scene->mMeshes[meshIndex];

                    std::cout << "  submesh " << i
                        << ": vertices = " << mesh->mNumVertices
                        << ", triangles = " << mesh->mNumFaces
                        << ", bones = " << mesh->mNumBones
                        << "\n";
                }
            }




            // write headerdata link nodes count after splitting meshes into submeshes and ensuring a max of 6 each
            

            check.close();

            SaveMKDXFile(filePathInput, headerData, materialsData, textureNames, boneNames, nodeLinks, allNodeNames, rootNodes, fullNodeDataList);
        }
	}
    else {
	    std::cerr << "eyyooo mai bruther, '" << ext << "' this file idk wtf are this welcome to the thanos world\n";
	    system("pause");
	    return 1;
	}

    system("pause");
    return 0;
}