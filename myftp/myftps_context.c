
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftps_context.c */

#include <assert.h>
#include <string.h>

#include "myftps_context.h"

/*
 * FTPサーバに関する情報を初期化
 */
bool initialize_ftp_server_context(struct ftp_server_context* context)
{
    assert(context != NULL);

    memset(context->cwd, 0, sizeof(context->cwd));
    context->listen_sock = -1;
    context->accept_sock = -1;
    context->client_addr.s_addr = htonl(0);
    context->client_port = htons(0);

    return true;
}

/*
 * FTPサーバに関する情報を破棄
 */
void free_ftp_server_context(struct ftp_server_context* context)
{
    assert(context != NULL);

    memset(context->cwd, 0, sizeof(context->cwd));
    context->listen_sock = -1;
    context->accept_sock = -1;
    context->client_addr.s_addr = htonl(0);
    context->client_port = htons(0);

    return;
}

