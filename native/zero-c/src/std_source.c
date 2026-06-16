#include "std_source.h"

#include "embedded_stdlib_graph.inc"
#include "program_graph_format.h"
#include "program_graph_store_binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *public_name;
  const char *target_name;
  const char *module;
} ZStdSourceCall;

static const ZStdSourceModule std_source_modules[] = {
  {"std.args", "std/args.0", zero_embedded_stdlib_graph_std_args_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_args_graph_bytes)},
  {"std.ascii", "std/ascii.0", zero_embedded_stdlib_graph_std_ascii_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_ascii_graph_bytes)},
  {"std.cli", "std/cli.0", zero_embedded_stdlib_graph_std_cli_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_cli_graph_bytes)},
  {"std.codec", "std/codec.0", zero_embedded_stdlib_graph_std_codec_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_codec_graph_bytes)},
  {"std.collections", "std/collections.0", zero_embedded_stdlib_graph_std_collections_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_collections_graph_bytes)},
  {"std.crypto", "std/crypto.0", zero_embedded_stdlib_graph_std_crypto_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_crypto_graph_bytes)},
  {"std.csv", "std/csv.0", zero_embedded_stdlib_graph_std_csv_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_csv_graph_bytes)},
  {"std.env", "std/env.0", zero_embedded_stdlib_graph_std_env_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_env_graph_bytes)},
  {"std.fmt", "std/fmt.0", zero_embedded_stdlib_graph_std_fmt_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_fmt_graph_bytes)},
  {"std.fs", "std/fs.0", zero_embedded_stdlib_graph_std_fs_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_fs_graph_bytes)},
  {"std.http", "std/http.0", zero_embedded_stdlib_graph_std_http_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_http_graph_bytes)},
  {"std.inet", "std/inet.0", zero_embedded_stdlib_graph_std_inet_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_inet_graph_bytes)},
  {"std.io", "std/io.0", zero_embedded_stdlib_graph_std_io_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_io_graph_bytes)},
  {"std.json", "std/json.0", zero_embedded_stdlib_graph_std_json_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_json_graph_bytes)},
  {"std.log", "std/log.0", zero_embedded_stdlib_graph_std_log_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_log_graph_bytes)},
  {"std.math", "std/math.0", zero_embedded_stdlib_graph_std_math_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_math_graph_bytes)},
  {"std.mem", "std/mem.0", zero_embedded_stdlib_graph_std_mem_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_mem_graph_bytes)},
  {"std.net", "std/net.0", zero_embedded_stdlib_graph_std_net_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_net_graph_bytes)},
  {"std.parse", "std/parse.0", zero_embedded_stdlib_graph_std_parse_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_parse_graph_bytes)},
  {"std.path", "std/path.0", zero_embedded_stdlib_graph_std_path_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_path_graph_bytes)},
  {"std.proc", "std/proc.0", zero_embedded_stdlib_graph_std_proc_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_proc_graph_bytes)},
  {"std.rand", "std/rand.0", zero_embedded_stdlib_graph_std_rand_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_rand_graph_bytes)},
  {"std.regex", "std/regex.0", zero_embedded_stdlib_graph_std_regex_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_regex_graph_bytes)},
  {"std.search", "std/search.0", zero_embedded_stdlib_graph_std_search_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_search_graph_bytes)},
  {"std.sort", "std/sort.0", zero_embedded_stdlib_graph_std_sort_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_sort_graph_bytes)},
  {"std.str", "std/str.0", zero_embedded_stdlib_graph_std_str_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_str_graph_bytes)},
  {"std.testing", "std/testing.0", zero_embedded_stdlib_graph_std_testing_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_testing_graph_bytes)},
  {"std.text", "std/text.0", zero_embedded_stdlib_graph_std_text_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_text_graph_bytes)},
  {"std.time", "std/time.0", zero_embedded_stdlib_graph_std_time_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_time_graph_bytes)},
  {"std.toml", "std/toml.0", zero_embedded_stdlib_graph_std_toml_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_toml_graph_bytes)},
  {"std.unicode", "std/unicode.0", zero_embedded_stdlib_graph_std_unicode_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_unicode_graph_bytes)},
  {"std.url", "std/url.0", zero_embedded_stdlib_graph_std_url_graph_bytes, sizeof(zero_embedded_stdlib_graph_std_url_graph_bytes)},
};

typedef struct {
  const ZStdSourceModule *module;
  bool present;
  ZProgramGraph graph;
} ZStdSourceGraphCacheEntry;

static ZStdSourceGraphCacheEntry std_source_graph_cache[sizeof(std_source_modules) / sizeof(std_source_modules[0])];

static ZStdSourceGraphCacheEntry *std_source_graph_cache_entry(const ZStdSourceModule *module) {
  for (size_t i = 0; module && i < sizeof(std_source_graph_cache) / sizeof(std_source_graph_cache[0]); i++) {
    if (!std_source_graph_cache[i].module || std_source_graph_cache[i].module == module) {
      std_source_graph_cache[i].module = module;
      return &std_source_graph_cache[i];
    }
  }
  return NULL;
}

static const ZStdSourceCall std_source_calls[] = {
  {"std.codec.base64EncodedLen", "__zero_std_codec_base64_encoded_len", "std.codec"},
  {"std.codec.base64Encode", "__zero_std_codec_base64_encode", "std.codec"},
  {"std.codec.base64Decode", "__zero_std_codec_base64_decode", "std.codec"},
  {"std.codec.base64DecodedLen", "__zero_std_codec_base64_decoded_len", "std.codec"},
  {"std.codec.base64RawEncodedLen", "__zero_std_codec_base64_raw_encoded_len", "std.codec"},
  {"std.codec.base64RawEncode", "__zero_std_codec_base64_raw_encode", "std.codec"},
  {"std.codec.base64RawDecodedLen", "__zero_std_codec_base64_raw_decoded_len", "std.codec"},
  {"std.codec.base64RawDecode", "__zero_std_codec_base64_raw_decode", "std.codec"},
  {"std.codec.base64UrlEncodedLen", "__zero_std_codec_base64_url_encoded_len", "std.codec"},
  {"std.codec.base64UrlEncode", "__zero_std_codec_base64_url_encode", "std.codec"},
  {"std.codec.base64UrlDecodedLen", "__zero_std_codec_base64_url_decoded_len", "std.codec"},
  {"std.codec.base64UrlDecode", "__zero_std_codec_base64_url_decode", "std.codec"},
  {"std.codec.base32EncodedLen", "__zero_std_codec_base32_encoded_len", "std.codec"},
  {"std.codec.base32Encode", "__zero_std_codec_base32_encode", "std.codec"},
  {"std.codec.base32DecodedLen", "__zero_std_codec_base32_decoded_len", "std.codec"},
  {"std.codec.base32Decode", "__zero_std_codec_base32_decode", "std.codec"},
  {"std.codec.base32RawEncodedLen", "__zero_std_codec_base32_raw_encoded_len", "std.codec"},
  {"std.codec.base32RawEncode", "__zero_std_codec_base32_raw_encode", "std.codec"},
  {"std.codec.base32RawDecodedLen", "__zero_std_codec_base32_raw_decoded_len", "std.codec"},
  {"std.codec.base32RawDecode", "__zero_std_codec_base32_raw_decode", "std.codec"},
  {"std.codec.encodedVarintLen", "__zero_std_codec_encoded_varint_len", "std.codec"},
  {"std.codec.encodedVarintLen64", "__zero_std_codec_encoded_varint_len64", "std.codec"},
  {"std.codec.encodedSignedVarintLen", "__zero_std_codec_encoded_signed_varint_len", "std.codec"},
  {"std.codec.encodedSignedVarintLen64", "__zero_std_codec_encoded_signed_varint_len64", "std.codec"},
  {"std.codec.hexEncode", "__zero_std_codec_hex_encode", "std.codec"},
  {"std.codec.hexDecode", "__zero_std_codec_hex_decode", "std.codec"},
  {"std.codec.hexDecodedLen", "__zero_std_codec_hex_decoded_len", "std.codec"},
  {"std.codec.readU16Be", "__zero_std_codec_read_u16_be", "std.codec"},
  {"std.codec.readU16Le", "__zero_std_codec_read_u16_le", "std.codec"},
  {"std.codec.readU32Be", "__zero_std_codec_read_u32_be", "std.codec"},
  {"std.codec.readU32Le", "__zero_std_codec_read_u32_le", "std.codec"},
  {"std.codec.readU64Be", "__zero_std_codec_read_u64_be", "std.codec"},
  {"std.codec.readU64Le", "__zero_std_codec_read_u64_le", "std.codec"},
  {"std.codec.varintDecode", "__zero_std_codec_varint_decode", "std.codec"},
  {"std.codec.varintEncode", "__zero_std_codec_varint_encode", "std.codec"},
  {"std.codec.varintDecode64", "__zero_std_codec_varint_decode64", "std.codec"},
  {"std.codec.varintEncode64", "__zero_std_codec_varint_encode64", "std.codec"},
  {"std.codec.signedVarintDecode", "__zero_std_codec_signed_varint_decode", "std.codec"},
  {"std.codec.signedVarintEncode", "__zero_std_codec_signed_varint_encode", "std.codec"},
  {"std.codec.signedVarintDecode64", "__zero_std_codec_signed_varint_decode64", "std.codec"},
  {"std.codec.signedVarintEncode64", "__zero_std_codec_signed_varint_encode64", "std.codec"},
  {"std.codec.writeU16Be", "__zero_std_codec_write_u16_be", "std.codec"},
  {"std.codec.writeU16Le", "__zero_std_codec_write_u16_le", "std.codec"},
  {"std.codec.writeU32Be", "__zero_std_codec_write_u32_be", "std.codec"},
  {"std.codec.writeU32Le", "__zero_std_codec_write_u32_le", "std.codec"},
  {"std.codec.writeU64Be", "__zero_std_codec_write_u64_be", "std.codec"},
  {"std.codec.writeU64Le", "__zero_std_codec_write_u64_le", "std.codec"},
  {"std.codec.urlEncode", "__zero_std_codec_url_encode", "std.codec"},
  {"std.csv.encodedFieldLen", "csv_encoded_field_len", "std.csv"},
  {"std.csv.field", "csv_field", "std.csv"},
  {"std.csv.fieldCount", "csv_field_count", "std.csv"},
  {"std.csv.record", "csv_record", "std.csv"},
  {"std.csv.recordCount", "csv_record_count", "std.csv"},
  {"std.csv.valid", "csv_valid", "std.csv"},
  {"std.csv.writeField", "csv_write_field", "std.csv"},
  {"std.csv.writeRecord2", "csv_write_record2", "std.csv"},
  {"std.csv.writeRecord3", "csv_write_record3", "std.csv"},
  {"std.cli.argOr", "__zero_std_cli_arg_or", "std.cli"},
  {"std.cli.argU32Or", "__zero_std_cli_arg_u32_or", "std.cli"},
  {"std.cli.command", "__zero_std_cli_command", "std.cli"},
  {"std.cli.commandEquals", "__zero_std_cli_command_equals", "std.cli"},
  {"std.cli.commandOr", "__zero_std_cli_command_or", "std.cli"},
  {"std.cli.optionBool", "__zero_std_cli_option_bool", "std.cli"},
  {"std.cli.optionBoolOr", "__zero_std_cli_option_bool_or", "std.cli"},
  {"std.cli.optionI32", "__zero_std_cli_option_i32", "std.cli"},
  {"std.cli.optionI32Or", "__zero_std_cli_option_i32_or", "std.cli"},
  {"std.cli.optionU32Or", "__zero_std_cli_option_u32_or", "std.cli"},
  {"std.cli.optionUsize", "__zero_std_cli_option_usize", "std.cli"},
  {"std.cli.optionUsizeOr", "__zero_std_cli_option_usize_or", "std.cli"},
  {"std.collections.append", "__zero_std_collections_append", "std.collections"},
  {"std.collections.clear", "__zero_std_collections_clear", "std.collections"},
  {"std.collections.contains", "__zero_std_collections_contains", "std.collections"},
  {"std.collections.count", "__zero_std_collections_count", "std.collections"},
  {"std.collections.dequeBack", "__zero_std_collections_deque_back", "std.collections"},
  {"std.collections.dequeFront", "__zero_std_collections_deque_front", "std.collections"},
  {"std.collections.dequePopBack", "__zero_std_collections_deque_pop_back", "std.collections"},
  {"std.collections.dequePopFront", "__zero_std_collections_deque_pop_front", "std.collections"},
  {"std.collections.dequePushBack", "__zero_std_collections_deque_push_back", "std.collections"},
  {"std.collections.dequePushFront", "__zero_std_collections_deque_push_front", "std.collections"},
  {"std.collections.fill", "__zero_std_collections_fill", "std.collections"},
  {"std.collections.first", "__zero_std_collections_first", "std.collections"},
  {"std.collections.fixedDeque", "__zero_std_collections_fixed_deque", "std.collections"},
  {"std.collections.fixedDequeBack", "__zero_std_collections_fixed_deque_back", "std.collections"},
  {"std.collections.fixedDequeClear", "__zero_std_collections_fixed_deque_clear", "std.collections"},
  {"std.collections.fixedDequeFront", "__zero_std_collections_fixed_deque_front", "std.collections"},
  {"std.collections.fixedDequeIsFull", "__zero_std_collections_fixed_deque_is_full", "std.collections"},
  {"std.collections.fixedDequeLen", "__zero_std_collections_fixed_deque_len", "std.collections"},
  {"std.collections.fixedDequePopBack", "__zero_std_collections_fixed_deque_pop_back", "std.collections"},
  {"std.collections.fixedDequePopFront", "__zero_std_collections_fixed_deque_pop_front", "std.collections"},
  {"std.collections.fixedDequePushBack", "__zero_std_collections_fixed_deque_push_back", "std.collections"},
  {"std.collections.fixedDequePushFront", "__zero_std_collections_fixed_deque_push_front", "std.collections"},
  {"std.collections.fixedDequeRemaining", "__zero_std_collections_fixed_deque_remaining", "std.collections"},
  {"std.collections.fixedDequeTruncate", "__zero_std_collections_fixed_deque_truncate", "std.collections"},
  {"std.collections.fixedDequeView", "__zero_std_collections_fixed_deque_view", "std.collections"},
  {"std.collections.fixedRingBuffer", "__zero_std_collections_fixed_ring_buffer", "std.collections"},
  {"std.collections.fixedRingBufferBack", "__zero_std_collections_fixed_ring_buffer_back", "std.collections"},
  {"std.collections.fixedRingBufferCapacity", "__zero_std_collections_fixed_ring_buffer_capacity", "std.collections"},
  {"std.collections.fixedRingBufferClear", "__zero_std_collections_fixed_ring_buffer_clear", "std.collections"},
  {"std.collections.fixedRingBufferFront", "__zero_std_collections_fixed_ring_buffer_front", "std.collections"},
  {"std.collections.fixedRingBufferGet", "__zero_std_collections_fixed_ring_buffer_get", "std.collections"},
  {"std.collections.fixedRingBufferIsFull", "__zero_std_collections_fixed_ring_buffer_is_full", "std.collections"},
  {"std.collections.fixedRingBufferLen", "__zero_std_collections_fixed_ring_buffer_len", "std.collections"},
  {"std.collections.fixedRingBufferPopBack", "__zero_std_collections_fixed_ring_buffer_pop_back", "std.collections"},
  {"std.collections.fixedRingBufferPopFront", "__zero_std_collections_fixed_ring_buffer_pop_front", "std.collections"},
  {"std.collections.fixedRingBufferPushBack", "__zero_std_collections_fixed_ring_buffer_push_back", "std.collections"},
  {"std.collections.fixedRingBufferPushFront", "__zero_std_collections_fixed_ring_buffer_push_front", "std.collections"},
  {"std.collections.fixedRingBufferRemaining", "__zero_std_collections_fixed_ring_buffer_remaining", "std.collections"},
  {"std.collections.fixedRingBufferTruncate", "__zero_std_collections_fixed_ring_buffer_truncate", "std.collections"},
  {"std.collections.fixedMap", "__zero_std_collections_fixed_map", "std.collections"},
  {"std.collections.fixedMapClear", "__zero_std_collections_fixed_map_clear", "std.collections"},
  {"std.collections.fixedMapContains", "__zero_std_collections_fixed_map_contains", "std.collections"},
  {"std.collections.fixedMapGet", "__zero_std_collections_fixed_map_get", "std.collections"},
  {"std.collections.fixedMapIndex", "__zero_std_collections_fixed_map_index", "std.collections"},
  {"std.collections.fixedMapIsFull", "__zero_std_collections_fixed_map_is_full", "std.collections"},
  {"std.collections.fixedMapKeys", "__zero_std_collections_fixed_map_keys", "std.collections"},
  {"std.collections.fixedMapLen", "__zero_std_collections_fixed_map_len", "std.collections"},
  {"std.collections.fixedMapPut", "__zero_std_collections_fixed_map_put", "std.collections"},
  {"std.collections.fixedMapRemaining", "__zero_std_collections_fixed_map_remaining", "std.collections"},
  {"std.collections.fixedMapRemove", "__zero_std_collections_fixed_map_remove", "std.collections"},
  {"std.collections.fixedMapTruncate", "__zero_std_collections_fixed_map_truncate", "std.collections"},
  {"std.collections.fixedMapValues", "__zero_std_collections_fixed_map_values", "std.collections"},
  {"std.collections.fixedSet", "__zero_std_collections_fixed_set", "std.collections"},
  {"std.collections.fixedSetClear", "__zero_std_collections_fixed_set_clear", "std.collections"},
  {"std.collections.fixedSetContains", "__zero_std_collections_fixed_set_contains", "std.collections"},
  {"std.collections.fixedSetInsert", "__zero_std_collections_fixed_set_insert", "std.collections"},
  {"std.collections.fixedSetIsFull", "__zero_std_collections_fixed_set_is_full", "std.collections"},
  {"std.collections.fixedSetLen", "__zero_std_collections_fixed_set_len", "std.collections"},
  {"std.collections.fixedSetRemaining", "__zero_std_collections_fixed_set_remaining", "std.collections"},
  {"std.collections.fixedSetRemove", "__zero_std_collections_fixed_set_remove", "std.collections"},
  {"std.collections.fixedSetTruncate", "__zero_std_collections_fixed_set_truncate", "std.collections"},
  {"std.collections.fixedSetView", "__zero_std_collections_fixed_set_view", "std.collections"},
  {"std.collections.insertAt", "__zero_std_collections_insert_at", "std.collections"},
  {"std.collections.insertUnique", "__zero_std_collections_insert_unique", "std.collections"},
  {"std.collections.isFull", "__zero_std_collections_is_full", "std.collections"},
  {"std.collections.last", "__zero_std_collections_last", "std.collections"},
  {"std.collections.mapClear", "__zero_std_collections_map_clear", "std.collections"},
  {"std.collections.mapContains", "__zero_std_collections_map_contains", "std.collections"},
  {"std.collections.mapGet", "__zero_std_collections_map_get", "std.collections"},
  {"std.collections.mapIndex", "__zero_std_collections_map_index", "std.collections"},
  {"std.collections.mapIsFull", "__zero_std_collections_map_is_full", "std.collections"},
  {"std.collections.mapKeys", "__zero_std_collections_map_keys", "std.collections"},
  {"std.collections.mapPut", "__zero_std_collections_map_put", "std.collections"},
  {"std.collections.mapRemaining", "__zero_std_collections_map_remaining", "std.collections"},
  {"std.collections.mapRemove", "__zero_std_collections_map_remove", "std.collections"},
  {"std.collections.mapTruncate", "__zero_std_collections_map_truncate", "std.collections"},
  {"std.collections.mapValues", "__zero_std_collections_map_values", "std.collections"},
  {"std.collections.moveToFront", "__zero_std_collections_move_to_front", "std.collections"},
  {"std.collections.pop", "__zero_std_collections_pop", "std.collections"},
  {"std.collections.push", "__zero_std_collections_push", "std.collections"},
  {"std.collections.remaining", "__zero_std_collections_remaining", "std.collections"},
  {"std.collections.replaceAt", "__zero_std_collections_replace_at", "std.collections"},
  {"std.collections.removeAt", "__zero_std_collections_remove_at", "std.collections"},
  {"std.collections.removeValue", "__zero_std_collections_remove_value", "std.collections"},
  {"std.collections.removeSwap", "__zero_std_collections_remove_swap", "std.collections"},
  {"std.collections.reverse", "__zero_std_collections_reverse", "std.collections"},
  {"std.collections.rotateLeft", "__zero_std_collections_rotate_left", "std.collections"},
  {"std.collections.rotateRight", "__zero_std_collections_rotate_right", "std.collections"},
  {"std.collections.setClear", "__zero_std_collections_set_clear", "std.collections"},
  {"std.collections.setContains", "__zero_std_collections_set_contains", "std.collections"},
  {"std.collections.setInsert", "__zero_std_collections_set_insert", "std.collections"},
  {"std.collections.setIsFull", "__zero_std_collections_set_is_full", "std.collections"},
  {"std.collections.setRemaining", "__zero_std_collections_set_remaining", "std.collections"},
  {"std.collections.setRemove", "__zero_std_collections_set_remove", "std.collections"},
  {"std.collections.setTruncate", "__zero_std_collections_set_truncate", "std.collections"},
  {"std.collections.setView", "__zero_std_collections_set_view", "std.collections"},
  {"std.collections.swapAt", "__zero_std_collections_swap_at", "std.collections"},
  {"std.collections.truncate", "__zero_std_collections_truncate", "std.collections"},
  {"std.collections.view", "__zero_std_collections_view", "std.collections"},
  {"std.crypto.fixedHex32", "crypto_fixed_hex32", "std.crypto"},
  {"std.crypto.hashHex32", "crypto_hash_hex32", "std.crypto"},
  {"std.crypto.hmacHex32", "crypto_hmac_hex32", "std.crypto"},
  {"std.crypto.randomId32", "crypto_random_id32", "std.crypto"},
  {"std.crypto.stableId32", "crypto_stable_id32", "std.crypto"},
  {"std.env.equals", "__zero_std_env_equals", "std.env"},
  {"std.env.getOr", "__zero_std_env_get_or", "std.env"},
  {"std.env.has", "__zero_std_env_has", "std.env"},
  {"std.env.parseBool", "__zero_std_env_parse_bool", "std.env"},
  {"std.env.parseBoolOr", "__zero_std_env_parse_bool_or", "std.env"},
  {"std.env.parseI32", "__zero_std_env_parse_i32", "std.env"},
  {"std.env.parseI32Or", "__zero_std_env_parse_i32_or", "std.env"},
  {"std.env.parseU32", "__zero_std_env_parse_u32", "std.env"},
  {"std.env.parseU32Or", "__zero_std_env_parse_u32_or", "std.env"},
  {"std.env.parseUsize", "__zero_std_env_parse_usize", "std.env"},
  {"std.env.parseUsizeOr", "__zero_std_env_parse_usize_or", "std.env"},
  {"std.fs.copyFile", "__zero_std_fs_copy_file", "std.fs"},
  {"std.fs.ensureDir", "__zero_std_fs_ensure_dir", "std.fs"},
  {"std.fs.fileSize", "__zero_std_fs_file_size", "std.fs"},
  {"std.fs.isFile", "__zero_std_fs_is_file", "std.fs"},
  {"std.fs.readFileBytes", "__zero_std_fs_read_file_bytes", "std.fs"},
  {"std.fs.readFileEquals", "__zero_std_fs_read_file_equals", "std.fs"},
  {"std.fs.readFile", "__zero_std_fs_read_file", "std.fs"},
  {"std.fs.writeFile", "__zero_std_fs_write_file", "std.fs"},
  {"std.fmt.i64", "__zero_std_fmt_i64", "std.fmt"},
  {"std.fmt.i64Base", "__zero_std_fmt_i64_base", "std.fmt"},
  {"std.fmt.i64Sign", "__zero_std_fmt_i64_sign", "std.fmt"},
  {"std.fmt.i32Base", "__zero_std_fmt_i32_base", "std.fmt"},
  {"std.fmt.i32Sign", "__zero_std_fmt_i32_sign", "std.fmt"},
  {"std.fmt.padLeft", "__zero_std_fmt_pad_left", "std.fmt"},
  {"std.fmt.padRight", "__zero_std_fmt_pad_right", "std.fmt"},
  {"std.fmt.u64", "__zero_std_fmt_u64", "std.fmt"},
  {"std.fmt.u64Base", "__zero_std_fmt_u64_base", "std.fmt"},
  {"std.fmt.u32Base", "__zero_std_fmt_u32_base", "std.fmt"},
  {"std.fmt.usizeBase", "__zero_std_fmt_usize_base", "std.fmt"},
  {"std.fmt.writeBool", "__zero_std_fmt_write_bool", "std.fmt"},
  {"std.fmt.writeI32", "__zero_std_fmt_write_i32", "std.fmt"},
  {"std.fmt.writeI32Sign", "__zero_std_fmt_write_i32_sign", "std.fmt"},
  {"std.fmt.writeI64", "__zero_std_fmt_write_i64", "std.fmt"},
  {"std.fmt.writeI64Sign", "__zero_std_fmt_write_i64_sign", "std.fmt"},
  {"std.fmt.writeSpan", "__zero_std_fmt_write_span", "std.fmt"},
  {"std.fmt.writeU32", "__zero_std_fmt_write_u32", "std.fmt"},
  {"std.fmt.writeU64", "__zero_std_fmt_write_u64", "std.fmt"},
  {"std.fmt.writeUsize", "__zero_std_fmt_write_usize", "std.fmt"},
  {"std.http.headerBytes", "__zero_std_http_header_bytes", "std.http"},
  {"std.http.pathSegment", "__zero_std_http_path_segment", "std.http"},
  {"std.http.pathSegmentCount", "__zero_std_http_path_segment_count", "std.http"},
  {"std.http.requestBody", "__zero_std_http_request_body", "std.http"},
  {"std.http.requestBodyWithin", "__zero_std_http_request_body_within", "std.http"},
  {"std.http.requestBearerToken", "__zero_std_http_request_bearer_token", "std.http"},
  {"std.http.requestCookie", "__zero_std_http_request_cookie", "std.http"},
  {"std.http.requestHeader", "__zero_std_http_request_header", "std.http"},
  {"std.http.requestHasJsonContentType", "__zero_std_http_request_has_json_content_type", "std.http"},
  {"std.http.requestJsonBodyWithin", "__zero_std_http_request_json_body_within", "std.http"},
  {"std.http.requestMatches", "__zero_std_http_request_matches", "std.http"},
  {"std.http.requestMethodIs", "__zero_std_http_request_method_is", "std.http"},
  {"std.http.requestMethodName", "__zero_std_http_request_method_name", "std.http"},
  {"std.http.requestPath", "__zero_std_http_request_path", "std.http"},
  {"std.http.requestQuery", "__zero_std_http_request_query", "std.http"},
  {"std.http.requestQueryValue", "__zero_std_http_request_query_value", "std.http"},
  {"std.http.requestIsDelete", "__zero_std_http_request_is_delete", "std.http"},
  {"std.http.requestIsGet", "__zero_std_http_request_is_get", "std.http"},
  {"std.http.requestIsHead", "__zero_std_http_request_is_head", "std.http"},
  {"std.http.requestIsOptions", "__zero_std_http_request_is_options", "std.http"},
  {"std.http.requestIsPatch", "__zero_std_http_request_is_patch", "std.http"},
  {"std.http.requestIsPost", "__zero_std_http_request_is_post", "std.http"},
  {"std.http.requestIsPut", "__zero_std_http_request_is_put", "std.http"},
  {"std.http.requestPathSegment", "__zero_std_http_request_path_segment", "std.http"},
  {"std.http.requestPathSegmentCount", "__zero_std_http_request_path_segment_count", "std.http"},
  {"std.http.requestPathStartsWith", "__zero_std_http_request_path_starts_with", "std.http"},
  {"std.http.requestPathTailAfter", "__zero_std_http_request_path_tail_after", "std.http"},
  {"std.http.requestTarget", "__zero_std_http_request_target", "std.http"},
  {"std.http.responseBody", "__zero_std_http_response_body", "std.http"},
  {"std.http.responseBodyBytes", "__zero_std_http_response_body_bytes", "std.http"},
  {"std.http.statusReason", "__zero_std_http_status_reason", "std.http"},
  {"std.http.writeCorsJsonResponse", "__zero_std_http_write_cors_json_response", "std.http"},
  {"std.http.writeCorsPreflight", "__zero_std_http_write_cors_preflight", "std.http"},
  {"std.http.writeFound", "__zero_std_http_write_found", "std.http"},
  {"std.http.writeHtmlOk", "__zero_std_http_write_html_ok", "std.http"},
  {"std.http.writeHtmlResponse", "__zero_std_http_write_html_response", "std.http"},
  {"std.http.writeJsonBadRequest", "__zero_std_http_write_json_bad_request", "std.http"},
  {"std.http.writeJsonConflict", "__zero_std_http_write_json_conflict", "std.http"},
  {"std.http.writeJsonCreated", "__zero_std_http_write_json_created", "std.http"},
  {"std.http.writeJsonError", "__zero_std_http_write_json_error", "std.http"},
  {"std.http.writeJsonForbidden", "__zero_std_http_write_json_forbidden", "std.http"},
  {"std.http.writeJsonInternalServerError", "__zero_std_http_write_json_internal_server_error", "std.http"},
  {"std.http.writeJsonMethodNotAllowed", "__zero_std_http_write_json_method_not_allowed", "std.http"},
  {"std.http.writeJsonNotFound", "__zero_std_http_write_json_not_found", "std.http"},
  {"std.http.writeJsonOk", "__zero_std_http_write_json_ok", "std.http"},
  {"std.http.writeJsonRequest", "__zero_std_http_write_json_request", "std.http"},
  {"std.http.writeJsonTooManyRequests", "__zero_std_http_write_json_too_many_requests", "std.http"},
  {"std.http.writeJsonUnauthorized", "__zero_std_http_write_json_unauthorized", "std.http"},
  {"std.http.writeJsonUnprocessable", "__zero_std_http_write_json_unprocessable", "std.http"},
  {"std.http.writeMovedPermanently", "__zero_std_http_write_moved_permanently", "std.http"},
  {"std.http.writeNoContent", "__zero_std_http_write_no_content", "std.http"},
  {"std.http.writePermanentRedirect", "__zero_std_http_write_permanent_redirect", "std.http"},
  {"std.http.writeRedirect", "__zero_std_http_write_redirect", "std.http"},
  {"std.http.writeRequest", "__zero_std_http_write_request", "std.http"},
  {"std.http.writeResponse", "__zero_std_http_write_response", "std.http"},
  {"std.http.writeSeeOther", "__zero_std_http_write_see_other", "std.http"},
  {"std.http.writeTextOk", "__zero_std_http_write_text_ok", "std.http"},
  {"std.http.writeTextResponse", "__zero_std_http_write_text_response", "std.http"},
  {"std.io.copy", "__zero_std_io_copy", "std.io"},
  {"std.io.copyBuffer", "__zero_std_io_copy_buffer", "std.io"},
  {"std.io.copyN", "__zero_std_io_copy_n", "std.io"},
  {"std.io.copyReaderN", "__zero_std_io_copy_reader_n", "std.io"},
  {"std.io.countLines", "__zero_std_io_count_lines", "std.io"},
  {"std.io.discard", "__zero_std_io_discard", "std.io"},
  {"std.io.errorCapacity", "__zero_std_io_error_capacity", "std.io"},
  {"std.io.errorEof", "__zero_std_io_error_eof", "std.io"},
  {"std.io.errorIo", "__zero_std_io_error_io", "std.io"},
  {"std.io.errorName", "__zero_std_io_error_name", "std.io"},
  {"std.io.errorNone", "__zero_std_io_error_none", "std.io"},
  {"std.io.errorPermission", "__zero_std_io_error_permission", "std.io"},
  {"std.io.errorShortRead", "__zero_std_io_error_short_read", "std.io"},
  {"std.io.errorShortWrite", "__zero_std_io_error_short_write", "std.io"},
  {"std.io.errorTimeout", "__zero_std_io_error_timeout", "std.io"},
  {"std.io.fixedReader", "__zero_std_io_fixed_reader", "std.io"},
  {"std.io.fixedReaderCursor", "__zero_std_io_fixed_reader_cursor", "std.io"},
  {"std.io.fixedReaderDone", "__zero_std_io_fixed_reader_done", "std.io"},
  {"std.io.fixedReaderLen", "__zero_std_io_fixed_reader_len", "std.io"},
  {"std.io.fixedReaderLimit", "__zero_std_io_fixed_reader_limit", "std.io"},
  {"std.io.fixedReaderRead", "__zero_std_io_fixed_reader_read", "std.io"},
  {"std.io.fixedReaderReadAll", "__zero_std_io_fixed_reader_read_all", "std.io"},
  {"std.io.fixedReaderReadAt", "__zero_std_io_fixed_reader_read_at", "std.io"},
  {"std.io.fixedReaderReadByte", "__zero_std_io_fixed_reader_read_byte", "std.io"},
  {"std.io.fixedReaderReadExact", "__zero_std_io_fixed_reader_read_exact", "std.io"},
  {"std.io.fixedReaderReadLine", "__zero_std_io_fixed_reader_read_line", "std.io"},
  {"std.io.fixedReaderReadUntilDelimiter", "__zero_std_io_fixed_reader_read_until_delimiter", "std.io"},
  {"std.io.fixedReaderRemaining", "__zero_std_io_fixed_reader_remaining", "std.io"},
  {"std.io.fixedReaderSeek", "__zero_std_io_fixed_reader_seek", "std.io"},
  {"std.io.fixedWriter", "__zero_std_io_fixed_writer", "std.io"},
  {"std.io.fixedWriterCapacity", "__zero_std_io_fixed_writer_capacity", "std.io"},
  {"std.io.fixedWriterClear", "__zero_std_io_fixed_writer_clear", "std.io"},
  {"std.io.fixedWriterCursor", "__zero_std_io_fixed_writer_cursor", "std.io"},
  {"std.io.fixedWriterRemaining", "__zero_std_io_fixed_writer_remaining", "std.io"},
  {"std.io.fixedWriterSeek", "__zero_std_io_fixed_writer_seek", "std.io"},
  {"std.io.fixedWriterTruncate", "__zero_std_io_fixed_writer_truncate", "std.io"},
  {"std.io.fixedWriterView", "__zero_std_io_fixed_writer_view", "std.io"},
  {"std.io.fixedWriterWrite", "__zero_std_io_fixed_writer_write", "std.io"},
  {"std.io.fixedWriterWriteAll", "__zero_std_io_fixed_writer_write_all", "std.io"},
  {"std.io.fixedWriterWriteAt", "__zero_std_io_fixed_writer_write_at", "std.io"},
  {"std.io.fixedWriterWriteByte", "__zero_std_io_fixed_writer_write_byte", "std.io"},
  {"std.io.multiRead", "__zero_std_io_multi_read", "std.io"},
  {"std.io.nextLine", "__zero_std_io_next_line", "std.io"},
  {"std.io.nextLineStart", "__zero_std_io_next_line_start", "std.io"},
  {"std.io.read", "__zero_std_io_read", "std.io"},
  {"std.io.readAll", "__zero_std_io_read_all", "std.io"},
  {"std.io.readAt", "__zero_std_io_read_at", "std.io"},
  {"std.io.readByte", "__zero_std_io_read_byte", "std.io"},
  {"std.io.readExact", "__zero_std_io_read_exact", "std.io"},
  {"std.io.readLine", "__zero_std_io_read_line", "std.io"},
  {"std.io.readLineStart", "__zero_std_io_read_line_start", "std.io"},
  {"std.io.readUntilDelimiter", "__zero_std_io_read_until_delimiter", "std.io"},
  {"std.io.readUntilDelimiterStart", "__zero_std_io_read_until_delimiter_start", "std.io"},
  {"std.io.remaining", "__zero_std_io_remaining", "std.io"},
  {"std.io.teeRead", "__zero_std_io_tee_read", "std.io"},
  {"std.io.writeAll", "__zero_std_io_write_all", "std.io"},
  {"std.io.writeAt", "__zero_std_io_write_at", "std.io"},
  {"std.io.writeByte", "__zero_std_io_write_byte", "std.io"},
  {"std.io.writeSpan", "__zero_std_io_write_span", "std.io"},
  {"std.io.written", "__zero_std_io_written", "std.io"},
  {"std.json.arrayCount", "__zero_std_json_array_count", "std.json"},
  {"std.json.arrayValue", "__zero_std_json_array_value", "std.json"},
  {"std.json.bool", "__zero_std_json_bool", "std.json"},
  {"std.json.field", "__zero_std_json_field", "std.json"},
  {"std.json.objectFieldCount", "__zero_std_json_object_field_count", "std.json"},
  {"std.json.objectKey", "__zero_std_json_object_key", "std.json"},
  {"std.json.objectValue", "__zero_std_json_object_value", "std.json"},
  {"std.json.path", "__zero_std_json_path", "std.json"},
  {"std.json.pathBool", "__zero_std_json_path_bool", "std.json"},
  {"std.json.pathString", "__zero_std_json_path_string", "std.json"},
  {"std.json.pathU32", "__zero_std_json_path_u32", "std.json"},
  {"std.json.string", "__zero_std_json_string", "std.json"},
  {"std.json.stringDecode", "__zero_std_json_string_decode", "std.json"},
  {"std.json.streamTokens", "__zero_std_json_stream_tokens", "std.json"},
  {"std.json.streamTokensBytes", "__zero_std_json_stream_tokens_bytes", "std.json"},
  {"std.json.u32", "__zero_std_json_u32", "std.json"},
  {"std.json.validate", "__zero_std_json_validate", "std.json"},
  {"std.json.validateBytes", "__zero_std_json_validate_bytes", "std.json"},
  {"std.json.validateError", "__zero_std_json_validate_error", "std.json"},
  {"std.json.writeObject1Bool", "__zero_std_json_write_object1_bool", "std.json"},
  {"std.json.writeObject1String", "__zero_std_json_write_object1_string", "std.json"},
  {"std.json.writeObject1U32", "__zero_std_json_write_object1_u32", "std.json"},
  {"std.json.writeFieldBool", "__zero_std_json_write_field_bool", "std.json"},
  {"std.json.writeFieldRaw", "__zero_std_json_write_field_raw", "std.json"},
  {"std.json.writeFieldString", "__zero_std_json_write_field_string", "std.json"},
  {"std.json.writeFieldU32", "__zero_std_json_write_field_u32", "std.json"},
  {"std.json.writeObject2Fields", "__zero_std_json_write_object2_fields", "std.json"},
  {"std.json.writeObject2StringField", "__zero_std_json_write_object2_string_field", "std.json"},
  {"std.json.writeObject2U32Field", "__zero_std_json_write_object2_u32_field", "std.json"},
  {"std.json.writeObject2BoolField", "__zero_std_json_write_object2_bool_field", "std.json"},
  {"std.json.writeArray2Strings", "__zero_std_json_write_array2_strings", "std.json"},
  {"std.json.writeArray2U32", "__zero_std_json_write_array2_u32", "std.json"},
  {"std.json.writeArray2Bools", "__zero_std_json_write_array2_bools", "std.json"},
  {"std.json.writeString", "__zero_std_json_write_string", "std.json"},
  {"std.json.writeStringBytes", "__zero_std_json_write_string_bytes", "std.json"},
  {"std.log.keyValue", "__zero_std_log_key_value", "std.log"},
  {"std.log.message", "__zero_std_log_message", "std.log"},
  {"std.mem.contains", "__zero_std_mem_contains", "std.mem"},
  {"std.mem.compareI32", "__zero_std_mem_compare_i32", "std.mem"},
  {"std.mem.compareU8", "__zero_std_mem_compare_u8", "std.mem"},
  {"std.mem.compareBytes", "__zero_std_mem_compare_bytes", "std.mem"},
  {"std.mem.compareU32", "__zero_std_mem_compare_u32", "std.mem"},
  {"std.mem.compareUsize", "__zero_std_mem_compare_usize", "std.mem"},
  {"std.mem.startsWith", "__zero_std_mem_starts_with", "std.mem"},
  {"std.mem.endsWith", "__zero_std_mem_ends_with", "std.mem"},
  {"std.mem.splitAfter", "__zero_std_mem_split_after", "std.mem"},
  {"std.mem.splitBefore", "__zero_std_mem_split_before", "std.mem"},
  {"std.mem.copyItems", "__zero_std_mem_copy_items", "std.mem"},
  {"std.mem.chunk", "__zero_std_mem_chunk", "std.mem"},
  {"std.mem.chunkCount", "__zero_std_mem_chunk_count", "std.mem"},
  {"std.mem.window", "__zero_std_mem_window", "std.mem"},
  {"std.mem.windowCount", "__zero_std_mem_window_count", "std.mem"},
  {"std.mem.advance", "__zero_std_mem_advance", "std.mem"},
  {"std.mem.cursorChunk", "__zero_std_mem_cursor_chunk", "std.mem"},
  {"std.mem.cursorDone", "__zero_std_mem_cursor_done", "std.mem"},
  {"std.mem.dropPrefix", "__zero_std_mem_drop_prefix", "std.mem"},
  {"std.mem.dropSuffix", "__zero_std_mem_drop_suffix", "std.mem"},
  {"std.mem.fillItems", "__zero_std_mem_fill_items", "std.mem"},
  {"std.mem.prefix", "__zero_std_mem_prefix", "std.mem"},
  {"std.mem.remaining", "__zero_std_mem_remaining", "std.mem"},
  {"std.mem.slice", "__zero_std_mem_slice", "std.mem"},
  {"std.mem.suffix", "__zero_std_mem_suffix", "std.mem"},
  {"std.net.localhost", "__zero_std_net_localhost", "std.net"},
  {"std.net.loopback", "__zero_std_net_loopback", "std.net"},
  {"std.inet.isIpv4", "__zero_std_inet_is_ipv4", "std.inet"},
  {"std.inet.parseIpv4", "__zero_std_inet_parse_ipv4", "std.inet"},
  {"std.inet.isIpv6", "__zero_std_inet_is_ipv6", "std.inet"},
  {"std.inet.parseIpv6", "__zero_std_inet_parse_ipv6", "std.inet"},
  {"std.inet.isHostname", "__zero_std_inet_is_hostname", "std.inet"},
  {"std.path.abs", "__zero_std_path_abs", "std.path"},
  {"std.path.component", "__zero_std_path_component", "std.path"},
  {"std.path.componentCount", "__zero_std_path_component_count", "std.path"},
  {"std.path.isAbs", "__zero_std_path_is_abs", "std.path"},
  {"std.path.join", "__zero_std_path_join", "std.path"},
  {"std.path.normalize", "__zero_std_path_normalize", "std.path"},
  {"std.path.relative", "__zero_std_path_relative", "std.path"},
  {"std.path.splitBase", "__zero_std_path_split_base", "std.path"},
  {"std.path.splitDir", "__zero_std_path_split_dir", "std.path"},
  {"std.path.stem", "__zero_std_path_stem", "std.path"},
  {"std.parse.parseI32Base", "__zero_std_parse_parse_i32_base", "std.parse"},
  {"std.parse.parseI32Prefix", "__zero_std_parse_parse_i32_prefix", "std.parse"},
  {"std.parse.parseI64", "__zero_std_parse_parse_i64", "std.parse"},
  {"std.parse.parseI64Base", "__zero_std_parse_parse_i64_base", "std.parse"},
  {"std.parse.parseI64Prefix", "__zero_std_parse_parse_i64_prefix", "std.parse"},
  {"std.parse.parseByteSize", "__zero_std_parse_parse_byte_size", "std.parse"},
  {"std.parse.parseDuration", "__zero_std_parse_parse_duration", "std.parse"},
  {"std.parse.parseU32Base", "__zero_std_parse_parse_u32_base", "std.parse"},
  {"std.parse.parseU32Prefix", "__zero_std_parse_parse_u32_prefix", "std.parse"},
  {"std.parse.parseU64", "__zero_std_parse_parse_u64", "std.parse"},
  {"std.parse.parseU64Base", "__zero_std_parse_parse_u64_base", "std.parse"},
  {"std.parse.parseU64Prefix", "__zero_std_parse_parse_u64_prefix", "std.parse"},
  {"std.parse.parseUsizeBase", "__zero_std_parse_parse_usize_base", "std.parse"},
  {"std.parse.parseUsizePrefix", "__zero_std_parse_parse_usize_prefix", "std.parse"},
  {"std.proc.runCode", "__zero_std_proc_run_code", "std.proc"},
  {"std.proc.runOk", "__zero_std_proc_run_ok", "std.proc"},
  {"std.rand.entropyHex32", "rand_entropy_hex32", "std.rand"},
  {"std.regex.compile", "__zero_std_regex_compile", "std.regex"},
  {"std.regex.compileErrorOffset", "__zero_std_regex_compile_error_offset", "std.regex"},
  {"std.regex.compileStatus", "__zero_std_regex_compile_status", "std.regex"},
  {"std.regex.contains", "__zero_std_regex_contains", "std.regex"},
  {"std.regex.find", "__zero_std_regex_find", "std.regex"},
  {"std.regex.findCount", "__zero_std_regex_find_count", "std.regex"},
  {"std.regex.findIndex", "__zero_std_regex_find_index", "std.regex"},
  {"std.regex.findNth", "__zero_std_regex_find_nth", "std.regex"},
  {"std.regex.findNthIndex", "__zero_std_regex_find_nth_index", "std.regex"},
  {"std.regex.replace", "__zero_std_regex_replace", "std.regex"},
  {"std.regex.split", "__zero_std_regex_split", "std.regex"},
  {"std.regex.splitCount", "__zero_std_regex_split_count", "std.regex"},
  {"std.regex.statusName", "__zero_std_regex_status_name", "std.regex"},
  {"std.regex.isMatch", "__zero_std_regex_is_match", "std.regex"},
  {"std.regex.matches", "__zero_std_regex_matches", "std.regex"},
  {"std.search.containsSortedI32", "__zero_std_search_contains_sorted_i32", "std.search"},
  {"std.search.containsSortedDescI32", "__zero_std_search_contains_sorted_desc_i32", "std.search"},
  {"std.search.containsSortedU32", "__zero_std_search_contains_sorted_u32", "std.search"},
  {"std.search.containsSortedDescU32", "__zero_std_search_contains_sorted_desc_u32", "std.search"},
  {"std.search.containsSortedUsize", "__zero_std_search_contains_sorted_usize", "std.search"},
  {"std.search.containsSortedDescUsize", "__zero_std_search_contains_sorted_desc_usize", "std.search"},
  {"std.search.countSortedI32", "__zero_std_search_count_sorted_i32", "std.search"},
  {"std.search.countSortedDescI32", "__zero_std_search_count_sorted_desc_i32", "std.search"},
  {"std.search.countSortedU32", "__zero_std_search_count_sorted_u32", "std.search"},
  {"std.search.countSortedDescU32", "__zero_std_search_count_sorted_desc_u32", "std.search"},
  {"std.search.countSortedUsize", "__zero_std_search_count_sorted_usize", "std.search"},
  {"std.search.countSortedDescUsize", "__zero_std_search_count_sorted_desc_usize", "std.search"},
  {"std.search.equalRangeI32", "__zero_std_search_equal_range_i32", "std.search"},
  {"std.search.equalRangeDescI32", "__zero_std_search_equal_range_desc_i32", "std.search"},
  {"std.search.equalRangeU32", "__zero_std_search_equal_range_u32", "std.search"},
  {"std.search.equalRangeDescU32", "__zero_std_search_equal_range_desc_u32", "std.search"},
  {"std.search.equalRangeUsize", "__zero_std_search_equal_range_usize", "std.search"},
  {"std.search.equalRangeDescUsize", "__zero_std_search_equal_range_desc_usize", "std.search"},
  {"std.search.partitionPointI32", "__zero_std_search_partition_point_i32", "std.search"},
  {"std.search.partitionPointDescI32", "__zero_std_search_partition_point_desc_i32", "std.search"},
  {"std.search.partitionPointU32", "__zero_std_search_partition_point_u32", "std.search"},
  {"std.search.partitionPointDescU32", "__zero_std_search_partition_point_desc_u32", "std.search"},
  {"std.search.partitionPointUsize", "__zero_std_search_partition_point_usize", "std.search"},
  {"std.search.partitionPointDescUsize", "__zero_std_search_partition_point_desc_usize", "std.search"},
  {"std.search.indexOf", "__zero_std_search_index_of", "std.search"},
  {"std.search.lastIndexOf", "__zero_std_search_last_index_of", "std.search"},
  {"std.search.binaryDescI32", "__zero_std_search_binary_desc_i32", "std.search"},
  {"std.search.binaryDescU32", "__zero_std_search_binary_desc_u32", "std.search"},
  {"std.search.binaryDescUsize", "__zero_std_search_binary_desc_usize", "std.search"},
  {"std.search.lowerBoundDescI32", "__zero_std_search_lower_bound_desc_i32", "std.search"},
  {"std.search.lowerBoundDescU32", "__zero_std_search_lower_bound_desc_u32", "std.search"},
  {"std.search.lowerBoundDescUsize", "__zero_std_search_lower_bound_desc_usize", "std.search"},
  {"std.search.maxI32", "__zero_std_search_max_i32", "std.search"},
  {"std.search.maxIndexI32", "__zero_std_search_max_index_i32", "std.search"},
  {"std.search.maxIndexU32", "__zero_std_search_max_index_u32", "std.search"},
  {"std.search.maxIndexUsize", "__zero_std_search_max_index_usize", "std.search"},
  {"std.search.maxU32", "__zero_std_search_max_u32", "std.search"},
  {"std.search.maxUsize", "__zero_std_search_max_usize", "std.search"},
  {"std.search.minI32", "__zero_std_search_min_i32", "std.search"},
  {"std.search.minIndexI32", "__zero_std_search_min_index_i32", "std.search"},
  {"std.search.minIndexU32", "__zero_std_search_min_index_u32", "std.search"},
  {"std.search.minIndexUsize", "__zero_std_search_min_index_usize", "std.search"},
  {"std.search.minU32", "__zero_std_search_min_u32", "std.search"},
  {"std.search.minUsize", "__zero_std_search_min_usize", "std.search"},
  {"std.search.upperBoundDescI32", "__zero_std_search_upper_bound_desc_i32", "std.search"},
  {"std.search.upperBoundDescU32", "__zero_std_search_upper_bound_desc_u32", "std.search"},
  {"std.search.upperBoundDescUsize", "__zero_std_search_upper_bound_desc_usize", "std.search"},
  {"std.sort.dedupeSortedI32", "__zero_std_sort_dedupe_sorted_i32", "std.sort"},
  {"std.sort.dedupeSortedU32", "__zero_std_sort_dedupe_sorted_u32", "std.sort"},
  {"std.sort.dedupeSortedUsize", "__zero_std_sort_dedupe_sorted_usize", "std.sort"},
  {"std.sort.insertionI32", "__zero_std_sort_insertion_i32", "std.sort"},
  {"std.sort.insertionDescI32", "__zero_std_sort_insertion_desc_i32", "std.sort"},
  {"std.sort.insertionU32", "__zero_std_sort_insertion_u32", "std.sort"},
  {"std.sort.insertionDescU32", "__zero_std_sort_insertion_desc_u32", "std.sort"},
  {"std.sort.insertionUsize", "__zero_std_sort_insertion_usize", "std.sort"},
  {"std.sort.insertionDescUsize", "__zero_std_sort_insertion_desc_usize", "std.sort"},
  {"std.sort.stableI32", "__zero_std_sort_stable_i32", "std.sort"},
  {"std.sort.stableU32", "__zero_std_sort_stable_u32", "std.sort"},
  {"std.sort.stableUsize", "__zero_std_sort_stable_usize", "std.sort"},
  {"std.sort.stableDescI32", "__zero_std_sort_stable_desc_i32", "std.sort"},
  {"std.sort.stableDescU32", "__zero_std_sort_stable_desc_u32", "std.sort"},
  {"std.sort.stableDescUsize", "__zero_std_sort_stable_desc_usize", "std.sort"},
  {"std.sort.unstableI32", "__zero_std_sort_unstable_i32", "std.sort"},
  {"std.sort.unstableU32", "__zero_std_sort_unstable_u32", "std.sort"},
  {"std.sort.unstableUsize", "__zero_std_sort_unstable_usize", "std.sort"},
  {"std.sort.unstableDescI32", "__zero_std_sort_unstable_desc_i32", "std.sort"},
  {"std.sort.unstableDescU32", "__zero_std_sort_unstable_desc_u32", "std.sort"},
  {"std.sort.unstableDescUsize", "__zero_std_sort_unstable_desc_usize", "std.sort"},
  {"std.sort.reverseI32", "__zero_std_sort_reverse_i32", "std.sort"},
  {"std.sort.swapI32", "__zero_std_sort_swap_i32", "std.sort"},
  {"std.sort.rotateLeftI32", "__zero_std_sort_rotate_left_i32", "std.sort"},
  {"std.sort.rotateRightI32", "__zero_std_sort_rotate_right_i32", "std.sort"},
  {"std.sort.reverseU32", "__zero_std_sort_reverse_u32", "std.sort"},
  {"std.sort.swapU32", "__zero_std_sort_swap_u32", "std.sort"},
  {"std.sort.rotateLeftU32", "__zero_std_sort_rotate_left_u32", "std.sort"},
  {"std.sort.rotateRightU32", "__zero_std_sort_rotate_right_u32", "std.sort"},
  {"std.sort.reverseUsize", "__zero_std_sort_reverse_usize", "std.sort"},
  {"std.sort.swapUsize", "__zero_std_sort_swap_usize", "std.sort"},
  {"std.sort.rotateLeftUsize", "__zero_std_sort_rotate_left_usize", "std.sort"},
  {"std.sort.rotateRightUsize", "__zero_std_sort_rotate_right_usize", "std.sort"},
  {"std.sort.isSortedI32", "__zero_std_sort_is_sorted_i32", "std.sort"},
  {"std.sort.isSortedDescI32", "__zero_std_sort_is_sorted_desc_i32", "std.sort"},
  {"std.sort.isSortedU32", "__zero_std_sort_is_sorted_u32", "std.sort"},
  {"std.sort.isSortedDescU32", "__zero_std_sort_is_sorted_desc_u32", "std.sort"},
  {"std.sort.isSortedUsize", "__zero_std_sort_is_sorted_usize", "std.sort"},
  {"std.sort.isSortedDescUsize", "__zero_std_sort_is_sorted_desc_usize", "std.sort"},
  {"std.sort.partitionI32", "__zero_std_sort_partition_i32", "std.sort"},
  {"std.sort.partitionDescI32", "__zero_std_sort_partition_desc_i32", "std.sort"},
  {"std.sort.partitionU32", "__zero_std_sort_partition_u32", "std.sort"},
  {"std.sort.partitionDescU32", "__zero_std_sort_partition_desc_u32", "std.sort"},
  {"std.sort.partitionUsize", "__zero_std_sort_partition_usize", "std.sort"},
  {"std.sort.partitionDescUsize", "__zero_std_sort_partition_desc_usize", "std.sort"},
  {"std.sort.isPartitionedI32", "__zero_std_sort_is_partitioned_i32", "std.sort"},
  {"std.sort.isPartitionedDescI32", "__zero_std_sort_is_partitioned_desc_i32", "std.sort"},
  {"std.sort.isPartitionedU32", "__zero_std_sort_is_partitioned_u32", "std.sort"},
  {"std.sort.isPartitionedDescU32", "__zero_std_sort_is_partitioned_desc_u32", "std.sort"},
  {"std.sort.isPartitionedUsize", "__zero_std_sort_is_partitioned_usize", "std.sort"},
  {"std.sort.isPartitionedDescUsize", "__zero_std_sort_is_partitioned_desc_usize", "std.sort"},
  {"std.sort.selectNthI32", "__zero_std_sort_select_nth_i32", "std.sort"},
  {"std.sort.selectNthDescI32", "__zero_std_sort_select_nth_desc_i32", "std.sort"},
  {"std.sort.selectNthU32", "__zero_std_sort_select_nth_u32", "std.sort"},
  {"std.sort.selectNthDescU32", "__zero_std_sort_select_nth_desc_u32", "std.sort"},
  {"std.sort.selectNthUsize", "__zero_std_sort_select_nth_usize", "std.sort"},
  {"std.sort.selectNthDescUsize", "__zero_std_sort_select_nth_desc_usize", "std.sort"},
  {"std.sort.mergeSortedI32", "__zero_std_sort_merge_sorted_i32", "std.sort"},
  {"std.sort.mergeSortedDescI32", "__zero_std_sort_merge_sorted_desc_i32", "std.sort"},
  {"std.sort.mergeSortedU32", "__zero_std_sort_merge_sorted_u32", "std.sort"},
  {"std.sort.mergeSortedDescU32", "__zero_std_sort_merge_sorted_desc_u32", "std.sort"},
  {"std.sort.mergeSortedUsize", "__zero_std_sort_merge_sorted_usize", "std.sort"},
  {"std.sort.mergeSortedDescUsize", "__zero_std_sort_merge_sorted_desc_usize", "std.sort"},
  {"std.str.fieldAscii", "__zero_std_str_field_ascii", "std.str"},
  {"std.str.fieldCountAscii", "__zero_std_str_field_count_ascii", "std.str"},
  {"std.str.line", "__zero_std_str_line", "std.str"},
  {"std.str.lineCount", "__zero_std_str_line_count", "std.str"},
  {"std.str.replace", "__zero_std_str_replace", "std.str"},
  {"std.str.split", "__zero_std_str_split", "std.str"},
  {"std.str.splitCount", "__zero_std_str_split_count", "std.str"},
  {"std.time.abs", "__zero_std_time_abs", "std.time"},
  {"std.time.between", "__zero_std_time_between", "std.time"},
  {"std.time.hasElapsed", "__zero_std_time_has_elapsed", "std.time"},
  {"std.time.isRfc3339Date", "__zero_std_time_is_rfc3339_date", "std.time"},
  {"std.time.isRfc3339Time", "__zero_std_time_is_rfc3339_time", "std.time"},
  {"std.time.isRfc3339DateTime", "__zero_std_time_is_rfc3339_datetime", "std.time"},
  {"std.time.parseRfc3339DateTimeOr", "__zero_std_time_parse_rfc3339_datetime_or", "std.time"},
  {"std.time.isLeapYear", "__zero_std_time_is_leap_year", "std.time"},
  {"std.time.daysInMonth", "__zero_std_time_days_in_month", "std.time"},
  {"std.toml.bool", "__zero_std_toml_bool", "std.toml"},
  {"std.toml.arrayBool", "__zero_std_toml_array_bool", "std.toml"},
  {"std.toml.arrayCount", "__zero_std_toml_array_count", "std.toml"},
  {"std.toml.arrayI32", "__zero_std_toml_array_i32", "std.toml"},
  {"std.toml.arrayString", "__zero_std_toml_array_string", "std.toml"},
  {"std.toml.arrayU32", "__zero_std_toml_array_u32", "std.toml"},
  {"std.toml.arrayValue", "__zero_std_toml_array_value", "std.toml"},
  {"std.toml.field", "__zero_std_toml_field", "std.toml"},
  {"std.toml.i32", "__zero_std_toml_i32", "std.toml"},
  {"std.toml.string", "__zero_std_toml_string", "std.toml"},
  {"std.toml.stringDecode", "__zero_std_toml_string_decode", "std.toml"},
  {"std.toml.u32", "__zero_std_toml_u32", "std.toml"},
  {"std.toml.validate", "__zero_std_toml_validate", "std.toml"},
  {"std.toml.validateBytes", "__zero_std_toml_validate_bytes", "std.toml"},
  {"std.toml.writeKeyValueBool", "__zero_std_toml_write_key_value_bool", "std.toml"},
  {"std.toml.writeKeyValueString", "__zero_std_toml_write_key_value_string", "std.toml"},
  {"std.toml.writeKeyValueU32", "__zero_std_toml_write_key_value_u32", "std.toml"},
  {"std.toml.writeTableHeader", "__zero_std_toml_write_table_header", "std.toml"},
  {"std.unicode.decodeAt", "__zero_std_unicode_decode_at", "std.unicode"},
  {"std.unicode.decodeStatusAt", "__zero_std_unicode_decode_status_at", "std.unicode"},
  {"std.unicode.widthAt", "__zero_std_unicode_width_at", "std.unicode"},
  {"std.unicode.encode", "__zero_std_unicode_encode", "std.unicode"},
  {"std.unicode.encodedWidth", "__zero_std_unicode_encoded_width", "std.unicode"},
  {"std.unicode.invalidIndex", "__zero_std_unicode_invalid_index", "std.unicode"},
  {"std.unicode.isDigit", "__zero_std_unicode_is_digit", "std.unicode"},
  {"std.unicode.isWord", "__zero_std_unicode_is_word", "std.unicode"},
  {"std.unicode.isSpace", "__zero_std_unicode_is_space", "std.unicode"},
  {"std.unicode.nextIndex", "__zero_std_unicode_next_index", "std.unicode"},
  {"std.unicode.statusName", "__zero_std_unicode_status_name", "std.unicode"},
  {"std.url.appendQuery", "__zero_std_url_append_query", "std.url"},
  {"std.url.appendFragment", "__zero_std_url_append_fragment", "std.url"},
  {"std.url.appendFormField", "__zero_std_url_append_form_field", "std.url"},
  {"std.url.authority", "__zero_std_url_authority", "std.url"},
  {"std.url.fragment", "__zero_std_url_fragment", "std.url"},
  {"std.url.formValue", "__zero_std_url_form_value", "std.url"},
  {"std.url.host", "__zero_std_url_host", "std.url"},
  {"std.url.path", "__zero_std_url_path", "std.url"},
  {"std.url.percentDecode", "__zero_std_url_percent_decode", "std.url"},
  {"std.url.percentEncode", "__zero_std_url_percent_encode", "std.url"},
  {"std.url.query", "__zero_std_url_query", "std.url"},
  {"std.url.queryEscape", "__zero_std_url_query_escape", "std.url"},
  {"std.url.queryUnescape", "__zero_std_url_query_unescape", "std.url"},
  {"std.url.queryValue", "__zero_std_url_query_value", "std.url"},
  {"std.url.queryValueDecoded", "__zero_std_url_query_value_decoded", "std.url"},
  {"std.url.scheme", "__zero_std_url_scheme", "std.url"},
  {"std.url.writeFormField", "__zero_std_url_write_form_field", "std.url"},
  {"std.url.writeQueryParam", "__zero_std_url_write_query_param", "std.url"},
  {"std.url.writeUrl", "__zero_std_url_write_url", "std.url"},
};

static bool std_source_text_eq(const char *left, const char *right) {
  left = left ? left : "";
  right = right ? right : "";
  size_t left_len = strlen(left);
  return left_len == strlen(right) && memcmp(left, right, left_len) == 0;
}

static uint64_t std_source_hash_text(uint64_t hash, const char *text) {
  const unsigned char *bytes = (const unsigned char *)(text ? text : "");
  while (*bytes) {
    hash ^= (uint64_t)*bytes++;
    hash *= 1099511628211ull;
  }
  hash ^= 0xffu;
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t std_source_hash_bytes(uint64_t hash, const unsigned char *bytes, size_t len) {
  hash ^= (uint64_t)len;
  hash *= 1099511628211ull;
  for (size_t i = 0; bytes && i < len; i++) {
    hash ^= (uint64_t)bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

uint64_t z_std_source_graph_fingerprint(void) {
  static uint64_t cached = 0;
  if (cached != 0) return cached;
  uint64_t hash = 1469598103934665603ull;
  hash = std_source_hash_text(hash, "zero-embedded-stdlib-graph-v1");
  for (size_t i = 0; i < z_std_source_module_count(); i++) {
    const ZStdSourceModule *module = z_std_source_module_at(i);
    hash = std_source_hash_text(hash, module ? module->module : "");
    hash = std_source_hash_text(hash, module ? module->path : "");
    hash = std_source_hash_bytes(hash, module ? module->graph_bytes : NULL, module ? module->graph_len : 0);
  }
  cached = hash ? hash : 1;
  return cached;
}

size_t z_std_source_module_count(void) {
  return sizeof(std_source_modules) / sizeof(std_source_modules[0]);
}

const ZStdSourceModule *z_std_source_module_at(size_t index) {
  return index < z_std_source_module_count() ? &std_source_modules[index] : NULL;
}

const ZStdSourceModule *z_std_source_module_for_name(const char *module) {
  for (size_t i = 0; i < sizeof(std_source_modules) / sizeof(std_source_modules[0]); i++) {
    if (std_source_text_eq(std_source_modules[i].module, module)) return &std_source_modules[i];
  }
  return NULL;
}

static const char *std_source_basename(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  return slash ? slash + 1 : (path ? path : "");
}

static bool std_source_stem_eq(const char *left, const char *right) {
  const char *left_base = std_source_basename(left);
  const char *right_base = std_source_basename(right);
  const char *left_dot = strrchr(left_base, '.');
  const char *right_dot = strrchr(right_base, '.');
  size_t left_len = left_dot ? (size_t)(left_dot - left_base) : strlen(left_base);
  size_t right_len = right_dot ? (size_t)(right_dot - right_base) : strlen(right_base);
  return left_len == right_len && strncmp(left_base, right_base, left_len) == 0;
}

const ZStdSourceModule *z_std_source_module_for_path(const char *path) {
  for (size_t i = 0; path && i < sizeof(std_source_modules) / sizeof(std_source_modules[0]); i++) {
    const ZStdSourceModule *module = &std_source_modules[i];
    if (std_source_text_eq(module->path, path) ||
        std_source_text_eq(std_source_basename(module->path), std_source_basename(path)) ||
        std_source_stem_eq(module->path, path)) {
      return module;
    }
  }
  return NULL;
}

bool z_std_source_path_is_module_artifact(const char *path) {
  return path && path[0] &&
         (strncmp(path, "std/", strlen("std/")) == 0 || strstr(path, "/std/") != NULL) &&
         z_std_source_module_for_path(path) != NULL;
}

static const ZStdSourceCall *std_source_call_for_name(const char *qualified_name) {
  for (size_t i = 0; i < sizeof(std_source_calls) / sizeof(std_source_calls[0]); i++) {
    if (std_source_text_eq(std_source_calls[i].public_name, qualified_name)) return &std_source_calls[i];
  }
  return NULL;
}

const ZStdSourceModule *z_std_source_module_for_public_call(const char *qualified_name) {
  const ZStdSourceCall *call = std_source_call_for_name(qualified_name);
  return call ? z_std_source_module_for_name(call->module) : NULL;
}

const char *z_std_source_target_for_public_call(const char *qualified_name) {
  const ZStdSourceCall *call = std_source_call_for_name(qualified_name);
  return call ? call->target_name : NULL;
}

bool z_std_source_module_load_graph(const ZStdSourceModule *module, ZProgramGraph *out, ZDiag *diag) {
  if (!module || !module->graph_bytes || module->graph_len == 0) {
    if (diag) {
      *diag = (ZDiag){0};
      diag->code = 2002;
      diag->path = module ? module->path : "std";
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "embedded stdlib graph is missing");
      snprintf(diag->expected, sizeof(diag->expected), "binary graph bytes");
      snprintf(diag->actual, sizeof(diag->actual), "%s", module && module->module ? module->module : "unknown std module");
    }
    return false;
  }
  ZStdSourceGraphCacheEntry *cache = std_source_graph_cache_entry(module);
  if (cache && cache->present) return out ? z_program_graph_clone(&cache->graph, out) : true;
  if (!z_program_graph_store_bytes_are_binary(module->graph_bytes, module->graph_len)) {
    char *text = z_checked_malloc(module->graph_len + 1);
    memcpy(text, module->graph_bytes, module->graph_len);
    text[module->graph_len] = '\0';
    bool ok = z_program_graph_parse_dump(text, out, diag);
    free(text);
    if (ok) {
      ZProgramGraphValidation validation = {0};
      ok = z_program_graph_validate(out, &validation);
      if (!ok && diag) {
        *diag = (ZDiag){0};
        diag->code = 2002;
        diag->path = module->path;
        diag->line = 1;
        diag->column = 1;
        diag->length = 1;
        snprintf(diag->message, sizeof(diag->message), "embedded stdlib graph failed validation: %s",
                 validation.message[0] ? validation.message : "invalid graph shape");
        snprintf(diag->expected, sizeof(diag->expected), "shape-valid program graph");
        snprintf(diag->actual, sizeof(diag->actual), "%s", validation.code[0] ? validation.code : "invalid graph");
      }
    }
    if (!ok) {
      if (diag && !diag->path) diag->path = module->path;
      if (out) z_program_graph_free(out);
    } else if (cache && out) {
      z_program_graph_free(&cache->graph);
      z_program_graph_clone(out, &cache->graph);
      cache->present = true;
    }
    return ok;
  }
  ZProgramGraphStore store;
  z_program_graph_store_init(&store);
  bool ok = z_program_graph_store_parse_binary(module->path, module->graph_bytes, module->graph_len, &store, diag);
  if (ok && out) {
    *out = store.graph;
    store.graph = (ZProgramGraph){0};
  }
  z_program_graph_store_free(&store);
  if (!ok && diag && !diag->path) diag->path = module->path;
  if (ok && cache && out) {
    z_program_graph_free(&cache->graph);
    z_program_graph_clone(out, &cache->graph);
    cache->present = true;
  }
  return ok;
}
