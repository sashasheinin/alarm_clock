#pragma once
#include <stdio.h>
#include <stdint.h>
#include <string.h>
uint32_t millis(void);
struct FakeSerial {
    template <typename... A> void printf(const char* f, A... a) { ::printf(f, a...); }
    void println(const char* s) { ::puts(s); }
};
extern FakeSerial Serial;
