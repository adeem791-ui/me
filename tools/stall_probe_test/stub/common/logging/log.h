#pragma once
// Test stand-in for the emulator's logger: keeps each line so tests can inspect it. snprintf
// lets the compiler check every format string against its arguments.
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
inline std::vector<std::string> g_log;
inline std::mutex               g_log_mutex;
#define LOGF(...)                                                                                  \
	do {                                                                                           \
		char log_line_[4096];                                                                      \
		std::snprintf(log_line_, sizeof(log_line_), __VA_ARGS__);                                  \
		std::lock_guard log_lock_(g_log_mutex);                                                    \
		g_log.emplace_back(log_line_);                                                             \
	} while (false)
