#pragma once

#include <math.h>

#define sleep(x) Sleep(x * 1000)

static inline void usleep(DWORD waitTime)
{
    Sleep(ceil(waitTime/1000.0));
}
