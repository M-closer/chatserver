#ifndef GROUPUSER_H
#define GROUPUSER_H

#include "user.hpp"

class GroupUser : public User
{
public:
    //群组用户，多了一个role角色信息，从User类直接继承，复用user的其它信息
    void setRole(string role) {this->role = role;}
    string getRole() {return this->role;} 

private:
    string role;
};

#endif