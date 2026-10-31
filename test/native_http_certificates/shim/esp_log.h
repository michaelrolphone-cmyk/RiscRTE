#pragma once
#include <stdio.h>
#define ESP_LOGE(tag,...) do { fprintf(stderr,"%s: ",tag); fprintf(stderr,__VA_ARGS__); fputc('\n',stderr); } while(0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
