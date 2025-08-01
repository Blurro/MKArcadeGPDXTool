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
    printf("visiting node: %s\n", nodeId ? nodeId : "<unnamed>");

    auto* instGeom = node->FirstChildElement("instance_geometry");
    if (instGeom && ctx.count < ctx.maxEntries) {
        const char* geomUrl = instGeom->Attribute("url");
        printf("  found instance_geometry: url=%s\n", geomUrl ? geomUrl : "<null>");

        if (geomUrl && geomUrl[0] == '#') {
            std::string meshName = geomUrl + 1;
            printf("  mesh name: %s\n", meshName.c_str());

            auto* bindMat = instGeom->FirstChildElement("bind_material");
            if (bindMat) {
                auto* techCommon = bindMat->FirstChildElement("technique_common");
                if (techCommon) {
                    auto* instMat = techCommon->FirstChildElement("instance_material");
                    if (instMat) {
                        const char* matTarget = instMat->Attribute("target");
                        printf("  instance_material target: %s\n", matTarget ? matTarget : "<null>");

                        if (matTarget && matTarget[0] == '#') {
                            std::string matId = matTarget + 1;
                            int matIndex = -1;

                            for (size_t i = 0; i < ctx.materialIDs.size(); ++i) {
                                if (ctx.materialIDs[i] == matId) {
                                    matIndex = static_cast<int>(i);
                                    break;
                                }
                            }

                            printf("  resolved matId: %s to matIndex: %d\n", matId.c_str(), matIndex);

                            if (matIndex >= 0) {
                                strncpy_s(ctx.outMeshNames[ctx.count], 256, meshName.c_str(), _TRUNCATE);
                                ctx.outMaterialIndices[ctx.count] = matIndex;
                                printf("  stored: mesh=%s, index=%d\n", meshName.c_str(), matIndex);
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

extern "C" __declspec(dllexport)
void __cdecl GetMaterialIndices_C(
    const char* filePath,
    char** outMeshNames,
    int* outMaterialIndices,
    int maxEntries,
    int* outCount)
{
    printf("GetMaterialIndices_C called\n");
    printf("  filePath: %s\n", filePath ? filePath : "<null>");
    printf("  outMeshNames: %p\n", outMeshNames);
    printf("  outMaterialIndices: %p\n", outMaterialIndices);
    printf("  maxEntries: %d\n", maxEntries);
    printf("  outCount: %p\n", outCount);

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
            printf("  found material id: %s\n", id ? id : "<null>");
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