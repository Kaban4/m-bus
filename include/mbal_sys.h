#ifndef MBAL_SYS_H
#define MBAL_SYS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ssize_t is usually not present on bare-metal.
 *
 * On hosted/posix targets it is provided by <sys/types.h>.
 */
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__) || defined(__CYGWIN__)
#include <sys/types.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* MBAL_SYS_H */
