
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftps.c */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "myftp.h"
#include "myftps_context.h"
#include "util.h"

/*
 * サーバ上で実行されるコマンドの関数
 */
typedef bool (*server_command_handler)(
    struct ftp_server_context* context,
    struct ftp_header* header,
    bool* app_exit);

/*
 * サーバ上で実行されるコマンドを表す構造体
 */
struct server_command_entry {
    uint8_t                 type;
    server_command_handler  handler;
};

/*
 * シグナルSIGCHLDのハンドラ
 */
void sigchld_handler(int sig)
{
    pid_t cpid;
    int status;

    (void)sig;
    
    /* 終了した子プロセス(ゾンビプロセス)の処理 */
    while ((cpid = waitpid(-1, &status, WNOHANG)) != 0) {
        if (cpid == -1) {
            if (errno == ECHILD) {
                /* 終了した子プロセスは存在しない */
                break;
            } else if (errno == EINTR) {
                /* シグナルに割り込まれたので再度実行 */
                continue;
            } else {
                /* その他のエラー */
                print_error(__func__, "waitpid() failed: %s\n", strerror(errno));
                break;
            }
        } else {
            print_message(__func__, "child process (pid: %d) exited\n", cpid);
        }
    }
}

/*
 * シグナルハンドラを設定
 */
bool setup_signal_handlers()
{
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(struct sigaction));

    /* シグナルSIGINTのハンドラを設定 */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = SIG_DFL;
    sigact.sa_flags = 0;

    if (sigaction(SIGINT, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to set default signal handler for SIGINT\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGCHLDのハンドラを設定 */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = sigchld_handler;
    sigact.sa_flags = SA_NOCLDSTOP | SA_RESTART;

    if (sigaction(SIGCHLD, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to register signal handler for SIGCHLD\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGTTINのハンドラを設定 */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = SIG_IGN;
    sigact.sa_flags = SA_RESTART;

    if (sigaction(SIGTTIN, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to set signal handler for SIGTTIN\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGTTOUのハンドラを設定 */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = SIG_IGN;
    sigact.sa_flags = SA_RESTART;

    if (sigaction(SIGTTOU, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to set signal handler for SIGTTOU\n",
                    strerror(errno));
        return false;
    }

    return true;
}

/*
 * シグナルハンドラをリセット
 */
bool reset_signal_handlers()
{
    struct sigaction sigact;

    memset(&sigact, 0, sizeof(struct sigaction));

    sigemptyset(&sigact.sa_mask);
    sigact.sa_handler = SIG_DFL;
    sigact.sa_flags = 0;

    /* シグナルSIGINTのハンドラをリセット */
    if (sigaction(SIGINT, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to reset signal handler for SIGINT\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGCHLDのハンドラを設定 */
    if (sigaction(SIGCHLD, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to reset signal handler for SIGCHLD\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGTTINのハンドラを設定 */
    if (sigaction(SIGTTIN, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to reset signal handler for SIGTTIN\n",
                    strerror(errno));
        return false;
    }

    /* シグナルSIGTTOUのハンドラを設定 */
    if (sigaction(SIGTTOU, &sigact, NULL) < 0) {
        print_error(__func__,
                    "sigaction() failed: %s, "
                    "unable to reset signal handler for SIGTTOU\n",
                    strerror(errno));
        return false;
    }
    
    return true;
}

/*
 * サーバのカレントディレクトリを初期化
 */
bool initialize_current_directory(struct ftp_server_context* context)
{
    assert(context != NULL);

    /* カレントディレクトリを取得 */
    if (getcwd(context->cwd, sizeof(context->cwd)) == NULL) {
        print_error(__func__, "getcwd() failed: %s\n", strerror(errno));
        return false;
    }

    print_message(__func__, "server current directory: %s\n", context->cwd);

    return true;
}

/*
 * サーバのカレントディレクトリを変更
 */
bool update_current_directory(
    struct ftp_server_context* context, const char* path, int* save_errno)
{
    assert(context != NULL);
    assert(path != NULL);
    assert(save_errno != NULL);

    /* カレントディレクトリを変更 */
    if (chdir(path) < 0) {
        *save_errno = errno;
        print_error(__func__, "chdir() failed: %s\n", strerror(errno));
        return false;
    }

    /* カレントディレクトリを取得 */
    if (getcwd(context->cwd, sizeof(context->cwd)) == NULL) {
        *save_errno = errno;
        print_error(__func__, "getcwd() failed: %s\n", strerror(errno));
        return false;
    }

    *save_errno = 0;
    print_message(__func__, "server current directory: %s\n", context->cwd);

    return true;
}

/*
 * サーバの接続待ちソケットの作成
 */
bool setup_listen_socket(int* listen_sock)
{
    struct sockaddr_in server_addr;
    int reuse_addr = 1;

    assert(listen_sock != NULL);

    /* ソケットを作成 */
    if ((*listen_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_error(__func__,
                    "socket() failed: %s, failed to create listening socket\n",
                    strerror(errno));
        return false;
    }

    print_message(__func__,
                  "listening socket (sockfd: %d) successfully created\n",
                  *listen_sock);

    /* TIME_WAIT状態のソケットに再度bindできるように設定 */
    if (setsockopt(*listen_sock, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_addr, sizeof(reuse_addr)) < 0) {
        print_error(__func__, "setsockopt() failed: %s\n", strerror(errno));
        return false;
    }

    /* ソケットに割り当てるアドレスの設定 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(FTP_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* ソケットにアドレスを割り当て */
    if (bind(*listen_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        print_error(__func__,
                    "bind() failed: %s, failed to bind listening socket\n",
                    strerror(errno));
        return false;
    }

    print_message(__func__,
                  "server ip address: %s (INADDR_ANY), port: %" PRIu16 "\n",
                  inet_ntoa(server_addr.sin_addr), FTP_SERVER_PORT);

    return true;
}

/*
 * ソケットを閉じる
 */
bool close_socket(int sock)
{
    assert(sock >= 0);

    /* ソケットでの通信を停止 */
    if (shutdown(sock, SHUT_RDWR) < 0) {
        print_error(__func__, "shutdown() failed: %s\n", strerror(errno));
        return false;
    }

    /* ソケットを閉じる */
    if (close(sock) < 0) {
        print_error(__func__, "close() failed: %s\n", strerror(errno));
        return false;
    }

    print_message(__func__, "socket (sockfd: %d) successfully closed\n", sock);

    return true;
}

/*
 * サーバ上で実行される各種コマンド群の関数
 */

/*
 * QUITコマンドの処理
 */
bool on_quit_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    uint16_t data_length;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_QUIT);
    assert(app_exit != NULL);
    
    /* データ長を取得 */
    data_length = ntohs(received_header->length);

    /* プロセスを終了 */
    *app_exit = true;

    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid quit command: 'code' field must be set to 0\n");
    }
    
    /* lengthフィールドが0でない場合はエラー */
    if (data_length != 0) {
        print_error(__func__,
                    "invalid quit command: 'length' field must be set to 0\n");

        /* クライアントにエラーメッセージを送信 */
        send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                        FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_SYNTAX,
                        0, NULL);

        return false;
    }

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         FTP_HEADER_TYPE_OK, FTP_HEADER_CODE_OK, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));
        return false;
    }

    return true;
}

/*
 * PWDコマンドの処理
 */
bool on_pwd_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    uint16_t data_length;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_PWD);
    assert(app_exit != NULL);

    /* データ長を取得 */
    data_length = ntohs(received_header->length);
    
    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid pwd command: 'code' field must be set to 0\n");
    }
    
    /* lengthフィールドが0でない場合はエラー */
    if (data_length != 0) {
        print_error(__func__,
                    "invalid pwd command: 'length' field must be set to 0\n");

        /* クライアントにエラーメッセージを送信 */
        send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                        FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_SYNTAX,
                        0, NULL);

        return false;
    }

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         FTP_HEADER_TYPE_OK, FTP_HEADER_CODE_OK,
                         strlen(context->cwd), context->cwd)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));
        return false;
    }

    return true;
}

/*
 * CWDコマンドの処理
 */
bool on_cwd_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    char* data;
    int save_errno;
    uint8_t type;
    uint8_t code;
    uint16_t data_length;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_CWD);
    assert(app_exit != NULL);

    /* データ長を取得 */
    data_length = ntohs(received_header->length);

    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid cwd command: 'code' field must be set to 0\n");
    }
    
    /* lengthフィールドが0である場合はエラー */
    if (data_length == 0) {
        print_error(__func__,
                    "invalid cwd command: 'length' field must be greater than 0\n");

        /* クライアントにエラーメッセージを送信 */
        send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                        FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_SYNTAX,
                        0, NULL);

        return false;
    }

    /* データ部分を受信 */
    if (!receive_string_data(context->accept_sock, context->client_addr, "client",
                             true, data_length, &data)) {
        print_error(__func__, "receive_string_data() failed\n");
        return false;
    }

    /* カレントディレクトリを変更 */
    if (!update_current_directory(context, data, &save_errno))
        print_error(__func__, "update_current_directory() failed\n");
    
    /* 受信したデータを解放 */
    SAFE_FREE(data);

    /* クライアントに送信するメッセージタイプの判定 */
    switch (save_errno) {
        case 0:
            type = FTP_HEADER_TYPE_OK;
            code = FTP_HEADER_CODE_OK;
            break;
        case EACCES:
            /* 指定されたパスのいずれかの構成要素に検索許可がない */
            type = FTP_HEADER_TYPE_FILE_ERR;
            code = FTP_HEADER_CODE_FILE_ERR_PERM_ERR;
            break;
        case ENOENT:
            /* ファイルが存在しない */
            type = FTP_HEADER_TYPE_FILE_ERR;
            code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
            break;
        case ENOTDIR:
            /* 指定されたパスの構成要素がディレクトリでない */
            type = FTP_HEADER_TYPE_FILE_ERR;
            code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
            break;
        default:
            /* その他のエラー */
            type = FTP_HEADER_TYPE_UNKWN_ERR;
            code = FTP_HEADER_CODE_UNKWN_ERR;
            break;
    }

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         type, code, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));
        return false;
    }

    return true;
}

/*
 * LISTコマンドの処理
 */
bool on_list_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    char* data = NULL;
    char* send_data = NULL;
    int save_errno = 0;
    uint8_t type;
    uint8_t code;
    uint16_t data_length;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_LIST);
    assert(app_exit != NULL);

    /* データ長を取得 */
    data_length = ntohs(received_header->length);

    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid list command: 'code' field must be set to 0\n");
    }
    
    /* データ部分を受信 */
    if (data_length != 0) {
        if (!receive_string_data(context->accept_sock, context->client_addr, "client",
                                 true, data_length, &data)) {
            print_error(__func__, "receive_string_data() failed\n");
            return false;
        }
    }
    
    /* リプライメッセージのデフォルト値 */
    type = FTP_HEADER_TYPE_OK;
    code = FTP_HEADER_CODE_OK_SERVER_DATA_REMAINING;

    /* ファイルに関する情報を取得 */
    if (!get_list_command_result(&send_data, &save_errno,
                                 (data != NULL) ? data : context->cwd, true)) {
        print_error(__func__, "get_list_command_result() failed\n");

        switch (save_errno) {
            case 0:
                /* 不明なエラー */
                type = FTP_HEADER_TYPE_UNKWN_ERR;
                code = FTP_HEADER_CODE_UNKWN_ERR;
                break;
            case EACCES:
                /* 指定されたパスのいずれかの構成要素に検索許可がない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_PERM_ERR;
                break;
            case ENOENT:
                /* 指定されたパスの構成要素が存在しない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                break;
            case ENOTDIR:
                /* 指定されたパスの構成要素がディレクトリでない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                break;
            default:
                /* その他のエラー */
                type = FTP_HEADER_TYPE_UNKWN_ERR;
                code = FTP_HEADER_CODE_UNKWN_ERR;
                break;
        }
    }
    
    /* 受信したデータを解放 */
    SAFE_FREE(data);

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         type, code, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));
        return false;
    }

    /* エラーメッセージの場合は終了 */
    if (type != FTP_HEADER_TYPE_OK)
        return true;
    
    /* クライアントにデータメッセージを送信 */
    if (!send_data_message(context->accept_sock, context->client_addr, "client", true,
                           send_data, strlen(send_data), 1024)) {
        print_error(__func__,
                    "send_data_message() failed, could not send data message to client %s\n",
                    inet_ntoa(context->client_addr));
        return false;
    }

    /* 送信データを解放 */
    SAFE_FREE(send_data);

    return true;
}

/*
 * RETRコマンドの処理
 */
bool on_retr_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    int fd;
    int save_errno;
    uint8_t type;
    uint8_t code;
    uint16_t data_length;
    struct stat stat_buf;
    char* data = NULL;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_RETR);
    assert(app_exit != NULL);

    /* データ長を取得 */
    data_length = ntohs(received_header->length);

    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid retr command: 'code' field must be set to 0\n");
    }

    /* lengthフィールドが0である場合はエラー */
    if (data_length == 0) {
        print_error(__func__,
                    "invalid retr command: 'length' field must be greater than 0\n");

        /* クライアントにエラーメッセージを送信 */
        send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                        FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_SYNTAX,
                        0, NULL);

        return false;
    }

    /* データ部分を受信 */
    if (!receive_string_data(context->accept_sock, context->client_addr, "client",
                             true, data_length, &data)) {
        print_error(__func__, "receive_string_data() failed\n");
        return false;
    }

    /* リプライメッセージのデフォルト値 */
    type = FTP_HEADER_TYPE_OK;
    code = FTP_HEADER_CODE_OK_SERVER_DATA_REMAINING;

    /* 指定されたファイルをオープン */
    if ((fd = open(data, O_RDONLY)) < 0) {
        save_errno = errno;
        print_error(__func__, "could not open file \'%s\': %s\n",
                    data, strerror(errno));

        /* エラーの種別を判定 */
        switch (save_errno) {
            case EACCES:
                /* 指定されたファイルに対するアクセス許可がない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_PERM_ERR;
                break;
            case ENOENT:
                /* 指定されたファイルが存在しない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                break;
            default:
                /* その他のエラー */
                type = FTP_HEADER_TYPE_UNKWN_ERR;
                code = FTP_HEADER_CODE_UNKWN_ERR;
                break;
        }
    } else {
        /* 指定されたファイルの属性を取得 */
        if (fstat(fd, &stat_buf) < 0) {
            save_errno = errno;
            print_error(__func__, "fstat() failed: %s\n", strerror(errno));
            
            /* エラーの種別を判定 */
            switch (save_errno) {
                case EACCES:
                    /* 指定されたファイルに対するアクセス許可がない */
                    type = FTP_HEADER_TYPE_FILE_ERR;
                    code = FTP_HEADER_CODE_FILE_ERR_PERM_ERR;
                    break;
                case ENOENT:
                    /* 指定されたパスの構成要素が存在しない */
                    type = FTP_HEADER_TYPE_FILE_ERR;
                    code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                    break;
                case ENOTDIR:
                    /* 指定されたパスの構成要素がディレクトリでない */
                    type = FTP_HEADER_TYPE_FILE_ERR;
                    code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                    break;
                default:
                    /* その他のエラー */
                    type = FTP_HEADER_TYPE_UNKWN_ERR;
                    code = FTP_HEADER_CODE_UNKWN_ERR;
                    break;
            }
        } else {
            /* 通常のファイルでない場合はエラー */
            if (!S_ISREG(stat_buf.st_mode)) {
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
            }
        }
    }

    /* 受信したデータを解放 */
    SAFE_FREE(data);

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         type, code, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));
        
        /* ファイルが開かれていれば閉じる */
        if (fd >= 0 && close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }
    
    /* エラーメッセージの場合は終了 */
    if (type != FTP_HEADER_TYPE_OK) {
        /* ファイルが開かれていれば閉じる */
        if (fd >= 0 && close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return true;
    }

    /* ファイルの内容をデータメッセージで送信 */
    if (!send_file_data_message(context->accept_sock, context->client_addr, "client",
                                true, fd, 1024)) {
        print_error(__func__,
                    "send_file_data_message() failed, "
                    "could not send file content to client %s\n",
                    inet_ntoa(context->client_addr));
        
        /* ファイルを閉じる */
        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* ファイルを閉じる */
    if (close(fd) < 0)
        print_error(__func__, "close() failed: %s\n", strerror(errno));
    
    return true;
}

/*
 * STORコマンドの処理
 */
bool on_stor_command_received(
    struct ftp_server_context* context,
    struct ftp_header* received_header,
    bool* app_exit)
{
    int fd;
    int save_errno;
    uint8_t type;
    uint8_t code;
    uint16_t data_length;
    char* data = NULL;

    assert(context != NULL);
    assert(received_header != NULL);
    assert(received_header->type == FTP_HEADER_TYPE_STOR);
    assert(app_exit != NULL);

    /* データ長を取得 */
    data_length = ntohs(received_header->length);

    /* FTPヘッダのチェック */
    /* codeフィールドの誤りについては無視 */
    if (received_header->code != 0) {
        print_message(__func__,
                      "invalid stor command: 'code' field must be set to 0\n");
    }

    /* lengthフィールドが0である場合はエラー */
    if (data_length == 0) {
        print_error(__func__,
                    "invalid stor command: 'length' field must be greater than 0\n");

        /* クライアントにエラーメッセージを送信 */
        send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                        FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_SYNTAX,
                        0, NULL);

        return false;
    }

    /* データ部分を受信 */
    if (!receive_string_data(context->accept_sock, context->client_addr, "client",
                             true, data_length, &data)) {
        print_error(__func__, "receive_string_data() failed\n");
        return false;
    }

    /* リプライメッセージのデフォルト値 */
    type = FTP_HEADER_TYPE_OK;
    code = FTP_HEADER_CODE_OK_CLIENT_DATA_REMAINING;

    /* 指定されたファイルをオープン */
    if ((fd = open(data, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        save_errno = errno;
        print_error(__func__, "could not open file \'%s\': %s\n",
                    data, strerror(errno));
        
        /* エラーの種別を判定 */
        switch (save_errno) {
            case EACCES:
                /* 指定されたファイルに対するアクセス許可がない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_PERM_ERR;
                break;
            case ENOTDIR:
                /* 指定されたパスの構成要素がディレクトリでない */
                type = FTP_HEADER_TYPE_FILE_ERR;
                code = FTP_HEADER_CODE_FILE_ERR_NOT_EXIST;
                break;
            default:
                /* その他のエラー */
                type = FTP_HEADER_TYPE_UNKWN_ERR;
                code = FTP_HEADER_CODE_UNKWN_ERR;
                break;
        }
    }

    /* 受信したデータを解放 */
    SAFE_FREE(data);

    /* クライアントにメッセージを送信 */
    if (!send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                         type, code, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to client %s\n",
                    inet_ntoa(context->client_addr));

        /* ファイルが開かれていれば閉じる */
        if (fd >= 0 && close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));
        
        return false;
    }

    /* エラーメッセージの場合は終了 */
    if (type != FTP_HEADER_TYPE_OK) {
        /* ファイルが開かれていれば閉じる */
        if (fd >= 0 && close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return true;
    }

    /* ファイルの内容をデータメッセージで受信して書き込み */
    if (!receive_file_data_message(context->accept_sock, context->client_addr,
                                   "client", true, fd)) {
        print_error(__func__,
                    "receive_file_data_message() failed, "
                    "could not receive file content from client %s\n",
                    inet_ntoa(context->client_addr));

        /* ファイルを閉じる */
        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return true;
    }

    /* ファイルを閉じる */
    if (close(fd) < 0)
        print_error(__func__, "close() failed: %s\n", strerror(errno));
    
    return true;
}

/*
 * サーバ上で実行されるコマンドのテーブル
 */
struct server_command_entry server_command_table[] = {
    { FTP_HEADER_TYPE_QUIT, on_quit_command_received    },
    { FTP_HEADER_TYPE_PWD,  on_pwd_command_received     },
    { FTP_HEADER_TYPE_CWD,  on_cwd_command_received     },
    { FTP_HEADER_TYPE_LIST, on_list_command_received    },
    { FTP_HEADER_TYPE_RETR, on_retr_command_received    },
    { FTP_HEADER_TYPE_STOR, on_stor_command_received    },
    { FTP_HEADER_TYPE_NONE, NULL                        },
};

/*
 * サーバ上で実行されるコマンドの関数を返す
 */
server_command_handler lookup_server_command_table(uint8_t type)
{
    struct server_command_entry* entry;

    for (entry = server_command_table; entry->handler != NULL; ++entry)
        if (entry->type == type)
            return entry->handler;
    
    return NULL;
}

/*
 * クライアントからのメッセージを処理
 */
bool handle_client_message(struct ftp_server_context* context)
{
    struct ftp_header header;
    bool app_exit = false;
    server_command_handler handler = NULL;

    assert(context != NULL);

    /* メインループ */
    while (!app_exit) {
        /* FTPヘッダの先頭4バイトを受信 */
        if (!receive_ftp_header(context->accept_sock, context->client_addr, "client",
                                true, &header)) {
            print_error(__func__,
                        "receive_ftp_header() failed, "
                        "could not receive ftp header from client %s\n",
                        inet_ntoa(context->client_addr));
            return false;
        }
        
        /* コマンドが見つからない場合はエラー */
        if ((handler = lookup_server_command_table(header.type)) == NULL) {
            print_error(__func__,
                        ANSI_ESCAPE_COLOR_RED
                        "lookup_server_command_table() failed: "
                        "corresponding command handler not found, "
                        "therefore connection to client %s will be shut down"
                        ANSI_ESCAPE_COLOR_RESET "\n",
                        inet_ntoa(context->client_addr));

            /* クライアントにエラーメッセージを送信 */
            send_ftp_header(context->accept_sock, context->client_addr, "client", true,
                            FTP_HEADER_TYPE_CMD_ERR, FTP_HEADER_CODE_CMD_ERR_UNKNOWN_CMD,
                            0, NULL);
            return false;
        }

        /* コマンドの実行 */
        if (!(*handler)(context, &header, &app_exit)) {
            print_error(__func__,
                        ANSI_ESCAPE_COLOR_RED
                        "failed to execute %s command handler, "
                        "therefore connection to client %s will be shut down"
                        ANSI_ESCAPE_COLOR_RESET "\n",
                        ftp_header_type_to_string(header.type),
                        inet_ntoa(context->client_addr));
            return false;
        }
    }

    return true;
}

/*
 * サーバの実行
 */
bool run_ftp_server(struct ftp_server_context* context)
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int accept_sock = -1;
    pid_t pid_child;

    assert(context != NULL);
    
    /* シグナルハンドラを設定 */
    if (!setup_signal_handlers()) {
        print_error(__func__, "setup_signal_handlers() failed\n");
        return false;
    }

    /* サーバの接続待ちソケットの作成 */
    if (!setup_listen_socket(&context->listen_sock)) {
        print_error(__func__, "setup_listen_socket() failed\n");
        return false;
    }

    /* 接続の受け付けを開始 */
    if (listen(context->listen_sock, FTP_SERVER_LISTEN_BACKLOG) < 0) {
        print_error(__func__, "listen() failed: %s\n", strerror(errno));
        close_socket(context->listen_sock);
        return false;
    }

    print_message(__func__, "ready for connection ...\n");

    /* メインループ */
    while (true) {
        print_message(__func__, "waiting for connection ...\n");

        /* 接続を受け付け */
        client_addr_len = sizeof(client_addr);
        accept_sock = accept(context->listen_sock,
                             (struct sockaddr*)&client_addr, &client_addr_len);
        
        if (accept_sock < 0) {
            print_error(__func__, "accept() failed: %s\n", strerror(errno));
            close_socket(context->listen_sock);
            return false;
        }

        print_message(__func__,
                      "connection to client %s (port: %" PRIu16 ", sockfd: %d) is established\n",
                      inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
                      accept_sock);
        
        /* 子プロセスを作成 */
        if ((pid_child = fork()) < 0) {
            print_error(__func__, "fork() failed: %s\n", strerror(errno));
            close_socket(context->listen_sock);
            return false;
        }

        if (pid_child == 0) {
            /* 子プロセスの処理 */
            /* 接続待ちソケットは不必要なので閉じる */
            if (close(context->listen_sock) < 0) {
                print_error(__func__, "close() failed: %s\n", strerror(errno));
                return false;
            }

            /* サーバの情報を更新 */
            context->accept_sock = accept_sock;
            context->client_addr.s_addr = client_addr.sin_addr.s_addr;
            context->client_port = client_addr.sin_port;

            /* シグナルハンドラをリセット */
            if (!reset_signal_handlers()) {
                print_error(__func__, "reset_signal_handlers() failed\n");
                close_socket(context->accept_sock);
                return false;
            }

            /* クライアントからのメッセージを処理 */
            if (!handle_client_message(context)) {
                print_error(__func__, "handle_client_message() failed\n");
                close_socket(context->accept_sock);
                return false;
            }

            /* 接続済みソケットを閉じる */
            if (!close_socket(context->accept_sock)) {
                print_error(__func__, "close_socket() failed\n");
                return false;
            }
            
            return true;
        }

        /* 親プロセスの処理 */

        /* 接続済みのソケットは不必要なので閉じる */
        if (close(accept_sock) < 0) {
            print_error(__func__, "close() failed: %s\n", strerror(errno));
            close_socket(context->listen_sock);
            return false;
        }
    }

    return true;
}

int main(int argc, char** argv)
{
    struct ftp_server_context context;
    int save_errno;
    bool exit_status;

    if (argc > 2) {
        fprintf(stderr, "usage: %s [<current directory>]\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* サーバの情報を初期化 */
    if (!initialize_ftp_server_context(&context)) {
        print_error(__func__, "initialize_ftp_server_context() failed\n");
        return EXIT_FAILURE;
    }

    /* サーバのカレントディレクトリを初期化 */
    if (!initialize_current_directory(&context)) {
        print_error(__func__, "initialize_current_directory() failed\n");
        free_ftp_server_context(&context);
        return EXIT_FAILURE;
    }

    /* サーバのカレントディレクトリを設定 */
    if (argc == 2) {
        if (!update_current_directory(&context, argv[1], &save_errno)) {
            print_error(__func__, "update_current_directory() failed\n");
            free_ftp_server_context(&context);
            return EXIT_FAILURE;
        }
    }

    /* サーバの実行 */
    exit_status = run_ftp_server(&context);

    /* サーバの情報を破棄 */
    free_ftp_server_context(&context);

    return exit_status ? EXIT_SUCCESS : EXIT_FAILURE;
}

