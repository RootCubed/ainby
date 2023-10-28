#include "sarc.hpp"

#include <cassert>
#include <cstring>
#include <sstream>

void SARCFile::Clear() {
    files.clear();
}

void SARCFile::Read(std::istream &sarcFile) {
    Clear();
    SARCHeader sarcHeader;
    sarcFile.read((char *) &sarcHeader, sizeof(SARCHeader));

    if (strncmp(sarcHeader.magic, "SARC", 4) != 0) {
        throw std::runtime_error("Invalid SARC magic");
    }
    assert(sarcHeader.headerLen == 0x14);
    assert(sarcHeader.bom == 0xFEFF);
    assert(sarcHeader.versionNum == 0x0100);

    u32 dataBegin = sarcHeader.dataBegin;

    SFATHeader sfatHeader;
    sarcFile.read((char *) &sfatHeader, sizeof(SFATHeader));

    if (strncmp(sfatHeader.magic, "SFAT", 4) != 0) {
        throw std::runtime_error("Invalid SFAT magic");
    }
    assert(sfatHeader.headerLen == 0xC);
    assert(sfatHeader.hashKey == 0x65);

    std::vector<SFATNode> sfatNodes;
    for (int i = 0; i < sfatHeader.nodeCount; i++) {
        SFATNode sfatNode;
        sarcFile.read((char *) &sfatNode, sizeof(SFATNode));
        sfatNodes.push_back(sfatNode);
    }

    SFNTHeader sfntHeader;
    sarcFile.read((char *) &sfntHeader, sizeof(SFNTHeader));

    if (strncmp(sfntHeader.magic, "SFNT", 4) != 0) {
        throw std::runtime_error("Invalid SFNT magic");
    }
    assert(sfntHeader.headerLen == 0x8);

    std::vector<std::string> filePaths;
    std::unordered_map<u32, int> filePathIndices;
    size_t stringStart = sarcFile.tellg();
    while (sarcFile.tellg() < sarcHeader.dataBegin) {
        std::string filePath;
        std::getline(sarcFile, filePath, '\0');
        if (filePath.length() == 0) {
            break;
        }
        filePathIndices[PathHash(filePath)] = filePaths.size();
        filePaths.push_back(filePath);

        // Align to 4 bytes
        u32 currPos = sarcFile.tellg();
        u32 nextPos = (currPos + 3) & ~3;
        sarcFile.seekg(nextPos);
    }

    for (const SFATNode &node : sfatNodes) {
        std::string filePath;
        if ((node.fileAttributes >> 24) == 0) {
            filePath = filePaths[filePathIndices[node.fileNameHash]];
        } else {
            u32 offset = (node.fileAttributes & 0xFFFFFF) * 4;
            sarcFile.seekg(stringStart + offset);
            std::getline(sarcFile, filePath, '\0');
        }

        u32 size = node.nodeFileDataEnd - node.nodeFileDataBegin;

        files[filePath] = SFATFile {
            size,
            std::make_unique<u8[]>(size)
        };

        sarcFile.seekg(node.nodeFileDataBegin + dataBegin);
        sarcFile.read((char *) files[filePath].data.get(), size);
    }
}

const u8 *SARCFile::GetFileByPath(const std::string &path, u32 &size) const {
    if (files.find(path) == files.end()) {
        size = -1;
        return nullptr;
    }
    const SFATFile &file = files.at(path);
    size = file.size;
    return file.data.get();
}

const std::vector<std::string> SARCFile::GetFileList() const {
    std::vector<std::string> fileList;
    for (const auto &pair : files) {
        fileList.push_back(pair.first);
    }
    return fileList;
}

u32 SARCFile::PathHash(std::string &path) {
    u32 hash = 0;
    for (int i = 0; i < path.length(); i++) {
        hash = hash * 0x65 + path[i];
    }
    return hash;
}
