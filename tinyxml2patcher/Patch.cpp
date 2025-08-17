#include <string>
#include <vector>
#include <tinyxml2.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <array>
#include <algorithm>
using namespace tinyxml2;

struct Vec3 {
    float x, y, z;
};

tinyxml2::XMLElement* FindInstance(tinyxml2::XMLElement* node, const std::string& geomUrl, const std::string& ctrlUrl) {
    for (auto inst = node->FirstChildElement("instance_geometry"); inst; inst = inst->NextSiblingElement("instance_geometry")) {
        const char* urlAttr = inst->Attribute("url");
        if (urlAttr) {
            //std::cout << "  checking instance_geometry url=" << urlAttr << "\n";
            if (std::string(urlAttr) == geomUrl) return inst;
        }
    }

    for (auto inst = node->FirstChildElement("instance_controller"); inst; inst = inst->NextSiblingElement("instance_controller")) {
        const char* urlAttr = inst->Attribute("url");
        if (urlAttr) {
            //std::cout << "  checking instance_controller url=" << urlAttr << "\n";
            if (std::string(urlAttr) == ctrlUrl) return inst;
        }
    }

    for (auto child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
        auto found = FindInstance(child, geomUrl, ctrlUrl);
        if (found) return found;
    }

    return nullptr;
}

bool IsMeshNode(tinyxml2::XMLElement* node)
{
    for (tinyxml2::XMLElement* child = node->FirstChildElement(); child; child = child->NextSiblingElement())
    {
        const char* name = child->Name();
        if (strcmp(name, "instance_geometry") == 0 || strcmp(name, "instance_controller") == 0)
        {
            return true;
        }
    }
    return false;
}

void FindMeshNodes(tinyxml2::XMLElement* node,
    tinyxml2::XMLElement* parent,
    std::vector<std::pair<tinyxml2::XMLElement*, tinyxml2::XMLElement*> >& out)
{
    if (IsMeshNode(node))
    {
        tinyxml2::XMLElement* meshParent = 0;
        if (parent && IsMeshNode(parent))
        {
            meshParent = parent;
        }

        out.push_back(std::make_pair(node, meshParent));

        printf("[mesh found] node=%s parent=%s\n",
            node->Attribute("name") ? node->Attribute("name") : "(unnamed)",
            meshParent && meshParent->Attribute("name") ? meshParent->Attribute("name") : "(null)");
    }

    for (tinyxml2::XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node"))
    {
        FindMeshNodes(child, node, out);
    }
}

void RecursiveStripUnderNode(tinyxml2::XMLElement* node)
{
    // strip this node if it contains mesh instances
    tinyxml2::XMLElement* child = node->FirstChildElement();
    while (child)
    {
        const char* name = child->Name();
        tinyxml2::XMLElement* nextChild = child->NextSiblingElement();

        if (strcmp(name, "instance_geometry") == 0 || strcmp(name, "instance_controller") == 0)
        {
            printf("[strip] removing %s from node %s\n",
                name,
                node->Attribute("name") ? node->Attribute("name") : "(unnamed)");
            node->DeleteChild(child);
        }

        child = nextChild;
    }

    // go deeper into child <node> elements
    tinyxml2::XMLElement* childNode = node->FirstChildElement("node");
    while (childNode)
    {
        RecursiveStripUnderNode(childNode);
        childNode = childNode->NextSiblingElement("node");
    }
}

void RecursiveDeleteNonMeshNodes(tinyxml2::XMLElement* node)
{
    tinyxml2::XMLElement* child = node->FirstChildElement("node");
    while (child)
    {
        tinyxml2::XMLElement* nextChild = child->NextSiblingElement("node");

        if (!IsMeshNode(child))
        {
            printf("[delete non-mesh] %s under mesh node %s\n",
                child->Attribute("name") ? child->Attribute("name") : "(unnamed)",
                node->Attribute("name") ? node->Attribute("name") : "(unnamed)");
            node->DeleteChild(child);
        }
        else
        {
            // recurse into mesh children
            RecursiveDeleteNonMeshNodes(child);
        }

        child = nextChild;
    }
}

struct Mat4
{
    float m[4][4];

    Mat4() { std::memset(m, 0, sizeof(m)); m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.f; } // identity

    Mat4 operator*(const Mat4& o) const
    {
        Mat4 r;
        for (int i = 0;i < 4;i++)
            for (int j = 0;j < 4;j++)
            {
                r.m[i][j] = 0.f;
                for (int k = 0;k < 4;k++)
                    r.m[i][j] += m[i][k] * o.m[k][j];
            }
        return r;
    }
};

Mat4 ParseMatrix(tinyxml2::XMLElement* node)
{
    Mat4 mat;
    tinyxml2::XMLElement* matElem = node->FirstChildElement("matrix");
    if (matElem && matElem->GetText())
    {
        std::istringstream iss(matElem->GetText());
        for (int i = 0;i < 4;i++)
            for (int j = 0;j < 4;j++)
                iss >> mat.m[i][j];
    }
    return mat;
}

Mat4 GetWorldMatrix(tinyxml2::XMLElement* node)
{
    Mat4 local = ParseMatrix(node);

    tinyxml2::XMLElement* parent = node->Parent()->ToElement();
    if (parent && strcmp(parent->Name(), "node") == 0)
    {
        Mat4 parentWorld = GetWorldMatrix(parent);
        return parentWorld * local;
    }
    return local;
}

std::string MatrixToString(const Mat4& mat)
{
    std::ostringstream oss;
    oss.precision(9);
    oss << std::fixed
        << mat.m[0][0] << " " << mat.m[0][1] << " " << mat.m[0][2] << " " << mat.m[0][3] << " "
        << mat.m[1][0] << " " << mat.m[1][1] << " " << mat.m[1][2] << " " << mat.m[1][3] << " "
        << mat.m[2][0] << " " << mat.m[2][1] << " " << mat.m[2][2] << " " << mat.m[2][3] << " "
        << mat.m[3][0] << " " << mat.m[3][1] << " " << mat.m[3][2] << " " << mat.m[3][3];
    return oss.str();
}

extern "C" __declspec(dllexport) void __cdecl PatchDaeFile_C(const char* filePath, const char* matInfoPath)
{
    XMLDocument doc;
    doc.LoadFile(filePath);

    auto collada = doc.FirstChildElement("COLLADA");
    auto asset = collada->FirstChildElement("asset");

    for (auto unit = asset->FirstChildElement("unit"); unit;) {
        auto next = unit->NextSiblingElement("unit");
        const char* nameAttr = unit->Attribute("name");
        if (nameAttr && std::string(nameAttr) == "meter")
            asset->DeleteChild(unit);
        unit = next;
    }

    // contributor block (insert blurro & mkdxtool above assimp block)
    auto contrib = asset->FirstChildElement("contributor");
    if (contrib) {
        // clone the original
        auto cloned = contrib->DeepClone(&doc);

        // remove the original
        asset->DeleteChild(contrib);

        // add your custom one first
        auto newContrib = doc.NewElement("contributor");

        auto author = doc.NewElement("author");
        author->SetText("Blurro");
        newContrib->InsertEndChild(author);

        auto tool = doc.NewElement("authoring_tool");
        tool->SetText("MKDXtool");
        newContrib->InsertEndChild(tool);

        asset->InsertEndChild(newContrib);
        asset->InsertEndChild(cloned);
    }

    // below is for adding materials to a mesh cus assimp can only do 1 mat per mesh

    // turn polylist block to triangles
    for (auto mesh = doc.FirstChildElement("COLLADA")
        ->FirstChildElement("library_geometries")
        ->FirstChildElement("geometry");
        mesh;
        mesh = mesh->NextSiblingElement("geometry")) {

        auto meshElem = mesh->FirstChildElement("mesh");
        if (!meshElem) continue;

        for (auto polylist = meshElem->FirstChildElement("polylist"); polylist;) {
            auto next = polylist->NextSiblingElement("polylist");

            // change tag name to triangles
            auto triangles = doc.NewElement("triangles");

            // copy all attributes
            const tinyxml2::XMLAttribute* attr = polylist->FirstAttribute();
            while (attr) {
                triangles->SetAttribute(attr->Name(), attr->Value());
                attr = attr->Next();
            }

            // move children except <vcount>
            for (auto child = polylist->FirstChildElement(); child;) {
                auto nextChild = child->NextSiblingElement();
                if (std::string(child->Name()) != "vcount")
                    triangles->InsertEndChild(child->DeepClone(&doc));
                child = nextChild;
            }

            // replace in tree
            meshElem->InsertAfterChild(polylist, triangles);
            meshElem->DeleteChild(polylist);

            polylist = next;
        }
    }

    // load allmaterialtoindices from temp file
    std::vector<std::vector<std::pair<unsigned int, std::vector<unsigned int>>>> allMaterialToIndices;
    std::ifstream file(matInfoPath);
    std::string line;
    std::vector<std::pair<unsigned int, std::vector<unsigned int>>> currentMesh;

    while (std::getline(file, line)) {
        if (line == "mesh") {
            currentMesh.clear();
        }
        else if (line == "endmesh") {
            allMaterialToIndices.push_back(currentMesh);
        }
        else {
            size_t colon = line.find(":");
            if (colon != std::string::npos) {
                unsigned int matIdx = std::stoi(line.substr(0, colon));
                std::vector<unsigned int> indices;
                std::stringstream ss(line.substr(colon + 1));
                std::string indexStr;
                while (std::getline(ss, indexStr, ',')) {
                    if (!indexStr.empty())
                        indices.push_back(std::stoul(indexStr));
                }
                currentMesh.emplace_back(matIdx, indices);
            }
        }
    }

    auto libGeometries = collada->FirstChildElement("library_geometries");
    auto libControllers = collada->FirstChildElement("library_controllers");
    auto libVisualScenes = collada->FirstChildElement("library_visual_scenes");

    size_t meshIdx = 0;
    for (auto geometry = libGeometries->FirstChildElement("geometry"); geometry; geometry = geometry->NextSiblingElement("geometry"), meshIdx++)
    {
        if (meshIdx >= allMaterialToIndices.size()) break;
        auto mesh = geometry->FirstChildElement("mesh");
        if (!mesh) continue;

        auto oldTri = mesh->FirstChildElement("triangles");
        if (!oldTri) continue;

        size_t matCount = allMaterialToIndices[meshIdx].size();
        if (matCount <= 1) continue;

        auto pElem = oldTri->FirstChildElement("p");
        if (pElem) pElem->SetText("");

        std::vector<tinyxml2::XMLElement*> inputElems;
        for (auto input = oldTri->FirstChildElement("input"); input; input = input->NextSiblingElement("input"))
            inputElems.push_back(input->DeepClone(&doc)->ToElement());

        mesh->DeleteChild(oldTri); // remove old

        for (size_t matIdx = 0; matIdx < matCount; matIdx++) {
            auto newTri = doc.NewElement("triangles");
            newTri->SetAttribute("material", ("defaultMaterial" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first)).c_str());
            newTri->SetAttribute("count", (int)(allMaterialToIndices[meshIdx][matIdx].second.size() / 3));
            for (auto input : inputElems)
                newTri->InsertEndChild(input->DeepClone(&doc));
            auto p = doc.NewElement("p");
            std::string indices;
            for (auto idx : allMaterialToIndices[meshIdx][matIdx].second)
                indices += std::to_string(idx) + " ";
            if (!indices.empty()) indices.pop_back();
            p->SetText(indices.c_str());
            newTri->InsertEndChild(p);
            mesh->InsertEndChild(newTri);
        }

        std::string geometryId = geometry->Attribute("id") ? geometry->Attribute("id") : "";
        std::string geomSearchStr = "#" + geometryId;
        std::string ctrlSearchStr = geomSearchStr + "-skin";

        tinyxml2::XMLElement* targetInstance = nullptr;

        for (auto visualScene = libVisualScenes->FirstChildElement("visual_scene"); visualScene; visualScene = visualScene->NextSiblingElement("visual_scene")) {
            for (auto node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
                //std::cout << "searching for instance_geometry with url=" << geomSearchStr << " or instance_controller with url=" << ctrlSearchStr << "\n";
                targetInstance = FindInstance(node, geomSearchStr, ctrlSearchStr);
                if (targetInstance) break;
            }
            if (targetInstance) break;
        }

        if (!targetInstance) {
            std::cerr << "no instance_controller or instance_geometry found for geometry " << geometryId << "\n";
            continue;
        }

        auto matBind = targetInstance->FirstChildElement("bind_material");
        if (!matBind) {
            std::cerr << "no bind_material found\n";
            continue;
        }

        auto techCommon = matBind->FirstChildElement("technique_common");
        if (!techCommon) {
            std::cerr << "no technique_common found in bind_material\n";
            continue;
        }

        auto mat = techCommon->FirstChildElement("instance_material");
        if (!mat) {
            std::cerr << "no instance_material found in bind_material\n";
            continue;
        }

        // clone + remap materials
        std::vector<tinyxml2::XMLElement*> newInstanceMaterials;
        for (size_t matIdx = 0; matIdx < matCount; matIdx++) {
            auto block = mat->DeepClone(&doc)->ToElement();
            std::string symbol = "defaultMaterial" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first);
            std::string target = "#material_" + std::to_string(allMaterialToIndices[meshIdx][matIdx].first);
            block->SetAttribute("symbol", symbol.c_str());
            block->SetAttribute("target", target.c_str());
            newInstanceMaterials.push_back(block);
        }

        // clear old materials
        while (techCommon->FirstChild()) techCommon->DeleteChild(techCommon->FirstChild());
        // insert new ones
        for (auto m : newInstanceMaterials) techCommon->InsertEndChild(m);

        std::string prettyName = geometryId.size() > 2 && geometryId.compare(geometryId.size() - 2, 2, "_1") == 0
            ? geometryId.substr(0, geometryId.size() - 2) : geometryId;

        std::cout << "Fixed mesh " << prettyName << " to have " << (matCount - 1) << " extra mat(s)" << std::endl;
    }

    // make mesh names unique
    std::unordered_map<std::string, int> nameCounts;
    for (auto geometry = libGeometries->FirstChildElement("geometry"); geometry; geometry = geometry->NextSiblingElement("geometry")) {
        const char* nameAttr = geometry->Attribute("name");
        if (!nameAttr) continue;

        std::string baseName(nameAttr);
        int count = nameCounts[baseName] + 1;

        if (count > 1) {
            std::string newName;
            do {
                newName = baseName + "_" + std::to_string(count);
                count++;
            } while (nameCounts.find(newName) != nameCounts.end());
            count--;

            geometry->SetAttribute("name", newName.c_str());
            nameCounts[newName] = 1;
            nameCounts[baseName] = count;
        }
        else {
            nameCounts[baseName] = 1;
        }
    }

	// cool block to merge vertices with the same position, also doubles p block for texcoord to have offset 1
    for (auto geometry = libGeometries->FirstChildElement("geometry"); geometry; geometry = geometry->NextSiblingElement("geometry")) {
        auto mesh = geometry->FirstChildElement("mesh");
        if (!mesh) continue;

        for (auto tri = mesh->FirstChildElement("triangles"); tri; tri = tri->NextSiblingElement("triangles")) {
            auto p = tri->FirstChildElement("p");
            if (!p || !p->GetText()) continue;

            // detect if TEXCOORD or COLOR exists
            bool hasTexcoord = false;
            bool hasColor = false;
            for (auto input = tri->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
                const char* semantic = input->Attribute("semantic");
                if (!semantic) continue;
                std::string semStr = semantic;
                if (semStr == "TEXCOORD") {
                    hasTexcoord = true;
                    input->SetAttribute("offset", 1);
                }
                else if (semStr == "COLOR") {
                    hasColor = true;
                    input->SetAttribute("offset", 1);
                }
                else {
                    input->SetAttribute("offset", 0);
                }
            }

            // only double <p> if TEXCOORD or COLOR exists
            if (hasTexcoord || hasColor) {
                std::stringstream ss(p->GetText());
                std::vector<unsigned int> doubled;
                unsigned int val;
                while (ss >> val) {
                    doubled.push_back(val);
                    doubled.push_back(val);
                }
                std::stringstream doubledSS;
                for (auto v : doubled) doubledSS << v << " ";
                std::string doubledStr = doubledSS.str();
                if (!doubledStr.empty()) doubledStr.pop_back();
                p->SetText(doubledStr.c_str());
            }

            auto vertices = mesh->FirstChildElement("vertices");
            if (!vertices) continue;
            std::string posSourceId;
            for (auto input = vertices->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
                if (std::string(input->Attribute("semantic")) == "POSITION") {
                    posSourceId = input->Attribute("source");
                    if (posSourceId[0] == '#') posSourceId = posSourceId.substr(1);
                    break;
                }
            }
            if (posSourceId.empty()) continue;

            tinyxml2::XMLElement* posSourceElem = nullptr;
            for (auto source = mesh->FirstChildElement("source"); source; source = source->NextSiblingElement("source")) {
                if (std::string(source->Attribute("id")) == posSourceId) {
                    posSourceElem = source;
                    break;
                }
            }
            if (!posSourceElem) continue;

            auto floatArray = posSourceElem->FirstChildElement("float_array");
            if (!floatArray) continue;

            std::stringstream fss(floatArray->GetText());
            std::vector<float> positions;
            float f;
            while (fss >> f) positions.push_back(f);

            auto technique = posSourceElem->FirstChildElement("technique_common");
            if (!technique) continue;
            auto accessor = technique->FirstChildElement("accessor");
            if (!accessor || !accessor->Attribute("stride")) continue;
            int posStride = atoi(accessor->Attribute("stride"));

            // load normals too
            std::string normSourceId;
            for (auto input = tri->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
                if (std::string(input->Attribute("semantic")) == "NORMAL") {
                    normSourceId = input->Attribute("source");
                    if (normSourceId[0] == '#') normSourceId = normSourceId.substr(1);
                    break;
                }
            }

            std::vector<float> normals;
            int normStride = 0;
            if (!normSourceId.empty()) {
                tinyxml2::XMLElement* normSourceElem = nullptr;
                for (auto source = mesh->FirstChildElement("source"); source; source = source->NextSiblingElement("source")) {
                    if (std::string(source->Attribute("id")) == normSourceId) {
                        normSourceElem = source;
                        break;
                    }
                }
                if (normSourceElem) {
                    auto normArray = normSourceElem->FirstChildElement("float_array");
                    if (normArray) {
                        std::stringstream nss(normArray->GetText());
                        float nf;
                        while (nss >> nf) normals.push_back(nf);
                    }
                    auto ntech = normSourceElem->FirstChildElement("technique_common");
                    if (ntech) {
                        auto naccessor = ntech->FirstChildElement("accessor");
                        if (naccessor && naccessor->Attribute("stride")) normStride = atoi(naccessor->Attribute("stride"));
                    }
                }
            }

            std::stringstream pss(p->GetText());
            std::vector<unsigned int> indices;
            unsigned int idx;
            while (pss >> idx) indices.push_back(idx);

            int inputCount = 2;

            std::unordered_map<unsigned int, unsigned int> canonicalMap;
            for (unsigned int i = 0; i < positions.size() / posStride; ++i) {
                canonicalMap[i] = i;
            }

            auto equalPosAndNorm = [&](unsigned int i, unsigned int j) {
                for (int k = 0; k < posStride; ++k)
                    if (fabs(positions[i * posStride + k] - positions[j * posStride + k]) > 1e-5f) return false;

                if (normStride > 0) {
                    for (size_t triIdx = 0; triIdx < indices.size(); triIdx += inputCount) {
                        if (indices[triIdx] == i && indices[triIdx + 1] && indices[triIdx + 1] < normals.size() / normStride) {
                            for (size_t searchIdx = 0; searchIdx < indices.size(); searchIdx += inputCount) {
                                if (indices[searchIdx] == j && indices[searchIdx + 1] && indices[searchIdx + 1] < normals.size() / normStride) {
                                    for (int k = 0; k < normStride; ++k) {
                                        float ni = normals[indices[triIdx + 1] * normStride + k];
                                        float nj = normals[indices[searchIdx + 1] * normStride + k];
                                        if (fabs(ni - nj) > 1e-5f) return false;
                                    }
                                    return true;
                                }
                            }
                        }
                    }
                    return false;
                }

                return true;
            };

            for (unsigned int i = 0; i < positions.size() / posStride; ++i) {
                for (unsigned int j = i + 1; j < positions.size() / posStride; ++j) {
                    if (equalPosAndNorm(i, j) && canonicalMap[j] == j) {
                        canonicalMap[j] = canonicalMap[i];
                    }
                }
            }

            for (size_t triIdx = 0; triIdx < indices.size(); triIdx += inputCount) {
                unsigned int vertexIndex = indices[triIdx];
                unsigned int canonicalIndex = canonicalMap[vertexIndex];
                indices[triIdx] = canonicalIndex;
            }

            std::stringstream outSS;
            for (auto i : indices) outSS << i << " ";
            std::string outStr = outSS.str();
            if (!outStr.empty()) outStr.pop_back();
            p->SetText(outStr.c_str());
        }
    }

    bool aaa = false;
    if (aaa) {
        // create a duplicate for every mesh node (that isnt under armature) to be placed under armature (with baked matrix), strip out instance geometry from original mesh nodes
        tinyxml2::XMLElement* root = doc.FirstChildElement("COLLADA")->FirstChildElement("library_visual_scenes")->FirstChildElement("visual_scene");
        tinyxml2::XMLElement* armatureNode = root->FirstChildElement("node");

        std::vector<tinyxml2::XMLElement*> nonMeshUnderArmature;
        tinyxml2::XMLElement* child = armatureNode->FirstChildElement("node");
        while (child)
        {
            if (!IsMeshNode(child))
            {
                nonMeshUnderArmature.push_back(child);
                printf("[non-mesh direct child under armature] %s\n",
                    child->Attribute("name") ? child->Attribute("name") : "(unnamed)");
            }
            child = child->NextSiblingElement("node");
        }

        std::vector<std::pair<tinyxml2::XMLElement*, tinyxml2::XMLElement*> > foundMeshes;
        for (size_t i = 0; i < nonMeshUnderArmature.size(); ++i)
        {
            FindMeshNodes(nonMeshUnderArmature[i], 0, foundMeshes);
        }

        for (size_t i = 0; i < foundMeshes.size(); ++i)
        {
            tinyxml2::XMLElement* meshNode = foundMeshes[i].first;
            tinyxml2::XMLElement* meshParent = foundMeshes[i].second;

            // skip duplication if this mesh has a mesh-node parent
            if (meshParent)
            {
                printf("[skipped duplicate] %s (had mesh parent %s)\n",
                    meshNode->Attribute("name") ? meshNode->Attribute("name") : "(unnamed)",
                    meshParent->Attribute("name") ? meshParent->Attribute("name") : "(unnamed)");
                continue;
            }

            tinyxml2::XMLElement* duplicate = meshNode->DeepClone(&doc)->ToElement();

            // bake world transform
            Mat4 worldMat = GetWorldMatrix(meshNode);
            tinyxml2::XMLElement* matElem = duplicate->FirstChildElement("matrix");
            if (!matElem) {
                matElem = doc.NewElement("matrix");
                duplicate->InsertFirstChild(matElem);
            }
            matElem->SetText(MatrixToString(worldMat).c_str());

            // insert under armature
            armatureNode->InsertEndChild(duplicate);


            printf("[duplicated mesh] %s placed under armature\n",
                duplicate->Attribute("name") ? duplicate->Attribute("name") : "(unnamed)");
        }

        for (size_t i = 0; i < nonMeshUnderArmature.size(); ++i)
        {
            RecursiveStripUnderNode(nonMeshUnderArmature[i]);
        }

        std::vector<tinyxml2::XMLElement*> meshUnderArmature;
        tinyxml2::XMLElement* mChild = armatureNode->FirstChildElement("node");
        while (mChild)
        {
            if (IsMeshNode(mChild))
            {
                meshUnderArmature.push_back(mChild);
                printf("[mesh direct child under armature for delete pass] %s\n",
                    mChild->Attribute("name") ? mChild->Attribute("name") : "(unnamed)");
            }
            mChild = mChild->NextSiblingElement("node");
        }

        for (size_t i = 0; i < meshUnderArmature.size(); ++i)
        {
            RecursiveDeleteNonMeshNodes(meshUnderArmature[i]);
        }
    }

    doc.SaveFile(filePath);
}

extern "C" __declspec(dllexport) void __cdecl GetDaeNormals_C(const char* filePath) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filePath) != tinyxml2::XML_SUCCESS) {
        std::cerr << "failed to load dae\n";
        return;
    }

    auto libGeometries = doc.FirstChildElement("COLLADA")
        ->FirstChildElement("library_geometries");

    if (!libGeometries) {
        std::cerr << "no library_geometries found\n";
        return;
    }

    std::vector<std::pair<std::string, std::vector<Vec3>>> meshNormals;

    for (auto geometry = libGeometries->FirstChildElement("geometry"); geometry; geometry = geometry->NextSiblingElement("geometry")) {
        const char* nameAttr = geometry->Attribute("name");
        if (!nameAttr) continue;

        std::string rawName(nameAttr);
        for (size_t i = 0; i < rawName.size(); ++i) {
            if (rawName[i] == '.')
                rawName.replace(i, 1, "FBXASC046"), i += sizeof("FBXASC046") - 2;
        }

        std::string currentMesh = rawName + "Shape";

        auto mesh = geometry->FirstChildElement("mesh");
        if (!mesh) continue;

        auto triangles = mesh->FirstChildElement("triangles");
        if (!triangles) continue;

        const char* normalSourceAttr = nullptr;
        for (auto input = triangles->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
            if (const char* semantic = input->Attribute("semantic")) {
                if (std::string(semantic) == "NORMAL") {
                    normalSourceAttr = input->Attribute("source");
                    break;
                }
            }
        }

        if (!normalSourceAttr || normalSourceAttr[0] != '#') continue;

        std::string normalSourceID = normalSourceAttr + 1;

        tinyxml2::XMLElement* normalsSource = nullptr;
        for (auto source = mesh->FirstChildElement("source"); source; source = source->NextSiblingElement("source")) {
            const char* idAttr = source->Attribute("id");
            if (idAttr && normalSourceID == idAttr) {
                normalsSource = source;
                break;
            }
        }

        if (!normalsSource) continue;

        auto floatArray = normalsSource->FirstChildElement("float_array");
        if (!floatArray) continue;

        const char* floatText = floatArray->GetText();
        if (!floatText) continue;

        std::istringstream iss(floatText);
        std::vector<float> floats;
        float val;
        while (iss >> val) floats.push_back(val);

        std::vector<Vec3> normals;
        for (size_t i = 0; i + 2 < floats.size(); i += 3)
            normals.push_back(Vec3{ floats[i], floats[i + 1], floats[i + 2] });

        meshNormals.emplace_back(currentMesh, std::move(normals));
    }

    std::string outputName = std::string(filePath).substr(0, std::string(filePath).find_last_of('.')) + "_normals.txt";
    std::ofstream py(outputName.c_str());
    if (!py.is_open()) {
        std::cerr << "couldn't write normals txt file\n";
        return;
    }

	// write the python script to apply normals in Maya
    py << "import maya.api.OpenMaya as om\n";
    py << "import maya.utils\n\n";

    int meshIndex = 0;
    for (const auto& meshPair : meshNormals) {
        const std::string& mesh = meshPair.first;
        const std::vector<Vec3>& normals = meshPair.second;

        py << "def apply_normals_" << meshIndex << "():\n";
        py << "    meshName = \"" << mesh << "\"\n";
        py << "    normalsList = [\n        ";
        for (size_t j = 0; j < normals.size(); ++j) {
            const Vec3& n = normals[j];
            py << "(" << n.x << ", " << n.y << ", " << n.z << ")";
            if (j + 1 < normals.size()) py << ", ";
            if ((j + 1) % 5 == 0) py << "\n        ";
        }
        py << "\n    ]\n";
        py << "    try:\n";
        py << "        sel = om.MSelectionList()\n";
        py << "        sel.add(meshName)\n";
        py << "        dagPath = sel.getDagPath(0)\n";
        py << "        fnMesh = om.MFnMesh(dagPath)\n";
        py << "        normals = [om.MVector(x, y, z) for (x, y, z) in normalsList]\n";
        py << "        vertexIndices = list(range(len(normals)))\n";
        py << "        fnMesh.setVertexNormals(normals, vertexIndices)\n";
        py << "        print(f\"done {meshName}\")\n";
        if (meshIndex + 1 < (int)meshNormals.size()) {
            py << "        maya.utils.executeDeferred(apply_normals_" << (meshIndex + 1) << ")\n";
        }
        else {
            py << "        maya.utils.executeDeferred(apply_normals_" << (meshIndex + 1) << ")\n"; // call the dummy one
        }
        py << "    except Exception as e:\n";
        py << "        print(f\"error applying to {meshName}: {e}\")\n";
        py << "\n";
        meshIndex++;
    }

    // dummy last function to finalize and resize joints
    py << "def apply_normals_" << meshIndex << "():\n";
    py << "    print('All normals applied!')\n";
    py << "    import maya.cmds as cmds\n";
    py << "    joints = cmds.ls(type='joint')\n";
    py << "    for j in joints:\n";
    py << "        if cmds.attributeQuery('radius', node=j, exists=True):\n";
    py << "            cmds.setAttr(f\"{j}.radius\", 0.3)\n";
    py << "\n";

    // kickoff
    py << "maya.utils.executeDeferred(apply_normals_0)\n";

    py.close();

    std::cout << "\nMaya py script to import normals after dae import: " << outputName << " <- run that in script editor!\n";
}

void ProcessNode(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* node, const std::unordered_set<std::string>& meshNameSet) {
    using namespace tinyxml2;

    // make id become the name field
    if (const char* name = node->Attribute("name")) {
        node->SetAttribute("id", name);
    }

    const char* nodeName = node->Attribute("name");
    if (!nodeName) return;

    bool hasMeshInstance = node->FirstChildElement("instance_controller") || node->FirstChildElement("instance_geometry");
    if (hasMeshInstance && meshNameSet.find(nodeName) != meshNameSet.end()) {
        std::cout << "processing mesh node: " << nodeName << "\n";

        bool found = true;
        while (found) {
            found = false;
            for (XMLElement* child = node->FirstChildElement("node"); child;) {
                std::string childName = child->Attribute("name");

                if (meshNameSet.count(childName)) {
                    std::cout << "  skipping main mesh node: " << childName << "\n";
                    child = child->NextSiblingElement("node");
                    continue;
                }

                // skip absorbing main mesh + suffix into parent if blender split it to .001 .002 etc
                size_t dot = childName.find_last_of('.');
                if (dot != std::string::npos && dot + 4 == childName.length()) {
                    std::string prefix = childName.substr(0, dot);
                    if (meshNameSet.count(prefix)) {
                        // check if the base mesh is a child alongside the base + blender suffix
                        bool hasBaseAsChild = false;
                        for (XMLElement* c = node->FirstChildElement("node"); c; c = c->NextSiblingElement("node")) {
                            const char* cname = c->Attribute("name");
                            if (cname && prefix == cname) {
                                hasBaseAsChild = true;
                                break;
                            }
                        }

                        if (hasBaseAsChild) {
                            std::cout << "  skipping suffix mesh (base exists as child): " << childName << "\n";
                            child = child->NextSiblingElement("node");
                            continue;
                        }
                    }
                }

                std::cout << "  absorbing submesh: " << childName << "\n";

                XMLElement* next = child->NextSiblingElement("node");

                // move all instance_controllers
                for (XMLElement* instCtrl = child->FirstChildElement("instance_controller"); instCtrl; instCtrl = instCtrl->NextSiblingElement("instance_controller")) {
                    node->InsertEndChild(instCtrl->DeepClone(&doc));
                }

                // move all instance_geometry
                for (XMLElement* instGeo = child->FirstChildElement("instance_geometry"); instGeo; instGeo = instGeo->NextSiblingElement("instance_geometry")) {
                    node->InsertEndChild(instGeo->DeepClone(&doc));
                }

                // move all nested nodes
                for (XMLElement* sub = child->FirstChildElement("node"); sub; sub = sub->NextSiblingElement("node")) {
                    node->InsertEndChild(sub->DeepClone(&doc));
                }

                node->DeleteChild(child);
                found = true;
                child = next;
            }
        }
    }

    // go deeper into the tree
    for (XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
        ProcessNode(doc, child, meshNameSet);
    }
}

void CollectMeshNodes(XMLElement* node, const std::unordered_set<std::string>& meshNameSet, std::unordered_map<std::string, XMLElement*>& meshNodeMap) {
    const char* nameAttr = node->Attribute("name");
    if (!nameAttr) return;

    std::string name = nameAttr;
    if (meshNameSet.count(name)) {
        meshNodeMap[name] = node;

        // recurse into children ONLY if they're in meshNameSet
        for (XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node"))
            CollectMeshNodes(child, meshNameSet, meshNodeMap);
    }
}

void MergeSuffixMeshes(XMLElement* node, const std::unordered_set<std::string>& meshNameSet, std::unordered_map<std::string, XMLElement*>& meshNodeMap, XMLDocument& doc) {
    std::vector<XMLElement*> toDelete;

    for (XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
        const char* nameAttr = child->Attribute("name");
        if (!nameAttr) continue;

        std::string name = nameAttr;
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot + 4 != name.length()) continue;

        std::string prefix = name.substr(0, dot);
        if (!meshNameSet.count(prefix)) continue;

        auto it = meshNodeMap.find(prefix);
        if (it == meshNodeMap.end()) continue;

        std::cout << "  suffix mesh found: " << name << " -> " << prefix << "\n";

        for (XMLElement* inst = child->FirstChildElement("instance_controller"); inst; inst = inst->NextSiblingElement("instance_controller")) {
            it->second->InsertEndChild(inst->DeepClone(&doc));
        }

        for (XMLElement* inst = child->FirstChildElement("instance_geometry"); inst; inst = inst->NextSiblingElement("instance_geometry")) {
            it->second->InsertEndChild(inst->DeepClone(&doc));
        }

        toDelete.push_back(child);
    }

    for (XMLElement* d : toDelete) node->DeleteChild(d);

    // recurse into children that are in meshNameSet
    for (XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
        const char* nameAttr = child->Attribute("name");
        if (!nameAttr) continue;

        if (meshNameSet.count(nameAttr)) {
            MergeSuffixMeshes(child, meshNameSet, meshNodeMap, doc);
        }
    }
}

tinyxml2::XMLElement* FindNodeByIdRecursive(tinyxml2::XMLElement* parent, const std::string& targetId) {
    for (tinyxml2::XMLElement* node = parent->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        const char* id = node->Attribute("id");
        if (id && targetId == id)
            return node;

        // search child nodes recursively
        tinyxml2::XMLElement* found = FindNodeByIdRecursive(node, targetId);
        if (found)
            return found;
    }
    return nullptr;
}

// helper to find node by sid and return its 'name'
std::string FindNodeNameBySID(tinyxml2::XMLElement* parent, const std::string& sid) {
    for (tinyxml2::XMLElement* node = parent->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        const char* thisSid = node->Attribute("sid");
        if (thisSid && sid == thisSid) {
            const char* name = node->Attribute("name");
            if (name) return name;
        }

        // recurse into child nodes
        std::string result = FindNodeNameBySID(node, sid);
        if (!result.empty()) return result;
    }
    return {};
}

extern "C" __declspec(dllexport) void __cdecl GetDaeBoneNames_C(const char* filePath, const char* meshName, char** outputBones, int maxBones, int* outCount)
{
    if (!filePath || !meshName || !outputBones || !outCount) return;
    *outCount = 0;

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filePath) != tinyxml2::XML_SUCCESS) {
        printf("failed to load dae: %s\n", filePath);
        return;
    }

    auto* collada = doc.FirstChildElement("COLLADA");
    if (!collada) {
        printf("missing <COLLADA> root\n");
        return;
    }

    auto* libVisualScenes = collada->FirstChildElement("library_visual_scenes");
    if (!libVisualScenes) {
        printf("missing <library_visual_scenes>\n");
        return;
    }

    auto* visualScene = libVisualScenes->FirstChildElement("visual_scene");
    if (!visualScene) {
        printf("missing <visual_scene>\n");
        return;
    }

    auto* libControllers = collada->FirstChildElement("library_controllers");
    if (!libControllers) {
        printf("missing <library_controllers>\n");
        return;
    }

    tinyxml2::XMLElement* targetController = nullptr;
    tinyxml2::XMLElement* skin = nullptr;

    std::string meshSourceRef = "#" + std::string(meshName);

    for (auto* controller = libControllers->FirstChildElement("controller"); controller; controller = controller->NextSiblingElement("controller")) {
        auto* trySkin = controller->FirstChildElement("skin");
        if (!trySkin) continue;

        const char* src = trySkin->Attribute("source");
        if (src && meshSourceRef == src) {
            targetController = controller;
            skin = trySkin;
            //printf("matched skin controller for mesh '%s': %s\n", meshName, controller->Attribute("id") ? controller->Attribute("id") : "<unnamed>");
            break;
        }
    }

    if (!targetController || !skin) {
        //printf("no matching skin controller found for mesh: %s\n", meshName);
        return;
    }

    auto* joints = skin->FirstChildElement("joints");
    if (!joints) {
        //printf("no <joints> inside <skin>\n");
        return;
    }

    const char* jointSourceId = nullptr;
    for (auto* input = joints->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
        const char* semantic = input->Attribute("semantic");
        const char* src = input->Attribute("source");
        if (semantic && src && std::string(semantic) == "JOINT" && src[0] == '#') {
            jointSourceId = src + 1;
            break;
        }
    }

    if (!jointSourceId) {
        //printf("no JOINT input source found\n");
        return;
    }

    tinyxml2::XMLElement* jointSource = nullptr;
    for (auto* source = skin->FirstChildElement("source"); source; source = source->NextSiblingElement("source")) {
        const char* id = source->Attribute("id");
        if (id && std::string(id) == jointSourceId) {
            jointSource = source;
            break;
        }
    }

    if (!jointSource) {
        //printf("joint source not found: %s\n", jointSourceId);
        return;
    }

    auto* nameArray = jointSource->FirstChildElement("Name_array");
    if (!nameArray || !nameArray->GetText()) {
        //printf("no Name_array found under joint source\n");
        return;
    }

    std::istringstream iss(nameArray->GetText());
    std::string bone;
    int count = 0;
    while (iss >> bone && count < maxBones) {
        strncpy_s(outputBones[count], 256, bone.c_str(), _TRUNCATE);
        ++count;
    }

    for (int i = 0; i < count; ++i) {
        std::string original = outputBones[i];
        std::string newName = FindNodeNameBySID(visualScene, original);
        if (!newName.empty()) {
            strncpy_s(outputBones[i], 256, newName.c_str(), _TRUNCATE);
        }
    }

    *outCount = count;
    printf("extracted %d bones for mesh %s\n", count, meshName);
}


extern "C" __declspec(dllexport) void __cdecl NodeToSubmesh_C(const char* filePath, const char** meshNames, int meshCount) {
    XMLDocument doc;
    if (doc.LoadFile(filePath) != XML_SUCCESS) return;

    std::unordered_set<std::string> meshNameSet;
    for (int i = 0; i < meshCount; ++i) meshNameSet.insert(meshNames[i]);

    XMLElement* root = doc.RootElement();
    if (!root) return;

    XMLElement* sceneLib = root->FirstChildElement("library_visual_scenes");
    if (!sceneLib) return;

    XMLElement* visualScene = sceneLib->FirstChildElement("visual_scene");
    if (!visualScene) return;

    // strip all <extra> tags from any node anywhere
    for (XMLElement* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        std::vector<XMLElement*> stack = { node };
        while (!stack.empty()) {
            XMLElement* current = stack.back(); stack.pop_back();
            for (XMLElement* child = current->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
                stack.push_back(child);
            }
            for (XMLElement* extra = current->FirstChildElement("extra"); extra;) {
                XMLElement* toDelete = extra;
                extra = extra->NextSiblingElement("extra");
                current->DeleteChild(toDelete);
            }
        }
    }

    // do the nodes to submesh stuff
    for (XMLElement* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        ProcessNode(doc, node, meshNameSet);
    }

    // do method 2 of looking for mesh name with suffix ".xxx"
    XMLElement* armature = nullptr;
    std::unordered_map<std::string, XMLElement*> meshNodeMap;
    for (XMLElement* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        const char* nodeName = node->Attribute("name");
        if (!nodeName || std::string(nodeName) != "Armature") continue;
        armature = node;

        for (XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
            const char* name = child->Attribute("name");
            if (!name) continue;

            if (meshNameSet.count(name)) {
                CollectMeshNodes(child, meshNameSet, meshNodeMap);
            }
        }

        MergeSuffixMeshes(node, meshNameSet, meshNodeMap, doc);

        //continue;
        
        // move built mesh node to its correct place in hierarchy, if that exists

        std::vector<tinyxml2::XMLElement*> meshChildren;    // immediate children of armature that have mesh data
        std::vector<tinyxml2::XMLElement*> nonMeshChildren; // immediate children of armature without mesh data

        std::cout << "scanning immediate children of armature...\n";

        for (tinyxml2::XMLElement* child = armature->FirstChildElement("node"); child; child = child->NextSiblingElement("node")) {
            const char* name = child->Attribute("name");
            std::cout << "child node: " << (name ? name : "<unnamed>") << "\n";

            bool hasMesh = child->FirstChildElement("instance_controller") || child->FirstChildElement("instance_geometry");
            if (hasMesh) {
                std::cout << "  has mesh data\n";
                meshChildren.push_back(child);
            }
            else {
                std::cout << "  no mesh data\n";
                nonMeshChildren.push_back(child);
            }
        }

        // helper to recursively collect all mesh nodes under a given node (including itself if it has mesh data)
        auto collectAllMeshNodes = [](tinyxml2::XMLElement* node, std::vector<tinyxml2::XMLElement*>& outList, auto&& self) -> void {
            if (!node) return;
            bool hasMesh = node->FirstChildElement("instance_controller") || node->FirstChildElement("instance_geometry");
            if (hasMesh) {
                const char* name = node->Attribute("name");
                std::cout << "  mesh node found: " << (name ? name : "<unnamed>") << "\n";
                outList.push_back(node);
            }

            for (tinyxml2::XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node"))
                self(child, outList, self);
            };

        std::vector<tinyxml2::XMLElement*> allMeshes;
        //std::cout << "\ncollecting all mesh nodes under mesh children:\n";
        for (tinyxml2::XMLElement* meshChild : meshChildren)
            collectAllMeshNodes(meshChild, allMeshes, collectAllMeshNodes);

        std::unordered_set<std::string> meshNames;
        for (tinyxml2::XMLElement* meshNode : allMeshes) {
            const char* name = meshNode->Attribute("name");
            if (name)
                meshNames.insert(name);
        }

        std::unordered_set<std::string> seenPlaceholders;
        std::vector<tinyxml2::XMLElement*> placeholderMeshesUnderNonMeshChildren;

        auto collectPlaceholders = [&](tinyxml2::XMLElement* node, auto&& self) -> void {
            const char* name = node->Attribute("name");
            if (name && meshNames.count(name) && !seenPlaceholders.count(name)) {
                //std::cout << "  placeholder mesh found: " << name << "\n";
                seenPlaceholders.insert(name);
                placeholderMeshesUnderNonMeshChildren.push_back(node);
            }

            for (tinyxml2::XMLElement* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node"))
                self(child, self);
            };

        //std::cout << "\ncollecting all mesh placeholders under non-mesh children:\n";
        for (tinyxml2::XMLElement* nonMeshChild : nonMeshChildren)
            collectPlaceholders(nonMeshChild, collectPlaceholders);

        std::unordered_set<std::string> meshNamesToDelete;

        for (tinyxml2::XMLElement* meshNode : allMeshes) {
            const char* name = meshNode->Attribute("name");
            if (!name) continue;

            tinyxml2::XMLElement* matchingPlaceholder = nullptr;
            for (tinyxml2::XMLElement* placeholder : placeholderMeshesUnderNonMeshChildren) {
                const char* placeholderName = placeholder->Attribute("name");
                if (placeholderName && std::string(placeholderName) == name) {
                    matchingPlaceholder = placeholder;
                    break;
                }
            }

            if (!matchingPlaceholder) {
                //std::cout << "no placeholder found for mesh: " << name << "\n";
                continue;
            }

            std::cout << "moving instance_controller(s) from " << name << " to placeholder\n";
            meshNamesToDelete.insert(name);

            for (const char* tag : { "instance_controller", "instance_geometry" }) {
                for (tinyxml2::XMLElement* inst = meshNode->FirstChildElement(tag); inst;) {
                    tinyxml2::XMLElement* next = inst->NextSiblingElement(tag);

                    // sanity check before moving child
                    if (inst->Parent() != meshNode) {
                        std::cout << "warning: instance node parent mismatch before move: " << (void*)inst << "\n";
                    }

                    matchingPlaceholder->InsertEndChild(inst);

                    // sanity check after moving child
                    if (inst->Parent() != matchingPlaceholder) {
                        std::cout << "warning: instance node parent mismatch after move: " << (void*)inst << "\n";
                    }

                    inst = next;
                }
            }
        }

        // clear n reopen cus mysterious mem pointer crash otherwise lol
        tinyxml2::XMLPrinter printer;
        doc.Print(&printer);
        std::string xmlString = printer.CStr();
        doc.Clear();
        doc.Parse(xmlString.c_str());

        for (tinyxml2::XMLElement* armatureNode = doc.FirstChildElement("COLLADA")
            ->FirstChildElement("library_visual_scenes")
            ->FirstChildElement("visual_scene")
            ->FirstChildElement("node"); armatureNode; armatureNode = armatureNode->NextSiblingElement("node")) {
            const char* nodeName = armatureNode->Attribute("name");
            if (!nodeName || std::string(nodeName) != "Armature")
                continue;

            for (tinyxml2::XMLElement* child = armatureNode->FirstChildElement("node"); child;) {
                tinyxml2::XMLElement* nextChild = child->NextSiblingElement("node");
                const char* childName = child->Attribute("name");
                if (childName && meshNamesToDelete.count(childName)) {
                    std::cout << "deleting node by name: " << childName << "\n";
                    armatureNode->DeleteChild(child);
                }
                child = nextChild;
            }
        }
    }

    doc.SaveFile(filePath);
    std::cout << "\nmodified tmp file wahoo\n";
    //system("pause");
}

struct MaterialIndicesContext {
    char** outMeshNames;
    int* outMaterialIndices;
    int maxEntries;
    int count;
    std::vector<std::string> materialIDs;
};

void VisitNodeRecursive(tinyxml2::XMLElement* node, MaterialIndicesContext& ctx) {
    const char* nodeId = node->Attribute("id");
    //printf("visiting node: %s\n", nodeId ? nodeId : "<unnamed>");

    auto* instGeom = node->FirstChildElement("instance_geometry");
    if (instGeom && ctx.count < ctx.maxEntries) {
        const char* geomUrl = instGeom->Attribute("url");
        //printf("  found instance_geometry: url=%s\n", geomUrl ? geomUrl : "<null>");

        if (geomUrl && geomUrl[0] == '#') {
            std::string meshName = geomUrl + 1;
            //printf("  mesh name: %s\n", meshName.c_str());

            auto* bindMat = instGeom->FirstChildElement("bind_material");
            if (bindMat) {
                auto* techCommon = bindMat->FirstChildElement("technique_common");
                if (techCommon) {
                    auto* instMat = techCommon->FirstChildElement("instance_material");
                    if (instMat) {
                        const char* matTarget = instMat->Attribute("target");
                        //printf("  instance_material target: %s\n", matTarget ? matTarget : "<null>");

                        if (matTarget && matTarget[0] == '#') {
                            std::string matId = matTarget + 1;
                            int matIndex = -1;

                            for (size_t i = 0; i < ctx.materialIDs.size(); ++i) {
                                if (ctx.materialIDs[i] == matId) {
                                    matIndex = static_cast<int>(i);
                                    break;
                                }
                            }

                            //printf("  resolved matId: %s to matIndex: %d\n", matId.c_str(), matIndex);

                            if (matIndex >= 0) {
                                strncpy_s(ctx.outMeshNames[ctx.count], 256, meshName.c_str(), _TRUNCATE);
                                ctx.outMaterialIndices[ctx.count] = matIndex;
                                //printf("  stored: mesh=%s, index=%d\n", meshName.c_str(), matIndex);
                                ++ctx.count;
                            }
                        }
                    }
                }
            }
        }
    }

    for (auto* child = node->FirstChildElement("node"); child; child = child->NextSiblingElement("node"))
        VisitNodeRecursive(child, ctx);
}

extern "C" __declspec(dllexport) void __cdecl GetMaterialIndices_C(
    const char* filePath,
    char** outMeshNames,
    int* outMaterialIndices,
    int maxEntries,
    int* outCount)
{
    *outCount = 0;
    if (!filePath || !outMeshNames || !outMaterialIndices || !outCount) return;

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filePath) != tinyxml2::XML_SUCCESS) return;

    auto* collada = doc.FirstChildElement("COLLADA");
    if (!collada) return;

    MaterialIndicesContext ctx{};
    ctx.outMeshNames = outMeshNames;
    ctx.outMaterialIndices = outMaterialIndices;
    ctx.maxEntries = maxEntries;
    ctx.count = 0;

    auto* libMaterials = collada->FirstChildElement("library_materials");
    if (libMaterials) {
        for (auto* mat = libMaterials->FirstChildElement("material"); mat; mat = mat->NextSiblingElement("material")) {
            const char* id = mat->Attribute("id");
            ctx.materialIDs.push_back(id ? id : "");
            //printf("  found material id: %s\n", id ? id : "<null>");
        }
    }

    auto* libVisualScenes = collada->FirstChildElement("library_visual_scenes");
    if (!libVisualScenes) return;

    auto* visualScene = libVisualScenes->FirstChildElement("visual_scene");
    if (!visualScene) return;

    for (auto* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node"))
        VisitNodeRecursive(node, ctx);

    *outCount = ctx.count;
    printf("  total materials assigned: %d\n", ctx.count);
}

static void splitString(const std::string& s, char delim, std::vector<std::string>& out) {
    out.clear();
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        out.push_back(item);
    }
}

extern "C" __declspec(dllexport) void __cdecl PatchDaePreImport_C(const char* daePath, const char* groupsPath)
{
    struct FinalGroup {
        std::string meshName;
        std::vector<unsigned int> triangles;
    };
    std::vector<FinalGroup> finalGroups;

    //printf("[dae-scan] loading DAE: %s\n", daePath);
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(daePath) != tinyxml2::XML_SUCCESS) {
        printf("[dae-scan] failed to load DAE\n");
        return;
    }

    tinyxml2::XMLElement* root = doc.RootElement();
    if (!root) {
        printf("[dae-scan] no root element\n");
        return;
    }

    tinyxml2::XMLElement* libGeoms = root->FirstChildElement("library_geometries");
    tinyxml2::XMLElement* libCtrls = root->FirstChildElement("library_controllers");
    if (!libGeoms) {
        printf("[dae-scan] no library_geometries\n");
        return;
    }
    if (!libCtrls) {
        printf("[dae-scan] no library_controllers (no skin info)\n");
    }

    // turn polylist block to triangles
    for (auto mesh = doc.FirstChildElement("COLLADA")
        ->FirstChildElement("library_geometries")
        ->FirstChildElement("geometry");
        mesh;
        mesh = mesh->NextSiblingElement("geometry")) {

        auto meshElem = mesh->FirstChildElement("mesh");
        if (!meshElem) continue;

        for (auto polylist = meshElem->FirstChildElement("polylist"); polylist;) {
            auto next = polylist->NextSiblingElement("polylist");

            // change tag name to triangles
            auto triangles = doc.NewElement("triangles");

            // copy all attributes
            const tinyxml2::XMLAttribute* attr = polylist->FirstAttribute();
            while (attr) {
                triangles->SetAttribute(attr->Name(), attr->Value());
                attr = attr->Next();
            }

            // move children except <vcount>
            for (auto child = polylist->FirstChildElement(); child;) {
                auto nextChild = child->NextSiblingElement();
                if (std::string(child->Name()) != "vcount")
                    triangles->InsertEndChild(child->DeepClone(&doc));
                child = nextChild;
            }

            // replace in tree
            meshElem->InsertAfterChild(polylist, triangles);
            meshElem->DeleteChild(polylist);

            polylist = next;
        }
    }

    // Build map: geometryId -> per-vertex bone weight map (vertexIndex -> map<boneName,weight>)
    std::unordered_map<std::string, std::unordered_map<unsigned int, std::unordered_map<std::string, float>>> boneWeightsPerGeometry;
    //printf("[dae-scan] scanning controllers for skin weights...\n");
    for (tinyxml2::XMLElement* ctrl = libCtrls->FirstChildElement("controller"); ctrl; ctrl = ctrl->NextSiblingElement("controller")) {
        tinyxml2::XMLElement* skin = ctrl->FirstChildElement("skin");
        if (!skin) continue;
        const char* srcAttr = skin->Attribute("source");
        if (!srcAttr) continue;
        if (srcAttr[0] != '#') continue;
        std::string geomId = std::string(srcAttr + 1);
        //printf("[dae-scan] controller -> skin for geometry id: %s\n", geomId.c_str());

        // find <vertex_weights>
        tinyxml2::XMLElement* vw = skin->FirstChildElement("vertex_weights");
        if (!vw) {
            printf("[dae-scan]  no vertex_weights for %s, skipping skin\n", geomId.c_str());
            continue;
        }

        // parse inputs in vertex_weights to find JOINT and WEIGHT source ids and offsets
        int maxOffset = -1;
        std::string jointSourceId;
        std::string weightSourceId;
        std::unordered_map<std::string, int> vwInputOffset;
        for (tinyxml2::XMLElement* in = vw->FirstChildElement("input"); in; in = in->NextSiblingElement("input")) {
            const char* sem = in->Attribute("semantic");
            const char* src = in->Attribute("source");
            int off = in->IntAttribute("offset", 0);
            if (!sem || !src) continue;
            if (off > maxOffset) maxOffset = off;
            vwInputOffset[std::string(sem)] = off;
            if (src[0] == '#') {
                std::string s = std::string(src + 1);
                if (std::string(sem) == "JOINT") jointSourceId = s;
                else if (std::string(sem) == "WEIGHT") weightSourceId = s;
            }
        }
        int vwStride = (maxOffset >= 0) ? (maxOffset + 1) : 1;
        //printf("[dae-scan]  vertex_weights stride=%d jointSrc='%s' weightSrc='%s'\n", vwStride, jointSourceId.c_str(), weightSourceId.c_str());

        // parse joint names from jointSourceId (a <source> containing a Name_array or similar)
        std::vector<std::string> jointNames;
        if (!jointSourceId.empty()) {
            for (tinyxml2::XMLElement* s = skin->FirstChildElement("source"); s; s = s->NextSiblingElement("source")) {
                const char* sid = s->Attribute("id");
                if (!sid) continue;
                if (std::string(sid) != jointSourceId) continue;
                tinyxml2::XMLElement* nameArray = s->FirstChildElement("Name_array");
                if (!nameArray) nameArray = s->FirstChildElement("name_array"); // fallback
                if (!nameArray) continue;
                const char* namesText = nameArray->GetText();
                if (!namesText) continue;
                std::stringstream ss(namesText);
                std::string tok;
                while (ss >> tok) {
                    jointNames.push_back(tok);
                }
                //printf("[dae-scan]   parsed %zu joint names\n", jointNames.size());
                break;
            }
        }

        // parse weights floats from weightSourceId
        std::vector<float> weightValues;
        if (!weightSourceId.empty()) {
            for (tinyxml2::XMLElement* s = skin->FirstChildElement("source"); s; s = s->NextSiblingElement("source")) {
                const char* sid = s->Attribute("id");
                if (!sid) continue;
                if (std::string(sid) != weightSourceId) continue;
                tinyxml2::XMLElement* fa = s->FirstChildElement("float_array");
                if (!fa) continue;
                const char* ft = fa->GetText();
                if (!ft) continue;
                std::stringstream ss(ft);
                float fv;
                while (ss >> fv) weightValues.push_back(fv);
                //printf("[dae-scan]   parsed %zu weight floats\n", weightValues.size());
                break;
            }
        }

        // parse <vcount> and <v>
        tinyxml2::XMLElement* vcountElem = vw->FirstChildElement("vcount");
        tinyxml2::XMLElement* vElem = vw->FirstChildElement("v");
        if (!vcountElem || !vElem) {
            printf("[dae-scan]  vertex_weights missing vcount or v, skipping\n");
            continue;
        }
        std::vector<int> vcounts;
        {
            const char* vcText = vcountElem->GetText();
            if (vcText) {
                std::stringstream ssv(vcText);
                int vi;
                while (ssv >> vi) vcounts.push_back(vi);
            }
        }
        std::vector<unsigned int> vvals;
        {
            const char* vText = vElem->GetText();
            if (vText) {
                std::stringstream ssv(vText);
                unsigned int vv;
                while (ssv >> vv) vvals.push_back(vv);
            }
        }
        //printf("[dae-scan]  vertex_weights vcount entries=%zu, v tokens=%zu\n", vcounts.size(), vvals.size());

        // now iterate vertices and build per-vertex bone weights: jointIndex,weightIndex pairs per influence
        std::unordered_map<unsigned int, std::unordered_map<std::string, float>> perVertexWeights;
        size_t cursor = 0;
        for (size_t vi = 0; vi < vcounts.size(); ++vi) {
            int numInf = vcounts[vi];
            for (int inf = 0; inf < numInf; ++inf) {
                if (cursor + vwStride > vvals.size()) {
                    printf("[dae-scan]   malformed v tokens, cursor out of range\n");
                    break;
                }
                unsigned int jointIndexToken = 0;
                unsigned int weightIndexToken = 0;
                // find tokens by offset
                std::unordered_map<std::string, int>::iterator itJointOff = vwInputOffset.find("JOINT");
                std::unordered_map<std::string, int>::iterator itWeightOff = vwInputOffset.find("WEIGHT");
                if (itJointOff != vwInputOffset.end()) {
                    int offsetJoint = itJointOff->second;
                    jointIndexToken = vvals[cursor + offsetJoint];
                }
                if (itWeightOff != vwInputOffset.end()) {
                    int offsetWeight = itWeightOff->second;
                    weightIndexToken = vvals[cursor + offsetWeight];
                }
                std::string boneName = "(unknown)";
                if (jointIndexToken < jointNames.size()) boneName = jointNames[jointIndexToken];
                float wval = 0.0f;
                if (weightIndexToken < weightValues.size()) wval = weightValues[weightIndexToken];
                if (wval != 0.0f) {
                    perVertexWeights[(unsigned int)vi][boneName] += wval;
                }
                cursor += vwStride;
            }
        }
        // store in global map
        boneWeightsPerGeometry[geomId] = perVertexWeights;
    //    printf("[dae-scan]  stored bone weights for geometry '%s' vertex-count=%zu\n", geomId.c_str(), perVertexWeights.size());
    }

    // Now iterate geometries and group triangles by bone sets
    //printf("[dae-scan] scanning geometries and triangles...\n");
    for (tinyxml2::XMLElement* geom = libGeoms->FirstChildElement("geometry"); geom; geom = geom->NextSiblingElement("geometry")) {
        const char* geomIdC = geom->Attribute("id");
        std::string geomId;
        if (geomIdC) geomId = geomIdC;
        else {
            printf("[dae-scan] geometry without id, skipping\n");
            continue;
        }
        tinyxml2::XMLElement* meshElem = geom->FirstChildElement("mesh");
        if (!meshElem) {
            printf("[dae-scan] geometry '%s' has no <mesh>, skipping\n", geomId.c_str());
            continue;
        }
        //printf("[dae-scan] geometry '%s'\n", geomId.c_str());

        size_t triGlobalOffset = 0;

        for (tinyxml2::XMLElement* tri = meshElem->FirstChildElement("triangles"); tri; tri = tri->NextSiblingElement("triangles")) {
            const char* mat = tri->Attribute("material");
            const char* countAttr = tri->Attribute("count");
            //printf("[dae-scan]  found <triangles> material='%s' count='%s'\n", mat ? mat : "(null)", countAttr ? countAttr : "(null)");

            // parse input offsets to find VERTEX offset
            std::unordered_map<std::string, int> inputOffsets;
            int triMaxOffset = -1;
            for (tinyxml2::XMLElement* in = tri->FirstChildElement("input"); in; in = in->NextSiblingElement("input")) {
                const char* sem = in->Attribute("semantic");
                int off = in->IntAttribute("offset", 0);
                if (sem) inputOffsets[std::string(sem)] = off;
                if (off > triMaxOffset) triMaxOffset = off;
            }
            int triInputCount = (triMaxOffset >= 0) ? (triMaxOffset + 1) : 1;
            tinyxml2::XMLElement* pElem = tri->FirstChildElement("p");
            if (!pElem || !pElem->GetText()) {
                printf("[dae-scan]   <triangles> has no <p> content, skipping this block\n");
                continue;
            }
            std::string ptext = pElem->GetText();
            std::vector<unsigned int> pvals;
            {
                std::stringstream ssp(ptext);
                unsigned int v;
                while (ssp >> v) pvals.push_back(v);
            }
            size_t numbersPerTri = (size_t)triInputCount * 3u;
            if (numbersPerTri == 0) {
                printf("[dae-scan]   bad input count, skipping\n");
                continue;
            }
            size_t triCount = 0;
            unsigned int countAttrVal = tri->UnsignedAttribute("count", 0);
            if (countAttrVal > 0) triCount = (size_t)countAttrVal;
            else {
                if (pvals.size() % numbersPerTri == 0) triCount = pvals.size() / numbersPerTri;
                else triCount = pvals.size() / numbersPerTri; // fallback
            }
            //printf("[dae-scan]   tokens=%zu triCount=%zu inputCount=%d\n", pvals.size(), triCount, triInputCount);

            int vertexOffset = 0;
            std::unordered_map<std::string, int>::iterator itVert = inputOffsets.find("VERTEX");
            if (itVert != inputOffsets.end()) vertexOffset = itVert->second;
            else {
                printf("[dae-scan]   no VERTEX input found, assuming offset 0\n");
                vertexOffset = 0;
            }

            // collect triangles for this block only
            std::vector<std::array<unsigned int, 3>> blockTriangles;
            blockTriangles.reserve(triCount);
            for (size_t t = 0; t < triCount; ++t) {
                size_t triStart = t * numbersPerTri;
                std::array<unsigned int, 3> triVerts;
                for (int vtx = 0; vtx < 3; ++vtx) {
                    size_t tokStart = triStart + (size_t)vtx * (size_t)triInputCount;
                    size_t idxPos = tokStart + (size_t)vertexOffset;
                    unsigned int vertIndex = 0;
                    if (idxPos < pvals.size()) vertIndex = pvals[idxPos];
                    else {
                        printf("[dae-scan]    p token missing for geometry '%s' tri %zu vtx %d\n", geomId.c_str(), t, vtx);
                        vertIndex = 0;
                    }
                    triVerts[vtx] = vertIndex;
                }
                blockTriangles.push_back(triVerts);
            }

            // build triangle -> boneSet mapping using boneWeightsPerGeometry for this block only
            std::map<std::set<std::string>, std::vector<unsigned int>> boneSetToTriangleIndices;
            auto bwGeomIt = boneWeightsPerGeometry.find(geomId);
            for (unsigned int ti = 0; ti < blockTriangles.size(); ++ti) {
                std::array<unsigned int, 3> triVerts = blockTriangles[ti];
                std::set<std::string> boneSet;
                if (bwGeomIt != boneWeightsPerGeometry.end()) {
                    std::unordered_map<unsigned int, std::unordered_map<std::string, float>>& perVert = bwGeomIt->second;
                    for (int k = 0; k < 3; ++k) {
                        unsigned int vindex = triVerts[k];
                        auto pvIt = perVert.find(vindex);
                        if (pvIt != perVert.end()) {
                            for (auto const& ppair : pvIt->second) {
                                boneSet.insert(ppair.first);
                            }
                        }
                    }
                }
                boneSetToTriangleIndices[boneSet].push_back(ti);
            }

            // convert map to list of group structs
            struct BoneGroup {
                std::set<std::string> bones;
                std::vector<unsigned int> triangles;
            };
            std::vector<BoneGroup> groups;
            for (auto& pair : boneSetToTriangleIndices) {
                groups.push_back({ pair.first, pair.second });
            }

            // 1) subset merges: if group A bones ⊆ group B bones, merge A into B (if total <=6)
            bool mergedAny = true;
            while (mergedAny) {
                mergedAny = false;
                for (size_t i = 0; i < groups.size(); ++i) {
                    for (size_t j = 0; j < groups.size(); ++j) {
                        if (i == j) continue;
                        // check if bones[i] is subset of bones[j]
                        bool isSubset = std::includes(groups[j].bones.begin(), groups[j].bones.end(),
                            groups[i].bones.begin(), groups[i].bones.end());
                        if (isSubset) {
                            if (groups[j].bones.size() <= 6) {
                                groups[j].triangles.insert(groups[j].triangles.end(),
                                    groups[i].triangles.begin(), groups[i].triangles.end());
                                groups.erase(groups.begin() + i);
                                mergedAny = true;
                                break;
                            }
                        }
                    }
                    if (mergedAny) break;
                }
            }

            // 2) then merge any two groups where combined bones <= 6 (normal brute force)
            mergedAny = true;
            while (mergedAny) {
                mergedAny = false;
                for (size_t i = 0; i < groups.size(); ++i) {
                    for (size_t j = i + 1; j < groups.size(); ++j) {
                        std::set<std::string> combined = groups[i].bones;
                        combined.insert(groups[j].bones.begin(), groups[j].bones.end());
                        if (combined.size() <= 6) {
                            groups[i].bones = combined;
                            groups[i].triangles.insert(groups[i].triangles.end(),
                                groups[j].triangles.begin(), groups[j].triangles.end());
                            groups.erase(groups.begin() + j);
                            mergedAny = true;
                            break;
                        }
                    }
                    if (mergedAny) break;
                }
            }

            // dump final groups for this block
            //printf("== FINAL MERGED GROUPS (%llu total) for <triangles> block ==\n", groups.size());
            //for (size_t i = 0; i < groups.size(); ++i) {
            //    printf("  Group %llu: %llu triangles | bones used (%llu): ", i, groups[i].triangles.size(), groups[i].bones.size());
            //    for (const auto& b : groups[i].bones) printf("%s ", b.c_str());
            //    printf("\n");
            //}

            // append groups with global tri indices offset
            for (size_t i = 0; i < groups.size(); ++i) {
                FinalGroup fg;
                fg.meshName = geomId;
                for (auto tri : groups[i].triangles)
                    fg.triangles.push_back((unsigned int)(triGlobalOffset + tri));
                finalGroups.push_back(std::move(fg));
            }

            triGlobalOffset += blockTriangles.size();
        }
    }
    printf("[dae-scan] finished. finalGroups.size=%zu\n", finalGroups.size());

    // end of group building, below creates new triangles blocks
    // map meshName -> vector of indices into finalGroups vector
    std::unordered_map<std::string, std::vector<size_t>> meshNameToGroupIdx;
    for (size_t i = 0; i < finalGroups.size(); ++i) {
        meshNameToGroupIdx[finalGroups[i].meshName].push_back(i);
    }

    unsigned int geomCounter = 0;
    for (XMLElement* geom = libGeoms->FirstChildElement("geometry"); geom; geom = geom->NextSiblingElement("geometry"), geomCounter++) {
        XMLElement* meshElem = geom->FirstChildElement("mesh");
        if (!meshElem) continue;

        const char* geomId = geom->Attribute("id");
        std::string geomName = geomId ? geomId : "";

        //printf("[patch] geometry id='%s'\n", geomName.c_str());

        auto mgIt = meshNameToGroupIdx.find(geomName);
        if (mgIt == meshNameToGroupIdx.end()) {
            printf("[patch] no final groups for geometry '%s'\n", geomName.c_str());
            continue;
        }

        // collect all original <triangles> blocks in this mesh
        struct OrigTriBlock {
            XMLElement* elem;
            std::string material;
            std::vector<unsigned int> triIndices; // indices 0..n for this block triangles
            int inputCount;
            std::vector<unsigned int> pIndices; // raw indices from <p> for entire block
        };
        std::vector<OrigTriBlock> origTriBlocks;

        for (XMLElement* tri = meshElem->FirstChildElement("triangles"); tri; tri = tri->NextSiblingElement("triangles")) {
            OrigTriBlock block;
            block.elem = tri;
            block.material = tri->Attribute("material") ? tri->Attribute("material") : "";

            int maxOffset = -1;
            for (XMLElement* input = tri->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
                int off = input->IntAttribute("offset", 0);
                if (off > maxOffset) maxOffset = off;
            }
            block.inputCount = maxOffset + 1;

            XMLElement* pElem = tri->FirstChildElement("p");
            if (!pElem || !pElem->GetText()) {
                //printf("[patch] triangles block missing <p> content, skipping\n");
                continue;
            }
            std::string ptext = pElem->GetText();
            std::stringstream ssp(ptext);
            unsigned int idx;
            while (ssp >> idx) block.pIndices.push_back(idx);

            unsigned int triCount = (unsigned int)(block.pIndices.size() / (block.inputCount * 3));
            for (unsigned int i = 0; i < triCount; ++i) block.triIndices.push_back(i);

            origTriBlocks.push_back(std::move(block));
            //printf("[patch] found <triangles> block material='%s' triCount=%u\n", origTriBlocks.back().material.c_str(), triCount);
        }

        if (origTriBlocks.empty()) {
            printf("[patch] no <triangles> blocks in mesh, skipping\n");
            continue;
        }

        // for each group, find which original block contains its first triangle index
        std::unordered_map<XMLElement*, std::vector<size_t>> blockToGroups; // block ptr -> finalGroups indices
        std::vector<size_t> groupsForMesh = mgIt->second;

        for (size_t gi : groupsForMesh) {
            const FinalGroup& fg = finalGroups[gi];
            if (fg.triangles.empty()) continue;
            unsigned int firstTri = fg.triangles[0];

            bool found = false;
            for (auto& block : origTriBlocks) {
                if (firstTri < block.triIndices.size()) {
                    // check if firstTri fits inside this block (global tri index must be relative to block)
                    // but finalGroups triangles indices are global per mesh, so we assume firstTri is local to mesh,
                    // so we must confirm firstTri fits into this block’s range by offset.
                    // we must find block’s global start triangle index on mesh (sum previous blocks' tri count)
                    // let's do that:
                    unsigned int blockStart = 0;
                    for (auto& b2 : origTriBlocks) {
                        if (b2.elem == block.elem) break;
                        blockStart += (unsigned int)b2.triIndices.size();
                    }
                    if (firstTri >= blockStart && firstTri < blockStart + block.triIndices.size()) {
                        blockToGroups[block.elem].push_back(gi);
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                printf("[patch] warning: could not find original <triangles> block for group first triangle %u\n", firstTri);
            }
        }

        // now for each original triangles block with groups assigned, delete original block and add new <triangles> blocks per group
        for (auto& pair : blockToGroups) {
            XMLElement* origBlock = pair.first;
            std::vector<size_t>& groupIndices = pair.second;

            // find corresponding OrigTriBlock info
            OrigTriBlock* blockInfo = nullptr;
            for (auto& b : origTriBlocks) {
                if (b.elem == origBlock) {
                    blockInfo = &b;
                    break;
                }
            }
            if (!blockInfo) continue;

            std::string materialName = blockInfo->material;
            int inputCount = blockInfo->inputCount;

            // clone input elements from original block once
            std::vector<XMLElement*> inputClones;
            for (XMLElement* input = origBlock->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
                inputClones.push_back(input->DeepClone(&doc)->ToElement());
            }

            XMLElement* parentMesh = origBlock->Parent()->ToElement();
            if (!parentMesh) continue;

            // delete original block
            parentMesh->DeleteChild(origBlock);

            for (size_t gi : groupIndices) {
                const FinalGroup& fg = finalGroups[gi];

                // build p text by gathering indices of each triangle from original pIndices
                std::string ptext;
                for (unsigned int triIdx : fg.triangles) {
                    // calculate local triangle index in this block
                    unsigned int blockStart = 0;
                    for (auto& b2 : origTriBlocks) {
                        if (b2.elem == origBlock) break;
                        blockStart += (unsigned int)b2.triIndices.size();
                    }
                    unsigned int localTri = triIdx - blockStart;
                    if (localTri >= blockInfo->triIndices.size()) {
                        printf("[patch] warning: triangle index %u out of bounds for block with %zu tris\n", triIdx, blockInfo->triIndices.size());
                        continue;
                    }
                    size_t baseIdx = localTri * inputCount * 3;
                    if (baseIdx + (inputCount * 3) > blockInfo->pIndices.size()) {
                        printf("[patch] warning: index out of pIndices range\n");
                        continue;
                    }
                    for (int k = 0; k < inputCount * 3; ++k) {
                        ptext += std::to_string(blockInfo->pIndices[baseIdx + k]) + " ";
                    }
                }
                if (!ptext.empty()) ptext.pop_back();

                XMLElement* newTri = doc.NewElement("triangles");
                if (!materialName.empty()) newTri->SetAttribute("material", materialName.c_str());

                int triCount = (int)(fg.triangles.size());
                newTri->SetAttribute("count", triCount);

                for (auto* inpClone : inputClones) {
                    newTri->InsertEndChild(inpClone->DeepClone(&doc));
                }

                XMLElement* pElem = doc.NewElement("p");
                pElem->SetText(ptext.c_str());
                newTri->InsertEndChild(pElem);

                parentMesh->InsertEndChild(newTri);
            }
        }
    }
    doc.SaveFile(daePath);
}

extern "C" __declspec(dllexport) void __cdecl PatchDaePreAll_C(const char* daePath)
{
    tinyxml2::XMLDocument doc;
    doc.LoadFile(daePath);

    tinyxml2::XMLElement* collada = doc.FirstChildElement("COLLADA");
    if (!collada) return;

    tinyxml2::XMLElement* libVisualScenes = collada->FirstChildElement("library_visual_scenes");
    if (!libVisualScenes) return;

    tinyxml2::XMLElement* visualScene = libVisualScenes->FirstChildElement("visual_scene");
    if (!visualScene) return;

    auto toLower = [](const char* s) {
        std::string r;
        while (*s) r += (char)std::tolower(*s++);
        return r;
        };

    tinyxml2::XMLElement* armature = nullptr;
    for (tinyxml2::XMLElement* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        const char* id = node->Attribute("id");
        const char* name = node->Attribute("name");
        if ((id && toLower(id) == "armature") || (name && toLower(name) == "armature")) {
            armature = node;
            break;
        }
    }

    if (!armature) {
        return; // nothing to do if no armature node
    }

    // move all siblings of armature into armature
    std::vector<tinyxml2::XMLElement*> toMove;
    for (tinyxml2::XMLElement* node = visualScene->FirstChildElement("node"); node; node = node->NextSiblingElement("node")) {
        if (node != armature) {
            toMove.push_back(node);
        }
    }

    for (tinyxml2::XMLElement* node = visualScene->FirstChildElement("node"); node; ) {
        tinyxml2::XMLElement* next = node->NextSiblingElement("node");
        if (node != armature) {
            armature->InsertEndChild(node->DeepClone(&doc)); // clone into armature
            visualScene->DeleteChild(node);                 // delete original
        }
        node = next;
    }

    doc.SaveFile(daePath);
}