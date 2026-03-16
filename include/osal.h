#ifndef OSAL_H
#define OSAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* В The m-bus uses OS_WAIT_FOREVER */
#define OS_WAIT_FOREVER (0xFFFFFFFFu)

/* Suppress unused parameter warnings */
#ifndef MB_UNUSED
#define MB_UNUSED(x) do { (void)(x); } while (0)
#endif

/* ---- Critical sections ----
 * By default, no-op. If you have real RX/TX interrupts,
 * it is better to redefine these macros (for example, via __disable_irq/__enable_irq).
 */
#ifndef OSAL_CRITICAL_ENTER
#define OSAL_CRITICAL_ENTER() do {} while (0)
#endif
#ifndef OSAL_CRITICAL_EXIT
#define OSAL_CRITICAL_EXIT() do {} while (0)
#endif

/* ---------- rt-labs m-bus portability helpers ---------- */

/* Struct packing */
#ifndef CC_PACKED_BEGIN
  #if defined(_MSC_VER)
    #define CC_PACKED_BEGIN __pragma(pack(push, 1))
  #else
    #define CC_PACKED_BEGIN
  #endif
#endif

#ifndef CC_PACKED
  #if defined(__GNUC__) || defined(__clang__)
    #define CC_PACKED __attribute__((packed))
  #elif defined(_MSC_VER)
    #define CC_PACKED
  #else
    #define CC_PACKED
  #endif
#endif

#ifndef CC_PACKED_END
  #if defined(_MSC_VER)
    #define CC_PACKED_END __pragma(pack(pop))
  #else
    #define CC_PACKED_END
  #endif
#endif

/* Static assert usable in C99 (file scope) */
#ifndef CC_STATIC_ASSERT
  #define CC_CONCAT2_(a,b) a##b
  #define CC_CONCAT2(a,b) CC_CONCAT2_(a,b)
  #define CC_STATIC_ASSERT(expr) \
     typedef char CC_CONCAT2(cc_static_assert_, __LINE__)[(expr) ? 1 : -1]
#endif

/* ---------- endian helpers for m-bus ---------- */

#ifndef CC_BSWAP16
static inline uint16_t CC_BSWAP16(uint16_t x)
{
   return (uint16_t)((x << 8) | (x >> 8));
}
#endif

#ifndef CC_TO_BE16
  #if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define CC_TO_BE16(x)   CC_BSWAP16((uint16_t)(x))
    #define CC_FROM_BE16(x) CC_BSWAP16((uint16_t)(x))
  #else
    /* Big-endian (or unknown): assume data is already BE */
    #define CC_TO_BE16(x)   ((uint16_t)(x))
    #define CC_FROM_BE16(x) ((uint16_t)(x))
  #endif
#endif

typedef struct os_event
{
   volatile uint32_t flags;
} os_event_t;

os_event_t * os_event_create (void);
void os_event_set (os_event_t * ev, uint32_t flags);
void os_event_clr (os_event_t * ev, uint32_t flags);

/* Return:
 * 0 — waited, *out_flags contains triggered bits (mask & flags)
 * 1 — timeout
 */
int os_event_wait (os_event_t * ev, uint32_t mask, uint32_t * out_flags, uint32_t timeout_ticks);

/* Sleep in microseconds (used by mb_slave.c as os_usleep(1 * 1000)) */
void os_usleep (uint32_t usec);

/* “Threads":
 * m-bus calls os_thread_create() to run the slave task.
 * On bare-metal, we'll do a delayed launch: create() remembers entry/arg,
 * and you call osal_run() once in main().
 */
typedef void (*os_thread_entry_t) (void * arg);

void os_thread_create (
   const char * name,
   uint32_t priority,
   size_t stack_size,
   os_thread_entry_t entry,
   void * arg);

/* Run all “created threads" (at our minimum, one) */
void osal_run (void);

/* The timeout for waits (ticks = milliseconds at this minimum)
 * Should increment with a period of 1ms from the SysTick/Timer IRQ.
 */
void osal_tick_1ms_isr (void);

uint32_t osal_now_ms(void);

void osal_cpu_relax(void);
#ifndef OSAL_CPU_RELAX
#define OSAL_CPU_RELAX() osal_cpu_relax()
#endif

#ifdef __cplusplus
}
#endif

#endif /* OSAL_H */
