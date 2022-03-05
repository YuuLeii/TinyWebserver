#ifndef SQL_CONNECTION_POOL_H
#define SQL_CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <error.h>

#include <string>
#include <list>
#include <iostream>

#include "locker.h"

using std::string;
class connection_pool{
public:
	
	void init(string url, string user, string passwd,
				string dbName, int port, int maxConn);
	~connection_pool();

	MYSQL *getConnection();
	bool releaseConnection(MYSQL *conn);
	int getFreeConnCount();

	static connection_pool *getInstance();
private:
	connection_pool();
private:
	int maxConn_;
	int curConn_;
	int freeConn_;
	locker lock_;
	std::list<MYSQL*> connList_;
	sem reserve_;

public:
	//主机地址
	string url_;
	//数据库端口号
	int port_;
	//数据库登录用户名
	string user_;
	//数据库登录密码
	string passwd_;
	//数据库名字
	string dbName_;
	// bool isOpenLog;
};

class connectionRAII{
public:
	connectionRAII(MYSQL **conn, connection_pool *connPool);
	~connectionRAII();
private:
	MYSQL *connRAII;
	connection_pool *poolRAII;
};
#endif // __SQL_CONNECTION_POOL_H__