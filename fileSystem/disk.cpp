//
// Created by 追影子的熊 on 2024/10/30.
//

#include "disk.h"
#include <stdexcept>
#include <cstring>

void Disk::open(const char *path, size_t nblocks)
{
    // 打开文件
    FileHandle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (FileHandle == INVALID_HANDLE_VALUE)
    {
        throw std::runtime_error("Unable to open file: " + std::string(path));
    }
    // 计算文件大小
    LARGE_INTEGER size;
    size.QuadPart = nblocks * BLOCK_SIZE;
    // 设置文件大小
    if (!SetFilePointerEx(FileHandle, size, nullptr,
                          FILE_BEGIN) || !SetEndOfFile(FileHandle)) {
        CloseHandle(FileHandle);
        throw std::runtime_error("Unable to set file size: " + std::string(path));
    }

    Blocks = nblocks;
    Reads = 0;
    Writes = 0;
}



Disk::~Disk()
{
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(FileHandle);
        FileHandle = INVALID_HANDLE_VALUE;
    }
}

void Disk::sanity_check(int blockNum, const char *data) const {
    if (blockNum < 0) {
        throw std::invalid_argument("blockNum is negative!");
    }

    if (blockNum >= static_cast<int>(Blocks)) {
        throw std::invalid_argument("blockNum is too big!");
    }

    if (data == nullptr) {
        throw std::invalid_argument("null data pointer!");
    }
}

void Disk::read(int blockNum, char *data) {
    sanity_check(blockNum, data); // 安全检查

    LARGE_INTEGER offset; // 偏移量
    offset.QuadPart = blockNum * BLOCK_SIZE; // 计算偏移量
    // 移动文件指针到指定位置
    if (!SetFilePointerEx(FileHandle, offset,
                          nullptr, FILE_BEGIN)) {
        throw std::runtime_error("Unable to set file pointer for read operation.");
    }

    DWORD bytesRead; // 读取字节数
    // 读取指定大小的数据
    if (!ReadFile(FileHandle, data, BLOCK_SIZE,
                  &bytesRead, nullptr) || bytesRead != BLOCK_SIZE) {
        throw std::runtime_error("Unable to read from disk.");
    }
    Reads++;
}

void Disk::write(int blockNum, char *data) {
    sanity_check(blockNum, data);

    LARGE_INTEGER offset;
    offset.QuadPart = blockNum * BLOCK_SIZE;

    if (!SetFilePointerEx(FileHandle, offset,
                          nullptr, FILE_BEGIN)) {
        throw std::runtime_error("Unable to set file pointer for write operation.");
    }

    DWORD bytesWritten;
    if (!WriteFile(FileHandle, data, BLOCK_SIZE,
                   &bytesWritten, nullptr) || bytesWritten != BLOCK_SIZE) {
        throw std::runtime_error("Unable to write to disk.");
    }
    Writes++;
}

