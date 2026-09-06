#pragma once

// Set to 1 only for a verbose hardware investigation build. Normal firmware
// keeps summaries, stack markers and critical errors without path tracing.
#ifndef NETRUN_DEBUG_VERBOSE
#define NETRUN_DEBUG_VERBOSE 0
#endif

// Set to 1 only for a diagnostic build that should execute the complete
// regression suite during boot. Normal firmware keeps the suite compiled out
// of the boot path.
#ifndef NETRUN_RUN_BOOT_TESTS
#define NETRUN_RUN_BOOT_TESTS 0
#endif

#if NETRUN_DEBUG_VERBOSE
#define NETRUN_VERBOSE_PRINTF(...) Serial.printf(__VA_ARGS__)
#define NETRUN_VERBOSE_PRINT(value) Serial.println(value)
#else
#define NETRUN_VERBOSE_PRINTF(...) do { } while (0)
#define NETRUN_VERBOSE_PRINT(value) do { } while (0)
#endif
