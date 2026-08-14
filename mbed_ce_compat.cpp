// Small pieces mbed 2 provided and Mbed CE does not.

#include "mbed.h"

// The USB stack in mbed 2 could dump its event log for debugging, and main.cpp
// calls it at three points around connect and disconnect. Nothing in the new
// stack offers it, and it never did anything in a release build, so it stays
// as a no-op rather than being cut out of main.cpp — leaving the calls where
// they are keeps the diff to the original honest.
extern "C++" void HAL_DEBUG_PRINTEVENTS(const char *prefix)
{
    (void)prefix;
}
