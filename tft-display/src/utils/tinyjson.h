#ifndef TINYJSON_H
#define TINYJSON_H

#include <stdbool.h>
#include <stddef.h>

/*
 * tinyjson —— 够用就好的只读 JSON 取值工具（不分配内存、不递归建树）。
 *
 * 用途：解析 modemserver 的 WebSocket 推送（type/serialId/category/content/data）
 * 与 /api/login 的应答。
 *
 * 约定：
 *   - 只在**当前这一层**找 key（不会钻到子对象里去找同名的 key）；
 *   - 字符串会做反转义：\" \\ \/ \b \f \n \r \t \uXXXX（\uXXXX 输出 UTF-8，含代理对）；
 *   - 数字支持整数/小数/指数，也接受被引号包起来的数字（"12.5"）；
 *   - 找不到或类型不符一律返回 false，out 先被清空/置 0，调用方不用先清。
 */

/* 找到 key 对应的值，返回值的起始指针（已跳过空白）；end_out 收值之后的位置。 */
const char *tj_find(const char *json, const char *key, const char **end_out);

bool tj_get_str(const char *json, const char *key, char *out, size_t cap);
bool tj_get_num(const char *json, const char *key, double *out);
bool tj_get_int(const char *json, const char *key, int *out);
bool tj_get_bool(const char *json, const char *key, bool *out);

/* 数组里第 idx 个元素（下标从 0 起），字符串元素会自动反转义 */
bool tj_get_arr_str(const char *json, const char *key, int idx, char *out, size_t cap);

/* 数组里第 idx 个**对象**的原文（含花括号），便于再用 tj_get_* 取里面的字段 */
bool tj_get_arr_obj(const char *json, const char *key, int idx, char *out, size_t cap);

/* key 对应值的**原文**（对象/数组会带上花括号，字符串带引号） */
bool tj_get_raw(const char *json, const char *key, char *out, size_t cap);

/* 遍历当前层对象的每个成员；val/val_end 是该值的原文区间 */
typedef void (*tj_each_cb)(const char *key, const char *val, const char *val_end, void *user);
void tj_each(const char *json, tj_each_cb cb, void *user);

#endif /* TINYJSON_H */
