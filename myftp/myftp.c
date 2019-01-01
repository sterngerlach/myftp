
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftp.c */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#include <arpa/inet.h>

#include "dynamic_string.h"
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
            "type: " ANSI_ESCAPE_COLOR_GREEN "%s" ANSI_ESCAPE_COLOR_RESET
            " (%" PRIu8 "), "
            "code: " ANSI_ESCAPE_COLOR_GREEN "%s" ANSI_ESCAPE_COLOR_RESET
            " (%" PRIu8 "), "
            "length: " ANSI_ESCAPE_COLOR_GREEN "%" PRIu16 ANSI_ESCAPE_COLOR_RESET "\n",
            ftp_header_type_to_string(header->type), header->type,
            ftp_header_code_to_string(header->type, header->code), header->code,
            ntohs(header->length));
}

/*
 * FTPヘッダ(先頭の4バイト)を受信
 */
bool receive_ftp_header(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
    struct ftp_header* header)
{
    ssize_t recv_bytes = 0;

    assert(sockfd >= 0);
    assert(name != NULL);
    assert(header != NULL);

    /* FTPヘッダを受信 */
    recv_bytes = recv_all(sockfd, header, sizeof(struct ftp_header));

    /* FTPヘッダの先頭4バイトを受信できなかった場合はエラー */
    if (recv_bytes != sizeof(struct ftp_header)) {
        print_error(__func__,
                    ANSI_ESCAPE_COLOR_RED
                    "could not receive full ftp header\n"
                    ANSI_ESCAPE_COLOR_RESET);
        return false;
    }
    
    /* メッセージを表示 */
    if (verbose) {
        print_message(__func__, "ftp header received from %s %s: ",
                      name, inet_ntoa(addr));
        dump_ftp_header(header);
    }

    return true;
}

/*
 * 文字列データを受信
 */
bool receive_string_data(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
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
    (*data)[len] = '\0';
    
    if (verbose)
        print_message(__func__, "string data (%zd bytes) received from %s %s\n",
                      recv_bytes, name, inet_ntoa(addr));

    return true;
}

/*
 * 生データ(バイト列)を受信
 */
bool receive_raw_data(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
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
    
    if (verbose)
        print_message(__func__, "raw data (%zd bytes) received from %s %s\n",
                      recv_bytes, name, inet_ntoa(addr));

    return true;
}

/*
 * ファイルをデータメッセージで受信
 */
bool receive_file_data_message(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
    int fd)
{
    char* buffer = NULL;
    ssize_t recv_bytes = 0;
    ssize_t written_bytes = 0;
    ssize_t data_length = 0;
    ssize_t total_length = 0;
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
        if (header.code != FTP_HEADER_CODE_DATA_REMAINING &&
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
    
        if (verbose) {
            print_message(__func__, "ftp data message received from %s %s: ",
                          name, inet_ntoa(addr));
            dump_ftp_header(&header);
        }

        /* データ長を取得 */
        data_length = ntohs(header.length);
        total_length += data_length;

        /* ファイルの一部を受信 */
        if (!receive_raw_data(sockfd, addr, name, verbose, data_length, &buffer)) {
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

    if (verbose)
        print_message(__func__,
                      "%zd bytes received from %s %s and written to file (fd: %d)\n",
                      total_length, name, inet_ntoa(addr), fd);

    return true;
}

/*
 * メッセージを送信
 */
bool send_ftp_header(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
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
    send_bytes = send_all(sockfd, header, header_length);

    /* 送信できなかった場合はエラー */
    if (send_bytes != header_length) {
        print_error(__func__,
                    "send_all() failed, could not send full ftp header to %s %s\n",
                    name, inet_ntoa(addr));
        SAFE_FREE(header);
        return false;
    }

    /* メッセージを表示 */
    if (verbose) {
        print_message(__func__, "ftp header has been sent to %s %s: ",
                      name, inet_ntoa(addr));
        dump_ftp_header(header);
    }

    /* メッセージを解放 */
    SAFE_FREE(header);

    return true;
}

/*
 * データメッセージを送信
 */
bool send_data_message(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
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
        send_bytes = send_all(sockfd, header, header_length);
        
        /* 送信できなかった場合はエラー */
        if (send_bytes != header_length) {
            print_error(__func__,
                        "send_all() failed, could not send full ftp header to %s %s\n",
                        name, inet_ntoa(addr));
            SAFE_FREE(header);
            return false;
        }

        if (verbose) {
            print_message(__func__, "ftp data message sent to %s %s: ",
                          name, inet_ntoa(addr));
            dump_ftp_header(header);
        }

        /* メッセージを解放 */
        SAFE_FREE(header);

        /* 残りのバイト数を更新 */
        remaining_bytes -= data_length;
    } while (remaining_bytes > 0);

    if (verbose)
        print_message(__func__, "raw data (%zd bytes) has been sent to %s %s\n",
                      len, name, inet_ntoa(addr));

    return true;
}

/*
 * ファイルをデータメッセージで送信
 */
bool send_file_data_message(
    int sockfd, struct in_addr addr, const char* name, bool verbose,
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
        send_bytes = send_all(sockfd, header, header_length);

        /* 送信できなかった場合はエラー */
        if (send_bytes != header_length) {
            print_error(__func__,
                        "send_all() failed, could not send full ftp header to %s %s\n",
                        name, inet_ntoa(addr));
            SAFE_FREE(header);
            SAFE_FREE(buffer);
            return false;
        }

        if (verbose) {
            print_message(__func__, "ftp data message sent to %s %s: ",
                          name, inet_ntoa(addr));
            dump_ftp_header(header);
        }

        /* メッセージを解放 */
        SAFE_FREE(header);
    } while (remaining_bytes > 0);

    if (verbose)
        print_message(__func__, "file data (%zd bytes) has been sent to %s %s\n",
                      file_size, name, inet_ntoa(addr));

    /* バッファを解放 */
    SAFE_FREE(buffer);

    return true;
}

/*
 * ファイルの属性を文字列に変換
 */
bool get_file_stat_string(
    char** buffer, int* save_errno,
    const struct stat* stat_buf, const char* file_name)
{
    char file_mode_str[11];
    struct passwd* pwd_owner = NULL;
    struct group* grp_owner = NULL;
    char* owner_user = NULL;
    char* owner_group = NULL;
    struct tm mtime;
    char* format;
    int len;

    assert(buffer != NULL);
    assert(save_errno != NULL);
    assert(stat_buf != NULL);
    assert(file_name != NULL);

    *buffer = NULL;

    /* ファイルモードを文字列に変換 */
    file_mode_str[0] = S_ISREG(stat_buf->st_mode) ? '-' :
                       S_ISDIR(stat_buf->st_mode) ? 'd' : '?';
    file_mode_str[1] = (stat_buf->st_mode & S_IRUSR) ? 'r' : '-';
    file_mode_str[2] = (stat_buf->st_mode & S_IWUSR) ? 'w' : '-';
    file_mode_str[3] = (stat_buf->st_mode & S_IXUSR) ? 'x' :
                       (stat_buf->st_mode & S_ISUID) ? 's' : '-';
    file_mode_str[4] = (stat_buf->st_mode & S_IRGRP) ? 'r' : '-';
    file_mode_str[5] = (stat_buf->st_mode & S_IWGRP) ? 'w' : '-';
    file_mode_str[6] = (stat_buf->st_mode & S_IXGRP) ? 'x' :
                       (stat_buf->st_mode & S_ISGID) ? 's' : '-';
    file_mode_str[7] = (stat_buf->st_mode & S_IROTH) ? 'r' : '-';
    file_mode_str[8] = (stat_buf->st_mode & S_IWOTH) ? 'w' : '-';
    file_mode_str[9] = (stat_buf->st_mode & S_IXOTH) ? 'x' :
                       (stat_buf->st_mode & S_ISVTX) ? 't' : '-';
    file_mode_str[10] = '\0';
    
    /* 所有者名を取得 */
    if ((pwd_owner = getpwuid(stat_buf->st_uid)) == NULL) {
        *save_errno = errno;
        print_error(__func__, "getpwuid() failed: %s\n", strerror(errno));
        return false;
    }

    /* 所有者名をコピー */
    if ((owner_user = strdup(pwd_owner->pw_name)) == NULL) {
        *save_errno = errno;
        print_error(__func__, "strdup() failed: %s\n", strerror(errno));
        return false;
    }

    /* 所有グループ名を取得 */
    if ((grp_owner = getgrgid(stat_buf->st_gid)) == NULL) {
        *save_errno = errno;
        print_error(__func__, "getgrgid() failed: %s\n", strerror(errno));
        SAFE_FREE(owner_user);
        return false;
    }

    /* 所有グループ名をコピー */
    if ((owner_group = strdup(grp_owner->gr_name)) == NULL) {
        *save_errno = errno;
        print_error(__func__, "strdup() failed: %s\n", strerror(errno));
        SAFE_FREE(owner_user);
        return false;
    }

    /* ファイルの最終変更時刻を時刻要素別に変換 */
    if (localtime_r(&stat_buf->st_mtime, &mtime) == NULL) {
        print_error(__func__, "localtime_r() failed\n");
        SAFE_FREE(owner_user);
        SAFE_FREE(owner_group);
        return false;
    }

    /* ファイルの属性を文字列に変換 */
    format = "%s %" PRIuMAX " %s %s %" PRIuMAX " %d-%d-%d %d:%d:%d %s%s\n";
    len = snprintf(NULL, 0, format,
                   file_mode_str, (uintmax_t)stat_buf->st_nlink,
                   owner_user, owner_group, (uintmax_t)stat_buf->st_size,
                   mtime.tm_year + 1900, mtime.tm_mon + 1, mtime.tm_mday,
                   mtime.tm_hour, mtime.tm_min, mtime.tm_sec,
                   file_name, S_ISDIR(stat_buf->st_mode) ? "/" : "");
    
    if (len < 0) {
        print_error(__func__, "snprintf() failed\n");
        SAFE_FREE(owner_user);
        SAFE_FREE(owner_group);
        return false;
    }

    *buffer = (char*)calloc(len + 1, sizeof(char));
    
    if (*buffer == NULL) {
        *save_errno = errno;
        print_error(__func__, "calloc() failed: %s\n", strerror(errno));
        SAFE_FREE(owner_user);
        SAFE_FREE(owner_group);
        return false;
    }
    
    if (snprintf(*buffer, len + 1, format,
                 file_mode_str, (uintmax_t)stat_buf->st_nlink,
                 owner_user, owner_group, (uintmax_t)stat_buf->st_size,
                 mtime.tm_year + 1900, mtime.tm_mon + 1, mtime.tm_mday,
                 mtime.tm_hour, mtime.tm_min, mtime.tm_sec,
                 file_name, S_ISDIR(stat_buf->st_mode) ? "/" : "") < 0) {
        print_error(__func__, "snprintf() failed\n");
        SAFE_FREE(*buffer);
        SAFE_FREE(owner_user);
        SAFE_FREE(owner_group);
        return false;
    }
    
    SAFE_FREE(owner_user);
    SAFE_FREE(owner_group);
    
    return true;
}

/*
 * ファイルの情報を取得して文字列に変換
 */
bool get_list_command_result(
    char** buffer, int* save_errno, const char* path, bool verbose)
{
    struct stat stat_buf;
    struct dynamic_string dyn_str;
    DIR* dirp = NULL;
    struct dirent* ent = NULL;
    char* tmp_buffer = NULL;

    assert(buffer != NULL);
    assert(save_errno != NULL);
    assert(path != NULL);

    *buffer = NULL;
    
    /* 指定されたファイルの属性を取得 */
    if (stat(path, &stat_buf) < 0) {
        *save_errno = errno;

        if (verbose)
            print_error(__func__, "stat() failed: %s\n", strerror(errno));

        return false;
    }

    /* ファイルの種別を判定 */
    /* 通常のファイルである場合 */
    if (S_ISREG(stat_buf.st_mode)) {
        /* ファイルの属性を文字列に変換 */
        if (!get_file_stat_string(buffer, save_errno, &stat_buf, path)) {
            print_error(__func__, "get_file_stat_string() failed\n");
            return false;
        }

        return true;
    }
    
    /* ディレクトリである場合 */
    if (S_ISDIR(stat_buf.st_mode)) {
        /* 動的文字列を初期化 */
        if (!initialize_dynamic_string(&dyn_str)) {
            print_error(__func__, "initialize_dynamic_string() failed\n");
            return false;
        }
        
        /* ディレクトリを開く */
        if ((dirp = opendir(path)) == NULL) {
            *save_errno = errno;

            if (verbose)
                print_error(__func__, "opendir() failed: %s\n", strerror(errno));

            free_dynamic_string(&dyn_str);
            return false;
        }
        
        /* ディレクトリに含まれるファイルを走査 */
        while ((ent = readdir(dirp)) != NULL) {
            /* ファイルの属性を取得 */
            if (stat(ent->d_name, &stat_buf) < 0) {
                *save_errno = errno;

                if (verbose)
                    print_error(__func__, "stat() failed: %s\n", strerror(errno));

                continue;
            }

            /* ファイルの属性を文字列に変換 */
            if (!get_file_stat_string(&tmp_buffer, save_errno, &stat_buf, ent->d_name)) {
                print_error(__func__, "get_file_stat_string() failed\n");
                continue;
            }
            
            /* ファイルの属性を表す文字列を追加 */
            if (!dynamic_string_append(&dyn_str, tmp_buffer)) {
                print_error(__func__, "dynamic_string_append() failed\n");
                SAFE_FREE(tmp_buffer);
                free_dynamic_string(&dyn_str);
                return false;
            }

            SAFE_FREE(tmp_buffer);
        }

        /* ディレクトリを閉じる */
        if (closedir(dirp) < 0) {
            *save_errno = errno;
            print_error(__func__, "closedir() failed: %s\n", strerror(errno));
            free_dynamic_string(&dyn_str);
            return false;
        }

        /* ファイルの属性を表す文字列を返す */
        *buffer = move_dynamic_string(&dyn_str);
        
        return true;
    }

    /* それ以外である場合はエラー */
    *save_errno = ENOENT;

    if (verbose)
        print_error(__func__, "no such file or directory: %s\n", path);

    return false;
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
    
    if (type > FTP_HEADER_TYPE_DATA)
        return "Unknown";

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

    if (type > FTP_HEADER_TYPE_DATA)
        return "Unknown";

    if ((type == FTP_HEADER_TYPE_OK) ?
        (code > FTP_HEADER_CODE_OK_CLIENT_DATA_REMAINING) :
        (type == FTP_HEADER_TYPE_CMD_ERR) ?
        (code > FTP_HEADER_CODE_CMD_ERR_PROTOCOL) :
        (type == FTP_HEADER_TYPE_FILE_ERR) ?
        (code > FTP_HEADER_CODE_FILE_ERR_PERM_ERR) :
        (type == FTP_HEADER_TYPE_UNKWN_ERR) ?
        (code > FTP_HEADER_CODE_UNKWN_ERR) :
        (type == FTP_HEADER_TYPE_DATA) ?
        (code > FTP_HEADER_CODE_DATA_REMAINING) : false)
        return "Unknown";

    return header_code_str[type] ?
           header_code_str[type][code] ?
           header_code_str[type][code] : "Unknown" : "Unknown";
}

