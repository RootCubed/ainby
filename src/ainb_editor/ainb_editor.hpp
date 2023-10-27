#pragma once

#include <istream>

#include "ainb_node.hpp"
#include "file_formats/ainb.hpp"
#include "node_editor/imgui_node_editor.h"

namespace ed = ax::NodeEditor;

class AINBEditor {
private:
    AINB::AINB *ainb;

    // Node positioning information; add an entry to this map
    // to change the position of a node.
    std::unordered_map<int, AINBImGuiNode::AuxInfo> newAuxInfos;

    // Node Editor
    ed::Config edConfig;
    ed::EditorContext *edContext = nullptr;
    std::vector<AINBImGuiNode> guiNodes;
    ed::NodeId rightClickedNode = 0;
    int selectedNodeIdx = -1;
    std::string selectedCommand = "";

    void AutoLayout();

public:
    AINBEditor();
    ~AINBEditor();

    void RegisterAINB(AINB::AINB &ainb);
    void UnloadAINB();

    void SavePositionToFile(const std::vector<AINBImGuiNode::AuxInfo> &auxInfos) const;
    void LoadPositionFromFile();

    void DrawInspector();
    void DrawNodeEditor();
};
