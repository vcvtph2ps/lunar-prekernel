#pragma once

[[noreturn, gnu::format(printf, 1, 2)]] void panic(const char* format, ...);
