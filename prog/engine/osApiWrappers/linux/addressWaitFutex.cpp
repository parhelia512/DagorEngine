// Copyright (C) Gaijin Games KFT.  All rights reserved.

// Native Linux/Android impl for os_wait_on_address using futex
// The kernel compares *addr == *cmpaddr atomically before parking the thread, which closes the
// lost-wake race without any userspace lock -- unlike the std::condition_variable bucket fallback
// in addressWaitStdCondVar.cpp (used on platforms that lack a native wait-on-address primitive).

#include <osApiWrappers/dag_addressWait.h>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <climits>
#include <ctime>

// FUTEX_*_PRIVATE: same-process only, so the kernel skips the global inode hash -> cheaper.
// wait and wake must agree on the private flag; both use it here.
static inline long dag_futex(volatile uint32_t *addr, int op, uint32_t val, const struct timespec *ts)
{
  return syscall(SYS_futex, const_cast<uint32_t *>(addr), op | FUTEX_PRIVATE_FLAG, val, ts, nullptr, 0);
}

void os_wait_on_address(volatile uint32_t *addr, const uint32_t *cmpaddr, int wait_ms)
{
  const uint32_t expected = *cmpaddr;
  if (wait_ms < 0)
    dag_futex(addr, FUTEX_WAIT, expected, nullptr);
  else
  {
    // note: FUTEX_WAIT timeout parameter is relative value, i.e. wait from current time
    const struct timespec ts = {wait_ms / 1000, (long)(wait_ms % 1000) * 1000000L};
    dag_futex(addr, FUTEX_WAIT, expected, &ts);
  }
}

void os_wake_on_address_all(volatile uint32_t *addr) { dag_futex(addr, FUTEX_WAKE, INT_MAX, nullptr); }

void os_wake_on_address_one(volatile uint32_t *addr) { dag_futex(addr, FUTEX_WAKE, 1, nullptr); }
