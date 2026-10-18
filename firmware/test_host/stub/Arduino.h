#pragma once
/* Host stand-in for the bits of Arduino.h the converter uses. */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>

extern "C" uint32_t millis(void);
