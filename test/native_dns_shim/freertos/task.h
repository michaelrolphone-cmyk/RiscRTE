#pragma once
int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,void*,int);
void vTaskDelete(void*);
