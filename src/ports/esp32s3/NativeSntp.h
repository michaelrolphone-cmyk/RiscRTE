#pragma once
#include "NativeBootstrap.h"
namespace RiscBootstrap {
// Reads only the optional owner 'time' blob. No server defaults or activity.
FreshTime configuredFreshTime(RiscProvision::Input,uint64_t (*now)());
}
