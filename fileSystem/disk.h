//
// Created by 追影子的熊 on 2024/10/30.
//

#ifndef FSTEST_DISK_H
#define FSTEST_DISK_H


#include <windows.h>
#include <stdexcept>

class Disk {
private:
    HANDLE FileHandle; // 磁盘文件的句柄
    size_t Blocks;     // 磁盘块的数量
    size_t Reads;      // 读者数量
    size_t Writes;     // 写者数量
    size_t Used;     // 挂载次数

    void sanity_check(int blockNum, const char *data) const; // 基础性检查

public:
    // 每块的大小
    const static size_t BLOCK_SIZE = 1024;

    // 默认构造函数
    Disk() : FileHandle(INVALID_HANDLE_VALUE), Blocks(0), Reads(0), Writes(0), Used(0) {}

    // 析构函数
    ~Disk();

    // 打开磁盘文件
    void open(const char *path, size_t nblocks);

    // 返回磁盘数量
    size_t size() const { return Blocks; }

    // 增加挂载次数
    void mount() { Used++; }

    // 从磁盘中读某个块
    void read(int blocknum, char *data);

    // 往磁盘某个块写入数据
    void write(int blocknum, char *data);
};


#endif//FSTEST_DISK_H
