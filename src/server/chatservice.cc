#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h>
#include<vector>
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    _MsgHandlerMap.insert({LOGIN_MSG, bind(&ChatService::login, this, _1, _2, _3)});
    _MsgHandlerMap.insert({REG_MSG, bind(&ChatService::reg, this, _1, _2, _3)});
    _MsgHandlerMap.insert({ONE_CHAT_MSG, bind(&ChatService::oneChat, this, _1, _2, _3)});
    _MsgHandlerMap.insert({ADD_FRIEND_MSG,bind(&ChatService::addFriend, this, _1, _2, _3)});
    _MsgHandlerMap.insert({CREATE_GROUP_MSG,bind(&ChatService::createGroup,this,_1,_2,_3)});
    _MsgHandlerMap.insert({ADD_GROUP_MSG,bind(&ChatService::addGroup,this,_1,_2,_3)});
    _MsgHandlerMap.insert({GROUP_CHAT_MSG,bind(&ChatService::groupChat,this,_1,_2,_3)});
    _MsgHandlerMap.insert({LOGINOUT_MSG,bind(&ChatService::loginout,this,_1,_2,_3)}); 

    //连接redis服务器
    if(_redis.connect())
    {
        //设置上报消息的回调
        _redis.init_notify_handler(bind(&ChatService::handleRedisSubscribeMessage,this,_1,_2));
    }
}

msgHandler ChatService::getHandler(int msgId)
{
    // 记录错误日志，msgid没有对应的事件处理回调
    auto it = _MsgHandlerMap.find(msgId);
    if (it == _MsgHandlerMap.end())
    {
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            LOG_ERROR << "msgid:" << msgId << "can not find handler!";
        };
    }
    else
        return _MsgHandlerMap[msgId];
}

// 服务器异常，业务重置方法
void ChatService::reset() 
{
    // 把online状态的用户，设置为offline
    _userModel.resetState();
}


void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if(it != _userConnMap.end())
        {
            _userConnMap.erase(userid);
        }
    }

    //用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid);

    //更新用户的状态信息
    User user(userid,"","","offline");
    _userModel.updateState(user);
}


// 处理登录业务 id ,name ,password
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    // if (user.getId() == id && user.getPwd() == pwd)
    if (user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录，不准重复登录
            json response;
            response["msgId"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该账号已经登录";
            conn->send(response.dump());
        }
        else
        {
            // 登录成功，记录用户连接信息
            {
                lock_guard<mutex> lock(_connMutex);
                _userConnMap.insert({id, conn});
            }

            // id用户登录成功后，向redis订阅channel()
            _redis.subscribe(id);

            // 登录成功，更新用户状态信息
            user.setState("online");
            _userModel.updateState(user);

            // 每个线程栈都是隔离的，都是自己的，不需要加锁
            json response;
            response["msgId"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            response["time"] = time.toString();
            //查询该用 户是否有离线消息
            vector<string> vec = _offlinemsgmodel.query(id);
            if(!vec.empty())
            {
                response["offlinemsg"] = vec;
                //读取该用户的离线消息后,把该用户的所有离线消息删除掉
                _offlinemsgmodel.remove(id);
            }

            //查询用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if(!userVec.empty()) 
            {
                vector<string> vec2;
                for(User& user:userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }
                response["friends"] = vec2;
            }

            //查询用户的群组信息
            vector<Group> groupuserVec =  _groupmodel.queryGroups(id);
            if(!groupuserVec.empty())
            {
                vector<string> groupV;
                for(Group &group : groupuserVec)
                {
                    json grpjs;
                    grpjs["id"] = group.getId();
                    grpjs["groupname"] = group.getName();
                    grpjs["groupdesc"] = group.getDesc(); 
                    vector<string> userV;
                    for(GroupUser &user:group.getUsers())
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjs["users"] = userV;
                    groupV.push_back(grpjs.dump());
                }
                response["groups"] = groupV;
            }

            conn->send(response.dump());
        }
    }
    else
    {
        // 该用户不存在，用户存在但是密码错误，登录失败
        json response;
        response["msgId"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或者密码错误";
        conn->send(response.dump());
    }
}

//一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn,json &js,Timestamp time)
{
    int toid = js["to"].get<int>();
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if(it != _userConnMap.end())
        {
            //toid在线，转发消息
            it->second->send(js.dump());
            return;
        }
    }

    //查询toid是否在线
    User user = _userModel.query(toid);
    if(user.getState() == "online")
    {
        _redis.publish(toid,js.dump());
        return;
    }

    //toid不在线，存储离线消息
    _offlinemsgmodel.insert(toid,js.dump());
}

// 处理注册业务 ORM 业务层操作的都是对象 DAO
//  name password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    user.setState("offline");
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgId"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgId"] = REG_MSG_ACK;
        response["errno"] = 1;
        // response["errmsg"] = 1;
        conn->send(response.dump());
    }
}

//处理客户端异常退出
/*先从客户表中_usermap中删除，然后再数据库中将online改成offline*/
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        lock_guard<mutex> lock(_connMutex);
        for(auto it=_userConnMap.begin();it != _userConnMap.end();it++)
        {
            if(it->second == conn)
            {
                //从map表中删除用户的连接信息
                _userConnMap.erase(it);
                user.setId(it->first);
                break;
            }
        }
    }
    //用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());
    //更新用户的状态信息
    if(user.getId() != -1)
    {
        user.setState("offline");
        _userModel.updateState(user);
    }
}

 
//添加好友业务 msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    //存储好友信息
    _friendModel.insert(userid,friendid);

}
// 创建群组任务
void ChatService::createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int useid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    //创建群组
    Group group(-1,name,desc);
    if(_groupmodel.createGroup(group))
    {
        //加入群组业务
        _groupmodel.addGroup(useid,group.getId(),"creator");
    }
}
//加入群组业务
void ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{   
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();

    _groupmodel.addGroup(userid,groupid,"normal");
}
//群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    vector<int> useridVec = _groupmodel.queryGroupsUsers(userid,groupid);
    {
        lock_guard<mutex> lock(_connMutex);
        for(int id : useridVec)
        {
            auto it = _userConnMap.find(id);
            if(it != _userConnMap.end())
            {
                //转发消息
                it->second->send(js.dump());
            }
            else
            {
                //查询toid是否在线
                User user = _userModel.query(id);
                if(user.getState() == "online")
                {
                    _redis.publish(id,js.dump());
                }
                else
                    //存储离线消息
                    _offlinemsgmodel.insert(id,js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if(it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    //存储该用户的离线消息
    _offlinemsgmodel.insert(userid,msg);
}