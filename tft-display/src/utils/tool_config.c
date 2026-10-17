#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 256
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 64

// 配置项结构体
typedef struct {
    char key[MAX_KEY_LENGTH];
    char value[MAX_VALUE_LENGTH];
} ConfigItem;

// 配置文件结构体
typedef struct {
    char section[MAX_KEY_LENGTH];
    ConfigItem *items;
    int item_count;
    int item_capacity;
} ConfigSection;

// 配置文件集合结构体
typedef struct {
    ConfigSection *sections;
    int section_count;
    int section_capacity;
    char filename[MAX_LINE_LENGTH];
} ConfigFile;

// 函数声明
void free_config_file(ConfigFile* config_file);

// 读取配置文件
ConfigFile* read_config_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("无法打开配置文件");
        return NULL;
    }

    ConfigFile* config_file = (ConfigFile*)malloc(sizeof(ConfigFile));
    if (!config_file) {
        perror("内存分配失败");
        fclose(file);
        return NULL;
    }
    
    strcpy(config_file->filename, filename);
    config_file->section_count = 0;
    config_file->section_capacity = 10;
    config_file->sections = (ConfigSection*)malloc(config_file->section_capacity * sizeof(ConfigSection));
    if (!config_file->sections) {
        perror("内存分配失败");
        free(config_file);
        fclose(file);
        return NULL;
    }

    char line[MAX_LINE_LENGTH];
    ConfigSection* current_section = NULL;

    while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
        // 去除换行符
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // 跳过空行
        if (line[0] == '\0') continue;

        // 检查是否是节
        if (line[0] == '[' && line[strlen(line)-1] == ']') {
            char section_name[MAX_KEY_LENGTH];
            sscanf(line, "[%[^]]", section_name);
            
            // 确保有足够的空间
            if (config_file->section_count >= config_file->section_capacity) {
                config_file->section_capacity *= 2;
                config_file->sections = (ConfigSection*)realloc(config_file->sections, 
                                                     config_file->section_capacity * sizeof(ConfigSection));
                if (!config_file->sections) {
                    perror("内存分配失败");
                    free_config_file(config_file);
                    fclose(file);
                    return NULL;
                }
            }
            
            current_section = &config_file->sections[config_file->section_count++];
            strcpy(current_section->section, section_name);
            current_section->item_count = 0;
            current_section->item_capacity = 10;
            current_section->items = (ConfigItem*)malloc(current_section->item_capacity * sizeof(ConfigItem));
            if (!current_section->items) {
                perror("内存分配失败");
                free_config_file(config_file);
                fclose(file);
                return NULL;
            }
            continue;
        }

        // 如果没有当前节，继续
        if (!current_section) continue;

        // 解析选项
        char key[MAX_KEY_LENGTH];
        char value[MAX_VALUE_LENGTH];
        if (sscanf(line, "option %[^']'%[^']'", key, value) == 2) {
            // 确保有足够的空间
            if (current_section->item_count >= current_section->item_capacity) {
                current_section->item_capacity *= 2;
                current_section->items = (ConfigItem*)realloc(current_section->items, 
                                                     current_section->item_capacity * sizeof(ConfigItem));
                if (!current_section->items) {
                    perror("内存分配失败");
                    free_config_file(config_file);
                    fclose(file);
                    return NULL;
                }
            }

            // 添加到项目列表
            strcpy(current_section->items[current_section->item_count].key, key);
            strcpy(current_section->items[current_section->item_count].value, value);
            current_section->item_count++;
        }
    }

    fclose(file);
    return config_file;
}

// 释放配置内存
void free_config_file(ConfigFile* config_file) {
    if (config_file) {
        for (int i = 0; i < config_file->section_count; i++) {
            if (config_file->sections[i].items) {
                free(config_file->sections[i].items);
            }
        }
        if (config_file->sections) {
            free(config_file->sections);
        }
        free(config_file);
    }
}

// 获取选项值
const char* get_option_value(ConfigFile* config_file, const char* section_name, const char* key) {
    if (!config_file) return NULL;

    for (int i = 0; i < config_file->section_count; i++) {
        if (strcmp(config_file->sections[i].section, section_name) == 0) {
            for (int j = 0; j < config_file->sections[i].item_count; j++) {
                if (strcmp(config_file->sections[i].items[j].key, key) == 0) {
                    return config_file->sections[i].items[j].value;
                }
            }
            return NULL; // 找到节但未找到键
        }
    }
    return NULL; // 未找到节
}

// 添加或更新选项
int set_option_value(ConfigFile* config_file, const char* section_name, 
                     const char* key, const char* value) {
    if (!config_file) return 0;

    // 查找节
    for (int i = 0; i < config_file->section_count; i++) {
        if (strcmp(config_file->sections[i].section, section_name) == 0) {
            // 查找键
            for (int j = 0; j < config_file->sections[i].item_count; j++) {
                if (strcmp(config_file->sections[i].items[j].key, key) == 0) {
                    // 更新现有值
                    strcpy(config_file->sections[i].items[j].value, value);
                    return 1;
                }
            }
            
            // 键不存在，添加新项
            if (config_file->sections[i].item_count >= config_file->sections[i].item_capacity) {
                config_file->sections[i].item_capacity *= 2;
                config_file->sections[i].items = (ConfigItem*)realloc(config_file->sections[i].items, 
                                                     config_file->sections[i].item_capacity * sizeof(ConfigItem));
                if (!config_file->sections[i].items) {
                    perror("内存分配失败");
                    return 0;
                }
            }
            
            strcpy(config_file->sections[i].items[config_file->sections[i].item_count].key, key);
            strcpy(config_file->sections[i].items[config_file->sections[i].item_count].value, value);
            config_file->sections[i].item_count++;
            return 1;
        }
    }
    
    // 节不存在，创建新节
    if (config_file->section_count >= config_file->section_capacity) {
        config_file->section_capacity *= 2;
        config_file->sections = (ConfigSection*)realloc(config_file->sections, 
                                             config_file->section_capacity * sizeof(ConfigSection));
        if (!config_file->sections) {
            perror("内存分配失败");
            return 0;
        }
    }
    
    ConfigSection* new_section = &config_file->sections[config_file->section_count++];
    strcpy(new_section->section, section_name);
    new_section->item_count = 0;
    new_section->item_capacity = 10;
    new_section->items = (ConfigItem*)malloc(new_section->item_capacity * sizeof(ConfigItem));
    if (!new_section->items) {
        perror("内存分配失败");
        return 0;
    }
    
    // 添加新项
    strcpy(new_section->items[new_section->item_count].key, key);
    strcpy(new_section->items[new_section->item_count].value, value);
    new_section->item_count++;
    
    return 1;
}

// 保存配置文件
int save_config_file(ConfigFile* config_file) {
    if (!config_file) return 0;
    
    FILE* file = fopen(config_file->filename, "w");
    if (file == NULL) {
        perror("无法打开配置文件进行写入");
        return 0;
    }
    
    for (int i = 0; i < config_file->section_count; i++) {
        ConfigSection* section = &config_file->sections[i];
        
        // 写入节名
        fprintf(file, "[%s]\n", section->section);
        
        // 写入所有选项
        for (int j = 0; j < section->item_count; j++) {
            fprintf(file, "option %s'%s'\n", 
                    section->items[j].key, section->items[j].value);
        }
        
        // 节之间留空行
        if (i < config_file->section_count - 1) {
            fprintf(file, "\n");
        }
    }
    
    fclose(file);
    return 1;
}

int GetconfigKey(const char* section_name, const char* option_key,char* option_value) {
    const char* filename = "/etc/lvglui.conf";
    
    // 读取配置文件
    ConfigFile* config = read_config_file(filename);
    if (!config) {
        // printf("无法读取配置文件，将创建新文件\n");
        config = (ConfigFile*)malloc(sizeof(ConfigFile));
        if (!config) {
            perror("内存分配失败");
            return 1;
        }
        strcpy(config->filename, filename);
        config->section_count = 0;
        config->section_capacity = 10;
        config->sections = (ConfigSection*)malloc(config->section_capacity * sizeof(ConfigSection));
        if (!config->sections) {
            perror("内存分配失败");
            free(config);
            return 1;
        }
    }
    // 获取并显示现有值
    // const char* section_name = "global";
    // const char* option_key = "apn";
    const char* found_value = get_option_value(config, section_name, option_key);
    if (found_value) {
        // printf("现有值: %s = %s\n", option_key, found_value);
        if (option_value) {
            strncpy(option_value, found_value, MAX_VALUE_LENGTH - 1);
            option_value[MAX_VALUE_LENGTH - 1] = '\0';
        }
    } else {
        // printf("未找到选项: %s，将创建它\n", option_key);
        // 设置新值
        if (set_option_value(config, section_name, option_key, "auto")) {
            // printf("已设置新值: %s = new_apn_value\n", option_key);
            if (option_value) {
                strncpy(option_value, "auto", MAX_VALUE_LENGTH - 1);
                option_value[MAX_VALUE_LENGTH - 1] = '\0';
            }
        } else {
            // printf("设置值失败\n");
        }

        // 保存更改
        if (save_config_file(config)) {
            // printf("配置已保存到 %s\n", filename);
        } else {
            // printf("保存配置失败\n");
        }
    }

    // 释放内存
    free_config_file(config);

    return 0;
}

int SetconfigKey(const char* section_name, const char* option_key,char* option_value) {
    const char* filename = "/etc/lvglui.conf";
    
    // 读取配置文件
    ConfigFile* config = read_config_file(filename);
    if (!config) {
        // printf("无法读取配置文件，将创建新文件\n");
        config = (ConfigFile*)malloc(sizeof(ConfigFile));
        if (!config) {
            perror("内存分配失败");
            return 1;
        }
        strcpy(config->filename, filename);
        config->section_count = 0;
        config->section_capacity = 10;
        config->sections = (ConfigSection*)malloc(config->section_capacity * sizeof(ConfigSection));
        if (!config->sections) {
            perror("内存分配失败");
            free(config);
            return 1;
        }
    }

    // 设置新值
    if (set_option_value(config, section_name, option_key, option_value)) {
        // printf("已设置新值: %s = new_apn_value\n", option_key);
        if (option_value) {
            strncpy(option_value, option_value, MAX_VALUE_LENGTH - 1);
            option_value[MAX_VALUE_LENGTH - 1] = '\0';
        }
    } else {
        // printf("设置值失败\n");
    }

    // 保存更改
    if (save_config_file(config)) {
        // printf("配置已保存到 %s\n", filename);
    } else {
        // printf("保存配置失败\n");
    }

    // 释放内存
    free_config_file(config);

    return 0;
}