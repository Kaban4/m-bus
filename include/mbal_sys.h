#ifndef MBAL_SYS_H
#define MBAL_SYS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* On hosted/posix targets ssize_t comes from sys/types.h */
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__) || defined(__CYGWIN__)
#include <sys/types.h>
#else
/* Bare-metal fallback */
typedef ptrdiff_t ssize_t;
#endif

#ifdef __cplusplus
}
#endif

#endif /* MBAL_SYS_H */
