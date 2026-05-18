#include "crypto.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char* name;
  const char* seed_hex;
  const char* public_hex;
  const char* message_hex;
  const char* signature_hex;
} Ed25519Vector;

typedef struct {
  unsigned char seed[32];
  unsigned char public_key[32];
  unsigned char signature[64];
  unsigned char* message;
  size_t message_len;
} Ed25519VectorData;

static int hex_digit_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

static bool hex_bytes_parse(const char* hex, size_t expected_len, unsigned char* out) {
  size_t hex_len = strlen(hex);
  if (hex_len != expected_len * 2) return false;
  for (size_t i = 0; i < expected_len; i++) {
    int hi = hex_digit_value(hex[i * 2]);
    int lo = hex_digit_value(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (unsigned char)((hi << 4) | lo);
  }
  return true;
}

static bool hex_bytes_parse_dynamic(const char* hex, unsigned char** bytes, size_t* bytes_len) {
  size_t hex_len = strlen(hex);
  if (hex_len == 0) {
    *bytes = NULL;
    *bytes_len = 0;
    return true;
  }
  if (hex_len % 2 != 0) return false;
  size_t len = hex_len / 2;
  unsigned char* data = malloc(len);
  if (!data) return false;
  if (!hex_bytes_parse(hex, len, data)) {
    free(data);
    return false;
  }
  *bytes = data;
  *bytes_len = len;
  return true;
}

static void hex_bytes_write(const unsigned char* bytes, size_t len, char* out) {
  static const char* digits = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[i * 2] = digits[(bytes[i] >> 4) & 0xf];
    out[i * 2 + 1] = digits[bytes[i] & 0xf];
  }
  out[len * 2] = 0;
}

static bool bytes_match(const unsigned char* left, const unsigned char* right, size_t len) {
  return memcmp(left, right, len) == 0;
}

static bool sha256_vector_check(const char* name, const unsigned char* message, size_t message_len,
                                const char* expected_hex) {
  unsigned char digest[32];
  unsigned char expected[32];
  if (!hex_bytes_parse(expected_hex, sizeof(expected), expected)) {
    fprintf(stderr, "sha256 vector %s has invalid hex\n", name);
    return false;
  }
  sha256_hash(message, message_len, digest);
  if (!bytes_match(digest, expected, sizeof(digest))) {
    char got_hex[65];
    hex_bytes_write(digest, sizeof(digest), got_hex);
    fprintf(stderr, "sha256 vector %s mismatch\nexpected: %s\nactual:   %s\n", name, expected_hex, got_hex);
    return false;
  }
  return true;
}

static bool sha256_tests_run(void) {
  static const char* message_short = "abc";
  static const char* message_long = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  unsigned char* million = malloc(1000000);
  if (!million) return false;
  memset(million, 'a', 1000000);

  bool ok = true;
  ok = ok && sha256_vector_check("empty", (const unsigned char*)"", 0,
                                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  ok = ok && sha256_vector_check("abc", (const unsigned char*)message_short, strlen(message_short),
                                 "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
  ok = ok && sha256_vector_check("long", (const unsigned char*)message_long, strlen(message_long),
                                 "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1");
  ok = ok && sha256_vector_check("million", million, 1000000,
                                 "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0");

  free(million);
  return ok;
}

static void ed25519_vector_data_clear(Ed25519VectorData* data) {
  free(data->message);
  data->message = NULL;
  data->message_len = 0;
}

static bool ed25519_vector_parse(const Ed25519Vector* vector, Ed25519VectorData* data) {
  data->message = NULL;
  data->message_len = 0;
  if (!hex_bytes_parse(vector->seed_hex, sizeof(data->seed), data->seed)) {
    fprintf(stderr, "ed25519 vector %s seed hex invalid\n", vector->name);
    return false;
  }
  if (!hex_bytes_parse(vector->public_hex, sizeof(data->public_key), data->public_key)) {
    fprintf(stderr, "ed25519 vector %s public hex invalid\n", vector->name);
    return false;
  }
  if (!hex_bytes_parse(vector->signature_hex, sizeof(data->signature), data->signature)) {
    fprintf(stderr, "ed25519 vector %s signature hex invalid\n", vector->name);
    return false;
  }
  if (!hex_bytes_parse_dynamic(vector->message_hex, &data->message, &data->message_len)) {
    fprintf(stderr, "ed25519 vector %s message hex invalid\n", vector->name);
    return false;
  }
  return true;
}

static bool ed25519_keypair_check(const Ed25519Vector* vector, const Ed25519VectorData* data,
                                  unsigned char public_key[32], unsigned char private_key[64]) {
  if (!ed25519_keypair_from_seed(public_key, private_key, data->seed)) {
    fprintf(stderr, "ed25519 vector %s keypair from seed failed\n", vector->name);
    return false;
  }
  if (!bytes_match(public_key, data->public_key, sizeof(data->public_key))) {
    fprintf(stderr, "ed25519 vector %s public key mismatch\n", vector->name);
    return false;
  }
  if (!bytes_match(private_key + 32, data->public_key, sizeof(data->public_key))) {
    fprintf(stderr, "ed25519 vector %s private key suffix mismatch\n", vector->name);
    return false;
  }
  return true;
}

static bool ed25519_signature_check(const Ed25519Vector* vector, const Ed25519VectorData* data,
                                    const unsigned char public_key[32], const unsigned char private_key[64],
                                    unsigned char signature[64]) {
  if (!ed25519_sign(signature, data->message, data->message_len, public_key, private_key)) {
    fprintf(stderr, "ed25519 vector %s sign failed\n", vector->name);
    return false;
  }
  if (!bytes_match(signature, data->signature, sizeof(data->signature))) {
    fprintf(stderr, "ed25519 vector %s signature mismatch\n", vector->name);
    return false;
  }
  if (!ed25519_verify(signature, data->message, data->message_len, public_key)) {
    fprintf(stderr, "ed25519 vector %s verify failed\n", vector->name);
    return false;
  }
  return true;
}

static bool ed25519_tamper_check(const Ed25519Vector* vector, const Ed25519VectorData* data,
                                 const unsigned char public_key[32], const unsigned char signature[64]) {
  unsigned char signature_bad[64];
  memcpy(signature_bad, signature, sizeof(signature_bad));
  signature_bad[0] ^= 0x01;
  if (ed25519_verify(signature_bad, data->message, data->message_len, public_key)) {
    fprintf(stderr, "ed25519 vector %s signature tamper accepted\n", vector->name);
    return false;
  }

  unsigned char public_bad[32];
  memcpy(public_bad, public_key, sizeof(public_bad));
  public_bad[0] ^= 0x01;
  if (ed25519_verify(signature, data->message, data->message_len, public_bad)) {
    fprintf(stderr, "ed25519 vector %s public key tamper accepted\n", vector->name);
    return false;
  }

  if (data->message_len == 0) {
    unsigned char message_bad[1] = {0};
    if (ed25519_verify(signature, message_bad, sizeof(message_bad), public_key)) {
      fprintf(stderr, "ed25519 vector %s message tamper accepted\n", vector->name);
      return false;
    }
    return true;
  }

  unsigned char* message_bad = malloc(data->message_len);
  if (!message_bad) return false;
  memcpy(message_bad, data->message, data->message_len);
  message_bad[0] ^= 0x01;
  if (ed25519_verify(signature, message_bad, data->message_len, public_key)) {
    fprintf(stderr, "ed25519 vector %s message tamper accepted\n", vector->name);
    free(message_bad);
    return false;
  }
  free(message_bad);
  return true;
}

static bool ed25519_vector_check(const Ed25519Vector* vector) {
  Ed25519VectorData data = {0};
  if (!ed25519_vector_parse(vector, &data)) {
    ed25519_vector_data_clear(&data);
    return false;
  }

  unsigned char public_key[32];
  unsigned char private_key[64];
  unsigned char signature[64];
  bool ok = ed25519_keypair_check(vector, &data, public_key, private_key) &&
            ed25519_signature_check(vector, &data, public_key, private_key, signature) &&
            ed25519_tamper_check(vector, &data, public_key, signature);
  ed25519_vector_data_clear(&data);
  return ok;
}

static bool ed25519_tests_run(void) {
  static const Ed25519Vector vectors[] = {{"test1", "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
                                           "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
                                           "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a"
                                           "33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
                                          {"test2", "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
                                           "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
                                           "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e1"
                                           "5996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
                                          {"test3", "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
                                           "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
                                           "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d1"
                                           "6f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"}};

  bool ok = true;
  for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
    ok = ok && ed25519_vector_check(&vectors[i]);
  }
  return ok;
}

static int hash_cli(const char* message_hex) {
  unsigned char* message = NULL;
  size_t message_len = 0;
  if (!hex_bytes_parse_dynamic(message_hex, &message, &message_len)) return 1;
  unsigned char digest[32];
  sha256_hash(message ? message : (const unsigned char*)"", message_len, digest);
  char out_hex[65];
  hex_bytes_write(digest, sizeof(digest), out_hex);
  printf("%s\n", out_hex);
  free(message);
  return 0;
}

static int sign_cli(const char* seed_hex, const char* message_hex) {
  unsigned char seed[32];
  if (!hex_bytes_parse(seed_hex, sizeof(seed), seed)) return 1;
  unsigned char* message = NULL;
  size_t message_len = 0;
  if (!hex_bytes_parse_dynamic(message_hex, &message, &message_len)) return 1;

  unsigned char public_key[32];
  unsigned char private_key[64];
  if (!ed25519_keypair_from_seed(public_key, private_key, seed)) {
    free(message);
    return 1;
  }
  unsigned char signature[64];
  if (!ed25519_sign(signature, message, message_len, public_key, private_key)) {
    free(message);
    return 1;
  }
  char out_hex[129];
  hex_bytes_write(signature, sizeof(signature), out_hex);
  printf("%s\n", out_hex);
  free(message);
  return 0;
}

static int verify_cli(const char* public_hex, const char* message_hex, const char* signature_hex) {
  unsigned char public_key[32];
  unsigned char signature[64];
  if (!hex_bytes_parse(public_hex, sizeof(public_key), public_key)) return 1;
  if (!hex_bytes_parse(signature_hex, sizeof(signature), signature)) return 1;

  unsigned char* message = NULL;
  size_t message_len = 0;
  if (!hex_bytes_parse_dynamic(message_hex, &message, &message_len)) return 1;

  bool ok = ed25519_verify(signature, message, message_len, public_key);
  free(message);
  if (!ok) return 1;
  printf("ok\n");
  return 0;
}

int main(int argc, char** argv) {
  if (argc == 1) {
    if (!sha256_tests_run()) return 1;
    if (!ed25519_tests_run()) return 1;
    printf("ok\n");
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "hash") == 0) {
    return hash_cli(argv[2]);
  }
  if (argc == 4 && strcmp(argv[1], "sign") == 0) {
    return sign_cli(argv[2], argv[3]);
  }
  if (argc == 5 && strcmp(argv[1], "verify") == 0) {
    return verify_cli(argv[2], argv[3], argv[4]);
  }
  fprintf(stderr,
          "usage: crypto_test [hash <hex>] [sign <seed_hex> <msg_hex>] [verify <pub_hex> <msg_hex> <sig_hex>]\n");
  return 1;
}
