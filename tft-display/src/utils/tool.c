#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "tool.h"

// 从字符串中提取指定键对应的值
void extractValue(const char *json, const char *key, char *value, int maxLen) {
    char searchKey[128];
    snprintf(searchKey, sizeof(searchKey), "\"%s\":\"", key);  // 构造键的搜索格式
    
    const char *keyPos = strstr(json, searchKey);
    if (keyPos == NULL) {
        value[0] = '\0';
        return;
    }
    
    // 移动到值的起始位置
    const char *valueStart = keyPos + strlen(searchKey);
    // 找到值的结束位置（下一个双引号）
    const char *valueEnd = strchr(valueStart, '"');
    if (valueEnd == NULL) {
        value[0] = '\0';
        return;
    }
    
    // 复制值到结果缓冲区
    int copyLen = valueEnd - valueStart;
    if (copyLen >= maxLen) {
        copyLen = maxLen - 1;
    }
    strncpy(value, valueStart, copyLen);
    value[copyLen] = '\0';
}