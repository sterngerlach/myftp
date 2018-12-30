
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* myftp.h */

#ifndef MYFTP_H
#define MYFTP_H

#include <stdint.h>

/*
 * サーバが使用するポート番号
 */
#define FTP_SERVER_PORT                             50021

/*
 * クライアントの接続要求を保持する待ち行列の長さ
 */
#define FTP_SERVER_LISTEN_BACKLOG                   5

/*
 * 疑似FTPヘッダを表す構造体
 * dataはフレキシブル配列メンバ(不完全配列型)
 * dataメンバよりも後ろに, 新たなメンバを追加してはならない
 */
struct ftp_header {
    uint8_t     type;
    uint8_t     code;
    uint16_t    length;
    char        data[];
};

/*
 * FTPヘッダのTypeフィールドを表す各種定数
 */
#define FTP_HEADER_TYPE_NONE                        0x00

/*
 * コマンドメッセージのTypeフィールド
 */
#define FTP_HEADER_TYPE_QUIT                        0x01
#define FTP_HEADER_TYPE_PWD                         0x02
#define FTP_HEADER_TYPE_CWD                         0x03
#define FTP_HEADER_TYPE_LIST                        0x04
#define FTP_HEADER_TYPE_RETR                        0x05
#define FTP_HEADER_TYPE_STOR                        0x06

/*
 * リプライメッセージのTypeフィールド
 */
#define FTP_HEADER_TYPE_OK                          0x10
#define FTP_HEADER_TYPE_CMD_ERR                     0x11
#define FTP_HEADER_TYPE_FILE_ERR                    0x12
#define FTP_HEADER_TYPE_UNKWN_ERR                   0x13

/*
 * データメッセージのTypeフィールド
 */
#define FTP_HEADER_TYPE_DATA                        0x20

/*
 * FTPヘッダのCodeフィールドを表す各種定数
 */
#define FTP_HEADER_CODE_OK                          0x00
#define FTP_HEADER_CODE_OK_SERVER_DATA_REMAINING    0x01
#define FTP_HEADER_CODE_OK_CLIENT_DATA_REMAINING    0x02
#define FTP_HEADER_CODE_CMD_ERR_SYNTAX              0x01
#define FTP_HEADER_CODE_CMD_ERR_UNKNOWN_CMD         0x02
#define FTP_HEADER_CODE_CMD_ERR_PROTOCOL            0x03
#define FTP_HEADER_CODE_FILE_ERR_NOT_EXIST          0x00
#define FTP_HEADER_CODE_FILE_ERR_PERM_ERR           0x01
#define FTP_HEADER_CODE_UNKWN_ERR                   0x05
#define FTP_HEADER_CODE_DATA_NOT_REMAINING          0x00
#define FTP_HEADER_CODE_DATA_REMAINING              0x01

/*
 * 疑似FTPヘッダを作成
 */
struct ftp_header* create_ftp_header(
    uint8_t type, uint8_t code,
    uint16_t length, char* data);

/*
 * 疑似FTPヘッダを出力
 */
void dump_ftp_header(const struct ftp_header* header);

/*
 * 文字列データを受信
 */
bool receive_string_data(
    int sockfd, struct in_addr addr, const char* name,
    uint16_t len, char** data);

/*
 * 生データ(バイト列)を受信
 */
bool receive_raw_data(
    int sockfd, struct in_addr addr, const char* name,
    uint16_t len, char** data);

/*
 * ファイルをデータメッセージで受信
 */
bool receive_file_data_message(
    int sockfd, struct in_addr addr, const char* name,
    int fd);

/*
 * メッセージを送信
 */
bool send_ftp_header(
    int sockfd, struct in_addr addr, const char* name,
    uint8_t type, uint8_t code,
    uint16_t len, char* buf);

/*
 * データメッセージを送信
 */
bool send_data_message(
    int sockfd, struct in_addr addr, const char* name,
    char* buf, ssize_t len, ssize_t chunk_size);

/*
 * ファイルをデータメッセージで送信
 */
bool send_file_data_message(
    int sockfd, struct in_addr addr, const char* name,
    int fd, ssize_t chunk_size);

/*
 * FTPヘッダのTypeフィールドを表す定数を文字列に変換
 */
const char* ftp_header_type_to_string(uint8_t type);

/*
 * FTPヘッダのCodeフィールドを表す定数を文字列に変換
 */
const char* ftp_header_code_to_string(uint8_t type, uint8_t code);

#endif /* MYFTP_H */

