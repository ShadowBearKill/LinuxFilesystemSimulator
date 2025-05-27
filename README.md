# Linux-FileSystem-Simulator
基于C++实现的，运行在window平台的，前后端分离，多用户Linux模拟文件系统

#### 使用说明：
1. 先运行文件系统，再运行shell
2. 使用管理员运行
3. 默认有一个用户为 root，密码为 123

#### 功能：
1. 注册，格式为register username password
2. 删除用户，格式为unregister username
3. 登录，格式为login username password
4. 显示当前目录，格式为pwd（虽然终端会自动加上pwd，但还是建议加上）
5. 切换目录，格式为cd path（path为相对路径或绝对路径）
6. 显示当前目录下的文件，格式为ls
7. 创建文件，格式为touch filename（注意文件名不能有空格）
8. 删除文件，格式为rm filename（注意文件名不能有空格）
9. 创建文件夹，格式为mkdir dirname（注意文件夹名不能有空格）
10. 显示文件内容，格式为cat filename（注意文件名不能有空格）
11. 编辑文件内容，格式为vim filename content（注意文件名不能有空格）模式为w，即覆盖原内容
12. 追加文件内容，格式为append filename content（注意文件名不能有空格）,模式为a，即在原内容后追加
13. 执行文件，格式为exec filename（注意文件名不能有空格）（并不能真的执行==）
14. 复制，将外部文件系统infile的内容拷贝到内部文件系统outfile中，格式为copyin infile outfile(字节级复制)
15. 复制，将内部infile的内容拷贝到外部文件系统outfile中，格式为copyout infile outfile(字节级复制)
16. 修改文件权限 chmod mode filename （本系统的权限为6位的RWXRWX编码）
17. 显示帮助信息，格式为help，
18. 退出系统，格式为exit
