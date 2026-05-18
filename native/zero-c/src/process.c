#include "process.h"
#include "zero.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool z_command_available(const char* name) {
  char command[128];
  snprintf(command, sizeof(command), "command -v %s >/dev/null 2>&1", name);
  return system(command) == 0;
}

bool z_command_ok(const char* command) {
  return system(command) == 0;
}

char* z_command_first_line(const char* command) {
  FILE* pipe = popen(command, "r");
  if (!pipe) return z_strdup("");
  char line[256];
  if (!fgets(line, sizeof(line), pipe)) line[0] = 0;
  pclose(pipe);
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
  return z_strdup(line);
}
