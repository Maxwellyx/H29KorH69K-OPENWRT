#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

int export_gpio(int pin);
int set_gpio_direction(int pin, const char* dir);
int set_gpio_value(int pin, int value);
int read_gpio_value(int pin);


int Open5Gled();
int Open4Gled();
int CloseAllLed();
