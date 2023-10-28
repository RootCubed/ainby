#pragma once

#include "ainb_editor/ainb_editor.hpp"
#include "file_formats/ainb.hpp"
#include "file_formats/sarc.hpp"

// Main editor class
class AINBY {
private:
    AINBEditor editor;

    SARC currentSarc;
    bool sarcLoaded = false;
    AINB::AINB currentAinb;
    bool ainbLoaded = false;

    bool shouldOpenErrorPopup = false;
    std::string fileOpenErrorMessage = "";

    bool firstFrame = true;

    void DrawMainWindow();
    void DrawFileBrowser();
    std::string DrawFileTree(const std::vector<std::string> &fileList);

public:
    void Draw();

    bool shouldClose = false;
};
