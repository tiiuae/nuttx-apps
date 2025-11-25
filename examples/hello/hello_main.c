/****************************************************************************
 * apps/examples/hello/hello_main.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MPFS_CLINT_BASE      (0x02000000UL)
#define MPFS_CLINT_MTIME     (MPFS_CLINT_BASE + 0xbff8)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static uint64_t cntfrq;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_ARCH_ARM64

static inline uint64_t arch_timer_get_cntfrq(void)
{
  return read_sysreg(cntfrq_el0);
}

static inline uint64_t arch_timer_count(void)
{
  return read_sysreg(cntvct_el0);
}

#elif CONFIG_ARCH_CHIP_MPFS

static inline uint64_t arch_timer_get_cntfrq(void)
{
  return 1000000;
}

static inline uint64_t arch_timer_count(void)
{
  return *(volatile uint64_t *)MPFS_CLINT_MTIME;
}

#else
#error Not supported
#endif

static inline uint64_t time_usec(void)
{
  return 1000000 * arch_timer_count() / cntfrq;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * hello_main
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  const int udelay_times[] =
    {
      1, 10, 100
    };

  int i;
  irqstate_t flags;
  uint64_t t1;
  uint64_t t2;

  bool errors = false;

  cntfrq = arch_timer_get_cntfrq();
  DEBUGASSERT(cntfrq != 0);

  usleep(1000);

  /* Comment out the LOOPSMERMSEC sanity check below if "udelay_coarse" is
   * not in use
   */

#if 1
  printf("Calculating expected CONFIG_BOARD_LOOPSPERMSEC for udelay_coarse"
         "(this will take ~10s)\n");
  sleep(1);

  /* Coarse LOOPSPERMSEC calibration */

  flags = enter_critical_section();
  t1 = time_usec();
  up_udelay(10000000); /* 10s busyloop */
  t2 = time_usec();
  leave_critical_section(flags);

  printf("CONFIG_BOARD_LOOPSPERMSEC should be approximately %" PRIu64
         " and is %d\n", (uint64_t)10000000 * CONFIG_BOARD_LOOPSPERMSEC /
         (t2 - t1), CONFIG_BOARD_LOOPSPERMSEC);
#endif

  printf("udelay test\n");
  for (i = 0; i < sizeof(udelay_times) / sizeof(udelay_times[0]); i++)
    {
      /* Enter critical section for accurate measurement of udelay; no
       * interrupts allowed
       */

      flags = enter_critical_section();

      t1 = time_usec();
      up_udelay(udelay_times[i]);
      t2 = time_usec();

      /* Allow +10us error */

      if (t2 - t1 > udelay_times[i] + 10 || t2 - t1 < udelay_times[i])
        {
          printf("ERR: Delay lasted %" PRIu64 " instead of %d\n", t2 - t1,
                 udelay_times[i]);
          errors = true;
        }

      leave_critical_section(flags);
    }

  printf("udelay test %s\n", errors ? "FAILED" : "PASSED");

  printf ("usleep test; this will take approx. 1 hour\n");

  errors = false;
  for (i = 0; i < 60 * 60 * (1000000 / CONFIG_USEC_PER_TICK); i++)
    {
      flags = enter_critical_section();
      t1 = time_usec();
      usleep(CONFIG_USEC_PER_TICK);
      t2 = time_usec();
      leave_critical_section(flags);

      /* Allow sleeps + 2 ticks, but never shorter than 1 */

      if (t2 - t1 < CONFIG_USEC_PER_TICK ||
          t2 - t1 > 3 * CONFIG_USEC_PER_TICK)
        {
          errors = true;
          printf("ERR: Sleep lasted %" PRIu64 "us instead of %d-%dus\n",
                 t2 - t1, CONFIG_USEC_PER_TICK, 3 * CONFIG_USEC_PER_TICK);
        }

      /* printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n", t1, t2, t2 - t1); */
    }

  printf("usleep test %s\n", errors ? "FAILED" : "PASSED");

  return 0;
}
