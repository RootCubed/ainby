#include "ainb_node.hpp"

#include <sstream>

#include "node_editor/imgui_node_editor.h"
#include "pin_icons.hpp"

u32 AINBImGuiNode::nextID = 0;

AINBImGuiNode::AINBImGuiNode(AINB::Node &node) : node(node) {
    PreparePinIDs();
    CalculateFrameWidth();
}

void AINBImGuiNode::PreparePinIDs() {
    nodeID = MakeNodeID();
    flowPinID = MakePinID();

    const auto params = node.GetParams();

    for (size_t i = 0; i < params.size(); i++) {
        AINB::Param &param = params[i];
        ed::PinId pinID = MakePinID();
        idxToID[i] = pinID;
        nameToPinID[param.name] = pinID;
        if (param.paramType == AINB::ParamType::Output) {
            outputPins.push_back(i);
        } else {
            inputPins.push_back(i);
        }

        if (param.paramType == AINB::ParamType::Input) {
            AINB::InputParam &inputParam = static_cast<AINB::InputParam &>(param);
            if (inputParam.inputNodeIdxs.size() == 0) {
                nonNodeInputs.push_back(NonNodeInput {
                    .genNodeID = MakeNodeID(),
                    .genNodePinID = MakePinID(),
                    .outputPinID = pinID,
                    .linkID = MakeLinkID(),
                    .inputParam = inputParam
                });
            } else {
                for (size_t j = 0; j < inputParam.inputNodeIdxs.size(); j++) {
                    paramLinks.push_back(ParamLink {
                        .linkID = MakeLinkID(),
                        .inputType = inputParam.dataType,
                        .inputNodeIdx = inputParam.inputNodeIdxs[j],
                        .inputParameterIdx = inputParam.inputParamIdxs[j],
                        .inputPinID = pinID
                    });
                }
            }
        }
    }

    // Make index offsets so that the parameter indices work correctly
    // outputIdxOffset[t] is the index of the first output parameter of type t
    int offset = 0;
    for (int i = 0; i < AINB::ValueTypeCount; i++) {
        outputIdxOffset[i] = offset;
        offset += node.outputParams[i].size();
    }


    // Extra pins / Flow links
    for (const AINB::NodeLink &nl : node.nodeLinks) {
        if (nl.type != AINB::LinkType::Flow) {
            if (node.type == AINB::UserDefined || nl.type != AINB::LinkType::ForkJoin) {
                continue;
            }
        }
        ed::PinId pinID = MakePinID();
        extraPins.push_back(pinID);
        switch (node.type) {
            case AINB::Element_Simultaneous:
            case AINB::Element_Fork:
                flowLinks.push_back(FlowLink {MakeLinkID(), extraPins[0], nl});
                break;
            case AINB::Element_BoolSelector: {
                flowLinks.push_back(FlowLink {MakeLinkID(), pinID, nl});
                break;
            }
            default:
                flowLinks.push_back(FlowLink {MakeLinkID(), pinID, nl});
                break;
        }
    }
    if (node.type == AINB::Element_Simultaneous) {
        extraPins.resize(1);
    }
}

void AINBImGuiNode::CalculateFrameWidth() {
    int itemSpacingX = ImGui::GetStyle().ItemSpacing.x;
    frameWidth = 8 * 2 + ImGui::CalcTextSize(node.TypeName().c_str()).x + iconSize.x + itemSpacingX;
    for (size_t i = 0; i < inputPins.size() || i < outputPins.size(); i++) {
        int size = 8 * 2; // Frame Padding
        if (i < inputPins.size()) {
            AINB::Param &param = node.GetParams()[inputPins[i]];
            size += ImGui::CalcTextSize(param.name.c_str()).x;
            size += itemSpacingX + iconSize.x;
            if (param.paramType == AINB::ParamType::Immediate) {
                AINB::InputParam &ip = static_cast<AINB::InputParam &>(param);
                size += itemSpacingX + ImGui::GetStyle().FramePadding.x * 2;
                switch (ip.dataType) {
                    case AINB::ValueType::Int:
                    case AINB::ValueType::String:
                    case AINB::ValueType::Float:
                        size += minImmTextboxWidth;
                        break;
                    case AINB::ValueType::Bool:
                        size += ImGui::GetFrameHeight();
                        break;
                    case AINB::ValueType::Vec3f:
                        size += minImmTextboxWidth * 3 + itemSpacingX * 2;
                        break;
                    default:
                        break;
                }
            } else {
                size += itemSpacingX;
            }
        }
        if (i < outputPins.size()) {
            AINB::Param &param = node.GetParams()[outputPins[i]];
            size += ImGui::CalcTextSize(param.name.c_str()).x;
            size += itemSpacingX * 2 + iconSize.x;
        }
        frameWidth = std::max(frameWidth, size);
    }
}

ImColor AINBImGuiNode::GetNodeHeaderColor(AINB::NodeType type) {
    switch (type) {
        case AINB::Element_S32Selector:
        case AINB::Element_F32Selector:
        case AINB::Element_StringSelector:
        case AINB::Element_RandomSelector:
        case AINB::Element_BoolSelector:
            return ImColor(60, 0, 60);
        case AINB::Element_Sequential:
            return ImColor(60, 0, 0);
        case AINB::Element_Simultaneous:
            return ImColor(0, 60, 0);
        case AINB::Element_ModuleIF_Input_S32:
        case AINB::Element_ModuleIF_Input_F32:
        case AINB::Element_ModuleIF_Input_Vec3f:
        case AINB::Element_ModuleIF_Input_String:
        case AINB::Element_ModuleIF_Input_Bool:
        case AINB::Element_ModuleIF_Input_Ptr:
            return ImColor(0, 60, 60);
        case AINB::Element_ModuleIF_Output_S32:
        case AINB::Element_ModuleIF_Output_F32:
        case AINB::Element_ModuleIF_Output_Vec3f:
        case AINB::Element_ModuleIF_Output_String:
        case AINB::Element_ModuleIF_Output_Bool:
        case AINB::Element_ModuleIF_Output_Ptr:
            return ImColor(0, 0, 60);
        case AINB::Element_ModuleIF_Child:
            return ImColor(0, 0, 0);
        default: return ImColor(60, 60, 0);
    }
}

void AINBImGuiNode::PrepareTextAlignRight(std::string str, int extraMargin) {
    int cursorPosX = HeaderMax.x;
    cursorPosX -= 8 + ImGui::CalcTextSize(str.c_str()).x + extraMargin;
    ImGui::SetCursorPosX(cursorPosX);
}

void AINBImGuiNode::DrawPinIcon(ed::PinId id, bool isOutput) {
    ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0.5f, 1.0f));
    ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));
    ed::BeginPin(id, isOutput ? ed::PinKind::Output : ed::PinKind::Input);
    PinIcons::DrawIcon(iconSize);
    ed::EndPin();
    ed::PopStyleVar(2);
}

void AINBImGuiNode::DrawPinTextCommon(const AINB::Param &param) {
    ImGui::TextUnformatted(param.name.c_str());
}

void AINBImGuiNode::DrawInputPin(AINB::Param &param, ed::PinId id) {
    DrawPinIcon(id, false);
    ImGui::SameLine();
    DrawPinTextCommon(param);
    if (param.paramType == AINB::ParamType::Immediate) {
        AINB::ImmediateParam &immParam = static_cast<AINB::ImmediateParam &>(param);
        ImGui::SameLine();
        switch (immParam.dataType) {
            case AINB::ValueType::Int: {
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputScalar(("##" + param.name).c_str(), ImGuiDataType_U32, &std::get<u32>(immParam.value));
                ImGui::PopItemWidth();
                break;
            }
            case AINB::ValueType::Float: {
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputScalar(("##" + param.name).c_str(), ImGuiDataType_Float, &std::get<float>(immParam.value));
                ImGui::PopItemWidth();
                break;
            }
            case AINB::ValueType::Bool:
                ImGui::Checkbox(("##" + param.name).c_str(), &std::get<bool>(immParam.value));
                break;
            case AINB::ValueType::String: {
                static char strBuf[256];
                strncpy(strBuf, std::get<std::string>(immParam.value).c_str(), 256);
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputText(("##" + param.name).c_str(), strBuf, 256);
                ImGui::PopItemWidth();
                break;
            }
            default:
                ImGui::Text("%s", AINB::AINBValueToString(immParam.value).c_str());
                break;
        }
    }
}

void AINBImGuiNode::DrawOutputPin(const AINB::Param &param, ed::PinId id) {
    PrepareTextAlignRight(param.name, iconSize.x + ImGui::GetStyle().ItemSpacing.x);
    DrawPinTextCommon(param);
    ImGui::SameLine();
    DrawPinIcon(id, true);
}

std::string MakeTitle(const AINB::Node &node, const AINB::NodeLink &nl, int idx, int count) {
    switch (node.type) {
        case AINB::UserDefined:
        case AINB::Element_BoolSelector:
        case AINB::Element_SplitTiming:
            return nl.name;
        case AINB::Element_Simultaneous:
            return "Control";
        case AINB::Element_Sequential:
            return "Seq " + std::to_string(idx);
        case AINB::Element_S32Selector:
        case AINB::Element_F32Selector:
            if (idx == count - 1) {
                return "Default";
            }
            return "=" + AINB::AINBValueToString(nl.value);
        case AINB::Element_Fork:
            return "Fork";
        default:
            return "<name unavailable>";
    }
}

void AINBImGuiNode::DrawExtraPins() {
    for (size_t i = 0; i < flowLinks.size(); i++) {
        const FlowLink &flowLink = flowLinks[i];
        std::string title = MakeTitle(node, flowLink.nodeLink, i, flowLinks.size());
        PrepareTextAlignRight(title, iconSize.x + ImGui::GetStyle().ItemSpacing.x);
        ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine();
        DrawPinIcon(extraPins[i], true);
        if (node.type == AINB::Element_Simultaneous || node.type == AINB::Element_Fork) {
            break; // Simultaneous and Fork only have one pin
        }
    }
}

void AINBImGuiNode::Draw() {
    ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 8, 8, 8));
    ed::BeginNode(nodeID);
        ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0.5f, 1.0f));
        ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));
        ed::BeginPin(flowPinID, ed::PinKind::Input);
        PinIcons::DrawIcon(iconSize);
        ed::EndPin();
        ed::PopStyleVar(2);

        ImGui::SameLine();
        ImGui::Text("%s", node.TypeName().c_str());

        HeaderMin = ImGui::GetItemRectMin() - ImVec2(iconSize.x + ImGui::GetStyle().ItemSpacing.x + 8, 8);
        HeaderMax = ImVec2(HeaderMin.x + frameWidth, ImGui::GetItemRectMax().y + 8);

        ImGui::Dummy(ImVec2(0, 8));

        // Main content frame
        for (size_t i = 0; i < inputPins.size() || i < outputPins.size(); i++) {
            if (i < inputPins.size()) {
                DrawInputPin(node.GetParams()[inputPins[i]], idxToID[inputPins[i]]);
            } else {
                ImGui::Dummy(ImVec2(0, 0));
            }
            ImGui::SameLine();
            if (i < outputPins.size()) {
                DrawOutputPin(node.GetParams()[outputPins[i]], idxToID[outputPins[i]]);
            } else {
                ImGui::Dummy(ImVec2(0, 0));
            }
        }
        DrawExtraPins();
    ed::EndNode();
    ed::PopStyleVar();

    if (ImGui::IsItemVisible()) {
        int alpha = ImGui::GetStyle().Alpha;
        ImColor headerColor = GetNodeHeaderColor(node.type);
        headerColor.Value.w = alpha;

        ImDrawList *drawList = ed::GetNodeBackgroundDrawList(nodeID);

        const auto borderWidth = ed::GetStyle().NodeBorderWidth;

        HeaderMin.x += borderWidth;
        HeaderMin.y += borderWidth;
        HeaderMax.x -= borderWidth;
        HeaderMax.y -= borderWidth;

        drawList->AddRectFilled(HeaderMin, HeaderMax, headerColor, ed::GetStyle().NodeRounding,
            ImDrawFlags_RoundCornersTopLeft | ImDrawFlags_RoundCornersTopRight);

        ImVec2 headerSeparatorLeft = ImVec2(HeaderMin.x - borderWidth / 2, HeaderMax.y - 0.5f);
        ImVec2 headerSeparatorRight = ImVec2(HeaderMax.x, HeaderMax.y - 0.5f);

        drawList->AddLine(headerSeparatorLeft, headerSeparatorRight, ImColor(255, 255, 255, (int) (alpha * 255 / 2)), borderWidth);
    }
}

void AINBImGuiNode::DrawLinks(std::vector<AINBImGuiNode> &nodes) {
    // Draw inputs not connected to a node
    for (const NonNodeInput &input : nonNodeInputs) {
        const AINB::InputParam &inputParam = input.inputParam;
        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(32, 117, 21, 192));
        ed::BeginNode(input.genNodeID);
            std::string titleStr = inputParam.name;
            std::string defaultValueStr = "(" + AINB::AINBValueToString(inputParam.defaultValue) + ")";

            ImGui::TextUnformatted(titleStr.c_str());
            int titleSizeX = ImGui::CalcTextSize(titleStr.c_str()).x;
            int defaultValueSizeX = ImGui::CalcTextSize(defaultValueStr.c_str()).x + ImGui::GetStyle().ItemSpacing.x + iconSize.x;
            if (defaultValueSizeX < titleSizeX) {
                int cursorPosX = ImGui::GetCursorPosX();
                ImGui::SetCursorPosX(cursorPosX + titleSizeX - defaultValueSizeX);
            }
            ImGui::TextUnformatted(defaultValueStr.c_str());
            ImGui::SameLine();
            ed::PushStyleVar(ed::StyleVar_PivotAlignment, ImVec2(0.5f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_PivotSize, ImVec2(0, 0));
            ed::BeginPin(input.genNodePinID, ed::PinKind::Output);
            PinIcons::DrawIcon(iconSize);
            ed::EndPin();
            ed::PopStyleVar(2);
        ed::EndNode();
        ed::PopStyleColor();

        ed::Link(input.linkID, input.genNodePinID, input.outputPinID, ImColor(255, 255, 255));
    }

    // Draw flow links
    for (const FlowLink &flowLink : flowLinks) {
        ed::Link(flowLink.linkID, flowLink.flowFromPinID, nodes[flowLink.nodeLink.idx].flowPinID, ImColor(255, 255, 255));
    }

    // Draw node inputs
    for (const ParamLink &paramLink : paramLinks) {
        if (paramLink.inputNodeIdx < 0) {
            // TODO: Multi-links
            continue;
        }

        const AINBImGuiNode &inputNode = nodes[paramLink.inputNodeIdx];
        ed::PinId outPin = inputNode.idxToID.at(inputNode.outputPins[inputNode.outputIdxOffset[static_cast<int>(paramLink.inputType)] + paramLink.inputParameterIdx]);
        ed::Link(paramLink.linkID, outPin, paramLink.inputPinID, ImColor(140, 140, 40));
    }
}

AINBImGuiNode::AuxInfo AINBImGuiNode::GetAuxInfo() const {
    AuxInfo auxInfo;
    auxInfo.nodeIdx = node.Idx();
    auxInfo.pos = ed::GetNodePosition(nodeID);
    for (const NonNodeInput &input : nonNodeInputs) {
        auxInfo.extraNodePos[input.inputParam.name] = ed::GetNodePosition(input.genNodeID);
    }
    return auxInfo;
}

void AINBImGuiNode::LoadAuxInfo(const AuxInfo &auxInfo) {
    ed::SetNodePosition(nodeID, auxInfo.pos);
    for (NonNodeInput &input : nonNodeInputs) {
        if (auxInfo.extraNodePos.contains(input.inputParam.name)) {
            ed::SetNodePosition(input.genNodeID, auxInfo.extraNodePos.at(input.inputParam.name));
        }
    }
}
