/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* 
 * 建立与目标的TCP连接，返回客户端连接使用的套接字
 * host: 目标域名或主机名
 * clientPort: 目标端口
 */
int Socket(const char *host, int clientPort)
{
    int sock;               // 客户端套接字标识符
    unsigned long inaddr;   // 主机名的IP地址的数字形式
    struct sockaddr_in ad;  // 套接字地址结构
    struct hostent *hp;     // 域名IP地址
    
    // 初始化目标套接字的地址，指定使用的IP协议为IPv4
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;

    // 将目标主机名的IP地址转换为数字
    inaddr = inet_addr(host);
    // 如果返回值不为INADDR_NONE，说明不是无效的IP地址，设置目标套接字的IP地址
    if (inaddr != INADDR_NONE)
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    // 否则说明是无效的IP地址，host不是主机名而是域名
    else
    {
        // 通过域名获取IP地址
        hp = gethostbyname(host);
        // 如果获取失败。返回-1
        if (hp == NULL)
            return -1;
        // 设置目标套接字的IP地址
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    // 设置目标套接字的端口号
    ad.sin_port = htons(clientPort);
    
    // 创建一个使用TCP协议的socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    // 如果创建失败，直接返回
    if (sock < 0)
        return sock;
    // 进行连接，如果连接不成功，返回-1
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    // 如果连接成功，返回sock
    return sock;
}
