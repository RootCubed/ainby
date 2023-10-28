#pragma once

#include <istream>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "types.h"

class SARCFile {
public:
    void Read(std::istream &sarcFile);
    void Clear();

    const u8 *GetFileByPath(const std::string &path, u32 &size) const;
    const std::vector<std::string> GetFileList() const;

private:
    struct SARCHeader {
        char magic[4];
        u16 headerLen;
        u16 bom;
        u32 fileSize;
        u32 dataBegin;
        u16 versionNum;
        u16 _pad;
    };
    struct SFATHeader {
        char magic[4];
        u16 headerLen;
        u16 nodeCount;
        u32 hashKey;
    };
    struct SFATNode {
        u32 fileNameHash;
        u32 fileAttributes;
        u32 nodeFileDataBegin;
        u32 nodeFileDataEnd;
    };
    struct SFNTHeader {
        char magic[4];
        u16 headerLen;
        u16 _pad;
    };

    static u32 PathHash(std::string &path);

    struct SFATFile {
        u32 size;
        std::unique_ptr<u8[]> data;
    };
    std::unordered_map<std::string, SFATFile> files;
};
