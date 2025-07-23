//
// Created by 追影子的熊 on 2024/11/5.
//

#include "FSManager.h"
#include <algorithm>
#include <limits>
#include <utility>

#define USING() \
    using std::cerr; \
    using std::cout; \
    using std::endl; \
    using std::cin; \


// 文件管理系统初始化
FSManager::FSManager(const std::string& diskFile, int blocks){
    USING();
    // 初始化用户信息数组
    userInfos_.resize(10);
    for(auto& u : userInfos_){
        u.username_ = "";
        u.userID_ = -1;
        u.currentDirPath_ = "";
        u.workDirInumber_ = -1;
    }

    // 初始化文件映射
    hPool = CreateFileMapping(
            INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Pool), L"Local\\Pool");
    if (!hPool) {
        throw std::runtime_error("Could not create file mapping for pool.");
    }
    pool = (Pool*)MapViewOfFile(hPool, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Pool));
    if (!pool) {
        CloseHandle(hPool);
        throw std::runtime_error("Could not map view of file.");
    }
    // 初始化互斥信号量
    pool->poolMutex = CreateSemaphore(nullptr, 1, 1, L"Local\\PoolMutex");
    if (!pool->poolMutex) {
        throw std::runtime_error("Could not create semaphore.");
    }
    // 初始化状态数组
    for (char & i : pool->pool) {
        i = 'n';  // 所有槽位初始为空闲
    }
    // 初始化共享内存
    for (int i = 0; i <10;i++){
        std::string s = "Local\\SharedMemory" + std::to_string(i+1);
        hMapFiles[i] = CreateFileMapping(
                INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedMemory), s.c_str());
        if (!hMapFiles[i]) {
            throw std::runtime_error("Could not create file mapping.");
        }
        shm[i] = (SharedMemory*)MapViewOfFile(hMapFiles[i], FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory));
        if (!shm[i]) {
            CloseHandle(hMapFiles[i]);
            throw std::runtime_error("Could not map view of file.");
        }
        ZeroMemory(shm[i], sizeof(SharedMemory));
    }
    // 初始化信号量
    for (int i = 0; i < 10; i++) {
        std::string eventName = "Local\\ShellEvent" + std::to_string(i + 1);
        hEvents[i] = CreateEvent(nullptr, FALSE, FALSE, eventName.c_str());
        if (!hEvents[i]) {
            throw std::runtime_error("Could not create event.");
        }
    }
    newShell = CreateEvent(nullptr, FALSE, FALSE, L"Local\\NewShellEvent");
    if (!newShell) {
        throw std::runtime_error("Could not create new shell event.");
    }


    // 先打开一个磁盘
    disk_ = std::make_unique<Disk>();
    disk_->open(diskFile.c_str(), blocks);
    // 挂载磁盘到文件系统上，让文件系统来管理磁盘, 如果磁盘本身就是合法的，那自然挂载成功
    if(!fileSystem_.mount(disk_.get())) {
        // 挂载失败的原因: MagicNumber不合法, 这通常是因为没有对磁盘进行格式化导致的
        // 需要提示用户是否需要进行格式化
        FileSystem::format(disk_.get());
        if(fileSystem_.mount(disk_.get())) {
            // 格式化好文件目录
            // 每当系统创建一个新用户，就会有一个对应的目录在/home下被创建
            createRootDir();
            createHomeDir();
            touch(0,"/etc/passwd",RW_R__,true);
            touch(0,"/etc/shadow",RW____,true);
            vim2(0,"/etc/passwd","root:0\n",false,true);
            vim2(0,"/etc/shadow","root:"+std::to_string(std::hash<std::string>()("123"))+"\n",false,true);
        }
    }
    // 默认将根目录存放在Inode号为0的文件中。如果不是这样，那出现的问题就是格式问题，不是本系统负责的内容
    for (int i = 0; i < 10; i++) {
        getHomeDirInumber(i);
    }
    getUserInfo();
}

FSManager::~FSManager() {
        USING();
        // 关闭文件映射
        UnmapViewOfFile(pool);
        CloseHandle(hPool);
        for (int i = 0; i < 10; i++) {
            UnmapViewOfFile(shm[i]);
            CloseHandle(hMapFiles[i]);
            CloseHandle(hEvents[i]);
        }
}

// 创建根目录的i结点和初始目录项
void FSManager::createRootDir() {
    int inumber = fileSystem_.create();
    int initRootDirSize = 2 * sizeof(DirItem);
    int ownerID = 0;
    char* data = (char*)malloc(initRootDirSize);
    char* start = data;
    if(data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    // 准备一个DirItem文件名为："."指向自己
    DirItem item = makeDirItem(DIR, ownerID, inumber, RWXR_X, ".");
    appendDirItem(&data, item);
    // 准备一个DirItem文件名为：".."指向自己
    item = makeDirItem(DIR, ownerID, inumber, RWXR_X, "..");
    appendDirItem(&data, item);
    // 最后，将这个数据写入文件中
    auto fileLock = getFileLock(inumber); // 获取锁
    {
        fileLock->increment(); // 增加引用计数
        std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
        fileSystem_.write(inumber, start, initRootDirSize, 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    rootDirInumber_ = inumber;
    free(start);
}

// 将目录项追加到数据块中
void FSManager::appendDirItem(char** data, DirItem& item) {
    int size = sizeof(DirItem);
    memcpy(*data, (const void*)&item, size);
    *data += size;
}

// 创建一个目录项
FSManager::DirItem FSManager::makeDirItem(
        const uint32_t& fileType,
        const uint32_t& ownerID,
        const uint32_t& inumber,
        const uint32_t& filePermission,
        std::string filename) {
    DirItem item{};
    item.FileType = fileType;
    item.FileOwnerID = ownerID;
    item.FilePermission = filePermission;
    item.Inumber = inumber;
    memset(item.FileName, 0, FILENAME_MAX_LENGTH);
    int filenameLen = (filename.length() + 1) >= FILENAME_MAX_LENGTH ? FILENAME_MAX_LENGTH : (filename.length() + 1);
    memcpy(item.FileName, filename.data(), filenameLen);
    return item;
}

// 获取目录项
std::vector<FSManager::DirItem> FSManager::getDirItems(int inumber) {
    // 第一步应该查看目录的大小，以此来判断有多少个目录项
    int size = fileSystem_.stat(inumber);
    int itemNum = size / (sizeof(DirItem));
    // 获取完成之后，就是真正开始从磁盘中读取存储目录项的数据块
    char* data = (char*)malloc(size);
    char* start = data;
    if (data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    auto fileLock = getFileLock(inumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
        fileSystem_.read(inumber, data, size, 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    // 从获取的数据中, 读取目录项数据
    std::vector<DirItem> items;
    for (int i = 0; i < itemNum; i++) {
        DirItem item{};
        memcpy(&item, data, sizeof(DirItem));
        data = data + sizeof(DirItem);
        items.emplace_back(item);
    }
    // 最后，释放内存
    free(start);
    return items;
}

void FSManager::getUserInfo(){
    // 打开/etc/passwd文件，读取用户ID
    auto items = getDirItems(etcDirInumber_);
    for (const auto& item : items) {
        std::unique_lock<std::shared_mutex> lock(userMapMtx); // 独占锁（写锁）
        if (strcmp("passwd", item.FileName) == 0) {
            if (item.FileType == DIR) {
                throw std::runtime_error("getUserPassword: /etc/passwd is a directory\n");
            }
            int size = fileSystem_.stat(item.Inumber);
            char* data = (char*)malloc(size);
            if (data == nullptr) {
                throw std::runtime_error("malloc() failed!\n");
            }
            auto fileLock = getFileLock(item.Inumber); // 获取锁
            {
                fileLock->increment();// 增加引用计数
                std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
                fileSystem_.read(item.Inumber, data, size, 0);
            }
            releaseFileLock(fileLock); // 尝试销毁锁
            std::istringstream stream(data);
            std::string line;
            std::string user, id;
            while (std::getline(stream, line)) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    user = line.substr(0, pos);
                    id = line.substr(pos + 1);
                    userIDMap_[user] = stoi(id);
                }
            }
            free(data);
        }
        if (strcmp("shadow", item.FileName) == 0) {
            if (item.FileType == DIR) {
                throw std::runtime_error("getUserPassword: /etc/shadow is a directory\n");
            }
            int size = fileSystem_.stat(item.Inumber);
            char* data = (char*)malloc(size);
            if (data == nullptr) {
                throw std::runtime_error("malloc() failed!\n");
            }
            auto fileLock = getFileLock(item.Inumber); // 获取锁
            {
                fileLock->increment();// 增加引用计数
                std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
                fileSystem_.read(item.Inumber, data, size, 0);
            }
            releaseFileLock(fileLock); // 尝试销毁锁
            std::istringstream stream(data);
            std::string line;
            std::string user, hash;
            while (std::getline(stream, line)) {
                size_t pos = line.find(':');
                if (pos != std::string::npos) {
                    user = line.substr(0, pos);
                    hash = line.substr(pos + 1);
                    userHashMap_[user] = hash;
                }
            }
            free(data);
        }
    }
}

int FSManager::getUserID(const std:: string& username, bool isNewUser){
    if(isNewUser){
        std::cout << "Creating new user: " << username << std::endl;
        int id = 0;
        for(auto& u : userIDMap_){
            if(u.second > id){
                id = u.second;
            }
        }
        return id+1;
    }
    else{
        std::cout << "Finding user ID for user: " << username << std::endl;
        if(userIDMap_.find(username) == userIDMap_.end()){
            throw std::invalid_argument("User not found in passwd file.");
        }
        return userIDMap_[username];
    }
}


std::string FSManager::getUserPassword(const std:: string& username)
{
    std::shared_lock<std::shared_mutex> lock(userMapMtx);
    if (userHashMap_.find(username) == userHashMap_.end()) {
        throw std::invalid_argument("User not found in shadow file.");
    }
    return userHashMap_[username];
}


void FSManager::writeBackDir(int inumber, std::vector<DirItem>& items) {
    char* data = (char*)malloc(items.size() * sizeof(DirItem));
    char* start = data;
    if (data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    for (auto & item : items) {
        memcpy(data, &item, sizeof(DirItem));
        data += sizeof(DirItem);
    }
    auto fileLock = getFileLock(inumber); // 获取锁
    {
        fileLock->increment(); // 增加引用计数
        std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
        fileSystem_.write(inumber, start, items.size() * sizeof(DirItem), 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    free(start);
}

void FSManager::createHomeDir() {
    // 首先，创建/home目录这个事件，应该是在根目录/下发生的，所以根目录首先需要进行改变
    int homeInumber = fileSystem_.create();
    auto rootItems = getDirItems(rootDirInumber_);
    DirItem item{};
    item = makeDirItem(DIR, 0, homeInumber, RWXR_X, "home");
    rootItems.emplace_back(item);
    writeBackDir(rootDirInumber_, rootItems);
    // 根目录的信息更新完毕，准备/home目录的基本信息
    // "."应指向/home自己
    // ".."应指向其parent目录，也就是根目录
    char* data = (char*)malloc(2 * sizeof(DirItem));
    char* start = data;
    if (data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    item = makeDirItem(DIR, 0, homeInumber, RWXR_X, ".");
    appendDirItem(&data, item);
    item = makeDirItem(DIR, 0, rootDirInumber_, RWXR_X, "..");
    appendDirItem(&data, item);
    auto fileLock = getFileLock(homeInumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
        fileSystem_.write(homeInumber, start, 2 * sizeof(DirItem), 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    homeDirInumber_ = homeInumber;
    free(start);
    /*
     *如果创建失败，那就需要保证根目录原有的数据不改变.这里简单起见先不考虑操作的原子性问题
     */
    // 首先，创建/home目录这个事件，应该是在根目录/下发生的，所以根目录首先需要进行改变
    int etcInumber = fileSystem_.create();
    rootItems = getDirItems(rootDirInumber_);
    item = makeDirItem(DIR, 0, etcInumber, RWXR_X, "etc");
    rootItems.emplace_back(item);
    writeBackDir(rootDirInumber_, rootItems);
    // 根目录的信息更新完毕，准备/home目录的基本信息
    // "."应指向/etc自己
    // ".."应指向其parent目录，也就是根目录
    data = (char*)malloc(2 * sizeof(DirItem));
    start = data;
    if (data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    item = makeDirItem(DIR, 0, etcInumber, RWXR_X, ".");
    appendDirItem(&data, item);
    item = makeDirItem(DIR, 0, rootDirInumber_, RWXR_X, "..");
    appendDirItem(&data, item);
    fileLock = getFileLock(etcInumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
        fileSystem_.write(etcInumber, start, 2 * sizeof(DirItem), 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    etcDirInumber_ = etcInumber;
    free(start);
    /*
     *如果创建失败，那就需要保证根目录原有的数据不改变.这里简单起见先不考虑操作的原子性问题
     */
}

// 初始化Home目录的i结点
void FSManager::getHomeDirInumber(int slot) {
    auto items = getDirItems(rootDirInumber_);
    for(const auto& item : items) {
        if (strcmp("home", item.FileName) == 0) {
            homeDirInumber_ = item.Inumber;
        }
        if (strcmp("etc", item.FileName) == 0) {
            etcDirInumber_ = item.Inumber;
        }
    }
    userInfos_[slot].workDirInumber_ = homeDirInumber_;
}

// 判断某个文件名的目录项是否存在
bool FSManager::isFileExist(std::vector<DirItem>& items, std::string name) {
    for (const auto& item : items) {
        if (name == item.FileName) {
            return true;
        }
    }
    return false;
}

void FSManager::makeDir(int slot,int inumber, std::string name,int userID1,bool init) {
    // 首先，创建/home目录这个事件，应该是在根目录/下发生的，所以根目录首先需要进行改变
    int id;
    if(userID1 == -1) id = getDirOwnerID(inumber);
    else id = userID1;
    int newDirInumber = fileSystem_.create();
    auto parentItems = getDirItems(inumber);
    for(auto& item : parentItems){
        if(strcmp(item.FileName,".") == 0){
            // 有无权限创建目录
            if(!haveWritePermission(slot,item.FilePermission,id)&&!init){
                std::string err;
                err += "Permission refused: can't make directory in <" + std::string(item.FileName) + ">\n";
                strcpy(shm[slot]->result, err.c_str());
                throw std::runtime_error(err.c_str());
            }
            break;
        }
    }
    // 看看有没有同名的文件
    if (isFileExist(parentItems, name)) {
        std::string err;
        err = err + "The file: <" + name + "> is existing!\n";
        std::cout<<err<<std::endl;
        return;
    }
    // 在父目录下创建新目录
    DirItem item{};
    item = makeDirItem(DIR, id, newDirInumber, RWXR_X, name);
    parentItems.emplace_back(item);
    writeBackDir(inumber, parentItems);
    // 根目录的信息更新完毕，准备/home目录的基本信息
    // "."应指向自己
    // ".."应指向其parent目录
    char* data = (char*)malloc(2 * sizeof(DirItem));
    char* start = data;
    if (data == nullptr) {
        throw std::runtime_error("malloc() fail!\n");
    }
    item = makeDirItem(DIR, id, newDirInumber, RWXR_X, ".");
    appendDirItem(&data, item);
    item = makeDirItem(DIR, getDirOwnerID(inumber), inumber, RWXR_X, "..");
    appendDirItem(&data, item);
    auto fileLock = getFileLock(newDirInumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
        fileSystem_.write(newDirInumber, start, 2 * sizeof(DirItem), 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    free(start);
    /*
     *如果创建失败，那就需要保证根目录原有的数据不改变.这里简单起见先不考虑操作的原子性问题
     */
}

void FSManager::registerUser(int slot,const std::string& username, const std::string& password) {

    std::unique_lock<std::shared_mutex> lock(userMapMtx);
    std::string passwd(username),shadow(username);
    std::cout<<0<<std::endl;
    int id = getUserID(username,true);
    std::cout<<1<<std::endl;
    makeDir(slot,homeDirInumber_, username,id, true);
    std::cout<<2<<std::endl;
    passwd += ":" + std::to_string(id)+"\n";
    shadow += ":" + std::to_string(std::hash<std::string>()(password)) + "\n";
    vim2(0,"/etc/passwd",passwd,true,true);
    vim2(0,"/etc/shadow",shadow,true,true);
    std::cout<<3<<std::endl;
    userIDMap_[username] = id;
    userHashMap_[username] = std::to_string(std::hash<std::string>()(password));
    std::cout<<4<<std::endl;
}

void FSManager::unregisterUser(int slot,const std::string& username) {
    // 检查是否为root用户，只有root可以删除用户
    if (userInfos_[slot].userID_ != 0) {
        std::string err = "Permission refused: only root can unregister users\n";
        throw std::runtime_error(err);
    }

    auto items = getDirItems(homeDirInumber_);
    for (auto it = items.begin(); it != items.end(); ++it) {
        if (username == it->FileName) {
            if (it->FileType == DIR) {
                // 递归删除用户目录及其所有内容
                rmRecursive(slot, it->Inumber);
                // 删除用户目录本身
                fileSystem_.remove(it->Inumber);
                // 从父目录中移除目录项
                items.erase(it);
                writeBackDir(homeDirInumber_, items);

                // 从用户映射中删除用户信息
                {
                    std::unique_lock<std::shared_mutex> lock(userMapMtx);
                    userIDMap_.erase(username);
                    userHashMap_.erase(username);
                }

                return;
            }
            else {
                std::string err = "The user directory <" + username + "> is not a directory!\n";
                throw std::runtime_error(err);
            }
        }
    }
    std::string err = "The user <" + username + "> does not exist!\n";
    throw std::runtime_error(err);
}

void FSManager::login(int slot,std:: string username,std:: string  password) {
    // 首先，检查用户名和密码是否正确
    std::string storedPasswordHash = getUserPassword(username);
    if(storedPasswordHash != std::to_string(std::hash<std::string>()(password))){
        // 密码错误
        std::string err;
        err = err + "The password for user: <" + username + "> is incorrect!\n";
        throw std::runtime_error(err.c_str());
        return;
    }
    // 登录成功，更新用户信息
    if(username == "root"){ // root用户直接进入根目录
        userInfos_[slot].workDirInumber_ = homeDirInumber_;
        userInfos_[slot].currentDirPath_ = getCurrentDirPath(slot);
        // userInfos_[slot].username_是char*类型，需要进行内存分配，之后需要释放
        userInfos_[slot].username_ = username;
        userInfos_[slot].userID_ = getUserID(username);
        strcpy(shm[slot]->path, userInfos_[slot].currentDirPath_.c_str());
        strcpy(shm[slot]->name, userInfos_[slot].username_.c_str());
        return;
    }
    auto items = getDirItems(homeDirInumber_);
    for (auto & item : items) {
        if (username == item.FileName) {
            if (item.FileType == DIR) {
                userInfos_[slot].workDirInumber_ = item.Inumber;
                userInfos_[slot].currentDirPath_ = getCurrentDirPath(slot);
                // userInfos_[slot].username_是char*类型，需要进行内存分配，之后需要释放
                userInfos_[slot].username_ = username;
                userInfos_[slot].userID_ = getUserID(username);
                strcpy(shm[slot]->path, userInfos_[slot].currentDirPath_.c_str());
                strcpy(shm[slot]->name, userInfos_[slot].username_.c_str());
                return;
            }
            else {
                std::string err;
                err += + "The user: <" + username + "> isn't existing!\n";
                throw std::runtime_error(err.c_str());
            }
        }
    }
}

void FSManager::ls(int slot) {
    std::ostringstream resultStream; // 用于存储结果
    resultStream << "Permission:\t\tSize:\t\tName:\t\tOwner:\n";
    auto items = getDirItems(userInfos_[slot].workDirInumber_);
    for (const auto& item : items) {
        int fileSize = fileSystem_.stat(item.Inumber);
        std::string name;
        // 查找文件的拥有者名称
        std::shared_lock<std::shared_mutex> lock(userMapMtx);
        for (auto& u : userIDMap_) {
            if (u.second == item.FileOwnerID) {
                name = u.first;
                break;
            }
        }
        // 构造每一行的输出
        resultStream << permissionToString(item.FilePermission, item.FileType) << "\t\t"
                     << fileSize << "\t\t"
                     << item.FileName << "\t\t"
                     << name << "\n";
    }
    // 将结果字符串存储到共享内存的 result 中
    std::string resultStr = resultStream.str();
    if (resultStr.size() >= sizeof(shm[slot]->result)) {
        // 如果结果超出了共享内存的大小，则截断
        strncpy(shm[slot]->result, resultStr.c_str(), sizeof(shm[slot]->result) - 1);
        shm[slot]->result[sizeof(shm[slot]->result) - 1] = '\0'; // 确保字符串以 '\0' 结尾
    } else {
        strcpy(shm[slot]->result, resultStr.c_str()); // 直接拷贝
    }
}

std::string FSManager::permissionToString(uint32_t p, uint32_t type) {
    std::string str;
    switch(type) {
        case DIR:
            str += "d";
            break;
        case NORMAL:
            str += "-";
            break;
        default:
            break;
    }
    std::string temp;
    int count = 0;
    while(p>0){
        if(p%2==0) temp+="-";
        else {
            if(count % 3 == 0) temp+="X";
            else if(count % 3 == 1) temp+="W";
            else temp+="R";
        }
        count++;
        p/=2;
    }
    std::reverse(temp.begin(), temp.end());
    str += temp;
    return str;
}

uint32_t FSManager::StringToPermission(const std::string& str) {
    uint32_t count = 0;
    for(auto i : str){
        count*=2;
        if(i != '-')count+=1;
    }
    return count;
}

void FSManager::mkdir(int slot,std::string filename1) {
    int tInumber;
    std::string filename(std::move(filename1));
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    makeDir(slot,tInumber, filename);
}

uint32_t FSManager::getPathInumber(const std::string& dirname) {
    const std::string& path(dirname);
    std::vector<std::string> dirs;

    // 处理路径分割，按 '/' 分割成各个部分
    std::stringstream ss(path);
    std::string subDir;
    while (std::getline(ss, subDir, '/')) {
        if (!subDir.empty()) {  // 忽略空的目录名（如连续的 '/'）
            dirs.push_back(subDir);
        }
    }
    dirs.pop_back();  // 去掉最后一个操作对象
    int tempDirInumber = rootDirInumber_;

    // 遍历路径的各个部分
    for (const auto& dir : dirs) {
        bool found = false;
        auto items = getDirItems(tempDirInumber);
        for (const auto& item : items) {
            if (strcmp(dir.c_str(), item.FileName) == 0) {
                tempDirInumber = item.Inumber;  // 进入该目录
                found = true;
                break;
            }
        }
        // 如果没有找到该目录，抛出异常
        if (!found) {
            std::string err = "<" + std::string(dir) + "> not found!\n";
            throw std::invalid_argument(err.c_str());
        }
    }
    return tempDirInumber;
}

void FSManager::cd(int slot,std::string dirname) {
    std::string path(std::move(dirname));
    std::vector<std::string> dirs;

    // 处理路径分割，按 '/' 分割成各个部分
    std::stringstream ss(path);
    std::string subDir;
    while (std::getline(ss, subDir, '/')) {
        if (!subDir.empty()) {  // 忽略空的目录名（如连续的 '/'）
            dirs.push_back(subDir);
        }
    }

    // 如果路径是以 '/' 开头，表示绝对路径，从根目录开始
    if (path[0] == '/') {
        userInfos_[slot].workDirInumber_ = rootDirInumber_;  // 从根目录开始
    }

    // 遍历路径的各个部分
    for (const auto& dir : dirs) {
        bool found = false;
        auto items = getDirItems(userInfos_[slot].workDirInumber_);
        for (const auto& item : items) {
            if (strcmp(dir.c_str(), item.FileName) == 0) {
                if (item.FileType == DIR) {
                    userInfos_[slot].workDirInumber_ = item.Inumber;  // 进入该目录
                    found = true;
                    break;
                } else {
                    std::string err = "The file <" + std::string(dir) + "> isn't a directory!\n";
                    throw std::invalid_argument(err.c_str());
                }
            }
        }
        // 如果没有找到该目录，抛出异常
        if (!found) {
            std::string err = "Directory <" + std::string(dir) + "> not found!\n";
            throw std::invalid_argument(err.c_str());
        }
    }
    this->userInfos_[slot].currentDirPath_ = getCurrentDirPath(slot);
    strcpy(shm[slot]->path, this->userInfos_[slot].currentDirPath_.c_str());
}

std::string FSManager::getDirName(int parentInumber, int childInumber) {
    auto items = getDirItems(parentInumber);
    for (const auto& item : items) {
        if (item.Inumber == childInumber) {
            return std::string(item.FileName);
        }
    }
}
int FSManager::getDirOwnerID(int Inumber){
    auto items = getDirItems(Inumber);
    for (const auto& item : items) {
        if (strcmp(item.FileName, ".") == 0) {
            return item.FileOwnerID;
        }
    }
}

void FSManager::pwd(int slot) {
//    int currentDir = userInfos_[slot].workDirInumber_;
//    std::vector<std::string> names;
//    while (currentDir != rootDirInumber_) {
//        auto items = getDirItems(currentDir);
//        int dirInumber = -1;
//        bool foundCurrent = false, foundParent = false;
//        for (const auto& item : items) {
//            if (strcmp(item.FileName, ".") == 0) {
//                dirInumber = item.Inumber;
//                foundCurrent = true;
//            }
//            // 目录本身是不知道自己的名字的，目录的名字存储在parent目录中
//            if (strcmp(item.FileName, "..") == 0) {
//                names.emplace_back(getDirName(item.Inumber, dirInumber));
//                currentDir = item.Inumber;
//                foundParent = true;
//            }
//            if (foundCurrent && foundParent) {
//                break;
//            }
//        }
//    }
//    std::reverse(names.begin(), names.end());
//    std::cout << "/";
//    for (int i = 0 ; i < names.size(); i++) {
//        std::cout << names[i];
//        if (i < names.size() - 1) {
//            std::cout << "/";
//        }
//    }
//    std::cout << std::endl;
    strcpy(shm[slot]->path, getCurrentDirPath(slot).c_str());
}

void FSManager::splitPath(std::string& path){
        size_t lastSlash = path.find_last_of('/'); // 找到最后一个 '/'
        if (lastSlash != std::string::npos) {
            // 保留最后一个斜杠之后的部分
            path = path.substr(lastSlash + 1);
        } else {
            // 如果没有斜杠，不做任何修改
        }
    }

std::string FSManager::getCurrentDirPath(int slot) {
    int currentDir = userInfos_[slot].workDirInumber_;
    std::vector<std::string> names;

    // 走到根目录
    while (currentDir != rootDirInumber_) {
        auto items = getDirItems(currentDir);
        int dirInumber = -1;
        bool foundCurrent = false, foundParent = false;

        for (const auto& item : items) {
            if (strcmp(item.FileName, ".") == 0) {
                dirInumber = item.Inumber;
                foundCurrent = true;
            }
            if (strcmp(item.FileName, "..") == 0) {
                names.emplace_back(getDirName(item.Inumber, dirInumber));
                currentDir = item.Inumber;
                foundParent = true;
            }
            if (foundCurrent && foundParent) {
                break;
            }
        }
    }

    // Reverse the names so that the path is from root to current
    std::reverse(names.begin(), names.end());
    // Build the final path string
    std::string fullPath = "/";
    for (int i = 0; i < names.size(); ++i) {
        fullPath += names[i];
        if (i < names.size() - 1) {
            fullPath += "/";
        }
    }

    // Allocate memory for the final path string (including null terminator)
    return fullPath;
}

// 创建普通文件
void FSManager::touch(int slot,std::string filename,uint32_t permission,bool init) {
    int tInumber;
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);
    // 先看看当前目录下有没有这个文件吧
    if (isFileExist(items, filename)) {
        std::string err;
        err = err + "The file <" + filename + "> is existing!\n";
        throw std::runtime_error(err.c_str());
    }
    int inumber = fileSystem_.create();
    DirItem item{};
    if(init)
    {
        item = makeDirItem(NORMAL, 0, inumber, RW_R__, filename);
    }
    else{
        item = makeDirItem(NORMAL, userInfos_[slot].userID_, inumber, RW_R__, filename);
    }
    items.emplace_back(item);
    writeBackDir(tInumber, items);
}

bool FSManager::haveWritePermission(int slot,uint32_t permission, uint32_t userID) const {
    if(userInfos_[slot].userID_ == 0) return true; // root用户拥有所有权限
    if(userInfos_[slot].userID_ == userID) return (permission & 0b010000) != 0;
    else return (permission & 0b000010) != 0;
}

// 递归删除目录的辅助函数
void FSManager::rmRecursive(int slot, int dirInumber) {
    auto items = getDirItems(dirInumber);

    // 递归删除所有子文件和子目录
    for (auto& item : items) {
        if (strcmp(item.FileName, ".") != 0 && strcmp(item.FileName, "..") != 0) {
            if (item.FileType == DIR) {
                // 递归删除子目录
                rmRecursive(slot, item.Inumber);
            }
            // 删除文件或空目录
            fileSystem_.remove(item.Inumber);
        }
    }
}

void FSManager::rm(int slot,std::string  filename) {
    rm(slot, filename, false); // 默认不强制删除
}

void FSManager::rm(int slot,std::string  filename, bool force) {
    int tInumber;
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);

    // 禁止删除"."和".."目录
    if (filename == "." || filename == "..") {
        std::string err = "System error! Can't remove directory <" + filename + ">\n";
        throw std::runtime_error(err);
    }

    for (auto it = items.begin(); it != items.end(); ++it) {
        if (filename == it->FileName) {
            // 检查是否有删除权限
            if (!haveWritePermission(slot,it->FilePermission,it->FileOwnerID)) {
                std::string err;
                err += "Permission refused: can't remove <" + filename + ">\n";
                throw std::runtime_error(err.c_str());
            }

            // 如果是目录文件，需要递归删除
            if (it->FileType == DIR) {
                bool shouldDelete = force;

                if (!force) {
                    // 通过共享内存请求用户确认
                    std::string promptMsg = "The file <" + filename + "> is a directory, it will also remove all its sub-directory\nDo you really want to remove it?(Y/N): ";
                    strcpy(shm[slot]->prompt, promptMsg.c_str());
                    shm[slot]->needConfirm = true;

                    // 通知shell需要用户输入
                    SetEvent(hEvents[slot]);

                    // 等待用户输入
                    WaitForSingleObject(hEvents[slot], INFINITE);

                    // 检查用户输入
                    std::string answer = shm[slot]->userInput;
                    shouldDelete = (answer == "Y" || answer == "y");
                    shm[slot]->needConfirm = false;
                }

                if (shouldDelete) {
                    // 使用辅助函数递归删除目录内容
                    rmRecursive(slot, it->Inumber);
                    // 删除目录本身
                    fileSystem_.remove(it->Inumber);
                    items.erase(it);
                }
                else {
                    return;
                }
            }
            else {
                // 删除普通文件
                fileSystem_.remove(it->Inumber);
                items.erase(it);
            }
            writeBackDir(tInumber, items);
            return;
        }
    }

    // 如果没有找到文件，抛出错误
    std::string err = "The file <" + filename + "> does not exist!\n";
    throw std::runtime_error(err);
}

// 编辑文件
void FSManager::vim(int slot,std::string  filename, std::string append) {
    // 看看这个文件存不存在
    int tInumber;
    std::string temp(filename);
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);
    int inumber = -1;
    for (auto& item : items) {
        if (filename == item.FileName) {
            if (item.FileType == DIR) {
                throw std::runtime_error("Can't write a directory!\n");
            }
            if (!haveWritePermission(slot,item.FilePermission,item.FileOwnerID)) {
                std::string err;
                err +=  "Permission refused: can't write <" + filename + ">\n";
                throw std::runtime_error(err.c_str());
            }
            inumber = item.Inumber;
        }
    }
    char str[BUFSIZ];
    std::string text;
    while(true) {
        fgets(str, BUFSIZ, stdin);
        if(strncmp(str, "exit", 4) == 0) {
            break;
        }
        int n = strlen(str);
        char* tmp = (char*)malloc(n + 1);
        memset(tmp, 0, n + 1);
        memcpy(tmp, str, n);
        text.append(tmp);
        free(tmp);
        tmp = nullptr;
        memset(str, 0, BUFSIZ);
    }
    text[text.size() - 1] = '\0';
    if (inumber == -1) {
        touch(slot,temp);
        items = getDirItems(tInumber);
        // 这里其实可以直接找最后一个元素，因为是刚插入的新文件
        for (auto& item : items) {
            if (filename == item.FileName) {
                inumber = item.Inumber;
                break;
            }
        }
    }
    char* data = (char*)malloc(text.size());
    if (data == nullptr) {
        throw std::runtime_error("malloc() failed!\n");
    }
    try {
        // 在这里读取的时候完全是没有问题的。
        memcpy(data, text.c_str(), text.size());
        auto fileLock = getFileLock(inumber); // 获取锁
        {
            fileLock->increment();// 增加引用计数
            std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
            if (append == "append") {
                size_t currentSize = fileSystem_.stat(inumber);            // 获取文件当前大小
                fileSystem_.write(inumber, data, text.size(), currentSize);// 从文件末尾开始写入
            } else {
                fileSystem_.write(inumber, data, text.size(), 0);
            }
        }
        releaseFileLock(fileLock); // 尝试销毁锁
        free(data);
    }
    catch(const std::exception& e) {
        if (data != nullptr) {
            free(data);
        }
        throw e;
    }
}

void FSManager::vim2(int slot,std::string filename, const std::string& text,bool append,bool init){
    // 看看这个文件存不存在
    int tInumber;
    std::string temp(filename);
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);
    int inumber = -1;
    for (auto& item : items) {
        if (filename == item.FileName) {
            if (item.FileType == DIR) {
                throw std::runtime_error("Can't write a directory!\n");
            }
            if (!haveWritePermission(slot,item.FilePermission,item.FileOwnerID)&&!init) {
                std::string err;
                err +=  "Permission refused: can't write <" + filename + ">\n";
                throw std::runtime_error(err.c_str());
            }
            inumber = item.Inumber;
        }
    }
    if (inumber == -1) {
        touch(slot,temp);
        items = getDirItems(tInumber);
        // 这里其实可以直接找最后一个元素，因为是刚插入的新文件
        for (auto& item : items) {
            if (filename == item.FileName) {
                inumber = item.Inumber;
                break;
            }
        }
    }
    char* data = (char*)malloc(text.size());
    if (data == nullptr) {
        throw std::runtime_error("malloc() failed!\n");
    }
    try {
        // 在这里读取的时候完全是没有问题的。
        memcpy(data, text.c_str(), text.size());
        auto fileLock = getFileLock(inumber); // 获取锁
        {
            fileLock->increment();// 增加引用计数
            std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
            if (append) {
                size_t currentSize = fileSystem_.stat(inumber);            // 获取文件当前大小
                fileSystem_.write(inumber, data, text.size(), currentSize);// 从文件末尾开始写入
            } else {
                fileSystem_.write(inumber, data, text.size(), 0);
            }
        }
        releaseFileLock(fileLock); // 尝试销毁锁
        free(data);
    }
    catch(const std::exception& e) {
        if (data != nullptr) {
            free(data);
        }
        throw e;
    }
}


void FSManager::cat(int slot,std::string filename) {
    int tInumber;
    if (filename[0] == '/') {
        tInumber = getPathInumber(filename);
        splitPath(filename);
    } else {
        tInumber = userInfos_[slot].workDirInumber_;
    }

    auto items = getDirItems(tInumber);
    for (const auto& item : items) {
        if (filename == item.FileName) {
            if (!haveReadPermission(slot,item.FilePermission, item.FileOwnerID)) {
                throw std::runtime_error("Permission refused: can't read <" + filename + ">\n");
                strcpy(shm[slot]->result, std::string("Permission refused: can't read <" + filename + ">\n").c_str());
            }
            if (item.FileType == DIR) {
                throw std::runtime_error("cat: " + filename + ": is a directory\n");
            }

            int size = fileSystem_.stat(item.Inumber);
            char* data = (char*)malloc(size);
            if (data == nullptr) {
                throw std::runtime_error("malloc() failed!\n");
            }

            try {
                auto fileLock = getFileLock(item.Inumber); // 获取锁
                {
                    fileLock->increment();// 增加引用计数
                    std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
                    fileSystem_.read(item.Inumber, data, size, 0);
                }
                releaseFileLock(fileLock); // 尝试销毁锁
                // 使用 std::ostringstream 来构建文件内容字符串
                std::ostringstream resultStream;
                for (int i = 0; i < size; ++i) {
                    if (data[i] == '\0') {
                        break;
                    } else {
                        resultStream << data[i]; // 显示字符
                    }
                }
                resultStream << std::endl; // 输出换行符
                std::string resultStr = resultStream.str();

                // 确保不会超出共享内存的大小
                if (resultStr.size() >= sizeof(shm[slot]->result)) {
                    strncpy(shm[slot]->result, resultStr.c_str(), sizeof(shm[slot]->result) - 1);
                    shm[slot]->result[sizeof(shm[slot]->result) - 1] = '\0'; // 确保字符串以 '\0' 结尾
                } else {
                    strcpy(shm[slot]->result, resultStr.c_str()); // 直接拷贝
                }

                free(data);
            } catch (const std::exception& e) {
                free(data);
                throw e;
            }
        }
    }
}

// 将内部文件系统infile的内容拷贝到内部文件系统outfile中
void FSManager::copy(int slot,std::string infile, std::string outfile){
    int tInumber;
    if(infile[0] == '/') {tInumber = getPathInumber(infile);splitPath(infile);}
    else tInumber = userInfos_[slot].workDirInumber_;
    // 查找源文件infile
    auto items = getDirItems(tInumber);
    int inumber = -1;
    for (const auto& item : items) {
        if (infile == item.FileName) {
            if (item.FileType == DIR) {
                throw std::runtime_error("The directory " + infile + " can't be written to!\n");
            }
            // 检查是否有读权限
            if (!haveReadPermission(slot,item.FilePermission,item.FileOwnerID)) {
                throw std::runtime_error("Permission refused: can't read <" + infile + ">\n");
            }
            inumber = item.Inumber;
            break;
        }
    }
    if (inumber == -1) { // 源文件不存在
        throw std::runtime_error("The file <" + infile + "> does not exist!\n");
    }
    // 2. 获取源文件的大小
    int size = fileSystem_.stat(inumber);
    char* data = (char*)malloc(size);
    if (data == nullptr) {
        throw std::runtime_error("malloc() failed!\n");
    }
    // 3. 读取源文件的内容
    auto fileLock = getFileLock(inumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
        fileSystem_.read(inumber, data, size, 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁
    // 以十六进制显示内部文件系统读取的原始字节数据
    for (int i = 0; i < size; ++i) {
        printf("%02x ", static_cast<unsigned char>(data[i]));
    }
    printf("\n");

    // 4. 查找目标文件(outfile)
    std::string temp(outfile);
    if(outfile[0] == '/') {tInumber = getPathInumber(outfile);splitPath(outfile);}
    else tInumber = userInfos_[slot].workDirInumber_;
    items = getDirItems(tInumber);
    int outInumber = -1;
    for (auto& item : items) {
        if (outfile == item.FileName) {
            if (item.FileType == DIR) {
                throw std::runtime_error("The directory " + outfile + " can't be written to!\n");
            }
            // 检查是否有写权限
            if (!haveWritePermission(slot,item.FilePermission,item.FileOwnerID)) {
                throw std::runtime_error("Permission refused: can't write <" + outfile + ">\n");
            }
            outInumber = item.Inumber;
            break;
        }
    }
    // 5. 如果目标文件不存在，则创建目标文件
    if (outInumber == -1) {
        touch(slot,temp);
        items = getDirItems(tInumber);
        // 查找新创建的目标文件
        for (auto& item : items) {
            if (outfile == item.FileName) {
                outInumber = item.Inumber;
                break;
            }
        }
    }
    // 6. 将源文件内容写入目标文件
    try {
        auto fileLock = getFileLock(inumber); // 获取锁
        fileLock->increment(); // 增加引用计数
        {
            std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
            fileSystem_.write(outInumber, data, size, 0);
        }
        releaseFileLock(fileLock); // 尝试销毁锁
        free(data);  // 释放资源
    }
    catch(const std::exception& e) {
        free(data);
        throw e;
    }
}

// 将外部文件系统infile的内容拷贝到内部文件系统outfile中
void FSManager::copyin(int slot,std::string infile, std::string outfile) {
    FILE *stream = fopen(infile.c_str(), "rb"); // 使用 "rb" 模式以二进制方式打开
    if (stream == nullptr) {
        std::string err = "Unable to open " + std::string(infile) + ": " + strerror(errno) + "\n";
        throw std::runtime_error(err);
    }

    std::string text;
    char buf[BUFSIZ] = {0};
    while (true) {
        int readNum = fread(buf, 1, sizeof(buf), stream);
        if (readNum <= 0) {
            break;
        }
        // 输出读取的字节数据，以十六进制查看每个字节内容
        for (int i = 0; i < readNum; ++i) {
            printf("%02x ", static_cast<unsigned char>(buf[i]));
        }
        printf("\n");

        text.append(buf, readNum); // 直接追加读取的内容
        memset(buf, 0, BUFSIZ);
    }
    fclose(stream);
    int tInumber;
    std::string temp(outfile);
    if(outfile[0] == '/') {tInumber = getPathInumber(outfile);splitPath(outfile);}
    else tInumber = userInfos_[slot].workDirInumber_;
    // 以下部分保持不变，进行文件权限检查和文件创建
    auto items = getDirItems(tInumber);
    int inumber = -1;
    for (auto& item : items) {
        if (outfile == item.FileName) {
            if (item.FileType == DIR) {
                throw std::runtime_error("The directory " + outfile+ " can't be written to!\n");
            }
            if (!haveWritePermission(slot,item.FilePermission,item.FileOwnerID)) {
                throw std::runtime_error("Permission refused: can't write <" + outfile + ">\n");
            }
            inumber = item.Inumber;
            break;
        }
    }
    if (inumber == -1) {
        touch(slot,temp);
        items = getDirItems(tInumber);
        for (auto& item : items) {
            if (outfile == item.FileName) {
                inumber = item.Inumber;
                break;
            }
        }
    }

    // 写入文件内容到内部文件系统
    try {
        auto fileLock = getFileLock(inumber); // 获取锁
        fileLock->increment(); // 增加引用计数
        {
            std::unique_lock<WritePrioritizedLock> writeLock(fileLock->lock);
            fileSystem_.write(inumber, text.data(), text.size(), 0);
        }
        releaseFileLock(fileLock); // 尝试销毁锁
    }
    catch(const std::exception& e) {
        throw e;
    }
}


// 将内部infile的内容拷贝到外部文件系统outfile中
void FSManager::copyout(int slot,std::string infile, std::string outfile) {
    int tInumber;
    if(infile[0] == '/') {tInumber = getPathInumber(infile);splitPath(infile);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);
    int inumber = -1;
    for (const auto& item : items) {
        if (infile == item.FileName) {
            inumber = item.Inumber;
            break;
        }
    }
    if (inumber == -1) {
        throw std::runtime_error("The file <" + infile + "> does not exist!\n");
    }

    int size = fileSystem_.stat(inumber);
    char* data = (char*)malloc(size);
    if (data == nullptr) {
        throw std::runtime_error("malloc() failed!\n");
    }
    auto fileLock = getFileLock(inumber); // 获取锁
    {
        fileLock->increment();// 增加引用计数
        std::shared_lock<WritePrioritizedLock> readLock(fileLock->lock);
        fileSystem_.read(inumber, data, size, 0);
    }
    releaseFileLock(fileLock); // 尝试销毁锁

    // 以十六进制显示内部文件系统读取的原始字节数据
    for (int i = 0; i < size; ++i) {
        printf("%02x ", static_cast<unsigned char>(data[i]));
    }
    printf("\n");

    FILE *stream = fopen(outfile.c_str(), "wb"); // 使用 "wb" 模式以二进制方式写入
    fwrite(data, 1, size, stream);
    fclose(stream);
    free(data);
}


// 检查是否有执行权限
bool FSManager::haveExePermission(int slot,uint32_t permission, uint32_t userID) const {
    if(userInfos_[slot].userID_ == 0) return true; // root用户拥有所有权限
    if(userInfos_[slot].userID_ == userID) return (permission & 0b001000) != 0;
    else return (permission & 0b000001) != 0;
}

// 执行文件
void FSManager::exec(int slot,std::string filename) {
    int tInumber;
    std::string temp{filename};
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    auto items = getDirItems(tInumber);
    DirItem targetItem{};
    bool found = false;
    for (auto& item : items) {
        if (item.FileName == filename) {
            targetItem = item;
            found = true;
        }
    }
    if (!found) {
        std::string err;
        err = err + "The file <" + filename + "> isn't existing!\n";
        throw std::runtime_error(err);
    }
    if (!haveExePermission(slot,targetItem.FilePermission,targetItem.FileOwnerID)) {
        std::string err;
        err = err + "Permission refused: can't execute <" + filename+ ">\n";
        throw std::runtime_error(err);
    }
    if (targetItem.FileType == DIR) {
        cd(slot,temp);
    }
    else {
        std::ostringstream resultStream;
        resultStream << "Execute " << filename << std::endl;
        std::string resultStr = resultStream.str();

        // 确保不会超出共享内存的大小
        if (resultStr.size() >= sizeof(shm[slot]->result)) {
            strncpy(shm[slot]->result, resultStr.c_str(), sizeof(shm[slot]->result) - 1);
            shm[slot]->result[sizeof(shm[slot]->result) - 1] = '\0'; // 确保字符串以 '\0' 结尾
        } else {
            strcpy(shm[slot]->result, resultStr.c_str()); // 直接拷贝
        }
    }
}

// 修改文件权限
void FSManager::chmod(int slot,std::string mod, std::string  filename) {
    int tInumber;
    if(filename[0] == '/') {tInumber = getPathInumber(filename);splitPath(filename);}
    else tInumber = userInfos_[slot].workDirInumber_;
    uint32_t permission = StringToPermission(mod);
    if (permission < NUL || permission > RWXRWX) {
        std::string err;
        err = err + mod + " is invalid!\n";
        throw std::invalid_argument(err);
    }
    auto items = getDirItems(tInumber);
    for (auto& item : items) {
        if (filename == item.FileName) {
            item.FilePermission = permission;
            break;
        }
    }
    writeBackDir(tInumber, items);
}

void FSManager::info(int slot) {
    std::string resultStr = this->fileSystem_.debug();
    resultStr+="Current user: "+userInfos_[slot].username_+"\n";
    // 确保不会超出共享内存的大小
    if (resultStr.size() >= sizeof(shm[slot]->result)) {
        strncpy(shm[slot]->result, resultStr.c_str(), sizeof(shm[slot]->result) - 1);
        shm[slot]->result[sizeof(shm[slot]->result) - 1] = '\0'; // 确保字符串以 '\0' 结尾
    } else {
        strcpy(shm[slot]->result, resultStr.c_str()); // 直接拷贝
    }
}

void FSManager::check(int slot){
    // 加载并检查超级块

    this->fileSystem_.check();
}

// 是否有读权限
bool FSManager::haveReadPermission(int slot,uint32_t permission, uint32_t userID) const {
    if(userInfos_[slot].userID_ == 0) return true; // root用户拥有所有权限
    if(userInfos_[slot].userID_ == userID) return (permission & 0b100000) != 0;
    else return (permission & 0b000100) != 0;
}

// 获得i结点的读写锁
std::shared_ptr<LockWrapper> FSManager::getFileLock(int inumber) {
    std::lock_guard<std::mutex> mapLock(fileLocksMutex_); // 保护 fileLocks_
    auto it = fileLocks_.find(inumber);
    if (it == fileLocks_.end()) {
        auto newLock = std::make_shared<LockWrapper>();
        fileLocks_[inumber] = newLock;
        return newLock;
    }
    return it->second;
}

// 释放i结点的读写锁
void FSManager::releaseFileLock(const std::shared_ptr<LockWrapper>& lock) {
    std::lock_guard<std::mutex> mapLock(fileLocksMutex_); // 保护 fileLocks_
    lock->decrement(); // 减少引用计数
    if (lock->isUnused()) {
        // 遍历删除 fileLocks_ 中对应的锁
        for (auto it = fileLocks_.begin(); it != fileLocks_.end();) {
            if (it->second == lock) {
                it = fileLocks_.erase(it);
                std::cout << "Lock released and destroyed" << std::endl;
            } else {
                ++it;
            }
        }
    }
}
