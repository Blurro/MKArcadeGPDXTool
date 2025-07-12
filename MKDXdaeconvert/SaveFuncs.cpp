#include "SaveFuncs.h"
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

void PatchDaeFile(const std::string& filePath, const std::vector<std::vector<std::pair<unsigned int, std::vector<unsigned int>>>>& allMaterialToIndices) {
    std::ifstream in(filePath);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();

    std::string content = buffer.str();

    // delete unit line else maya will import at scale 100, or blender at 100x smaller, they just dont agree
    EraseWholeLineContaining(content, "<unit name=\"meter\" meter=\"1\" />");

    // contributor block (insert blurro & mkdxtool above assimp block)
    size_t contribPos = content.find("<contributor>");
    if (contribPos != std::string::npos) {
        std::string newContrib =
            "<contributor>\n"
            "      <author>Blurro</author>\n"
            "      <authoring_tool>MKDXtool</authoring_tool>\n"
            "    </contributor>\n    ";

        // find end of existing contributor block to insert new one before
        size_t endContribPos = content.find("</contributor>", contribPos);
        if (endContribPos != std::string::npos) {
            // insert new contributor before existing one
            content.insert(contribPos, newContrib);
        }
    }

    // below is for adding materials to a mesh cus assimp can only do 1 mat per mesh

    // turn polylist block to triangles
    size_t searchPos = 0;
    while ((searchPos = content.find("<polylist", searchPos)) != std::string::npos) {
        size_t lineEnd = content.find('>', searchPos);
        if (lineEnd == std::string::npos) break;

        // grab and modify line
        std::string originalLine = content.substr(searchPos, lineEnd - searchPos + 1);
        std::string modifiedLine = originalLine;
        size_t polylistNamePos = modifiedLine.find("polylist");
        if (polylistNamePos != std::string::npos)
            modifiedLine.replace(polylistNamePos, strlen("polylist"), "triangles");

        //std::cout << modifiedLine << std::endl;

        content.replace(searchPos, lineEnd - searchPos + 1, modifiedLine);
        searchPos += modifiedLine.length();

        // nuke <vcount> line
        size_t vcountPos = content.find("<vcount>", searchPos);
        if (vcountPos != std::string::npos) {
            size_t vcountEnd = content.find("</vcount>", vcountPos);
            if (vcountEnd != std::string::npos)
                EraseWholeLineContaining(content, "<vcount>", searchPos);
        }

        // fix closing tag
        size_t closeTagPos = content.find("</polylist>", searchPos);
        if (closeTagPos != std::string::npos)
            content.replace(closeTagPos, strlen("</polylist>"), "</triangles>");
    }

    // split triangles block per material

    size_t filePos = 0;
    for (size_t meshIdx = 0; meshIdx < allMaterialToIndices.size(); meshIdx++) {
        int processedMats = 0;
        // find next <geometry
        size_t geoStart = content.find("<geometry", filePos);
        if (geoStart == std::string::npos) break;

		// get geometry id
        std::string geometryId;
        size_t idPos = content.find("id=\"", geoStart);
        if (idPos != std::string::npos) {
            size_t startQuote = idPos + 4;
            size_t endQuote = content.find('"', startQuote);
            if (endQuote != std::string::npos) {
                geometryId = content.substr(startQuote, endQuote - startQuote);
                //std::cout << "geometry id: " << geometryId << std::endl;
            }
        }

        // find next <triangles inside geometry
        size_t triStart = content.find("<triangles", geoStart);
        size_t lineStart = content.rfind('\n', triStart);
        if (lineStart == std::string::npos) lineStart = 0;
        else lineStart++; // move past the '\n'
        triStart = lineStart;

        // fetch the updated triangles block after erase
        size_t triEnd = content.find("</triangles>", triStart);
        if (triEnd == std::string::npos) break;
        triEnd += strlen("</triangles>"); // include closing tag

        // get material count for this mesh
        size_t matCount = allMaterialToIndices[meshIdx].size();
        // skip duplicating if only 1 mat
        if (matCount <= 1) { // debug change number to high to skip all
            filePos = triEnd; // move forward past this triangles block so next loop can find next geometry
            continue;
        }

        // yeet p line but keep entering <p> tag
        size_t pStart = content.find("<p>", triStart);
        if (pStart != std::string::npos) {
            size_t pEnd = content.find("</p>", pStart);
            if (pEnd != std::string::npos) {
                // erase from just after <p> (pStart + 3) to after </p> (pEnd + 4)
                content.erase(pStart + 3, (pEnd + 4) - (pStart + 3));
            }
        }

        triEnd = content.find("</triangles>", triStart);
        if (triEnd == std::string::npos) break;
        triEnd += strlen("</triangles>");
        filePos = triEnd;

        std::string triBlock = content.substr(triStart, triEnd - triStart);

        // prepare new triangles blocks concatenated
        std::string newTrianglesBlocks;

        for (size_t matIdx = 0; matIdx < matCount; matIdx++) {
            std::string duplicatedBlock = triBlock;

            // print material index to console
            //std::cout << "mesh " << meshIdx << " material index: " << allMaterialToIndices[meshIdx][matIdx].first << std::endl;

            // replace count attribute in <triangles> tag
            size_t countPos = duplicatedBlock.find("count=");
            if (countPos != std::string::npos) {
                size_t quoteStart = duplicatedBlock.find('"', countPos);
                size_t quoteEnd = duplicatedBlock.find('"', quoteStart + 1);
                if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
                    duplicatedBlock.replace(quoteStart + 1, quoteEnd - quoteStart - 1, std::to_string(allMaterialToIndices[meshIdx][matIdx].second.size() / 3));
            }
            // replace material attribute in <triangles> tag
            size_t matAttrPos = duplicatedBlock.find("material=");
            if (matAttrPos != std::string::npos) {
                size_t quoteStart = duplicatedBlock.find('"', matAttrPos);
                size_t quoteEnd = duplicatedBlock.find('"', quoteStart + 1);
                if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
                    duplicatedBlock.replace(quoteStart + 1, quoteEnd - quoteStart - 1, "defaultMaterial" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first));
            }

			// add data after <p> tag with indices for this material
            size_t pPos = duplicatedBlock.find("<p>");
            if (pPos != std::string::npos) {
                pPos += 3; // after "<p>"

                std::string indicesStr;
                for (auto idx : allMaterialToIndices[meshIdx][matIdx].second)
                    indicesStr += std::to_string(idx) + " ";
                if (!indicesStr.empty()) indicesStr.pop_back(); // trim trailing space

                duplicatedBlock.insert(pPos, indicesStr + "</p>");
            }

            newTrianglesBlocks += duplicatedBlock;
            if (matIdx != matCount - 1) newTrianglesBlocks += "\n"; // add newline between blocks except last

            processedMats++;
        }

        // replace original triangles block with new concatenated ones
        content.replace(triStart, triEnd - triStart, newTrianglesBlocks);

        // update filePos so next search starts after the new blocks
        filePos = triStart + newTrianglesBlocks.length();

		// find instance_geometry line and patch it with extra material
        size_t searchPos = filePos;
        std::string searchStr = "url=\"#" + geometryId + "-skin\"";
        while (true) {
            size_t instancePos = content.find("<instance_controller", searchPos);
            if (instancePos == std::string::npos) break;

            size_t lineStart = content.rfind('\n', instancePos);
            if (lineStart == std::string::npos) lineStart = 0; else lineStart++;
            size_t lineEnd = content.find('\n', instancePos);
            if (lineEnd == std::string::npos) lineEnd = content.length();

            std::string line = content.substr(lineStart, lineEnd - lineStart);

            if (line.find(searchStr) != std::string::npos) {
                //std::cout << "Found instance_controller line: " << line << std::endl;
                searchPos = instancePos;
                break;
            }
            searchPos = lineEnd + 1; // move past this line for next search
        }

		// do the instance_material block duplication after finding the right instance_controller
        size_t matStart = content.find("<instance_material", searchPos);
        if (matStart == std::string::npos) {
            std::cerr << "no instance_material found after instance_controller\n";
        }
        else {
            size_t matEnd = content.find("</instance_material>", matStart);

            matEnd += strlen("</instance_material>");
            size_t lineStart = content.rfind('\n', matStart);
            if (lineStart == std::string::npos) lineStart = 0; else lineStart++; // move past newline
            std::string instanceMaterialBlock = content.substr(lineStart, matEnd - lineStart);

            // duplicate instance_material blocks per material
            std::string newInstanceMaterials;
            for (size_t matIdx = 0; matIdx < matCount; matIdx++) {
                std::string block = instanceMaterialBlock;

                size_t symPos = block.find("symbol=");
                if (symPos != std::string::npos) {
                    size_t qStart = block.find('"', symPos);
                    size_t qEnd = block.find('"', qStart + 1);
                    if (qStart != std::string::npos && qEnd != std::string::npos)
                        block.replace(qStart + 1, qEnd - qStart - 1, "defaultMaterial" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first));
                }

                size_t tgtPos = block.find("target=");
                if (tgtPos != std::string::npos) {
                    size_t qStart = block.find('"', tgtPos);
                    size_t qEnd = block.find('"', qStart + 1);
                    if (qStart != std::string::npos && qEnd != std::string::npos)
                        block.replace(qStart + 1, qEnd - qStart - 1, "#material_" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first));
                }

                newInstanceMaterials += block;
                if (matIdx != matCount - 1) newInstanceMaterials += "\n";
            }

            // replace original instance_material block with the new ones
            content.replace(lineStart, matEnd - lineStart, newInstanceMaterials);
        }
        std::cout << "Fixed mesh " << (geometryId.size() > 2 && geometryId.compare(geometryId.size() - 2, 2, "_1") == 0 ? geometryId.substr(0, geometryId.size() - 2) : geometryId) << " to have " << processedMats - 1 << " extra mat(s)" << std::endl;
    }

    // write patched file back
    std::ofstream out(filePath);
    if (out.is_open()) {
        out << content;
        out.close();
    }
}

void GetDaeNormals(const std::string& outFile) {
    std::ifstream dae(outFile.c_str());
    if (!dae.is_open()) {
        std::cerr << "couldn't open dae file\n";
        return;
    }

    std::string line;
    std::vector<std::pair<std::string, std::vector<Vec3> > > meshNormals;
    std::string currentMesh;
    std::vector<float> currentNormals;

    while (std::getline(dae, line)) {
        if (line.find("<geometry") != std::string::npos && line.find("name=\"") != std::string::npos) {
            size_t nameStart = line.find("name=\"") + 6;
            size_t nameEnd = line.find("\"", nameStart);
            if (nameEnd != std::string::npos) {
                std::string rawName = line.substr(nameStart, nameEnd - nameStart);
                for (size_t i = 0; i < rawName.size(); ++i) {
                    if (rawName[i] == '.')
                        rawName.replace(i, 1, "FBXASC046"), i += sizeof("FBXASC046") - 2;
                }
                currentMesh = rawName + "Shape";
                currentNormals.clear();
                //std::cout << "Retrieved name: " << currentMesh << "\n";
            }
        }
        else {
            std::string lowerLine = line;
            std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::tolower);

            if (!currentMesh.empty() && lowerLine.find("<float_array") != std::string::npos && lowerLine.find("normal") != std::string::npos) {
                std::string floatData;

                size_t gtPos = line.find('>');
                if (gtPos != std::string::npos)
                    floatData += line.substr(gtPos + 1);

                while (line.find("</float_array>") == std::string::npos && std::getline(dae, line))
                    floatData += " " + line;

                size_t ltPos = floatData.find('<');
                if (ltPos != std::string::npos)
                    floatData = floatData.substr(0, ltPos);

                std::istringstream iss(floatData);
                float f;
                currentNormals.clear();
                while (iss >> f)
                    currentNormals.push_back(f);

                std::vector<Vec3> vecs;
                for (size_t i = 0; i + 2 < currentNormals.size(); i += 3)
                    vecs.push_back(Vec3{ currentNormals[i], currentNormals[i + 1], currentNormals[i + 2] });

                meshNormals.push_back(std::make_pair(currentMesh, vecs));
                currentMesh.clear();
            }
        }
    }

    dae.close();

    std::string outputName = outFile.substr(0, outFile.find_last_of('.')) + "_normals.txt";
    std::ofstream py(outputName.c_str());
    if (!py.is_open()) {
        std::cerr << "couldn't write normals txt file\n";
        return;
    }

    py << "import maya.cmds as cmds\n\n";

    for (size_t i = 0; i < meshNormals.size(); ++i) {
        const std::string& mesh = meshNormals[i].first;
        const std::vector<Vec3>& normals = meshNormals[i].second;

        py << "mesh = \"" << mesh << "\"\n";
        py << "normals = [";
        for (size_t j = 0; j < normals.size(); ++j) {
            const Vec3& n = normals[j];
            py << "(" << n.x << "," << n.y << "," << n.z << ")";
            if (j + 1 < normals.size()) py << ",";
        }
        py << "]\n";
        py << "for i, (x, y, z) in enumerate(normals):\n";
        py << "    cmds.polyNormalPerVertex(f\"{mesh}.vtx[{i}]\", xyz=(x, y, z))\n\n";
    }
    // write joint resizing to have a better radius cus may aswell
    py << "# resize all joints radius\n";
    py << "joints = cmds.ls(type='joint')\n";
    py << "for j in joints:\n";
    py << "    if cmds.attributeQuery('radius', node=j, exists=True):\n";
    py << "        cmds.setAttr(f\"{j}.radius\", 0.3)";

    py.close();

    std::cout << "\nMaya py script to import normals after dae import: " << outputName << " <- run that in script editor!\n";
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

void SaveDaeFile(const std::string& path, Header& headerData, std::vector<Material>& materialsData, std::vector<TextureName>& textureNames,
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

    // create bones with weights on new mesh

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

            if (linkIt != nodeLinks.end()) {
                size_t start = mergeSubmeshes ? 0 : m;
                size_t end = mergeSubmeshes ? subCount : m + 1;

                for (size_t s = start; s < end; s++) {
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
            }

            // bones with actual influence first
            std::vector<uint32_t> weightedBones;
            std::vector<uint32_t> unweightedBones;

            for (const auto& kv : nodeMap) {
                uint32_t nodeIdx = kv.first;
                if (kv.second->mNumMeshes > 0) continue;

                if (boneWeightsMap.count(nodeIdx))
                    weightedBones.push_back(nodeIdx);
                else
                    unweightedBones.push_back(nodeIdx);
            }

            mesh->mNumBones = static_cast<unsigned int>(weightedBones.size() + unweightedBones.size());
            mesh->mBones = new aiBone * [mesh->mNumBones];
            size_t boneIdx = 0;

            // first write bones that actually influence
            for (uint32_t nodeIdx : weightedBones) {
                aiBone* bone = new aiBone();
                bone->mName = aiString(allNodeNames[nodeIdx].Name.c_str());
                bone->mOffsetMatrix = GetOffsetMatrix(nodeMap[nodeIdx], parentNode);

                const auto& weights = boneWeightsMap[nodeIdx];
                bone->mNumWeights = static_cast<unsigned int>(weights.size());
                bone->mWeights = new aiVertexWeight[weights.size()];
                std::copy(weights.begin(), weights.end(), bone->mWeights);

                mesh->mBones[boneIdx++] = bone;
            }

            // then write zero-weight bones after
            for (uint32_t nodeIdx : unweightedBones) {
                aiBone* bone = new aiBone();
                bone->mName = aiString(allNodeNames[nodeIdx].Name.c_str());
                bone->mOffsetMatrix = GetOffsetMatrix(nodeMap[nodeIdx], parentNode);
                bone->mNumWeights = 0;
                bone->mWeights = nullptr;

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

    std::cout << std::endl << "Writing..." << std::endl;

    // export
    std::string outFile = path.substr(0, path.find_last_of('.')) + "_out.dae";
    Assimp::Exporter exporter;
    aiReturn rc = exporter.Export(scene, "collada", outFile);
    PatchDaeFile(outFile, allMaterialToIndices);
    if (mergeSubmeshes) {
        GetDaeNormals(outFile);
    }
    std::cout << std::endl << "Saved file as " << outFile << std::endl;
}

void SaveMKDXFile(const std::string& path, Header& header, std::vector<Material>& materialsData,
    std::vector<TextureName>& textureNames, std::vector<NodeNames>& boneNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList)
{
    std::string outFile = path.substr(0, path.find_last_of('.')) + "_out.bin";
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
        for (float f : mat.UnknownValues)
            writer.write(reinterpret_cast<char*>(&f), sizeof(float));
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
                // write data (currently unknown)
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
        for (uint32_t u : bone.BoundingBox) writer.write((char*)&u, sizeof(uint32_t));
        writer.write((char*)&bone.ModelObjectArrayOffset, sizeof(uint32_t));
        writer.write((char*)&bone.ChildrenArrayOffset, sizeof(uint32_t));
        for (float f : bone.MoreFloats) writer.write((char*)&f, sizeof(float));
        for (uint32_t u : bone.Unknowns2) writer.write((char*)&u, sizeof(uint32_t));
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
    writer.seekp(posMaterialArrayOffset);
    writer.write(reinterpret_cast<char*>(&materialArrayOffset), sizeof(uint32_t));

    writer.seekp(posTextureNameArrayOffset);
    writer.write(reinterpret_cast<char*>(&posTextureNameArray), sizeof(uint32_t));

    writer.seekp(posBoneNameArrayOffset);
    writer.write(reinterpret_cast<char*>(&posBoneNamesArray), sizeof(uint32_t));

    writer.seekp(posRootNodeArrayOffset);
    writer.write(reinterpret_cast<char*>(&posRootNodeArray), sizeof(uint32_t));

    writer.seekp(posLinkNodeOffset);
    writer.write(reinterpret_cast<char*>(&posLinkNodeArray), sizeof(uint32_t));

    writer.seekp(posTotalNodeArrayOffset);
    writer.write(reinterpret_cast<char*>(&posAllNodeNamesArray), sizeof(uint32_t));

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
    std::cout << "\nSaved binary MKDX file as " << outFile << std::endl << "NO THIS WONT WORK IT'LL WORK NEXT UPDATE" << std::endl;
}