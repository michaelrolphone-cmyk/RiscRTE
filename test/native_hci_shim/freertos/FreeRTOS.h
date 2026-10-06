#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
void enterCritical(portMUX_TYPE*);
void exitCritical(portMUX_TYPE*);
#define portENTER_CRITICAL(m) enterCritical(m)
#define portEXIT_CRITICAL(m) exitCritical(m)
