
/* 情報工学科3年 学籍番号61610117 杉浦 圭祐 */
/* dynamic_string.h */

#ifndef DYNAMIC_STRING_H
#define DYNAMIC_STRING_H

#include <stdbool.h>
#include <stdarg.h>

/*
 * 動的な文字列を表す構造体
 * バッファのサイズが不足する場合は自動的に拡張される
 */
struct dynamic_string {
    char*   buffer;          /* 文字列を格納するバッファ */
    size_t  length;          /* ヌル文字を除いた文字列の長さ */
    size_t  capacity_buffer; /* 文字列を格納するバッファのサイズ */
};

/*
 * 文字列を初期化
 */
bool initialize_dynamic_string(struct dynamic_string* dyn_str);

/*
 * 文字列を破棄
 */
void free_dynamic_string(struct dynamic_string* dyn_str);

/*
 * 文字列の所有権を移動(ムーブ)
 */
char* move_dynamic_string(struct dynamic_string* dyn_str);

/*
 * 文字列を末尾に追加
 */
bool dynamic_string_append(
    struct dynamic_string* dyn_str, const char* str);

/*
 * 部分文字列を末尾に追加
 */
bool dynamic_string_append_substring(
    struct dynamic_string* dyn_str, const char* str,
    size_t index, size_t len);

/*
 * 単一の文字を末尾に追加
 */
bool dynamic_string_append_char(struct dynamic_string* dyn_str, char c);

/*
 * 単一の文字を指定された位置に追加
 */
bool dynamic_string_insert_char(
    struct dynamic_string* dyn_str, char c, size_t index);

/*
 * 文字列を指定された位置に追加
 */
bool dynamic_string_insert(
    struct dynamic_string* dyn_str, const char* str, size_t index);

/*
 * 指定されたインデックスの文字を削除
 */
bool dynamic_string_remove_at(
    struct dynamic_string* dyn_str, size_t index);

/*
 * 文字列を全て削除
 */
bool dynamic_string_remove_all(struct dynamic_string* dyn_str);

/*
 * 指定された範囲の文字列を削除
 */
bool dynamic_string_remove_range(
    struct dynamic_string* dyn_str, size_t index, size_t len);

/*
 * 指定された書式の文字列を追加
 */
bool dynamic_string_append_format(
    struct dynamic_string* dyn_str,
    const char* format, ...);

/*
 * 指定された書式の文字列を追加
 */
bool dynamic_string_append_format_v(
    struct dynamic_string* dyn_str,
    const char* format, va_list ap);

/*
 * 指定された書式の文字列を挿入
 */
bool dynamic_string_insert_format(
    struct dynamic_string* dyn_str, size_t index,
    const char* format, ...);

/*
 * 指定された書式の文字列を挿入
 */
bool dynamic_string_insert_format_v(
    struct dynamic_string* dyn_str, size_t index,
    const char* format, va_list ap);

#endif /* DYNAMIC_STRING_H */

