#include "config.h"
#include <unistd.h>
#include <stdlib.h>

Config::Config(){
	port_ = 9006;
	sqlnum_ = 8;
	threadnum_ = 8;
}

bool Config::parse_arg(int argc, char *argv[]){
	// getopt不输出错误信息，否则选项不带参数时会输出错误信息
	// 如 ./a.out -p   输出：  ./a.out: option requires an argument -- 'p'
	// opterr = 0;
	if (argc == 1) return true;
	else if (argc % 2 == 0) return false;
	int opt;
	const char *str = "p:s:t:";
	
	while ((opt = getopt(argc, argv, str)) != -1){
		int num = atoi(optarg);
		if (!num)
			return false;
		switch(opt){
			case 'p':
				port_ = num;
				break;
			case 's':
				sqlnum_ = num;
				break;
			case 't':
				threadnum_ = num;
				break;
			case '?':
				return false;
		}
	}
	return true;
}
