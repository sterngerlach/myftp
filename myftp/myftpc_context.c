
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftpc_context.c */

#include <assert.h>
#include <stdlib.h>

#include "myftpc_context.h"

/*
 * FTPクライアントに関する情報を初期化
 */
bool initialize_ftp_client_context(struct ftp_client_context* context)
{
    assert(context != NULL);

    context->client_sock = -1;
    context->addr_info = NULL;
    context->server_addr.s_addr = htonl(0);

    return true;
}

/*
 * FTPクライアントに関する情報を破棄
 */
void free_ftp_client_context(struct ftp_client_context* context)
{
    assert(context != NULL);

    context->client_sock = -1;

    if (context->addr_info != NULL)
        freeaddrinfo(context->addr_info);

    context->addr_info = NULL;
    context->server_addr.s_addr = htonl(0);

    return;
}

