#include<iostream>
#include<string>
#include<cstring>

using namespace std;

int main()
{
    char name[50];
    char pwd[50];
    memset(name,0x00,sizeof(name));
    memset(pwd,0x00,sizeof(pwd));
    cout<<"username:"<<endl;
    cin.getline(name,50);
    cout<<"userpassword:"<<endl;
    cin.getline(pwd,50);
    cout<<name<<" : "<<pwd<<endl;
    return 0;
}