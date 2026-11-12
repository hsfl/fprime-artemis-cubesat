#pragma once
// fprime-zephyr's time service includes a picolibc-private header. In the
// native_sim glibc profile, timespec is supplied by the standard time header.
#include <time.h>
