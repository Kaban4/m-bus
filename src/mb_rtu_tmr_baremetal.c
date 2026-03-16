#include <stdint.h>
#include <stddef.h>

/* Implemented by target platform */
typedef void (*mb_hw_timer_cb_t)(void * arg);

/* timer_id: 0 for T1P5, 1 for T3P5 */
extern void mb_hw_timer_stop (int timer_id);
extern void mb_hw_timer_start_us (int timer_id, uint32_t delay_us, mb_hw_timer_cb_t cb, void * arg);

static uint32_t g_t1p5_us = 0;
static uint32_t g_t3p5_us = 0;

/* We pass these two functions to mb_rtu_cfg_t. */
void mb_bm_tmr_init (uint32_t t1p5_us, uint32_t t3p5_us)
{
   g_t1p5_us = t1p5_us;
   g_t3p5_us = t3p5_us;
}

void mb_bm_tmr_start (
   void (*t1p5_expired) (void * arg),
   void (*t3p5_expired) (void * arg),
   void * arg)
{
   if (t1p5_expired) {
      mb_hw_timer_start_us(0, g_t1p5_us, (mb_hw_timer_cb_t)t1p5_expired, arg);
   } else {
      mb_hw_timer_stop(0);
   }

   if (t3p5_expired) {
      mb_hw_timer_start_us(1, g_t3p5_us, (mb_hw_timer_cb_t)t3p5_expired, arg);
   } else {
      mb_hw_timer_stop(1);
   }
}
