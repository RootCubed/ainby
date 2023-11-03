#include <imgui.h>
#include <tinyfiledialogs.h>

#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "ainb_editor.hpp"
#include "file_formats/ainb.hpp"
#include "node_editor/imgui_node_editor.h"

AINBEditor::AINBEditor() {
    edConfig.SettingsFile = nullptr;
    edConfig.NavigateButtonIndex = 2;
}

AINBEditor::~AINBEditor() {
    ed::DestroyEditor(edContext);
}

void AINBEditor::RegisterAINB(AINB &ainb) {
    this->ainb = &ainb;
    guiNodes.clear();
    for (AINB::Node &node : ainb.nodes) {
        guiNodes.emplace_back(node);
    }

    if (edContext != nullptr) {
        ed::DestroyEditor(edContext);
    }
    edContext = ed::CreateEditor(&edConfig);
    selectedNodeIdx = -1;
    selectedCommand = "";
}

void AINBEditor::UnloadAINB() {
    guiNodes.clear();
    ainb = nullptr;
    selectedNodeIdx = -1;
    selectedCommand = "";
}

void AINBEditor::DrawInspector() {
    ImGui::Text("Name: %s", ainb->name.c_str());
    ImGui::Text("File Category: %s", ainb->fileCategory.c_str());

    int newSelectedNodeIdx = -1;
    if (ImGui::TreeNode("Commands")) {
        if (ImGui::BeginListBox("##Commands", ImVec2(FLT_MIN, 200))) {
            for (AINB::Command &cmd : ainb->commands) {
                if (ImGui::Selectable(cmd.name.c_str(), selectedCommand == cmd.name)) {
                    selectedCommand = cmd.name;
                    newSelectedNodeIdx = cmd.rootNode->Idx();
                }
            }
            ImGui::EndListBox();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Nodes")) {
        for (size_t i = 0; i < ainb->nodes.size(); i++) {
            std::stringstream title;
            title << "Node " << i << ": " << ainb->nodes[i].TypeName();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
            if (i == selectedNodeIdx) {
                flags |= ImGuiTreeNodeFlags_Selected;
            }
            if (ImGui::TreeNodeEx(title.str().c_str(), flags)) {
                ImGui::Text("Type: %s", ainb->nodes[i].TypeName().c_str());
                ImGui::Text("Index: %d", ainb->nodes[i].Idx());
                ImGui::Text("Flags: %08x", ainb->nodes[i].flags);

                std::stringstream precondString;
                for (u32 precond : ainb->nodes[i].preconditionNodes) {
                    precondString << precond << " ";
                }
                ImGui::Text("Preconditions: %s", precondString.str().c_str());

                ImGui::Text("Params:");
                for (AINB::Param &param : ainb->nodes[i].GetParams()) {
                    switch (param.paramType) {
                        case AINB::ParamType::Immediate: {
                            AINB::ImmediateParam &ip = static_cast<AINB::ImmediateParam &>(param);
                            ImGui::Text(" Imm %s = %s",
                                param.name.c_str(), AINB::AINBValueToString(ip.value).c_str());
                            break;
                        }
                        case AINB::ParamType::Input: {
                            AINB::InputParam &ip = static_cast<AINB::InputParam &>(param);

                            switch (ip.inputNodeIdxs.size()) {
                                case 0:
                                    ImGui::Text(" Input %s = %s", param.name.c_str(), AINB::AINBValueToString(ip.defaultValue).c_str());
                                    break;
                                case 1:
                                    ImGui::Text(" Input %s: N%d.%d (default = %s)", param.name.c_str(),
                                        ip.inputNodeIdxs[0], ip.inputParamIdxs[0],
                                        AINB::AINBValueToString(ip.defaultValue).c_str());
                                    break;
                                default:
                                    ImGui::Text(" Input %s: Multi-param:", param.name.c_str());
                                    for (size_t i = 0; i < ip.inputNodeIdxs.size(); i++) {
                                        ImGui::Text("  N%d.%d", ip.inputNodeIdxs[i], ip.inputParamIdxs[i]);
                                    }
                                    break;
                            }
                            break;
                        }
                        case AINB::ParamType::Output:
                            ImGui::Text(" Output %s", param.name.c_str());
                            break;
                    }
                }

                ImGui::Text("Links:");
                for (AINB::NodeLink &link : ainb->nodes[i].nodeLinks) {
                    const char *valueString = AINB::AINBValueToString(link.value).c_str();
                    ImGui::Text(" Type %d: to node %d with %s", static_cast<int>(link.type), link.idx, link.name.c_str());
                }
                ImGui::TreePop();
            }
            if (ImGui::IsItemClicked()) {
                newSelectedNodeIdx = i;
            }
        }
        ImGui::TreePop();
    }
    selectedNodeIdx = newSelectedNodeIdx;

    if (ImGui::TreeNode("Global Params")) {
        for (AINB::Gparams::Gparam &param : ainb->gparams.gparams) {
            if (ImGui::TreeNode(param.name.c_str())) {
                ImGui::Text("Type %s", param.TypeString().c_str());
                ImGui::Text("Default value: %s", AINB::AINBValueToString(param.defaultValue).c_str());
                ImGui::Text("Notes: %s", param.notes.c_str());
                if (param.hasFileRef) {
                    ImGui::Text("File reference: %s", param.fileRef.c_str());
                }

                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Embedded AINBs")) {
        for (const AINB::EmbeddedAINB &e : ainb->embeddedAinbs) {
            ImGui::Text(" %s", e.name.c_str());
        }
        ImGui::TreePop();
    }
}

void AINBEditor::DrawNodeEditor() {
    ImGui::Text("Node Viewer");
    ImGui::SameLine();
    bool wantAutoLayout = ImGui::Button("Auto layout");
    ImGui::SameLine();
    bool wantLoadPositions = ImGui::Button("Load node positions");
    ImGui::SameLine();
    bool wantSavePositions = ImGui::Button("Save node positions");
    ImGui::Separator();

    ed::SetCurrentEditor(edContext);
    ed::Begin("AINB Editor", ImVec2(0.0, 0.0f));

    if (newAuxInfos.size() > 0) {
        for (AINBImGuiNode &guiNode : guiNodes) {
            if (newAuxInfos.contains(guiNode.GetNode().Idx())) {
                guiNode.LoadAuxInfo(newAuxInfos[guiNode.GetNode().Idx()]);
            }
        }
        newAuxInfos.clear();
    }

    for (AINBImGuiNode &guiNode : guiNodes) {
        guiNode.Draw();
    }
    for (AINBImGuiNode &guiNode : guiNodes) {
        guiNode.DrawLinks(guiNodes);
    }

    ed::Suspend();

    if (ed::ShowNodeContextMenu(&rightClickedNode)) {
        ImGui::OpenPopup("Node Actions");
    }

    if (ImGui::BeginPopup("Node Actions")) {
        if (ImGui::MenuItem("Copy default value")) {
            for (const AINBImGuiNode &guiNode : guiNodes) {
                for (const AINBImGuiNode::NonNodeInput &input : guiNode.GetNonNodeInputs()) {
                    if (input.genNodeID == rightClickedNode) {
                        ImGui::SetClipboardText(AINB::AINBValueToString(input.inputParam.defaultValue).c_str());
                        goto found;
                    }
                }
            }
            found:;
        }
        ImGui::EndPopup();
    }

    if (selectedNodeIdx != -1) {
        for (AINBImGuiNode &guiNode : guiNodes) {
            if (guiNode.GetNode().Idx() == selectedNodeIdx) {
                ed::SelectNode(guiNode.GetNodeID());
                ed::NavigateToSelection(false);
                break;
            }
        }
    } else {
        ed::NodeId selectedNodeID;
        if (ed::GetSelectedNodes(&selectedNodeID, 1) > 0) {
            for (AINBImGuiNode &guiNode : guiNodes) {
                if (guiNode.GetNodeID() == selectedNodeID) {
                    selectedNodeIdx = guiNode.GetNode().Idx();
                    break;
                }
            }
        }
    }

    ed::Resume();

    if (wantAutoLayout) {
        AutoLayout();
    }

    if (wantSavePositions) {
        std::vector<AINBImGuiNode::AuxInfo> auxInfos;
        for (AINBImGuiNode &guiNode : guiNodes) {
            auxInfos.push_back(guiNode.GetAuxInfo());
        }
        SavePositionToFile(auxInfos);
    }

    ed::End();
    ed::SetCurrentEditor(nullptr);

    if (wantLoadPositions) {
        LoadPositionFromFile();
    }
}

void AINBEditor::AutoLayout() {
    // Use a very simple layouting strategy for now
    std::unordered_map<int, std::pair<int, int>> idxToPos;
    std::unordered_map<int, std::unordered_map<int, int>> posToIdx;

    std::function<void(int, std::pair<int, int>)> placeNode;

    placeNode = [&](int nodeIdx, std::pair<int, int> pos) {
        if (idxToPos.contains(nodeIdx)) {
            return;
        }
        while (posToIdx[pos.first].contains(pos.second)) {
            pos.second++;
        }
        idxToPos[nodeIdx] = pos;
        posToIdx[pos.first][pos.second] = nodeIdx;

        const std::vector<const AINB::Node *> &inNodes = ainb->nodes[nodeIdx].GetInNodes();
        const std::vector<const AINB::Node *> &outNodes = ainb->nodes[nodeIdx].GetOutNodes();
        for (const AINB::Node *node : outNodes) {
            placeNode(node->Idx(), {pos.first + 1, pos.second});
        }
        for (const AINB::Node *node : inNodes) {
            placeNode(node->Idx(), {pos.first - 1, pos.second});
        }
    };

    // First, try to place command root nodes
    for (size_t i = 0; i < ainb->commands.size(); i++) {
        placeNode(ainb->commands[i].rootNode->Idx(), {i, 0});
    }

    // Place remaining orphan nodes
    for (const AINB::Node &node : ainb->nodes) {
        if (!idxToPos.contains(node.Idx())) {
            placeNode(node.Idx(), {0, 0});
        }
    }

    newAuxInfos.clear();
    for (auto &[nodeIdx, coord] : idxToPos) {
        AINBImGuiNode::AuxInfo auxInfo;
        auxInfo.nodeIdx = nodeIdx;
        auxInfo.pos = ImVec2(coord.first * 600, coord.second * 400);

        int extraPinIdx = 0;
        for (AINB::Param &param : ainb->nodes[nodeIdx].GetParams()) {
            if (param.paramType == AINB::ParamType::Input) {
                const AINB::InputParam &inputParam = static_cast<const AINB::InputParam &>(param);
                if (inputParam.inputNodeIdxs.size() == 0) {
                    auxInfo.extraNodePos[param.name] = ImVec2(auxInfo.pos.x - 250, auxInfo.pos.y + extraPinIdx * 70);
                    extraPinIdx++;
                }
            }
        }
        newAuxInfos[nodeIdx] = auxInfo;
    }
}

void AINBEditor::SavePositionToFile(const std::vector<AINBImGuiNode::AuxInfo> &auxInfos) const {
    const char *fileTypes[] = { "*.txt" };
    std::string path = tinyfd_saveFileDialog("Save file", "", 1, fileTypes, "Text documents (*.txt)");
    if (path != "") {
        std::ofstream file(path);
        if (!file.is_open()) {
            std::cout << "Error opening file for writing: " << path << std::endl;
        } else {
            for (const AINBImGuiNode::AuxInfo &auxInfo : auxInfos) {
                file << "n" << auxInfo.nodeIdx << " " << auxInfo.pos.x << " " << auxInfo.pos.y << '\n';
                for (const auto &[name, pos] : auxInfo.extraNodePos) {
                    file << "e" << name << " " << pos.x << " " << pos.y << '\n';
                }
            }
            file.close();
        }
    }
}

void AINBEditor::LoadPositionFromFile() {
    const char *fileTypes[] = { "*.txt" };
    std::string path = tinyfd_openFileDialog("Save file", "", 1, fileTypes, "Text documents (*.txt)", 0);
    if (path != "") {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cout << "Error opening file for reading: " << path << std::endl;
        } else {
            newAuxInfos.clear();
            while (!file.eof()) {
                char type;
                file >> type;
                assert(type == 'n');
                AINBImGuiNode::AuxInfo auxInfo;
                file >> auxInfo.nodeIdx >> auxInfo.pos.x >> auxInfo.pos.y;
                char c;
                while (c = file.peek(), c == 'e' || c == '\n') {
                    if (c == '\n') {
                        file.get();
                        continue;
                    }
                    std::string name;
                    float x, y;
                    file >> type >> name >> x >> y;
                    auxInfo.extraNodePos[name] = ImVec2(x, y);
                }
                while (c = file.peek(), c == '\n') {
                    file.get();
                }
                newAuxInfos[auxInfo.nodeIdx] = auxInfo;
            }
        }
    }
}
