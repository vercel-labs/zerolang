#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "process_exec.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

void z_process_argv_init(ZProcessArgv *argv) {
  argv->items = NULL;
  argv->len = 0;
  argv->cap = 0;
}

bool z_process_argv_push(ZProcessArgv *argv, const char *value) {
  if (!argv || !value || !value[0]) return false;
  size_t required = argv->len + 2;
  if (required > argv->cap) {
    size_t next = z_grow_capacity(argv->cap, required, 8);
    argv->items = z_checked_reallocarray(argv->items, next, sizeof(char *));
    argv->cap = next;
  }
  argv->items[argv->len++] = z_strdup(value);
  argv->items[argv->len] = NULL;
  return true;
}

bool z_process_argv_append_flag_text(ZProcessArgv *argv, const char *text, bool *suppress_stderr) {
  const char *cursor = text ? text : "";
  while (*cursor) {
    while (isspace((unsigned char)*cursor)) cursor++;
    if (!*cursor) break;

    ZBuf token;
    zbuf_init(&token);
    while (*cursor && !isspace((unsigned char)*cursor)) {
      if (*cursor == '\'') {
        cursor++;
        while (*cursor && *cursor != '\'') zbuf_append_char(&token, *cursor++);
        if (*cursor == '\'') cursor++;
      } else if (*cursor == '\\' && cursor[1]) {
        cursor++;
        zbuf_append_char(&token, *cursor++);
      } else {
        zbuf_append_char(&token, *cursor++);
      }
    }

    bool ok = true;
    if (token.len > 0) {
      if (strcmp(token.data, "2>/dev/null") == 0) {
        if (suppress_stderr) *suppress_stderr = true;
      } else {
        ok = z_process_argv_push(argv, token.data);
      }
    }
    zbuf_free(&token);
    if (!ok) return false;
  }
  return true;
}

void z_process_argv_free(ZProcessArgv *argv) {
  if (!argv) return;
  for (size_t i = 0; i < argv->len; i++) free(argv->items[i]);
  free(argv->items);
  argv->items = NULL;
  argv->len = 0;
  argv->cap = 0;
}

bool z_process_ensure_dir(const char *path) {
  if (!path || !path[0]) return false;
#if defined(_WIN32)
  if (_mkdir(path) == 0) return true;
#else
  if (mkdir(path, 0777) == 0) return true;
#endif
  return errno == EEXIST;
}

bool z_process_run_argv(const ZProcessArgv *argv, bool suppress_stdout, bool suppress_stderr, bool uses_zig_env) {
  if (!argv || argv->len == 0 || !argv->items || !argv->items[0]) return false;
#if defined(_WIN32)
  (void)suppress_stdout;
  (void)suppress_stderr;
  (void)uses_zig_env;
  return _spawnvp(_P_WAIT, argv->items[0], (const char *const *)argv->items) == 0;
#else
  pid_t pid = fork();
  if (pid < 0) return false;
  if (pid == 0) {
    if (uses_zig_env) {
      setenv("ZIG_GLOBAL_CACHE_DIR", ".zero/zig-global-cache", 1);
      setenv("ZIG_LOCAL_CACHE_DIR", ".zero/zig-local-cache", 1);
    }
    if (suppress_stdout) {
      FILE *null_out = freopen("/dev/null", "w", stdout);
      (void)null_out;
    }
    if (suppress_stderr) {
      FILE *null_err = freopen("/dev/null", "w", stderr);
      (void)null_err;
    }
    execvp(argv->items[0], argv->items);
    _exit(127);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0) if (errno != EINTR) return false;
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
