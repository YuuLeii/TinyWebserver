#include "http_conn.h"

//API介绍：https://www.runoob.com/cprogramming/c-function-strpbrk.html
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <sys/mman.h>
#include <unordered_map>

#define LOG;
#include <unistd.h>

#include <assert.h>
#include "locker.h"

int http_conn::epollfd_ = -1;
int http_conn::user_count_ = 0;

http_conn::http_conn(){}
http_conn::~http_conn(){}

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";


locker lock_;
std::unordered_map<std::string, std::string> users;
/*一个线程读取某个socket上的数据后开始处理数据，在处理过程中如果有新数据到来，此时另一个线程将可能会被唤醒，出现
两个线程同时处理同一个连接的情况。我们希望的是一个连接在任意时刻都只被同一线程处理，可开启EPOLLONESHOT事件，一个
线程处理socket时，其他线程将无法处理，当该线程处理完后，需要通过epoll_ctl重置epolloneshot事件*/

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM users"))
    {
        // LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

int setnonblocking(int fd){
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

void addfd(int epollfd, int sockfd, bool one_shot){
    epoll_event event;
    event.data.fd = sockfd;
    event.events = EPOLLIN | EPOLLRDHUP;      //后面考虑边缘触发
    if (one_shot) 
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event);
    setnonblocking(sockfd);
}
void modfd(int epollfd, int sockfd, uint32_t event_){
    struct epoll_event ev;
    ev.data.fd = sockfd;
    ev.events = event_ | EPOLLONESHOT;              // 这里要开启oneshot
    int ret = epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &ev);    
    if (ret == -1){
        LOG;
    }
}

void http_conn::init(const int fd, const sockaddr_in &addr, char *root, std::string dbuser, std::string dbpasswd, std::string dbname, connection_pool *cp){
    
    sockfd_ = fd;
    addr_ = addr;

    addfd(epollfd_, fd, true);
    user_count_ ++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    root_ = root;

    strcpy(sql_user_, dbuser.c_str());
    strcpy(sql_passwd_, dbpasswd.c_str());
    strcpy(sql_name_, dbname.c_str());

    connPool_ = cp;

    init();

    //add timer
    printf("fd %d now is initiated. user_count: %d\n", fd, user_count_);
}
void http_conn::init() {
    mysql_ = NULL;
    bytes_to_send_ = 0;
    bytes_have_send_ = 0;
    check_state_ = CHECK_STATE::CHECK_STATE_REQUESTLINE;
    method_ = METHOD::GET;
    url_ = 0;
    version_ = 0;
    content_length_ = 0;
    host_ = 0;
    start_line_ = 0;
    checked_idx_ = 0;
    read_idx_ = 0;
    write_idx_ = 0;
    cgi_ = 0;
    state_ = -1;

    memset(readbuf_, 0, READ_BUFFER_SIZE);
    memset(writebuf_, 0, WRITE_BUFFER_SIZE);
    memset(real_file_, 0, FILENAME_LEN);
}

bool http_conn::read_once(){
    if (read_idx_ >= READ_BUFFER_SIZE){
        return false;
    }
    int readbytes = 0;
    //默认LT, 后面可以考虑ET
    readbytes = recv(sockfd_, readbuf_ + read_idx_, READ_BUFFER_SIZE - read_idx_, 0);
    if (readbytes <= 0)
        return false;
    read_idx_ += readbytes;
    
    // #ifdef DEBUG
    //     FILE *fp = freopen("./in", "w+", stdout);       //测试用例所在路径
    // #endif
    // printf("文件描述符：%d\n", sockfd_);
    // for (int i = 0; i < readbytes; ++ i) {
    //     printf("%c", readbuf_[i]);
    // }

    // #ifdef DEBUG
    //     fclose(fp);
    // #endif


    connectionRAII mysqlcon(&mysql_, connPool_);
    return true;
}

http_conn::HTTP_CODE http_conn::process_read(){
    printf("%s, %d\n", __func__, __LINE__);
    LINE_STATUS line_status = LINE_STATUS::LINE_OK;
    HTTP_CODE ret = HTTP_CODE::NO_REQUEST;
    char *text = nullptr;
    
    // printf("check_state: %d, line_status: %d\n", check_state_, line_status);
    while ((check_state_ == CHECK_STATE::CHECK_STATE_CONTENT && line_status == LINE_STATUS::LINE_OK) || ((line_status = parse_line()) == LINE_STATUS::LINE_OK)){
        text = get_line();
        start_line_ = checked_idx_;
        // LOG_INFO
        
        switch (check_state_){
            case CHECK_STATE::CHECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if (ret == HTTP_CODE::BAD_REQUEST){
                    return HTTP_CODE::BAD_REQUEST;
                }
                break;
            case CHECK_STATE:: CHECK_STATE_HEADER:
                ret = parse_headers(text);
                if (ret == HTTP_CODE::BAD_REQUEST){
                    return HTTP_CODE::BAD_REQUEST;
                } else if (ret == HTTP_CODE::GET_REQUEST){
                    return do_request();
                }
                break;
            case CHECK_STATE::CHECK_STATE_CONTENT:
                ret = parse_content(text);
                if (ret == HTTP_CODE::GET_REQUEST)
                    return do_request();
                line_status = LINE_STATUS::LINE_OPEN;
                break;
            default:
                return HTTP_CODE::INTERNAL_ERROR;
        }
    }
    return HTTP_CODE::NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(real_file_, root_);
    int len = strlen(root_);
    const char *p = strrchr(url_, '/');

    //处理cgi
    if (cgi_ == true && (*(p + 1) == '2' || *(p + 1) == '3')){
        //判断是登录还是注册
        char flag = url_[1];
        char *url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(url_real, "/");
        strcat(url_real, url_ + 2);
        strncpy(real_file_ + len, url_real, FILENAME_LEN - len - 1);
        free(url_real);

        // 读出用户名和密码
        // user=123&passwd=123
        char name[100], passwd[100];
        int i;
        for (i = 5; string_[i] != '&'; ++ i)
            name[i - 5] = string_[i];
        name[i - 5] = '\0';

        int j = 0;
        for (i += 10; string_[i] != '\0'; ++i, ++ j){
            passwd[j] = string_[i];
        }
        passwd[j] = '\0';
        
        // std::string name, passwd;  上面可以用string简化
        if (*(p + 1) == '3'){            //注册
            // std::string sql_insert;
            printf("注册中...\n");
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据

            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO users(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, passwd);
            strcat(sql_insert, "')");
            printf("%s\n", sql_insert);

            string str_name = string(name);
            string str_passwd = string(passwd);
            if (!users.count(str_name))
            {
                lock_.lock();
                assert(mysql_ != NULL);
                int res = mysql_query(mysql_, sql_insert);
                users.insert({str_name, str_passwd});
                lock_.unlock();

                if (!res)
                    strcpy(url_, "/log.html");
                else
                    strcpy(url_, "/registerError.html");
            }
            else
                strcpy(url_, "/registerError.html");
        }else if (*(p + 1) == '2') {           //登录
            printf("登录中...\n");
            if (users.count(name) && users[name] == passwd){
                strcpy(url_, "/welcome.html");
            }else{
                strcpy(url_, "/logError.html");
            }
        }
    }

    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(real_file_ + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        strncpy(real_file_ + len, url_, FILENAME_LEN - len - 1);

    //获取目标文件的文件属性
    if (stat(real_file_, &file_stat_) < 0)
        return HTTP_CODE::NO_RESOURCE;

    if (!(file_stat_.st_mode & S_IROTH))
        return HTTP_CODE::FORBIDDEN_REQUEST;

    if (S_ISDIR(file_stat_.st_mode))
        return HTTP_CODE::BAD_REQUEST;

    int fd = open(real_file_, O_RDONLY);
    file_address_ = (char *)mmap(0, file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return HTTP_CODE::FILE_REQUEST;
}

void http_conn::unmap(){
    if (file_address_){
        munmap(file_address_, file_stat_.st_size);
        file_address_ = 0;
    }
}

http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for (; checked_idx_ < read_idx_; ++ checked_idx_){
        temp = readbuf_[checked_idx_];
        if ('\r' == temp){
            if (checked_idx_ + 1 == read_idx_){
                return LINE_STATUS::LINE_OPEN;
            }else if ('\n' == readbuf_[checked_idx_ + 1]){
                readbuf_[checked_idx_++] = '\0';
                readbuf_[checked_idx_++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        } else if ('\n' == temp){
            if (checked_idx_ > 1 && readbuf_[checked_idx_ - 1] == '\r'){
                readbuf_[checked_idx_- 1] = '\0';
                readbuf_[checked_idx_++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        }
    }
    return LINE_STATUS::LINE_OPEN;
}


//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text){
    url_ = strpbrk(text, " \t");
    if (!url_){
        return HTTP_CODE::BAD_REQUEST;
    }
    *url_ ++ = '\0';
    char *method = text;
    if (!strcasecmp(method, "GET"))
        method_ = METHOD::GET;
    else if (!strcasecmp(method, "POST")){
        method_ = METHOD::POST;
        cgi_ = 1;
    }else{
        return HTTP_CODE::BAD_REQUEST;
    }
    url_ += strspn(url_, " \t");

    version_ = strpbrk(url_, " \t");
    if (!version_){
        return HTTP_CODE::BAD_REQUEST;
    }
    *version_ ++ = '\0';
    version_ += strspn(version_, " \t");
    if (strcasecmp(version_, "HTTP/1.1")){
        return HTTP_CODE::BAD_REQUEST;
    }
    if (strncasecmp(url_, "http://", 7) == 0){
        url_ += 7;
        url_ = strchr(url_, '/');
    }
    if (strncasecmp(url_, "https://", 8) == 0){
        url_ += 8;
        url_ = strchr(url_, '/');
    }
    if (!url_ || url_[0] != '/'){
        return HTTP_CODE::BAD_REQUEST;
    }
    if (strlen(url_) == 1)
        strcat(url_, "judge.html");               //主页
    check_state_ = CHECK_STATE::CHECK_STATE_HEADER;
    return HTTP_CODE::NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers(char *text){
    if (text[0] == '\0'){
        if (content_length_){
            check_state_ = CHECK_STATE::CHECK_STATE_CONTENT;
            return HTTP_CODE::NO_REQUEST;
        }
        return HTTP_CODE::GET_REQUEST;
    }else if (strncasecmp(text, "HOST:", 5) == 0){
        text += 5;
        text += strspn(text, " \t");
        host_ = text;
    }else if (strncasecmp(text, "Content-length:", 15) == 0){
        text += 15;
        text += strspn(text, " \t");
        content_length_ = atoi(text);    
    }else{
        //LOG: unknown header
        1;
    }
    return HTTP_CODE::NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text){
    if (read_idx_ >= content_length_ + checked_idx_){
        text[content_length_] = '\0';
        //POST请求中最后为输入的用户名和密码
        string_ = text;
        return HTTP_CODE::GET_REQUEST;
    }
    return HTTP_CODE::NO_REQUEST;
}

bool http_conn::write(){
    if (bytes_to_send_ == 0) {
        modfd(epollfd_, sockfd_, EPOLLIN);
        init();
        return true;
    }

    int temp = 0;
    while (1){
        temp = writev(sockfd_, ovec_, ovec_count_);
        if (temp < 0){
            if (errno == EAGAIN){
                modfd(epollfd_, sockfd_, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send_ += temp;
        bytes_to_send_ -= temp;
        if (bytes_have_send_ >= ovec_[0].iov_len){
            ovec_[0].iov_len = 0;
            ovec_[1].iov_base = file_address_ + (bytes_have_send_ - write_idx_);
            ovec_[1].iov_len = bytes_to_send_;
        } else {
            ovec_[0].iov_base = writebuf_ + bytes_have_send_;
            ovec_[0].iov_len = ovec_[0].iov_len - bytes_have_send_;
        }

        if (bytes_to_send_ <= 0){
            unmap();
            modfd(epollfd_, sockfd_, EPOLLIN);       

            // 长连接
            init();
            return true;
        }
    }
}

bool http_conn::process_write(HTTP_CODE ret){
    printf("%s, %d, ret: %d\n", __func__, __LINE__, ret);
    switch (ret){
        case HTTP_CODE::INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        case HTTP_CODE::BAD_REQUEST:
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        case HTTP_CODE::FORBIDDEN_REQUEST:
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        case HTTP_CODE::FILE_REQUEST:
            add_status_line(200, ok_200_title);
            if (file_stat_.st_size != 0)
            {
                add_headers(file_stat_.st_size);
                ovec_[0].iov_base = writebuf_;
                ovec_[0].iov_len = write_idx_;
                ovec_[1].iov_base = file_address_;
                ovec_[1].iov_len = file_stat_.st_size;
                ovec_count_ = 2;
                bytes_to_send_ = write_idx_ + file_stat_.st_size;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
            break;
        default:
            return false;

    }
    printf("%s, %d, ret: %d\n", __func__, __LINE__, ret);
    ovec_[0].iov_base = writebuf_;
    ovec_[0].iov_len = write_idx_;
    ovec_count_ = 1;
    bytes_to_send_ = write_idx_;
    return true;
}
bool http_conn::add_response(const char *format, ...)
{
    if (write_idx_ >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(writebuf_ + write_idx_, WRITE_BUFFER_SIZE - 1 - write_idx_, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - write_idx_))
    {
        va_end(arg_list);
        return false;
    }
    write_idx_ += len;
    va_end(arg_list);

    // LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len)
{
    // return add_content_length(content_len) && add_linger() &&
    //        add_blank_line();
    return add_content_length(content_len) && add_blank_line();
}
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
// bool http_conn::add_linger()
// {
//     return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
// }
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

void http_conn::closeConnection(){
        //delete the timer
    LOG;
    if (sockfd_ != -1){
        removefd(epollfd_, sockfd_);
        user_count_--;
        // state_ = -1;
        printf("fd %d is closed. Now have %d fds remained.\n", sockfd_, user_count_);

        sockfd_ = -1;
    }

}
void http_conn::process(){
    // printf("%s, %d\n", __func__, __LINE__);
    //处理读
    if (state_ == 0){
        if (!read_once()){ 
            closeConnection();
            return ;
        }

        HTTP_CODE read_ret = process_read();
        if (read_ret == HTTP_CODE::NO_REQUEST){
            modfd(epollfd_, sockfd_, EPOLLIN);
            return ;
        }
        bool write_ret = process_write(read_ret);
        

        if (!write_ret){
            closeConnection();
        }
        state_ = 1;
        modfd(epollfd_, sockfd_, EPOLLOUT);

    }else if (state_ == 1) {                       //处理写
        if (write()){
            printf("%d's write is completed..\n", sockfd_);
            LOG;
            //对定时器的操作最好在主线程完成，避免同意事件有多个线程同时操作timerqueue
            //adjust timer
            init();
        } else {
            printf("write is failed. Now close this connection, fd: %d\n", sockfd_);
            closeConnection();    
        }
    }
}

