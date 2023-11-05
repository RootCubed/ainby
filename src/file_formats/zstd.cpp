#include "zstd.hpp"

#include <zstd.h>

void ZSTD::Read(std::istream &szFile) {
    // This could maybe use streaming decompression in the future...
    // probably not worth it though, the SZ files are small enough
    szFile.seekg(0, std::ios::end);
    size_t szCompressedSize = szFile.tellg();
    szFile.seekg(0, std::ios::beg);
    std::vector<u8> buffer(szCompressedSize);
    szFile.read((char *) buffer.data(), szCompressedSize);

    size = ZSTD_getFrameContentSize(buffer.data(), szCompressedSize);
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("Could not get decompressed size of SZ file");
    }
    data.resize(size);

    size_t res = ZSTD_decompress(data.data(), size, buffer.data(), szCompressedSize);
    if (ZSTD_isError(res)) {
        throw std::runtime_error("Could not decompress SZ file: " + std::string(ZSTD_getErrorName(res)));
    }
}

const u8 *ZSTD::GetData(size_t &size) const {
    size = this->size;
    return data.data();
}

void ZSTD::Write(std::ostream &szFile, const u8 *data, size_t size, int compressionLevel) {
    size_t szCompressedSize = ZSTD_compressBound(size);
    std::vector<u8> buffer(szCompressedSize);
    size_t res = ZSTD_compress(buffer.data(), szCompressedSize, data, size, compressionLevel);
    if (ZSTD_isError(res)) {
        throw std::runtime_error("Could not compress SZ file: " + std::string(ZSTD_getErrorName(res)));
    }
    szFile.write((char *) buffer.data(), res);
}
