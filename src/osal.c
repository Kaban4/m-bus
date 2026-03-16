#include "osal.h"

static volatile uint32_t g_ms = 0;

void osal_tick_1ms_isr (void)
{
   g_ms++;
}

static uint32_t osal_time_ms (void)
{
   return g_ms;
}

/* ---- events ---- */

#define OSAL_MAX_EVENTS 4
static os_event_t g_events[OSAL_MAX_EVENTS];
static bool g_events_used[OSAL_MAX_EVENTS];

os_event_t * os_event_create (void)
{
   OSAL_CRITICAL_ENTER();
   for (unsigned i = 0; i < OSAL_MAX_EVENTS; i++)
   {
      if (!g_events_used[i])
      {
         g_events_used[i] = true;
         g_events[i].flags = 0;
         OSAL_CRITICAL_EXIT();
         return &g_events[i];
      }
   }
   OSAL_CRITICAL_EXIT();
   return NULL;
}


void os_event_set(os_event_t *ev, uint32_t flags)
{
    /* OR atomically: ISR + main do not lose bits */
    __atomic_fetch_or((uint32_t *)&ev->flags, flags, __ATOMIC_RELEASE);
}


void os_event_clr(os_event_t *ev, uint32_t flags)
{
    /* AND atomically: ISR + main do not lose bits */
    __atomic_fetch_and((uint32_t *)&ev->flags, ~flags, __ATOMIC_RELEASE);
}


int os_event_wait(os_event_t *ev, uint32_t mask, uint32_t *out_flags, uint32_t timeout_ticks)
{
    uint32_t start = g_ms;

    for (;;)
    {
        uint32_t f = __atomic_load_n((uint32_t *)&ev->flags, __ATOMIC_ACQUIRE);
        uint32_t hit = f & mask;

        if (hit)
        {
            if (out_flags) *out_flags = hit;
            return 0; /* We waited */
        }

        if (timeout_ticks != OS_WAIT_FOREVER)
        {
            uint32_t now = g_ms;
            if ((uint32_t)(now - start) >= timeout_ticks)
            {
                if (out_flags) *out_flags = 0;
                return 1; /* timeout */
            }
        }

        OSAL_CPU_RELAX();
    }
}

void os_usleep (uint32_t usec)
{
   /* Minimum: waiting in millisecond ticks.
    * This is NOT suitable for RTU timers — microseconds are needed there!
    * But mb_slave.c uses this only for “not spinning” during down-transport.
    */
   uint32_t wait_ms = (usec + 999u) / 1000u;
   uint32_t start = osal_time_ms();
   while ((osal_time_ms() - start) < wait_ms)
   {
      OSAL_CPU_RELAX();
   }
}

/* ---- “threads” ---- */

static os_thread_entry_t g_entry = NULL;
static void * g_arg = NULL;

void os_thread_create (
   const char * name,
   uint32_t priority,
   size_t stack_size,
   os_thread_entry_t entry,
   void * arg)
{
   (void)name;
   (void)priority;
   (void)stack_size;

   /* Minimum: we keep one “stream” */
   g_entry = entry;
   g_arg = arg;
}

void osal_run (void)
{
   if (g_entry)
      g_entry(g_arg);

   /* If it's back, we'll just hang out. */
   for (;;)
      OSAL_CPU_RELAX();
}

uint32_t osal_now_ms(void)
{
    return __atomic_load_n((uint32_t *)&g_ms, __ATOMIC_ACQUIRE);
    /* or just return g_ms */
}

// osal.c
__attribute__((weak)) void mb_port_rtu_isr_status(void) {}

/* call instead of an empty nop */
void osal_cpu_relax(void)
{
   __asm volatile ("nop");
}
