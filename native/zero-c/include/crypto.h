#ifndef ZERO_C_CRYPTO_H
#define ZERO_C_CRYPTO_H

#include <stddef.h>
#include <stdbool.h>

void sha256_hash(const unsigned char* data, size_t len, unsigned char hash[32]);

bool ed25519_keypair(unsigned char public_key[32], unsigned char private_key[64]);
bool ed25519_sign(unsigned char signature[64], const unsigned char* message, size_t message_len,
                  const unsigned char public_key[32], const unsigned char private_key[64]);
bool ed25519_verify(const unsigned char signature[64], const unsigned char* message, size_t message_len,
                    const unsigned char public_key[32]);

#if defined(ZERO_TEST)
bool ed25519_keypair_from_seed(unsigned char public_key[32], unsigned char private_key[64],
                               const unsigned char seed[32]);
#endif

#endif
