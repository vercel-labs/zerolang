#ifndef ZERO_C_PROCESS_H
#define ZERO_C_PROCESS_H

#include <stdbool.h>

bool z_command_available(const char* name);
bool z_command_ok(const char* command);
char* z_command_first_line(const char* command);

#endif
