#ifndef TOOL_CONFIG_H
#define TOOL_CONFIG_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_LINE_LENGTH 256
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 64

// 配置项结构体
typedef struct {
    char key[MAX_KEY_LENGTH];
    char value[MAX_VALUE_LENGTH];
} ConfigItem;

// 配置部分结构体
typedef struct {
    char type[MAX_KEY_LENGTH];
    char name[MAX_KEY_LENGTH];
    ConfigItem *items;
    int item_count;
    int item_capacity;
} ConfigSection;

// 配置文件结构体
typedef struct {
    ConfigSection *sections;
    int section_count;
    int section_capacity;
} ConfigFile;

int GetconfigKey(const char* section_name, const char* option_key,char* option_value);
int SetconfigKey(const char* section_name, const char* option_key,char* option_value);
#ifdef __cplusplus
}
#endif

#endif