//
// Created by 追影子的熊 on 2024/11/5.
//

#ifndef FSTEST_FSMANAGER_H
#define FSTEST_FSMANAGER_H

#include "FS.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

class WritePrioritizedLock {
private:
    std::mutex mtx_;                // 主互斥锁
    std::condition_variable cv_;    // 条件变量
    int readers_ = 0;               // 当前读者数
    int writersWaiting_ = 0;        // 等待写入的线程数
    bool writerActive_ = false;     // 当前是否有写线程占用

public:
    // 允许多个读线程，只要没有写线程在执行或者等待
    void lock_shared() {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this]() { return !writerActive_ && writersWaiting_ == 0; });
        ++readers_;
    }

    void unlock_shared() {
        std::unique_lock<std::mutex> lock(mtx_);
        --readers_;
        if (readers_ == 0) {
            cv_.notify_all();
        }
    }

    // 写优先策略：写线程需要等待所有线程退出
    void lock() {
        std::unique_lock<std::mutex> lock(mtx_);
        ++writersWaiting_;
        cv_.wait(lock, [this]() { return !writerActive_ && readers_ == 0; });
        --writersWaiting_;
        writerActive_ = true;
    }

    void unlock() {
        std::unique_lock<std::mutex> lock(mtx_);
        writerActive_ = false;
        cv_.notify_all();
    }
};

struct UserInfo {
    std::string username_;  // 用户名
    int userID_;            // 用户唯一ID
    std::string currentDirPath_;   // 当前目录路径
    int workDirInumber_;    // 当前目录的I结点
};

struct LockWrapper {
    WritePrioritizedLock lock;
    int activeCount = 0;  // 当前使用锁的线程数

    void increment() { activeCount++; }
    void decrement() { activeCount--; }
    bool isUnused() const { return activeCount == 0; }
};


struct SharedMemory {
    char command[256];      // 命令
    char arg1[256];         // 参数1
    char arg2[1024];         // 参数2
    char result[1024];       // 结果
    char path[128];
    char name[32];
    char prompt[512];       // 交互提示信息
    char userInput[256];    // 用户输入
    bool needConfirm;       // 是否需要用户确认
};

constexpr static int MaxUsers = 10;
struct Pool {
    char pool[MaxUsers];        // 状态数组：'n' 表示空闲，其他值表示已分配
    HANDLE poolMutex;     // 互斥信号量，用于同步访问 pool
};


class FSManager {
public:

    constexpr static uint32_t FILENAME_MAX_LENGTH = 16; // 说是16，但由于需要存'\0'所以实际是15

private:
    enum TYPE {
        DIR = 0,
        NORMAL = 1
    };

    enum PERMISSION {
        NUL = 0,X = 1,____W_ = 2,____WX = 3,___R__ = 4,___R_X = 5,___RW_ = 6,___RWX = 7,
        __X___ = 8,__X__X = 9,__X_W_ = 10,__X_WX = 11,__XR__ = 12,__XR_X = 13,__XRW_ = 14,__XRWX = 15,
        _W____ = 16,_W___X = 17,_W__W_ = 18,_W__WX = 19,_W_R__ = 20,_W_R_X = 21,_W_RW_ = 22,_W_RWX = 23,
        _WX___ = 24,_WX__X = 25,_WX_W_ = 26,_WX_WX = 27,_WXR__ = 28,_WXR_X = 29,_WXRW_ = 30,_WXRWX = 31,
        R_____ = 32,R____X = 33,R___W_ = 34,R___WX = 35,R__R__ = 36,R__R_X = 37,R__RW_ = 38,R__RWX = 39,
        R_X___ = 40,R_X__X = 41,R_X_W_ = 42,R_X_WX = 43,R_XR__ = 44,R_XR_X = 45,R_XRW_ = 46,R_XRWX = 47,
        RW____ = 48,RW___X = 49,RW__W_ = 50,RW__WX = 51,RW_R__ = 52,RW_R_X = 53,RW_RW_ = 54,RW_RWX = 55,
        RWX___ = 56,RWX__X = 57,RWX_W_ = 58,RWX_WX = 59,RWXR__ = 60,RWXR_X = 61,RWXRW_ = 62,RWXRWX = 63,
    };

    struct DirItem {
        uint32_t FileType;
        uint32_t FileOwnerID;
        uint32_t Inumber;
        uint32_t FilePermission;
        char FileName[FILENAME_MAX_LENGTH];
    };

public:
    FSManager(const std::string& diskFile, int blocks);
    ~FSManager();
    void registerUser(int slot,const std::string& username, const std::string& password);
    void unregisterUser(int slot,const std::string& username);
    void login(int slot,std:: string username,std:: string password);
    void ls(int slot);
    void mkdir(int slot,std::string filename1);
    void cd(int slot,std::string filename1);
    void pwd(int slot);
    std::string getCurrentDirPath(int slot);
    static void splitPath(std::string& path);
    void touch(int slot,std::string filename1,uint32_t permission = RW_R__,bool init = false);
    void rm(int slot,std::string filename1);
    void rm(int slot,std::string filename1, bool force); // 支持强制删除的版本
    void vim(int slot,std::string filename1,std::string text="");
    void vim2(int slot,std::string filename1, const std::string& text,bool append=false,bool init = false);
    void cat(int slot,std::string filename1);
    void copy(int slot,std::string outfile1, std::string infile1);
    void copyin(int slot,std::string infile1,std::string outfile1);
    void copyout(int slot,std::string outfile1,std::string infile1);
    void exec(int slot,std::string filename1);
    void chmod(int slot,std::string mod, std::string filename1);
    void do_exit(int slot);
    void info(int slot);
    void check(int slot);
    void run();

    void do_mkdir(int slot,std::string arg);
    void do_cd(int slot,std::string arg);
    void do_pwd(int slot,std::string arg);
    void do_touch(int slot,std::string arg);
    void do_rm(int slot,std::string arg);
    void do_cat(int slot,std::string arg);
    void do_ls(int slot,std::string arg);
    void do_vim(int slot,std::string arg, const std::string& arg1);
    void do_append(int slot,std::string arg, const std::string& arg1);
    void do_exec(int slot,std::string arg);
    void do_register(int slot,const std::string& arg1,const std::string& arg2);
    void do_unregister(int slot,const std::string& arg);
    void do_login(int slot,std::string arg1, std::string arg2);
    void do_help(int slot,std::string arg);
    void do_copyin(int slot,std::string arg1, std::string arg2);
    void do_copyout(int slot,std::string arg1, std::string arg2);
    void do_chmod(int slot,std::string arg1, std::string arg2);
    void do_info(int slot,std::string arg);
    void do_check(int slot,std::string arg);


private:
    void createRootDir();
    void createHomeDir();
    void makeDir(int slot,int inumber, std::string name,int ownerID = -1,bool init = false);
    uint32_t getPathInumber(const std::string& dirname);
    static bool isFileExist(std::vector<DirItem>& items, std::string name);
    void getHomeDirInumber(int slot);
    void appendDirItem(char** data, DirItem& item);
    void writeBackDir(int inumber, std::vector<DirItem>& items);
    [[nodiscard]] bool haveWritePermission(int slot,uint32_t permission, uint32_t userID) const;
    [[nodiscard]] bool haveExePermission(int slot,uint32_t permission, uint32_t userID) const;
    [[nodiscard]] bool haveReadPermission(int slot,uint32_t permission, uint32_t userID) const;
    std::string getDirName(int parentInumber, int childInumber);
    int getDirOwnerID(int Inumber);
    void rmRecursive(int slot, int dirInumber); // 递归删除目录的辅助函数
    static std::string permissionToString(uint32_t p, uint32_t type);
    uint32_t StringToPermission(const std::string& str);
    DirItem makeDirItem(const uint32_t& fileType, const uint32_t& fileSize, const uint32_t& inumber,
                        const uint32_t& filePermission, std::string filename);
    // 获取inumber的所有目录项, 默认程序通过某种方式已经判断了inumber对应的文件是一个目录文件
    std::vector<DirItem> getDirItems(int inumber);
    std::string getUserPassword(const std:: string& username);
    int getUserID(const std:: string& username, bool isNewUser = false);
    void getUserInfo();
    void handleSlot(int slot);
    std::shared_ptr<LockWrapper> getFileLock(int inumber);
    void releaseFileLock(const std::shared_ptr<LockWrapper>& lock);

    std::unique_ptr<Disk> disk_ = nullptr; // 磁盘对象
    FileSystem fileSystem_; // 文件系统对象
    int rootDirInumber_ = 0; // 根目录的I结点
    int homeDirInumber_{}; // 用户目录的I结点
    int etcDirInumber_{}; // etc目录的I结点
    std::vector<UserInfo> userInfos_; // 用户信息数组
    std::unordered_map<std::string, int> userIDMap_; // 用户名到ID的映射
    std::unordered_map<std::string, std::string> userHashMap_; // 用户名到密码哈希的映射

    SharedMemory* shm[10]{};  // 共享内存指针
    Pool* pool;            //编号池指针
    HANDLE hMapFiles[10]{};    // 文件映射句柄
    HANDLE hEvents[10]{};    // 信号量句柄
    HANDLE newShell;    // 信号量句柄
    HANDLE hPool;    // 文件映射句柄
    std::shared_mutex userMapMtx; // 用于保护 userHashMap_ 和 userIDMap_

    // 文件锁映射：以 i结点编号为键，锁为值
    std::unordered_map<int, std::shared_ptr<LockWrapper>> fileLocks_;
    std::mutex fileLocksMutex_; // 用于保护 fileLocks_ 本身
};


#endif//FSTEST_FSMANAGER_H
