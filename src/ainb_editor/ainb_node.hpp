#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>

#include "file_formats/ainb.hpp"
#include "node_editor/imgui_node_editor.h"

namespace ed = ax::NodeEditor;

class AINBImGuiNode {
public:
    struct NonNodeInput {
        ed::NodeId genNodeID;
        ed::PinId genNodePinID;
        ed::PinId outputPinID;
        ed::LinkId linkID;
        AINB::InputParam &inputParam;
    };
    struct FlowLink {
        ed::LinkId linkID;
        ed::PinId flowFromPinID;
        AINB::NodeLink nodeLink;
    };
    struct ParamLink {
        ed::LinkId linkID;
        int inputNodeIdx;
        int inputParameterIdx;
        ed::PinId inputPinID;
    };
    // Information for node drawing
    struct AuxInfo {
        u32 nodeIdx;
        ImVec2 pos;
        std::unordered_map<std::string, ImVec2> extraNodePos;
    };

    AINBImGuiNode(AINB::Node &node);

    void DrawLinks(std::vector<AINBImGuiNode> &nodes);
    void Draw();

    ed::NodeId GetNodeID() const { return nodeID; }
    const AINB::Node &GetNode() const { return node; }
    const std::vector<NonNodeInput> &GetNonNodeInputs() const { return nonNodeInputs; }

    AuxInfo GetAuxInfo() const;
    void LoadAuxInfo(const AuxInfo &auxInfo);

private:
    AINB::Node &node;

    int frameWidth;
    ImVec2 HeaderMin;
    ImVec2 HeaderMax;

    ed::NodeId nodeID;
    ed::PinId flowPinID;
    std::vector<int> inputPins;
    std::vector<int> outputPins;
    std::vector<ed::PinId> extraPins;
    std::vector<NonNodeInput> nonNodeInputs;
    std::vector<FlowLink> flowLinks;
    std::vector<ParamLink> paramLinks;

    std::unordered_map<std::string, ed::PinId> nameToPinID;
    std::unordered_map<int, ed::PinId> idxToID;

    ImVec2 iconSize = ImVec2(10, 10);
    const int minImmTextboxWidth = 150;

    static u32 nextID;
    ed::NodeId static MakeNodeID() { return ++nextID; }
    ed::PinId static MakePinID() { return ++nextID; }
    ed::LinkId static MakeLinkID() { return ++nextID; }

    void DrawPinIcon(ed::PinId id, bool isOutput);
    void DrawPinTextCommon(const AINB::Param &param);
    void DrawInputPin(AINB::Param &param, ed::PinId id);
    void DrawOutputPin(const AINB::Param &param, ed::PinId id);
    void DrawExtraPins();

    void PreparePinIDs();
    void CalculateFrameWidth();
    static ImColor GetNodeHeaderColor(AINB::NodeType type);
    void PrepareTextAlignRight(std::string str, int extraMargin = 0);
};
