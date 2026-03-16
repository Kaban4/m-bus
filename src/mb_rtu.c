/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2012 rt-labs AB, Sweden.
 *
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 ********************************************************************/

#include "mb_rtu.h"
#include "mb_pdu.h"
#include "mb_crc.h"
#include "mbal_rtu.h"
#include "options.h"

#include "osal.h"
#include "osal_log.h"

#include <stdlib.h>
#include <errno.h>

#if defined(__linux__) && defined (USE_TRACE)
#include "mb-tp.h"
#else
#define tracepoint(...)
#endif

#define FLAG_TX_EMPTY BIT (0)
#define FLAG_RX_AVAIL BIT (1)
#define FLAG_T1P5     BIT (2)
#define FLAG_T3P5     BIT (3)

typedef struct mb_rtu
{
   mb_transport_t transport;

   int fd;
   os_event_t * flags;
   void (*tx_enable) (int level);
   void (*tmr_init) (uint32_t t1p5, uint32_t t3p5);
   void (*tmr_start) (
      void (*t1p5_expired) (void * arg),
      void (*t3p5_expired) (void * arg),
      void * arg);
   bool broadcast;
   uint32_t char_time_us;
} mb_rtu_t;

typedef enum {
  ST_IDLE = 0,
  ST_RX_WAIT_FIRST,
  ST_RX_WAIT_BODY,
  ST_TX_WAIT_EMPTY,
  ST_TX_DRAIN,
  ST_TX_WAIT_T3
} mb_state_t;

int mb_tx_hook (void * arg, void * data)
{
   mb_rtu_t * rtu = (mb_rtu_t *)arg;
   os_event_set (rtu->flags, FLAG_TX_EMPTY);
   return 0;
}

static void mb_t1p5_expired (void * arg)
{
   mb_rtu_t * rtu = (mb_rtu_t *)arg;
   tracepoint (mb, t1p5);
   os_event_set (rtu->flags, FLAG_T1P5);
}

static void mb_t3p5_expired (void * arg)
{
   mb_rtu_t * rtu = (mb_rtu_t *)arg;
   tracepoint (mb, t3p5);
   os_event_set (rtu->flags, FLAG_T3P5);
}

volatile uint32_t dbg_rx_hook_cnt = 0;

volatile uint32_t dbg_last_rxhook_ms = 0;

int mb_rx_hook(void * arg, void * data)
{
   mb_rtu_t * rtu = (mb_rtu_t *)arg;

   dbg_last_rxhook_ms = osal_now_ms();;

   dbg_rx_hook_cnt++;
   tracepoint (mb, rx_hook);
   
   os_event_clr (rtu->flags, FLAG_T1P5 | FLAG_T3P5);
   rtu->tmr_start (mb_t1p5_expired, mb_t3p5_expired, rtu);
   os_event_set (rtu->flags, FLAG_RX_AVAIL);

   return 0;
}

static void mb_rtu_dump (
   const char * header,
   const uint8_t * buffer,
   size_t size)
{
#if LOG_DEBUG_ENABLED(MB_RTU_LOG)
   LOG_DEBUG (MB_RTU_LOG, "%s", header);
   while (size--)
      LOG_DEBUG (MB_RTU_LOG, " %02x\n", *buffer++);
#endif
}

static int mb_rtu_bringup (mb_transport_t * transport, const char * name)
{
   const char * p     = name;
   unsigned int slave = 0;
   mb_rtu_t * mb_rtu = (mb_rtu_t *)transport;

   CC_ASSERT (name != NULL);

   /* Remainder of filename is modbus slave */
   while (*p != '\0')
   {
      uint8_t digit = *p++ - '0';

      if (digit > 9)
         goto error;

      slave = slave * 10 + digit;
   }

   /* Check that slave is valid */
   if (slave > 247)
      goto error;

   if (mb_rtu->transport.up_cb)
   {
      mb_rtu->transport.up_cb (&mb_rtu->transport);
   }

   return slave;

error:
   errno = ENOENT;
   return -1;
}

static int mb_rtu_shutdown (mb_transport_t * transport)
{
   mb_rtu_t * mb_rtu = (mb_rtu_t *)transport;
   if (mb_rtu->transport.down_cb)
   {
      mb_rtu->transport.down_cb (&mb_rtu->transport);
   }
   return 0;
}

static bool mb_rtu_is_down ()
{
   return false;
}

static void mb_rtu_write (mb_rtu_t * rtu, const void * buffer, size_t size)
{
   ssize_t nwrite;
   const uint8_t * p = buffer;

   while (size > 0)
   {
      nwrite = os_rtu_write (rtu->fd, p, size);
      if (nwrite <= 0)
      {
         LOG_ERROR (MB_RTU_LOG, "tx failure\n");
         break;
      }
      p += (size_t)nwrite;
      size -= (size_t)nwrite;
   }
}

static size_t mb_rtu_read(mb_rtu_t *rtu, void *buffer, size_t size)
{
    ssize_t navail, nread;

    os_event_clr(rtu->flags, FLAG_RX_AVAIL);

    navail = os_rtu_rx_avail(rtu->fd);
      if (navail <= 0) {
         return 0;
      }

      if ((size_t)navail > size) {
         /* Still data pending after we read only `size` bytes */
         os_event_set(rtu->flags, FLAG_RX_AVAIL);
         navail = (ssize_t)size;
      }

      nread = os_rtu_read(rtu->fd, buffer, (size_t)navail);

      /* IMPORTANT: if data is still pending after read, raise RX_AVAIL again */
      if (os_rtu_rx_avail(rtu->fd) > 0) {
         os_event_set(rtu->flags, FLAG_RX_AVAIL);
      }

      return (nread > 0) ? (size_t)nread : 0;
}

static void mb_rtu_tx (
   mb_transport_t * transport,
   const pdu_txn_t * transaction,
   size_t size)
{
   mb_rtu_t * rtu = (mb_rtu_t *)transport;
   uint32_t flags;
   crc_t crc;
   uint8_t slave = transaction->unit;

   tracepoint (mb, tx_trace, 1);
   mb_rtu_dump ("Tx:\n", transaction->data, size);

   /* Compute CRC */
   crc = mb_crc (&slave, 1, 0xFFFF);
   crc = mb_crc (transaction->data, (uint8_t)size, crc);

   /* Enable Tx */
   if (rtu->tx_enable)
      rtu->tx_enable (1);

   /* Send slave address */
   mb_rtu_write (rtu, &slave, 1);

   /* Send PDU */
   os_event_clr (rtu->flags, FLAG_TX_EMPTY);
   mb_rtu_write (rtu, transaction->data, size);

   /* Send CRC */
   mb_rtu_write (rtu, &crc, sizeof (crc_t));
   tracepoint (mb, tx_trace, 2);

   /* Wait for emission of last character */
#if !defined(__linux__)
   /* Timeout while waiting for TX_EMPTY to avoid hanging forever
   * char_time_us has already been computed in mb_rtu_serial_cfg().
   * 3 + size = address + PDU + CRC(2) => total bytes on wire.
   */
   uint32_t bytes_on_wire = 3u + (uint32_t)size;
   uint32_t tx_time_us    = bytes_on_wire * rtu->char_time_us;

   /* Safety margin: +2ms and x2 to cover jitter/IRQ latency */
   uint32_t tx_tmo_ms = (tx_time_us * 2u + 2000u + 999u) / 1000u;
   if (tx_tmo_ms < 5u) tx_tmo_ms = 5u;

   int timedout = os_event_wait(rtu->flags, FLAG_TX_EMPTY, &flags, tx_tmo_ms);
   if (timedout)
   {
      /* TX_EMPTY did not arrive (IRQ lost/wrong bit) — do not hang.
       * Continue by polling/draining to finish transmission.
       */
      // LOG_DEBUG(MB_RTU_LOG, "TX_EMPTY timeout\n");
   }
#endif

   os_rtu_tx_drain(rtu->fd, 3 + size);
   tracepoint (mb, tx_trace, 3);

   /* Clear state for reception */
   os_event_clr (rtu->flags, FLAG_T1P5 | FLAG_T3P5);

   /* Disable Tx */
   if (rtu->tx_enable)
      rtu->tx_enable (0);

/* Start and wait for T3P5 timer */
rtu->tmr_start(NULL, mb_t3p5_expired, rtu);

#if !defined(__linux__)
   /* T3.5 = 3.5 * char_time_us (char_time_us is already computed) */
   uint32_t t3_time_us = (35u * rtu->char_time_us) / 10u;   // 1750us for 500us char time
   uint32_t t3_tmo_ms  = (t3_time_us + 5000u + 999u) / 1000u; // +5ms margin
   if (t3_tmo_ms < 5u) t3_tmo_ms = 5u;

   int t3_to = os_event_wait(rtu->flags, FLAG_T3P5, &flags, t3_tmo_ms);
   if (t3_to) {
      /* Do not hang — just continue */
      // LOG_DEBUG(MB_RTU_LOG, "T3P5 timeout\n");
      os_event_set(rtu->flags, FLAG_T3P5);  // optional: avoid leaving the waiter stuck
   }
#else
  os_event_wait(rtu->flags, FLAG_T3P5, &flags, OS_WAIT_FOREVER);
#endif
   tracepoint (mb, tx_trace, 4);
}

static bool mb_rtu_rx_avail (mb_transport_t * transport)
{
   mb_rtu_t * rtu = (mb_rtu_t *)transport;
   ssize_t navail;

   navail = os_rtu_rx_avail (rtu->fd);
   if (navail < 0)
   {
      LOG_ERROR (MB_RTU_LOG, "rx_avail failure\n");
   }
   return navail > 0;
}

static int mb_rtu_rx(
   mb_transport_t * transport,
   pdu_txn_t * transaction,
   uint32_t tmo)
{
   mb_rtu_t * rtu = (mb_rtu_t *)transport;
   bool frame_ok  = true;
   size_t count   = 0;
   uint32_t flags;
   crc_t crc;
   uint8_t slave_rx;
   uint8_t * p = transaction->data;
   int error;

   os_event_clr(rtu->flags, FLAG_T1P5 | FLAG_T3P5 | FLAG_RX_AVAIL);

   tracepoint (mb, rx_trace, 1);

   /* Wait for first character */
   if (tmo)
   {
      int timedout;

      uint32_t mask = FLAG_RX_AVAIL;   // RX_AVAIL only
      timedout = os_event_wait(rtu->flags, mask, &flags, tmo ? tmo : OS_WAIT_FOREVER);

      if (timedout)
      {
         tracepoint (mb, rx_trace, 2);
         return ETIMEOUT;
      }
   }
   else
   {
      uint32_t mask = FLAG_RX_AVAIL;   // RX_AVAIL only
      os_event_wait (
         rtu->flags,
         mask,
         &flags,
         OS_WAIT_FOREVER);
   }

   /* Get slave ID */
   mb_rtu_read (rtu, &slave_rx, 1);
   crc = mb_crc (&slave_rx, 1, 0xFFFF);

   /* Get remainder of message (until T1P5 expires) */
   do {
      size_t nread;

      os_event_wait(rtu->flags, FLAG_T1P5 | FLAG_RX_AVAIL, &flags, OS_WAIT_FOREVER);

      if (flags & FLAG_RX_AVAIL) {
         nread = mb_rtu_read(rtu, p, MAX_PDU_SIZE - count);
         p += nread;
         count += nread;
      }
   } while ((flags & FLAG_T1P5) == 0);

   while (os_rtu_rx_avail(rtu->fd) > 0 && count < MAX_PDU_SIZE) {
      size_t n = mb_rtu_read(rtu, p, MAX_PDU_SIZE - count);
      if (!n) break;
      p += n;
      count += n;
   }

   /* Verify message */
   crc = mb_crc (transaction->data, (uint8_t)count, crc);
   if (crc != 0)
   {
      error    = ECRC_FAIL;
      frame_ok = false;
   }

   /* Match station ID with our ID or the broadcast ID */
   if ((slave_rx != transaction->unit) && (slave_rx != 0))
   {
      error    = ESLAVE_ID;
      frame_ok = false;
   }

   /* Set broadcast flag if it was a broadcast station ID */
   rtu->broadcast = (slave_rx == 0);

   /* Wait for end of frame (until T3P5 expires) */
   uint32_t t3_tmo_ms = 20u;
   do
   {
      int to = os_event_wait(
         rtu->flags,
         FLAG_T3P5 | FLAG_RX_AVAIL,
         &flags,
         t3_tmo_ms
      );

      if (to) {
         /* If timer/IRQ is missed — do not hang */
         flags |= FLAG_T3P5;
      }

      if (flags & FLAG_RX_AVAIL) {
         error = EFRAME_NOK;
         frame_ok = false;

         uint8_t tmp2[32];
         while (os_rtu_rx_avail(rtu->fd) > 0) {
            os_rtu_read(rtu->fd, tmp2, sizeof(tmp2));
         }
      }
   } while ((flags & FLAG_T3P5) == 0);

   uint8_t tmp[32];
   while (os_rtu_rx_avail(rtu->fd) > 0) {
      os_rtu_read(rtu->fd, tmp, sizeof(tmp));
   }

   os_event_clr (rtu->flags, FLAG_T1P5 | FLAG_T3P5 | FLAG_RX_AVAIL);

   if (!frame_ok)
   {
      LOG_DEBUG (MB_RTU_LOG, "RxErr: %d\n", error);
      mb_rtu_dump ("RxErr:\n", transaction->data, count);
      tracepoint (mb, rx_trace, 3);
      return error;
   }

   count -= sizeof (crc_t);
   mb_rtu_dump ("Rx:\n", transaction->data, count);
   tracepoint (mb, rx_trace, 4);

   return (int)count;
}

static bool mb_rtu_rx_bc (mb_transport_t * transport)
{
   mb_rtu_t * rtu = (mb_rtu_t *)transport;
   return rtu->broadcast;
}

static uint32_t mb_rtu_char_time (int baudrate)
{
   uint32_t char_time;

   /* If baud rate is > 19200 then 1p5 should be 750 us and 3p5 1750
    * us. If less or equal to 19200 we calculate the values.
    */
   if (baudrate > 19200)
   {
      /* This char_time makes 1p5 = 750 us and 3p5 = 1750 us */
      char_time = 500;
   }
   else
   {
      /* Calculate the time for 1 character in us
       * t (us) = bits / (baudrate / 10^6) =>
       * t (us) = (bits * 10^6) / baudrate
       *
       * For modbus RTU, 1 character is always 11 bits.
       */
      char_time = (11 * 1000 * 1000) / baudrate;
   }

   return char_time;
}

void mb_rtu_serial_cfg (
   mb_transport_t * transport,
   const mb_rtu_serial_cfg_t * cfg)
{
   mb_rtu_t * rtu = (mb_rtu_t *)transport;
   uint32_t t1p5, t3p5;

   /* Configure serial port */
   os_rtu_set_serial_cfg (rtu->fd, cfg);

   /* Calculate T1P5 and T3P5 timeouts */
   rtu->char_time_us = mb_rtu_char_time (cfg->baudrate);

   t1p5 = 15 * rtu->char_time_us / 10;
   t3p5 = 35 * rtu->char_time_us / 10;

   /* Configure timers */
   rtu->tmr_init (t1p5, t3p5);
}

mb_transport_t * mb_rtu_init (const mb_rtu_cfg_t * cfg)
{
   static mb_rtu_t rtu1;
   /* Allocate and initialise driver structure */
   mb_rtu_t * rtu = &rtu1;

   CC_ASSERT (rtu != NULL);

   rtu->transport.bringup  = mb_rtu_bringup;
   rtu->transport.shutdown = mb_rtu_shutdown;
   rtu->transport.is_down  = mb_rtu_is_down;
   rtu->transport.tx       = mb_rtu_tx;
   rtu->transport.rx       = mb_rtu_rx;
   rtu->transport.rx_is_bc = mb_rtu_rx_bc;
   rtu->transport.rx_avail = mb_rtu_rx_avail;
   rtu->transport.up_cb    = cfg->up_cb;
   rtu->transport.down_cb  = cfg->down_cb;

   rtu->tx_enable = cfg->tx_enable;
   rtu->tmr_init  = cfg->tmr_init;
   rtu->tmr_start = cfg->tmr_start;
   rtu->flags     = os_event_create();

   /* Open serial port */
   rtu->fd = os_rtu_open (cfg->serial, rtu);

   /* Configure RTU layer */
   mb_rtu_serial_cfg (&rtu->transport, cfg->serial_cfg);

   return (mb_transport_t *)rtu;
}
