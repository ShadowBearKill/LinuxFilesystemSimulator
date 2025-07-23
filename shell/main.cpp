#include <windows.h>
#include <iostream>
#include <string>
#include <stdexcept>

#define streq(a, b) (strcmp((a), (b)) == 0)

constexpr int SHARED_MEMORY_SIZE = 1024;
// 共享内存结构
// 共享内存结构
struct SharedMemory {
    char command[256];      // 命令
    char arg1[256];         // 参数1
    char arg2[1024];         // 参数2
    char result[1024];       // 结果
    char path[128];
    char name[32];
    char eventName[64]; // 用于存储事件对象的名称
};
struct Pool {
    char pool[10];        // 状态数组：'n' 空闲, 'r' 正在申请, 'y' 已分配
    HANDLE poolMutex;     // 互斥信号量，用于同步访问 pool
};

class Shell {
private:
    HANDLE hEvent;  // 事件对象句柄
    HANDLE newShell;  // 新Shell事件句柄
    SharedMemory* shm;  // 共享内存指针
    Pool* pool;          // 编号池执政
    HANDLE hMapFile;    // 文件映射句柄
    HANDLE hPool;    // 文件映射句柄
    int userId;         // 当前用户ID
    HANDLE hConsole;    // 控制台句柄，用来修改控制台字体颜色
    char* username = nullptr;  // 用户名
    std::string dir;      // 当前目录


public:
    Shell() {

        hPool = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, L"Local\\Pool");
        if (!hPool) {
            throw std::runtime_error("Could not open file mapping.");
        }

        pool = (Pool*)MapViewOfFile(hPool, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Pool));
        if (!pool) {
            CloseHandle(hPool);
            throw std::runtime_error("Could not map view of file.");
        }


        // 初始化
        userId = allocateUserId();
        if(userId==-1){
            std::cout<<"No available user id.";
            throw std::runtime_error("No available user id.");
        }
        std::cout << "Allocated User ID: " << userId << std::endl;

        std::string s = "Local\\SharedMemory" + std::to_string(userId);

        // 打开共享内存
        hMapFile = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, s.c_str());
        if (!hMapFile) {
            throw std::runtime_error("Could not open file mapping.");
        }

        shm = (SharedMemory*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory));
        if (!shm) {
            CloseHandle(hMapFile);
            throw std::runtime_error("Could not map view of file.");
        }

        std::string eventName = "Local\\ShellEvent" + std::to_string(userId);
        strcpy(shm->eventName, eventName.c_str());
        hEvent = OpenEvent(EVENT_ALL_ACCESS, FALSE, eventName.c_str());
        if (!hEvent) {
            throw std::runtime_error("Could not open event.");
        }

        newShell = OpenEvent(EVENT_ALL_ACCESS, FALSE, L"Local\\NewShellEvent");
        if (!newShell) {
            throw std::runtime_error("Could not open new shell event.");
        }
        SetEvent(newShell);  // 通知新Shell事件

        hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    }

    ~Shell() {
        CloseHandle(hEvent);
        CloseHandle(newShell);
        UnmapViewOfFile(shm);
        UnmapViewOfFile(pool);
        CloseHandle(hMapFile);
        CloseHandle(hPool);
        CloseHandle(hConsole);
    }

    void wait() {
        // 等待事件触发
        WaitForSingleObject(hEvent, INFINITE);
    }

    void notify() {
        // 通知事件触发
        SetEvent(hEvent);
    }

    // 分配用户ID
    int allocateUserId() {
        int retryCount = 0;
        const int maxRetries = 100; // 最大重试次数

        while (retryCount < maxRetries) {
            // 获取互斥信号量
            WaitForSingleObject(pool->poolMutex, INFINITE);

            // 遍历 pool，寻找空闲槽位
            for (int i = 0; i < 10; i++) {
                if (pool->pool[i] == 'n') {  // 找到空闲槽位
                    pool->pool[i] = char(i + 1);  // 分配槽位并设置为用户 ID

                    // 释放互斥信号量
                    ReleaseSemaphore(pool->poolMutex, 1, nullptr);

                    return i + 1;  // 返回分配的用户 ID
                }
            }

            // 释放互斥信号量
            ReleaseSemaphore(pool->poolMutex, 1, nullptr);

            // 当前无空闲槽位，等待一段时间后重试
            retryCount++;
            Sleep(100); // 等待100毫秒
        }

        // 超过最大重试次数，返回失败
        return -1;
    }

    void releaseUserId() {
        // 重置共享内存的所有字段
        ZeroMemory(shm, sizeof(SharedMemory));
        std::cout<< "Start Releasing: " << userId << std::endl;

        // 获取互斥信号量
        WaitForSingleObject(pool->poolMutex, INFINITE);
        std::cout << "Releasing User ID: " << userId << std::endl;

        // 检查并释放槽位
        int index = userId - 1;
        if (index >= 0 && index < 10 && pool->pool[index] == 'y') {
            pool->pool[index] = 'n';  // 重置为空闲状态
        }
        // 释放互斥信号量
        ReleaseSemaphore(pool->poolMutex, 1, nullptr);

    }


    void printPre()
    {
        if(username==nullptr){
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
            std::cout<<"WITHOUT LOGIN";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout<<":";
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE);
            std::cout<<"~"<<dir;
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout<<"$ ";
            std::cout.flush();
        }
        else{
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN);
            std::cout<<username<<"@";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout<<":";
            SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE);
            std::cout<<dir<<"~";
            SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
            std::cout<<"$ ";
            std::cout.flush();
        }
    }

    void sendCommand(const std::string& cmd, const std::string& arg1 = "", const std::string& arg2 = "") {
        strcpy(shm->command, cmd.c_str());
        strcpy(shm->arg1, arg1.c_str());
        strcpy(shm->arg2, arg2.c_str());
    }
    void clearCommand() {
        ZeroMemory(shm->command,strlen(shm->command));
        ZeroMemory(shm->arg1,strlen(shm->arg1));
        ZeroMemory(shm->arg2,strlen(shm->arg2));
        ZeroMemory(shm->result,strlen(shm->result));
    }

    void run() {
        std::cout << "Shell started. Enter commands:" << std::endl;

        while (true) {
            char line[BUFSIZ];
            char cmd[BUFSIZ];
            char arg[BUFSIZ];
            char arg1[BUFSIZ];
            printPre();
            if (fgets(line, BUFSIZ, stdin) == nullptr) {
                break;
            }
            sscanf(line, "%s %s %s", cmd, arg, arg1);
            if (arg == 0) {
                continue;
            }
            sendCommand(cmd, arg, arg1);
            notify();  // 通知后端处理命令

            if (streq(cmd, "mkdir")) {
                wait();
            }
            else if (streq(cmd, "cd")) {
                wait();
                dir = shm->path;
            }
            else if (streq(cmd, "pwd")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "touch")) {
                wait();
            }
            else if (streq(cmd, "rm")) {
                wait();
            }
            else if (streq(cmd, "cat")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "ls")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "vim")) {
                wait();
            }
            else if (streq(cmd, "append")) {
                wait();
            }
            else if (streq(cmd, "exec")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "register")) {
                wait();
            }
            else if (streq(cmd, "unregister")) {
                wait();
            }
            else if (streq(cmd, "login")) {
                wait();
                username = shm->name;
                dir = shm->path;
            }
            else if (streq(cmd, "help")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "copyin")) {
                wait();
            }
            else if (streq(cmd, "copyout")) {
                wait();
            }
            else if (streq(cmd, "chmod")) {
                wait();
            }
            else if (streq(cmd, "info")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "check")) {
                wait();
                std::cout<<shm->result;
            }
            else if (streq(cmd, "exit")) {
                wait();
                releaseUserId();
                break;
            }
            else{
                std::cout << "Unknown command: " << cmd << std::endl;
                std::cout << "Type 'help' for a list of commands." << std::endl;
            }
            clearCommand();
            Sleep(100);
        }
    }
};

int main() {
    try {
        Shell shell;
        shell.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}