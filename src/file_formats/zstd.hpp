#pragma once

#include <iostream>
#include <vector>

#include "types.h"

class ZSTD {
public:
    void Read(std::istream &szFile);
    const u8 *GetData(size_t &size) const;

    static void Write(std::ostream &szFile, const u8 *data, size_t size, int compressionLevel = 19);

private:
    std::vector<u8> data;
    size_t size;
};
