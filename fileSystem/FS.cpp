//
// Created by 追影子的熊 on 2024/10/30.
//

#include "FS.h"
#include <algorithm>


// 文件系统测试函数
std::string FileSystem::debug() {
    Block block{};
    // 阅读超级块
    std::string result;
    result += "SuperBlock:\n\tThe magic number: " + std::to_string(MAGIC_NUMBER)
              + "\n\tThe number of blocks: "+std::to_string(disk_->size())
              + "\n\tThe number of inode blocks: "+std::to_string(disk_->size()/10)
              + "\n\tThe number of inode"+std::to_string(disk_->size()/10*INODES_PER_BLOCK)+" inodes\n";
    int UsedInodesNum = std::count(freeInodeBitmap.begin(), freeInodeBitmap.end(), false);
    int UsedBlocksNum = std::count(freeDataBlockBitmap.begin(), freeDataBlockBitmap.end(), false);

    result+="Used inodes: "+std::to_string(UsedInodesNum)+"\n";
    result+="Used blocks: "+std::to_string(UsedBlocksNum)+"\n";

    return result;
}

void FileSystem::check() {
    Block block{};
    bool SuperBlockValid = true;
    // 加载并检查超级块
    std::cout<< "Checking SuperBlock...\n";
    disk_->read(0, block.Data);
    if (block.Super.MagicNumber != MAGIC_NUMBER) {
        std::cerr << "Error: Invalid magic number in superblock" << std::endl;
        block.Super.MagicNumber = MAGIC_NUMBER;
        SuperBlockValid = false;
    }
    if (block.Super.BlockNum != disk_->size()) {
        std::cerr << "Error: Invalid number of blocks in superblock" << std::endl;
        block.Super.BlockNum = disk_->size();
        SuperBlockValid = false;
    }
    if (block.Super.InodeBlocksIndex != disk_->size()/10) {
        std::cerr << "Error: Invalid number of inode blocks in superblock" << std::endl;
        block.Super.InodeBlocksIndex = disk_->size()/10;
        SuperBlockValid = false;
    }
    if (block.Super.InodesNum != disk_->size()/10*INODES_PER_BLOCK) {
        std::cerr << "Error: Invalid number of inodes in superblock" << std::endl;
        block.Super.InodesNum = disk_->size()/10*INODES_PER_BLOCK;
        SuperBlockValid = false;
    }
    if (SuperBlockValid) {
        std::cout << "Superblock is valid!!!" << std::endl;
    }
    else{
        std::cout << "Superblock is invalid, fixing..." << std::endl;
        disk_->write(0, block.Data);
    }


    // 初始化位图
    std::vector<bool> tempFreeInodeBitmap(INODES_PER_BLOCK * inodeBlocks, true);
    std::vector<bool> tempDataBlockBitmap(disk_->size() - inodeBlocks - 1, true);

    for (int i = 1; i <= inodeBlocks; i++) {
        disk_->read(i, block.Data);
        // 对于每一个Inode块，都有INODES_PER_BLOCK个Inode
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            // 将非空闲的Inode打印出来
            const Inode& inode = block.Inodes[j];
            if (inode.Valid == 1) {
                tempFreeInodeBitmap[(i - 1) * INODES_PER_BLOCK + j] = false;
                // 由于这个Inode不为空，所以拥有DataBlock, 也有可能啥也没存这个Inode
                // 读直接块
                for (unsigned int k : inode.Direct) {
                    if (k != 0) {
                        tempDataBlockBitmap[k - inodeBlocks - 1] = false;
                    }
                }
                // 读间接块
                if (inode.Indirect != 0) {
                    tempDataBlockBitmap[inode.Indirect - inodeBlocks - 1] = false;
                    // 读间接块指向的数据块
                    Block indirectBlock{};
                    disk_->read(inode.Indirect, indirectBlock.Data);
                    for (unsigned int Pointer : indirectBlock.Pointers) {
                        if (Pointer != 0) {
                            tempDataBlockBitmap[Pointer - inodeBlocks - 1] = false;
                        }
                    }
                }
            }
        }
    }
    std::cout << "Checking free inode bitmap..." << std::endl;
    if (tempFreeInodeBitmap != freeInodeBitmap) {
        std::cerr << "Error: Invalid free inode bitmap" << std::endl;
        // 合并位图
        freeInodeBitmap = tempFreeInodeBitmap;
    }
    else {
        std::cout << "Free inode bitmap is valid!!!" << std::endl;
    }

    std::cout << "Checking free data block bitmap..." << std::endl;
    if (tempDataBlockBitmap != freeDataBlockBitmap) {
        std::cerr << "Error: Invalid free data block bitmap" << std::endl;
        // 合并位图
        freeDataBlockBitmap = tempDataBlockBitmap;
    }
    else {
        std::cout << "Free data block bitmap is valid!!!" << std::endl;
    }
    std::cout.flush();
}

// 格式化文件系统
bool FileSystem::format(Disk* disk) {
    // 写超级块
    Block block{};
    block.Super.MagicNumber = MAGIC_NUMBER;
    block.Super.BlockNum = disk->size();
    block.Super.InodeBlocksIndex = block.Super.BlockNum / 10;
    block.Super.InodesNum = block.Super.InodeBlocksIndex * INODES_PER_BLOCK;
    disk->write(0, block.Data);
    // 清空所有其他块
    memset(block.Data, 0, Disk::BLOCK_SIZE);
    for (int i = 1; i < disk->size(); i++) {
        disk->write(i, block.Data);
    }
    return true;
}

// 挂载文件系统
bool FileSystem::mount(Disk* disk) {
    Block block{};
    // 读超级块
    disk->read(0, block.Data);
    if (block.Super.MagicNumber != MAGIC_NUMBER) {
        return false;
    }
    // 设置设备和安装
    disk->mount();
    disk_ = disk;
    // 复制元数据
    inodeBlocks = block.Super.InodeBlocksIndex;
    // 初始化位图
    freeInodeBitmap.clear();
    freeInodeBitmap.resize(INODES_PER_BLOCK * inodeBlocks, true);
    freeDataBlockBitmap.clear();
    freeDataBlockBitmap.resize(disk->size() - inodeBlocks - 1, true);
    // 遍历Inode，将空闲的Inode加入freeInodeBitmap; 将非空闲的DataBlock加入freeDataBlockBitmap
    for (int i = 1; i <= inodeBlocks; i++) {
        disk->read(i, block.Data);
        // 对于每一个Inode块，都有INODES_PER_BLOCK个Inode
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            // 将非空闲的Inode打印出来
            const Inode& inode = block.Inodes[j];
            if (inode.Valid == 1) {
                freeInodeBitmap[(i - 1) * INODES_PER_BLOCK + j] = false;
            }
            // 由于这个Inode不为空，所以拥有DataBlock, 也有可能啥也没存这个Inode
            // 读直接块
            for (unsigned int k : inode.Direct) {
                if (k != 0) {
                    freeDataBlockBitmap[k - inodeBlocks - 1] = false;
                }
            }
            // 读间接块
            if (inode.Indirect != 0) {
                freeDataBlockBitmap[inode.Indirect - inodeBlocks - 1] = false;
                // 读间接块指向的数据块
                Block indirectBlock{};
                disk->read(inode.Indirect, indirectBlock.Data);
                for (unsigned int Pointer : indirectBlock.Pointers) {
                    if (Pointer != 0) {
                        freeDataBlockBitmap[Pointer - inodeBlocks - 1] = false;
                    }
                }
            }
        }
    }
    return true;
}

// 创建i结点
ssize_t FileSystem::create() {
    // 在i结点表中查找空闲i结点
    try {
        Block block{};
        ssize_t inodeNumber = allocInode();
        if (inodeNumber < 0) {
            return -1;
        }
        // 如果找到，记录i节点所在位置
        size_t inodeBlock = (inodeNumber / INODES_PER_BLOCK) + 1;
        size_t inodeInBlock = inodeNumber % INODES_PER_BLOCK;
        // 读出Inode所在的Inode块
        disk_->read(inodeBlock, block.Data);
        // 初始化Inode节点，一开始的Inode当然是没有存东西，所以没有指向数据块
        Inode &inode = block.Inodes[inodeInBlock];
        inode.Valid = 1;
        inode.Size = 0;
        std::fill(std::begin(inode.Direct), std::end(inode.Direct), 0);
        inode.Indirect = 0;
        // 写回Inode块
        disk_->write(inodeBlock, block.Data);
        return static_cast<ssize_t>(inodeNumber);
    }
    catch(const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
}

// 移除i结点
bool FileSystem::remove(size_t inumber) {
    try {
        Block block{};
        Block tmpBlock{};
        size_t inodeBlock = inumber / INODES_PER_BLOCK + 1;
        size_t inodeInBlock = inumber % INODES_PER_BLOCK;
        // 读出Inode所在的Inode块
        disk_->read(inodeBlock, block.Data);
        Inode &inode = block.Inodes[inodeInBlock];
        // 所谓释放数据块，其实就是操纵这些数据块的空闲位图
        // 释放直接数据块
        for (unsigned int & i : inode.Direct) {
            if (i >= (1 + inodeBlocks)) {
                freeDataBlockBitmap[i - 1 - inodeBlocks] = true;
                disk_->read(i, tmpBlock.Data);
                memset(tmpBlock.Data, 0, Disk::BLOCK_SIZE);
            }
            i = 0;
        }
        // 释放间接数据块
        if (inode.Indirect >= (1 + inodeBlocks)) {
            freeDataBlockBitmap[inode.Indirect - 1 - inodeBlocks] = true;
            disk_->read(inode.Indirect, tmpBlock.Data);
            for (unsigned int & i : tmpBlock.Pointers) {
                if (i >= (1 + inodeBlocks)) {
                    freeDataBlockBitmap[i - 1 - inodeBlocks] = true;
                }
                i = 0;
            }
            memset(tmpBlock.Data, 0, Disk::BLOCK_SIZE);
        }
        inode.Indirect = 0;
        inode.Valid = 0; // 标记i结点为无效
        inode.Size = 0;
        freeInodeBitmap[inumber] = true;
        // 写回Inode块
        disk_->write(inodeBlock, block.Data);
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return false;
    }
}

// 获得i结点的文件大小
ssize_t FileSystem::stat(size_t inumber) {
    try {
        if (freeInodeBitmap[inumber]) {
            return -1;
        }
        size_t inodeBlock = (inumber / INODES_PER_BLOCK) + 1;
        size_t inodeInBlock = inumber % INODES_PER_BLOCK;
        Block block{};
        disk_->read(inodeBlock, block.Data);
        return block.Inodes[inodeInBlock].Size;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return -1;
    }
}

// 读i结点的文件数据
ssize_t FileSystem::read(size_t inumber, char* data, size_t length, size_t offset) {
    Block block{};
    Block dataBlock{};
    char* start = data;
    disk_->read(inumber / Disk::BLOCK_SIZE + 1, block.Data);
    Inode& inode = block.Inodes[inumber % Disk::BLOCK_SIZE];

    if (offset > inode.Size) {
        return -1;
    }
    if (offset + length > inode.Size) {
        length = inode.Size - offset;
    }
    int beginOffset = offset % Disk::BLOCK_SIZE;
    int beginDataBlock = offset / Disk::BLOCK_SIZE;
    int endDataBlock = (offset + length) / Disk::BLOCK_SIZE;
    int endOffset = (offset + length) % Disk::BLOCK_SIZE;
    int readNum = 0;

    if (beginOffset + length < Disk::BLOCK_SIZE) {
        int dataBlockNum = getInodeDataBlock(inode, beginDataBlock);
        disk_->read(dataBlockNum, dataBlock.Data);
        memcpy(data, dataBlock.Data + beginOffset, length);
        data += length;
        readNum += length;
    } else {
        for (int i = beginDataBlock; i <= endDataBlock; i++) {
            int dataBlockNum = getInodeDataBlock(inode, i);
            disk_->read(dataBlockNum, dataBlock.Data);
            if (i == beginDataBlock) {
                memcpy(data, dataBlock.Data + beginOffset, Disk::BLOCK_SIZE - beginOffset);
                data += Disk::BLOCK_SIZE - beginOffset;
                readNum += Disk::BLOCK_SIZE - beginOffset;
            } else if (i == endDataBlock) {
                memcpy(data, dataBlock.Data, endOffset);
                data += endOffset;
                readNum += endOffset;
            } else {
                memcpy(data, dataBlock.Data, Disk::BLOCK_SIZE);
                data += Disk::BLOCK_SIZE;
                readNum += Disk::BLOCK_SIZE;
            }
        }
    }
    return readNum;
}

// Write to inode
ssize_t FileSystem::write(size_t inumber, char* data, size_t length, size_t offset) {
    const size_t MAX_LENGTH = 267264; // 16KB + 16MB
    Block block{};
    if (offset + length > MAX_LENGTH) {
        length = MAX_LENGTH - offset;
    }
    disk_->read(inumber / Disk::BLOCK_SIZE + 1, block.Data);
    Inode& inode = block.Inodes[inumber % Disk::BLOCK_SIZE];
    if (offset > inode.Size) {
        return -1;
    }
    // 要去找一个数据块出来
    // 数据块需要通过offset来获得
    int beginDataBlock = offset / Disk::BLOCK_SIZE;
    int beginOffset = offset % Disk::BLOCK_SIZE;
    int endDataBlock = (offset + length) / Disk::BLOCK_SIZE;
    int remainWrite = (offset + length) % Disk::BLOCK_SIZE;
    inode.Size = offset + length;
    int writeNum = 0;
    if (beginOffset + length < Disk::BLOCK_SIZE) {
        Block dataBlock{};
        int dataBlockNum = getInodeDataBlock(inode, beginDataBlock);
        if (dataBlockNum <= 0) {
            allocateNewBlock(inode);
            dataBlockNum = getInodeDataBlock(inode, beginDataBlock);
        }
        disk_->read(dataBlockNum, dataBlock.Data);
        memcpy(dataBlock.Data + beginOffset, data, length);
        disk_->write(dataBlockNum, dataBlock.Data);
        writeNum += length;
    } else {
        for (int i = beginDataBlock; i <= endDataBlock; i++) {
            Block dataBlock{};
            int dataBlockNum = getInodeDataBlock(inode, i);
            if (dataBlockNum <= 0) {
                allocateNewBlock(inode);
                dataBlockNum = getInodeDataBlock(inode, i);
            }
            disk_->read(dataBlockNum, dataBlock.Data);
            if (i == beginDataBlock) {
                memcpy(dataBlock.Data + beginOffset, data, Disk::BLOCK_SIZE - beginOffset);
                data = data + Disk::BLOCK_SIZE - beginOffset;
                disk_->write(dataBlockNum, dataBlock.Data);
                writeNum += Disk::BLOCK_SIZE - beginOffset;
            } else if (i == endDataBlock) {
                memcpy(dataBlock.Data, data, remainWrite);
                data = data + remainWrite;
                disk_->write(dataBlockNum, dataBlock.Data);
                writeNum += remainWrite;
            } else {
                memcpy(dataBlock.Data, data, Disk::BLOCK_SIZE);
                data = data + Disk::BLOCK_SIZE;
                disk_->write(dataBlockNum, dataBlock.Data);
                writeNum += Disk::BLOCK_SIZE;
            }
        }
    }
    disk_->write(inumber / Disk::BLOCK_SIZE + 1, block.Data);
    return writeNum;
}

// 找到空闲的i结点
ssize_t FileSystem::allocInode() {
    auto it = std::find(freeInodeBitmap.begin(), freeInodeBitmap.end(), true);
    if (it == freeInodeBitmap.end()) {
        return -1; // No free inode
    }
    size_t index = std::distance(freeInodeBitmap.begin(), it);
    freeInodeBitmap[index] = false;
    return index;
}

// 找到空闲数据块
ssize_t FileSystem::allocDataBlock() {
    auto it = std::find(freeDataBlockBitmap.begin(), freeDataBlockBitmap.end(), true);
    if (it == freeDataBlockBitmap.end()) {
        return -1; // No free data block
    }
    size_t index = std::distance(freeDataBlockBitmap.begin(), it);
    freeDataBlockBitmap[index] = false;
    return index + inodeBlocks + 1; // Adjust for superblock and inode blocks
}


// 直接返回直接指针 如果nowBlock在间接指针块中，然而间接指针块不存在，返回-1
int FileSystem::getInodeDataBlock(Inode& inode, int nowBlock) {
    if (nowBlock < POINTERS_PER_INODE) {
        return inode.Direct[nowBlock];
    }
    if (inode.Indirect != 0) {
        Block block{};
        disk_->read(inode.Indirect, block.Data);
        return block.Pointers[nowBlock - POINTERS_PER_INODE];
    }
    return -1; // 没有数据块被分配
}

// 分配新块
void FileSystem::allocateNewBlock(Inode& inode) {
    for(unsigned int & i : inode.Direct) {
        if (i == 0) {
            int newDataBlock = allocDataBlock();
            if (newDataBlock < 0) {
                throw std::runtime_error("disk is full");
            }
            i = newDataBlock;
            return;
        }
    }
    if (inode.Indirect == 0) {
        int newDataBlock = allocDataBlock();
        if (newDataBlock < 0) {
            throw std::runtime_error("disk is full");
        }
        inode.Indirect = newDataBlock;
    }
    Block indirectBlock{};
    disk_->read(inode.Indirect, indirectBlock.Data);
    int newDataBlock = allocDataBlock();
    if (newDataBlock < 0) {
        throw std::runtime_error("disk is full");
    }
    for (unsigned int & Pointer : indirectBlock.Pointers) {
        if (Pointer == 0) {
            int newDataBlock = allocDataBlock();
            if (newDataBlock < 0) {
                throw std::runtime_error("disk is full");
            }
            Pointer = newDataBlock;
            disk_->write(inode.Indirect, indirectBlock.Data);
            return;
        }
    }
    throw std::runtime_error("file is full");
}

