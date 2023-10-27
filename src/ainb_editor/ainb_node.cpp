#include "ainb_node.hpp"

#include <sstream>

#include "node_editor/imgui_node_editor.h"
#include "pin_icons.hpp"

u32 AINBImGuiNode::nextID = 0;

AINBImGuiNode::AINBImGuiNode(const AINB::Node &node) : node(node) {
    PreparePinIDs();
    CalculateFrameWidth();
}

void AINBImGuiNode::PreparePinIDs() {
    nodeID = MakeNodeID();
    flowPinID = MakePinID();

    for (int i = 0; i < node.params.size(); i++) {
        AINB::Param *param = node.params[i];
        ed::PinId pinID = MakePinID();
        idxToID[i] = pinID;
        nameToPinID[param->name] = pinID;
        if (param->paramType == AINB::Param_Output) {
            outputPins.push_back(i);
        } else {
            inputPins.push_back(i);
        }

        if (param->paramType == AINB::Param_Input) {
            AINB::InputParam *inputParam = static_cast<AINB::InputParam *>(param);
            if (inputParam->inputNodeIdxs.size() == 0) {
                nonNodeInputs.push_back(NonNodeInput {
                    .genNodeID = MakeNodeID(),
                    .genNodePinID = MakePinID(),
                    .outputPinID = pinID,
                    .linkID = MakeLinkID(),
                    .inputParam = inputParam
                });
            } else {
                for (int j = 0; j < inputParam->inputNodeIdxs.size(); j++) {
                    paramLinks.push_back(ParamLink {
                        .linkID = MakeLinkID(),
                        .inputNodeIdx = inputParam->inputNodeIdxs[j],
                        .inputParameterIdx = inputParam->inputParamIdxs[j],
                        .inputPinID = pinID
                    });
                }
            }
        }
    }

    // Extra pins / Flow links
    for (const AINB::NodeLink &nl : node.nodeLinks) {
        if (nl.type != AINB::LinkFlow) {
            if (node.type == AINB::UserDefined || nl.type != AINB::LinkForkJoin) {
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
                int idx = (nl.name == "True") ? 0 : 1;
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
    for (int i = 0; i < inputPins.size() || i < outputPins.size(); i++) {
        int size = 8 * 2; // Frame Padding
        if (i < inputPins.size()) {
            size += ImGui::CalcTextSize(node.params[inputPins[i]]->name.c_str()).x;
            size += itemSpacingX + iconSize.x;
            if (node.params[inputPins[i]]->paramType == AINB::Param_Imm) {
                AINB::InputParam *ip = static_cast<AINB::InputParam *>(node.params[inputPins[i]]);
                size += itemSpacingX + ImGui::GetStyle().FramePadding.x * 2;
                switch (ip->dataType) {
                    case AINB::AINBInt:
                    case AINB::AINBString:
                    case AINB::AINBFloat:
                        size += minImmTextboxWidth;
                        break;
                    case AINB::AINBBool:
                        size += ImGui::GetFrameHeight();
                        break;
                }
            } else {
                size += itemSpacingX;
            }
        }
        if (i < outputPins.size()) {
            size += ImGui::CalcTextSize(node.params[outputPins[i]]->name.c_str()).x;
            size += itemSpacingX * 2 + iconSize.x;
        }
        frameWidth = std::max(frameWidth, size);
    }
}

ImColor AINBImGuiNode::GetNodeHeaderColor(AINB::nodeType_e type) {
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
    int currentCursorPosX = ImGui::GetCursorPosX();
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

void AINBImGuiNode::DrawPinTextCommon(AINB::Param *param, bool isOutput) {
    ImGui::TextUnformatted(param->name.c_str());
}

void AINBImGuiNode::DrawInputPin(AINB::Param *param, ed::PinId id) {
    DrawPinIcon(id, false);
    ImGui::SameLine();
    DrawPinTextCommon(param, false);
    if (param->paramType == AINB::Param_Imm) {
        AINB::ImmediateParam *immParam = static_cast<AINB::ImmediateParam *>(param);
        ImGui::SameLine();
        switch (immParam->dataType) {
            case AINB::AINBInt: {
                int currentCursorPosX = ImGui::GetCursorPosX();
                int boxEnd = HeaderMax.x - 8;
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputScalar(("##" + param->name).c_str(), ImGuiDataType_U32, &std::get<u32>(immParam->value));
                ImGui::PopItemWidth();
                break;
            }
            case AINB::AINBFloat: {
                int currentCursorPosX = ImGui::GetCursorPosX();
                int boxEnd = HeaderMax.x - 8;
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputScalar(("##" + param->name).c_str(), ImGuiDataType_Float, &std::get<float>(immParam->value));
                ImGui::PopItemWidth();
                break;
            }
            case AINB::AINBBool:
                ImGui::Checkbox(("##" + param->name).c_str(), &std::get<bool>(immParam->value));
                break;
            case AINB::AINBString: {
                static char strBuf[256];
                strcpy(strBuf, std::get<std::string>(immParam->value).c_str());
                ImGui::PushItemWidth(minImmTextboxWidth);
                ImGui::InputText(("##" + param->name).c_str(), strBuf, 256);
                ImGui::PopItemWidth();
                break;
            }
            default:
                ImGui::Text("%s", AINB::AINBValueToString(immParam->value).c_str());
                break;
        }
    }
}

void AINBImGuiNode::DrawOutputPin(AINB::Param *param, ed::PinId id) {
    PrepareTextAlignRight(param->name, iconSize.x + ImGui::GetStyle().ItemSpacing.x);
    DrawPinTextCommon(param, true);
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
    }
    return "<name unavailable>";
}

void AINBImGuiNode::DrawExtraPins() {
    for (int i = 0; i < flowLinks.size(); i++) {
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
        for (int i = 0; i < inputPins.size() || i < outputPins.size(); i++) {
            if (i < inputPins.size()) {
                DrawInputPin(node.params[inputPins[i]], idxToID[inputPins[i]]);
            } else {
                ImGui::Dummy(ImVec2(0, 0));
            }
            ImGui::SameLine();
            if (i < outputPins.size()) {
                DrawOutputPin(node.params[outputPins[i]], idxToID[outputPins[i]]);
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
        const AINB::InputParam *inputParam = input.inputParam;
        ed::PushStyleColor(ed::StyleColor_NodeBg, ImColor(32, 117, 21, 192));
        ed::BeginNode(input.genNodeID);
            std::string titleStr = inputParam->name;
            std::string defaultValueStr = "(" + AINB::AINBValueToString(inputParam->defaultValue) + ")";

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
        ed::PinId outPin = inputNode.idxToID.at(inputNode.outputPins[paramLink.inputParameterIdx]);
        ed::Link(paramLink.linkID, outPin, paramLink.inputPinID, ImColor(140, 140, 40));
    }
}

AINBImGuiNode::AuxInfo AINBImGuiNode::GetAuxInfo() const {
    AuxInfo auxInfo;
    auxInfo.nodeIdx = node.Idx();
    auxInfo.pos = ed::GetNodePosition(nodeID);
    for (const NonNodeInput &input : nonNodeInputs) {
        auxInfo.extraNodePos[input.inputParam->name] = ed::GetNodePosition(input.genNodeID);
    }
    return auxInfo;
}

void AINBImGuiNode::LoadAuxInfo(const AuxInfo &auxInfo) {
    ed::SetNodePosition(nodeID, auxInfo.pos);
    for (NonNodeInput &input : nonNodeInputs) {
        if (auxInfo.extraNodePos.contains(input.inputParam->name)) {
            ed::SetNodePosition(input.genNodeID, auxInfo.extraNodePos.at(input.inputParam->name));
        }
    }
}
