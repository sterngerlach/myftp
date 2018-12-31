
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftpc.c */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dynamic_string.h"
#include "myftp.h"
#include "myftpc_context.h"
#include "util.h"

/*
 * サーバ上で実行されるコマンドの関数
 */
typedef bool (*client_command_handler)(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit);

/*
 * クライアント上で実行されるコマンドを表す構造体
 */
struct client_command_entry {
    char*                   name;
    client_command_handler  handler;
};

/*
 * サーバのIPアドレスを取得
 */
bool get_server_addr_info(
    struct ftp_client_context* context, const char* host_name)
{
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    int err = 0;

    assert(context != NULL);
    assert(host_name != NULL);

    /* サーバのIPアドレスを取得 */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;

    if ((err = getaddrinfo(host_name, FTP_SERVER_PORT_STR, &hints, &result)) != 0) {
        print_error(__func__,
                    "getaddrinfo() failed: %s, unable to get server ip address\n",
                    gai_strerror(err));
        return false;
    }
    
    /* サーバのIPアドレスを出力 */
    print_message(__func__,
                  "getaddrinfo() succeeded: server ip address: %s\n",
                  inet_ntoa(((struct sockaddr_in*)result->ai_addr)->sin_addr));
    
    context->addr_info = result;
    context->server_addr = ((struct sockaddr_in*)result->ai_addr)->sin_addr;

    return true;
}

/*
 * クライアントのソケットの作成
 */
bool setup_client_socket(const struct addrinfo* addr_info, int* client_sock)
{
    assert(addr_info != NULL);
    assert(client_sock != NULL);

    /* ソケットを作成 */
    if ((*client_sock = socket(addr_info->ai_family,
                               addr_info->ai_socktype,
                               addr_info->ai_protocol)) < 0) {
        print_error(__func__,
                    "socket() failed: %s, unable to create client socket\n",
                    strerror(errno));
        return false;
    }

    print_message(__func__, "client socket (sockfd: %d) successfully created\n",
                  *client_sock);

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
 * ユーザの入力文字列を取得
 */
char* get_line()
{
    char* input = NULL;
    size_t length = 0;
    ssize_t cc = 0;

    if ((cc = getline(&input, &length, stdin)) == -1)
        return NULL;

    /* 改行を除去 */
    chomp(input);

    return input;
}

/*
 * quitコマンドの処理
 */
bool on_command_quit(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    struct ftp_header header;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc != 1) {
        fprintf(stderr, "quit: too many arguments\n");
        return true;
    }

    /* サーバにQUITコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_QUIT, 0, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server", false,
                            &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* エラーメッセージであった場合はその旨を表示 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unexpected message type: %s, %s expected\n",
                ftp_header_type_to_string(header.type),
                ftp_header_type_to_string(FTP_HEADER_TYPE_OK));
    }
    
    /* クライアントを終了 */
    *app_exit = true;

    return true;
}

/*
 * pwdコマンドの処理
 */
bool on_command_pwd(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    struct ftp_header header;
    char* data = NULL;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc != 1) {
        fprintf(stderr, "pwd: too many arguments\n");
        return true;
    }

    /* サーバにPWDコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_PWD, 0, 0, NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server",
                            false, &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* エラーメッセージであった場合 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unable to get current directory of remote server (type: %s, code: %s)\n",
                ftp_header_type_to_string(header.type),
                ftp_header_code_to_string(header.type, header.code));
        return true;
    }

    /* サーバからデータを受信 */
    if (!receive_string_data(context->client_sock, context->server_addr, "server",
                             false, ntohs(header.length), &data)) {
        print_error(__func__,
                    "receive_string_data() failed, "
                    "could not receive string data from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* サーバのカレントディレクトリを表示 */
    fprintf(stderr, "%s\n", data);

    SAFE_FREE(data);

    return true;
}

/*
 * cdコマンドの処理
 */
bool on_command_cd(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    struct ftp_header header;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc != 2) {
        fprintf(stderr, "usage: cd <server current directory>\n");
        return true;
    }

    /* サーバにCWDコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_CWD, 0, strlen(args[1]), args[1])) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server",
                            false, &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* エラーメッセージであった場合 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unable to change current directory of remote server (type: %s, code: %s)\n",
                ftp_header_type_to_string(header.type),
                ftp_header_code_to_string(header.type, header.code));
        return true;
    }

    return true;
}

/*
 * dirコマンドの処理
 */
bool on_command_dir(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    struct ftp_header header;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);
    
    if (argc > 2) {
        fprintf(stderr, "usage: dir [<path>]\n");
        return true;
    }

    /* サーバにLISTコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_LIST, 0,
                         (argc == 2) ? strlen(args[1]) : 0,
                         (argc == 2) ? args[1] : NULL)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server",
                            false, &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    /* エラーメッセージであった場合 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unable to list files on remote server (type: %s, code: %s)\n",
                ftp_header_type_to_string(header.type),
                ftp_header_code_to_string(header.type, header.code));
        return true;
    }

    /* データメッセージを取得 */
    if (!receive_file_data_message(context->client_sock, context->server_addr, "server",
                                   false, STDERR_FILENO)) {
        print_error(__func__,
                    "receive_file_data_message() failed, "
                    "could not receive data from server %s\n",
                    inet_ntoa(context->server_addr));
        return false;
    }

    return true;
}

/*
 * lpwdコマンドの処理
 */
bool on_command_lpwd(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    char buffer[PATH_MAX + 1];

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc != 1) {
        fprintf(stderr, "lpwd: too many arguments\n");
        return true;
    }
    
    /* カレントディレクトリを取得 */
    if (getcwd(buffer, sizeof(buffer)) == NULL) {
        fprintf(stderr, "unable to get current directory\n");
        return true;
    }
    
    /* カレントディレクトリを表示 */
    fprintf(stderr, "%s\n", buffer);

    return true;
}

/*
 * lcdコマンドの処理
 */
bool on_command_lcd(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc != 2) {
        fprintf(stderr, "usage: lcd <local current directory>\n");
        return true;
    }
    
    /* カレントディレクトリを変更 */
    if (chdir(args[1]) < 0) {
        fprintf(stderr, "unable to change current directory\n");
        return true;
    }

    return true;
}

/*
 * ldirコマンドの処理
 */
bool on_command_ldir(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    char* path = NULL;
    char* buffer = NULL;
    int save_errno = 0;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc > 2) {
        fprintf(stderr, "usage: ldir [<path>]\n");
        return true;
    }
    
    /* パスが指定されていない場合はカレントディレクトリを使用 */
    path = (argc == 2) ? args[1] : ".";
    
    /* 指定されたパスの情報を取得 */
    if (!get_list_command_result(&buffer, &save_errno, path, false)) {
        switch (save_errno) {
            case 0:
                fprintf(stderr, "unable to list files due to unknown error\n");
                break;
            default:
                fprintf(stderr, "unable to list files: %s\n", strerror(errno));
                break;
        }
        return true;
    }
    
    /* 指定されたパスの情報を表示 */
    fprintf(stderr, "%s", buffer);
    
    SAFE_FREE(buffer);

    return true;
}

/*
 * getコマンドの処理
 */
bool on_command_get(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    int fd = -1;
    char* path = NULL;
    struct ftp_header header;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: get <server file path> [<client file path>]\n");
        return true;
    }

    /* 書き込み用のファイルパスの設定 */
    path = (argc == 3) ? args[2] : args[1];

    /* 書き込み用のファイルを開く */
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0) {
        print_error(__func__, "could not open file \'%s\': %s\n",
                    path, strerror(errno));
        return true;
    }

    /* サーバにRETRコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_RETR, 0, strlen(args[1]), args[1])) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        
        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server",
                            false, &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* エラーメッセージであった場合 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unable to get file on remote server (type: %s, code: %s)\n",
                ftp_header_type_to_string(header.type),
                ftp_header_code_to_string(header.type, header.code));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return true;
    }
    
    /* ファイルの内容をデータメッセージで受信 */
    if (!receive_file_data_message(context->client_sock, context->server_addr, "server",
                                   false, fd)) {
        print_error(__func__,
                    "receive_file_data_message() failed, "
                    "could not receive data from server %s\n",
                    inet_ntoa(context->server_addr));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* 書き込み用のファイルを閉じる */
    if (close(fd) < 0) {
        print_error(__func__, "close() failed: %s\n", strerror(errno));
        return true;
    }

    return true;
}

/*
 * putコマンドの処理
 */
bool on_command_put(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    int fd = -1;
    char* path = NULL;
    struct ftp_header header;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: put <client file path> [<server file path>]\n");
        return true;
    }
    
    /* 読み込み用のファイルを開く */
    if ((fd = open(args[1], O_RDONLY)) < 0) {
        print_error(__func__, "could not open file \'%s\': %s\n",
                    args[1], strerror(errno));
        return true;
    }
    
    /* 書き込み用のファイルパスの設定 */
    path = (argc == 3) ? args[2] : args[1];

    /* サーバにSTORコマンドを送信 */
    if (!send_ftp_header(context->client_sock, context->server_addr, "server", false,
                         FTP_HEADER_TYPE_STOR, 0, strlen(path), path)) {
        print_error(__func__,
                    "send_ftp_header() failed, could not send ftp header to server %s\n",
                    inet_ntoa(context->server_addr));
        
        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* サーバからリプライメッセージを受信 */
    if (!receive_ftp_header(context->client_sock, context->server_addr, "server",
                            false, &header)) {
        print_error(__func__,
                    "receive_ftp_header() failed, "
                    "could not receive ftp header from server %s\n",
                    inet_ntoa(context->server_addr));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* エラーメッセージであった場合 */
    if (header.type != FTP_HEADER_TYPE_OK) {
        fprintf(stderr,
                "unable to send file to remote server (type: %s, code: %s)\n",
                ftp_header_type_to_string(header.type),
                ftp_header_code_to_string(header.type, header.code));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return true;
    }
    
    /* ファイルの内容をデータメッセージで送信 */
    if (!send_file_data_message(context->client_sock, context->server_addr, "server",
                                false, fd, 1024)) {
        print_error(__func__,
                    "send_file_data_message() failed, "
                    "could not send data to server %s\n",
                    inet_ntoa(context->server_addr));

        if (close(fd) < 0)
            print_error(__func__, "close() failed: %s\n", strerror(errno));

        return false;
    }

    /* 読み込み用のファイルを閉じる */
    if (close(fd) < 0) {
        print_error(__func__, "close() failed: %s\n", strerror(errno));
        return false;
    }

    return true;
}

/*
 * helpコマンドの処理
 */
bool on_command_help(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    return true;
}

/*
 * クライアント上で実行されるコマンドのテーブル
 */
struct client_command_entry client_command_table[] = {
    { "quit",   on_command_quit },
    { "pwd",    on_command_pwd },
    { "cd",     on_command_cd },
    { "dir",    on_command_dir },
    { "lpwd",   on_command_lpwd },
    { "lcd",    on_command_lcd },
    { "ldir",   on_command_ldir },
    { "get",    on_command_get },
    { "put",    on_command_put },
    { "help",   on_command_help },
    { NULL,     NULL },
};

/*
 * クライアント上で実行されるコマンドの関数を返す
 */
client_command_handler lookup_client_command_table(const char* command)
{
    struct client_command_entry* entry;

    for (entry = client_command_table; entry->handler != NULL; ++entry)
        if (!strcmp(entry->name, command))
            return entry->handler;

    return NULL;
}

/*
 * コマンドを実行
 */
bool execute_command(
    struct ftp_client_context* context,
    int argc, char** args, bool* app_exit)
{
    client_command_handler handler = NULL;

    assert(context != NULL);
    assert(argc > 0);
    assert(args != NULL);
    assert(app_exit != NULL);
    
    /* コマンドが見つからない場合はエラー */
    if ((handler = lookup_client_command_table(args[0])) == NULL) {
        print_error(__func__, "unknown command: %s\n", args[0]);
        return true;
    }

    /* コマンドの実行 */
    if (!(*handler)(context, argc, args, app_exit)) {
        print_error(__func__, "failed to execute %s command handler\n", args[0]);
        return false;
    }

    return true;
}

/*
 * クライアントの実行
 */
bool run_ftp_client(struct ftp_client_context* context)
{
    char* input = NULL;
    char** args = NULL;
    int argc = 0;
    bool app_exit = false;

    /* クライアントのソケットの作成 */
    if (!setup_client_socket(context->addr_info, &context->client_sock)) {
        print_error(__func__, "setup_client_socket() failed\n");
        return false;
    }
    
    /* サーバのソケットに接続 */
    if (connect(context->client_sock,
                context->addr_info->ai_addr,
                context->addr_info->ai_addrlen) < 0) {
        print_error(__func__,
                    "connect() failed: %s, unable to connect to server\n",
                    strerror(errno));
        close_socket(context->client_sock);
        return false;
    }

    /* メインループ */
    while (!app_exit) {
        /* プロンプトを表示 */
        fprintf(stderr, "myftp$ ");

        /* ユーザの入力を取得 */
        if ((input = get_line()) == NULL)
            break;
        
        /* 分割後の文字列の個数を取得 */
        if ((argc = split(NULL, input, is_blank_char)) == 0)
            continue;

        if ((args = (char**)calloc(argc, sizeof(char*))) == NULL) {
            print_error(__func__, "calloc() failed: %s\n", strerror(errno));
            SAFE_FREE(input);
            close_socket(context->client_sock);
            return false;
        }
        
        /* 文字列を分割 */
        split(args, input, is_blank_char);
        
        /* コマンドを実行 */
        if (!execute_command(context, argc, args, &app_exit)) {
            print_error(__func__, "execute_command() failed\n");
            SAFE_FREE(args);
            SAFE_FREE(input);
            close_socket(context->client_sock);
            return false;
        }
        
        SAFE_FREE(args);
        SAFE_FREE(input);
    }

    /* クライアントのソケットを閉じる */
    if (!close_socket(context->client_sock)) {
        print_error(__func__, "close_socket() failed\n");
        return false;
    }

    return true;
}

int main(int argc, char** argv)
{
    struct ftp_client_context context;
    bool exit_status;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <server host name>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* クライアントの情報を初期化 */
    if (!initialize_ftp_client_context(&context)) {
        print_error(__func__, "initialize_ftp_client_context() failed\n");
        return EXIT_FAILURE;
    }

    /* サーバのIPアドレスを取得 */
    if (!get_server_addr_info(&context, argv[1])) {
        print_error(__func__, "get_server_addr_info() failed\n");
        free_ftp_client_context(&context);
        return EXIT_FAILURE;
    }
    
    /* クライアントの実行 */
    exit_status = run_ftp_client(&context);

    /* クライアントの情報を破棄 */
    free_ftp_client_context(&context);
    
    return exit_status ? EXIT_SUCCESS : EXIT_FAILURE;
}

