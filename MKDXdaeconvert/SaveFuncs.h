#pragma once

#include <string>
#include <vector>
#include "CoolStructs.h"

void SaveMKDXFile(const std::string& path, const std::string& outDir, Header& headerData, std::vector<Material>& materialsData,
    std::vector<TextureName>& textureNames, std::vector<NodeNames>& boneNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList);

void SaveDaeFile(const std::string& path, const std::string& outDir, Header& headerData, std::vector<Material>& materialsData, std::vector<TextureName>& textureNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList, const bool mergeSubmeshes);

MKDXData LoadMKDXFile(std::ifstream& fs);

extern std::string logPath;
extern std::string exeDir;