
#include <iostream>
#include <string>

#include "webserver.h"
#include "config.h"

using namespace std;

int main(int argc, char *argv[]){
	Config conf;
	bool isParseArgSuccess = conf.parse_arg(argc, argv);
	if (!isParseArgSuccess){
		cout << "usage: " << argv[0] << " [-p port] [-s sql_num] [-t thread_num]" << endl;
		return 0;
	}
	string user = "";               // 数据库 用户名
	string passwd = "";			// 数据库 密码
	string dbname = "";             // 数据库 库名

	WebServer webserver(conf.port_, conf.threadnum_, conf.sqlnum_, user, passwd, dbname);
	webserver.eventLoop();

	
	return 0;
}
