#include "FSManager.h"
#include <cstring>
#include <future>
#include <mutex>
#include <windows.h>


void FSManager::run() {
    std::vector<std::future<void>> futures(10); // 保存线程的 future


    while (true) {
        WaitForSingleObject(newShell, INFINITE); // 等待新Shell事件
        // 获取互斥信号量
        WaitForSingleObject(pool->poolMutex, INFINITE);
        for (int i = 0; i < 10; i++) {
            if (pool->pool[i] == char(i + 1)) {  // 找到空闲槽位
                pool->pool[i] = 'y';  // 分配线程
                futures[i] = std::async(std::launch::async, &FSManager::handleSlot, this, i);
                std::cout << "Starting thread for slot " << i << std::endl;
            }
        }
        ReleaseSemaphore(pool->poolMutex, 1, nullptr);
    }
}



void FSManager::handleSlot(int slot) {
    std::string cmd;
    std::string arg;
    std::string arg1;

    while (true) {
        WaitForSingleObject(hEvents[slot], INFINITE); // 等待命令准备好
        cmd = shm[slot]->command;
        arg = shm[slot]->arg1;
        arg1 = shm[slot]->arg2;
        try {
            if (cmd == "mkdir") {
                do_mkdir(slot,arg);
            }
            else if (cmd == "cd") {
                do_cd(slot,arg);
            }
            else if (cmd == "pwd") {
                do_pwd(slot,arg);
            }
            else if (cmd == "touch") {
                do_touch(slot,arg);
            }
            else if (cmd == "rm") {
                do_rm(slot,arg);
            }
            else if (cmd == "cat") {
                do_cat(slot,arg);
            }
            else if (cmd == "ls") {
                do_ls(slot,arg);
            }
            else if (cmd == "vim") {
                do_vim(slot,arg,arg1);
            }
            else if (cmd == "append") {
                do_append(slot,arg,arg1);
            }
            else if (cmd == "exec") {
                do_exec(slot,arg);
            }
            else if (cmd == "register") {
                do_register(slot,arg,arg1);
            }
            else if (cmd == "unregister") {
                do_unregister(slot,arg);
            }
            else if (cmd == "login") {
                do_login(slot,arg, arg1);
            }
            else if (cmd == "help") {
                std::cout << "Available commands:" << std::endl;
                do_help(slot,arg);
            }
            else if (cmd == "copyin") {
                do_copyin(slot,arg, arg1);
            }
            else if (cmd == "copyout") {
                do_copyout(slot,arg, arg1);
            }
            else if (cmd == "chmod") {
                do_chmod(slot,arg, arg1);
            }
            else if (cmd == "info") {
                do_info(slot,arg);
            }
            else if (cmd == "check") {
                do_check(slot,arg);
            }
            else if (cmd == "exit") {
                std::cout << "Exiting..." << std::endl;
                do_exit(slot);
                SetEvent(hEvents[slot]); // 通知主线程命令已处理完毕
                break;
            }
            else{
                std::cout << "Unknown command: " << cmd << std::endl;
                std::cout << "Type 'help' for a list of commands." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }
        SetEvent(hEvents[slot]); // 通知主线程命令已处理完毕
    }
    std::cout << "Thread for slot " << slot << " has exited" << std::endl;
}



int main() {

    std::string diskname("./data/disk.100");
    FSManager file_system_manager(diskname, 1024);

    file_system_manager.run();

    return 0;
}

void FSManager::do_mkdir(int slot,std::string arg) {
    try {
        mkdir(slot,std::string(arg));
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_cd(int slot,std::string arg) {
    try {
        cd(slot,std::string(arg));
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_pwd(int slot,std::string arg) {
    try {
        pwd(slot);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_touch(int slot,std::string arg) {
    try {
        touch(slot,std::string(arg));
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_rm(int slot,std::string arg) {
    try {
        rm(slot,std::string(arg));
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_cat(int slot,std::string arg) {
    try {
        cat(slot,std::string(arg));
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_ls(int slot,std::string arg) {
    try {
        ls(slot);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_vim(int slot,std::string arg, const std::string& arg1) {
    try {
        vim2(slot,arg, arg1);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_append(int slot,std::string arg, const std::string& arg1) {
    try {
        vim2(slot,arg, arg1,true);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_exec(int slot,std::string arg) {
    try {
        exec(slot,std::string(arg));
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_register(int slot,const std::string& arg1, const std::string& arg2) {
    try {
        registerUser(slot,arg1, arg2);
    }
    catch (const std::exception& e){
        std::cerr << e.what() << std::endl;
        strcat(shm[slot]->result, e.what());
    }
}

void FSManager::do_unregister(int slot,const std::string& arg) {
    try {
        unregisterUser(slot,arg);
    }
    catch (const std::exception& e){
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_login(int slot,std::string arg1, std::string arg2) {
    try {
        login(slot,arg1, arg2);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_help(int slot,std::string arg) {
    std::ostringstream resultStream;
    using std::cout;
    using std::endl;
    resultStream << "mkdir <filename>       --- create a new directory in workspace" << endl;
    resultStream << "cd    <filename>       --- change workspace to <filename>" << endl;
    resultStream << "pwd                    --- print the path of the workspace" << endl;
    resultStream << "touch <filename>       --- create a normal file in workspace" << endl;
    resultStream << "ls                     --- print all files of the workspace" << endl;
    resultStream << "vim   <filename>       --- edit <filename>" << endl;
    resultStream << "exec  <filename>       --- execute <filename> if it has permission" << endl;
    resultStream << "rm    <filename>       --- remove the <filename>" << endl;
    resultStream << "cat   <filename>       --- print the content of <filename>" << endl;
    resultStream << "register <username>    --- register a new user" << endl;
    resultStream << "unregister <username>  --- unregister a user if existing" << endl;
    resultStream << "login <username>       --- change workspace to /home/<username>" << endl;
    resultStream << "copyin <infile> <outfile>  --- copy outside <infile> to inside <outfile>" << endl;
    resultStream << "copyout <outfile> <infile> --- copy inside <infile> to outside <outfile>" << endl;
    resultStream << "chmod <mod> <filename> --- change the permission of <filename> to <mod>" << endl;
    resultStream << "help                   --- print the usage of commands" << endl;
    resultStream << "exit                   --- exit the program" << endl;
    std::string resultStr = resultStream.str();

    // 确保不会超出共享内存的大小
    if (resultStr.size() >= sizeof(shm[slot]->result)) {
        strncpy(shm[slot]->result, resultStr.c_str(), sizeof(shm[slot]->result) - 1);
        shm[slot]->result[sizeof(shm[slot]->result) - 1] = '\0'; // 确保字符串以 '\0' 结尾
    } else {
        strcpy(shm[slot]->result, resultStr.c_str()); // 直接拷贝
    }
}

void FSManager::do_copyin(int slot,std::string arg1, std::string arg2) {
    try {
        copyin(slot,arg1, std::string(arg2));
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_copyout(int slot,std::string arg1, std::string arg2) {
    try {
        copyout(slot,std::string(arg1), arg2);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_chmod(int slot,std::string arg1, std::string arg2) {
    try {
        chmod(slot,arg1, std::string(arg2));
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_info(int slot,std::string arg) {
    try {
        info(slot);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void FSManager::do_check(int slot,std::string arg) {
    try {
        check(slot);
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}
void FSManager::do_exit(int slot) {
    // 清空退出的 Shell 的用户信息
    userInfos_[slot].username_ = "";
    userInfos_[slot].currentDirPath_ = "";
    userInfos_[slot].userID_ = -1;
    userInfos_[slot].workDirInumber_ = -1;

    ZeroMemory(shm[slot], sizeof(SharedMemory)); // 清空共享内存
    ResetEvent(hEvents[slot]); // 重置事件

    // 输出日志，方便调试
    std::cout << "Cleared resources for shell ID: " << slot << std::endl;
}

