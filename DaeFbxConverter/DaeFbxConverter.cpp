#include <fbxsdk.h>
#include <tinyxml2.h>
#include <string>
#include <vector>
#include <sstream>
#include <set>

// Helper: Find the correct writer format for FBX or DAE
static int GetWriterFormat(FbxManager* mgr, const std::string& path) {
    auto pos = path.find_last_of('.');
    if (pos == std::string::npos) return -1;
    std::string ext = path.substr(pos + 1);
    FbxIOPluginRegistry* reg = mgr->GetIOPluginRegistry();

    if (ext == "dae") {
        int count = reg->GetWriterFormatCount();
        for (int i = 0; i < count; ++i)
            if (FbxString(reg->GetWriterFormatDescription(i)).Find("COLLADA") != -1)
                return i;
    }
    else if (ext == "fbx") {
        int count = reg->GetWriterFormatCount();
        for (int i = 0; i < count; ++i)
            if (FbxString(reg->GetWriterFormatDescription(i)).Find("FBX") != -1)
                return i;
    }
    return -1;
}

static FbxNode* FindBoneByName(FbxNode* node, const std::string& name) {
    if (!node) return nullptr;
    if (node->GetName() == name) return node;
    for (int i = 0; i < node->GetChildCount(); ++i) {
        FbxNode* found = FindBoneByName(node->GetChild(i), name);
        if (found) return found;
    }
    return nullptr;
}

static void FillDAEMatrices(FbxScene* scene, const std::string& daeFile) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(daeFile.c_str()) != tinyxml2::XML_SUCCESS) return;

    FbxNode* root = scene->GetRootNode();
    if (!root) return;

    auto* collada = doc.FirstChildElement("COLLADA");
    if (!collada) return;
    auto* libControllers = collada->FirstChildElement("library_controllers");
    if (!libControllers) return;

    for (auto* controller = libControllers->FirstChildElement("controller");
        controller;
        controller = controller->NextSiblingElement("controller")) {

        auto* skin = controller->FirstChildElement("skin");
        if (!skin) continue;

        auto* jointsElem = skin->FirstChildElement("joints");
        if (!jointsElem) continue;

        tinyxml2::XMLElement* jointsSource = nullptr;
        tinyxml2::XMLElement* matricesSource = nullptr;

        for (auto* input = jointsElem->FirstChildElement("input"); input; input = input->NextSiblingElement("input")) {
            const char* semantic = input->Attribute("semantic");
            const char* sourceId = input->Attribute("source");
            if (!semantic || !sourceId) continue;

            std::string idStr = sourceId[0] == '#' ? sourceId + 1 : sourceId;

            for (auto* src = skin->FirstChildElement("source"); src; src = src->NextSiblingElement("source")) {
                const char* sid = src->Attribute("id");
                if (!sid || idStr != sid) continue;

                if (!strcmp(semantic, "JOINT")) jointsSource = src;
                else if (!strcmp(semantic, "INV_BIND_MATRIX")) matricesSource = src;
            }
        }

        if (!jointsSource || !matricesSource) continue;

        auto* nameArray = jointsSource->FirstChildElement("Name_array");
        auto* floatArray = matricesSource->FirstChildElement("float_array");
        if (!nameArray || !floatArray) continue;

        std::vector<std::string> jointNames;
        std::istringstream iss(nameArray->GetText() ? nameArray->GetText() : "");
        std::string j;
        while (iss >> j) jointNames.push_back(j);

        std::ostringstream matricesStream;
        FbxTime t; t.SetFrame(0, FbxTime::eFrames30);
        int count = 0;

        for (auto& name : jointNames) {
            FbxNode* bone = FindBoneByName(root, name);
            FbxAMatrix m;
            if (bone) m = bone->EvaluateGlobalTransform(t).Inverse();
            else m.SetIdentity();

            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r) {
                    matricesStream << m.Get(r, c) << " ";
                    ++count;
                }
        }

        std::string matricesText = matricesStream.str();
        if (!matricesText.empty() && matricesText.back() == ' ') matricesText.pop_back();
        floatArray->SetText(matricesText.c_str());
        floatArray->SetAttribute("count", count);
    }

    doc.SaveFile(daeFile.c_str());
}

static void CleanDAEMaterials(const std::set<std::string>& validMats, const std::string& daeFile) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(daeFile.c_str()) != tinyxml2::XML_SUCCESS) return;

    tinyxml2::XMLElement* collada = doc.FirstChildElement("COLLADA");
    if (!collada) return;

    // ---- delete <unit> inside <asset> ----
    tinyxml2::XMLElement* asset = collada->FirstChildElement("asset");
    if (asset) {
        tinyxml2::XMLElement* unit = asset->FirstChildElement("unit");
        if (unit) asset->DeleteChild(unit);
    }

    // ---- clean library_materials first ----
    tinyxml2::XMLElement* libMats = collada->FirstChildElement("library_materials");
    if (libMats) {
        tinyxml2::XMLNode* node = libMats->FirstChild();
        while (node) {
            tinyxml2::XMLNode* next = node->NextSibling();
            tinyxml2::XMLElement* matElem = node->ToElement();
            if (matElem) {
                const char* id = matElem->Attribute("id");
                if (!id || validMats.find(id) == validMats.end()) {
                    libMats->DeleteChild(node);
                }
            }
            node = next;
        }
    }

    // ---- clean triangles in all geometries ----
    tinyxml2::XMLElement* libGeoms = collada->FirstChildElement("library_geometries");
    if (libGeoms) {
        for (tinyxml2::XMLElement* geom = libGeoms->FirstChildElement("geometry"); geom; geom = geom->NextSiblingElement("geometry")) {
            tinyxml2::XMLElement* mesh = geom->FirstChildElement("mesh");
            if (!mesh) continue;

            tinyxml2::XMLNode* triNode = mesh->FirstChild();
            while (triNode) {
                tinyxml2::XMLNode* nextTri = triNode->NextSibling();
                tinyxml2::XMLElement* triElem = triNode->ToElement();
                if (triElem && std::string(triElem->Name()) == "triangles") {
                    const char* mat = triElem->Attribute("material");
                    if (!mat || validMats.find(mat) == validMats.end()) {
                        mesh->DeleteChild(triNode);
                    }
                }
                triNode = nextTri;
            }
        }
    }

    doc.SaveFile(daeFile.c_str());
}

static bool Convert(const char* inFile, const char* outFile) {
    FbxManager* m = FbxManager::Create();
    if (!m) return false;

    FbxIOSettings* ios = FbxIOSettings::Create(m, IOSROOT);
    m->SetIOSettings(ios);

    FbxImporter* imp = FbxImporter::Create(m, "");
    if (!imp->Initialize(inFile, -1, m->GetIOSettings())) {
        printf("import fail: %s\n", imp->GetStatus().GetErrorString());
        imp->Destroy(); m->Destroy();
        return false;
    }

    FbxScene* scene = FbxScene::Create(m, "scene");
    imp->Import(scene);
    imp->Destroy();

    std::set<std::string> validMats; // <-- capture only the real FBX materials
    auto pos = std::string(outFile).find_last_of('.');
    if (pos != std::string::npos && std::string(outFile).substr(pos + 1) == "dae") {
        int matCount = scene->GetMaterialCount();
        for (int i = 0; i < matCount; ++i) {
            FbxSurfaceMaterial* mat = scene->GetMaterial(i);
            if (mat) {
                validMats.insert(mat->GetName());
            }
        }
    }

    FbxExporter* exp = FbxExporter::Create(m, "");
    int format = GetWriterFormat(m, outFile);

    if (!exp->Initialize(outFile, format, m->GetIOSettings())) {
        printf("export fail: %s\n", exp->GetStatus().GetErrorString());
        exp->Destroy(); m->Destroy();
        return false;
    }

    bool ok = exp->Export(scene);
    exp->Destroy();

    FillDAEMatrices(scene, outFile);
    CleanDAEMaterials(validMats, outFile); // <-- pass only the real valid materials

    m->Destroy();
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    if (!Convert(argv[1], argv[2])) return 1;
    return 0;
}