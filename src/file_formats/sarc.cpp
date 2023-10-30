#include "sarc.hpp"

#include <cassert>
#include <cstring>
#include <sstream>

#define ALIGN4(x) (((x) + 3) & ~3)
#define ALIGN8(x) (((x) + 7) & ~7)

void SARC::Clear() {
    files.clear();
}

void SARC::Read(std::istream &sarcFile) {
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

    size_t stringStart = sarcFile.tellg();

    for (const SFATNode &node : sfatNodes) {
        std::string filePath;
        if ((node.fileAttributes >> 24) == 0) {
            throw std::runtime_error("File name by hash not supported");
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

void SARC::Write(std::ostream &sarcFile) const {
    u32 sfntSize = 0;
    for (const auto &[path, _] : files) {
        sfntSize += ALIGN4(path.length() + 1);
    }

    u32 dataBegin = ALIGN8(
        sizeof(SARCHeader)
        + sizeof(SFATHeader)
        + files.size() * sizeof(SFATNode)
        + sizeof(SFNTHeader)
        + sfntSize);
    u32 dataPos = 0;

    std::vector<SFATNode> sfatNodes;
    u32 strOffs = 0;
    for (const auto &[path, file] : files) {
        dataPos = ALIGN8(dataPos);
        sfatNodes.push_back(SFATNode {
            .fileNameHash = PathHash(path),
            .fileAttributes = 0x01000000 | (strOffs / 4),
            .nodeFileDataBegin = dataPos,
            .nodeFileDataEnd = dataPos + file.size
        });
        strOffs += ALIGN4(path.length() + 1);
        dataPos += file.size;
    }
    dataPos = ALIGN4(dataPos);

    SARCHeader sarcHeader = {
        .magic = {'S','A','R','C'},
        .headerLen = 0x14,
        .bom = 0xFEFF,
        .fileSize = dataBegin + dataPos,
        .dataBegin = dataBegin,
        .versionNum = 0x0100
    };
    SFATHeader sfatHeader = {
        .magic = {'S','F','A','T'},
        .headerLen = sizeof(SFATHeader),
        .nodeCount = (u16) files.size(),
        .hashKey = 0x65
    };
    SFNTHeader sfntHeader = {
        .magic = {'S','F','N','T'},
        .headerLen = sizeof(SFNTHeader)
    };

    sarcFile.write((char *) &sarcHeader, sizeof(SARCHeader));
    sarcFile.write((char *) &sfatHeader, sizeof(SFATHeader));
    sarcFile.write((char *) sfatNodes.data(), sfatNodes.size() * sizeof(SFATNode));
    sarcFile.write((char *) &sfntHeader, sizeof(SFNTHeader));
    for (const auto &[path, _] : files) {
        sarcFile.seekp(ALIGN4((u32) sarcFile.tellp()));
        sarcFile.write(path.c_str(), path.length() + 1);
    }
    for (const auto &[path, _] : files) {
        const SFATFile &file = files.at(path);
        sarcFile.seekp(ALIGN8((u32) sarcFile.tellp()));
        sarcFile.write((const char *) file.data.get(), file.size);
    }
    sarcFile.seekp(ALIGN4((u32) sarcFile.tellp()));

    assert(sarcFile.tellp() == dataBegin + dataPos);
}

const u8 *SARC::GetFileByPath(const std::string &path, u32 &size) const {
    if (files.find(path) == files.end()) {
        size = -1;
        return nullptr;
    }
    const SFATFile &file = files.at(path);
    size = file.size;
    return file.data.get();
}

const std::vector<std::string> SARC::GetFileList() const {
    std::vector<std::string> fileList;
    for (const auto &pair : files) {
        fileList.push_back(pair.first);
    }
    return fileList;
}

void SARC::SetFile(const std::string &path, const u8 *data, u32 size) {
    files[path] = SFATFile {
        size,
        std::make_unique<u8[]>(size)
    };
    memcpy(files[path].data.get(), data, size);
}

void SARC::RemoveFile(const std::string &path) {
    files.erase(path);
}

u32 SARC::PathHash(const std::string &path) {
    u32 hash = 0;
    for (size_t i = 0; i < path.length(); i++) {
        hash = hash * 0x65 + path[i];
    }
    return hash;
}
