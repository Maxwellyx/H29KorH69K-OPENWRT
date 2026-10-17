#include "tinyjson.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 扫描 */

static const char *tw_skip(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/* p 指向开引号，返回结束引号**之后**的位置；未闭合则返回指向 '\0' 的位置 */
static const char *tw_skip_str(const char *p)
{
    p++;
    while (*p) {
        if (*p == '\\' && p[1]) {
            p += 2;
            continue;
        }
        if (*p == '"')
            return p + 1;
        p++;
    }
    return p;
}

/* p 指向某个值，返回该值**之后**的位置（能正确跳过嵌套对象/数组） */
static const char *tw_skip_val(const char *p)
{
    p = tw_skip(p);

    if (*p == '"')
        return tw_skip_str(p);

    if (*p == '{' || *p == '[') {
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                p = tw_skip_str(p);
                continue;
            }
            if (*p == open) {
                depth++;
                p++;
                continue;
            }
            if (*p == close) {
                depth--;
                p++;
                if (depth == 0)
                    return p;
                continue;
            }
            p++;
        }
        return p;
    }

    /* 数字 / true / false / null */
    while (*p && *p != ',' && *p != '}' && *p != ']' &&
           *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t')
        p++;
    return p;
}

const char *tj_find(const char *json, const char *key, const char **end_out)
{
    if (!json || !key || !*key)
        return NULL;

    const char *p = tw_skip(json);
    if (*p != '{')
        return NULL;
    p++;

    size_t klen = strlen(key);

    for (;;) {
        p = tw_skip(p);
        if (*p != '"')
            return NULL;

        const char *ks = p + 1;
        const char *ke = tw_skip_str(p); /* ke 指向结束引号之后 */
        size_t l = (size_t)((ke - 1) - ks);

        p = tw_skip(ke);
        if (*p != ':')
            return NULL;
        p = tw_skip(p + 1);

        const char *ve = tw_skip_val(p);

        if (l == klen && strncmp(ks, key, klen) == 0) {
            if (end_out)
                *end_out = ve;
            return p;
        }

        p = tw_skip(ve);
        if (*p == ',') {
            p++;
            continue;
        }
        return NULL; /* '}' 或者格式坏了 */
    }
}

/* ---------------------------------------------------------------- 反转义 */

static int tw_hex4(const char *s, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return 0;
    }
    *out = v;
    return 1;
}

/* [s, e) 是字符串内容（不含两端的引号） */
static void tw_unescape(const char *s, const char *e, char *out, size_t cap)
{
    size_t o = 0;

    if (!out || cap == 0)
        return;

    while (s < e) {
        if (*s != '\\') {
            if (o + 1 < cap)
                out[o++] = *s;
            s++;
            continue;
        }

        s++;
        if (s >= e)
            break;

        switch (*s) {
        case 'n':  out[o++] = '\n'; s++; break;
        case 't':  out[o++] = '\t'; s++; break;
        case 'r':  out[o++] = '\r'; s++; break;
        case 'b':  out[o++] = '\b'; s++; break;
        case 'f':  out[o++] = '\f'; s++; break;
        case '/':  out[o++] = '/';  s++; break;
        case '\\': out[o++] = '\\'; s++; break;
        case '"':  out[o++] = '"';  s++; break;
        case 'u': {
            unsigned cp = 0;
            if (s + 4 < e && tw_hex4(s + 1, &cp)) {
                s += 5;
                /* UTF-16 代理对 */
                if (cp >= 0xD800 && cp <= 0xDBFF && s + 5 < e &&
                    s[0] == '\\' && s[1] == 'u') {
                    unsigned lo = 0;
                    if (tw_hex4(s + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + (lo - 0xDC00u);
                        s += 6;
                    }
                }
                if (cp < 0x80u) {
                    if (o + 1 < cap)
                        out[o++] = (char)cp;
                } else if (cp < 0x800u) {
                    if (o + 2 < cap) {
                        out[o++] = (char)(0xC0 | (cp >> 6));
                        out[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else if (cp < 0x10000u) {
                    if (o + 3 < cap) {
                        out[o++] = (char)(0xE0 | (cp >> 12));
                        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                } else {
                    if (o + 4 < cap) {
                        out[o++] = (char)(0xF0 | (cp >> 18));
                        out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                        out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[o++] = (char)(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                s++;
            }
            break;
        }
        default:
            if (o + 1 < cap)
                out[o++] = *s;
            s++;
            break;
        }
    }

    out[o] = '\0';
}

/* ------------------------------------------------------------------ 取值 */

bool tj_get_str(const char *json, const char *key, char *out, size_t cap)
{
    const char *e = NULL;
    const char *p;

    if (out && cap)
        out[0] = '\0';

    p = tj_find(json, key, &e);
    if (!p || *p != '"' || !out || cap == 0)
        return false;

    tw_unescape(p + 1, e - 1, out, cap);
    return true;
}

bool tj_get_num(const char *json, const char *key, double *out)
{
    const char *e = NULL;
    const char *p;
    char buf[64];
    size_t n;

    if (out)
        *out = 0;

    p = tj_find(json, key, &e);
    if (!p)
        return false;

    if (*p == '"') {
        if (!tj_get_str(json, key, buf, sizeof(buf)))
            return false;
        if (out)
            *out = atof(buf);
        return true;
    }

    if (!((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.'))
        return false;

    n = (size_t)(e - p);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, p, n);
    buf[n] = '\0';

    if (out)
        *out = atof(buf);
    return true;
}

bool tj_get_int(const char *json, const char *key, int *out)
{
    double d;

    if (out)
        *out = 0;

    if (!tj_get_num(json, key, &d))
        return false;

    if (out)
        *out = (int)(d < 0 ? d - 0.5 : d + 0.5);
    return true;
}

bool tj_get_bool(const char *json, const char *key, bool *out)
{
    const char *e = NULL;
    const char *p;
    double d = 0;
    char buf[16];

    if (out)
        *out = false;

    p = tj_find(json, key, &e);
    if (!p)
        return false;

    if (strncmp(p, "true", 4) == 0) {
        if (out)
            *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        if (out)
            *out = false;
        return true;
    }

    /* 兼容 "true" / "1" / 1 这几种写法 */
    if (*p == '"') {
        if (!tj_get_str(json, key, buf, sizeof(buf)))
            return false;
        if (out)
            *out = (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T' ||
                    buf[0] == 'y' || buf[0] == 'Y');
        return true;
    }

    if (tj_get_num(json, key, &d)) {
        if (out)
            *out = (d != 0);
        return true;
    }
    return false;
}

bool tj_get_arr_str(const char *json, const char *key, int idx, char *out, size_t cap)
{
    const char *e = NULL;
    const char *p;
    int i = 0;

    if (out && cap)
        out[0] = '\0';

    p = tj_find(json, key, &e);
    if (!p || *p != '[' || !out || cap == 0)
        return false;
    p++;

    for (;;) {
        const char *ve;

        p = tw_skip(p);
        if (*p != '"' && *p != '{' && *p != '[' &&
            !((*p >= '0' && *p <= '9') || *p == '-' || *p == 't' || *p == 'f' || *p == 'n'))
            return false;

        ve = tw_skip_val(p);

        if (i == idx) {
            if (*p == '"') {
                tw_unescape(p + 1, ve - 1, out, cap);
                return true;
            }
            size_t n = (size_t)(ve - p);
            if (n >= cap)
                n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }

        i++;
        p = tw_skip(ve);
        if (*p == ',') {
            p++;
            continue;
        }
        return false;
    }
}

bool tj_get_raw(const char *json, const char *key, char *out, size_t cap)
{
    const char *e = NULL;
    const char *p;
    size_t n;

    if (out && cap)
        out[0] = '\0';

    p = tj_find(json, key, &e);
    if (!p || !out || cap == 0)
        return false;

    n = (size_t)(e - p);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

bool tj_get_arr_obj(const char *json, const char *key, int idx, char *out, size_t cap)
{
    const char *e = NULL;
    const char *p;
    int i = 0;

    if (out && cap)
        out[0] = '\0';

    if (!json || !out || cap == 0)
        return false;

    if (!key || !*key) {
        p = tw_skip(json);          /* key 为空：把根当数组（服务端 QSCAN 推的就是数组） */
    } else {
        p = tj_find(json, key, &e);
        if (!p)
            return false;
    }

    /* 兼容"不是数组、只有一个对象"的写法 */
    if (*p == '{') {
        if (idx != 0)
            return false;
        {
            const char *oe = tw_skip_val(p);
            size_t n = (size_t)(oe - p);
            if (n >= cap)
                n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
        }
        return true;
    }

    if (*p != '[')
        return false;
    p++;

    for (;;) {
        const char *ve;

        p = tw_skip(p);
        if (*p == ']' || !*p)
            return false;
        ve = tw_skip_val(p);

        if (i == idx) {
            size_t n = (size_t)(ve - p);
            if (n >= cap)
                n = cap - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }

        i++;
        p = tw_skip(ve);
        if (*p == ',') {
            p++;
            continue;
        }
        return false;
    }
}

void tj_each(const char *json, tj_each_cb cb, void *user)
{
    const char *p;

    if (!json || !cb)
        return;

    p = tw_skip(json);
    if (*p != '{')
        return;
    p++;

    for (;;) {
        const char *ks, *ke, *ve;
        size_t l, n;
        char kbuf[128];

        p = tw_skip(p);
        if (*p != '"')
            return;

        ks = p + 1;
        ke = tw_skip_str(p);
        l = (size_t)((ke - 1) - ks);
        n = l < sizeof(kbuf) - 1 ? l : sizeof(kbuf) - 1;
        memcpy(kbuf, ks, n);
        kbuf[n] = '\0';

        p = tw_skip(ke);
        if (*p != ':')
            return;
        p = tw_skip(p + 1);

        ve = tw_skip_val(p);
        cb(kbuf, p, ve, user);

        p = tw_skip(ve);
        if (*p == ',') {
            p++;
            continue;
        }
        return;
    }
}
