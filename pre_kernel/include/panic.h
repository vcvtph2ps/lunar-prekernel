#pragma once

[[noreturn, gnu::format(printf, 1, 2)]] void pk_panic(const char* format, ...); // NOLINT @todo: fix elysium tidy naming
