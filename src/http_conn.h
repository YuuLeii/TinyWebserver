#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <string>
using std::string;

#include <netinet/in.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
// #include <sys/types.h>
#include <fcntl.h>
#include <mysql/mysql.h>
#include "sql_connection_pool.h"

class http_conn{
    static const int FILENAME_LEN = 200;   //读取文件名称的大小
    static const int READ_BUFFER_SIZE = 2048;  //读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024; //写缓冲区大小
    enum class METHOD: int{
        GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATH 
    };
    enum class CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
    };
    enum class HTTP_CODE{
        NO_REQUEST = 0, GET_REQUEST, BAD_REQUEST, NO_RESOURCE,
        FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION
    };
    enum class LINE_STATUS{
        LINE_OK = 0, LINE_BAD, LINE_OPEN
    };

    // For debug
    // const char*
public:
    static int user_count_;
    static int epollfd_;
    int state_;                 //读为0， 写为1
    http_conn();
    ~http_conn();

    void init(const int clientfd, const sockaddr_in& addr, char *, string, string, string, connection_pool *);
    void initmysql_result(connection_pool *connPool);
    void closeConnection();
    void process();
    void init();
private:
    bool read_once();
    HTTP_CODE process_read();
    HTTP_CODE do_request();
    bool process_write(HTTP_CODE ret);
    bool write();
    

    HTTP_CODE parse_request_line(char *);
    HTTP_CODE parse_headers(char *);
    HTTP_CODE parse_content(char *);
    LINE_STATUS parse_line();
    char *get_line(){ return readbuf_ + start_line_; }

    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    // bool add_linger();  
    bool add_blank_line();
private:
    //该HTTP连接的socket和对方的socket地址
    int sockfd_;
    sockaddr_in addr_;

    //读缓冲区
    char readbuf_[READ_BUFFER_SIZE];
    //读入缓冲区read_buf中的数据的最后一个字节的下一个位置
    int read_idx_;
    //当前正在解析的字符在读缓冲区中的位置
    int checked_idx_;
    //当前正在解析的行的起始位置
    int start_line_;

    //写缓冲区
    char writebuf_[WRITE_BUFFER_SIZE];
    //写缓冲中的待发送的字节数
    int write_idx_;
    
    //主状态机的方法
    CHECK_STATE check_state_;
    //请求方法
    METHOD method_;

    //客户请求的目标文件的完整路径，其内容等于"网站根目录" + url
    char real_file_[FILENAME_LEN];
    //客户请求的目标文件的文件名
    char *url_;
    //HTTP协议版本号
    char *version_;
    //主机名
    char *host_;
    //HTTP请求的消息体长度
    int content_length_;
    //HTTP请求是否要求保持连接
    // bool linger_;

    //客户请求的目标文件mmap到内存中的起始地址
    char *file_address_;    
    //目标文件的状态，可通过它判断文件类型、文件大小、是否存在、是否可读等信息
    struct stat file_stat_;
    //使用writev来执行写操作
    struct iovec ovec_[2];
    int ovec_count_;

    //是否启用cgi
    bool cgi_;
    //存储请求头数据
    char *string_;
    
    int bytes_to_send_;
    int bytes_have_send_;
    //网站根目录
    char *root_;

    MYSQL *mysql_;
    connection_pool *connPool_;
    //数据库用户名和密码
    char sql_user_[100];
    char sql_passwd_[100];
    char sql_name_[100];
};


int setnonblocking(int fd);
void removefd(int epollfd, int fd);
void addfd(int epollfd, int sockfd, bool one_shot);
void modfd(int epollfd, int sockfd, uint32_t event_);
#endif