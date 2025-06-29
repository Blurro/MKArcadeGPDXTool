#pragma once

struct Header {
    uint16_t Type = 0;
    uint16_t Unknown = 0;
    uint32_t Alignment = 0;
    uint32_t Padding = 0;
    uint32_t MaterialCount = 0;
    uint32_t MaterialArrayOffset = 0;
    uint32_t TextureMapsCount = 0;
    uint32_t TextureNameArrayOffset = 0;
    uint32_t BoneCount = 0;
    uint32_t BoneNameArrayOffset = 0;
    uint32_t RootNodeArrayOffset = 0;
    uint32_t LinkNodeCount = 0;
    uint32_t LinkNodeOffset = 0;
    uint32_t TotalNodeCount = 0;
    uint32_t TotalNodeArrayOffset = 0;
    uint32_t Padding2 = 0;
};

struct Material {
    std::vector<uint32_t> Unknowns = std::vector<uint32_t>(6, 0);
    std::vector<float> UnknownValues = std::vector<float>(4, 0.f);
    std::vector<float> Diffuse = std::vector<float>(4, 0.f);
    std::vector<float> Specular = std::vector<float>(4, 0.f);
    std::vector<float> Ambience = std::vector<float>(4, 0.f);
    float Shiny = 0.f;
    std::vector<float> Unknowns2 = std::vector<float>(19, 0.f);
    std::vector<int16_t> TextureIndices = std::vector<int16_t>(6, 0);
};

struct BoneData {
    uint32_t Visibility = 0;
    std::vector<float> Scale = std::vector<float>(3, 0.f);
    std::vector<float> Rotation = std::vector<float>(3, 0.f);
    std::vector<float> Translation = std::vector<float>(3, 0.f);
    std::vector<uint32_t> BoundingBox = std::vector<uint32_t>(4, 0); // xyz centre then radius
    uint32_t ModelObjectArrayOffset = 0;
    uint32_t ChildrenArrayOffset = 0;
    std::vector<float> MoreFloats = std::vector<float>(3, 0.f);
    std::vector<uint32_t> Unknowns2 = std::vector<uint32_t>(12, 0); // first 6 are 0s next 6 are bounding box xyz max and xyz min
};

struct SubMesh {
    uint32_t Padding = 0;
    uint32_t TriangleCount = 0;
    uint32_t MaterialIndex = 0;
    std::vector<float> BoundingBox = std::vector<float>(4, 0.f);
    uint32_t VertexCount = 0;
    uint32_t VertexPositionOffset = 0;
    uint32_t VertexNormalOffset = 0;
    uint32_t ColorBufferOffset = 0;
    uint32_t TexCoord0Offset = 0;
    uint32_t TexCoord1Offset = 0;
    uint32_t TexCoord2Offset = 0;
    uint32_t TexCoord3Offset = 0;
    uint32_t FaceOffset = 0;
    uint32_t SkinnedBonesCount = 0;
    uint32_t BonesIndexMask = 0;
    uint32_t WeightOffset = 0;
    std::vector<float> BoundingBoxMaxMin = std::vector<float>(6, 0.f);
};

struct FullNodeData {
    BoneData boneData;
    std::vector<SubMesh> subMeshes;
    std::vector<uint32_t> childrenIndexList;
    std::vector<std::vector<float>> verticesList;
    std::vector<std::vector<float>> normalsList;
    std::vector<std::vector<float>> colorsList;
    std::vector<std::vector<float>> uvs0List;
    std::vector<std::vector<float>> uvs1List;
    std::vector<std::vector<float>> uvs2List;
    std::vector<std::vector<float>> uvs3List;
    std::vector<std::vector<int16_t>> polygonsList;
    std::vector<std::vector<float>> weightsList;
};

struct NodeNames {
    uint32_t DataOffset;
    std::string Name;
    uint32_t NamePointer;
};

struct TextureName {
    std::string Name;
    uint32_t NamePointer;
};

struct NodeLinks {
    uint32_t MeshOffset;
    std::vector<uint32_t> BoneOffsets;
};

struct Vec3 {
    float x, y, z;
};