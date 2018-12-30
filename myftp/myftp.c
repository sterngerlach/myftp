
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftp.c */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "myftp.h"
#include "util.h"

/*
 * 疑似FTPヘッダを作成
 */
struct ftp_header* create_ftp_header(
    uint8_t type, uint8_t code, uint16_t length, char* data)
{
    struct ftp_header* header;
    
    /* FTPヘッダを格納するメモリを動的確保 */
    header = (struct ftp_header*)malloc(sizeof(struct ftp_header) + 
                                        sizeof(char) * length);

    if (header == NULL) {
        print_error(__func__,
                    "malloc() failed: %s, "
                    "unable to allocate sufficient memory for 'data' field\n");
        return NULL;
    }
    
    /* FTPヘッダの各フィールドを設定 */
    header->type = type;
    header->code = code;
    header->length = htons(length);

    if (length > 0)
        memcpy(header->data, data, length);

    return header;
}

/*
 * 疑似FTPヘッダを出力
 */
void dump_ftp_header(const struct ftp_header* header)
{
    assert(header != NULL);

    fprintf(stderr,
            "type: " ANSI_ESCAPE_COLOR_GREEN "%s" ANSI_ESCAPE_COLOR_RESET ", "
            "code: " ANSI_ESCAPE_COLOR_GREEN "%s" ANSI_ESCAPE_COLOR_RESET ", "
            "length: " ANSI_ESCAPE_COLOR_GREEN "%" PRIu16 ANSI_ESCAPE_COLOR_RESET "\n",
            ftp_header_type_to_string(header->type),
            ftp_header_code_to_string(header->type, header->code),
            ntohs(header->length));
}

/*
 * 文字列データを受信
 */
bool receive_string_data(
    int sockfd, struct in_addr addr, const char* name,
    uint16_t len, char** data)
{
    ssize_t recv_bytes = 0;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(data != NULL);

    /*
     * receive_raw_data()関数とは異なり, 末尾がヌル終端される
     */

    /* データを受け取るバッファを確保 */
    *data = (char*)calloc(len + 1, sizeof(char));

    if (*data == NULL) {
        print_error(__func__,
                    "calloc() failed: %s, "
                    "unable to allocate sufficient memory for 'data' field\n",
                    strerror(errno));
        return false;
    }

    /* 文字列データを受信 */
    recv_bytes = recv_all(sockfd, *data, sizeof(char) * len);

    /* 全て受信できなかった場合はエラー */
    if (recv_bytes < (ssize_t)(sizeof(char) * len)) {
        print_error(__func__,
                    "recv_all() failed, unable to receive full data, "
                    "expected %zu bytes, but received only %zd bytes\n",
                    sizeof(char) * len, recv_bytes);
        SAFE_FREE(*data);
        return false;
    }

    /* 末尾をヌル文字で終端 */
    data[len] = '\0';

    print_message(__func__, "string data (%zd bytes) received from %s %s\n",
                  recv_bytes, name, inet_ntoa(addr));

    return true;
}

/*
 * 生データ(バイト列)を受信
 */
bool receive_raw_data(
    int sockfd, struct in_addr addr, const char* name,
    uint16_t len, char** data)
{
    ssize_t recv_bytes = 0;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(data != NULL);
    
    /* 受信バイト数が0であれば常に成功 */
    if (len == 0)
        return true;

    /* データを受け取るバッファを確保 */
    *data = (char*)calloc(len, sizeof(char));

    if (*data == NULL) {
        print_error(__func__,
                    "calloc() failed: %s, "
                    "unable to allocate sufficient memory for 'data' field\n",
                    strerror(errno));
        return false;
    }

    /* バイト列データを受信 */
    recv_bytes = recv_all(sockfd, *data, sizeof(char) * len);

    /* 全て受信できなかった場合はエラー */
    if (recv_bytes < (ssize_t)(sizeof(char) * len)) {
        print_error(__func__,
                    "recv_all() failed, unable to receive full data, "
                    "expected %zu bytes, but received only %zd bytes\n",
                    sizeof(char) * len, recv_bytes);
        SAFE_FREE(*data);
        return false;
    }

    print_message(__func__, "raw data (%zd bytes) received from %s %s\n",
                  recv_bytes, name, inet_ntoa(addr));

    return true;
}

/*
 * ファイルをデータメッセージで受信
 */
bool receive_file_data_message(
    int sockfd, struct in_addr addr, const char* name,
    int fd)
{
    char* buffer = NULL;
    ssize_t recv_bytes = 0;
    ssize_t written_bytes = 0;
    ssize_t data_length = 0;
    struct ftp_header header;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(fd >= 0);

    /*
     * 1つ以上のデータメッセージが到着することが前提の実装
     * サイズ0のファイルが転送される場合も, 必ず1つのデータメッセージ
     * (codeフィールドが0x00, lengthフィールドが0)が届く
     */
    
    do {
        /* FTPヘッダを受信 */
        recv_bytes = recv_all(sockfd, &header, sizeof(struct ftp_header));

        /* 最低限のFTPヘッダを受信できなかった場合はエラー */
        if (recv_bytes != sizeof(struct ftp_header)) {
            print_error(__func__,
                        "recv_all() failed, could not receive full ftp header from %s %s\n",
                        name, inet_ntoa(addr));
            return false;
        }

        /* データメッセージでない場合はエラー */
        if (header.type != FTP_HEADER_TYPE_DATA) {
            print_error(__func__,
                        "invalid message type: %s, %s expected\n",
                        ftp_header_type_to_string(header.type),
                        ftp_header_type_to_string(FTP_HEADER_TYPE_DATA));
            return false;
        }

        /* codeフィールドが誤っている場合はエラー */
        if (header.code != FTP_HEADER_CODE_DATA_REMAINING ||
            header.code != FTP_HEADER_CODE_DATA_NOT_REMAINING) {
            print_error(__func__,
                        "invalid message code: %" PRIu8 ", "
                        "%" PRIu8 " (%s) or %" PRIu8 " (%s) expected\n",
                        header.code,
                        FTP_HEADER_CODE_DATA_REMAINING,
                        ftp_header_code_to_string(FTP_HEADER_TYPE_DATA,
                                                  FTP_HEADER_CODE_DATA_REMAINING),
                        FTP_HEADER_CODE_DATA_NOT_REMAINING,
                        ftp_header_code_to_string(FTP_HEADER_TYPE_DATA,
                                                  FTP_HEADER_CODE_DATA_NOT_REMAINING));
            return false;
        }

        /* データ長を取得 */
        data_length = ntohs(header.length);

        /* ファイルの一部を受信 */
        if (!receive_raw_data(sockfd, addr, name, data_length, &buffer)) {
            print_error(__func__, "receive_raw_data() failed\n");
            return false;
        }

        /* ファイルに受信データを書き込み */
        written_bytes = write(fd, buffer, data_length);
        
        /* 全て書き込めなかった場合はエラー */
        if (written_bytes != data_length) {
            print_error(__func__, "write() failed: %s\n", strerror(errno));
            SAFE_FREE(buffer);
            return false;
        }

        /* 受信したデータを解放 */
        SAFE_FREE(buffer);
    } while (header.code == FTP_HEADER_CODE_DATA_REMAINING);

    return true;
}

/*
 * メッセージを送信
 */
bool send_ftp_header(
    int sockfd, struct in_addr addr, const char* name,
    uint8_t type, uint8_t code,
    uint16_t len, char* buf)
{
    struct ftp_header* header = NULL;
    ssize_t send_bytes = 0;
    ssize_t header_length = 0;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert((len == 0 && buf == NULL) || (len > 0 && buf != NULL));

    /* メッセージの作成 */
    header = create_ftp_header(type, code, len, buf);
    
    /* 作成できなかった場合はエラー */
    if (header == NULL) {
        print_error(__func__, "create_ftp_header() failed\n");
        return false;
    }

    /* メッセージを送信 */
    header_length = sizeof(struct ftp_header) + sizeof(char) * len;
    send_bytes = send_all(sockfd, &header, header_length);

    /* 送信できなかった場合はエラー */
    if (send_bytes != header_length) {
        print_error(__func__,
                    "send_all() failed, could not send full ftp header to %s %s\n",
                    name, inet_ntoa(addr));
        SAFE_FREE(header);
        return false;
    }

    /* メッセージを表示 */
    print_message(__func__, "ftp header has been sent to %s %s: ",
                  name, inet_ntoa(addr));
    dump_ftp_header(header);

    /* メッセージを解放 */
    SAFE_FREE(header);

    return true;
}

/*
 * データメッセージを送信
 */
bool send_data_message(
    int sockfd, struct in_addr addr, const char* name,
    char* buf, ssize_t len, ssize_t chunk_size)
{
    struct ftp_header* header = NULL;
    ssize_t remaining_bytes = len;
    ssize_t send_bytes = 0;
    ssize_t data_length = 0;
    ssize_t header_length = 0;
    uint8_t code;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(buf != NULL);
    assert(len >= 0);
    assert(chunk_size > 0);

    /*
     * データのサイズが0であっても, 必ず1つ以上のデータメッセージ
     * (codeフールドが0x01, lengthフィールドが0)が送信される
     */
    
    do {
        /* メッセージの種類とサイズを設定 */
        code = remaining_bytes > chunk_size ?
               FTP_HEADER_CODE_DATA_REMAINING :
               FTP_HEADER_CODE_DATA_NOT_REMAINING;
        data_length = min(remaining_bytes, chunk_size);

        /* メッセージの作成 */
        header = create_ftp_header(FTP_HEADER_TYPE_DATA, code,
                                   data_length, buf + len - remaining_bytes);

        /* 作成できなかった場合はエラー */
        if (header == NULL) {
            print_error(__func__, "create_ftp_header() failed\n");
            return false;
        }
        
        /* メッセージを送信 */
        header_length = sizeof(struct ftp_header) + sizeof(char) * data_length;
        send_bytes = send_all(sockfd, &header, header_length);
        
        /* 送信できなかった場合はエラー */
        if (send_bytes != header_length) {
            print_error(__func__,
                        "send_all() failed, could not send full ftp header to %s %s\n",
                        name, inet_ntoa(addr));
            SAFE_FREE(header);
            return false;
        }

        /* メッセージを解放 */
        SAFE_FREE(header);

        /* 残りのバイト数を更新 */
        remaining_bytes -= send_bytes;
    } while (remaining_bytes > 0);

    return true;
}

/*
 * ファイルをデータメッセージで送信
 */
bool send_file_data_message(
    int sockfd, struct in_addr addr, const char* name,
    int fd, ssize_t chunk_size)
{
    struct ftp_header* header = NULL;
    off_t file_size = 0;
    char* buffer = NULL;
    ssize_t remaining_bytes = 0;
    ssize_t send_bytes = 0;
    ssize_t data_length = 0;
    ssize_t header_length = 0;
    uint8_t code;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(fd >= 0);
    assert(chunk_size > 0);

    /* ファイルの内容を格納するバッファを確保 */
    if ((buffer = (char*)calloc(chunk_size, sizeof(char))) == NULL) {
        print_error(__func__, "calloc() failed: %s\n", strerror(errno));
        return false;
    }
    
    /* ファイルの末尾に移動してサイズを調べる */
    if ((file_size = lseek(fd, 0, SEEK_END)) < 0) {
        print_error(__func__, "lseek() failed: %s\n", strerror(errno));
        SAFE_FREE(buffer);
        return false;
    }

    /* 残りのバイト数を設定 */
    remaining_bytes = file_size;

    /* ファイルの先頭に移動 */
    if (lseek(fd, 0, SEEK_SET) < 0) {
        print_error(__func__, "lseek() failed: %s\n", strerror(errno));
        SAFE_FREE(buffer);
        return false;
    }

    do {
        /* ファイルの内容をバッファに読み込み */
        data_length = read(fd, buffer, chunk_size);
        
        if (data_length < 0) {
            print_error(__func__, "read() failed: %s\n", strerror(errno));
            SAFE_FREE(buffer);
            return false;
        }

        if (data_length == 0 && remaining_bytes > 0) {
            print_error(__func__, "read() failed: %s\n", strerror(errno));
            SAFE_FREE(buffer);
            return false;
        }
        
        /* 残りのバイト数を更新 */
        remaining_bytes -= data_length;

        /* メッセージの種類を判定 */
        code = remaining_bytes > 0 ? FTP_HEADER_CODE_DATA_REMAINING :
                                     FTP_HEADER_CODE_DATA_NOT_REMAINING;

        /* メッセージの作成 */
        header = create_ftp_header(FTP_HEADER_TYPE_DATA, code,
                                   data_length, buffer);

        /* 作成できなかった場合はエラー */
        header_length = sizeof(struct ftp_header) + sizeof(char) * data_length;
        send_bytes = send_all(sockfd, &header, header_length);

        /* 送信できなかった場合はエラー */
        if (send_bytes != header_length) {
            print_error(__func__,
                        "send_all() failed, could not send full ftp header to %s %s\n",
                        name, inet_ntoa(addr));
            SAFE_FREE(header);
            SAFE_FREE(buffer);
            return false;
        }

        /* メッセージを解放 */
        SAFE_FREE(header);
    } while (remaining_bytes > 0);

    /* バッファを解放 */
    SAFE_FREE(buffer);

    return true;
}

/*
 * FTPヘッダのTypeフィールドを表す定数を文字列に変換
 */
const char* ftp_header_type_to_string(uint8_t type)
{
    static const char* header_type_str[] = {
        [FTP_HEADER_TYPE_QUIT]      = "QUIT",
        [FTP_HEADER_TYPE_PWD]       = "PWD",
        [FTP_HEADER_TYPE_CWD]       = "CWD",
        [FTP_HEADER_TYPE_LIST]      = "LIST",
        [FTP_HEADER_TYPE_RETR]      = "RETR",
        [FTP_HEADER_TYPE_STOR]      = "STOR",
        [FTP_HEADER_TYPE_OK]        = "OK",
        [FTP_HEADER_TYPE_CMD_ERR]   = "CMD_ERR",
        [FTP_HEADER_TYPE_FILE_ERR]  = "FILE_ERR",
        [FTP_HEADER_TYPE_UNKWN_ERR] = "UNKWN_ERR",
        [FTP_HEADER_TYPE_DATA]      = "DATA",
    };
    
    assert(type <= FTP_HEADER_TYPE_DATA);

    return header_type_str[type] ?
           header_type_str[type] : "Unknown";
}

/*
 * FTPヘッダのCodeフィールドを表す定数を文字列に変換
 */
const char* ftp_header_code_to_string(uint8_t type, uint8_t code)
{
    static const char* header_code_ok_str[] = {
        [FTP_HEADER_CODE_OK]                        = "OK",
        [FTP_HEADER_CODE_OK_SERVER_DATA_REMAINING]  = "OK_SERVER_DATA_REMAINING",
        [FTP_HEADER_CODE_OK_CLIENT_DATA_REMAINING]  = "OK_CLIENT_DATA_REMAINING",
    };

    static const char* header_code_cmd_err_str[] = {
        [FTP_HEADER_CODE_CMD_ERR_SYNTAX]            = "CMD_ERR_SYNTAX",
        [FTP_HEADER_CODE_CMD_ERR_UNKNOWN_CMD]       = "CMD_ERR_UNKNOWN_CMD",
        [FTP_HEADER_CODE_CMD_ERR_PROTOCOL]          = "CMD_ERR_PROTOCOL",
    };

    static const char* header_code_file_err_str[] = {
        [FTP_HEADER_CODE_FILE_ERR_NOT_EXIST]        = "FILE_ERR_NOT_EXIST",
        [FTP_HEADER_CODE_FILE_ERR_PERM_ERR]         = "FILE_ERR_PERM_ERR",
    };

    static const char* header_code_unkwn_err_str[] = {
        [FTP_HEADER_CODE_UNKWN_ERR]                 = "UNKWN_ERR",
    };

    static const char* header_code_data_str[] = {
        [FTP_HEADER_CODE_DATA_NOT_REMAINING]        = "DATA_NOT_REMAINING",
        [FTP_HEADER_CODE_DATA_REMAINING]            = "DATA_REMAINING",
    };

    static const char** header_code_str[] = {
        [FTP_HEADER_TYPE_OK]        = header_code_ok_str,
        [FTP_HEADER_TYPE_CMD_ERR]   = header_code_cmd_err_str,
        [FTP_HEADER_TYPE_FILE_ERR]  = header_code_file_err_str,
        [FTP_HEADER_TYPE_UNKWN_ERR] = header_code_unkwn_err_str,
        [FTP_HEADER_TYPE_DATA]      = header_code_data_str,
    };

    assert(type <= FTP_HEADER_TYPE_DATA);
    assert((type == FTP_HEADER_TYPE_OK) ?
           (code <= FTP_HEADER_CODE_OK_CLIENT_DATA_REMAINING) :
           (type == FTP_HEADER_TYPE_CMD_ERR) ?
           (code <= FTP_HEADER_CODE_CMD_ERR_PROTOCOL) :
           (type == FTP_HEADER_TYPE_FILE_ERR) ?
           (code <= FTP_HEADER_CODE_FILE_ERR_PERM_ERR) :
           (type == FTP_HEADER_TYPE_UNKWN_ERR) ?
           (code <= FTP_HEADER_CODE_UNKWN_ERR) :
           (type == FTP_HEADER_TYPE_DATA) ?
           (code <= FTP_HEADER_CODE_DATA_REMAINING) : true);

    return header_code_str[type] ?
           header_code_str[type][code] ?
           header_code_str[type][code] : "Unknown" : "Unknown";
}

