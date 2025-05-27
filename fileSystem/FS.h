//
// Created by 追影子的熊 on 2024/10/30.
//

#ifndef FSTEST_FS_H
#define FSTEST_FS_H

#include "disk.h"
#include <cstdint>
#include <vector>
#include <iostream>

class FileSystem {
public:
    static constexpr uint32_t MAGIC_NUMBER = 0xf0f03410; // 魔数
    static constexpr uint32_t INODES_PER_BLOCK = 32; // 每个块可以存放32个i结点
    static constexpr uint32_t POINTERS_PER_INODE = 5; // 每个i结点可以存放5个直接指针
    static constexpr uint32_t POINTERS_PER_BLOCK = 256; // 每个块可以存放256个指针
    static constexpr uint32_t FILENAME_MAX_LENGTH = 16; // 文件名最长长度，因为要存储‘\0’，所以只剩19个字符
    static constexpr uint32_t ITEMS_PER_BLOCK = 32; // 每个块可以存放32个目录项

private:
    struct SuperBlock {          // 超级块
        uint32_t MagicNumber;    // 魔数
        uint32_t BlockNum;         // 块数
        uint32_t InodeBlocksIndex;    // i节点结束时所在的块数
        uint32_t InodesNum;         // 索引节点数
    };

    struct Inode {               // 索引节点
        uint32_t Valid;           // 是否有效
        uint32_t Size;           // 文件大小，B为单位
        uint32_t Direct[POINTERS_PER_INODE];  // 直接指针
        uint32_t Indirect;       // 间接指针
    };
    // Block有不同的类型，所以干脆写成联合体方便复用
    union Block {                               // 块
        SuperBlock Super;                       // 超级块
        Inode Inodes[INODES_PER_BLOCK];         // i结点块
        uint32_t Pointers[POINTERS_PER_BLOCK];  // 指针块
        char Data[Disk::BLOCK_SIZE];            // 数据块
    };


    size_t inodeBlocks = 0;
    // 位图，用于记录哪些块是空闲的
    std::vector<bool> freeDataBlockBitmap;
    // 位图，用于记录哪些索引节点是空闲的
    std::vector<bool> freeInodeBitmap;
    // 虚拟磁盘指针
    Disk* disk_ = nullptr;

    ssize_t allocInode();                               // 分配i结点
    ssize_t allocDataBlock();                           // 分配数据块
    int getInodeDataBlock(Inode& inode, int nowBlock);  // 获得i结点的数据块
    void allocateNewBlock(Inode& inode);                // 分配新块

public:
    std::string debug();
    void check();
    static bool format(Disk* disk);
    bool mount(Disk* disk);
    ssize_t create();
    bool remove(size_t inumber);
    ssize_t stat(size_t inumber);
    ssize_t read(size_t inumber, char* data, size_t length, size_t offset);
    ssize_t write(size_t inumber, char* data, size_t length, size_t offset);
};


#endif//FSTEST_FS_H
