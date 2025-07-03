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

        std::cout << "Split mesh " << geometryId << " to have " << processedMats << " extra mat(s)" << std::endl;
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
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList)
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

            std::vector<int16_t> rawIndices = (nodeData.polygonsList.size() > s) ? nodeData.polygonsList[s] : std::vector<int16_t>{};

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

    // create bones with weights on new mesh

    for (size_t nodeIndex = 0; nodeIndex < fullNodeDataList.size(); nodeIndex++) {
        const auto& nodeData = fullNodeDataList[nodeIndex];
        aiNode* parentNode = nodeMap[static_cast<unsigned int>(nodeIndex)];
        if (nodeData.subMeshes.empty()) continue;

        auto linkIt = std::find_if(nodeLinks.begin(), nodeLinks.end(), [&](const NodeLinks& link) { return link.MeshOffset == nodeIndex; });
        //if (linkIt == nodeLinks.end()) continue;

        unsigned int meshIndex = parentNode->mMeshes[0];
        aiMesh* mesh = scene->mMeshes[meshIndex];

        std::map<uint32_t, std::vector<aiVertexWeight>> boneWeightsMap;
        size_t globalVertexOffset = 0;

        // if link found, process weights, else skip but continue to add all bones with zero weights later
        if (linkIt != nodeLinks.end()) {
            for (size_t s = 0; s < nodeData.subMeshes.size(); s++) {
                if (nodeData.subMeshes[s].SkinnedBonesCount == 0) {
                    globalVertexOffset += nodeData.subMeshes[s].VertexCount;
                    continue;
                }

                uint32_t rawMask = nodeData.subMeshes[s].BonesIndexMask;
                std::vector<uint32_t> filteredBoneIndices;
                for (uint32_t i = 0; i < linkIt->BoneOffsets.size(); ++i)
                    if (rawMask & (1 << i))
                        filteredBoneIndices.push_back(linkIt->BoneOffsets[i]);

                size_t vertexCount = nodeData.subMeshes[s].VertexCount;
                const auto& weightsFlat = nodeData.weightsList[s];

                for (size_t b = 0; b < filteredBoneIndices.size(); b++) {
                    uint32_t boneNodeIndex = filteredBoneIndices[b];
                    for (size_t v = 0; v < vertexCount; v++) {
                        float weight = weightsFlat[b * vertexCount + v];
                        if (weight > 0.0f)
                            boneWeightsMap[boneNodeIndex].push_back(aiVertexWeight(static_cast<unsigned int>(globalVertexOffset + v), weight));
                    }
                }
                globalVertexOffset += vertexCount;
            }
        }
        else {
            // if no link found, still need to advance globalVertexOffset for all vertices so total stays correct
            for (size_t s = 0; s < nodeData.subMeshes.size(); s++)
                globalVertexOffset += nodeData.subMeshes[s].VertexCount;
        }

        // if no bones were added, add the first non mesh parent bone with full weights BLENDER ANIMATION FIX ONLY, REMOVED CUS MAYA DOES NOT LIKE THIS (and it'd be a pain for an importer to handle this case anyway)
        bool blender = false;
        if (blender) {
            if (boneWeightsMap.empty()) {
                int fallbackBoneNodeIndex = static_cast<int>(nodeIndex);
                bool foundValidParent = false;

                while (true) {
                    int parentIndex = -1;
                    for (size_t i = 0; i < fullNodeDataList.size(); ++i) {
                        if (std::find(fullNodeDataList[i].childrenIndexList.begin(),
                            fullNodeDataList[i].childrenIndexList.end(),
                            fallbackBoneNodeIndex) != fullNodeDataList[i].childrenIndexList.end()) {
                            parentIndex = static_cast<int>(i);
                            break;
                        }
                    }

                    if (parentIndex == -1) break; // no parent found, give up

                    if (fullNodeDataList[parentIndex].subMeshes.empty()) {
                        fallbackBoneNodeIndex = parentIndex;
                        foundValidParent = true;
                        break;
                    }

                    fallbackBoneNodeIndex = parentIndex;
                }

                if (foundValidParent) {
                    std::vector<aiVertexWeight> fullWeights;
                    for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
                        fullWeights.emplace_back(i, 1.0f);

                    boneWeightsMap[static_cast<uint32_t>(fallbackBoneNodeIndex)] = std::move(fullWeights);
                    std::cout << "[fallback bone] mesh " << allNodeNames[nodeIndex].Name << " had no skinning, added dummy bone weights to valid parent "
                        << allNodeNames[fallbackBoneNodeIndex].Name << ".\n";
                }
                else {
                    std::cout << "[fallback bone] mesh " << allNodeNames[nodeIndex].Name << " had no skinning and no valid parent found, skipping fallback.\n";
                }
            }
        }

        // add all bones with 0 weights so that blender recognises it all as a skeleton

        mesh->mNumBones = 0;

        for (const auto& kv : nodeMap) {
            aiNode* node = kv.second;
            if (node->mNumMeshes > 0) continue; // skip nodes that host meshes

            //auto it = boneWeightsMap.find(nodeIndex);
            //if (it == boneWeightsMap.end() || it->second.empty()) continue; // skip nodes with no weights

            mesh->mNumBones++;
        }

        mesh->mBones = new aiBone * [mesh->mNumBones];
        size_t boneIdx = 0;

        for (const auto& kv : nodeMap) {
            uint32_t nodeIndex = kv.first;
            aiNode* node = kv.second;
            if (node->mNumMeshes > 0) continue; // skip meshes

            aiBone* bone = new aiBone();
            bone->mName = aiString(allNodeNames[nodeIndex].Name.c_str());
            bone->mOffsetMatrix = GetOffsetMatrix(node, parentNode);

            auto it = boneWeightsMap.find(nodeIndex);
            if (it != boneWeightsMap.end()) {
                const auto& weights = it->second;
                bone->mNumWeights = static_cast<unsigned int>(weights.size());
                bone->mWeights = new aiVertexWeight[weights.size()];
                std::copy(weights.begin(), weights.end(), bone->mWeights);
            }
            else {
                // no weights for this bone, assign 0 weights (or empty array)
                bone->mNumWeights = 0;
                bone->mWeights = nullptr;
            }
            mesh->mBones[static_cast<unsigned int>(boneIdx++)] = static_cast<aiBone*>(bone);
        }
        // modify the bone with this index
        std::cout << std::endl << "Processed mesh " << allNodeNames[nodeIndex].Name;
    }
    std::cout << std::endl << "Writing..." << std::endl;

    // export
    std::string outFile = path.substr(0, path.find_last_of('.')) + "_out.dae";
    Assimp::Exporter exporter;
    aiReturn rc = exporter.Export(scene, "collada", outFile);
    PatchDaeFile(outFile, allMaterialToIndices);
    GetDaeNormals(outFile);
    std::cout << std::endl << "Saved file as " << outFile << std::endl << "Maya users make sure to run the script saved above if dae import skips normals" << std::endl;
}

void SaveMKDXFile(const std::string& path, Header& headerData, std::vector<Material>& materialsData,
    std::vector<TextureName>& textureNames, std::vector<NodeNames>& boneNames,
    std::vector<NodeLinks>& nodeLinks, std::vector<NodeNames>& allNodeNames,
    std::vector<uint32_t>& rootNodes, std::vector<FullNodeData>& fullNodeDataList)
{

}