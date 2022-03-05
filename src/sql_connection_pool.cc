#include "sql_connection_pool.h"


connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->getConnection();
	connRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->releaseConnection(connRAII);
}

connection_pool::connection_pool(){
	curConn_ = freeConn_ = 0;
}
connection_pool *connection_pool::getInstance(){
	static connection_pool con;
	return &con;
}
void connection_pool::init(string url, string user, string passwd,
								string dbName, int port, int maxConn){
	url_ = url;
	user_ = user;
	passwd_ = passwd;
	dbName_ = dbName;
	port_ = port;
	maxConn_ = maxConn;
	curConn_ = 0;
	freeConn_ = 0;

	for (int i = 0; i < maxConn; ++ i){
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL){
			//LOG_ERROR("MySQL Error");
			exit(-1);
		}
		con = mysql_real_connect(con, url_.c_str(), user_.c_str(), passwd_.c_str(), dbName_.c_str(), port_, NULL, 0);

		if (con == NULL){
			//LOG_ERROR("MySQl Error");
			exit(-1);
		}
		connList_.push_back(con);
		++ freeConn_;
	}
	reserve_ = sem(freeConn_);
	maxConn_ = freeConn_;
}

connection_pool::~connection_pool(){
	lock_.lock();
	if (connList_.size() > 0){
		for (auto it = connList_.begin(); it != connList_.end(); it ++){
			MYSQL *con = *it;
			mysql_close(con);
		}
		curConn_ = 0;
		freeConn_ = 0;
		connList_.clear();
	}
	lock_.unlock();
}

MYSQL *connection_pool::getConnection(){
	if (connList_.empty())
		return NULL;
	MYSQL *con = NULL;
	// printf("函数: %s, cur: %d, free: %d\n", __func__, curConn_, freeConn_);
	reserve_.wait();
	lock_.lock();

	con = connList_.front();
	connList_.pop_front();

	--freeConn_;
	++curConn_;
	lock_.unlock();
	// printf("函数: %s, cur: %d, free: %d\n", __func__, curConn_, freeConn_);
	return con;
}

bool connection_pool::releaseConnection(MYSQL *con){
	if (!con)
		return false;
	// printf("函数: %s, cur: %d, free: %d\n", __func__, curConn_, freeConn_);
	lock_.lock();

	connList_.push_back(con);
	++ freeConn_;
	-- curConn_;
	lock_.unlock();

	reserve_.post();
	// printf("函数: %s, cur: %d, free: %d\n", __func__, curConn_, freeConn_);
	return true;
}

int connection_pool::getFreeConnCount(){
	return freeConn_;
}


