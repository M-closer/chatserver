#include "json.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <vector>
#include <chrono>
#include <ctime>
using namespace std;
using json = nlohmann::json;

//基于linux的TCP 
#include<unistd.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

//记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 控制主菜单聊天页面程序
bool isMainMenuRunning = false;

//接受线程
void readTaskHandler(int clientfd);
// 获取系统时间
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int clientfd);

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接受线程
int main(int argc,char **argv)
{
    if(argc < 3)
    {
        cerr <<"command invalid! example: ./ChatClient 127.0.0.1 6000"<<endl;
        exit(-1);
    }

    //通过解析命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    //创建client端的socket
    int clientfd = socket(AF_INET,SOCK_STREAM,0);
    if(-1 == clientfd)
    {
        cerr <<"socket create error"<<endl;
        exit(-1);
    }

    //填写client需要连接的server信息的ip+port
    sockaddr_in server;
    memset(&server,0,sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    //client和server进行链接
    if(-1 == connect(clientfd,(sockaddr *)&server,sizeof(sockaddr_in)))
    {
        cerr<<" connect server error" <<endl;
        close(clientfd);
        exit(-1);
    }

    //main线程用于接受用户输入，负责发送数据
    while(1)
    {
        //显示首页菜单 登录，注册，退出
        cout<<"====================="<<endl;
        cout<<"1.login"<<endl;
        cout<<"2.register"<<endl;
        cout<<"3.quit"<<endl;
        cout<<"====================="<<endl;
        cout<<"choice:";
        int choice = 0;
        cin>>choice; 
        cin.get();//读掉缓冲区残留的回车

        switch(choice)
        {
            case 1:
            {
                int id = 0;
                char pwd[50] = {0};
                cout<<"userid: ";
                cin>>id;
                cin.get(); //读掉缓冲区残留的回车
                cout<<"userpassword:";
                cin.getline(pwd,50);

                json js;
                js["msgId"] = LOGIN_MSG;
                js["id"] = id;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(-1 == len)
                {
                    cerr<<"send login msg error: "<<request<<endl;
                }
                else
                {
                    char buffer[1024] = {0};
                    len = recv(clientfd,buffer,1024,0);
                    if(-1 == len) //登录失败
                    {
                        cerr<<"recv login response error" <<endl;
                    }
                    else //登录成功
                    {
                        json responsejs = json::parse(buffer);
                        if(0!=responsejs["errno"].get<int>()) //密码错误或已登录
                        {
                            cerr <<responsejs["errmsg"]<<endl;
                        }
                        else
                        {
                            //记录当前用户的id和name
                            g_currentUser.setId(responsejs["id"].get<int>());
                            g_currentUser.setName(responsejs["name"]);

                            //记录当前用户的好友列表信息
                            if(responsejs.contains("friends"))
                            {
                                //初始化
                                g_currentUserFriendList.clear();

                                vector<string> vec = responsejs["friends"];
                                for(string &str : vec)
                                {
                                    json js = json::parse(str);
                                    User user;
                                    user.setId(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    g_currentUserFriendList.push_back(user);
                                }
                            }

                            //记录当前用户的群组列表消息
                            if(responsejs.contains("groups"))
                            {
                                g_currentUserGroupList.clear();

                               vector<string> vec3 = responsejs["groups"];
                               for(string &str:vec3)
                               {
                                    json js = json::parse(str);
                                    Group group;
                                    group.setId(js["id"].get<int>());
                                    group.setName(js["groupname"]);
                                    group.setDesc(js["groupdesc"]);
                                    
                                    vector<string> vec4 = js["users"];
                                    for(string &userstr : vec4)
                                    {
                                        GroupUser user;
                                        json grpjs = json::parse(userstr);
                                        user.setId(grpjs["id"]);
                                        user.setName(grpjs["name"]);
                                        user.setState(grpjs["state"]);
                                        user.setRole(grpjs["role"]);
                                        group.getUsers().push_back(user);
                                    }
                                    g_currentUserGroupList.push_back(group);
                               }                                 
                            }
                            
                            //显示登录用户的基本信息
                            showCurrentUserData();

                            //记录是否有离线消息
                            if(responsejs.contains("offlinemsg"))
                            {
                                vector<string> vec2 = responsejs["offlinemsg"];
                                for(string &str : vec2)
                                {
                                    json js = json::parse(str);
                                    if(ONE_CHAT_MSG == js["msgId"].get<int>())
                                    {
                                        cout<<js["time"].get<string>() << "[" <<js["id"]<<"]"<<js["name"].get<string>()
                                            <<" said: "<<js["msg"].get<string>() <<endl;
                                        continue;
                                    } 
                                    else
                                    {
                                        cout<<"群消息[" <<js["groupid"]<<"]:"<<js["time"].get<string>() << "[" <<js["id"]<<"]"<<js["name"].get<string>()
                                            <<" said: "<<js["msg"].get<string>() <<endl;
                                        continue;
                                    }
                                }
                            }

                            //登录成功，启动接受线程负责接受数据，该线程只启动一次
                            static int readthreadnumber = 0;
                            if(readthreadnumber == 0)
                            {
                                std::thread readTask(readTaskHandler,clientfd); //pthread_create
                                readTask.detach();
                                readthreadnumber++;
                            }
                            //进入聊天主菜单界面
                            isMainMenuRunning = true;
                            mainMenu(clientfd);
                        }
                    }
                }
                break;
            }
            case 2:
            {
                char name[50]={0};
                char pwd[50] = {0};
                cout<<"username:"<<endl;
                cin.getline(name,50);
                cout<<"userpassword:"<<endl;
                cin.getline(pwd,50);

                json js;
                js["msgId"] = REG_MSG;
                js["name"] = name;
                js["password"] = pwd;
                string request = js.dump();

                int len = send(clientfd,request.c_str(),strlen(request.c_str())+1,0);
                if(len == -1)
                {
                    cerr<<"send reg msg error: "<<request<<endl;
                }
                else
                {
                    char buffer[1024]={0};
                    len = recv(clientfd,buffer,1024,0);
                    if(-1 == len)
                    {
                        cerr<<"recv reg response error"<<endl;
                    }
                    else
                    {
                        json responsejs = json::parse(buffer);//反序列化
                        if(0 != responsejs["errno"].get<int>())
                        {
                            cerr<<name<<"is already exist, register error"<<endl;
                        }
                        else
                        {
                            cout <<name <<"register success,userid is "<<responsejs["id"]
                                <<", do not forget it!"<<endl;
                        }
                    }
                }
                break;
            }
            case 3://quit业务
            {
                close(clientfd);
                exit(0);
            }
            default:
            {
                cerr<<"invalid input!"<<endl;
                break;
            }
        }
    }

}


// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout<<"======================login user===================="<<endl;
    cout<<"current login user => id:"<<g_currentUser.getId()<<"name: "<<g_currentUser.getName();
    cout<<"----------------------friend list--------------------"<<endl;
    if(!g_currentUserFriendList.empty())
    {
        for(User &user : g_currentUserFriendList)
        {
            cout<<user.getId()<<" "<<user.getName()<<" "<<user.getState()<<endl;
        }
    }
    cout<<"----------------------group list--------------------"<<endl;
    if(!g_currentUserGroupList.empty())
    {
        for(Group &group : g_currentUserGroupList)
        {
            cout<<group.getId()<<" "<<group.getName()<<" : "<<group.getDesc()<<endl;
            for(GroupUser &user : group.getUsers())
            {
                cout<<user.getId()<<" "<<user.getName()<<" "<<user.getState()
                    <<" "<<user.getRole()<<endl;
            }
        }
    }
    cout<<"================================"<<endl;
}

//接受线程
void readTaskHandler(int clientfd)
{
    while(1)
    {
        char buffer[1024] = {0};
        int len = recv(clientfd,buffer,1024,0); //阻塞
        if(len<=0)
        {
            close(clientfd);
            exit(-1);
        }

        // 接受ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgId"].get<int>();
        if(ONE_CHAT_MSG == msgtype)
        {
            cout<<js["time"].get<string>() << "[" <<js["id"]<<"]"<<js["name"].get<string>()
                <<" said: "<<js["msg"].get<string>() <<endl;
            continue;
        } 
        
        if(GROUP_CHAT_MSG == msgtype)
        {
            cout<<"群消息[" <<js["groupid"]<<"]:"<<js["time"].get<string>() << "[" <<js["id"]<<"]"<<js["name"].get<string>()
                <<" said: "<<js["msg"].get<string>() <<endl;
            continue;
        }
    }
}

void help(int fd = 0,string str = "");

void chat(int,string);

void addfriend(int,string);

void creatgroup(int,string);

void addgroup(int,string);

void groupchat(int,string);

void quit(int,string);

//系统支持的客户端命令列表
unordered_map<string,string> commandMap = {
    {"help","显示所有支持的命令,格式help"},
    {"char","一对一聊天"},
    {"addfriend","添加好友"},
    {"creategroup","创建群组"},
    {"addgroup","加入群组"},
    {"groupchat","群聊"},
    {"quit","注销"}
};

//系统支持的客户端命令处理
unordered_map<string,function<void(int,string)>> commandHandlerMap = {
    {"help",help},
    {"chat",chat},
    {"addfriend",addfriend},
    {"addgroup",addgroup},
    {"creatgroup",creatgroup},
    {"groupchat",groupchat},
    {"quit",quit}
};

//主聊天页面程序
void mainMenu(int clientfd)
{
    help();
    
    char buffer[1024] = {0};
    while(isMainMenuRunning)
    {
        cin.getline(buffer,1024);
        string commandbuf(buffer);
        string command; //存储命令
        int idx = commandbuf.find(":");//冒号的位置
        if(-1 == idx)
        {
            command = commandbuf;
        }
        else
        {
            command = commandbuf.substr(0,idx); //找到命令了
        }
        auto it = commandHandlerMap.find(command);
        if(it == commandHandlerMap.end())
        {
            cerr<<"invalid input command!"<<endl;
            continue;
        }

        //调用相应命令的时间回调，mainMenu对修改封闭
        it->second(clientfd,commandbuf.substr(idx+1,commandbuf.size()-idx));
    }
}

void help(int,string)
{
    cout<<"show command list >>> "<<endl;
    for(auto &p : commandMap)
    {
        cout<< p.first<<":"<<p.second<<endl;
    }
    cout<<endl;
}

void addfriend(int clientfd,string str)
{
    int friendid = atoi(str.c_str());
    json js;
    js["msgId"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId();
    js["friendid"] = friendid;
    string buffer = js.dump();

    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1 == len)
    {
        cerr <<"send addfriend msg error -> "<<buffer<<endl;
    }
}

void chat(int clientfd,string str)
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr<<"chat command invalid!"<<endl;
        return;
    }

    int friendid = atoi(str.substr(0,idx).c_str());
    string message = str.substr(idx+1,str.size() - idx);

    json js;
    js["msgId"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["to"] = friendid;
    js["time"] = getCurrentTime();
    js["msg"] = message;
    string buffer = js.dump();

    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1 == len)
    {
        cerr<<"send chat msg error -> "<<buffer<<endl;
    }
}

void creatgroup(int clientfd,string str)
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr<<"creatGroup command invalid!"<<endl;
        return;
    }

    string groupname = str.substr(0,idx);
    string groupdesc = str.substr(idx+1,str.size()-idx);

    json js;
    js["msgId"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupname"] = groupname;
    js["groupdesc"] = groupdesc;
    
    string buffer = js.dump();
    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1 == len)
    {
        cerr << "send chat msg error -> "<<buffer<<endl;
    }
}

void addgroup(int clientfd,string str)
{
    int groupid = atoi(str.c_str());
    json js;
    js["msgId"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;

    string buffer = js.dump();
    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1 == len)
    {
        cerr << "send chat msg error -> "<<buffer<<endl;
    }
}

//groupid:message
void groupchat(int clientfd,string str)
{
    int idx = str.find(":");
    if(-1 == idx)
    {
        cerr<<"creatGroup command invalid!"<<endl;
        return;
    }

    int groupid = atoi(str.substr(0,idx).c_str());
    string message = str.substr(idx,str.size()-idx);

    json js;
    js["msgId"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId(); 
    js["groupid"] = groupid;
    js["name"] = g_currentUser.getName();
    js["msg"] = message;
    js["time"] = getCurrentTime();
    string buffer = js.dump();

    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str())+1,0);
    if(-1 == len)
    {
        cerr << "send chat msg error -> "<<buffer<<endl;
    }
}

void quit(int clientfd,string str)
{
    json js;
    js["msgId"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId();
    string buffer = js.dump();

    int len = send(clientfd,buffer.c_str(),strlen(buffer.c_str()),0);
    if(-1 == len)
    {
        cerr << "send chat msg error -> "<<buffer<<endl;
    }
    else
    {
        isMainMenuRunning = false;
    }
}

string getCurrentTime()
{

}