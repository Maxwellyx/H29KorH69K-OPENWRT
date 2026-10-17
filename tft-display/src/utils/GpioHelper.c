#include "GpioHelper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>  // 增加错误码处理头文件

// 定义错误码（便于调用者判断失败原因）
#define GPIO_SUCCESS 0
#define GPIO_ERR_OPEN -1    // 打开文件失败
#define GPIO_ERR_WRITE -2   // 写入文件失败
#define GPIO_ERR_READ -3    // 读取文件失败
#define GPIO_ERR_ARG -4     // 参数无效

#define GPIO_EXPORT "/sys/class/gpio/export"
#define GPIO_DIR "/sys/class/gpio/gpio%d/direction"
#define GPIO_VAL "/sys/class/gpio/gpio%d/value"

// 新增：通用错误打印宏（减少重复代码，便于调试）
#define PRINT_ERR(func, path, err) \
    fprintf(stderr, "[ERROR] %s failed: path=%s, errno=%d, reason=%s\n", \
            func, path, err, strerror(err))

/**
 * 导出GPIO引脚
 * @param pin GPIO引脚号（必须为正数）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int export_gpio(int pin) {
    // 1. 增加参数合法性检查
    if (pin < 0) {
        fprintf(stderr, "[ERROR] export_gpio: invalid pin=%d (must be non-negative)\n", pin);
        return GPIO_ERR_ARG;
    }

    int fd = open(GPIO_EXPORT, O_WRONLY);
    if (fd == -1) {
        PRINT_ERR("export_gpio", GPIO_EXPORT, errno);
        return GPIO_ERR_OPEN;
    }

    char buf[16];  // 缩小缓冲区（引脚号最多3位，16足够）
    // 2. 使用snprintf替代sprintf，避免缓冲区溢出
    int ret = snprintf(buf, sizeof(buf), "%d", pin);
    if (ret < 0 || ret >= sizeof(buf)) {
        close(fd);  // 避免资源泄漏
        fprintf(stderr, "[ERROR] export_gpio: snprintf buffer overflow\n");
        return GPIO_ERR_WRITE;
    }

    // 3. 检查write返回值，确保写入成功
    ssize_t write_len = write(fd, buf, strlen(buf));
    if (write_len != strlen(buf)) {
        PRINT_ERR("export_gpio", GPIO_EXPORT, errno);
        close(fd);
        return GPIO_ERR_WRITE;
    }

    close(fd);
    return GPIO_SUCCESS;
}

/**
 * 设置GPIO方向
 * @param pin GPIO引脚号
 * @param dir 方向（"in"或"out"，必须非空）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int set_gpio_direction(int pin, const char* dir) {
    // 1. 参数合法性检查
    if (pin < 0 || dir == NULL || (strcmp(dir, "in") != 0 && strcmp(dir, "out") != 0)) {
        fprintf(stderr, "[ERROR] set_gpio_direction: invalid pin=%d or dir=%s\n", pin, dir);
        return GPIO_ERR_ARG;
    }

    char path[64];
    int ret = snprintf(path, sizeof(path), GPIO_DIR, pin);
    if (ret < 0 || ret >= sizeof(path)) {
        fprintf(stderr, "[ERROR] set_gpio_direction: snprintf buffer overflow\n");
        return GPIO_ERR_WRITE;
    }

    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        PRINT_ERR("set_gpio_direction", path, errno);
        return GPIO_ERR_OPEN;
    }

    ssize_t write_len = write(fd, dir, strlen(dir));
    if (write_len != strlen(dir)) {
        PRINT_ERR("set_gpio_direction", path, errno);
        close(fd);
        return GPIO_ERR_WRITE;
    }

    close(fd);
    return GPIO_SUCCESS;
}

/**
 * 设置GPIO输出值
 * @param pin GPIO引脚号
 * @param value 输出值（0或1）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int set_gpio_value(int pin, int value) {
    // 1. 参数合法性检查
    if (pin < 0 || (value != 0 && value != 1)) {
        fprintf(stderr, "[ERROR] set_gpio_value: invalid pin=%d or value=%d\n", pin, value);
        return GPIO_ERR_ARG;
    }

    char path[64];
    int ret = snprintf(path, sizeof(path), GPIO_VAL, pin);
    if (ret < 0 || ret >= sizeof(path)) {
        fprintf(stderr, "[ERROR] set_gpio_value: snprintf buffer overflow\n");
        return GPIO_ERR_WRITE;
    }

    int fd = open(path, O_WRONLY);
    if (fd == -1) {
        PRINT_ERR("set_gpio_value", path, errno);
        return GPIO_ERR_OPEN;
    }

    const char* val_str = value ? "1" : "0";
    ssize_t write_len = write(fd, val_str, 1);
    if (write_len != 1) {
        PRINT_ERR("set_gpio_value", path, errno);
        close(fd);
        return GPIO_ERR_WRITE;
    }

    close(fd);
    return GPIO_SUCCESS;
}

/**
 * 读取GPIO输入值
 * @param pin GPIO引脚号
 * @return 0/1成功，其他错误码失败
 */
int read_gpio_value(int pin) {
    if (pin < 0) {
        fprintf(stderr, "[ERROR] read_gpio_value: invalid pin=%d\n", pin);
        return GPIO_ERR_ARG;
    }

    char path[64];
    int ret = snprintf(path, sizeof(path), GPIO_VAL, pin);
    if (ret < 0 || ret >= sizeof(path)) {
        fprintf(stderr, "[ERROR] read_gpio_value: snprintf buffer overflow\n");
        return GPIO_ERR_READ;
    }

    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        PRINT_ERR("read_gpio_value", path, errno);
        return GPIO_ERR_OPEN;
    }

    char value_str[3];
    memset(value_str, 0, sizeof(value_str));  // 初始化缓冲区，避免脏数据
    ssize_t read_len = read(fd, value_str, sizeof(value_str) - 1);  // 留1字节给终止符
    if (read_len <= 0) {
        PRINT_ERR("read_gpio_value", path, errno);
        close(fd);
        return GPIO_ERR_READ;
    }

    close(fd);

    // 解析值（处理可能的换行符等空白字符）
    int value = atoi(value_str);
    if (value != 0 && value != 1) {
        fprintf(stderr, "[ERROR] read_gpio_value: invalid value=%s (pin=%d)\n", value_str, pin);
        return GPIO_ERR_READ;
    }

    return value;
}

/**
 * 控制LED亮度（抽取重复代码，避免冗余）
 * @param ledname LED名称
 * @param brightness 亮度值（0=关闭，1=打开）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
static int set_led_brightness(const char* ledname, int brightness) {
    if (ledname == NULL || strlen(ledname) == 0) {
        fprintf(stderr, "[ERROR] set_led_brightness: ledname is empty\n");
        return GPIO_ERR_ARG;
    }

    char led_trigger[64];
    int ret = snprintf(led_trigger, sizeof(led_trigger), "/sys/class/leds/%s/trigger", ledname);
    if (ret < 0 || ret >= sizeof(led_trigger)) {
        fprintf(stderr, "[ERROR] set_led_brightness: snprintf trigger overflow\n");
        return GPIO_ERR_WRITE;
    }

    char led_brightness[64];
    ret = snprintf(led_brightness, sizeof(led_brightness), "/sys/class/leds/%s/brightness", ledname);
    if (ret < 0 || ret >= sizeof(led_brightness)) {
        fprintf(stderr, "[ERROR] set_led_brightness: snprintf brightness overflow\n");
        return GPIO_ERR_WRITE;
    }

    // 步骤1：设置trigger为none
    FILE *f_trigger = fopen(led_trigger, "w");
    if (f_trigger == NULL) {
        PRINT_ERR("set_led_brightness", led_trigger, errno);
        return GPIO_ERR_OPEN;
    }

    ret = fprintf(f_trigger, "none");  // 移除换行符（sysfs通常不需要）
    if (ret < 0) {
        PRINT_ERR("set_led_brightness", led_trigger, errno);
        fclose(f_trigger);
        return GPIO_ERR_WRITE;
    }
    fflush(f_trigger);  // 确保数据写入
    fclose(f_trigger);

    // 步骤2：设置亮度
    FILE *f_bright = fopen(led_brightness, "w");
    if (f_bright == NULL) {
        PRINT_ERR("set_led_brightness", led_brightness, errno);
        return GPIO_ERR_OPEN;
    }

    ret = fprintf(f_bright, "%d", brightness);
    if (ret < 0) {
        PRINT_ERR("set_led_brightness", led_brightness, errno);
        fclose(f_bright);
        return GPIO_ERR_WRITE;
    }
    fflush(f_bright);
    fclose(f_bright);

    return GPIO_SUCCESS;
}

/**
 * 打开LED
 * @param ledname LED名称（非空）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int open_led(const char *ledname) {
    return set_led_brightness(ledname, 1);
}

/**
 * 关闭LED
 * @param ledname LED名称（非空）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int close_led(const char *ledname) {
    return set_led_brightness(ledname, 0);
}

/**
 * 打开5G LED（关闭4G LED）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int Open5Gled() {
    int ret = close_led("blue:modem_5g");
    if (ret != GPIO_SUCCESS) return ret;

    ret = close_led("red:modem_4g");
    if (ret != GPIO_SUCCESS) return ret;

    return open_led("blue:modem_5g");
}

/**
 * 打开4G LED（关闭5G LED）
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int Open4Gled() {
    int ret = close_led("blue:modem_5g");
    if (ret != GPIO_SUCCESS) return ret;

    ret = close_led("red:modem_4g");
    if (ret != GPIO_SUCCESS) return ret;

    return open_led("red:modem_4g");
}

/**
 * 关闭所有调制解调器LED
 * @return GPIO_SUCCESS成功，其他错误码失败
 */
int CloseAllLed() {
    int ret = close_led("blue:modem_5g");
    if (ret != GPIO_SUCCESS) return ret;

    return close_led("red:modem_4g");
}
