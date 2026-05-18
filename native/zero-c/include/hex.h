#ifndef ZERO_C_HEX_H
#define ZERO_C_HEX_H

#include <stdbool.h>
#include <stddef.h>

bool z_hex_read(const char* hex, size_t byte_len, unsigned char* out);
void z_hex_write(const unsigned char* bytes, size_t len, char* out);

#endif
