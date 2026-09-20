// Copyright (C) Gaijin Games KFT.  All rights reserved.

#include <osApiWrappers/dag_watchDir.h>
#if _TARGET_PC_WIN
#include <osApiWrappers/dag_atomic.h>
#include <osApiWrappers/dag_miscApi.h>
#include <osApiWrappers/dag_direct.h>
#include <osApiWrappers/dag_vromfs.h>
#include <windows.h>
#include <process.h>

struct WatchedFolderMonitorData
{
  volatile HANDLE hFolder = NULL;
  volatile HANDLE hThread = NULL;
  static constexpr int BUF_SZ = (32 << 10) - 20;
  char buf[BUF_SZ];
  int sleepInterval = 30;
  volatile int changedGen = 0, lastChecked = 0;
  volatile int stop = 0;
  WatchedFolderMonitorData() { buf[0] = 0; }
};

static unsigned __stdcall monitor_folder_thread(void *p)
{
  WatchedFolderMonitorData &fd = *(WatchedFolderMonitorData *)p;

  while (!interlocked_acquire_load(fd.stop))
  {
    DWORD bytesret = 0;

    if (!ReadDirectoryChangesW(fd.hFolder, fd.buf, fd.BUF_SZ, TRUE,
          FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_FILE_NAME,
          &bytesret, NULL, NULL))
    {
      if (interlocked_acquire_load(fd.stop))
        break;
      sleep_msec(fd.sleepInterval);
      continue;
    }
    interlocked_increment(fd.changedGen); // return resulted incremented value for val
  }
  ::CloseHandle(fd.hFolder);
  fd.hFolder = NULL;
  _endthreadex(0);
  return 0;
}

#include <supp/_platform.h>

bool could_be_changed_folder(WatchedFolderMonitorData *d)
{
  if (!d)
    return true;
  WatchedFolderMonitorData &fd = *d;
  if (!fd.hFolder)
    return true;
  const bool ret = interlocked_acquire_load(fd.lastChecked) != interlocked_acquire_load(fd.changedGen);
  interlocked_release_store(fd.lastChecked, fd.changedGen);
  return ret;
}

WatchedFolderMonitorData *add_folder_monitor(const char *foldername, int sleep_interval)
{
  HANDLE hDir = CreateFile(foldername, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE | FILE_LIST_DIRECTORY,
    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

  if (hDir == INVALID_HANDLE_VALUE)
    return nullptr;

  WatchedFolderMonitorData *d = new WatchedFolderMonitorData;
  d->hFolder = hDir;
  d->sleepInterval = sleep_interval;

  uintptr_t handle = _beginthreadex(NULL, 4096, &monitor_folder_thread, d, CREATE_SUSPENDED, NULL);
  if (handle == -1)
  {
    CloseHandle(d->hFolder);
    delete d;
    return nullptr;
  }
  d->hThread = (HANDLE)handle;
  ResumeThread((HANDLE)handle);
  return d;
}

bool destroy_folder_monitor(WatchedFolderMonitorData *d, int attempts)
{
  if (!d)
    return true;
  WatchedFolderMonitorData &fd = *d;
  interlocked_release_store(fd.stop, 1);

  HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
  if (kernel32)
  {
    typedef BOOL(WINAPI * PCancelIoEx)(HANDLE hFile, LPOVERLAPPED lpOverlapped);
    PCancelIoEx pCancelIoEx = (PCancelIoEx)(void *)GetProcAddress(kernel32, "CancelIoEx"); // CancelIoEx is supported starting with
                                                                                           // Vista.
    if (pCancelIoEx)
      pCancelIoEx(fd.hFolder, NULL);
  }

  if (WaitForSingleObject(fd.hThread, attempts <= 0 ? INFINITE : fd.sleepInterval * attempts + 1) == WAIT_OBJECT_0)
  {
    CloseHandle(fd.hThread);
    delete d;
    return true;
  }
  return false;
}
#elif _TARGET_PC_LINUX

#include <sys/inotify.h>                   // inotify_*
#include <sys/eventfd.h>                   // eventfd
#include <sys/stat.h>                      // stat
#include <unistd.h>                        // close
#include <pthread.h>                       // pthread_*
#include <errno.h>                         // errno
#include <limits.h>                        // NAME_MAX
#include <poll.h>                          // poll
#include <osApiWrappers/dag_atomic.h>      // interlocked_*
#include <dirent.h>                        // opendir
#include <EASTL/string.h>                  // eastl::string
#include <EASTL/vector.h>                  // eastl::vector
#include <EASTL/unique_ptr.h>              // make_unique
#include <ska_hash_map/flat_hash_map2.hpp> // ska::flat_hash_map


struct WatchedFolderMonitorData
{
  int inotifyFd = -1;
  int eventFd = -1;
  int sleepInterval = 30;
  pthread_t thread = {};
  volatile int64_t changedGen = 0;
  volatile int64_t lastChecked = 0;
  struct WatchedData
  {
    eastl::string path;
    eastl::vector<int> children;
  };
  ska::flat_hash_map<int, WatchedData> watched;

  WatchedFolderMonitorData(int inotify_fd, int event_fd, int sleep_interval) :
    inotifyFd(inotify_fd), eventFd(event_fd), sleepInterval(sleep_interval)
  {}
  ~WatchedFolderMonitorData() { close(inotifyFd), close(eventFd); /* it's okay to close -1 */ }
};

inline eastl::string join_path(const char *base, const char *path) { return {eastl::string::CtorSprintf{}, "%s/%s", base, path}; }

static int add_watch_recursive(WatchedFolderMonitorData *fd, eastl::string &&path, int depth)
{
  constexpr int MAX_DEPTH = 30;
  if (depth == MAX_DEPTH)
    return -1;

  constexpr int mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVE;
  const int wd = inotify_add_watch(fd->inotifyFd, path.c_str(), mask);
  if (wd == -1)
    return -1;

  eastl::vector<int> children{};

  DIR *dir = opendir(path.c_str());
  if (dir)
  {
    for (struct dirent *entry = readdir(dir); entry != nullptr; entry = readdir(dir))
    {
      if (entry->d_name[0] == '.' && (entry->d_name[1] == 0 || entry->d_name[1] == '.' && entry->d_name[2] == 0))
        continue;

      eastl::string nextPath = join_path(path.c_str(), entry->d_name);
      struct stat statbuf = {};
      if (lstat(nextPath.c_str(), &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
      {
        const int childWd = add_watch_recursive(fd, eastl::move(nextPath), depth + 1);
        if (childWd != -1)
          children.push_back(childWd);
      }
    }
    closedir(dir);
  }

  fd->watched[wd] = {eastl::move(path), eastl::move(children)};
  return wd;
}

static void remove_watch_recursive(WatchedFolderMonitorData *fd, int wd)
{
  for (auto childWd : fd->watched[wd].children)
    remove_watch_recursive(fd, childWd);

  fd->watched.erase(wd);
  inotify_rm_watch(fd->inotifyFd, wd);
}

static void *monitor_folder_thread(void *p)
{
  auto *fd = reinterpret_cast<WatchedFolderMonitorData *>(p);

  pollfd pfd[] = {{fd->inotifyFd, POLLIN, 0}, {fd->eventFd, POLLIN, 0}};

  while (true)
  {
    const int polled = poll(pfd, /* pfd size */ 2, /* timeout */ -1);
    if (polled == -1)
    {
      if (errno == EINTR)
        continue;
      else
        break;
    }

    if (pfd[1].revents)
      break;

    alignas(struct inotify_event) char buff[sizeof(inotify_event) + NAME_MAX + 1];
    const int len = read(fd->inotifyFd, &buff, sizeof(buff));
    if (len < 0)
      continue;

    char *current = buff;
    while (current != buff + len)
    {
      auto *evt = reinterpret_cast<inotify_event *>(current);
      current += sizeof(inotify_event) + evt->len;

      if (evt->mask & IN_Q_OVERFLOW)
        continue; // wd is -1 here

      if (evt->mask & IN_ISDIR)
      {
        eastl::string path = join_path(fd->watched[evt->wd].path.c_str(), evt->name);
        if (evt->mask & (IN_CREATE | IN_MOVED_TO))
        {
          const int addedWd = add_watch_recursive(fd, eastl::move(path), /* depth */ 0);
          if (addedWd != -1)
            fd->watched[evt->wd].children.push_back(addedWd);
        }
        if (evt->mask & (IN_DELETE | IN_MOVED_FROM))
        {
          auto &children = fd->watched[evt->wd].children; // this ref dies after remove_watch_recursive call!
          for (auto childWdIt = children.begin(); childWdIt != children.end(); ++childWdIt)
          {
            const int childWd = *childWdIt;
            if (fd->watched[childWd].path == path)
            {
              children.erase(childWdIt);
              remove_watch_recursive(fd, childWd);
              break;
            }
          }
        }
      }
    }

    interlocked_increment(fd->changedGen);
  }

  return nullptr;
}

WatchedFolderMonitorData *add_folder_monitor(const char *foldername, int sleep_interval)
{
  auto fd = eastl::make_unique<WatchedFolderMonitorData>(inotify_init(), eventfd(0, /* flags */ 0), sleep_interval);
  if (fd->inotifyFd == -1 || fd->eventFd == -1)
    return {};
  if (add_watch_recursive(fd.get(), foldername, /* depth */ 0) == -1)
    return {};
  if (pthread_create(&fd->thread, nullptr, &monitor_folder_thread, fd.get()) != 0)
    return {};
  return fd.release();
}

bool destroy_folder_monitor(WatchedFolderMonitorData *fd, int attempts)
{
  if (!fd)
    return true;

  const uint64_t one = 1;
  if (write(fd->eventFd, &one, sizeof(one)) != ssize_t(sizeof(one)))
    return false;

  if (attempts <= 0)
  {
    pthread_join(fd->thread, /* retval */ nullptr);
  }
  else
  {
    const int ms = attempts * fd->sleepInterval; // by default the caller waits 1 sec on each thread, which is questionable
    timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_nsec += ms * 1'000'000L;
    timeout.tv_sec += timeout.tv_nsec / 1'000'000'000L;
    timeout.tv_nsec %= 1'000'000'000L;
    if (pthread_timedjoin_np(fd->thread, /* retval */ nullptr, &timeout) != 0)
      return false;
  }

  delete fd;
  return true;
}

bool could_be_changed_folder(WatchedFolderMonitorData *df)
{
  if (!df)
    return true;

  const bool ret = interlocked_acquire_load(df->lastChecked) != interlocked_acquire_load(df->changedGen);
  interlocked_release_store(df->lastChecked, df->changedGen);
  return ret;
}

#else

bool could_be_changed_folder(WatchedFolderMonitorData *) { return true; }
WatchedFolderMonitorData *add_folder_monitor(const char *, int) { return nullptr; }
bool destroy_folder_monitor(WatchedFolderMonitorData *, int) { return true; }
#endif

#define EXPORT_PULL dll_pull_osapiwrappers_directory_watch
#include <supp/exportPull.h>
