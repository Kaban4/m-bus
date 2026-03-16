#ifndef OSAL_LOG_H
#define OSAL_LOG_H

#include "options.h"

/* 0=ERROR, 1=INFO, 2=DEBUG */
#define LOG_LEVEL_ERROR 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_DEBUG 2

#if LOG_ENABLE
  #include <stdio.h>
  #define LOG_ERROR(mod, fmt, ...) do { (void)(mod); printf("[E] " fmt, ##__VA_ARGS__); } while (0)
  #define LOG_INFO(mod,  fmt, ...) do { (void)(mod); printf("[I] " fmt, ##__VA_ARGS__); } while (0)
  #if (LOG_LEVEL >= LOG_LEVEL_DEBUG)
    #define LOG_DEBUG(mod, fmt, ...) do { (void)(mod); printf("[D] " fmt, ##__VA_ARGS__); } while (0)
  #else
    #define LOG_DEBUG(mod, fmt, ...) do { (void)(mod); } while (0)
  #endif
#else
  #define LOG_ERROR(mod, fmt, ...) do { (void)(mod); } while (0)
  #define LOG_INFO(mod,  fmt, ...) do { (void)(mod); } while (0)
  #define LOG_DEBUG(mod, fmt, ...) do { (void)(mod); } while (0)
#endif

#define LOG_DEBUG_ENABLED(mod) (LOG_ENABLE && (LOG_LEVEL >= LOG_LEVEL_DEBUG) && ((mod) != 0))

#endif /* OSAL_LOG_H */
