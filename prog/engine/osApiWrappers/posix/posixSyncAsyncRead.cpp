// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <osApiWrappers/dag_asyncRead.h>
#include <osApiWrappers/dag_fileIoErr.h>
#include <osApiWrappers/dag_files.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <math/dag_intrin.h>
#include <osApiWrappers/dag_atomic.h>
#include <debug/dag_debug.h>

static volatile uint64_t ovFreeBitmask = ~uint64_t(0);
static constexpr int ASYNCDATA_COUNT = sizeof(ovFreeBitmask) * CHAR_BIT;
static int ovPool[ASYNCDATA_COUNT]; // bytes read, or -errno when the read failed

// fd 0 is valid (closed stdin), so tag it to keep NULL as the only "not opened" value
#define FD2HANDLE(fd) ((void *)(intptr_t)((fd) | 0x40000000))
#define HANDLE2FD(h)  (((int)(intptr_t)(h)) & ~0x40000000)

static int use_bit()
{
  for (uint64_t cur = interlocked_relaxed_load(ovFreeBitmask); cur;)
  {
    const uint64_t prev = interlocked_compare_exchange(ovFreeBitmask, __blsr(cur), cur);
    if (prev == cur)
      return __ctz_unsafe(cur);
    cur = prev; // lost the race, retry against what the winner left
  }
  return -1;
}

static bool unuse_bit(int idx) // false when the bit was set already, the handle is freed twice
{
  const uint64_t test_bit = uint64_t(1) << idx;
  return (interlocked_or(ovFreeBitmask, test_bit) & test_bit) == 0;
}

void *dfa_open_for_read(const char *fpath, bool /*non_cached*/)
{
  int fd = open(fpath, O_RDONLY);
  if (fd < 0)
  {
    if (dag_on_file_not_found)
      dag_on_file_not_found(fpath);
    return NULL;
  }
  void *h = FD2HANDLE(fd);
  if (dag_on_file_open)
    dag_on_file_open(fpath, h, DF_READ);
  return h;
}

void dfa_close(void *handle)
{
  if (!handle)
    return;
  close(HANDLE2FD(handle));
  if (dag_on_file_close)
    dag_on_file_close(handle);
}

unsigned dfa_chunk_size(const char * /*fname*/)
{
  return 2048; //==
}

int dfa_file_length(void *handle)
{
  struct stat st;
  return (fstat(HANDLE2FD(handle), &st) == 0) ? st.st_size : 0;
}

int dfa_alloc_asyncdata()
{
  int idx = use_bit();
  if (idx >= 0)
    return idx;

  DEBUG_CTX("no more free handles");
  return -1;
}

void dfa_free_asyncdata(int data_handle)
{
  if ((unsigned)data_handle >= ASYNCDATA_COUNT)
  {
    DEBUG_CTX("incorrect handle: %d", data_handle);
    return;
  }

  if (!unuse_bit(data_handle))
    DEBUG_CTX("already freed handle: %d", data_handle);
}

bool dfa_read_async(void *handle, int asyncdata_handle, int offset, void *buf, int len)
{
  G_ASSERT((unsigned)asyncdata_handle < ASYNCDATA_COUNT);

  ssize_t rd;
  int err = 0;
  while ((rd = pread(HANDLE2FD(handle), buf, len, offset)) < 0) [[unlikely]]
  {
    err = errno;
    if (err == EINTR)
      continue;
    if (!dag_on_read_error_cb || !dag_on_read_error_cb(handle, offset, len))
      break;
  }

  ovPool[asyncdata_handle] = rd < 0 ? -err : (int)rd;
  return rd >= 0;
}

bool dfa_check_complete(int asyncdata_handle, int *read_len)
{
  G_ASSERT((unsigned)asyncdata_handle < ASYNCDATA_COUNT);
  *read_len = ovPool[asyncdata_handle];
  return true;
}

#define EXPORT_PULL dll_pull_osapiwrappers_asyncRead
#include <supp/exportPull.h>
