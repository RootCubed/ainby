#include "ainby.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <strstream>

#include <imgui_internal.h> // Internal header needed for DockSpaceXXX functions
#include <tinyfiledialogs.h>

#include "file_formats/zstd.hpp"

void AINBY::Draw() {
    // Main Window -- Menu bar + Error popup
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus // Disable Ctrl-Tabbing onto this window
        | ImGuiWindowFlags_MenuBar;
    ImGui::Begin("Main Window", nullptr, windowFlags);

    if (shouldOpenErrorPopup) {
        ImGui::OpenPopup("Error##OpenFileError");
        shouldOpenErrorPopup = false;
    }

    DrawMainWindow();

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Begin("File Browser");
    DrawFileBrowser();
    ImGui::End();

    ImGui::Begin("AINB Inspector");
        if (ainbLoaded) {
            editor.DrawInspector();
        } else {
            ImGui::Text("No .ainb file loaded.");
        }
    ImGui::End();

    ImGui::Begin("Node Viewer");
        if (ainbLoaded) {
            editor.DrawNodeEditor();
        }
    ImGui::End();
}

void AINBY::DrawMainWindow() {
    // TODO: Make this a simple "Open" item instead and detect the file type automatically
    int openFileType = -1;
    bool savePack = false;
    bool saveSZ = false;
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open .zs")) {
                openFileType = 2;
            }
            if (ImGui::MenuItem("Open .pack")) {
                openFileType = 0;
            }
            if (ImGui::MenuItem("Open .ainb")) {
                openFileType = 1;
            }
            if (ImGui::MenuItem("Save .pack")) {
                savePack = true;
            }
            if (ImGui::MenuItem("Save .zs")) {
                saveSZ = true;
            }
            if (ImGui::MenuItem("Exit")) {
                shouldClose = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (openFileType != -1) {
        const char *path = tinyfd_openFileDialog("Open file", "", 0, nullptr, nullptr, 0);
        if (path != nullptr) {
            try {
                std::ifstream file(path, std::ios::binary);
                if (openFileType == 0) {
                    currentSarc.Read(file);
                    sarcLoaded = true;
                } else if (openFileType == 1) {
                    currentAinb.Read(file);
                    editor.RegisterAINB(currentAinb);
                    ainbLoaded = true;
                } else {
                    ZSTD zstdFile;
                    zstdFile.Read(file);

                    size_t decompressedSize;
                    const u8 *decompressed = zstdFile.GetData(decompressedSize);

                    std::istrstream stream((const char *) decompressed, decompressedSize);
                    currentSarc.Read(stream);
                    sarcLoaded = true;
                }
            } catch (std::exception &e) {
                fileOpenErrorMessage = e.what();
                shouldOpenErrorPopup = true;
            }
        }
    }

    if (savePack) {
        const char *path = tinyfd_saveFileDialog("Save file", "", 0, nullptr, nullptr);
        if (path != nullptr) {
            try {
                std::ofstream file(path, std::ios::binary);
                currentSarc.Write(file);
            } catch (std::exception &e) {
                fileOpenErrorMessage = e.what();
                shouldOpenErrorPopup = true;
            }
        }
    }

    if (saveSZ) {
        const char *path = tinyfd_saveFileDialog("Save file", "", 0, nullptr, nullptr);
        if (path != nullptr) {
            try {
                std::ostrstream stream;
                currentSarc.Write(stream);

                std::ofstream file(path, std::ios::binary);
                ZSTD::Write(file, (const u8 *) stream.str(), stream.pcount());

                stream.freeze(false);
            } catch (std::exception &e) {
                fileOpenErrorMessage = e.what();
                shouldOpenErrorPopup = true;
            }
        }
    }

    // Create the docking layout
    ImGuiID dockSpace = ImGui::DockSpace(ImGui::GetID("DockSpace"));
    if (firstFrame) {
        ImGui::DockBuilderRemoveNode(dockSpace);
        ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockSpace, ImGui::GetMainViewport()->Size);

        ImGuiID dockLeft, dockMiddle, dockRight;
        ImGui::DockBuilderSplitNode(dockSpace, ImGuiDir_Left, 0.2f, &dockLeft, &dockMiddle);
        ImGui::DockBuilderSplitNode(dockMiddle, ImGuiDir_Right, 0.3f, &dockRight, &dockMiddle);

        ImGui::DockBuilderDockWindow("File Browser", dockLeft);
        ImGui::DockBuilderDockWindow("Node Viewer", dockMiddle);
        ImGui::DockBuilderDockWindow("AINB Inspector", dockRight);

        ImGui::DockBuilderFinish(dockSpace);
        firstFrame = false;
    }

    // Error popup
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Error##OpenFileError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Could not open file!\nError: %s", fileOpenErrorMessage.c_str());
        if (ImGui::Button("OK")) {
            fileOpenErrorMessage = "";
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void AINBY::DrawFileBrowser() {
    if (!sarcLoaded) {
        ImGui::Text("No file loaded");
        return;
    }
    if (ImGui::TreeNodeEx("Files", ImGuiTreeNodeFlags_DefaultOpen)) {
        std::string selectedFile = DrawFileTree(currentSarc.GetFileList());

        if (selectedFile != "") {
            u32 fileSize;
            const u8 *buffer = currentSarc.GetFileByPath(selectedFile, fileSize);

            std::istrstream stream((const char *) buffer, fileSize);
            try {
                currentAinb.Read(stream);
                editor.RegisterAINB(currentAinb);
                ainbLoaded = true;
            } catch (std::exception &e) {
                fileOpenErrorMessage = e.what();
                shouldOpenErrorPopup = true;
                ainbLoaded = false;
                editor.UnloadAINB();
            }
        }

        ImGui::TreePop();
    }
}

std::string AINBY::DrawFileTree(const std::vector<std::string> &fileList) {
    // Sort file list so that the drawing algorithm works correctly
    // (and also so that the files are in alphabetical order lol)
    std::vector<std::string> sortedFileList = fileList;
    std::sort(sortedFileList.begin(), sortedFileList.end());

    // Keep track of opened folders
    std::vector<std::string> currPath;
    std::vector<bool> isOpened;
    isOpened.push_back(true);

    std::string selectedFile = "";
    for (const std::string &filePath : sortedFileList) {
        // Split path into its components
        std::vector<std::string> path;
        std::istringstream iss(filePath);
        std::string token;
        while (std::getline(iss, token, '/')) {
            path.push_back(token);
        }

        for (size_t i = 0; i < path.size(); i++) {
            // Navigate to the correct folder
            if (currPath.size() > i) {
                if (currPath[i] == path[i]) {
                    continue;
                }
                while (currPath.size() > i) {
                    currPath.pop_back();
                    if (isOpened.back()) {
                        ImGui::TreePop();
                    }
                    isOpened.pop_back();
                }
            }
            currPath.push_back(path[i]);

            // Draw the folder node
            if (isOpened.back()) {
                ImGuiTreeNodeFlags nodeFlags = (i == path.size() - 1) ? ImGuiTreeNodeFlags_Leaf : ImGuiTreeNodeFlags_None;
                bool openRes = ImGui::TreeNodeEx(path[i].c_str(), nodeFlags);
                if (i == path.size() - 1 && ImGui::IsItemClicked()) {
                    selectedFile = filePath;
                }
                isOpened.push_back(openRes);
            } else {
                // If parent folder is closed, the child folder must be closed too
                isOpened.push_back(false);
            }
        }
    }
    // Finish popping off the tree node stack
    while (currPath.size() > 0) {
        currPath.pop_back();
        if (isOpened.back()) {
            ImGui::TreePop();
        }
        isOpened.pop_back();
    }

    return selectedFile;
}
