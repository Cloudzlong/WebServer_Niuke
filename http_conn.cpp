#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <libgen.h>
#include <signal.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include "locker.h"
#include "http_conn.h"

int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void setnonblocking(int fd) {
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

int addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    //event.events = EPOLLIN | EPOLLRDHUP; //EPOLLRDHUP用于判断连接断开，不用根据返回值判断异常断开了
    event.events = EPOLLIN | EPOLLRDHUP | EPOLLET ; 

    if(one_shot) {
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    //设置文件描述符非阻塞
    setnonblocking(fd);
}

int removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符， 重置EPOLLONESHOT事件， 确保下一次可读时， EPOLLIN事件能触发
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
}

void http_conn::init(int sockfd, const sockaddr_in & addr) {
    m_sockfd = sockfd;
    m_address = addr;

    //设置端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //添加到epoll对象中
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

//单独写一个init，后续参数初始化就不用每次都传参数，不用执行其他操作
void http_conn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE; //初始化状态为解析请求首行
    m_checked_index = 0;
    m_start_line = 0;
    m_read_idx = 0;
}

void http_conn::close_conn() {
    if(m_sockfd != -1) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

bool http_conn::read() {
    
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    //读取到的字节
    int bytes_read = 0;
    while(true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                //没有数据
                break;
            } 
        } else if(bytes_read == 0) {
            //对方关闭连接
            return false;
        }
        m_read_idx += bytes_read;
    }
    
    printf("读取到了数据:\n%s\n", m_read_buf);

    return true;
}

bool http_conn::write() {
    printf("一次性写完数据\n");
    return true;
}

//主状态机，解析请求
http_conn::HTTP_CODE http_conn::process_read() {

    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char * text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) 
        || ((line_status = parse_line()) == LINE_OK)) {
            //解析到了请求体，是完整的数据 或者 解析到了一行完整的数据

            //获取一行数据
            text = get_line();

            m_start_line = m_checked_index;
            printf("got 1 http line : %s\n", text);

            switch(m_check_state) {
                case CHECK_STATE_REQUESTLINE:
                {
                    ret = parse_request_line(text);
                    if(ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    }
                    break;
                }

                case CHECK_STATE_HEADER:
                {
                    ret = parse_headers(text);
                    if(ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    } else if(ret = GET_REQUEST) {
                        return do_request();
                    }
                }

                case CHECK_STATE_CONTENT:
                {
                    ret = parse_content(text);
                    if(ret = GET_REQUEST) {
                        return do_request();
                    }
                    line_status = LINE_OPEN;
                    break;
                }

                default:
                {
                    return INTERNAL_ERROR;
                }
            }
        return NO_REQUEST;
    }


}

http_conn::HTTP_CODE http_conn::parse_request_line(char * text) {

}

http_conn::HTTP_CODE http_conn::parse_headers(char * text) {

}

http_conn::HTTP_CODE http_conn::parse_content(char * text) {

}

//解析一行，判断依据\r\n
http_conn::LINE_STATUS http_conn::parse_line() {

}

http_conn::HTTP_CODE http_conn::do_request() {

}

//由线程池中的工作线程调用，这是处理HTTP请求的入口函数
void http_conn::process() {

    //解析HTTP请求
    HTTP_CODE read_ret =  process_read();
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }


    printf("parse request, create response\n");
    //生成相应

}
