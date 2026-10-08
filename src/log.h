#pragma once

#include <stdio.h>

// --- Log Levels ---
#define LOG_LEVEL_NONE 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 4

// --- Default Log Level ---
// Define LOG_LEVEL in your project's build configuration or here.
#ifndef LOG_LEVEL
#ifdef NDEBUG
#define LOG_LEVEL LOG_LEVEL_INFO
#else
#define LOG_LEVEL LOG_LEVEL_DEBUG  // Default to all logs enabled
#endif
#endif

// --- Log Macros ---
#define LOG_BASE(level, ...) \
  do {                       \
    printf("[%s] ", level);  \
    printf(__VA_ARGS__);     \
    puts("");                \
  } while (0)

// LOG_ERROR
#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(...) LOG_BASE("ERROR", __VA_ARGS__)
#else
#define LOG_ERROR(...)
#endif

// LOG_WARN
#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(...) LOG_BASE("WARN", __VA_ARGS__)
#else
#define LOG_WARN(...)
#endif

// LOG_INFO
#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(...) LOG_BASE("INFO", __VA_ARGS__)
#else
#define LOG_INFO(...)
#endif

// LOG_DEBUG
#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...) LOG_BASE("DEBUG", __VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif
