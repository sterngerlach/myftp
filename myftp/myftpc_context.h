
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftpc_context.h */

#ifndef MYFTPC_CONTEXT_H
#define MYFTPC_CONTEXT_H

#include <stdbool.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * FTPクライアントに関する情報を保持する構造体
 */
struct ftp_client_context {
    int                 client_sock;
    struct addrinfo*    addr_info;
    struct in_addr      server_addr;
};

/*
 * FTPクライアントに関する情報を初期化
 */
bool initialize_ftp_client_context(struct ftp_client_context* context);

/*
 * FTPクライアントに関する情報を破棄
 */
void free_ftp_client_context(struct ftp_client_context* context);

#endif /* MYFTPC_CONTEXT_H */

