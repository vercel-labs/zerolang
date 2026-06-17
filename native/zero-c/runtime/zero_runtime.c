#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "zero_runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <conio.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#include <windows.h>
typedef int ZeroWriteResult;
typedef int ZeroReadResult;
typedef struct _stat ZeroRuntimeStat;
#define ZERO_RUNTIME_WRITE _write
#define ZERO_RUNTIME_OPEN _open
#define ZERO_RUNTIME_READ _read
#define ZERO_RUNTIME_CLOSE _close
#define ZERO_RUNTIME_FSTAT _fstat
#define ZERO_RUNTIME_LSEEK _lseeki64
#define ZERO_RUNTIME_OPEN_FLAGS (_O_RDONLY | _O_BINARY)
#define ZERO_RUNTIME_IS_REGULAR(mode) (((mode) & _S_IFREG) != 0)
#define ZERO_RUNTIME_ISATTY _isatty
#define ZERO_RUNTIME_STDIN_FD 0
#define ZERO_RUNTIME_STDOUT_FD 1
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#else
#include <signal.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
typedef ssize_t ZeroWriteResult;
typedef ssize_t ZeroReadResult;
typedef struct stat ZeroRuntimeStat;
#define ZERO_RUNTIME_WRITE write
#define ZERO_RUNTIME_OPEN open
#define ZERO_RUNTIME_READ read
#define ZERO_RUNTIME_CLOSE close
#define ZERO_RUNTIME_FSTAT fstat
#define ZERO_RUNTIME_LSEEK lseek
#define ZERO_RUNTIME_OPEN_FLAGS O_RDONLY
#define ZERO_RUNTIME_IS_REGULAR(mode) S_ISREG(mode)
#define ZERO_RUNTIME_ISATTY isatty
#define ZERO_RUNTIME_STDIN_FD STDIN_FILENO
#define ZERO_RUNTIME_STDOUT_FD STDOUT_FILENO
#endif

#define ZERO_RUNTIME_PATH_BYTES 4096u
#define ZERO_RUNTIME_READ_CHUNK 1048576u
#define ZERO_RUNTIME_PROC_COMMAND_BYTES 4096u
#define ZERO_RUNTIME_PROC_MAX_ARGS 64u
#define ZERO_RUNTIME_PROC_CHILD_MAX 64u

uint32_t zero_rand_entropy_u32(void) {
#if defined(_WIN32)
  LARGE_INTEGER counter;
  QueryPerformanceCounter(&counter);
  uint64_t value = (uint64_t)counter.QuadPart ^ (uint64_t)GetTickCount64() ^ ((uint64_t)GetCurrentProcessId() << 32);
  value ^= value >> 33;
  value *= 0xff51afd7ed558ccdull;
  value ^= value >> 33;
  return (uint32_t)value;
#else
  uint32_t value = 0;
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0) return 0;
  ssize_t got = read(fd, &value, sizeof(value));
  close(fd);
  return got == (ssize_t)sizeof(value) ? value : 0;
#endif
}

static ZeroMaybeUsize zero_runtime_none_usize(void) {
  return (ZeroMaybeUsize){0, 0};
}

static int zero_runtime_path_copy(ZeroByteView path, char out[ZERO_RUNTIME_PATH_BYTES]) {
  if ((!path.ptr && path.len > 0) || path.len == 0 || path.len >= ZERO_RUNTIME_PATH_BYTES) return 0;
  memcpy(out, path.ptr, path.len);
  out[path.len] = '\0';
  return 1;
}

static int zero_runtime_open_readonly(const char *path) {
  int fd;
  do {
    fd = ZERO_RUNTIME_OPEN(path, ZERO_RUNTIME_OPEN_FLAGS);
  } while (fd < 0 && errno == EINTR);
  return fd;
}

static int zero_runtime_open_write_truncate(const char *path) {
#if defined(_WIN32)
  int fd;
  do {
    fd = ZERO_RUNTIME_OPEN(path, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
  } while (fd < 0 && errno == EINTR);
  return fd;
#else
  int fd;
  do {
    fd = ZERO_RUNTIME_OPEN(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  } while (fd < 0 && errno == EINTR);
  return fd;
#endif
}

static int zero_runtime_fd_regular_size(int fd, uint64_t *size_out) {
  ZeroRuntimeStat st;
  if (ZERO_RUNTIME_FSTAT(fd, &st) != 0) return 0;
  if (!ZERO_RUNTIME_IS_REGULAR(st.st_mode)) return 0;
  if (size_out) *size_out = st.st_size < 0 ? 0 : (uint64_t)st.st_size;
  return 1;
}

static int zero_runtime_close_fd(int fd) {
  return ZERO_RUNTIME_CLOSE(fd) == 0;
}

static int zero_runtime_read_fd(int fd, ZeroMutByteView buffer, uint64_t *read_len_out) {
  size_t total = 0;
  while (total < buffer.len) {
    size_t remaining = buffer.len - total;
    unsigned chunk = remaining > ZERO_RUNTIME_READ_CHUNK ? ZERO_RUNTIME_READ_CHUNK : (unsigned)remaining;
    ZeroReadResult read_len = ZERO_RUNTIME_READ(fd, buffer.ptr + total, chunk);
    if (read_len < 0) {
      if (errno == EINTR) continue;
      return 0;
    }
    if (read_len == 0) break;
    total += (size_t)read_len;
  }
  if (read_len_out) *read_len_out = (uint64_t)total;
  return 1;
}

int zero_world_write(int fd, const char *buf, unsigned len) {
  if (len == 0) return 0;
  if (!buf) return 1;
  const char *cursor = buf;
  unsigned remaining = len;
  while (remaining > 0) {
    ZeroWriteResult written = ZERO_RUNTIME_WRITE(fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      return 1;
    }
    if (written == 0) return 1;
    cursor += (unsigned)written;
    remaining -= (unsigned)written;
  }
  return 0;
}

ZeroMaybeUsize zero_fs_read_bytes(ZeroByteView path, ZeroMutByteView buffer) {
  if (!buffer.ptr) return zero_runtime_none_usize();
  char path_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_path_copy(path, path_buf)) return zero_runtime_none_usize();

  int fd = zero_runtime_open_readonly(path_buf);
  if (fd < 0) return zero_runtime_none_usize();

  uint64_t total = 0;
  uint64_t read_len = 0;
  int ok = zero_runtime_fd_regular_size(fd, &total) && zero_runtime_read_fd(fd, buffer, &read_len);
  int closed = zero_runtime_close_fd(fd);
  if (!ok || !closed) return zero_runtime_none_usize();
  /* snprintf convention: report the total file size so callers can detect
     truncation when the value exceeds the buffer length. */
  if (read_len > total) total = read_len;
  return (ZeroMaybeUsize){1, total};
}

ZeroMaybeUsize zero_fs_read_bytes_at(ZeroByteView path, uint64_t offset, ZeroMutByteView buffer) {
  if (!buffer.ptr) return zero_runtime_none_usize();
  char path_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_path_copy(path, path_buf)) return zero_runtime_none_usize();

  int fd = zero_runtime_open_readonly(path_buf);
  if (fd < 0) return zero_runtime_none_usize();

  uint64_t total = 0;
  uint64_t read_len = 0;
  int ok = zero_runtime_fd_regular_size(fd, &total);
  if (ok && offset < total && buffer.len > 0) {
    ok = ZERO_RUNTIME_LSEEK(fd, (int64_t)offset, SEEK_SET) == (int64_t)offset &&
         zero_runtime_read_fd(fd, buffer, &read_len);
  }
  int closed = zero_runtime_close_fd(fd);
  if (!ok || !closed) return zero_runtime_none_usize();
  /* snprintf convention: report the total file size so callers can loop
     offset += len(buffer) until offset reaches the returned total. */
  if (read_len > 0 && offset + read_len > total) total = offset + read_len;
  return (ZeroMaybeUsize){1, total};
}

ZeroMaybeUsize zero_fs_write_bytes(ZeroByteView path, ZeroByteView bytes) {
  if (!bytes.ptr && bytes.len > 0) return zero_runtime_none_usize();
  char path_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_path_copy(path, path_buf)) return zero_runtime_none_usize();
  int fd = zero_runtime_open_write_truncate(path_buf);
  if (fd < 0) return zero_runtime_none_usize();

  size_t total = 0;
  int ok = 1;
  while (total < bytes.len) {
    size_t remaining = bytes.len - total;
    unsigned chunk = remaining > ZERO_RUNTIME_READ_CHUNK ? ZERO_RUNTIME_READ_CHUNK : (unsigned)remaining;
    ZeroWriteResult written = ZERO_RUNTIME_WRITE(fd, bytes.ptr + total, chunk);
    if (written < 0) {
      if (errno == EINTR) continue;
      ok = 0;
      break;
    }
    if (written == 0) {
      ok = 0;
      break;
    }
    total += (size_t)written;
  }
  int closed = zero_runtime_close_fd(fd);
  if (!ok || !closed) return zero_runtime_none_usize();
  return (ZeroMaybeUsize){1, (uint64_t)total};
}

static int zero_runtime_stat_path(const char *path, ZeroRuntimeStat *st) {
  if (!path || !path[0] || !st) return 0;
#if defined(_WIN32)
  return _stat(path, st) == 0;
#else
  return stat(path, st) == 0;
#endif
}

uint32_t zero_fs_path_op(ZeroByteView path, uint32_t op) {
  char path_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_path_copy(path, path_buf)) return 0;
  switch ((ZeroFsPathOp)op) {
    case ZERO_FS_PATH_EXISTS: {
      ZeroRuntimeStat st;
      return zero_runtime_stat_path(path_buf, &st) ? 1u : 0u;
    }
    case ZERO_FS_PATH_IS_DIR: {
      ZeroRuntimeStat st;
      if (!zero_runtime_stat_path(path_buf, &st)) return 0;
#if defined(_WIN32)
      return (st.st_mode & _S_IFDIR) != 0 ? 1u : 0u;
#else
      return S_ISDIR(st.st_mode) ? 1u : 0u;
#endif
    }
    case ZERO_FS_PATH_MAKE_DIR:
#if defined(_WIN32)
      return _mkdir(path_buf) == 0 ? 1u : 0u;
#else
      return mkdir(path_buf, 0777) == 0 ? 1u : 0u;
#endif
    case ZERO_FS_PATH_REMOVE_DIR:
#if defined(_WIN32)
      return _rmdir(path_buf) == 0 ? 1u : 0u;
#else
      return rmdir(path_buf) == 0 ? 1u : 0u;
#endif
    case ZERO_FS_PATH_REMOVE:
#if defined(_WIN32)
      return _unlink(path_buf) == 0 ? 1u : 0u;
#else
      return unlink(path_buf) == 0 ? 1u : 0u;
#endif
  }
  return 0;
}

uint32_t zero_fs_rename(ZeroByteView from_path, ZeroByteView to_path) {
  char from_buf[ZERO_RUNTIME_PATH_BYTES];
  char to_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_path_copy(from_path, from_buf) || !zero_runtime_path_copy(to_path, to_buf)) return 0;
  return rename(from_buf, to_buf) == 0 ? 1u : 0u;
}

static int zero_runtime_ascii_space(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\f' || byte == '\v';
}

static int zero_runtime_proc_parse_command(ZeroByteView command, char storage[ZERO_RUNTIME_PROC_COMMAND_BYTES], char *argv[ZERO_RUNTIME_PROC_MAX_ARGS]) {
  if (!command.ptr || command.len == 0 || command.len >= ZERO_RUNTIME_PROC_COMMAND_BYTES) return 0;
  for (size_t i = 0; i < command.len; i++) {
    if (command.ptr[i] == 0) return 0;
  }

  size_t read = 0;
  size_t write = 0;
  size_t argc = 0;
  while (read < command.len) {
    while (read < command.len && zero_runtime_ascii_space(command.ptr[read])) read++;
    if (read >= command.len) break;
    if (argc + 1 >= ZERO_RUNTIME_PROC_MAX_ARGS) return 0;

    size_t token_start = write;
    argv[argc++] = &storage[token_start];
    while (read < command.len && !zero_runtime_ascii_space(command.ptr[read])) {
      if (command.ptr[read] == '\'') {
        read++;
        int closed = 0;
        while (read < command.len) {
          if (command.ptr[read] == '\'') {
            closed = 1;
            read++;
            break;
          }
          storage[write++] = (char)command.ptr[read++];
        }
        if (!closed) return 0;
      } else if (command.ptr[read] == '\\' && read + 1 < command.len) {
        read++;
        storage[write++] = (char)command.ptr[read++];
      } else {
        storage[write++] = (char)command.ptr[read++];
      }
    }
    if (write == token_start) {
      argc--;
    } else {
      storage[write++] = 0;
    }
  }
  if (argc == 0) return 0;
  argv[argc] = NULL;
  return 1;
}

int32_t zero_proc_spawn_inherit(ZeroByteView command) {
  char storage[ZERO_RUNTIME_PROC_COMMAND_BYTES];
  char *argv[ZERO_RUNTIME_PROC_MAX_ARGS];
  if (!zero_runtime_proc_parse_command(command, storage, argv)) return 127;

#if defined(_WIN32)
  (void)argv;
  return 127;
#else
  pid_t pid = fork();
  if (pid < 0) return 127;

  if (pid == 0) {
    execvp(argv[0], argv);
    _exit(127);
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    return 127;
  }
  if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int32_t)(128 + WTERMSIG(status));
  return 127;
#endif
}

ZeroMaybeUsize zero_proc_capture(ZeroByteView command, ZeroMutByteView buffer) {
  if (!buffer.ptr) return zero_runtime_none_usize();
  char storage[ZERO_RUNTIME_PROC_COMMAND_BYTES];
  char *argv[ZERO_RUNTIME_PROC_MAX_ARGS];
  if (!zero_runtime_proc_parse_command(command, storage, argv)) return zero_runtime_none_usize();

#if defined(_WIN32)
  (void)argv;
  return zero_runtime_none_usize();
#else
  int pipe_fd[2];
  if (pipe(pipe_fd) != 0) return zero_runtime_none_usize();

  pid_t pid = fork();
  if (pid < 0) {
    ZERO_RUNTIME_CLOSE(pipe_fd[0]);
    ZERO_RUNTIME_CLOSE(pipe_fd[1]);
    return zero_runtime_none_usize();
  }

  if (pid == 0) {
    ZERO_RUNTIME_CLOSE(pipe_fd[0]);
    if (dup2(pipe_fd[1], STDOUT_FILENO) < 0) _exit(127);
    ZERO_RUNTIME_CLOSE(pipe_fd[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  ZERO_RUNTIME_CLOSE(pipe_fd[1]);
  uint64_t total = 0;
  int too_large = 0;
  int read_ok = 1;
  unsigned char chunk[4096];
  for (;;) {
    ZeroReadResult n = ZERO_RUNTIME_READ(pipe_fd[0], chunk, sizeof(chunk));
    if (n < 0) {
      if (errno == EINTR) continue;
      read_ok = 0;
      break;
    }
    if (n == 0) break;
    for (ZeroReadResult i = 0; i < n; i++) {
      if (total < buffer.len) {
        buffer.ptr[total] = chunk[i];
      } else {
        too_large = 1;
      }
      total++;
    }
  }
  ZERO_RUNTIME_CLOSE(pipe_fd[0]);

  int status = 0;
  int wait_ok = 1;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    wait_ok = 0;
    break;
  }
  if (!read_ok || !wait_ok || too_large) return zero_runtime_none_usize();
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return zero_runtime_none_usize();
  return (ZeroMaybeUsize){1, total};
#endif
}

int32_t zero_proc_capture_files(ZeroByteView command, ZeroByteView stdout_path, ZeroByteView stderr_path) {
  char storage[ZERO_RUNTIME_PROC_COMMAND_BYTES];
  char *argv[ZERO_RUNTIME_PROC_MAX_ARGS];
  char stdout_buf[ZERO_RUNTIME_PATH_BYTES];
  char stderr_buf[ZERO_RUNTIME_PATH_BYTES];
  if (!zero_runtime_proc_parse_command(command, storage, argv) ||
      !zero_runtime_path_copy(stdout_path, stdout_buf) ||
      !zero_runtime_path_copy(stderr_path, stderr_buf)) {
    return 127;
  }

#if defined(_WIN32)
  (void)argv;
  return 127;
#else
  int stdout_fd = zero_runtime_open_write_truncate(stdout_buf);
  if (stdout_fd < 0) return 127;
  int stderr_fd = zero_runtime_open_write_truncate(stderr_buf);
  if (stderr_fd < 0) {
    ZERO_RUNTIME_CLOSE(stdout_fd);
    return 127;
  }

  pid_t pid = fork();
  if (pid < 0) {
    ZERO_RUNTIME_CLOSE(stdout_fd);
    ZERO_RUNTIME_CLOSE(stderr_fd);
    return 127;
  }

  if (pid == 0) {
    if (dup2(stdout_fd, STDOUT_FILENO) < 0) _exit(127);
    if (dup2(stderr_fd, STDERR_FILENO) < 0) _exit(127);
    ZERO_RUNTIME_CLOSE(stdout_fd);
    ZERO_RUNTIME_CLOSE(stderr_fd);
    execvp(argv[0], argv);
    _exit(127);
  }

  ZERO_RUNTIME_CLOSE(stdout_fd);
  ZERO_RUNTIME_CLOSE(stderr_fd);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    return 127;
  }
  if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int32_t)(128 + WTERMSIG(status));
  return 127;
#endif
}

#if !defined(_WIN32)
typedef struct {
  int active;
  int reaped;
  int status;
  pid_t pid;
  int stdin_fd;
  int stdout_fd;
  int stderr_fd;
} ZeroRuntimeProcChild;

static ZeroRuntimeProcChild zero_proc_children[ZERO_RUNTIME_PROC_CHILD_MAX];
static int zero_proc_sigpipe_ignored;

static int zero_proc_child_index(int32_t child) {
  if (child <= 0 || child > (int32_t)ZERO_RUNTIME_PROC_CHILD_MAX) return -1;
  int index = child - 1;
  return zero_proc_children[index].active ? index : -1;
}

static int zero_proc_child_alloc(void) {
  for (int i = 0; i < (int)ZERO_RUNTIME_PROC_CHILD_MAX; i++) {
    if (!zero_proc_children[i].active) return i;
  }
  return -1;
}

static void zero_proc_child_close_fd(int *fd) {
  if (*fd >= 0) {
    ZERO_RUNTIME_CLOSE(*fd);
    *fd = -1;
  }
}

static void zero_proc_child_close_pipes(ZeroRuntimeProcChild *child) {
  if (!child) return;
  zero_proc_child_close_fd(&child->stdin_fd);
  zero_proc_child_close_fd(&child->stdout_fd);
  zero_proc_child_close_fd(&child->stderr_fd);
}

static int zero_proc_child_set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return 0;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int32_t zero_proc_status_from_wait(int status) {
  if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return (int32_t)(128 + WTERMSIG(status));
  return 127;
}

static int zero_proc_child_reap(ZeroRuntimeProcChild *child, int nohang) {
  if (!child || !child->active) return 0;
  if (child->reaped) return 1;
  int raw = 0;
  for (;;) {
    pid_t got = waitpid(child->pid, &raw, nohang ? WNOHANG : 0);
    if (got == 0) return 0;
    if (got < 0) {
      if (errno == EINTR) continue;
      child->status = 127;
      child->reaped = 1;
      return 1;
    }
    child->status = zero_proc_status_from_wait(raw);
    child->reaped = 1;
    return 1;
  }
}
#endif

int32_t zero_proc_spawn_child(ZeroByteView command) {
#if defined(_WIN32)
  (void)command;
  return 0;
#else
  char storage[ZERO_RUNTIME_PROC_COMMAND_BYTES];
  char *argv[ZERO_RUNTIME_PROC_MAX_ARGS];
  if (!zero_runtime_proc_parse_command(command, storage, argv)) return 0;
  int index = zero_proc_child_alloc();
  if (index < 0) return 0;

  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    if (stdin_pipe[0] >= 0) { ZERO_RUNTIME_CLOSE(stdin_pipe[0]); ZERO_RUNTIME_CLOSE(stdin_pipe[1]); }
    if (stdout_pipe[0] >= 0) { ZERO_RUNTIME_CLOSE(stdout_pipe[0]); ZERO_RUNTIME_CLOSE(stdout_pipe[1]); }
    if (stderr_pipe[0] >= 0) { ZERO_RUNTIME_CLOSE(stderr_pipe[0]); ZERO_RUNTIME_CLOSE(stderr_pipe[1]); }
    return 0;
  }

  pid_t pid = fork();
  if (pid < 0) {
    ZERO_RUNTIME_CLOSE(stdin_pipe[0]); ZERO_RUNTIME_CLOSE(stdin_pipe[1]);
    ZERO_RUNTIME_CLOSE(stdout_pipe[0]); ZERO_RUNTIME_CLOSE(stdout_pipe[1]);
    ZERO_RUNTIME_CLOSE(stderr_pipe[0]); ZERO_RUNTIME_CLOSE(stderr_pipe[1]);
    return 0;
  }

  if (pid == 0) {
    ZERO_RUNTIME_CLOSE(stdin_pipe[1]);
    ZERO_RUNTIME_CLOSE(stdout_pipe[0]);
    ZERO_RUNTIME_CLOSE(stderr_pipe[0]);
    if (dup2(stdin_pipe[0], STDIN_FILENO) < 0) _exit(127);
    if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) _exit(127);
    if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) _exit(127);
    ZERO_RUNTIME_CLOSE(stdin_pipe[0]);
    ZERO_RUNTIME_CLOSE(stdout_pipe[1]);
    ZERO_RUNTIME_CLOSE(stderr_pipe[1]);
    execvp(argv[0], argv);
    _exit(127);
  }

  ZERO_RUNTIME_CLOSE(stdin_pipe[0]);
  ZERO_RUNTIME_CLOSE(stdout_pipe[1]);
  ZERO_RUNTIME_CLOSE(stderr_pipe[1]);
  if (!zero_proc_child_set_nonblock(stdin_pipe[1]) ||
      !zero_proc_child_set_nonblock(stdout_pipe[0]) ||
      !zero_proc_child_set_nonblock(stderr_pipe[0])) {
    ZERO_RUNTIME_CLOSE(stdin_pipe[1]);
    ZERO_RUNTIME_CLOSE(stdout_pipe[0]);
    ZERO_RUNTIME_CLOSE(stderr_pipe[0]);
    kill(pid, SIGTERM);
    return 0;
  }
  if (!zero_proc_sigpipe_ignored) {
    signal(SIGPIPE, SIG_IGN);
    zero_proc_sigpipe_ignored = 1;
  }

  zero_proc_children[index] = (ZeroRuntimeProcChild){
    .active = 1,
    .reaped = 0,
    .status = 127,
    .pid = pid,
    .stdin_fd = stdin_pipe[1],
    .stdout_fd = stdout_pipe[0],
    .stderr_fd = stderr_pipe[0],
  };
  return (int32_t)(index + 1);
#endif
}

int32_t zero_proc_child_op(int32_t child, uint32_t op) {
#if defined(_WIN32)
  (void)child;
  (void)op;
  return 0;
#else
  int index = zero_proc_child_index(child);
  if (index < 0) return op == ZERO_PROC_CHILD_OP_WAIT ? 127 : 0;
  ZeroRuntimeProcChild *slot = &zero_proc_children[index];
  switch ((ZeroProcChildOp)op) {
    case ZERO_PROC_CHILD_OP_VALID:
      return 1;
    case ZERO_PROC_CHILD_OP_RUNNING:
      return zero_proc_child_reap(slot, 1) ? 0 : 1;
    case ZERO_PROC_CHILD_OP_WAIT:
      zero_proc_child_reap(slot, 0);
      return slot->status;
    case ZERO_PROC_CHILD_OP_KILL:
      if (zero_proc_child_reap(slot, 1)) return 1;
      return kill(slot->pid, SIGTERM) == 0 ? 1 : 0;
    case ZERO_PROC_CHILD_OP_CLOSE:
      if (!slot->reaped) {
        kill(slot->pid, SIGTERM);
        zero_proc_child_reap(slot, 1);
      }
      zero_proc_child_close_pipes(slot);
      slot->active = 0;
      return 1;
    default:
      return 0;
  }
#endif
}

ZeroMaybeUsize zero_proc_child_io(int32_t child, ZeroMutByteView buffer, uint32_t op) {
  if (!buffer.ptr && buffer.len > 0) return zero_runtime_none_usize();
#if defined(_WIN32)
  (void)child;
  (void)op;
  return zero_runtime_none_usize();
#else
  int index = zero_proc_child_index(child);
  if (index < 0) return zero_runtime_none_usize();
  ZeroRuntimeProcChild *slot = &zero_proc_children[index];
  int fd = -1;
  if (op == ZERO_PROC_CHILD_IO_READ_STDOUT) fd = slot->stdout_fd;
  else if (op == ZERO_PROC_CHILD_IO_READ_STDERR) fd = slot->stderr_fd;
  else if (op == ZERO_PROC_CHILD_IO_WRITE_STDIN) fd = slot->stdin_fd;
  else return zero_runtime_none_usize();
  if (fd < 0) return zero_runtime_none_usize();

  if (op == ZERO_PROC_CHILD_IO_WRITE_STDIN) {
    if (buffer.len == 0) return (ZeroMaybeUsize){1, 0};
    ZeroWriteResult written = ZERO_RUNTIME_WRITE(fd, buffer.ptr, buffer.len > (size_t)UINT_MAX ? UINT_MAX : (unsigned)buffer.len);
    if (written < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return zero_runtime_none_usize();
      zero_proc_child_close_fd(&slot->stdin_fd);
      return zero_runtime_none_usize();
    }
    return (ZeroMaybeUsize){1, (uint64_t)written};
  }

  if (buffer.len == 0) return zero_runtime_none_usize();
  ZeroReadResult got = ZERO_RUNTIME_READ(fd, buffer.ptr, buffer.len > (size_t)UINT_MAX ? UINT_MAX : (unsigned)buffer.len);
  if (got < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return zero_runtime_none_usize();
    if (op == ZERO_PROC_CHILD_IO_READ_STDOUT) zero_proc_child_close_fd(&slot->stdout_fd);
    else zero_proc_child_close_fd(&slot->stderr_fd);
    return zero_runtime_none_usize();
  }
  if (got == 0) {
    if (op == ZERO_PROC_CHILD_IO_READ_STDOUT) zero_proc_child_close_fd(&slot->stdout_fd);
    else zero_proc_child_close_fd(&slot->stderr_fd);
    return zero_runtime_none_usize();
  }
  return (ZeroMaybeUsize){1, (uint64_t)got};
#endif
}

uint64_t zero_parse_u32(ZeroByteView text) {
  if ((!text.ptr && text.len > 0) || text.len == 0) return 0;
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (byte < '0' || byte > '9') return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > (UINT32_MAX - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  return (1ull << 32) | total;
}

uint64_t zero_parse_i32(ZeroByteView text) {
  if ((!text.ptr && text.len > 0) || text.len == 0) return 0;
  size_t index = 0;
  uint32_t limit = 2147483647u;
  uint32_t negative = 0;
  if (text.ptr[0] == '-') {
    negative = 1;
    limit = 2147483648u;
    index = 1;
  } else if (text.ptr[0] == '+') {
    index = 1;
  }
  if (index == text.len) return 0;
  uint64_t total = 0;
  for (; index < text.len; index++) {
    unsigned char byte = text.ptr[index];
    if (byte < '0' || byte > '9') return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > ((uint64_t)limit - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  int32_t value = negative ? (total == 2147483648ull ? INT32_MIN : -(int32_t)total) : (int32_t)total;
  return (1ull << 32) | (uint32_t)value;
}

uint32_t zero_fmt_u32(ZeroMutByteView buffer, uint32_t value) {
  if (!buffer.ptr) return 0;
  unsigned char tmp[10];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint32_t zero_fmt_bool(ZeroMutByteView buffer, uint32_t value) {
  static const unsigned char true_text[] = {'t', 'r', 'u', 'e'};
  static const unsigned char false_text[] = {'f', 'a', 'l', 's', 'e'};
  const unsigned char *text = value ? true_text : false_text;
  size_t len = value ? sizeof(true_text) : sizeof(false_text);
  if (!buffer.ptr || len > buffer.len) return 0;
  memcpy(buffer.ptr, text, len);
  return (uint32_t)len;
}

uint32_t zero_fmt_hex_lower_u32(ZeroMutByteView buffer, uint32_t value) {
  if (!buffer.ptr) return 0;
  static const unsigned char digits[] = "0123456789abcdef";
  unsigned char tmp[8];
  size_t len = 0;
  do {
    tmp[len++] = digits[value & 0xfu];
    value >>= 4;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint32_t zero_fmt_i32(ZeroMutByteView buffer, int32_t value) {
  if (!buffer.ptr) return 0;
  if (value >= 0) return zero_fmt_u32(buffer, (uint32_t)value);
  if (buffer.len == 0) return 0;
  buffer.ptr[0] = '-';
  uint32_t magnitude = value == INT32_MIN ? 2147483648u : (uint32_t)(-value);
  ZeroMutByteView rest = {buffer.ptr + 1, buffer.len - 1};
  uint32_t written = zero_fmt_u32(rest, magnitude);
  return written ? written + 1 : 0;
}

uint32_t zero_fmt_usize(ZeroMutByteView buffer, uint64_t value) {
  if (!buffer.ptr) return 0;
  unsigned char tmp[20];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (len > buffer.len) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[i] = tmp[len - 1 - i];
  return (uint32_t)len;
}

uint64_t zero_args_find(uint64_t argc, const char *const *argv, ZeroByteView name) {
  if (!argv || (!name.ptr && name.len > 0)) return 0;
  for (uint64_t i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (!arg) continue;
    size_t len = strlen(arg);
    if (len == name.len && memcmp(arg, name.ptr, len) == 0) {
      return (1ull << 32) | (i & 0xffffffffull);
    }
  }
  return 0;
}

uint32_t zero_ascii_op(uint32_t byte, uint32_t op) {
  byte &= 0xffu;
  uint32_t is_digit = byte >= '0' && byte <= '9';
  uint32_t is_lower = byte >= 'a' && byte <= 'z';
  uint32_t is_upper = byte >= 'A' && byte <= 'Z';
  uint32_t is_alpha = is_lower || is_upper;
  switch (op) {
    case ZERO_ASCII_OP_IS_DIGIT:
      return is_digit;
    case ZERO_ASCII_OP_IS_LOWER:
      return is_lower;
    case ZERO_ASCII_OP_IS_UPPER:
      return is_upper;
    case ZERO_ASCII_OP_IS_ALPHA:
      return is_alpha;
    case ZERO_ASCII_OP_IS_ALNUM:
      return is_alpha || is_digit;
    case ZERO_ASCII_OP_IS_WHITESPACE:
      return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
    case ZERO_ASCII_OP_IS_HEX_DIGIT:
      return is_digit || (byte >= 'A' && byte <= 'F') || (byte >= 'a' && byte <= 'f');
    case ZERO_ASCII_OP_TO_LOWER:
      return is_upper ? byte + ('a' - 'A') : byte;
    case ZERO_ASCII_OP_TO_UPPER:
      return is_lower ? byte - ('a' - 'A') : byte;
    case ZERO_ASCII_OP_DIGIT_VALUE:
      return is_digit ? 0x100u | (byte - '0') : 0;
    case ZERO_ASCII_OP_HEX_VALUE:
      if (is_digit) return 0x100u | (byte - '0');
      if (byte >= 'A' && byte <= 'F') return 0x100u | (byte - 'A' + 10u);
      if (byte >= 'a' && byte <= 'f') return 0x100u | (byte - 'a' + 10u);
      return 0;
    default:
      return 0;
  }
}

static int zero_str_view_valid(ZeroByteView view) {
  return view.ptr || view.len == 0;
}

static uint32_t zero_str_some_len(size_t len) {
  if (len > UINT32_MAX - 1u) return 0;
  return (uint32_t)(len + 1u);
}

static unsigned char zero_str_lower(unsigned char byte) {
  return byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte + ('a' - 'A')) : byte;
}

static unsigned char zero_str_upper(unsigned char byte) {
  return byte >= 'a' && byte <= 'z' ? (unsigned char)(byte - ('a' - 'A')) : byte;
}

typedef struct {
  uint32_t state[8];
  uint64_t bit_len;
  unsigned char block[64];
  size_t block_len;
} ZeroSha256;

static uint32_t zero_sha256_rotr(uint32_t value, unsigned bits) {
  return (value >> bits) | (value << (32u - bits));
}

static uint32_t zero_sha256_load_be32(const unsigned char *ptr) {
  return ((uint32_t)ptr[0] << 24) |
         ((uint32_t)ptr[1] << 16) |
         ((uint32_t)ptr[2] << 8) |
         (uint32_t)ptr[3];
}

static void zero_sha256_store_be32(unsigned char *ptr, uint32_t value) {
  ptr[0] = (unsigned char)((value >> 24) & 0xffu);
  ptr[1] = (unsigned char)((value >> 16) & 0xffu);
  ptr[2] = (unsigned char)((value >> 8) & 0xffu);
  ptr[3] = (unsigned char)(value & 0xffu);
}

static void zero_sha256_transform(ZeroSha256 *sha, const unsigned char block[64]) {
  static const uint32_t k[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
  };
  uint32_t w[64];
  for (size_t i = 0; i < 16; i++) w[i] = zero_sha256_load_be32(block + i * 4u);
  for (size_t i = 16; i < 64; i++) {
    uint32_t s0 = zero_sha256_rotr(w[i - 15u], 7) ^ zero_sha256_rotr(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
    uint32_t s1 = zero_sha256_rotr(w[i - 2u], 17) ^ zero_sha256_rotr(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
    w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
  }

  uint32_t a = sha->state[0];
  uint32_t b = sha->state[1];
  uint32_t c = sha->state[2];
  uint32_t d = sha->state[3];
  uint32_t e = sha->state[4];
  uint32_t f = sha->state[5];
  uint32_t g = sha->state[6];
  uint32_t h = sha->state[7];

  for (size_t i = 0; i < 64; i++) {
    uint32_t s1 = zero_sha256_rotr(e, 6) ^ zero_sha256_rotr(e, 11) ^ zero_sha256_rotr(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + ch + k[i] + w[i];
    uint32_t s0 = zero_sha256_rotr(a, 2) ^ zero_sha256_rotr(a, 13) ^ zero_sha256_rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  sha->state[0] += a;
  sha->state[1] += b;
  sha->state[2] += c;
  sha->state[3] += d;
  sha->state[4] += e;
  sha->state[5] += f;
  sha->state[6] += g;
  sha->state[7] += h;
}

static void zero_sha256_init(ZeroSha256 *sha) {
  sha->state[0] = UINT32_C(0x6a09e667);
  sha->state[1] = UINT32_C(0xbb67ae85);
  sha->state[2] = UINT32_C(0x3c6ef372);
  sha->state[3] = UINT32_C(0xa54ff53a);
  sha->state[4] = UINT32_C(0x510e527f);
  sha->state[5] = UINT32_C(0x9b05688c);
  sha->state[6] = UINT32_C(0x1f83d9ab);
  sha->state[7] = UINT32_C(0x5be0cd19);
  sha->bit_len = 0;
  sha->block_len = 0;
}

static void zero_sha256_update(ZeroSha256 *sha, const unsigned char *bytes, size_t len) {
  if (!bytes && len > 0) return;
  while (len > 0) {
    size_t take = 64u - sha->block_len;
    if (take > len) take = len;
    memcpy(sha->block + sha->block_len, bytes, take);
    sha->block_len += take;
    bytes += take;
    len -= take;
    if (sha->block_len == 64u) {
      zero_sha256_transform(sha, sha->block);
      sha->bit_len += 512u;
      sha->block_len = 0;
    }
  }
}

static void zero_sha256_final(ZeroSha256 *sha, unsigned char digest[32]) {
  uint64_t total_bits = sha->bit_len + (uint64_t)sha->block_len * 8u;
  sha->block[sha->block_len++] = 0x80u;
  if (sha->block_len > 56u) {
    while (sha->block_len < 64u) sha->block[sha->block_len++] = 0;
    zero_sha256_transform(sha, sha->block);
    sha->block_len = 0;
  }
  while (sha->block_len < 56u) sha->block[sha->block_len++] = 0;
  for (size_t i = 0; i < 8; i++) sha->block[63u - i] = (unsigned char)((total_bits >> (i * 8u)) & 0xffu);
  zero_sha256_transform(sha, sha->block);
  for (size_t i = 0; i < 8; i++) zero_sha256_store_be32(digest + i * 4u, sha->state[i]);
}

static void zero_sha256_hash(ZeroByteView bytes, unsigned char digest[32]) {
  ZeroSha256 sha;
  zero_sha256_init(&sha);
  zero_sha256_update(&sha, bytes.ptr, bytes.len);
  zero_sha256_final(&sha, digest);
}

static void zero_sha256_hmac(ZeroByteView key, ZeroByteView bytes, unsigned char digest[32]) {
  unsigned char key_block[64];
  unsigned char key_hash[32];
  unsigned char ipad[64];
  unsigned char opad[64];
  unsigned char inner[32];
  memset(key_block, 0, sizeof(key_block));
  if (key.len > 64u) {
    zero_sha256_hash(key, key_hash);
    memcpy(key_block, key_hash, sizeof(key_hash));
  } else if (key.len > 0) {
    memcpy(key_block, key.ptr, key.len);
  }
  for (size_t i = 0; i < 64u; i++) {
    ipad[i] = (unsigned char)(key_block[i] ^ 0x36u);
    opad[i] = (unsigned char)(key_block[i] ^ 0x5cu);
  }

  ZeroSha256 sha;
  zero_sha256_init(&sha);
  zero_sha256_update(&sha, ipad, sizeof(ipad));
  zero_sha256_update(&sha, bytes.ptr, bytes.len);
  zero_sha256_final(&sha, inner);

  zero_sha256_init(&sha);
  zero_sha256_update(&sha, opad, sizeof(opad));
  zero_sha256_update(&sha, inner, sizeof(inner));
  zero_sha256_final(&sha, digest);
}

static uint32_t zero_crypto_write_digest_hex(ZeroMutByteView buffer, const unsigned char digest[32]) {
  if (buffer.len < 64u) return 0;
  static const unsigned char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < 32u; i++) {
    buffer.ptr[i * 2u] = hex[digest[i] >> 4];
    buffer.ptr[i * 2u + 1u] = hex[digest[i] & 0x0fu];
  }
  return zero_str_some_len(64u);
}

uint32_t zero_crypto_digest(ZeroMutByteView buffer, ZeroByteView bytes, uint32_t op) {
  if (!buffer.ptr || !zero_str_view_valid(bytes)) return 0;
  unsigned char digest[32];
  zero_sha256_hash(bytes, digest);
  if (op == ZERO_CRYPTO_DIGEST_SHA256) {
    if (buffer.len < 32u) return 0;
    memcpy(buffer.ptr, digest, 32u);
    return zero_str_some_len(32u);
  }
  if (op == ZERO_CRYPTO_DIGEST_SHA256_HEX) {
    return zero_crypto_write_digest_hex(buffer, digest);
  }
  return 0;
}

uint32_t zero_crypto_hmac_sha256(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView bytes) {
  if (!buffer.ptr || !zero_str_view_valid(key) || !zero_str_view_valid(bytes) || buffer.len < 32u) return 0;
  unsigned char digest[32];
  zero_sha256_hmac(key, bytes, digest);
  memcpy(buffer.ptr, digest, sizeof(digest));
  return zero_str_some_len(sizeof(digest));
}

uint32_t zero_crypto_hmac_sha256_hex(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView bytes) {
  if (!buffer.ptr || !zero_str_view_valid(key) || !zero_str_view_valid(bytes)) return 0;
  unsigned char digest[32];
  zero_sha256_hmac(key, bytes, digest);
  return zero_crypto_write_digest_hex(buffer, digest);
}

static int zero_str_ascii_space(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r';
}

static size_t zero_text_utf8_sequence_len(unsigned char first) {
  if (first < 128u) return 1;
  if (first >= 194u && first <= 223u) return 2;
  if (first >= 224u && first <= 239u) return 3;
  if (first >= 240u && first <= 244u) return 4;
  return 0;
}

static int zero_text_is_cont(unsigned char byte) {
  return byte >= 128u && byte <= 191u;
}

static uint64_t zero_text_utf8_len_encoded(ZeroByteView text, int count_codepoints) {
  if (!zero_str_view_valid(text)) return 0;
  size_t index = 0;
  uint64_t count = 0;
  while (index < text.len) {
    unsigned char first = text.ptr[index];
    size_t width = zero_text_utf8_sequence_len(first);
    if (width == 0 || index + width > text.len) return 0;
    if (width == 2) {
      if (!zero_text_is_cont(text.ptr[index + 1])) return 0;
    } else if (width == 3) {
      unsigned char second = text.ptr[index + 1];
      if (!zero_text_is_cont(second) || !zero_text_is_cont(text.ptr[index + 2])) return 0;
      if (first == 224u && second < 160u) return 0;
      if (first == 237u && second > 159u) return 0;
    } else if (width == 4) {
      unsigned char second = text.ptr[index + 1];
      if (!zero_text_is_cont(second) || !zero_text_is_cont(text.ptr[index + 2]) || !zero_text_is_cont(text.ptr[index + 3])) return 0;
      if (first == 240u && second < 144u) return 0;
      if (first == 244u && second > 143u) return 0;
    }
    index += width;
    count++;
  }
  if (!count_codepoints) return 1;
  if (count == UINT64_MAX) return 0;
  return count + 1u;
}

uint64_t zero_text_op(ZeroByteView text, uint32_t op) {
  switch (op) {
    case ZERO_TEXT_OP_IS_ASCII:
      if (!zero_str_view_valid(text)) return 0;
      for (size_t i = 0; i < text.len; i++) {
        if (text.ptr[i] >= 128u) return 0;
      }
      return 1;
    case ZERO_TEXT_OP_UTF8_VALID:
      return zero_text_utf8_len_encoded(text, 0);
    case ZERO_TEXT_OP_UTF8_LEN:
      return zero_text_utf8_len_encoded(text, 1);
    default:
      return 0;
  }
}

#define ZERO_TERM_KEY_ARROW_UP UINT32_C(1114113)
#define ZERO_TERM_KEY_ARROW_DOWN UINT32_C(1114114)
#define ZERO_TERM_KEY_ARROW_RIGHT UINT32_C(1114115)
#define ZERO_TERM_KEY_ARROW_LEFT UINT32_C(1114116)
#define ZERO_TERM_KEY_DELETE UINT32_C(1114117)

static uint32_t zero_term_utf8_key(ZeroByteView text, size_t *width_out) {
  if (text.len == 0 || !text.ptr) return 0;
  unsigned char first = text.ptr[0];
  size_t width = zero_text_utf8_sequence_len(first);
  if (width == 0 || width > text.len) return 0;
  uint32_t code = first;
  if (width == 2) {
    unsigned char second = text.ptr[1];
    if (!zero_text_is_cont(second)) return 0;
    code = ((uint32_t)(first & 0x1fu) << 6) | (uint32_t)(second & 0x3fu);
  } else if (width == 3) {
    unsigned char second = text.ptr[1];
    unsigned char third = text.ptr[2];
    if (!zero_text_is_cont(second) || !zero_text_is_cont(third)) return 0;
    if (first == 224u && second < 160u) return 0;
    if (first == 237u && second > 159u) return 0;
    code = ((uint32_t)(first & 0x0fu) << 12) | ((uint32_t)(second & 0x3fu) << 6) | (uint32_t)(third & 0x3fu);
  } else if (width == 4) {
    unsigned char second = text.ptr[1];
    unsigned char third = text.ptr[2];
    unsigned char fourth = text.ptr[3];
    if (!zero_text_is_cont(second) || !zero_text_is_cont(third) || !zero_text_is_cont(fourth)) return 0;
    if (first == 240u && second < 144u) return 0;
    if (first == 244u && second > 143u) return 0;
    code = ((uint32_t)(first & 0x07u) << 18) | ((uint32_t)(second & 0x3fu) << 12) | ((uint32_t)(third & 0x3fu) << 6) | (uint32_t)(fourth & 0x3fu);
  }
  if (width_out) *width_out = width;
  return code;
}

static uint32_t zero_term_key_decode(ZeroByteView text, int return_len) {
  if (!zero_str_view_valid(text) || text.len == 0) return 0;
  unsigned char first = text.ptr[0];
  if (first == 0x1bu) {
    if (text.len == 1) return return_len ? 1u : 27u;
    if (text.len >= 3 && (text.ptr[1] == '[' || text.ptr[1] == 'O')) {
      unsigned char code = text.ptr[2];
      if (code == 'A') return return_len ? 3u : ZERO_TERM_KEY_ARROW_UP;
      if (code == 'B') return return_len ? 3u : ZERO_TERM_KEY_ARROW_DOWN;
      if (code == 'C') return return_len ? 3u : ZERO_TERM_KEY_ARROW_RIGHT;
      if (code == 'D') return return_len ? 3u : ZERO_TERM_KEY_ARROW_LEFT;
    }
    if (text.len >= 4 && text.ptr[1] == '[' && text.ptr[2] == '3' && text.ptr[3] == '~') {
      return return_len ? 4u : ZERO_TERM_KEY_DELETE;
    }
    return 0;
  }
  if (first == '\r' || first == '\n') return return_len ? 1u : 13u;
  if (first == '\t') return return_len ? 1u : 9u;
  if (first == 0x7fu || first == 0x08u) return return_len ? 1u : 127u;
  if (first < 32u) return return_len ? 1u : (uint32_t)first;
  if (first < 128u) return return_len ? 1u : (uint32_t)first;
  size_t width = 0;
  uint32_t code = zero_term_utf8_key(text, &width);
  if (!code) return 0;
  return return_len ? (uint32_t)width : code;
}

static uint64_t zero_term_size_or(uint64_t fallback, int height) {
#if !defined(_WIN32) && defined(TIOCGWINSZ)
  struct winsize size;
  if (ioctl(ZERO_RUNTIME_STDOUT_FD, TIOCGWINSZ, &size) == 0) {
    unsigned value = height ? size.ws_row : size.ws_col;
    if (value > 0) return value;
  }
#endif
  return fallback;
}

#if defined(_WIN32)
static DWORD zero_term_original_mode;
static int zero_term_raw_active;
static int zero_term_restore_registered;

static void zero_term_restore_at_exit(void) {
  if (!zero_term_raw_active) return;
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == INVALID_HANDLE_VALUE) return;
  SetConsoleMode(input, zero_term_original_mode);
  zero_term_raw_active = 0;
}

static uint64_t zero_term_enter_raw_mode(void) {
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == INVALID_HANDLE_VALUE) return 0;
  DWORD mode = 0;
  if (!GetConsoleMode(input, &mode)) return 0;
  if (zero_term_raw_active) return 1;
  DWORD raw = mode;
  raw &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  raw |= ENABLE_VIRTUAL_TERMINAL_INPUT;
  if (!SetConsoleMode(input, raw)) return 0;
  zero_term_original_mode = mode;
  zero_term_raw_active = 1;
  if (!zero_term_restore_registered) {
    atexit(zero_term_restore_at_exit);
    zero_term_restore_registered = 1;
  }
  return 1;
}

static uint64_t zero_term_leave_raw_mode(void) {
  if (!zero_term_raw_active) return 1;
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == INVALID_HANDLE_VALUE) return 0;
  if (!SetConsoleMode(input, zero_term_original_mode)) return 0;
  zero_term_raw_active = 0;
  return 1;
}
#else
static struct termios zero_term_original_mode;
static int zero_term_raw_active;
static int zero_term_restore_registered;

static void zero_term_restore_at_exit(void) {
  if (!zero_term_raw_active) return;
  tcsetattr(ZERO_RUNTIME_STDIN_FD, TCSAFLUSH, &zero_term_original_mode);
  zero_term_raw_active = 0;
}

static uint64_t zero_term_enter_raw_mode(void) {
  if (!ZERO_RUNTIME_ISATTY(ZERO_RUNTIME_STDIN_FD)) return 0;
  struct termios mode;
  if (tcgetattr(ZERO_RUNTIME_STDIN_FD, &mode) != 0) return 0;
  if (zero_term_raw_active) return 1;
  zero_term_original_mode = mode;
  mode.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  mode.c_oflag &= ~(OPOST);
  mode.c_cflag |= CS8;
#if defined(IEXTEN)
  mode.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
#else
  mode.c_lflag &= ~(ECHO | ICANON | ISIG);
#endif
  mode.c_cc[VMIN] = 0;
  mode.c_cc[VTIME] = 0;
  if (tcsetattr(ZERO_RUNTIME_STDIN_FD, TCSAFLUSH, &mode) != 0) return 0;
  zero_term_raw_active = 1;
  if (!zero_term_restore_registered) {
    atexit(zero_term_restore_at_exit);
    zero_term_restore_registered = 1;
  }
  return 1;
}

static uint64_t zero_term_leave_raw_mode(void) {
  if (!zero_term_raw_active) return 1;
  if (tcsetattr(ZERO_RUNTIME_STDIN_FD, TCSAFLUSH, &zero_term_original_mode) != 0) return 0;
  zero_term_raw_active = 0;
  return 1;
}
#endif

static size_t zero_term_read_chunk_len(size_t len) {
  return len > (size_t)UINT_MAX ? (size_t)UINT_MAX : len;
}

ZeroMaybeUsize zero_term_read_input(ZeroMutByteView buffer) {
  if (!buffer.ptr || buffer.len == 0) return zero_runtime_none_usize();
  size_t chunk = zero_term_read_chunk_len(buffer.len);
#if defined(_WIN32)
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  if (input == INVALID_HANDLE_VALUE) return zero_runtime_none_usize();
  if (ZERO_RUNTIME_ISATTY(ZERO_RUNTIME_STDIN_FD)) {
    if (!_kbhit()) return zero_runtime_none_usize();
    int first = _getch();
    if (first == 0 || first == 0xe0) {
      if (!_kbhit()) return zero_runtime_none_usize();
      int second = _getch();
      const unsigned char *sequence = NULL;
      size_t len = 0;
      static const unsigned char up[] = {0x1b, '[', 'A'};
      static const unsigned char down[] = {0x1b, '[', 'B'};
      static const unsigned char right[] = {0x1b, '[', 'C'};
      static const unsigned char left[] = {0x1b, '[', 'D'};
      static const unsigned char del[] = {0x1b, '[', '3', '~'};
      if (second == 72) { sequence = up; len = sizeof(up); }
      else if (second == 80) { sequence = down; len = sizeof(down); }
      else if (second == 77) { sequence = right; len = sizeof(right); }
      else if (second == 75) { sequence = left; len = sizeof(left); }
      else if (second == 83) { sequence = del; len = sizeof(del); }
      if (!sequence || len > buffer.len) return zero_runtime_none_usize();
      memcpy(buffer.ptr, sequence, len);
      return (ZeroMaybeUsize){1, (uint64_t)len};
    }
    buffer.ptr[0] = (unsigned char)first;
    return (ZeroMaybeUsize){1, 1};
  }
  DWORD type = GetFileType(input);
  if (type == FILE_TYPE_PIPE) {
    DWORD available = 0;
    if (!PeekNamedPipe(input, NULL, 0, NULL, &available, NULL) || available == 0) return zero_runtime_none_usize();
    if ((uint64_t)chunk > (uint64_t)available) chunk = (size_t)available;
  } else if (type != FILE_TYPE_DISK) {
    return zero_runtime_none_usize();
  }
  int got = ZERO_RUNTIME_READ(ZERO_RUNTIME_STDIN_FD, buffer.ptr, (unsigned)chunk);
  if (got <= 0) return zero_runtime_none_usize();
  return (ZeroMaybeUsize){1, (uint64_t)got};
#else
  for (;;) {
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(ZERO_RUNTIME_STDIN_FD, &read_set);
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    int ready = select(ZERO_RUNTIME_STDIN_FD + 1, &read_set, NULL, NULL, &timeout);
    if (ready < 0) {
      if (errno == EINTR) continue;
      return zero_runtime_none_usize();
    }
    if (ready == 0) return zero_runtime_none_usize();
    ZeroReadResult got = ZERO_RUNTIME_READ(ZERO_RUNTIME_STDIN_FD, buffer.ptr, (unsigned)chunk);
    if (got < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return zero_runtime_none_usize();
      return zero_runtime_none_usize();
    }
    if (got == 0) return zero_runtime_none_usize();
    return (ZeroMaybeUsize){1, (uint64_t)got};
  }
#endif
}

uint64_t zero_term_op(uint64_t fallback, uint32_t op) {
  switch ((ZeroTermOp)op) {
    case ZERO_TERM_OP_STDIN_IS_TTY:
      return ZERO_RUNTIME_ISATTY(ZERO_RUNTIME_STDIN_FD) ? 1u : 0u;
    case ZERO_TERM_OP_STDOUT_IS_TTY:
      return ZERO_RUNTIME_ISATTY(ZERO_RUNTIME_STDOUT_FD) ? 1u : 0u;
    case ZERO_TERM_OP_WIDTH_OR:
      return zero_term_size_or(fallback, 0);
    case ZERO_TERM_OP_HEIGHT_OR:
      return zero_term_size_or(fallback, 1);
    case ZERO_TERM_OP_ENTER_RAW_MODE:
      return zero_term_enter_raw_mode();
    case ZERO_TERM_OP_LEAVE_RAW_MODE:
      return zero_term_leave_raw_mode();
    default:
      return fallback;
  }
}

static int zero_parse_ascii_digit(unsigned char byte) {
  return byte >= '0' && byte <= '9';
}

static int zero_parse_ascii_alpha(unsigned char byte) {
  return (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
}

static int zero_parse_identifier_start(unsigned char byte) {
  return zero_parse_ascii_alpha(byte) || byte == '_';
}

static int zero_parse_identifier_byte(unsigned char byte) {
  return zero_parse_identifier_start(byte) || zero_parse_ascii_digit(byte);
}

static uint64_t zero_parse_u32_max(ZeroByteView text, uint32_t max) {
  if (!zero_str_view_valid(text) || text.len == 0) return 0;
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (!zero_parse_ascii_digit(byte)) return 0;
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > ((uint64_t)max - digit) / 10u) return 0;
    total = total * 10u + digit;
  }
  return (1ull << 32) | total;
}

uint64_t zero_parse_op(ZeroByteView text, uint32_t arg, uint32_t op) {
  if (!zero_str_view_valid(text)) return 0;
  unsigned char first = text.len > 0 ? text.ptr[0] : 0;
  switch (op) {
    case ZERO_PARSE_OP_IS_ASCII_DIGIT:
      return text.len > 0 && zero_parse_ascii_digit(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_ASCII_ALPHA:
      return text.len > 0 && zero_parse_ascii_alpha(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_IDENTIFIER_START:
      return text.len > 0 && zero_parse_identifier_start(first) ? 1 : 0;
    case ZERO_PARSE_OP_IS_WHITESPACE:
      return text.len > 0 && zero_str_ascii_space(first) ? 1 : 0;
    case ZERO_PARSE_OP_SCAN_DIGITS: {
      size_t index = 0;
      while (index < text.len && zero_parse_ascii_digit(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_SCAN_IDENTIFIER: {
      if (text.len == 0 || !zero_parse_identifier_start(first)) return 0;
      size_t index = 1;
      while (index < text.len && zero_parse_identifier_byte(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_SCAN_UNTIL_BYTE: {
      unsigned char target = (unsigned char)(arg & 0xffu);
      for (size_t i = 0; i < text.len; i++) {
        if (text.ptr[i] == target) return i;
      }
      return text.len;
    }
    case ZERO_PARSE_OP_SCAN_WHITESPACE: {
      size_t index = 0;
      while (index < text.len && zero_str_ascii_space(text.ptr[index])) index++;
      return index;
    }
    case ZERO_PARSE_OP_PARSE_BOOL:
      if (text.len == 4 && memcmp(text.ptr, "true", 4) == 0) return (1ull << 32) | 1u;
      if (text.len == 5 && memcmp(text.ptr, "false", 5) == 0) return (1ull << 32);
      return 0;
    case ZERO_PARSE_OP_PARSE_U8:
      return zero_parse_u32_max(text, UINT8_MAX);
    case ZERO_PARSE_OP_PARSE_U16:
      return zero_parse_u32_max(text, UINT16_MAX);
    case ZERO_PARSE_OP_TERM_KEY_CODE:
      return zero_term_key_decode(text, 0);
    case ZERO_PARSE_OP_TERM_KEY_BYTE_LEN:
      return zero_term_key_decode(text, 1);
    default:
      return 0;
  }
}

ZeroMaybeUsize zero_parse_usize(ZeroByteView text) {
  if (!zero_str_view_valid(text) || text.len == 0) return (ZeroMaybeUsize){0, 0};
  uint64_t total = 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    if (!zero_parse_ascii_digit(byte)) return (ZeroMaybeUsize){0, 0};
    uint64_t digit = (uint64_t)(byte - '0');
    if (total > (UINT64_MAX - digit) / 10u) return (ZeroMaybeUsize){0, 0};
    total = total * 10u + digit;
  }
  return (ZeroMaybeUsize){1, total};
}

static int64_t zero_time_floor_div(int64_t value, int64_t divisor) {
  int64_t quotient = value / divisor;
  int64_t remainder = value % divisor;
  if (value < 0 && remainder != 0) return quotient - 1;
  return quotient;
}

static int64_t zero_time_sleep_ns(int64_t ns) {
  if (ns <= 0) return 1;
#if defined(_WIN32)
  uint64_t remaining_ms = ((uint64_t)ns + 999999u) / 1000000u;
  while (remaining_ms > 0) {
    DWORD chunk = remaining_ms > 0xffffffffu ? 0xffffffffu : (DWORD)remaining_ms;
    Sleep(chunk);
    remaining_ms -= chunk;
  }
  return 1;
#else
  struct timespec req;
  req.tv_sec = (time_t)(ns / 1000000000);
  req.tv_nsec = (long)(ns % 1000000000);
  while (nanosleep(&req, &req) != 0) {
    if (errno == EINTR) continue;
    return 0;
  }
  return 1;
#endif
}

static int64_t zero_time_wall_seconds(void) {
#if defined(_WIN32)
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t ticks = ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
  if (ticks < 116444736000000000ull) return 0;
  return (int64_t)((ticks - 116444736000000000ull) / 10000000ull);
#else
  return (int64_t)time(NULL);
#endif
}

static int64_t zero_time_monotonic_ns(void) {
#if defined(_WIN32)
  LARGE_INTEGER frequency;
  LARGE_INTEGER counter;
  if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&counter) || frequency.QuadPart <= 0) return 0;
  return (int64_t)(((uint64_t)counter.QuadPart * 1000000000ull) / (uint64_t)frequency.QuadPart);
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
  return (int64_t)ts.tv_sec * 1000000000ll + (int64_t)ts.tv_nsec;
#endif
}

int64_t zero_time_op(int64_t a, int64_t b, int64_t c, uint32_t op) {
  switch (op) {
    case ZERO_TIME_OP_AS_US_FLOOR:
      return zero_time_floor_div(a, 1000);
    case ZERO_TIME_OP_AS_MS_FLOOR:
      return zero_time_floor_div(a, 1000000);
    case ZERO_TIME_OP_AS_SECONDS_FLOOR:
      return zero_time_floor_div(a, 1000000000);
    case ZERO_TIME_OP_MIN:
      return a < b ? a : b;
    case ZERO_TIME_OP_MAX:
      return a < b ? b : a;
    case ZERO_TIME_OP_CLAMP: {
      int64_t lower = b < c ? b : c;
      int64_t upper = b < c ? c : b;
      if (a < lower) return lower;
      if (upper < a) return upper;
      return a;
    }
    case ZERO_TIME_OP_SLEEP:
      return zero_time_sleep_ns(a);
    case ZERO_TIME_OP_WALL_SECONDS:
      (void)a;
      (void)b;
      (void)c;
      return zero_time_wall_seconds();
    case ZERO_TIME_OP_MONOTONIC:
      (void)a;
      (void)b;
      (void)c;
      return zero_time_monotonic_ns();
    default:
      return 0;
  }
}

static uint64_t zero_math_clamp_u64(uint64_t value, uint64_t low, uint64_t high) {
  uint64_t lower = low < high ? low : high;
  uint64_t upper = low < high ? high : low;
  if (value < lower) return lower;
  if (upper < value) return upper;
  return value;
}

static int64_t zero_math_clamp_i64(int64_t value, int64_t low, int64_t high) {
  int64_t lower = low < high ? low : high;
  int64_t upper = low < high ? high : low;
  if (value < lower) return lower;
  if (upper < value) return upper;
  return value;
}

static uint64_t zero_math_some_u32(uint32_t value) {
  return (1ull << 32) | (uint64_t)value;
}

static ZeroMaybeUsize zero_math_some_usize(uint64_t value) {
  return (ZeroMaybeUsize){1, value};
}

static uint64_t zero_math_checked_add_u32(uint32_t left, uint32_t right) {
  if (left > UINT32_MAX - right) return 0;
  return zero_math_some_u32(left + right);
}

static uint64_t zero_math_checked_sub_u32(uint32_t left, uint32_t right) {
  if (left < right) return 0;
  return zero_math_some_u32(left - right);
}

static uint64_t zero_math_checked_mul_u32(uint32_t left, uint32_t right) {
  if (left != 0 && right > UINT32_MAX / left) return 0;
  return zero_math_some_u32(left * right);
}

static uint64_t zero_math_checked_add_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left + (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint64_t zero_math_checked_sub_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left - (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint64_t zero_math_checked_mul_i32(int32_t left, int32_t right) {
  int64_t wide = (int64_t)left * (int64_t)right;
  if (wide > INT32_MAX || wide < INT32_MIN) return 0;
  return zero_math_some_u32((uint32_t)(int32_t)wide);
}

static uint32_t zero_math_gcd_u32(uint32_t a, uint32_t b) {
  while (b != 0) {
    uint32_t next = a % b;
    a = b;
    b = next;
  }
  return a;
}

static uint64_t zero_math_checked_lcm_u32(uint32_t left, uint32_t right) {
  if (left == 0 || right == 0) return zero_math_some_u32(0);
  uint32_t gcd = zero_math_gcd_u32(left, right);
  return zero_math_checked_mul_u32(left / gcd, right);
}

static uint64_t zero_math_checked_pow_u32(uint32_t base, uint32_t exponent) {
  uint32_t result = 1;
  uint32_t current = base;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) {
      uint64_t next_result = zero_math_checked_mul_u32(result, current);
      if ((next_result >> 32) == 0) return 0;
      result = (uint32_t)next_result;
    }
    exp = exp / 2;
    if (exp > 0) {
      uint64_t next_current = zero_math_checked_mul_u32(current, current);
      if ((next_current >> 32) == 0) return 0;
      current = (uint32_t)next_current;
    }
  }
  return zero_math_some_u32(result);
}

static uint32_t zero_math_pow_u32(uint32_t base, uint32_t exponent) {
  uint32_t result = 1;
  uint32_t current = base;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) result = result * current;
    current = current * current;
    exp = exp / 2;
  }
  return result;
}

static uint32_t zero_math_mod_pow_u32(uint32_t base, uint32_t exponent, uint32_t modulus) {
  if (modulus == 0) return 0;
  uint64_t wide_modulus = modulus;
  uint64_t result = 1ull % wide_modulus;
  uint64_t current = ((uint64_t)base) % wide_modulus;
  uint32_t exp = exponent;
  while (exp > 0) {
    if (exp % 2 == 1) result = result * current % wide_modulus;
    current = current * current % wide_modulus;
    exp = exp / 2;
  }
  return (uint32_t)result;
}

static uint32_t zero_math_is_prime_u32(uint32_t value) {
  if (value < 2) return 0;
  if (value == 2) return 1;
  if (value % 2 == 0) return 0;
  uint32_t divisor = 3;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) return 0;
    divisor += 2;
  }
  return 1;
}

static uint32_t zero_math_sqrt_floor_u32(uint32_t value) {
  uint32_t low = 0;
  uint32_t high = 65536;
  while (low + 1 < high) {
    uint32_t mid = low + (high - low) / 2;
    if (mid <= value / mid) low = mid;
    else high = mid;
  }
  return low;
}

static uint64_t zero_math_factorial_u32(uint32_t value) {
  uint32_t result = 1;
  uint32_t next = 2;
  while (next <= value) {
    uint64_t multiplied = zero_math_checked_mul_u32(result, next);
    if ((multiplied >> 32) == 0) return 0;
    result = (uint32_t)multiplied;
    next++;
  }
  return zero_math_some_u32(result);
}

static uint64_t zero_math_binomial_u32(uint32_t n, uint32_t k0) {
  if (k0 > n) return 0;
  uint32_t k = k0;
  if (k > n - k) k = n - k;
  uint32_t result = 1;
  uint32_t i = 1;
  while (i <= k) {
    uint32_t numerator = n - k + i;
    uint32_t denominator = i;
    uint32_t numerator_gcd = zero_math_gcd_u32(numerator, denominator);
    numerator = numerator / numerator_gcd;
    denominator = denominator / numerator_gcd;
    uint32_t result_gcd = zero_math_gcd_u32(result, denominator);
    result = result / result_gcd;
    denominator = denominator / result_gcd;
    if (denominator != 1) return 0;
    uint64_t multiplied = zero_math_checked_mul_u32(result, numerator);
    if ((multiplied >> 32) == 0) return 0;
    result = (uint32_t)multiplied;
    i++;
  }
  return zero_math_some_u32(result);
}

static uint32_t zero_math_divisor_count_u32(uint32_t value) {
  if (value == 0) return 0;
  uint32_t count = 0;
  uint32_t divisor = 1;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) {
      uint32_t paired = value / divisor;
      count += paired == divisor ? 1u : 2u;
    }
    divisor++;
  }
  return count;
}

static uint32_t zero_math_proper_divisor_sum_u32(uint32_t value) {
  if (value <= 1) return 0;
  uint32_t sum = 1;
  uint32_t divisor = 2;
  while (divisor <= value / divisor) {
    if (value % divisor == 0) {
      uint32_t paired = value / divisor;
      sum += divisor;
      if (paired != divisor) sum += paired;
    }
    divisor++;
  }
  return sum;
}

static ZeroMaybeUsize zero_math_checked_add_usize(uint64_t left, uint64_t right) {
  if (left > UINT64_MAX - right) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left + right);
}

static ZeroMaybeUsize zero_math_checked_sub_usize(uint64_t left, uint64_t right) {
  if (left < right) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left - right);
}

static ZeroMaybeUsize zero_math_checked_mul_usize(uint64_t left, uint64_t right) {
  if (left != 0 && right > UINT64_MAX / left) return (ZeroMaybeUsize){0, 0};
  return zero_math_some_usize(left * right);
}

ZeroMaybeUsize zero_math_usize_op(uint64_t a, uint64_t b, uint64_t c, uint32_t op) {
  (void)c;
  switch (op) {
    case ZERO_MATH_OP_CHECKED_ADD_USIZE:
      return zero_math_checked_add_usize(a, b);
    case ZERO_MATH_OP_CHECKED_SUB_USIZE:
      return zero_math_checked_sub_usize(a, b);
    case ZERO_MATH_OP_CHECKED_MUL_USIZE:
      return zero_math_checked_mul_usize(a, b);
    default:
      return (ZeroMaybeUsize){0, 0};
  }
}

uint64_t zero_math_op(int64_t a, int64_t b, int64_t c, uint32_t op) {
  switch (op) {
    case ZERO_MATH_OP_MIN_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      return (uint64_t)(int64_t)(left < right ? left : right);
    }
    case ZERO_MATH_OP_MAX_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      return (uint64_t)(int64_t)(left > right ? left : right);
    }
    case ZERO_MATH_OP_CLAMP_I32: {
      int32_t value = (int32_t)a;
      int32_t low = (int32_t)b;
      int32_t high = (int32_t)c;
      int32_t lower = low < high ? low : high;
      int32_t upper = low < high ? high : low;
      if (value < lower) return (uint64_t)(int64_t)lower;
      if (upper < value) return (uint64_t)(int64_t)upper;
      return (uint64_t)(int64_t)value;
    }
    case ZERO_MATH_OP_MIN_I64:
      return (uint64_t)(a < b ? a : b);
    case ZERO_MATH_OP_MAX_I64:
      return (uint64_t)(a > b ? a : b);
    case ZERO_MATH_OP_CLAMP_I64:
      return (uint64_t)zero_math_clamp_i64(a, b, c);
    case ZERO_MATH_OP_MIN_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      return left < right ? left : right;
    }
    case ZERO_MATH_OP_MAX_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      return left > right ? left : right;
    }
    case ZERO_MATH_OP_CLAMP_U32:
      return (uint32_t)zero_math_clamp_u64((uint32_t)a, (uint32_t)b, (uint32_t)c);
    case ZERO_MATH_OP_MIN_U64:
    case ZERO_MATH_OP_MIN_USIZE: {
      uint64_t left = (uint64_t)a;
      uint64_t right = (uint64_t)b;
      return left < right ? left : right;
    }
    case ZERO_MATH_OP_MAX_U64:
    case ZERO_MATH_OP_MAX_USIZE: {
      uint64_t left = (uint64_t)a;
      uint64_t right = (uint64_t)b;
      return left > right ? left : right;
    }
    case ZERO_MATH_OP_CLAMP_U64:
    case ZERO_MATH_OP_CLAMP_USIZE:
      return zero_math_clamp_u64((uint64_t)a, (uint64_t)b, (uint64_t)c);
    case ZERO_MATH_OP_ABS_I32: {
      int32_t value = (int32_t)a;
      if (value == INT32_MIN) return 2147483648u;
      return value < 0 ? (uint32_t)(-value) : (uint32_t)value;
    }
    case ZERO_MATH_OP_ABS_I64:
      if (a == INT64_MIN) return 9223372036854775808ull;
      return a < 0 ? (uint64_t)(-a) : (uint64_t)a;
    case ZERO_MATH_OP_CHECKED_ADD_U32:
      return zero_math_checked_add_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_SUB_U32:
      return zero_math_checked_sub_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_MUL_U32:
      return zero_math_checked_mul_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_SATURATING_ADD_U32: {
      uint64_t result = zero_math_checked_add_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : UINT32_MAX;
    }
    case ZERO_MATH_OP_SATURATING_SUB_U32: {
      uint64_t result = zero_math_checked_sub_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : 0;
    }
    case ZERO_MATH_OP_SATURATING_MUL_U32: {
      uint64_t result = zero_math_checked_mul_u32((uint32_t)a, (uint32_t)b);
      return (result >> 32) ? (uint32_t)result : UINT32_MAX;
    }
    case ZERO_MATH_OP_CHECKED_ADD_I32:
      return zero_math_checked_add_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_CHECKED_SUB_I32:
      return zero_math_checked_sub_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_CHECKED_MUL_I32:
      return zero_math_checked_mul_i32((int32_t)a, (int32_t)b);
    case ZERO_MATH_OP_SATURATING_ADD_I32: {
      int64_t wide = (int64_t)(int32_t)a + (int64_t)(int32_t)b;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_SATURATING_SUB_I32: {
      int64_t wide = (int64_t)(int32_t)a - (int64_t)(int32_t)b;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_SATURATING_MUL_I32: {
      int32_t left = (int32_t)a;
      int32_t right = (int32_t)b;
      int64_t wide = (int64_t)left * (int64_t)right;
      if (wide > INT32_MAX) return (uint32_t)INT32_MAX;
      if (wide < INT32_MIN) return (uint64_t)(int64_t)INT32_MIN;
      return (uint64_t)(int64_t)(int32_t)wide;
    }
    case ZERO_MATH_OP_GCD_U32:
      return zero_math_gcd_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_LCM_U32: {
      uint32_t left = (uint32_t)a;
      uint32_t right = (uint32_t)b;
      if (left == 0 || right == 0) return 0;
      return (uint32_t)((left / zero_math_gcd_u32(left, right)) * right);
    }
    case ZERO_MATH_OP_CHECKED_LCM_U32:
      return zero_math_checked_lcm_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_POW_U32:
      return zero_math_pow_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_CHECKED_POW_U32:
      return zero_math_checked_pow_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_MOD_POW_U32:
      return zero_math_mod_pow_u32((uint32_t)a, (uint32_t)b, (uint32_t)c);
    case ZERO_MATH_OP_IS_PRIME_U32:
      return zero_math_is_prime_u32((uint32_t)a);
    case ZERO_MATH_OP_SQRT_FLOOR_U32:
      return zero_math_sqrt_floor_u32((uint32_t)a);
    case ZERO_MATH_OP_FACTORIAL_U32:
      return zero_math_factorial_u32((uint32_t)a);
    case ZERO_MATH_OP_BINOMIAL_U32:
      return zero_math_binomial_u32((uint32_t)a, (uint32_t)b);
    case ZERO_MATH_OP_DIVISOR_COUNT_U32:
      return zero_math_divisor_count_u32((uint32_t)a);
    case ZERO_MATH_OP_PROPER_DIVISOR_SUM_U32:
      return zero_math_proper_divisor_sum_u32((uint32_t)a);
    case ZERO_MATH_OP_SATURATING_ADD_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_add_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : UINT64_MAX;
    }
    case ZERO_MATH_OP_SATURATING_SUB_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_sub_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : 0;
    }
    case ZERO_MATH_OP_SATURATING_MUL_USIZE: {
      ZeroMaybeUsize result = zero_math_checked_mul_usize((uint64_t)a, (uint64_t)b);
      return result.has ? result.value : UINT64_MAX;
    }
    default:
      return 0;
  }
}

static size_t zero_search_lower_bound_i32(const int32_t *items, size_t len, int32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_upper_bound_i32(const int32_t *items, size_t len, int32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] <= needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_lower_bound_u32(const uint32_t *items, size_t len, uint32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_upper_bound_u32(const uint32_t *items, size_t len, uint32_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] <= needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_lower_bound_usize(const uint64_t *items, size_t len, uint64_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] < needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

static size_t zero_search_upper_bound_usize(const uint64_t *items, size_t len, uint64_t needle) {
  size_t low = 0;
  size_t high = len;
  while (low < high) {
    size_t mid = low + (high - low) / 2u;
    if (items[mid] <= needle) low = mid + 1u;
    else high = mid;
  }
  return low;
}

uint64_t zero_search_op(ZeroByteView items, int64_t needle, uint32_t op) {
  if (!zero_str_view_valid(items)) return 0;
  switch (op) {
    case ZERO_SEARCH_OP_LOWER_BOUND_I32:
      return zero_search_lower_bound_i32((const int32_t *)items.ptr, items.len, (int32_t)needle);
    case ZERO_SEARCH_OP_BINARY_I32: {
      size_t index = zero_search_lower_bound_i32((const int32_t *)items.ptr, items.len, (int32_t)needle);
      return index < items.len && ((const int32_t *)items.ptr)[index] == (int32_t)needle ? index : items.len;
    }
    case ZERO_SEARCH_OP_LOWER_BOUND_U32:
      return zero_search_lower_bound_u32((const uint32_t *)items.ptr, items.len, (uint32_t)needle);
    case ZERO_SEARCH_OP_BINARY_U32: {
      size_t index = zero_search_lower_bound_u32((const uint32_t *)items.ptr, items.len, (uint32_t)needle);
      return index < items.len && ((const uint32_t *)items.ptr)[index] == (uint32_t)needle ? index : items.len;
    }
    case ZERO_SEARCH_OP_LOWER_BOUND_USIZE:
      return zero_search_lower_bound_usize((const uint64_t *)items.ptr, items.len, (uint64_t)needle);
    case ZERO_SEARCH_OP_BINARY_USIZE: {
      size_t index = zero_search_lower_bound_usize((const uint64_t *)items.ptr, items.len, (uint64_t)needle);
      return index < items.len && ((const uint64_t *)items.ptr)[index] == (uint64_t)needle ? index : items.len;
    }
    case ZERO_SEARCH_OP_UPPER_BOUND_I32:
      return zero_search_upper_bound_i32((const int32_t *)items.ptr, items.len, (int32_t)needle);
    case ZERO_SEARCH_OP_UPPER_BOUND_U32:
      return zero_search_upper_bound_u32((const uint32_t *)items.ptr, items.len, (uint32_t)needle);
    case ZERO_SEARCH_OP_UPPER_BOUND_USIZE:
      return zero_search_upper_bound_usize((const uint64_t *)items.ptr, items.len, (uint64_t)needle);
    default:
      return 0;
  }
}

static void zero_sort_insertion_i32(int32_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    int32_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

static void zero_sort_insertion_u32(uint32_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    uint32_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

static void zero_sort_insertion_usize(uint64_t *items, size_t len) {
  for (size_t index = 1; index < len; index++) {
    uint64_t value = items[index];
    size_t cursor = index;
    while (cursor > 0 && items[cursor - 1u] > value) {
      items[cursor] = items[cursor - 1u];
      cursor--;
    }
    items[cursor] = value;
  }
}

void zero_sort_op(ZeroMutByteView items, uint32_t op) {
  if ((!items.ptr && items.len > 0)) return;
  switch (op) {
    case ZERO_SORT_OP_INSERTION_I32:
      zero_sort_insertion_i32((int32_t *)items.ptr, items.len);
      break;
    case ZERO_SORT_OP_INSERTION_U32:
      zero_sort_insertion_u32((uint32_t *)items.ptr, items.len);
      break;
    case ZERO_SORT_OP_INSERTION_USIZE:
      zero_sort_insertion_usize((uint64_t *)items.ptr, items.len);
      break;
    default:
      break;
  }
}

uint32_t zero_sort_is_sorted_op(ZeroByteView items, uint32_t op) {
  if (!zero_str_view_valid(items)) return 0;
  switch (op) {
    case ZERO_SORT_OP_IS_SORTED_I32: {
      const int32_t *values = (const int32_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    case ZERO_SORT_OP_IS_SORTED_U32: {
      const uint32_t *values = (const uint32_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    case ZERO_SORT_OP_IS_SORTED_USIZE: {
      const uint64_t *values = (const uint64_t *)items.ptr;
      for (size_t i = 1; i < items.len; i++) if (values[i] < values[i - 1u]) return 0;
      return 1;
    }
    default:
      return 0;
  }
}

static int zero_str_eq_at(ZeroByteView text, size_t start, ZeroByteView needle) {
  if (start > text.len || needle.len > text.len - start) return 0;
  for (size_t i = 0; i < needle.len; i++) {
    if (text.ptr[start + i] != needle.ptr[i]) return 0;
  }
  return 1;
}

uint32_t zero_str_buffer_op(ZeroMutByteView buffer, ZeroByteView text, uint32_t op) {
  if (!buffer.ptr || !zero_str_view_valid(text) || text.len > buffer.len) return 0;
  uint32_t some_len = zero_str_some_len(text.len);
  if (!some_len) return 0;
  for (size_t i = 0; i < text.len; i++) {
    unsigned char byte = text.ptr[i];
    switch (op) {
      case ZERO_STR_OP_REVERSE:
        byte = text.ptr[text.len - 1u - i];
        break;
      case ZERO_STR_OP_COPY:
        break;
      case ZERO_STR_OP_TO_LOWER_ASCII:
        byte = zero_str_lower(byte);
        break;
      case ZERO_STR_OP_TO_UPPER_ASCII:
        byte = zero_str_upper(byte);
        break;
      default:
        return 0;
    }
    buffer.ptr[i] = byte;
  }
  return some_len;
}

uint32_t zero_str_concat(ZeroMutByteView buffer, ZeroByteView left, ZeroByteView right) {
  if (!buffer.ptr || !zero_str_view_valid(left) || !zero_str_view_valid(right)) return 0;
  if (left.len > buffer.len || right.len > buffer.len - left.len) return 0;
  uint32_t some_len = zero_str_some_len(left.len + right.len);
  if (!some_len) return 0;
  if (left.len > 0) memcpy(buffer.ptr, left.ptr, left.len);
  if (right.len > 0) memcpy(buffer.ptr + left.len, right.ptr, right.len);
  return some_len;
}

uint32_t zero_str_repeat(ZeroMutByteView buffer, ZeroByteView text, uint64_t count) {
  if (!buffer.ptr || !zero_str_view_valid(text)) return 0;
  if (text.len > 0 && count > (uint64_t)(buffer.len / text.len)) return 0;
  uint64_t total64 = (uint64_t)text.len * count;
  if (total64 > UINT32_MAX - 1u) return 0;
  size_t total = (size_t)total64;
  uint32_t some_len = zero_str_some_len(total);
  if (!some_len) return 0;
  size_t out = 0;
  for (uint64_t r = 0; r < count; r++) {
    if (text.len > 0) memcpy(buffer.ptr + out, text.ptr, text.len);
    out += text.len;
  }
  return some_len;
}

uint64_t zero_str_trim_op(ZeroByteView text, uint32_t op) {
  if (!zero_str_view_valid(text) || text.len > UINT32_MAX) return 0;
  size_t start = 0;
  size_t end = text.len;
  if (op == ZERO_STR_OP_PATH_BASENAME ||
      op == ZERO_STR_OP_PATH_DIRNAME ||
      op == ZERO_STR_OP_PATH_EXTENSION) {
    while (end > 0 && (text.ptr[end - 1u] == '/' || text.ptr[end - 1u] == '\\')) end--;
    if (op == ZERO_STR_OP_PATH_BASENAME) {
      start = end;
      while (start > 0 && text.ptr[start - 1u] != '/' && text.ptr[start - 1u] != '\\') start--;
      return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(end - start);
    }
    if (op == ZERO_STR_OP_PATH_DIRNAME) {
      size_t root_len = text.len > 0 && (text.ptr[0] == '/' || text.ptr[0] == '\\') ? 1u : 0u;
      if (root_len > 0 && end == 0) return (uint32_t)root_len;
      size_t split = end;
      while (split > 0 && text.ptr[split - 1u] != '/' && text.ptr[split - 1u] != '\\') split--;
      while (split > root_len && (text.ptr[split - 1u] == '/' || text.ptr[split - 1u] == '\\')) split--;
      return (uint32_t)split;
    }
    start = end;
    while (start > 0 && text.ptr[start - 1u] != '/' && text.ptr[start - 1u] != '\\') start--;
    size_t dot = end;
    size_t scan = end;
    while (scan > start) {
      scan--;
      if (text.ptr[scan] == '.') {
        dot = scan;
        scan = start;
      }
    }
    if (dot == end || dot == start) return ((uint64_t)(uint32_t)end << 32);
    return ((uint64_t)(uint32_t)(dot + 1u) << 32) | (uint32_t)(end - dot - 1u);
  }
  if (op == ZERO_STR_OP_PARSE_TOKEN_ASCII) {
    while (start < end && zero_str_ascii_space(text.ptr[start])) start++;
    size_t token_end = start;
    while (token_end < end && !zero_str_ascii_space(text.ptr[token_end])) token_end++;
    return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(token_end - start);
  }
  if (op == ZERO_STR_OP_TRIM_ASCII || op == ZERO_STR_OP_TRIM_START_ASCII) {
    while (start < end && zero_str_ascii_space(text.ptr[start])) start++;
  }
  if (op == ZERO_STR_OP_TRIM_ASCII || op == ZERO_STR_OP_TRIM_END_ASCII) {
    while (end > start && zero_str_ascii_space(text.ptr[end - 1u])) end--;
  }
  return ((uint64_t)(uint32_t)start << 32) | (uint32_t)(end - start);
}

uint64_t zero_str_pair_op(ZeroByteView text, ZeroByteView needle, uint32_t op) {
  if (!zero_str_view_valid(text) || !zero_str_view_valid(needle)) return 0;
  switch (op) {
    case ZERO_STR_OP_STARTS_WITH:
      return needle.len <= text.len && zero_str_eq_at(text, 0, needle) ? 1 : 0;
    case ZERO_STR_OP_ENDS_WITH:
      return needle.len <= text.len && zero_str_eq_at(text, text.len - needle.len, needle) ? 1 : 0;
    case ZERO_STR_OP_CONTAINS:
      return zero_str_pair_op(text, needle, ZERO_STR_OP_INDEX_OF) != text.len || needle.len == 0 ? 1 : 0;
    case ZERO_STR_OP_INDEX_OF:
      if (needle.len == 0) return 0;
      if (needle.len > text.len) return text.len;
      for (size_t start = 0; start <= text.len - needle.len; start++) {
        if (zero_str_eq_at(text, start, needle)) return start;
      }
      return text.len;
    case ZERO_STR_OP_LAST_INDEX_OF:
      if (needle.len == 0 || needle.len > text.len) return text.len;
      for (size_t start = text.len - needle.len + 1u; start > 0; start--) {
        size_t candidate = start - 1u;
        if (zero_str_eq_at(text, candidate, needle)) return candidate;
      }
      return text.len;
    case ZERO_STR_OP_COUNT: {
      if (needle.len == 0) return text.len + 1u;
      size_t index = 0;
      uint64_t count = 0;
      while (index <= text.len) {
        ZeroByteView rest = { text.ptr ? text.ptr + index : NULL, text.len - index };
        uint64_t found = zero_str_pair_op(rest, needle, ZERO_STR_OP_INDEX_OF);
        if (found == rest.len) return count;
        count++;
        index += (size_t)found + needle.len;
      }
      return count;
    }
    case ZERO_STR_OP_EQL_IGNORE_ASCII_CASE:
      if (text.len != needle.len) return 0;
      for (size_t i = 0; i < text.len; i++) {
        if (zero_str_lower(text.ptr[i]) != zero_str_lower(needle.ptr[i])) return 0;
      }
      return 1;
    default:
      return 0;
  }
}

uint64_t zero_str_count_byte(ZeroByteView text, uint32_t byte) {
  if (!zero_str_view_valid(text)) return 0;
  uint64_t count = 0;
  unsigned char target = (unsigned char)(byte & 0xffu);
  for (size_t i = 0; i < text.len; i++) {
    if (text.ptr[i] == target) count++;
  }
  return count;
}

uint64_t zero_str_word_count_ascii(ZeroByteView text) {
  if (!zero_str_view_valid(text)) return 0;
  uint64_t count = 0;
  int in_word = 0;
  for (size_t i = 0; i < text.len; i++) {
    if (zero_str_ascii_space(text.ptr[i])) {
      in_word = 0;
    } else if (!in_word) {
      count++;
      in_word = 1;
    }
  }
  return count;
}

typedef struct {
  const unsigned char *ptr;
  size_t len;
  size_t pos;
  size_t tokens;
  size_t error_pos;
} ZeroJsonScanner;

static int zero_json_fail(ZeroJsonScanner *scanner, size_t pos) {
  if (scanner) scanner->error_pos = pos;
  return 0;
}

static int zero_json_hex_value(unsigned char ch) {
  if (ch >= '0' && ch <= '9') return (int)(ch - '0');
  if (ch >= 'a' && ch <= 'f') return (int)(ch - 'a' + 10);
  if (ch >= 'A' && ch <= 'F') return (int)(ch - 'A' + 10);
  return -1;
}

static int zero_json_hex4_value(const unsigned char *ptr, size_t len, size_t pos, uint16_t *out) {
  if (!ptr || pos + 4 > len) return 0;
  uint16_t value = 0;
  for (size_t i = 0; i < 4; i++) {
    int digit = zero_json_hex_value(ptr[pos + i]);
    if (digit < 0) return 0;
    value = (uint16_t)((value << 4) | (uint16_t)digit);
  }
  if (out) *out = value;
  return 1;
}

static int zero_json_high_surrogate(uint16_t value) {
  return value >= 0xd800u && value <= 0xdbffu;
}

static int zero_json_low_surrogate(uint16_t value) {
  return value >= 0xdc00u && value <= 0xdfffu;
}

static uint32_t zero_json_surrogate_codepoint(uint16_t high, uint16_t low) {
  uint32_t high_part = (uint32_t)high - 0xd800u;
  uint32_t low_part = (uint32_t)low - 0xdc00u;
  return 0x10000u + ((high_part << 10) | low_part);
}

static size_t zero_json_write_utf8_bytes(uint32_t codepoint, unsigned char out[4]) {
  if (codepoint <= 0x7fu) {
    out[0] = (unsigned char)codepoint;
    return 1;
  }
  if (codepoint <= 0x7ffu) {
    out[0] = (unsigned char)(0xc0u | (codepoint >> 6));
    out[1] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    return 2;
  }
  if (codepoint <= 0xffffu) {
    out[0] = (unsigned char)(0xe0u | (codepoint >> 12));
    out[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
    out[2] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    return 3;
  }
  if (codepoint <= 0x10ffffu) {
    out[0] = (unsigned char)(0xf0u | (codepoint >> 18));
    out[1] = (unsigned char)(0x80u | ((codepoint >> 12) & 0x3fu));
    out[2] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
    out[3] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    return 4;
  }
  return 0;
}

static void zero_json_skip_ws(ZeroJsonScanner *scanner) {
  while (scanner->pos < scanner->len) {
    unsigned char ch = scanner->ptr[scanner->pos];
    if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') return;
    scanner->pos++;
  }
}

static size_t zero_json_skip_ws_at(ZeroByteView input, size_t pos) {
  while (pos < input.len) {
    unsigned char ch = input.ptr[pos];
    if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') break;
    pos++;
  }
  return pos;
}

static int zero_json_parse_value(ZeroJsonScanner *scanner, unsigned depth);
static size_t zero_json_utf8_char_len(const unsigned char *ptr, size_t len, size_t pos);

static int zero_json_parse_string(ZeroJsonScanner *scanner) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '"') return zero_json_fail(scanner, scanner->pos);
  scanner->pos++;
  while (scanner->pos < scanner->len) {
    size_t char_pos = scanner->pos;
    unsigned char ch = scanner->ptr[scanner->pos++];
    if (ch == '"') return 1;
    if (ch < 0x20u) return zero_json_fail(scanner, char_pos);
    if (ch >= 0x80u) {
      size_t width = zero_json_utf8_char_len(scanner->ptr, scanner->len, char_pos);
      if (width == 0) return zero_json_fail(scanner, char_pos);
      scanner->pos = char_pos + width;
      continue;
    }
    if (ch != '\\') continue;
    if (scanner->pos >= scanner->len) return zero_json_fail(scanner, scanner->pos);
    size_t escape_pos = scanner->pos;
    unsigned char esc = scanner->ptr[scanner->pos++];
    if (esc == '"' || esc == '\\' || esc == '/' || esc == 'b' || esc == 'f' || esc == 'n' || esc == 'r' || esc == 't') continue;
    if (esc != 'u') return zero_json_fail(scanner, escape_pos);
    uint16_t unit = 0;
    if (!zero_json_hex4_value(scanner->ptr, scanner->len, scanner->pos, &unit)) return zero_json_fail(scanner, scanner->pos);
    scanner->pos += 4;
    if (zero_json_high_surrogate(unit)) {
      size_t pair_pos = scanner->pos;
      if (scanner->pos + 6 > scanner->len || scanner->ptr[scanner->pos] != '\\' || scanner->ptr[scanner->pos + 1] != 'u') return zero_json_fail(scanner, pair_pos);
      scanner->pos += 2;
      uint16_t low = 0;
      if (!zero_json_hex4_value(scanner->ptr, scanner->len, scanner->pos, &low) || !zero_json_low_surrogate(low)) return zero_json_fail(scanner, scanner->pos);
      scanner->pos += 4;
    } else if (zero_json_low_surrogate(unit)) {
      return zero_json_fail(scanner, escape_pos);
    }
  }
  return zero_json_fail(scanner, scanner->len);
}

static int zero_json_match_literal(ZeroJsonScanner *scanner, const char *literal) {
  size_t start = scanner->pos;
  for (size_t i = 0; literal[i]; i++) {
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != (unsigned char)literal[i]) {
      size_t error_pos = scanner->pos;
      scanner->pos = start;
      return zero_json_fail(scanner, error_pos);
    }
    scanner->pos++;
  }
  return 1;
}

static int zero_json_parse_number(ZeroJsonScanner *scanner) {
  size_t start = scanner->pos;
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '-') scanner->pos++;
  if (scanner->pos >= scanner->len) {
    size_t error_pos = scanner->pos;
    scanner->pos = start;
    return zero_json_fail(scanner, error_pos);
  }
  if (scanner->ptr[scanner->pos] == '0') {
    scanner->pos++;
  } else if (scanner->ptr[scanner->pos] >= '1' && scanner->ptr[scanner->pos] <= '9') {
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
  } else {
    size_t error_pos = scanner->pos;
    scanner->pos = start;
    return zero_json_fail(scanner, error_pos);
  }
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '.') {
    scanner->pos++;
    size_t digits = scanner->pos;
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
    if (scanner->pos == digits) {
      size_t error_pos = scanner->pos;
      scanner->pos = start;
      return zero_json_fail(scanner, error_pos);
    }
  }
  if (scanner->pos < scanner->len && (scanner->ptr[scanner->pos] == 'e' || scanner->ptr[scanner->pos] == 'E')) {
    scanner->pos++;
    if (scanner->pos < scanner->len && (scanner->ptr[scanner->pos] == '+' || scanner->ptr[scanner->pos] == '-')) scanner->pos++;
    size_t digits = scanner->pos;
    while (scanner->pos < scanner->len && scanner->ptr[scanner->pos] >= '0' && scanner->ptr[scanner->pos] <= '9') scanner->pos++;
    if (scanner->pos == digits) {
      size_t error_pos = scanner->pos;
      scanner->pos = start;
      return zero_json_fail(scanner, error_pos);
    }
  }
  return 1;
}

static int zero_json_parse_array(ZeroJsonScanner *scanner, unsigned depth) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '[') return zero_json_fail(scanner, scanner->pos);
  scanner->tokens++;
  scanner->pos++;
  zero_json_skip_ws(scanner);
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == ']') {
    scanner->pos++;
    return 1;
  }
  for (;;) {
    if (!zero_json_parse_value(scanner, depth + 1)) return 0;
    zero_json_skip_ws(scanner);
    if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == ']') {
      scanner->pos++;
      return 1;
    }
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ',') return zero_json_fail(scanner, scanner->pos);
    scanner->pos++;
    zero_json_skip_ws(scanner);
  }
}

static int zero_json_parse_object(ZeroJsonScanner *scanner, unsigned depth) {
  if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != '{') return zero_json_fail(scanner, scanner->pos);
  scanner->tokens++;
  scanner->pos++;
  zero_json_skip_ws(scanner);
  if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '}') {
    scanner->pos++;
    return 1;
  }
  for (;;) {
    if (!zero_json_parse_string(scanner)) return 0;
    scanner->tokens++;
    zero_json_skip_ws(scanner);
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ':') return zero_json_fail(scanner, scanner->pos);
    scanner->pos++;
    if (!zero_json_parse_value(scanner, depth + 1)) return 0;
    zero_json_skip_ws(scanner);
    if (scanner->pos < scanner->len && scanner->ptr[scanner->pos] == '}') {
      scanner->pos++;
      return 1;
    }
    if (scanner->pos >= scanner->len || scanner->ptr[scanner->pos] != ',') return zero_json_fail(scanner, scanner->pos);
    scanner->pos++;
    zero_json_skip_ws(scanner);
  }
}

static int zero_json_parse_value(ZeroJsonScanner *scanner, unsigned depth) {
  if (depth > 64) return zero_json_fail(scanner, scanner->pos);
  zero_json_skip_ws(scanner);
  if (scanner->pos >= scanner->len) return zero_json_fail(scanner, scanner->pos);
  unsigned char ch = scanner->ptr[scanner->pos];
  if (ch == '{') return zero_json_parse_object(scanner, depth);
  if (ch == '[') return zero_json_parse_array(scanner, depth);
  if (ch == '"') {
    scanner->tokens++;
    return zero_json_parse_string(scanner);
  }
  if (ch == 't') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "true");
  }
  if (ch == 'f') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "false");
  }
  if (ch == 'n') {
    scanner->tokens++;
    return zero_json_match_literal(scanner, "null");
  }
  if (ch == '-' || (ch >= '0' && ch <= '9')) {
    scanner->tokens++;
    return zero_json_parse_number(scanner);
  }
  return zero_json_fail(scanner, scanner->pos);
}

int64_t zero_json_parse_bytes(ZeroByteView input) {
  if (!input.ptr || input.len == 0) return -1;
  ZeroJsonScanner scanner = {input.ptr, input.len, 0, 0, 0};
  if (!zero_json_parse_value(&scanner, 0)) return -1;
  zero_json_skip_ws(&scanner);
  if (scanner.pos != scanner.len || scanner.tokens > INT64_MAX) return scanner.pos != scanner.len ? -2 : -1;
  return (int64_t)scanner.tokens;
}

static uint64_t zero_json_error_offset(ZeroByteView input, uint32_t *status_out) {
  if (!input.ptr || input.len == 0) {
    if (status_out) *status_out = 1;
    return 0;
  }
  ZeroJsonScanner scanner = {input.ptr, input.len, 0, 0, 0};
  if (!zero_json_parse_value(&scanner, 0)) {
    if (status_out) *status_out = 1;
    return scanner.error_pos;
  }
  zero_json_skip_ws(&scanner);
  if (scanner.pos != scanner.len || scanner.tokens > INT64_MAX) {
    if (status_out) *status_out = scanner.pos != scanner.len ? 2u : 1u;
    return scanner.pos;
  }
  if (status_out) *status_out = 0;
  return input.len;
}

uint64_t zero_json_diagnostic(ZeroByteView input, uint32_t op) {
  uint32_t status = 0;
  uint64_t offset = zero_json_error_offset(input, &status);
  if (op == 0) return status;
  if (op == 1) return offset;
  uint64_t line = 1;
  uint64_t column = 1;
  uint64_t start = 0;
  if (input.ptr) {
    for (uint64_t index = 0; index < offset && index < input.len; index++) {
      if (input.ptr[index] == '\n') {
        line++;
        start = index + 1;
      }
    }
    column = offset >= start ? offset - start + 1 : 1;
  }
  if (op == 2) return line;
  if (op == 3) return column;
  return status;
}

static uint64_t zero_json_pack_span(int has, size_t offset, size_t len) {
  if (!has || offset > UINT32_MAX || len > 0x7fffffffu) return 0;
  return (1ull << 63) | ((uint64_t)len << 32) | (uint64_t)(uint32_t)offset;
}

static int zero_json_key_matches(ZeroByteView input, size_t start, size_t end, ZeroByteView key) {
  if (!input.ptr || start >= end || input.ptr[start] != '"') return 0;
  size_t pos = start + 1;
  size_t key_pos = 0;
  while (pos + 1 < end) {
    unsigned char decoded = input.ptr[pos];
    unsigned char scratch[4] = {0};
    size_t scratch_len = 0;
    if (decoded == '\\') {
      pos++;
      if (pos + 1 >= end) return 0;
      unsigned char escaped = input.ptr[pos];
      if (escaped == '"' || escaped == '\\' || escaped == '/') {
        decoded = escaped;
      } else if (escaped == 'b') {
        decoded = 8;
      } else if (escaped == 'f') {
        decoded = 12;
      } else if (escaped == 'n') {
        decoded = 10;
      } else if (escaped == 'r') {
        decoded = 13;
      } else if (escaped == 't') {
        decoded = 9;
      } else if (escaped == 'u') {
        uint16_t unit = 0;
        if (!zero_json_hex4_value(input.ptr, input.len, pos + 1, &unit)) return 0;
        uint32_t codepoint = unit;
        if (zero_json_high_surrogate(unit)) {
          if (pos + 10 >= end || input.ptr[pos + 5] != '\\' || input.ptr[pos + 6] != 'u') return 0;
          uint16_t low = 0;
          if (!zero_json_hex4_value(input.ptr, input.len, pos + 7, &low) || !zero_json_low_surrogate(low)) return 0;
          codepoint = zero_json_surrogate_codepoint(unit, low);
          pos += 10;
        } else if (zero_json_low_surrogate(unit)) {
          return 0;
        } else {
          pos += 4;
        }
        scratch_len = zero_json_write_utf8_bytes(codepoint, scratch);
        if (scratch_len == 0) return 0;
      } else {
        return 0;
      }
    }
    if (scratch_len > 0) {
      for (size_t i = 0; i < scratch_len; i++) {
        if (key_pos >= key.len || (key.len > 0 && key.ptr[key_pos] != scratch[i])) return 0;
        key_pos++;
      }
    } else {
      if (key_pos >= key.len || (key.len > 0 && key.ptr[key_pos] != decoded)) return 0;
      key_pos++;
    }
    pos++;
  }
  return key_pos == key.len;
}

uint64_t zero_json_field(ZeroByteView input, ZeroByteView key) {
  if (!input.ptr || (key.len > 0 && !key.ptr)) return 0;
  size_t start = zero_json_skip_ws_at(input, 0);
  if (start >= input.len || input.ptr[start] != '{') return 0;
  size_t pos = zero_json_skip_ws_at(input, start + 1);
  if (pos < input.len && input.ptr[pos] == '}') {
    size_t trailing = zero_json_skip_ws_at(input, pos + 1);
    return trailing == input.len ? 0 : 0;
  }
  int found = 0;
  size_t found_start = 0;
  size_t found_end = 0;
  while (pos < input.len) {
    ZeroJsonScanner key_scanner = {input.ptr, input.len, pos, 0, pos};
    if (!zero_json_parse_string(&key_scanner)) return 0;
    size_t key_end = key_scanner.pos;
    int matched = zero_json_key_matches(input, pos, key_end, key);
    pos = zero_json_skip_ws_at(input, key_end);
    if (pos >= input.len || input.ptr[pos] != ':') return 0;
    size_t value_start = zero_json_skip_ws_at(input, pos + 1);
    ZeroJsonScanner value_scanner = {input.ptr, input.len, value_start, 0, value_start};
    if (!zero_json_parse_value(&value_scanner, 0)) return 0;
    size_t value_end = value_scanner.pos;
    if (matched && !found) {
      found = 1;
      found_start = value_start;
      found_end = value_end;
    }
    pos = zero_json_skip_ws_at(input, value_end);
    if (pos < input.len && input.ptr[pos] == '}') {
      size_t trailing = zero_json_skip_ws_at(input, pos + 1);
      if (trailing != input.len) return 0;
      return zero_json_pack_span(found, found_start, found_end - found_start);
    }
    if (pos >= input.len || input.ptr[pos] != ',') return 0;
    pos = zero_json_skip_ws_at(input, pos + 1);
    if (pos >= input.len || input.ptr[pos] == '}') return 0;
  }
  return 0;
}

uint64_t zero_json_lookup_scalar(ZeroByteView input, ZeroByteView key, uint32_t op) {
  uint64_t packed = zero_json_field(input, key);
  if ((packed >> 63) == 0) return 0;
  size_t offset = (size_t)(uint32_t)packed;
  size_t len = (size_t)((packed >> 32) & 0x7fffffffu);
  if (offset > input.len || len > input.len - offset) return 0;
  ZeroByteView raw = {input.ptr + offset, len};
  if (op == 0) return zero_parse_u32(raw);
  if (op == 1) {
    static const unsigned char true_lit[] = {'t', 'r', 'u', 'e'};
    static const unsigned char false_lit[] = {'f', 'a', 'l', 's', 'e'};
    if (raw.len == sizeof(true_lit) && memcmp(raw.ptr, true_lit, sizeof(true_lit)) == 0) return (1ull << 32) | 1u;
    if (raw.len == sizeof(false_lit) && memcmp(raw.ptr, false_lit, sizeof(false_lit)) == 0) return (1ull << 32);
  }
  return 0;
}

static int zero_json_continuation(unsigned char ch) {
  return (ch & 0xc0u) == 0x80u;
}

static size_t zero_json_utf8_char_len(const unsigned char *ptr, size_t len, size_t pos) {
  if (!ptr || pos >= len) return 0;
  unsigned char ch = ptr[pos];
  if (ch < 0x80u) return 1;
  if (ch >= 0xc2u && ch <= 0xdfu) {
    return pos + 1 < len && zero_json_continuation(ptr[pos + 1]) ? 2 : 0;
  }
  if (ch == 0xe0u) {
    return pos + 2 < len && ptr[pos + 1] >= 0xa0u && ptr[pos + 1] <= 0xbfu && zero_json_continuation(ptr[pos + 2]) ? 3 : 0;
  }
  if ((ch >= 0xe1u && ch <= 0xecu) || (ch >= 0xeeu && ch <= 0xefu)) {
    return pos + 2 < len && zero_json_continuation(ptr[pos + 1]) && zero_json_continuation(ptr[pos + 2]) ? 3 : 0;
  }
  if (ch == 0xedu) {
    return pos + 2 < len && ptr[pos + 1] >= 0x80u && ptr[pos + 1] <= 0x9fu && zero_json_continuation(ptr[pos + 2]) ? 3 : 0;
  }
  if (ch == 0xf0u) {
    return pos + 3 < len && ptr[pos + 1] >= 0x90u && ptr[pos + 1] <= 0xbfu && zero_json_continuation(ptr[pos + 2]) && zero_json_continuation(ptr[pos + 3]) ? 4 : 0;
  }
  if (ch >= 0xf1u && ch <= 0xf3u) {
    return pos + 3 < len && zero_json_continuation(ptr[pos + 1]) && zero_json_continuation(ptr[pos + 2]) && zero_json_continuation(ptr[pos + 3]) ? 4 : 0;
  }
  if (ch == 0xf4u) {
    return pos + 3 < len && ptr[pos + 1] >= 0x80u && ptr[pos + 1] <= 0x8fu && zero_json_continuation(ptr[pos + 2]) && zero_json_continuation(ptr[pos + 3]) ? 4 : 0;
  }
  return 0;
}

uint64_t zero_json_string_decode(ZeroMutByteView buffer, ZeroByteView raw) {
  if ((!buffer.ptr && buffer.len > 0) || !raw.ptr || raw.len < 2 || raw.ptr[0] != '"') return 0;
  size_t pos = 1;
  size_t out = 0;
  while (pos < raw.len) {
    unsigned char ch = raw.ptr[pos++];
    if (ch == '"') {
      return pos == raw.len ? zero_json_pack_span(1, 0, out) : 0;
    }
    if (ch < 0x20u) return 0;
    if (ch != '\\') {
      size_t char_len = ch < 0x80u ? 1 : zero_json_utf8_char_len(raw.ptr, raw.len, pos - 1);
      if (char_len == 0 || out + char_len > buffer.len) return 0;
      for (size_t i = 0; i < char_len; i++) buffer.ptr[out++] = raw.ptr[pos - 1 + i];
      pos += char_len - 1;
      continue;
    }
    if (pos >= raw.len) return 0;
    unsigned char escaped = raw.ptr[pos++];
    unsigned char decoded = 0;
    if (escaped == '"' || escaped == '\\' || escaped == '/') {
      decoded = escaped;
    } else if (escaped == 'b') {
      decoded = 8;
    } else if (escaped == 'f') {
      decoded = 12;
    } else if (escaped == 'n') {
      decoded = 10;
    } else if (escaped == 'r') {
      decoded = 13;
    } else if (escaped == 't') {
      decoded = 9;
    } else if (escaped == 'u') {
      uint16_t unit = 0;
      if (!zero_json_hex4_value(raw.ptr, raw.len, pos, &unit)) return 0;
      uint32_t codepoint = unit;
      pos += 4;
      if (zero_json_high_surrogate(unit)) {
        if (pos + 6 > raw.len || raw.ptr[pos] != '\\' || raw.ptr[pos + 1] != 'u') return 0;
        pos += 2;
        uint16_t low = 0;
        if (!zero_json_hex4_value(raw.ptr, raw.len, pos, &low) || !zero_json_low_surrogate(low)) return 0;
        codepoint = zero_json_surrogate_codepoint(unit, low);
        pos += 4;
      } else if (zero_json_low_surrogate(unit)) {
        return 0;
      }
      unsigned char scratch[4] = {0};
      size_t scratch_len = zero_json_write_utf8_bytes(codepoint, scratch);
      if (scratch_len == 0 || out + scratch_len > buffer.len) return 0;
      for (size_t i = 0; i < scratch_len; i++) buffer.ptr[out++] = scratch[i];
      continue;
    } else {
      return 0;
    }
    if (out >= buffer.len) return 0;
    buffer.ptr[out++] = decoded;
  }
  return 0;
}

uint64_t zero_json_string_field(ZeroMutByteView buffer, ZeroByteView input, ZeroByteView key) {
  uint64_t packed = zero_json_field(input, key);
  if ((packed >> 63) == 0) return 0;
  size_t offset = (size_t)(uint32_t)packed;
  size_t len = (size_t)((packed >> 32) & 0x7fffffffu);
  if (offset > input.len || len > input.len - offset) return 0;
  return zero_json_string_decode(buffer, (ZeroByteView){input.ptr + offset, len});
}

static int zero_json_write_byte(ZeroMutByteView buffer, size_t *out, unsigned char byte) {
  if (!out || *out >= buffer.len) return 0;
  buffer.ptr[*out] = byte;
  *out += 1;
  return 1;
}

static int zero_json_write_escape_pair(ZeroMutByteView buffer, size_t *out, unsigned char escaped) {
  return zero_json_write_byte(buffer, out, '\\') && zero_json_write_byte(buffer, out, escaped);
}

static int zero_json_write_u00_escape(ZeroMutByteView buffer, size_t *out, unsigned char byte) {
  static const unsigned char hex[] = "0123456789abcdef";
  return zero_json_write_byte(buffer, out, '\\') &&
         zero_json_write_byte(buffer, out, 'u') &&
         zero_json_write_byte(buffer, out, '0') &&
         zero_json_write_byte(buffer, out, '0') &&
         zero_json_write_byte(buffer, out, hex[(byte >> 4) & 0xfu]) &&
         zero_json_write_byte(buffer, out, hex[byte & 0xfu]);
}

uint64_t zero_json_write_string(ZeroMutByteView buffer, ZeroByteView text) {
  if (!buffer.ptr || (text.len > 0 && !text.ptr)) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '"')) return 0;
  size_t pos = 0;
  while (pos < text.len) {
    unsigned char byte = text.ptr[pos];
    if (byte == '"') {
      if (!zero_json_write_escape_pair(buffer, &out, '"')) return 0;
      pos++;
    } else if (byte == '\\') {
      if (!zero_json_write_escape_pair(buffer, &out, '\\')) return 0;
      pos++;
    } else if (byte == '\b') {
      if (!zero_json_write_escape_pair(buffer, &out, 'b')) return 0;
      pos++;
    } else if (byte == '\f') {
      if (!zero_json_write_escape_pair(buffer, &out, 'f')) return 0;
      pos++;
    } else if (byte == '\n') {
      if (!zero_json_write_escape_pair(buffer, &out, 'n')) return 0;
      pos++;
    } else if (byte == '\r') {
      if (!zero_json_write_escape_pair(buffer, &out, 'r')) return 0;
      pos++;
    } else if (byte == '\t') {
      if (!zero_json_write_escape_pair(buffer, &out, 't')) return 0;
      pos++;
    } else if (byte < 0x20u) {
      if (!zero_json_write_u00_escape(buffer, &out, byte)) return 0;
      pos++;
    } else {
      size_t width = zero_json_utf8_char_len(text.ptr, text.len, pos);
      if (width == 0 || out + width > buffer.len) return 0;
      for (size_t i = 0; i < width; i++) buffer.ptr[out++] = text.ptr[pos + i];
      pos += width;
    }
  }
  if (!zero_json_write_byte(buffer, &out, '"')) return 0;
  return zero_json_pack_span(1, 0, out);
}

static int zero_json_write_span_bytes(ZeroMutByteView buffer, size_t *out, ZeroByteView bytes) {
  if (!out || (bytes.len > 0 && !bytes.ptr) || *out > buffer.len || bytes.len > buffer.len - *out) return 0;
  if (bytes.len > 0) memcpy(buffer.ptr + *out, bytes.ptr, bytes.len);
  *out += bytes.len;
  return 1;
}

static int zero_json_write_u32_value(ZeroMutByteView buffer, size_t *out, uint32_t value) {
  unsigned char digits[10];
  size_t len = 0;
  do {
    digits[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value != 0);
  while (len > 0) {
    if (!zero_json_write_byte(buffer, out, digits[--len])) return 0;
  }
  return 1;
}

static int zero_json_write_bool_value(ZeroMutByteView buffer, size_t *out, uint32_t value) {
  static const unsigned char true_lit[] = {'t', 'r', 'u', 'e'};
  static const unsigned char false_lit[] = {'f', 'a', 'l', 's', 'e'};
  ZeroByteView lit = value ? (ZeroByteView){true_lit, sizeof(true_lit)} : (ZeroByteView){false_lit, sizeof(false_lit)};
  return zero_json_write_span_bytes(buffer, out, lit);
}

static int zero_json_write_string_value_at(ZeroMutByteView buffer, size_t *out, ZeroByteView text) {
  if (!out || *out > buffer.len) return 0;
  uint64_t packed = zero_json_write_string((ZeroMutByteView){buffer.ptr + *out, buffer.len - *out}, text);
  if ((packed >> 63) == 0) return 0;
  size_t len = (size_t)((packed >> 32) & 0x7fffffffu);
  *out += len;
  return 1;
}

typedef enum {
  ZERO_JSON_WRITE_FIELD_RAW = 0,
  ZERO_JSON_WRITE_FIELD_STRING = 1,
  ZERO_JSON_WRITE_FIELD_U32 = 2,
  ZERO_JSON_WRITE_FIELD_BOOL = 3
} ZeroJsonWriteFieldKind;

static int zero_json_write_field_at(ZeroMutByteView buffer, size_t *out, ZeroByteView key, ZeroByteView value, uint64_t scalar, ZeroJsonWriteFieldKind kind) {
  if (!zero_json_write_string_value_at(buffer, out, key)) return 0;
  if (!zero_json_write_byte(buffer, out, ':')) return 0;
  switch (kind) {
    case ZERO_JSON_WRITE_FIELD_RAW:
      if (zero_json_parse_bytes(value) < 0) return 0;
      return zero_json_write_span_bytes(buffer, out, value);
    case ZERO_JSON_WRITE_FIELD_STRING:
      return zero_json_write_string_value_at(buffer, out, value);
    case ZERO_JSON_WRITE_FIELD_U32:
      return zero_json_write_u32_value(buffer, out, (uint32_t)scalar);
    case ZERO_JSON_WRITE_FIELD_BOOL:
      return zero_json_write_bool_value(buffer, out, scalar ? 1u : 0u);
  }
  return 0;
}

static uint64_t zero_json_write_field_result(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value, uint64_t scalar, ZeroJsonWriteFieldKind kind) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_field_at(buffer, &out, key, value, scalar, kind)) return 0;
  return zero_json_pack_span(1, 0, out);
}

static uint64_t zero_json_write_object1_result(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value, uint64_t scalar, ZeroJsonWriteFieldKind kind) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '{')) return 0;
  if (!zero_json_write_field_at(buffer, &out, key, value, scalar, kind)) return 0;
  if (!zero_json_write_byte(buffer, &out, '}')) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_field_raw(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value) {
  return zero_json_write_field_result(buffer, key, value, 0, ZERO_JSON_WRITE_FIELD_RAW);
}

uint64_t zero_json_write_field_string(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value) {
  return zero_json_write_field_result(buffer, key, value, 0, ZERO_JSON_WRITE_FIELD_STRING);
}

uint64_t zero_json_write_field_u32(ZeroMutByteView buffer, ZeroByteView key, uint32_t value) {
  return zero_json_write_field_result(buffer, key, (ZeroByteView){0}, value, ZERO_JSON_WRITE_FIELD_U32);
}

uint64_t zero_json_write_field_bool(ZeroMutByteView buffer, ZeroByteView key, uint32_t value) {
  return zero_json_write_field_result(buffer, key, (ZeroByteView){0}, value ? 1u : 0u, ZERO_JSON_WRITE_FIELD_BOOL);
}

uint64_t zero_json_write_object1_string(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value) {
  return zero_json_write_object1_result(buffer, key, value, 0, ZERO_JSON_WRITE_FIELD_STRING);
}

uint64_t zero_json_write_object1_u32(ZeroMutByteView buffer, ZeroByteView key, uint32_t value) {
  return zero_json_write_object1_result(buffer, key, (ZeroByteView){0}, value, ZERO_JSON_WRITE_FIELD_U32);
}

uint64_t zero_json_write_object1_bool(ZeroMutByteView buffer, ZeroByteView key, uint32_t value) {
  return zero_json_write_object1_result(buffer, key, (ZeroByteView){0}, value ? 1u : 0u, ZERO_JSON_WRITE_FIELD_BOOL);
}

uint64_t zero_json_write_object2_fields(ZeroMutByteView buffer, ZeroByteView field0, ZeroByteView field1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '{')) return 0;
  if (!zero_json_write_span_bytes(buffer, &out, field0)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_span_bytes(buffer, &out, field1)) return 0;
  if (!zero_json_write_byte(buffer, &out, '}')) return 0;
  ZeroByteView candidate = {buffer.ptr, out};
  if (zero_json_parse_bytes(candidate) < 0) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_object2_string_field(ZeroMutByteView buffer, ZeroByteView key, ZeroByteView value, ZeroByteView field1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '{')) return 0;
  if (!zero_json_write_field_at(buffer, &out, key, value, 0, ZERO_JSON_WRITE_FIELD_STRING)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_span_bytes(buffer, &out, field1)) return 0;
  if (!zero_json_write_byte(buffer, &out, '}')) return 0;
  ZeroByteView candidate = {buffer.ptr, out};
  if (zero_json_parse_bytes(candidate) < 0) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_object2_u32_field(ZeroMutByteView buffer, ZeroByteView key, uint32_t value, ZeroByteView field1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '{')) return 0;
  if (!zero_json_write_field_at(buffer, &out, key, (ZeroByteView){0}, value, ZERO_JSON_WRITE_FIELD_U32)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_span_bytes(buffer, &out, field1)) return 0;
  if (!zero_json_write_byte(buffer, &out, '}')) return 0;
  ZeroByteView candidate = {buffer.ptr, out};
  if (zero_json_parse_bytes(candidate) < 0) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_object2_bool_field(ZeroMutByteView buffer, ZeroByteView key, uint32_t value, ZeroByteView field1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '{')) return 0;
  if (!zero_json_write_field_at(buffer, &out, key, (ZeroByteView){0}, value ? 1u : 0u, ZERO_JSON_WRITE_FIELD_BOOL)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_span_bytes(buffer, &out, field1)) return 0;
  if (!zero_json_write_byte(buffer, &out, '}')) return 0;
  ZeroByteView candidate = {buffer.ptr, out};
  if (zero_json_parse_bytes(candidate) < 0) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_array2_strings(ZeroMutByteView buffer, ZeroByteView value0, ZeroByteView value1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '[')) return 0;
  if (!zero_json_write_string_value_at(buffer, &out, value0)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_string_value_at(buffer, &out, value1)) return 0;
  if (!zero_json_write_byte(buffer, &out, ']')) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_array2_u32(ZeroMutByteView buffer, uint32_t value0, uint32_t value1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '[')) return 0;
  if (!zero_json_write_u32_value(buffer, &out, value0)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_u32_value(buffer, &out, value1)) return 0;
  if (!zero_json_write_byte(buffer, &out, ']')) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint64_t zero_json_write_array2_bools(ZeroMutByteView buffer, uint32_t value0, uint32_t value1) {
  if (!buffer.ptr) return 0;
  size_t out = 0;
  if (!zero_json_write_byte(buffer, &out, '[')) return 0;
  if (!zero_json_write_bool_value(buffer, &out, value0 ? 1u : 0u)) return 0;
  if (!zero_json_write_byte(buffer, &out, ',')) return 0;
  if (!zero_json_write_bool_value(buffer, &out, value1 ? 1u : 0u)) return 0;
  if (!zero_json_write_byte(buffer, &out, ']')) return 0;
  return zero_json_pack_span(1, 0, out);
}

uint32_t zero_str_contains(ZeroByteView text, ZeroByteView needle) {
  return (uint32_t)zero_str_pair_op(text, needle, ZERO_STR_OP_CONTAINS);
}

static int zero_http_header_token_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
         ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
         ch == '`' || ch == '|' || ch == '~';
}

static unsigned char zero_http_header_lower(unsigned char ch) {
  return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + ('a' - 'A')) : ch;
}

static int zero_http_header_name_equal(const unsigned char *ptr, size_t len, ZeroByteView name) {
  if (!ptr || !name.ptr || len != name.len || len == 0) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char a = ptr[i];
    unsigned char b = name.ptr[i];
    if (!zero_http_header_token_char(b)) return 0;
    if (zero_http_header_lower(a) != zero_http_header_lower(b)) return 0;
  }
  return 1;
}

static uint64_t zero_http_header_pack(size_t offset, size_t len) {
  if (offset > 0x7fffffffu || len > 0xffffffffu) return 0;
  return (1ull << 63) | ((uint64_t)(uint32_t)offset << 32) | (uint64_t)(uint32_t)len;
}

static uint64_t zero_http_span_pack(size_t offset, size_t len) {
  if (offset > 0xffffffffu || len > 0x7fffffffu) return 0;
  return (1ull << 63) | ((uint64_t)(uint32_t)len << 32) | (uint64_t)(uint32_t)offset;
}

static uint32_t zero_http_read_u32le(const unsigned char *ptr) {
  return (uint32_t)ptr[0] |
         ((uint32_t)ptr[1] << 8) |
         ((uint32_t)ptr[2] << 16) |
         ((uint32_t)ptr[3] << 24);
}

static int zero_http_response_has_meta(ZeroByteView response) {
  if (!response.ptr || response.len < ZERO_HTTP_RESPONSE_META_BYTES) return 0;
  return response.ptr[0] == 'Z' &&
         response.ptr[1] == 'H' &&
         response.ptr[2] == 'R' &&
         response.ptr[3] == '1';
}

static size_t zero_http_raw_header_len(ZeroByteView response) {
  if (!response.ptr || response.len == 0) return 0;
  size_t pos = 0;
  while (pos < response.len) {
    size_t line_start = pos;
    while (pos < response.len && response.ptr[pos] != '\n') pos++;
    size_t line_end = pos;
    if (line_end > line_start && response.ptr[line_end - 1] == '\r') line_end--;
    size_t next = pos < response.len ? pos + 1 : pos;
    if (line_end == line_start) return next;
    pos = next;
  }
  return response.len;
}

uint32_t zero_http_response_len(ZeroByteView response) {
  if (!zero_http_response_has_meta(response)) {
    return response.len > 0xffffffffu ? 0xffffffffu : (uint32_t)response.len;
  }
  uint32_t headers_len = zero_http_read_u32le(response.ptr + 8);
  uint32_t body_len = zero_http_read_u32le(response.ptr + 12);
  if (headers_len > 0xffffffffu - body_len) return 0xffffffffu;
  return headers_len + body_len;
}

uint32_t zero_http_response_headers_len(ZeroByteView response) {
  if (zero_http_response_has_meta(response)) return zero_http_read_u32le(response.ptr + 8);
  size_t len = zero_http_raw_header_len(response);
  return len > 0xffffffffu ? 0xffffffffu : (uint32_t)len;
}

uint32_t zero_http_response_body_offset(ZeroByteView response) {
  if (zero_http_response_has_meta(response)) {
    uint32_t headers_len = zero_http_read_u32le(response.ptr + 8);
    return ZERO_HTTP_RESPONSE_META_BYTES + headers_len;
  }
  size_t offset = zero_http_raw_header_len(response);
  return offset > 0xffffffffu ? 0xffffffffu : (uint32_t)offset;
}

uint64_t zero_http_header_value(ZeroByteView headers, ZeroByteView name) {
  if (!headers.ptr || !name.ptr || headers.len == 0 || name.len == 0) return 0;
  for (size_t i = 0; i < name.len; i++) {
    if (!zero_http_header_token_char(name.ptr[i])) return 0;
  }

  size_t header_offset = 0;
  size_t header_len = headers.len;
  if (zero_http_response_has_meta(headers)) {
    header_offset = ZERO_HTTP_RESPONSE_META_BYTES;
    header_len = zero_http_read_u32le(headers.ptr + 8);
    if (header_offset > headers.len || header_len > headers.len - header_offset) return 0;
  }

  size_t pos = header_offset;
  size_t limit = header_offset + header_len;
  while (pos < limit) {
    size_t line_start = pos;
    while (pos < limit && headers.ptr[pos] != '\n') pos++;
    size_t line_end = pos;
    if (line_end > line_start && headers.ptr[line_end - 1] == '\r') line_end--;
    if (pos < limit) pos++;

    if (line_end == line_start) break;
    size_t colon = line_start;
    while (colon < line_end && headers.ptr[colon] != ':') colon++;
    if (colon == line_start || colon >= line_end) continue;
    if (!zero_http_header_name_equal(headers.ptr + line_start, colon - line_start, name)) continue;

    size_t value_start = colon + 1;
    while (value_start < line_end && (headers.ptr[value_start] == ' ' || headers.ptr[value_start] == '\t')) value_start++;
    size_t value_end = line_end;
    while (value_end > value_start && (headers.ptr[value_end - 1] == ' ' || headers.ptr[value_end - 1] == '\t')) value_end--;
    return zero_http_header_pack(value_start, value_end - value_start);
  }
  return 0;
}

uint32_t zero_http_header_found(uint64_t value) {
  return (value >> 63) ? 1u : 0u;
}

uint32_t zero_http_header_offset(uint64_t value) {
  return zero_http_header_found(value) ? (uint32_t)((value >> 32) & 0x7fffffffu) : 0u;
}

uint32_t zero_http_header_len(uint64_t value) {
  return zero_http_header_found(value) ? (uint32_t)(value & 0xffffffffu) : 0u;
}

static int zero_http_token_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '!' || ch == '#' || ch == '$' || ch == '%' ||
         ch == '&' || ch == '\'' || ch == '*' || ch == '+' ||
         ch == '-' || ch == '.' || ch == '^' || ch == '_' ||
         ch == '`' || ch == '|' || ch == '~';
}

static int zero_http_method_valid(ZeroByteView method) {
  if (!method.ptr || method.len == 0) return 0;
  for (size_t i = 0; i < method.len; i++) {
    if (!zero_http_token_char(method.ptr[i])) return 0;
  }
  return 1;
}

static ZeroByteView zero_http_trim_trailing_cr(ZeroByteView view) {
  if (view.len > 0 && view.ptr[view.len - 1] == '\r') view.len--;
  return view;
}

static int zero_http_first_line(ZeroByteView request, ZeroByteView *line_out) {
  if (line_out) *line_out = (ZeroByteView){0};
  if (!request.ptr || request.len == 0) return 0;
  size_t line_end = 0;
  while (line_end < request.len && request.ptr[line_end] != '\n') line_end++;
  if (line_end >= request.len) return 0;
  ZeroByteView line = zero_http_trim_trailing_cr((ZeroByteView){request.ptr, line_end});
  if (line.len == 0) return 0;
  if (line_out) *line_out = line;
  return 1;
}

static int zero_http_request_target(ZeroByteView request, size_t *offset_out, size_t *len_out) {
  if (offset_out) *offset_out = 0;
  if (len_out) *len_out = 0;
  ZeroByteView line = {0};
  if (!zero_http_first_line(request, &line)) return 0;
  size_t space = 0;
  while (space < line.len && line.ptr[space] != ' ') space++;
  if (space == 0 || space + 1 >= line.len) return 0;
  ZeroByteView method = {line.ptr, space};
  if (!zero_http_method_valid(method)) return 0;
  size_t start = space + 1;
  size_t end = start;
  while (end < line.len && line.ptr[end] != ' ') {
    if (line.ptr[end] <= 32u) return 0;
    end++;
  }
  if (end == start) return 0;
  if (end < line.len) {
    size_t version_start = end;
    while (version_start < line.len && line.ptr[version_start] == ' ') version_start++;
    if (version_start < line.len) {
      ZeroByteView version = {line.ptr + version_start, line.len - version_start};
      if (!((version.len == 8 && memcmp(version.ptr, "HTTP/1.0", 8) == 0) ||
            (version.len == 8 && memcmp(version.ptr, "HTTP/1.1", 8) == 0))) {
        return 0;
      }
    }
  }
  if (offset_out) *offset_out = (size_t)(line.ptr + start - request.ptr);
  if (len_out) *len_out = end - start;
  return 1;
}

uint64_t zero_http_request_method_name(ZeroByteView request) {
  ZeroByteView line = {0};
  if (!zero_http_first_line(request, &line)) return 0;
  size_t space = 0;
  while (space < line.len && line.ptr[space] != ' ') space++;
  if (space == 0 || space >= line.len) return 0;
  ZeroByteView method = {line.ptr, space};
  if (!zero_http_method_valid(method)) return 0;
  return zero_http_span_pack(0, space);
}

uint64_t zero_http_request_path(ZeroByteView request) {
  size_t target_offset = 0;
  size_t target_len = 0;
  if (!zero_http_request_target(request, &target_offset, &target_len)) return 0;
  const unsigned char *target = request.ptr + target_offset;

  size_t path_start = 0;
  if (target_len >= 7 && memcmp(target, "http://", 7) == 0) {
    path_start = 7;
    while (path_start < target_len && target[path_start] != '/') path_start++;
    if (path_start >= target_len) return zero_http_span_pack(target_offset + 5, 1);
  } else if (target_len >= 8 && memcmp(target, "https://", 8) == 0) {
    path_start = 8;
    while (path_start < target_len && target[path_start] != '/') path_start++;
    if (path_start >= target_len) return zero_http_span_pack(target_offset + 6, 1);
  }

  size_t path_end = path_start;
  while (path_end < target_len && target[path_end] != '?' && target[path_end] != '#') path_end++;
  if (path_end == path_start) return 0;
  return zero_http_span_pack(target_offset + path_start, path_end - path_start);
}

uint32_t zero_http_request_matches(ZeroByteView request, ZeroByteView method, ZeroByteView path) {
  if ((!method.ptr && method.len > 0) || (!path.ptr && path.len > 0)) return 0;
  uint64_t method_span = zero_http_request_method_name(request);
  if ((method_span >> 63) == 0) return 0;
  size_t method_len = (size_t)((method_span >> 32) & 0x7fffffffu);
  size_t method_offset = (size_t)(method_span & 0xffffffffu);
  if (method_offset > request.len || method_len > request.len - method_offset) return 0;
  if (method_len != method.len || (method_len > 0 && memcmp(request.ptr + method_offset, method.ptr, method_len) != 0)) return 0;

  uint64_t path_span = zero_http_request_path(request);
  if ((path_span >> 63) == 0) return 0;
  size_t path_len = (size_t)((path_span >> 32) & 0x7fffffffu);
  size_t path_offset = (size_t)(path_span & 0xffffffffu);
  if (path_offset > request.len || path_len > request.len - path_offset) return 0;
  return path_len == path.len && (path_len == 0 || memcmp(request.ptr + path_offset, path.ptr, path_len) == 0);
}

static int zero_http_body_start(ZeroByteView request, size_t *start_out) {
  if (start_out) *start_out = 0;
  if (!request.ptr) return 0;
  size_t start = 0;
  while (start < request.len) {
    size_t end = start;
    while (end < request.len && request.ptr[end] != '\n') end++;
    size_t line_end = end;
    if (line_end > start && request.ptr[line_end - 1] == '\r') line_end--;
    size_t next = end < request.len ? end + 1 : end;
    if (line_end == start) {
      if (start_out) *start_out = next;
      return 1;
    }
    start = next;
  }
  return 0;
}

static int zero_http_value_starts_with_ignore_ascii_case(ZeroByteView value, const char *prefix, size_t prefix_len) {
  if (!prefix || value.len < prefix_len) return 0;
  for (size_t i = 0; i < prefix_len; i++) {
    if (zero_http_header_lower(value.ptr[i]) != zero_http_header_lower((unsigned char)prefix[i])) return 0;
  }
  return 1;
}

static int zero_http_request_has_json_content_type(ZeroByteView request) {
  static const unsigned char header_name[] = "content-type";
  static const char json_prefix[] = "application/json";
  uint64_t header = zero_http_header_value(request, (ZeroByteView){header_name, sizeof(header_name) - 1});
  if ((header >> 63) == 0) return 0;
  size_t offset = (size_t)((header >> 32) & 0x7fffffffu);
  size_t len = (size_t)(header & 0xffffffffu);
  if (offset > request.len || len > request.len - offset) return 0;
  ZeroByteView value = {request.ptr + offset, len};
  if (!zero_http_value_starts_with_ignore_ascii_case(value, json_prefix, sizeof(json_prefix) - 1)) return 0;
  return value.len == sizeof(json_prefix) - 1 || value.ptr[sizeof(json_prefix) - 1] == ';';
}

uint64_t zero_http_request_body_within(ZeroByteView request, uint64_t max, uint32_t require_json) {
  size_t body_start = 0;
  if (!zero_http_body_start(request, &body_start)) return 0;
  if (body_start > request.len) return 0;
  size_t body_len = request.len - body_start;
  if (body_len > max) return 0;
  ZeroByteView body = {request.ptr + body_start, body_len};
  if (require_json) {
    if (!zero_http_request_has_json_content_type(request)) return 0;
    if (zero_json_parse_bytes(body) < 0) return 0;
  }
  return zero_http_span_pack(body_start, body_len);
}

static const char *zero_http_status_reason(uint32_t status) {
  switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 304: return "Not Modified";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 422: return "Unprocessable Content";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return "Unknown";
  }
}

static int zero_http_write_bytes(ZeroMutByteView buffer, size_t *offset, const unsigned char *bytes, size_t len) {
  if (!offset || !buffer.ptr || (!bytes && len > 0)) return 0;
  if (*offset > buffer.len || len > buffer.len - *offset) return 0;
  if (len > 0) memcpy(buffer.ptr + *offset, bytes, len);
  *offset += len;
  return 1;
}

static int zero_http_write_literal(ZeroMutByteView buffer, size_t *offset, const char *text) {
  return zero_http_write_bytes(buffer, offset, (const unsigned char *)text, strlen(text));
}

static int zero_http_write_u32_decimal(ZeroMutByteView buffer, size_t *offset, uint32_t value) {
  unsigned char tmp[10];
  size_t len = 0;
  do {
    tmp[len++] = (unsigned char)('0' + (value % 10u));
    value /= 10u;
  } while (value > 0 && len < sizeof(tmp));
  if (!offset || *offset > buffer.len || len > buffer.len - *offset) return 0;
  for (size_t i = 0; i < len; i++) buffer.ptr[*offset + i] = tmp[len - 1 - i];
  *offset += len;
  return 1;
}

uint32_t zero_http_write_json_response(ZeroMutByteView buffer, uint32_t status, ZeroByteView body) {
  if (!buffer.ptr || (!body.ptr && body.len > 0) || body.len > 0xffffffffu) return 0;
  size_t offset = 0;
  const char *reason = zero_http_status_reason(status);
  if (!zero_http_write_literal(buffer, &offset, "HTTP/1.1 ") ||
      !zero_http_write_u32_decimal(buffer, &offset, status) ||
      !zero_http_write_literal(buffer, &offset, " ") ||
      !zero_http_write_literal(buffer, &offset, reason) ||
      !zero_http_write_literal(buffer, &offset, "\r\ncontent-type: application/json\r\nconnection: close\r\ncontent-length: ") ||
      !zero_http_write_u32_decimal(buffer, &offset, (uint32_t)body.len) ||
      !zero_http_write_literal(buffer, &offset, "\r\n\r\n") ||
      !zero_http_write_bytes(buffer, &offset, body.ptr, body.len) ||
      offset > 0xffffffffu) {
    return 0;
  }
  return (uint32_t)offset;
}

uint32_t zero_http_result_ok(uint64_t result) {
  uint32_t status = zero_http_result_status(result);
  return zero_http_result_error(result) == ZERO_HTTP_OK && status >= 200 && status < 300 ? 1u : 0u;
}

uint32_t zero_http_result_status(uint64_t result) {
  return (uint32_t)((result >> 32) & 0xffffu);
}

uint32_t zero_http_result_body_len(uint64_t result) {
  return (uint32_t)(result & 0xffffffffu);
}

uint32_t zero_http_result_error(uint64_t result) {
  return (uint32_t)((result >> 48) & 0xffffu);
}
