#ifndef CONFIG_H
#define CONFIG_H

class Config{
public:
	Config();
	~Config(){};
	bool parse_arg(int argc, char *argv[]);
public:
	// 端口号,默认9006
	int port_;

	// 线程池内线程数量,默认8
	int threadnum_;

	// 数据库连接池,默认8
	int sqlnum_;
};

#endif