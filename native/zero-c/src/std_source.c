#include "std_source.h"

#include "embedded_stdlib.inc"

#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *public_name;
  const char *target_name;
  const char *module;
} ZStdSourceCall;

static const ZStdSourceModule std_source_modules[] = {
  {"std.ascii", "std/ascii.0", zero_embedded_stdlib_std_ascii_0_chunks},
  {"std.collections", "std/collections.0", zero_embedded_stdlib_std_collections_0_chunks},
  {"std.fmt", "std/fmt.0", zero_embedded_stdlib_std_fmt_0_chunks},
  {"std.math", "std/math.0", zero_embedded_stdlib_std_math_0_chunks},
  {"std.mem", "std/mem.0", zero_embedded_stdlib_std_mem_0_chunks},
  {"std.parse", "std/parse.0", zero_embedded_stdlib_std_parse_0_chunks},
  {"std.path", "std/path.0", zero_embedded_stdlib_std_path_0_chunks},
  {"std.search", "std/search.0", zero_embedded_stdlib_std_search_0_chunks},
  {"std.sort", "std/sort.0", zero_embedded_stdlib_std_sort_0_chunks},
  {"std.str", "std/str.0", zero_embedded_stdlib_std_str_0_chunks},
  {"std.text", "std/text.0", zero_embedded_stdlib_std_text_0_chunks},
  {"std.time", "std/time.0", zero_embedded_stdlib_std_time_0_chunks},
};

static const ZStdSourceCall std_source_calls[] = {
  {"std.ascii.digitValue", "__zero_std_ascii_digit_value", "std.ascii"},
  {"std.ascii.hexValue", "__zero_std_ascii_hex_value", "std.ascii"},
  {"std.ascii.isAlnum", "__zero_std_ascii_is_alnum", "std.ascii"},
  {"std.ascii.isAlpha", "__zero_std_ascii_is_alpha", "std.ascii"},
  {"std.ascii.isDigit", "__zero_std_ascii_is_digit", "std.ascii"},
  {"std.ascii.isHexDigit", "__zero_std_ascii_is_hex_digit", "std.ascii"},
  {"std.ascii.isLower", "__zero_std_ascii_is_lower", "std.ascii"},
  {"std.ascii.isUpper", "__zero_std_ascii_is_upper", "std.ascii"},
  {"std.ascii.isWhitespace", "__zero_std_ascii_is_whitespace", "std.ascii"},
  {"std.ascii.toLower", "__zero_std_ascii_to_lower", "std.ascii"},
  {"std.ascii.toUpper", "__zero_std_ascii_to_upper", "std.ascii"},
  {"std.collections.append", "__zero_std_collections_append", "std.collections"},
  {"std.collections.contains", "__zero_std_collections_contains", "std.collections"},
  {"std.collections.count", "__zero_std_collections_count", "std.collections"},
  {"std.collections.moveToFront", "__zero_std_collections_move_to_front", "std.collections"},
  {"std.collections.push", "__zero_std_collections_push", "std.collections"},
  {"std.collections.removeSwap", "__zero_std_collections_remove_swap", "std.collections"},
  {"std.collections.view", "__zero_std_collections_view", "std.collections"},
  {"std.fmt.bool", "__zero_std_fmt_bool", "std.fmt"},
  {"std.fmt.hexLowerU32", "__zero_std_fmt_hex_lower_u32", "std.fmt"},
  {"std.fmt.i32", "__zero_std_fmt_i32", "std.fmt"},
  {"std.fmt.u32", "__zero_std_fmt_u32", "std.fmt"},
  {"std.fmt.usize", "__zero_std_fmt_usize", "std.fmt"},
  {"std.math.absI32", "__zero_std_math_abs_i32", "std.math"},
  {"std.math.absI64", "__zero_std_math_abs_i64", "std.math"},
  {"std.math.binomialU32", "__zero_std_math_binomial_u32", "std.math"},
  {"std.math.checkedAddI32", "__zero_std_math_checked_add_i32", "std.math"},
  {"std.math.checkedAddU32", "__zero_std_math_checked_add_u32", "std.math"},
  {"std.math.checkedAddUsize", "__zero_std_math_checked_add_usize", "std.math"},
  {"std.math.checkedLcmU32", "__zero_std_math_checked_lcm_u32", "std.math"},
  {"std.math.checkedMulI32", "__zero_std_math_checked_mul_i32", "std.math"},
  {"std.math.checkedMulU32", "__zero_std_math_checked_mul_u32", "std.math"},
  {"std.math.checkedMulUsize", "__zero_std_math_checked_mul_usize", "std.math"},
  {"std.math.checkedPowU32", "__zero_std_math_checked_pow_u32", "std.math"},
  {"std.math.checkedSubI32", "__zero_std_math_checked_sub_i32", "std.math"},
  {"std.math.checkedSubU32", "__zero_std_math_checked_sub_u32", "std.math"},
  {"std.math.checkedSubUsize", "__zero_std_math_checked_sub_usize", "std.math"},
  {"std.math.clampI32", "__zero_std_math_clamp_i32", "std.math"},
  {"std.math.clampI64", "__zero_std_math_clamp_i64", "std.math"},
  {"std.math.clampU32", "__zero_std_math_clamp_u32", "std.math"},
  {"std.math.clampU64", "__zero_std_math_clamp_u64", "std.math"},
  {"std.math.clampUsize", "__zero_std_math_clamp_usize", "std.math"},
  {"std.math.divisorCountU32", "__zero_std_math_divisor_count_u32", "std.math"},
  {"std.math.factorialU32", "__zero_std_math_factorial_u32", "std.math"},
  {"std.math.gcdU32", "__zero_std_math_gcd_u32", "std.math"},
  {"std.math.isEvenU32", "__zero_std_math_is_even_u32", "std.math"},
  {"std.math.isOddU32", "__zero_std_math_is_odd_u32", "std.math"},
  {"std.math.isPrimeU32", "__zero_std_math_is_prime_u32", "std.math"},
  {"std.math.lcmU32", "__zero_std_math_lcm_u32", "std.math"},
  {"std.math.maxI32", "__zero_std_math_max_i32", "std.math"},
  {"std.math.maxI64", "__zero_std_math_max_i64", "std.math"},
  {"std.math.maxU32", "__zero_std_math_max_u32", "std.math"},
  {"std.math.maxU64", "__zero_std_math_max_u64", "std.math"},
  {"std.math.maxUsize", "__zero_std_math_max_usize", "std.math"},
  {"std.math.minI32", "__zero_std_math_min_i32", "std.math"},
  {"std.math.minI64", "__zero_std_math_min_i64", "std.math"},
  {"std.math.minU32", "__zero_std_math_min_u32", "std.math"},
  {"std.math.minU64", "__zero_std_math_min_u64", "std.math"},
  {"std.math.minUsize", "__zero_std_math_min_usize", "std.math"},
  {"std.math.modPowU32", "__zero_std_math_mod_pow_u32", "std.math"},
  {"std.math.powU32", "__zero_std_math_pow_u32", "std.math"},
  {"std.math.properDivisorSumU32", "__zero_std_math_proper_divisor_sum_u32", "std.math"},
  {"std.math.saturatingAddI32", "__zero_std_math_saturating_add_i32", "std.math"},
  {"std.math.saturatingAddU32", "__zero_std_math_saturating_add_u32", "std.math"},
  {"std.math.saturatingAddUsize", "__zero_std_math_saturating_add_usize", "std.math"},
  {"std.math.saturatingMulI32", "__zero_std_math_saturating_mul_i32", "std.math"},
  {"std.math.saturatingMulU32", "__zero_std_math_saturating_mul_u32", "std.math"},
  {"std.math.saturatingMulUsize", "__zero_std_math_saturating_mul_usize", "std.math"},
  {"std.math.saturatingSubI32", "__zero_std_math_saturating_sub_i32", "std.math"},
  {"std.math.saturatingSubU32", "__zero_std_math_saturating_sub_u32", "std.math"},
  {"std.math.saturatingSubUsize", "__zero_std_math_saturating_sub_usize", "std.math"},
  {"std.math.sqrtFloorU32", "__zero_std_math_sqrt_floor_u32", "std.math"},
  {"std.mem.contains", "__zero_std_mem_contains", "std.mem"},
  {"std.mem.copyItems", "__zero_std_mem_copy_items", "std.mem"},
  {"std.mem.dropPrefix", "__zero_std_mem_drop_prefix", "std.mem"},
  {"std.mem.fillItems", "__zero_std_mem_fill_items", "std.mem"},
  {"std.mem.isEmpty", "__zero_std_mem_is_empty", "std.mem"},
  {"std.mem.prefix", "__zero_std_mem_prefix", "std.mem"},
  {"std.parse.isAsciiAlpha", "__zero_std_parse_is_ascii_alpha", "std.parse"},
  {"std.parse.isAsciiDigit", "__zero_std_parse_is_ascii_digit", "std.parse"},
  {"std.parse.isIdentifierStart", "__zero_std_parse_is_identifier_start", "std.parse"},
  {"std.parse.isWhitespace", "__zero_std_parse_is_whitespace", "std.parse"},
  {"std.parse.parseBool", "__zero_std_parse_parse_bool", "std.parse"},
  {"std.parse.parseI32", "__zero_std_parse_parse_i32", "std.parse"},
  {"std.parse.parseU8", "__zero_std_parse_parse_u8", "std.parse"},
  {"std.parse.parseU16", "__zero_std_parse_parse_u16", "std.parse"},
  {"std.parse.parseU32", "__zero_std_parse_parse_u32", "std.parse"},
  {"std.parse.parseUsize", "__zero_std_parse_parse_usize", "std.parse"},
  {"std.parse.scanDigits", "__zero_std_parse_scan_digits", "std.parse"},
  {"std.parse.scanIdentifier", "__zero_std_parse_scan_identifier", "std.parse"},
  {"std.parse.scanUntilByte", "__zero_std_parse_scan_until_byte", "std.parse"},
  {"std.parse.scanWhitespace", "__zero_std_parse_scan_whitespace", "std.parse"},
  {"std.parse.tokenAscii", "__zero_std_parse_token_ascii", "std.parse"},
  {"std.path.basename", "__zero_std_path_basename", "std.path"},
  {"std.path.dirname", "__zero_std_path_dirname", "std.path"},
  {"std.path.extension", "__zero_std_path_extension", "std.path"},
  {"std.path.join", "__zero_std_path_join", "std.path"},
  {"std.path.normalize", "__zero_std_path_normalize", "std.path"},
  {"std.path.relative", "__zero_std_path_relative", "std.path"},
  {"std.search.binaryI32", "__zero_std_search_binary_i32", "std.search"},
  {"std.search.binaryU32", "__zero_std_search_binary_u32", "std.search"},
  {"std.search.binaryUsize", "__zero_std_search_binary_usize", "std.search"},
  {"std.search.indexOf", "__zero_std_search_index_of", "std.search"},
  {"std.search.lastIndexOf", "__zero_std_search_last_index_of", "std.search"},
  {"std.search.lowerBoundI32", "__zero_std_search_lower_bound_i32", "std.search"},
  {"std.search.lowerBoundU32", "__zero_std_search_lower_bound_u32", "std.search"},
  {"std.search.lowerBoundUsize", "__zero_std_search_lower_bound_usize", "std.search"},
  {"std.sort.insertionI32", "__zero_std_sort_insertion_i32", "std.sort"},
  {"std.sort.insertionU32", "__zero_std_sort_insertion_u32", "std.sort"},
  {"std.sort.insertionUsize", "__zero_std_sort_insertion_usize", "std.sort"},
  {"std.sort.isSortedI32", "__zero_std_sort_is_sorted_i32", "std.sort"},
  {"std.sort.isSortedU32", "__zero_std_sort_is_sorted_u32", "std.sort"},
  {"std.sort.isSortedUsize", "__zero_std_sort_is_sorted_usize", "std.sort"},
  {"std.str.contains", "__zero_std_str_contains", "std.str"},
  {"std.str.concat", "__zero_std_str_concat", "std.str"},
  {"std.str.copy", "__zero_std_str_copy", "std.str"},
  {"std.str.count", "__zero_std_str_count", "std.str"},
  {"std.str.countByte", "__zero_std_str_count_byte", "std.str"},
  {"std.str.eqlIgnoreAsciiCase", "__zero_std_str_eql_ignore_ascii_case", "std.str"},
  {"std.str.endsWith", "__zero_std_str_ends_with", "std.str"},
  {"std.str.indexOf", "__zero_std_str_index_of", "std.str"},
  {"std.str.lastIndexOf", "__zero_std_str_last_index_of", "std.str"},
  {"std.str.repeat", "__zero_std_str_repeat", "std.str"},
  {"std.str.reverse", "__zero_std_str_reverse", "std.str"},
  {"std.str.startsWith", "__zero_std_str_starts_with", "std.str"},
  {"std.str.toLowerAscii", "__zero_std_str_to_lower_ascii", "std.str"},
  {"std.str.toUpperAscii", "__zero_std_str_to_upper_ascii", "std.str"},
  {"std.str.trimAscii", "__zero_std_str_trim_ascii", "std.str"},
  {"std.str.trimEndAscii", "__zero_std_str_trim_end_ascii", "std.str"},
  {"std.str.trimStartAscii", "__zero_std_str_trim_start_ascii", "std.str"},
  {"std.str.wordCountAscii", "__zero_std_str_word_count_ascii", "std.str"},
  {"std.text.isAscii", "__zero_std_text_is_ascii", "std.text"},
  {"std.text.utf8Len", "__zero_std_text_utf8_len", "std.text"},
  {"std.text.utf8Valid", "__zero_std_text_utf8_valid", "std.text"},
  {"std.time.clamp", "__zero_std_time_clamp", "std.time"},
  {"std.time.hours", "__zero_std_time_hours", "std.time"},
  {"std.time.isZero", "__zero_std_time_is_zero", "std.time"},
  {"std.time.max", "__zero_std_time_max", "std.time"},
  {"std.time.min", "__zero_std_time_min", "std.time"},
  {"std.time.minutes", "__zero_std_time_minutes", "std.time"},
  {"std.time.zero", "__zero_std_time_zero", "std.time"},
};

size_t z_std_source_module_count(void) {
  return sizeof(std_source_modules) / sizeof(std_source_modules[0]);
}

const ZStdSourceModule *z_std_source_module_at(size_t index) {
  return index < z_std_source_module_count() ? &std_source_modules[index] : NULL;
}

const ZStdSourceModule *z_std_source_module_for_name(const char *module) {
  for (size_t i = 0; i < sizeof(std_source_modules) / sizeof(std_source_modules[0]); i++) {
    if (strcmp(std_source_modules[i].module, module ? module : "") == 0) return &std_source_modules[i];
  }
  return NULL;
}

static const ZStdSourceCall *std_source_call_for_name(const char *qualified_name) {
  for (size_t i = 0; i < sizeof(std_source_calls) / sizeof(std_source_calls[0]); i++) {
    if (strcmp(std_source_calls[i].public_name, qualified_name ? qualified_name : "") == 0) return &std_source_calls[i];
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

char *z_std_source_module_copy_source(const ZStdSourceModule *module) {
  if (!module || !module->chunks) return z_strdup("");
  ZBuf source;
  zbuf_init(&source);
  for (size_t i = 0; module->chunks[i]; i++) zbuf_append(&source, module->chunks[i]);
  return source.data ? source.data : z_strdup("");
}
