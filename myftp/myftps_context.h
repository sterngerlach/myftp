
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftps_context.h */

#ifndef MYFTPS_CONTEXT_H
#define MYFTPS_CONTEXT_H

#include <limits.h>
#include <stdbool.h>

#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * FTPサーバに関する情報を保持する構造体
 * 各クライアント(プロセス)ごとに別々の情報を保持
 * client_addr, client_portメンバはネットワークバイトオーダーで保持
 */
struct ftp_server_context {
    char            cwd[PATH_MAX + 1];
    int             listen_sock;
    int             accept_sock;
    struct in_addr  client_addr;
    in_port_t       client_port;
};

/*
 * FTPサーバに関する情報を初期化
 */
bool initialize_ftp_server_context(struct ftp_server_context* context);

/*
 * FTPサーバに関する情報を破棄
 */
void free_ftp_server_context(struct ftp_server_context* context);

#endif /* MYFTPS_CONTEXT_H */

