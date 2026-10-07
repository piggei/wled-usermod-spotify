#pragma once
#include <cassert>
using UBaseType_t=unsigned int;
using TaskHandle_t=void*;
using portMUX_TYPE=int;
extern int hostCriticalDepth;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) do { (void)(x); assert(hostCriticalDepth == 0); ++hostCriticalDepth; } while (0)
#define portEXIT_CRITICAL(x) do { (void)(x); assert(hostCriticalDepth == 1); --hostCriticalDepth; } while (0)
