#ifndef OPTIONS_H
#define OPTIONS_H

/* Logs can be turned off completely. */
#ifndef LOG_ENABLE
#define LOG_ENABLE 0
#endif

/* Log levels (compatible with osal_log.h below) */
#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

/* Ids of logging “modules” */
#ifndef MB_RTU_LOG
#define MB_RTU_LOG 1
#endif

#ifndef MB_TCP_LOG
#define MB_TCP_LOG 1
#endif

#endif /* OPTIONS_H */
