#include "pin_icons.hpp"

void PinIconTriangle(ImDrawList *drawList, ImVec2 &pos, ImVec2 &size, ImColor color) {
    ImVec2 p1 = pos + ImVec2(0, 0);
    ImVec2 p2 = pos + ImVec2(size.x, size.y / 2);
    ImVec2 p3 = pos + ImVec2(0, size.y);

    drawList->AddTriangleFilled(p1, p2, p3, color);
}

void PinIcons::DrawIcon(ImVec2 &size) {
    if (ImGui::IsRectVisible(size)) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();

        int fontSize = ImGui::GetFontSize();

        ImVec2 trueDrawPos = cursorPos + ImVec2(0, (size.y > fontSize) ? 0 : (fontSize - size.y) / 2);

        PinIconTriangle(drawList, trueDrawPos, size, ImColor(255, 255, 255));
    }

    ImGui::Dummy(size);
}
