#pragma once
#include "esp_ota_ops.h"
inline const esp_app_desc_t* esp_app_get_description(){return esp_ota_get_app_description();}
