#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "zero.h"
#include "buildability.h"
#include "std_sig.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#else
#include <process.h>
#endif
#include <unistd.h>

#define ZERO_VERSION "0.1.3"

#include "embedded_runtime_sources.inc"
#include "embedded_skills.inc"

typedef enum {
  EMIT_C,
  EMIT_EXE,
  EMIT_OBJ
} EmitKind;

typedef struct {
  const char *command;
  const char *kind;
  const char *input;
  const char *out;
  const char *target;
  const char *profile;
  const char *cc;
  const char *backend;
  const char *unknown_flag;
  const char *invalid_emit;
  const char *filter;
  int run_argc;
  char **run_argv;
  bool json;
  bool plan;
  bool apply;
  bool patch;
  bool all;
  bool fmt_check;
  bool legacy_backend;
  bool trace;
  EmitKind emit;
} Command;

typedef struct {
  char *binary_path;
  char *stripped_path;
  char *checksum_path;
  char *archive_path;
  char *debug_path;
  char *size_path;
  char *sbom_path;
  uint64_t checksum;
  long long binary_bytes;
  long long stripped_bytes;
  long long archive_bytes;
  long long debug_bytes;
  long long size_bytes;
  long long sbom_bytes;
} ShipArtifacts;

static void print_command_help(const char *command);

static const char *diag_code(int code) {
  switch (code) {
    case 1001: return "ERR001";
    case 1002: return "ERR002";
    case 1003: return "ERR003";
    case 2001: return "APP001";
    case 2002: return "BLD002";
    case 2003: return "BLD003";
    case 2004: return "BLD004";
    case 3002: return "NAM002";
    case 3003: return "NAM003";
    case 3004: return "NAM004";
    case 3005: return "TYP001";
    case 3006: return "TYP002";
    case 3007: return "TYP003";
    case 3008: return "NAM004";
    case 3009: return "TYP005";
    case 3010: return "TYP009";
    case 3011: return "STD002";
    case 3012: return "STD003";
    case 3013: return "OWN001";
    case 3014: return "OWN002";
    case 3015: return "MEM001";
    case 3016: return "TYP010";
    case 3017: return "TYP011";
    case 3018: return "TYP012";
    case 3019: return "TYP013";
    case 3020: return "TYP014";
    case 3021: return "TYP015";
    case 3022: return "TYP016";
    case 3023: return "TYP017";
    case 3024: return "TYP018";
    case 3025: return "TYP019";
    case 3026: return "TYP020";
    case 3027: return "TYP021";
    case 3028: return "TYP022";
    case 3029: return "BOR001";
    case 3030: return "BOR002";
    case 3031: return "ABI001";
    case 3032: return "TYP023";
    case 3033: return "TYP024";
    case 3034: return "TYP025";
    case 3035: return "MET001";
    case 3036: return "TYP026";
    case 3050: return "TYP027";
    case 3037: return "PUB001";
    case 3038: return "IFC001";
    case 3039: return "IFC002";
    case 3040: return "IFC003";
    case 3041: return "IFC004";
    case 3042: return "IFC005";
    case 3043: return "STC001";
    case 3044: return "STC002";
    case 3045: return "STC003";
    case 3046: return "SHM001";
    case 3047: return "SHM002";
    case 3048: return "RCV001";
    case 3049: return "RCV002";
    case 3101: return "FLD001";
    case 3102: return "FLD002";
    case 3103: return "VAR001";
    case 3104: return "VAR002";
    case 3105: return "MAT001";
    case 3106: return "MAT002";
    case 3107: return "MAT003";
    case 3108: return "VAR003";
    case 3109: return "VAR004";
    case 3110: return "MAT004";
    case 3111: return "MAT005";
    case 4004: return "CGEN004";
    case 5001: return "WEB001";
    case 6001: return "TAR001";
    case 6002: return "TAR002";
    case 7001: return "IMP001";
    case 7002: return "IMP002";
    case 7003: return "IMP003";
    case 8001: return "CIMP001";
    case 8002: return "CIMP002";
    case 8003: return "CIMP003";
    case 9001: return "PKG001";
    case 9002: return "PKG002";
    case 9003: return "PKG003";
    case 9004: return "PKG004";
    default: return "PAR100";
  }
}

static void print_diag(const char *path, const ZDiag *diag) {
  fprintf(stderr, "%s:%d:%d %s: %s\n", path ? path : "<input>", diag->line, diag->column, diag_code(diag->code), diag->message);
  if (diag->expected[0]) fprintf(stderr, "  expected: %s\n", diag->expected);
  if (diag->actual[0]) fprintf(stderr, "  actual: %s\n", diag->actual);
  if (diag->help[0]) fprintf(stderr, "  help: %s\n", diag->help);
  fprintf(stderr, "  explain: zero explain %s\n", diag_code(diag->code));
}

static void append_json_string(ZBuf *buf, const char *value) {
  zbuf_append_char(buf, '"');
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    if (*cursor == '"') zbuf_append(buf, "\\\"");
    else if (*cursor == '\\') zbuf_append(buf, "\\\\");
    else if (*cursor == '\n') zbuf_append(buf, "\\n");
    else if (*cursor == '\r') zbuf_append(buf, "\\r");
    else if (*cursor == '\t') zbuf_append(buf, "\\t");
    else zbuf_append_char(buf, *cursor);
  }
  zbuf_append_char(buf, '"');
}

static void append_json_string_or_null(ZBuf *buf, const char *value) {
  if (value && value[0]) {
    append_json_string(buf, value);
  } else {
    zbuf_append(buf, "null");
  }
}

static void print_json_string(const char *value) {
  ZBuf buf;
  zbuf_init(&buf);
  append_json_string(&buf, value);
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static const ZeroEmbeddedSkill *find_embedded_skill(const char *name) {
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    if (strcmp(zero_embedded_skills[i].name, name) == 0) return &zero_embedded_skills[i];
  }
  return NULL;
}

static void append_embedded_skill_content(ZBuf *buf, const ZeroEmbeddedSkill *skill) {
  if (!skill || !skill->content) return;
  for (size_t i = 0; skill->content[i]; i++) {
    zbuf_append(buf, skill->content[i]);
  }
}

static void print_embedded_skill_content(const ZeroEmbeddedSkill *skill) {
  if (!skill || !skill->content) return;
  for (size_t i = 0; skill->content[i]; i++) {
    fputs(skill->content[i], stdout);
  }
}

static int embedded_skills_error(bool json, const char *message) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":false,\"error\":");
    append_json_string(&buf, message);
    zbuf_append(&buf, "}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    fprintf(stderr, "error: %s\n", message);
  }
  return 1;
}

static int embedded_skills_list_command(bool json) {
  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"data\":[");
    bool first = true;
    for (size_t i = 0; i < zero_embedded_skill_count; i++) {
      const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
      if (skill->hidden) continue;
      if (!first) zbuf_append(&buf, ",");
      first = false;
      zbuf_append(&buf, "{\"name\":");
      append_json_string(&buf, skill->name);
      zbuf_append(&buf, ",\"description\":");
      append_json_string(&buf, skill->description);
      zbuf_append(&buf, "}");
    }
    zbuf_append(&buf, "]}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
    return 0;
  }

  size_t max_name = 0;
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
    if (!skill->hidden && strlen(skill->name) > max_name) max_name = strlen(skill->name);
  }
  if (max_name == 0) {
    printf("No skills found\n");
    return 0;
  }
  for (size_t i = 0; i < zero_embedded_skill_count; i++) {
    const ZeroEmbeddedSkill *skill = &zero_embedded_skills[i];
    if (skill->hidden) continue;
    printf("  %-*s  %s\n", (int)max_name, skill->name, skill->description);
  }
  return 0;
}

static int embedded_skills_get_command(int argc, char **argv, int subcommand_index, bool json) {
  bool get_all = false;
  const ZeroEmbeddedSkill *targets[64];
  size_t target_count = 0;

  for (int i = subcommand_index + 1; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--all") == 0) {
      get_all = true;
      continue;
    }
    if (strcmp(arg, "--full") == 0 || strcmp(arg, "--json") == 0) continue;
    if (arg[0] == '-') {
      char message[160];
      snprintf(message, sizeof(message), "Unknown skills flag: %s", arg);
      return embedded_skills_error(json, message);
    }
    const ZeroEmbeddedSkill *skill = find_embedded_skill(arg);
    if (!skill) {
      char message[160];
      snprintf(message, sizeof(message), "Skill not found: %s", arg);
      return embedded_skills_error(json, message);
    }
    if (target_count < sizeof(targets) / sizeof(targets[0])) targets[target_count++] = skill;
  }

  if (get_all) {
    target_count = 0;
    for (size_t i = 0; i < zero_embedded_skill_count && target_count < sizeof(targets) / sizeof(targets[0]); i++) {
      if (!zero_embedded_skills[i].hidden) targets[target_count++] = &zero_embedded_skills[i];
    }
  }

  if (target_count == 0) {
    return embedded_skills_error(json, "No skill name provided. Usage: zero skills get <name>");
  }

  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\"success\":true,\"data\":[");
    for (size_t i = 0; i < target_count; i++) {
      if (i > 0) zbuf_append(&buf, ",");
      zbuf_append(&buf, "{\"name\":");
      append_json_string(&buf, targets[i]->name);
      zbuf_append(&buf, ",\"content\":");
      ZBuf content;
      zbuf_init(&content);
      append_embedded_skill_content(&content, targets[i]);
      append_json_string(&buf, content.data ? content.data : "");
      zbuf_free(&content);
      zbuf_append(&buf, "}");
    }
    zbuf_append(&buf, "]}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
    return 0;
  }

  for (size_t i = 0; i < target_count; i++) {
    if (i > 0) printf("\n---\n\n");
    print_embedded_skill_content(targets[i]);
  }
  return 0;
}

static int embedded_skills_command(int argc, char **argv, bool json) {
  int subcommand_index = -1;
  const char *subcommand = "list";
  for (int i = 2; i < argc; i++) {
    const char *arg = argv[i];
    if (strcmp(arg, "--json") == 0 || strcmp(arg, "--all") == 0 || strcmp(arg, "--full") == 0) continue;
    if (arg[0] == '-') {
      char message[160];
      snprintf(message, sizeof(message), "Unknown skills flag: %s", arg);
      return embedded_skills_error(json, message);
    }
    if (subcommand_index < 0) {
      subcommand = arg;
      subcommand_index = i;
    }
  }

  if (strcmp(subcommand, "help") == 0) {
    print_command_help("skills");
    return 0;
  }
  if (strcmp(subcommand, "list") == 0) return embedded_skills_list_command(json);
  if (strcmp(subcommand, "get") == 0) return embedded_skills_get_command(argc, argv, subcommand_index >= 0 ? subcommand_index : 1, json);

  char message[160];
  snprintf(message, sizeof(message), "Unknown skills subcommand: %s", subcommand);
  return embedded_skills_error(json, message);
}

static const char *row_token_kind_name(ZRowTokenKind kind) {
  switch (kind) {
    case Z_ROW_TOKEN_WORD: return "word";
    case Z_ROW_TOKEN_STRING: return "string";
    case Z_ROW_TOKEN_CHAR: return "char";
    case Z_ROW_TOKEN_NUMBER: return "number";
    case Z_ROW_TOKEN_SYMBOL: return "symbol";
    case Z_ROW_TOKEN_COMMENT: return "comment";
    case Z_ROW_TOKEN_NEWLINE: return "newline";
    case Z_ROW_TOKEN_INDENT: return "indent";
    case Z_ROW_TOKEN_DEDENT: return "dedent";
    case Z_ROW_TOKEN_EOF: return "eof";
  }
  return "unknown";
}

static void append_row_tokens_json(ZBuf *buf, const char *source_file, const ZRowTokenVec *tokens) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, source_file);
  zbuf_append(buf, ",\n  \"syntax\": \"row\",\n  \"tokens\": [\n");
  for (size_t i = 0; tokens && i < tokens->len; i++) {
    const ZRowToken *token = &tokens->items[i];
    zbuf_append(buf, "    {\"kind\": ");
    append_json_string(buf, row_token_kind_name(token->kind));
    zbuf_append(buf, ", \"text\": ");
    append_json_string(buf, token->text);
    zbuf_appendf(
      buf,
      ", \"line\": %d, \"column\": %d, \"offset\": %zu, \"length\": %zu}%s\n",
      token->line,
      token->column,
      token->offset,
      token->length,
      i + 1 < tokens->len ? "," : ""
    );
  }
  zbuf_append(buf, "  ]\n}\n");
}

static const char *stmt_kind_name(StmtKind kind) {
  switch (kind) {
    case STMT_LET: return "let";
    case STMT_ASSIGN: return "assign";
    case STMT_DEFER: return "defer";
    case STMT_CHECK: return "check";
    case STMT_RETURN: return "return";
    case STMT_EXPR: return "expr";
    case STMT_IF: return "if";
    case STMT_WHILE: return "while";
    case STMT_FOR: return "for";
    case STMT_BREAK: return "break";
    case STMT_CONTINUE: return "continue";
    case STMT_MATCH: return "match";
    case STMT_RAISE: return "raise";
  }
  return "unknown";
}

static void append_parse_json(ZBuf *buf, const char *source_file, const Program *program) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, source_file);
  zbuf_appendf(
    buf,
    ",\n  \"root\": {\"kind\": \"module\", \"shapeCount\": %zu, \"enumCount\": %zu, \"choiceCount\": %zu, \"functionCount\": %zu},\n",
    program ? program->shapes.len : 0,
    program ? program->enums.len : 0,
    program ? program->choices.len : 0,
    program ? program->functions.len : 0
  );
  zbuf_append(buf, "  \"shapes\": [");
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"shape\",\"name\":");
    append_json_string(buf, shape->name);
    zbuf_appendf(buf, ",\"fieldCount\":%zu,\"methodCount\":%zu,\"line\":%d,\"column\":%d}", shape->fields.len, shape->methods.len, shape->line, shape->column);
  }
  zbuf_append(buf, program && program->shapes.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"enums\": [");
  for (size_t i = 0; program && i < program->enums.len; i++) {
    const EnumDecl *item = &program->enums.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"enum\",\"name\":");
    append_json_string(buf, item->name);
    zbuf_appendf(buf, ",\"caseCount\":%zu,\"line\":%d,\"column\":%d}", item->cases.len, item->line, item->column);
  }
  zbuf_append(buf, program && program->enums.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"choices\": [");
  for (size_t i = 0; program && i < program->choices.len; i++) {
    const Choice *item = &program->choices.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"choice\",\"name\":");
    append_json_string(buf, item->name);
    zbuf_appendf(buf, ",\"caseCount\":%zu,\"line\":%d,\"column\":%d}", item->cases.len, item->line, item->column);
  }
  zbuf_append(buf, program && program->choices.len ? "\n  ],\n" : "],\n");
  zbuf_append(buf, "  \"functions\": [");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    zbuf_append(buf, i == 0 ? "\n    " : ",\n    ");
    zbuf_append(buf, "{\"kind\":\"function\",\"name\":");
    append_json_string(buf, fun->name);
    zbuf_append(buf, ",\"returnType\":");
    append_json_string(buf, fun->return_type);
    zbuf_appendf(buf, ",\"paramCount\":%zu,\"bodyKinds\":[", fun->params.len);
    for (size_t j = 0; j < fun->body.len; j++) {
      if (j > 0) zbuf_append(buf, ",");
      append_json_string(buf, stmt_kind_name(fun->body.items[j]->kind));
    }
    zbuf_appendf(buf, "],\"line\":%d,\"column\":%d}", fun->line, fun->column);
  }
  zbuf_append(buf, program && program->functions.len ? "\n  ]\n}\n" : "]\n}\n");
}

static const char *zero_commit(void) {
  const char *commit = getenv("ZERO_COMMIT");
  return commit && commit[0] ? commit : "unknown";
}

static bool command_available(const char *name) {
  char command[128];
  snprintf(command, sizeof(command), "command -v %s >/dev/null 2>&1", name);
  return system(command) == 0;
}

typedef struct {
  char *source_file;
  char *include_dir;
  char *header_file;
} RuntimeCompileInputs;

static char *runtime_path_with_suffix(const char *path, const char *suffix) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, path ? path : "");
  zbuf_append(&buf, suffix ? suffix : "");
  return buf.data;
}

static char *runtime_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left ? left : "");
  if (buf.len > 0 && buf.data[buf.len - 1] != '/' && buf.data[buf.len - 1] != '\\') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static bool runtime_make_dir(const char *path, ZDiag *diag) {
#if defined(_WIN32)
  int rc = mkdir(path);
#else
  int rc = mkdir(path, 0777);
#endif
  if (rc == 0 || errno == EEXIST) return true;
  if (diag) {
    diag->code = 2002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "failed to create runtime compile directory '%s': %s", path, strerror(errno));
    snprintf(diag->help, sizeof(diag->help), "choose a writable output path");
  }
  return false;
}

static void runtime_compile_inputs_free(RuntimeCompileInputs *inputs) {
  if (!inputs) return;
  if (inputs->source_file) remove(inputs->source_file);
  if (inputs->header_file) remove(inputs->header_file);
  if (inputs->include_dir) rmdir(inputs->include_dir);
  free(inputs->source_file);
  free(inputs->include_dir);
  free(inputs->header_file);
  *inputs = (RuntimeCompileInputs){0};
}

static bool write_runtime_chunks_file(const char *path, const char *const *chunks, ZDiag *diag) {
  if (!path || !chunks) return false;
  ZBuf text;
  zbuf_init(&text);
  for (size_t i = 0; chunks[i]; i++) zbuf_append(&text, chunks[i]);
  bool ok = z_write_file(path, text.data ? text.data : "", diag);
  zbuf_free(&text);
  return ok;
}

static void runtime_diag_preserve_path(ZDiag *diag) {
  if (diag && diag->path) diag->path = z_strdup(diag->path);
}

static bool write_runtime_compile_inputs(
  const char *runtime_object_file,
  const char *source_suffix,
  const char *const *source_chunks,
  RuntimeCompileInputs *out,
  ZDiag *diag
) {
  if (!runtime_object_file || !source_suffix || !source_chunks || !out) return false;
  *out = (RuntimeCompileInputs){0};
  out->source_file = runtime_path_with_suffix(runtime_object_file, source_suffix);
  out->include_dir = runtime_path_with_suffix(runtime_object_file, ".include");
  out->header_file = runtime_join_path(out->include_dir, "zero_runtime.h");
  if (!out->source_file || !out->include_dir || !out->header_file) {
    runtime_compile_inputs_free(out);
    return false;
  }
  if (!runtime_make_dir(out->include_dir, diag) ||
      !write_runtime_chunks_file(out->header_file, zero_embedded_zero_runtime_h, diag) ||
      !write_runtime_chunks_file(out->source_file, source_chunks, diag)) {
    runtime_diag_preserve_path(diag);
    runtime_compile_inputs_free(out);
    return false;
  }
  return true;
}

static bool compile_zero_runtime_object(const char *runtime_object_file, const ZToolchainPlan *plan, const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  RuntimeCompileInputs inputs = {0};
  if (!write_runtime_compile_inputs(runtime_object_file, ".zero_runtime.c", zero_embedded_zero_runtime_c, &inputs, diag)) return false;
  bool ok = z_toolchain_compile_c_object(plan, command ? command->profile : NULL, target, inputs.source_file, runtime_object_file, inputs.include_dir, "-std=c11 -Wall -Wextra -Wpedantic");
  runtime_compile_inputs_free(&inputs);
  if (!ok && diag) {
    diag->code = 2003;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "host runtime object build failed");
    snprintf(diag->expected, sizeof(diag->expected), "C compiler can compile the embedded Zero runtime source");
    snprintf(diag->actual, sizeof(diag->actual), "runtime object compile command failed");
    snprintf(diag->help, sizeof(diag->help), "install a host C compiler or pass --cc for the runtime link plan");
  }
  return ok;
}

static bool compile_zero_http_curl_object(const char *runtime_object_file, const ZToolchainPlan *plan, const Command *command, const ZTargetInfo *target, ZDiag *diag) {
  RuntimeCompileInputs inputs = {0};
  if (!write_runtime_compile_inputs(runtime_object_file, ".zero_http_curl.c", zero_embedded_zero_http_curl_c, &inputs, diag)) return false;
  bool ok = z_toolchain_compile_c_object(plan, command ? command->profile : NULL, target, inputs.source_file, runtime_object_file, inputs.include_dir, "-std=c11 -Wall -Wextra -Wpedantic");
  runtime_compile_inputs_free(&inputs);
  if (!ok && diag) {
    diag->code = 2003;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "HTTP runtime provider build failed");
    snprintf(diag->expected, sizeof(diag->expected), "C compiler can compile the embedded HTTP provider source");
    snprintf(diag->actual, sizeof(diag->actual), "HTTP provider compile command failed");
    snprintf(diag->help, sizeof(diag->help), "install libcurl headers or pass --cc for the runtime link plan");
  }
  return ok;
}

static bool link_zero_runtime_executable(const char *object_file, const char *runtime_object_file, const char *http_object_file, const char *exe_file, const ZToolchainPlan *plan, const ZTargetInfo *target, ZDiag *diag) {
#if defined(__linux__)
  const char *linux_no_pie = " -no-pie";
#else
  const char *linux_no_pie = "";
#endif
  const char *object_files[3] = {object_file, runtime_object_file, http_object_file};
  size_t object_count = http_object_file && http_object_file[0] ? 3 : 2;
  bool ok = z_toolchain_link_objects(plan, target, object_files, object_count, exe_file, linux_no_pie, http_object_file && http_object_file[0] ? "-lcurl 2>/dev/null" : "2>/dev/null");
  if (!ok && diag) {
    diag->code = 2003;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "host runtime link failed");
    snprintf(diag->expected, sizeof(diag->expected), "direct object plus zero runtime object link successfully");
    snprintf(diag->actual, sizeof(diag->actual), "runtime link command failed");
    snprintf(diag->help, sizeof(diag->help), http_object_file && http_object_file[0] ? "install libcurl or inspect the direct object, runtime objects, and host C linker diagnostics" : "inspect the direct object, runtime object, and host C linker diagnostics");
  }
  return ok;
}

static char *command_first_line(const char *command) {
  FILE *pipe = popen(command, "r");
  if (!pipe) return z_strdup("");
  char line[256];
  if (!fgets(line, sizeof(line), pipe)) line[0] = 0;
  pclose(pipe);
  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
  return z_strdup(line);
}

static bool path_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

static int zero_mkdir(const char *path) {
#if defined(_WIN32)
  return mkdir(path);
#else
  return mkdir(path, 0777);
#endif
}

static int zero_lstat(const char *path, struct stat *st) {
#if defined(_WIN32)
  return stat(path, st);
#else
  return lstat(path, st);
#endif
}

static bool ensure_dir(const char *path, ZDiag *diag) {
  if (zero_mkdir(path) == 0 || errno == EEXIST) return true;
  diag->code = 2002;
  diag->path = path;
  diag->line = 1;
  diag->column = 1;
  snprintf(diag->message, sizeof(diag->message), "failed to create directory '%s': %s", path, strerror(errno));
  snprintf(diag->help, sizeof(diag->help), "choose a writable project path");
  return false;
}

static char *join_cli_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left);
  if (buf.len > 0 && buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right);
  return buf.data;
}

static bool remove_tree(const char *path, ZBuf *deleted) {
  struct stat st;
  if (zero_lstat(path, &st) != 0) return errno == ENOENT;
  if (S_ISDIR(st.st_mode)) {
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char *child = join_cli_path(path, entry->d_name);
      bool ok = remove_tree(child, deleted);
      free(child);
      if (!ok) {
        closedir(dir);
        return false;
      }
    }
    closedir(dir);
    if (rmdir(path) != 0) return false;
  } else if (unlink(path) != 0) {
    return false;
  }
  if (deleted) {
    if (deleted->len > 0) zbuf_append_char(deleted, '\n');
    zbuf_append(deleted, path);
  }
  return true;
}

typedef struct {
  bool args;
  bool env;
  bool fs;
  bool memory;
  bool alloc;
  bool path;
  bool codec;
  bool parse;
  bool time;
  bool rand;
  bool net;
  bool proc;
  bool web;
  bool world;
} CapabilitySummary;

#define STD_HELPER_MAX 192

typedef struct {
  bool used[STD_HELPER_MAX];
} HelperUseSummary;

typedef struct {
  const char *name;
  const char *symbol;
  const char *category;
  int estimated_direct_bytes;
} CompilerRuntimeHelperInfo;

typedef struct {
  const char *name;
  size_t byte_len;
} MemoryArrayBinding;

typedef struct {
  MemoryArrayBinding arrays[128];
  size_t len;
} MemoryScope;

typedef struct {
  size_t stack_estimate_bytes;
  size_t readonly_literal_bytes;
  size_t fixed_allocator_count;
  size_t arena_count;
  size_t null_allocator_count;
  size_t page_allocator_count;
  size_t general_allocator_count;
  size_t alloc_bytes_calls;
  size_t byte_buf_calls;
  size_t reset_calls;
  size_t capacity_calls;
  size_t vec_count;
  size_t vec_push_calls;
  size_t vec_len_calls;
  size_t vec_capacity_calls;
  size_t map_empty_count;
  size_t map_len_calls;
  size_t set_empty_count;
  size_t set_len_calls;
  size_t fixed_allocator_capacity_bytes;
  size_t arena_capacity_bytes;
  size_t collection_capacity_bytes;
  size_t allocation_requested_bytes;
  size_t unknown_capacity_sites;
} MemoryModelSummary;



static const CompilerRuntimeHelperInfo compiler_runtime_helpers[] = {
  {"runtime.arenaPlan", "compiler_runtime_arena_plan", "alloc", 160},
  {"runtime.sourceInputMode", "compiler_source_input_mode", "source-input", 176},
  {"runtime.sourceBuffer", "compiler_accept_source_buffer", "source-input", 96},
  {"runtime.tokenSummary", "compiler_token_summary", "lexer-json", 240},
  {"runtime.sourceHash", "compiler_source_hash", "source-input", 144},
  {"runtime.sourceLocation", "compiler_source_location_at", "source-map", 176},
  {"runtime.sourceOrder", "compiler_source_order_key", "source-order", 96},
  {"runtime.hostedPackageSourcePlan", "compiler_hosted_package_source_plan", "source-input", 160},
  {"runtime.abiSummary", "compiler_runtime_abi_summary", "runtime-abi", 192},
  {"runtime.memoryLayout", "compiler_runtime_memory_layout", "runtime-abi", 96},
  {"runtime.shimCost", "compiler_runtime_shim_cost", "runtime-abi", 96},
  {"runtime.helperSummary", "compiler_runtime_helper_summary", "runtime-helpers", 192},
  {"runtime.manifestSummary", "compiler_manifest_summary", "manifest", 320},
  {"runtime.moduleImportSummary", "compiler_module_import_summary", "module-graph", 240},
  {"runtime.moduleImportDiag", "compiler_module_import_diag_code", "module-graph", 112},
  {"runtime.moduleGraphReject", "compiler_module_graph_reject_code", "module-graph", 128},
  {"runtime.scopeSummary", "compiler_scope_summary", "name-resolution", 192},
  {"runtime.nameResolutionReport", "compiler_name_resolution_report", "name-resolution", 128},
  {"runtime.memberNameSummary", "compiler_member_name_summary", "name-resolution", 192},
  {"runtime.typeSurfaceSummary", "compiler_type_surface_summary", "checker-json", 192},
  {"runtime.expressionSurfaceSummary", "compiler_expression_surface_summary", "checker-json", 192},
  {"runtime.controlFlowSummary", "compiler_control_flow_summary", "checker-json", 128},
  {"runtime.genericStaticSummary", "compiler_generic_static_summary", "checker-json", 192},
  {"runtime.fallibilitySummary", "compiler_fallibility_summary", "checker-json", 160},
  {"runtime.fallibilityDiag", "compiler_fallibility_diag_code", "checker-json", 96},
  {"runtime.capabilitySummary", "compiler_capability_summary", "checker-json", 160},
  {"runtime.capabilityDiag", "compiler_capability_diag_code", "checker-json", 96},
  {"runtime.ownershipSummary", "compiler_ownership_summary", "checker-json", 160},
  {"runtime.ownershipDiag", "compiler_ownership_diag_code", "checker-json", 96},
  {"runtime.mirLoweringSummary", "compiler_mir_lowering_summary", "mir-json", 192},
  {"runtime.mirHash", "compiler_mir_hash", "mir-json", 144},
  {"runtime.mirModuleOrderHash", "compiler_mir_module_order_hash", "mir-json", 96},
  {"runtime.mirJsonContract", "compiler_mir_json_contract", "mir-json", 96},
  {"runtime.parseItemSummary", "compiler_parse_item_summary", "parse-json", 320},
  {"runtime.parseStmtExprSummary", "compiler_parse_stmt_expr_summary", "parse-json", 320},
  {"runtime.parseRootSummary", "compiler_parse_root_summary", "parse-json", 192},
  {"runtime.parsePublicApiSummary", "compiler_parse_public_api_summary", "graph-json", 192},
  {"runtime.parseAstSummary", "compiler_parse_ast_summary", "parse-json", 192},
  {"runtime.byteBufferPlan", "compiler_byte_buffer_plan", "buffer", 144},
  {"runtime.diagnosticReportBuild", "compiler_diagnostic_report_build", "diagnostics-json", 192},
  {"runtime.diagnosticDetailSummary", "compiler_diagnostic_detail_summary", "diagnostics-json", 128},
  {"runtime.relatedSpanSummary", "compiler_related_span_summary", "diagnostics-json", 96},
  {NULL, NULL, NULL, 0},
};

static void helper_summary_mark(HelperUseSummary *helpers, const char *name) {
  int index = z_std_helper_index(name, STD_HELPER_MAX);
  if (helpers && index >= 0) helpers->used[index] = true;
}

static void capability_summary_set(CapabilitySummary *caps, const char *capability) {
  if (!caps || !capability) return;
  if (strcmp(capability, "args") == 0) caps->args = true;
  else if (strcmp(capability, "env") == 0) caps->env = true;
  else if (strcmp(capability, "fs") == 0) caps->fs = true;
  else if (strcmp(capability, "memory") == 0) caps->memory = true;
  else if (strcmp(capability, "alloc") == 0) {
    caps->alloc = true;
    caps->memory = true;
  } else if (strcmp(capability, "path") == 0) caps->path = true;
  else if (strcmp(capability, "codec") == 0) caps->codec = true;
  else if (strcmp(capability, "parse") == 0) caps->parse = true;
  else if (strcmp(capability, "time") == 0) caps->time = true;
  else if (strcmp(capability, "rand") == 0) caps->rand = true;
  else if (strcmp(capability, "net") == 0) caps->net = true;
  else if (strcmp(capability, "proc") == 0) caps->proc = true;
  else if (strcmp(capability, "web") == 0) caps->web = true;
  else if (strcmp(capability, "world") == 0) caps->world = true;
}

static void append_capability_json_array(ZBuf *buf, const CapabilitySummary *caps) {
  bool wrote = false;
  zbuf_append(buf, "[");
#define APPEND_CAP(name, enabled) do { \
    if (enabled) { \
      if (wrote) zbuf_append(buf, ", "); \
      append_json_string(buf, name); \
      wrote = true; \
    } \
  } while (0)
  APPEND_CAP("args", caps && caps->args);
  APPEND_CAP("env", caps && caps->env);
  APPEND_CAP("fs", caps && caps->fs);
  APPEND_CAP("memory", caps && caps->memory);
  APPEND_CAP("alloc", caps && caps->alloc);
  APPEND_CAP("path", caps && caps->path);
  APPEND_CAP("codec", caps && caps->codec);
  APPEND_CAP("parse", caps && caps->parse);
  APPEND_CAP("time", caps && caps->time);
  APPEND_CAP("rand", caps && caps->rand);
  APPEND_CAP("net", caps && caps->net);
  APPEND_CAP("proc", caps && caps->proc);
  APPEND_CAP("web", caps && caps->web);
  APPEND_CAP("world", caps && caps->world);
#undef APPEND_CAP
  zbuf_append(buf, "]");
}

static void collect_member_name(const Expr *expr, ZBuf *buf) {
  if (!expr) return;
  if (expr->kind == EXPR_IDENT) {
    zbuf_append(buf, expr->text);
  } else if (expr->kind == EXPR_MEMBER) {
    collect_member_name(expr->left, buf);
    zbuf_append_char(buf, '.');
    zbuf_append(buf, expr->text);
  }
}

static char *expr_callee_name(const Expr *callee) {
  ZBuf name;
  zbuf_init(&name);
  collect_member_name(callee, &name);
  if (!name.data) zbuf_append(&name, "");
  return name.data;
}

static size_t memory_element_size(const char *element, bool *is_u8) {
  if (is_u8) *is_u8 = false;
  if (!element) return 0;
  while (*element == ' ' || *element == '\t') element++;
  if (strcmp(element, "u8") == 0 || strcmp(element, "i8") == 0 || strcmp(element, "Bool") == 0 || strcmp(element, "bool") == 0) {
    if (is_u8 && strcmp(element, "u8") == 0) *is_u8 = true;
    return 1;
  }
  if (strcmp(element, "u16") == 0 || strcmp(element, "i16") == 0) return 2;
  if (strcmp(element, "u32") == 0 || strcmp(element, "i32") == 0 || strcmp(element, "f32") == 0 || strcmp(element, "usize") == 0 || strcmp(element, "isize") == 0) return 4;
  if (strcmp(element, "u64") == 0 || strcmp(element, "i64") == 0 || strcmp(element, "f64") == 0) return 8;
  return 0;
}

static bool memory_parse_fixed_array_type(const char *type, size_t *out_len, size_t *out_bytes, bool *out_u8) {
  if (out_len) *out_len = 0;
  if (out_bytes) *out_bytes = 0;
  if (out_u8) *out_u8 = false;
  if (!type || type[0] != '[') return false;
  const char *cursor = type + 1;
  size_t len = 0;
  bool saw_digit = false;
  while (*cursor && *cursor != ']') {
    if (*cursor == '_') {
      cursor++;
      continue;
    }
    if (*cursor < '0' || *cursor > '9') return false;
    saw_digit = true;
    len = len * 10 + (size_t)(*cursor - '0');
    cursor++;
  }
  if (!saw_digit || *cursor != ']') return false;
  cursor++;
  bool is_u8 = false;
  size_t element_size = memory_element_size(cursor, &is_u8);
  if (element_size == 0) return false;
  if (out_len) *out_len = len;
  if (out_bytes) *out_bytes = len * element_size;
  if (out_u8) *out_u8 = is_u8;
  return true;
}

static bool memory_parse_number_literal(const Expr *expr, size_t *out_value) {
  if (out_value) *out_value = 0;
  if (!expr || expr->kind != EXPR_NUMBER || !expr->text) return false;
  const char *cursor = expr->text;
  size_t value = 0;
  bool saw_digit = false;
  while (*cursor) {
    if (*cursor == '_') {
      cursor++;
      continue;
    }
    if (*cursor < '0' || *cursor > '9') break;
    saw_digit = true;
    value = value * 10 + (size_t)(*cursor - '0');
    cursor++;
  }
  if (!saw_digit) return false;
  if (out_value) *out_value = value;
  return true;
}

static void memory_scope_note_array(MemoryScope *scope, const char *name, size_t byte_len) {
  if (!scope || !name || scope->len >= sizeof(scope->arrays) / sizeof(scope->arrays[0])) return;
  scope->arrays[scope->len++] = (MemoryArrayBinding){.name = name, .byte_len = byte_len};
}

static size_t memory_scope_lookup_array(const MemoryScope *scope, const char *name) {
  if (!scope || !name) return 0;
  for (size_t i = scope->len; i > 0; i--) {
    if (scope->arrays[i - 1].name && strcmp(scope->arrays[i - 1].name, name) == 0) return scope->arrays[i - 1].byte_len;
  }
  return 0;
}

static size_t memory_byte_capacity_expr(const MemoryScope *scope, const Expr *expr) {
  if (!expr) return 0;
  if (expr->kind == EXPR_BORROW) return memory_byte_capacity_expr(scope, expr->left);
  if (expr->kind == EXPR_IDENT) return memory_scope_lookup_array(scope, expr->text);
  return 0;
}

static void memory_model_collect_expr(const Expr *expr, MemoryScope *scope, MemoryModelSummary *summary);

static void memory_model_note_allocator_capacity(MemoryModelSummary *summary, size_t capacity, bool arena) {
  if (!summary) return;
  if (capacity == 0) {
    summary->unknown_capacity_sites++;
    return;
  }
  if (arena) summary->arena_capacity_bytes += capacity;
  else summary->fixed_allocator_capacity_bytes += capacity;
}

static void memory_model_collect_expr_vec(const ExprVec *exprs, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!exprs) return;
  for (size_t i = 0; i < exprs->len; i++) memory_model_collect_expr(exprs->items[i], scope, summary);
}

static void memory_model_collect_expr(const Expr *expr, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!expr || !summary) return;
  if (expr->kind == EXPR_STRING && expr->text) {
    summary->readonly_literal_bytes += strlen(expr->text) + 1;
  }
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    if (strcmp(callee, "std.mem.nullAlloc") == 0) summary->null_allocator_count++;
    else if (strcmp(callee, "std.mem.pageAlloc") == 0) summary->page_allocator_count++;
    else if (strcmp(callee, "std.mem.generalAlloc") == 0) summary->general_allocator_count++;
    else if (strcmp(callee, "std.mem.fixedBufAlloc") == 0) {
      summary->fixed_allocator_count++;
      memory_model_note_allocator_capacity(summary, expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0, false);
    } else if (strcmp(callee, "std.mem.arena") == 0) {
      summary->arena_count++;
      memory_model_note_allocator_capacity(summary, expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0, true);
    } else if (strcmp(callee, "std.mem.allocBytes") == 0) {
      summary->alloc_bytes_calls++;
      size_t requested = 0;
      if (expr->args.len > 1 && memory_parse_number_literal(expr->args.items[1], &requested)) summary->allocation_requested_bytes += requested;
    } else if (strcmp(callee, "std.mem.byteBuf") == 0) {
      summary->byte_buf_calls++;
      size_t requested = 0;
      if (expr->args.len > 1 && memory_parse_number_literal(expr->args.items[1], &requested)) summary->allocation_requested_bytes += requested;
    } else if (strcmp(callee, "std.mem.reset") == 0) summary->reset_calls++;
    else if (strcmp(callee, "std.mem.capacity") == 0) summary->capacity_calls++;
    else if (strcmp(callee, "std.mem.vec") == 0) {
      summary->vec_count++;
      size_t capacity = expr->args.len > 0 ? memory_byte_capacity_expr(scope, expr->args.items[0]) : 0;
      if (capacity > 0) summary->collection_capacity_bytes += capacity;
      else summary->unknown_capacity_sites++;
    } else if (strcmp(callee, "std.mem.vecPush") == 0) summary->vec_push_calls++;
    else if (strcmp(callee, "std.mem.vecLen") == 0) summary->vec_len_calls++;
    else if (strcmp(callee, "std.mem.vecCapacity") == 0) summary->vec_capacity_calls++;
    else if (strcmp(callee, "std.mem.mapEmpty") == 0) summary->map_empty_count++;
    else if (strcmp(callee, "std.mem.mapLen") == 0) summary->map_len_calls++;
    else if (strcmp(callee, "std.mem.setEmpty") == 0) summary->set_empty_count++;
    else if (strcmp(callee, "std.mem.setLen") == 0) summary->set_len_calls++;
    free(callee);
  }
  memory_model_collect_expr(expr->left, scope, summary);
  memory_model_collect_expr(expr->right, scope, summary);
  memory_model_collect_expr_vec(&expr->args, scope, summary);
  for (size_t i = 0; i < expr->fields.len; i++) memory_model_collect_expr(expr->fields.items[i].value, scope, summary);
}

static void memory_model_collect_stmt_vec(const StmtVec *body, MemoryScope *scope, MemoryModelSummary *summary) {
  if (!body || !summary) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->kind == STMT_LET) {
      const char *type = stmt->resolved_type ? stmt->resolved_type : stmt->type;
      size_t array_len = 0;
      size_t array_bytes = 0;
      bool is_u8 = false;
      if (memory_parse_fixed_array_type(type, &array_len, &array_bytes, &is_u8)) {
        (void)array_len;
        summary->stack_estimate_bytes += array_bytes;
        if (is_u8) memory_scope_note_array(scope, stmt->name, array_bytes);
      }
    }
    memory_model_collect_expr(stmt->target, scope, summary);
    memory_model_collect_expr(stmt->expr, scope, summary);
    memory_model_collect_expr(stmt->range_end, scope, summary);
    memory_model_collect_stmt_vec(&stmt->then_body, scope, summary);
    memory_model_collect_stmt_vec(&stmt->else_body, scope, summary);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      memory_model_collect_expr(stmt->match_arms.items[arm_index].guard, scope, summary);
      memory_model_collect_stmt_vec(&stmt->match_arms.items[arm_index].body, scope, summary);
    }
  }
}

static MemoryModelSummary memory_model_summary_from_program(const Program *program) {
  MemoryModelSummary summary = {0};
  if (!program) return summary;
  for (size_t i = 0; i < program->consts.len; i++) memory_model_collect_expr(program->consts.items[i].expr, &(MemoryScope){0}, &summary);
  for (size_t i = 0; i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      memory_model_collect_expr(program->shapes.items[i].fields.items[field_index].default_value, &(MemoryScope){0}, &summary);
    }
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    MemoryScope scope = {0};
    memory_model_collect_stmt_vec(&program->functions.items[i].body, &scope, &summary);
  }
  return summary;
}

static void collect_capabilities_from_std_name(const char *name, CapabilitySummary *caps) {
  if (!name || !caps) return;
  const ZStdHelperInfo *helper = z_std_helper_find(name);
  if (helper) {
    capability_summary_set(caps, helper->capability);
    return;
  }
  if (strncmp(name, "std.args.", strlen("std.args.")) == 0) caps->args = true;
  else if (strncmp(name, "std.env.", strlen("std.env.")) == 0) caps->env = true;
  else if (strncmp(name, "std.fs.", strlen("std.fs.")) == 0) caps->fs = true;
  else if (strncmp(name, "std.path.", strlen("std.path.")) == 0) caps->path = true;
  else if (strncmp(name, "std.codec.", strlen("std.codec.")) == 0) caps->codec = true;
  else if (strncmp(name, "std.parse.", strlen("std.parse.")) == 0) caps->parse = true;
  else if (strncmp(name, "std.json.", strlen("std.json.")) == 0) caps->parse = true;
  else if (strncmp(name, "std.time.", strlen("std.time.")) == 0) caps->time = true;
  else if (strncmp(name, "std.rand.", strlen("std.rand.")) == 0) caps->rand = true;
  else if (strncmp(name, "std.proc.", strlen("std.proc.")) == 0) caps->proc = true;
  else if (strncmp(name, "std.crypto.secureRandom", strlen("std.crypto.secureRandom")) == 0) caps->rand = true;
  else if (strncmp(name, "std.crypto.", strlen("std.crypto.")) == 0) caps->codec = true;
  else if (strncmp(name, "std.net.", strlen("std.net.")) == 0) caps->net = true;
  else if (strncmp(name, "std.http.", strlen("std.http.")) == 0) caps->net = true;
  else if (strncmp(name, "std.mem.", strlen("std.mem.")) == 0) {
    caps->memory = true;
    if (strcmp(name, "std.mem.nullAlloc") == 0 ||
        strcmp(name, "std.mem.fixedBufAlloc") == 0 ||
        strcmp(name, "std.mem.arena") == 0 ||
        strcmp(name, "std.mem.allocBytes") == 0 ||
        strcmp(name, "std.mem.byteBuf") == 0 ||
        strcmp(name, "std.mem.reset") == 0 ||
        strcmp(name, "std.mem.capacity") == 0) caps->alloc = true;
  }
}

static void collect_capabilities_from_expr(const Expr *expr, CapabilitySummary *caps) {
  if (!expr || !caps) return;
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    collect_capabilities_from_std_name(callee, caps);
    free(callee);
  }
  collect_capabilities_from_expr(expr->left, caps);
  collect_capabilities_from_expr(expr->right, caps);
  for (size_t i = 0; i < expr->args.len; i++) collect_capabilities_from_expr(expr->args.items[i], caps);
  for (size_t i = 0; i < expr->fields.len; i++) collect_capabilities_from_expr(expr->fields.items[i].value, caps);
}

static void collect_helpers_from_expr(const Expr *expr, HelperUseSummary *helpers) {
  if (!expr || !helpers) return;
  if (expr->kind == EXPR_CALL) {
    char *callee = expr_callee_name(expr->left);
    helper_summary_mark(helpers, callee);
    free(callee);
  }
  collect_helpers_from_expr(expr->left, helpers);
  collect_helpers_from_expr(expr->right, helpers);
  for (size_t i = 0; i < expr->args.len; i++) collect_helpers_from_expr(expr->args.items[i], helpers);
  for (size_t i = 0; i < expr->fields.len; i++) collect_helpers_from_expr(expr->fields.items[i].value, helpers);
}

static void collect_capabilities_from_stmt_vec(const StmtVec *body, CapabilitySummary *caps) {
  if (!body || !caps) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    collect_capabilities_from_expr(stmt->target, caps);
    collect_capabilities_from_expr(stmt->expr, caps);
    collect_capabilities_from_expr(stmt->range_end, caps);
    collect_capabilities_from_stmt_vec(&stmt->then_body, caps);
    collect_capabilities_from_stmt_vec(&stmt->else_body, caps);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      collect_capabilities_from_stmt_vec(&stmt->match_arms.items[arm_index].body, caps);
    }
  }
}

static void collect_helpers_from_stmt_vec(const StmtVec *body, HelperUseSummary *helpers) {
  if (!body || !helpers) return;
  for (size_t i = 0; i < body->len; i++) {
    const Stmt *stmt = body->items[i];
    collect_helpers_from_expr(stmt->target, helpers);
    collect_helpers_from_expr(stmt->expr, helpers);
    collect_helpers_from_expr(stmt->range_end, helpers);
    collect_helpers_from_stmt_vec(&stmt->then_body, helpers);
    collect_helpers_from_stmt_vec(&stmt->else_body, helpers);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      collect_helpers_from_stmt_vec(&stmt->match_arms.items[arm_index].body, helpers);
    }
  }
}

static CapabilitySummary function_capabilities(const Function *fun) {
  CapabilitySummary caps = {0};
  for (size_t i = 0; fun && i < fun->params.len; i++) {
    const char *type = fun->params.items[i].type;
    if (strcmp(type, "World") == 0) caps.world = true;
    else if (strcmp(type, "Fs") == 0) caps.fs = true;
    else if (strcmp(type, "Net") == 0) caps.net = true;
    else if (strcmp(type, "Proc") == 0) caps.proc = true;
    else if (strcmp(type, "Clock") == 0) caps.time = true;
    else if (strcmp(type, "Rand") == 0) caps.rand = true;
    else if (strcmp(type, "Alloc") == 0 || strcmp(type, "FixedBufAlloc") == 0 || strcmp(type, "NullAlloc") == 0) capability_summary_set(&caps, "alloc");
    if (strstr(type, "Span<") || strstr(type, "MutSpan<") || strstr(type, "ByteBuf")) caps.memory = true;
  }
  if (fun) collect_capabilities_from_stmt_vec(&fun->body, &caps);
  return caps;
}

static CapabilitySummary program_capabilities(const Program *program) {
  CapabilitySummary caps = {0};
  for (size_t i = 0; program && i < program->functions.len; i++) {
    CapabilitySummary fun_caps = function_capabilities(&program->functions.items[i]);
    caps.args = caps.args || fun_caps.args;
    caps.env = caps.env || fun_caps.env;
    caps.fs = caps.fs || fun_caps.fs;
    caps.memory = caps.memory || fun_caps.memory;
    caps.alloc = caps.alloc || fun_caps.alloc;
    caps.path = caps.path || fun_caps.path;
    caps.codec = caps.codec || fun_caps.codec;
    caps.parse = caps.parse || fun_caps.parse;
    caps.time = caps.time || fun_caps.time;
    caps.rand = caps.rand || fun_caps.rand;
    caps.net = caps.net || fun_caps.net;
    caps.proc = caps.proc || fun_caps.proc;
    caps.web = caps.web || fun_caps.web;
    caps.world = caps.world || fun_caps.world;
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      collect_capabilities_from_expr(program->shapes.items[i].fields.items[field_index].default_value, &caps);
    }
    for (size_t method_index = 0; method_index < program->shapes.items[i].methods.len; method_index++) {
      CapabilitySummary fun_caps = function_capabilities(&program->shapes.items[i].methods.items[method_index]);
      caps.args = caps.args || fun_caps.args;
      caps.env = caps.env || fun_caps.env;
      caps.fs = caps.fs || fun_caps.fs;
      caps.memory = caps.memory || fun_caps.memory;
      caps.alloc = caps.alloc || fun_caps.alloc;
      caps.path = caps.path || fun_caps.path;
      caps.codec = caps.codec || fun_caps.codec;
      caps.parse = caps.parse || fun_caps.parse;
      caps.time = caps.time || fun_caps.time;
      caps.rand = caps.rand || fun_caps.rand;
      caps.net = caps.net || fun_caps.net;
      caps.proc = caps.proc || fun_caps.proc;
      caps.web = caps.web || fun_caps.web;
      caps.world = caps.world || fun_caps.world;
    }
  }
  return caps;
}

static HelperUseSummary program_used_helpers(const Program *program) {
  HelperUseSummary helpers = {0};
  for (size_t i = 0; program && i < program->functions.len; i++) {
    collect_helpers_from_stmt_vec(&program->functions.items[i].body, &helpers);
  }
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    for (size_t field_index = 0; field_index < program->shapes.items[i].fields.len; field_index++) {
      collect_helpers_from_expr(program->shapes.items[i].fields.items[field_index].default_value, &helpers);
    }
    for (size_t method_index = 0; method_index < program->shapes.items[i].methods.len; method_index++) {
      collect_helpers_from_stmt_vec(&program->shapes.items[i].methods.items[method_index].body, &helpers);
    }
  }
  return helpers;
}

static bool target_is_host(const ZTargetInfo *target) {
  return z_target_is_host(target);
}

static bool validate_target_capabilities(const Program *program, const ZTargetInfo *target, ZDiag *diag, const char *path) {
  CapabilitySummary caps = program_capabilities(program);
#define DENY_CAP(field, cap_name, label) do { \
    if (caps.field && !z_target_has_capability(target, cap_name)) { \
      diag->code = 6002; \
      diag->path = path; \
      diag->line = 1; \
      diag->column = 1; \
      diag->length = 1; \
      snprintf(diag->message, sizeof(diag->message), "target does not provide required %s capability", label); \
      snprintf(diag->expected, sizeof(diag->expected), "target with %s capability", label); \
      snprintf(diag->actual, sizeof(diag->actual), "target %s lacks %s", target ? target->name : "<unknown>", label); \
      if (strcmp(cap_name, "fs") == 0) snprintf(diag->help, sizeof(diag->help), "build for host target %s or remove hosted std.fs usage from this target-neutral build", z_host_target()); \
      else snprintf(diag->help, sizeof(diag->help), "build for a target that declares %s or remove that capability from this target-neutral entry point", label); \
      return false; \
    } \
  } while (0)
  DENY_CAP(fs, "fs", "Fs");
  DENY_CAP(args, "args", "Args");
  DENY_CAP(env, "env", "Env");
  DENY_CAP(time, "time", "Clock");
  DENY_CAP(rand, "rand", "Rand");
  DENY_CAP(net, "net", "Net");
  DENY_CAP(proc, "proc", "Proc");
  DENY_CAP(web, "web", "Web");
#undef DENY_CAP
  if (caps.world && !z_target_has_capability(target, "stdio")) {
    diag->code = 6002;
    diag->path = path;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "target does not provide World stdio capability");
    snprintf(diag->expected, sizeof(diag->expected), "target with stdio capability");
    snprintf(diag->actual, sizeof(diag->actual), "target %s lacks stdio", target ? target->name : "<unknown>");
    snprintf(diag->help, sizeof(diag->help), "select a target with stdio or use a narrower capability");
    return false;
  }
  return true;
}

static int abi_pointer_size(const ZTargetInfo *target) {
  (void)target;
  return 8;
}

static int abi_type_size(const char *type, const ZTargetInfo *target) {
  if (!type) return 0;
  if (strcmp(type, "Bool") == 0 || strcmp(type, "u8") == 0 || strcmp(type, "i8") == 0 || strcmp(type, "char") == 0) return 1;
  if (strcmp(type, "u16") == 0 || strcmp(type, "i16") == 0) return 2;
  if (strcmp(type, "u32") == 0 || strcmp(type, "i32") == 0 || strcmp(type, "f32") == 0) return 4;
  if (strcmp(type, "u64") == 0 || strcmp(type, "i64") == 0 || strcmp(type, "f64") == 0) return 8;
  if (strcmp(type, "usize") == 0 || strcmp(type, "isize") == 0 || strncmp(type, "ref<", 4) == 0 || strncmp(type, "mutref<", 7) == 0) return abi_pointer_size(target);
  return 0;
}

static int abi_type_align(const char *type, const ZTargetInfo *target) {
  int size = abi_type_size(type, target);
  int pointer = abi_pointer_size(target);
  if (size > pointer) return pointer;
  return size > 0 ? size : 1;
}

static int align_to_int(int value, int align) {
  if (align <= 1) return value;
  int remainder = value % align;
  return remainder == 0 ? value : value + (align - remainder);
}

static void append_abi_primitives_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *types[] = {"Bool", "u8", "i8", "u16", "i16", "u32", "i32", "u64", "i64", "usize", "isize", "f32", "f64", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; types[i]; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, types[i]);
    zbuf_appendf(buf, ",\"size\":%d,\"align\":%d}", abi_type_size(types[i], target), abi_type_align(types[i], target));
  }
  zbuf_append(buf, "]");
}

static void append_abi_shapes_json(ZBuf *buf, const Program *program, const ZTargetInfo *target) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; program && i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    if (!shape->layout || strcmp(shape->layout, "extern") != 0) continue;
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    int offset = 0;
    int max_align = 1;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, shape->name);
    zbuf_append(buf, ",\"layout\":\"extern\",\"fields\":[");
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      const Param *field = &shape->fields.items[field_index];
      int align = abi_type_align(field->type, target);
      int size = abi_type_size(field->type, target);
      offset = align_to_int(offset, align);
      if (align > max_align) max_align = align;
      if (field_index > 0) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, field->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, field->type);
      zbuf_appendf(buf, ",\"offset\":%d,\"size\":%d,\"align\":%d}", offset, size, align);
      offset += size;
    }
    zbuf_appendf(buf, "],\"size\":%d,\"align\":%d}", align_to_int(offset, max_align), max_align);
  }
  zbuf_append(buf, "]");
}

static void append_abi_enums_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->enums.len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, program->enums.items[i].name);
    zbuf_appendf(buf, ",\"size\":4,\"align\":4,\"cases\":%zu}", program->enums.items[i].cases.len);
  }
  zbuf_append(buf, "]");
}

static const char *c_abi_type_name(const char *zero_type) {
  if (!zero_type || strcmp(zero_type, "Void") == 0) return "void";
  if (strcmp(zero_type, "Bool") == 0) return "bool";
  if (strcmp(zero_type, "u8") == 0) return "uint8_t";
  if (strcmp(zero_type, "i8") == 0) return "int8_t";
  if (strcmp(zero_type, "u16") == 0) return "uint16_t";
  if (strcmp(zero_type, "i16") == 0) return "int16_t";
  if (strcmp(zero_type, "u32") == 0) return "uint32_t";
  if (strcmp(zero_type, "i32") == 0) return "int32_t";
  if (strcmp(zero_type, "u64") == 0) return "uint64_t";
  if (strcmp(zero_type, "i64") == 0) return "int64_t";
  if (strcmp(zero_type, "usize") == 0) return "uintptr_t";
  if (strcmp(zero_type, "isize") == 0) return "intptr_t";
  if (strcmp(zero_type, "f32") == 0) return "float";
  if (strcmp(zero_type, "f64") == 0) return "double";
  return "void *";
}

static void append_c_exports_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->export_c) continue;
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, fun->name);
    zbuf_append(buf, ",\"returnType\":");
    append_json_string(buf, fun->return_type ? fun->return_type : "Void");
    zbuf_append(buf, ",\"cReturnType\":");
    append_json_string(buf, c_abi_type_name(fun->return_type));
    zbuf_append(buf, ",\"raises\":");
    zbuf_append(buf, fun->raises ? "true" : "false");
    zbuf_append(buf, ",\"params\":[");
    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
      const Param *param = &fun->params.items[param_index];
      if (param_index > 0) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, param->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, param->type);
      zbuf_append(buf, ",\"cType\":");
      append_json_string(buf, c_abi_type_name(param->type));
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "]");
}

static void append_c_export_header_text(ZBuf *header, const Program *program) {
  zbuf_append(header, "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n\n");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->export_c) continue;
    zbuf_append(header, c_abi_type_name(fun->return_type));
    zbuf_append_char(header, ' ');
    zbuf_append(header, fun->name);
    zbuf_append_char(header, '(');
    if (fun->params.len == 0) {
      zbuf_append(header, "void");
    } else {
      for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
        const Param *param = &fun->params.items[param_index];
        if (param_index > 0) zbuf_append(header, ", ");
        zbuf_append(header, c_abi_type_name(param->type));
        zbuf_append_char(header, ' ');
        zbuf_append(header, param->name);
      }
    }
    zbuf_append(header, ");\n");
  }
}

static void append_c_export_header_json(ZBuf *buf, const Program *program) {
  ZBuf header;
  zbuf_init(&header);
  append_c_export_header_text(&header, program);
  size_t export_count = 0;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    if (program->functions.items[i].export_c) export_count++;
  }
  zbuf_appendf(buf, "{\"available\":%s,\"format\":\"c-header\",\"exportCount\":%zu,\"text\":", export_count > 0 ? "true" : "false", export_count);
  append_json_string(buf, header.data ? header.data : "");
  zbuf_append(buf, "}");
  zbuf_free(&header);
}

static void append_c_imports_json(ZBuf *buf, const Program *program, const ZTargetInfo *target);

static void append_abi_dump_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\n  \"pointerSize\": ");
  zbuf_appendf(buf, "%d", abi_pointer_size(target));
  zbuf_append(buf, ",\n  \"objectFormat\": ");
  append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\n  \"callingConvention\": ");
  append_json_string(buf, target && target->abi ? target->abi : "host");
  zbuf_append(buf, ",\n  \"primitiveLayouts\": ");
  append_abi_primitives_json(buf, target);
  zbuf_append(buf, ",\n  \"externShapes\": ");
  append_abi_shapes_json(buf, program, target);
  zbuf_append(buf, ",\n  \"enums\": ");
  append_abi_enums_json(buf, program);
  zbuf_append(buf, ",\n  \"cImports\": ");
  append_c_imports_json(buf, program, target);
  zbuf_append(buf, ",\n  \"cExports\": ");
  append_c_exports_json(buf, program);
  zbuf_append(buf, ",\n  \"generatedHeader\": ");
  append_c_export_header_json(buf, program);
  zbuf_append(buf, "\n}\n");
}

static uint64_t fnv1a_text(const char *text) {
  uint64_t hash = 1469598103934665603ull;
  for (const unsigned char *cursor = (const unsigned char *)(text ? text : ""); *cursor; cursor++) {
    hash ^= (uint64_t)*cursor;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t fnv1a_bytes(const unsigned char *data, size_t len) {
  uint64_t hash = 1469598103934665603ull;
  for (size_t i = 0; i < len; i++) {
    hash ^= (uint64_t)data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

static char *read_optional_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    return NULL;
  }
  long size = ftell(file);
  if (size < 0) {
    fclose(file);
    return NULL;
  }
  rewind(file);
  char *data = z_checked_calloc((size_t)size + 1, 1);
  if (size > 0 && fread(data, 1, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  return data;
}

static void append_hash_json(ZBuf *buf, const char *key, uint64_t hash) {
  zbuf_appendf(buf, ",\n    \"%s\": \"%016llx\"", key, (unsigned long long)hash);
}

static uint64_t mix_hash_text(uint64_t hash, const char *text) {
  hash ^= fnv1a_text(text ? text : "");
  hash *= 1099511628211ull;
  return hash;
}

static uint64_t source_interface_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("interface");
  if (!input) return hash;
  for (size_t i = 0; i < input->module_count; i++) {
    hash = mix_hash_text(hash, input->module_names[i]);
    hash = mix_hash_text(hash, input->module_paths[i]);
  }
  for (size_t i = 0; i < input->symbol_count; i++) {
    hash = mix_hash_text(hash, input->symbol_names[i]);
    hash = mix_hash_text(hash, input->symbol_modules[i]);
    hash = mix_hash_text(hash, input->symbol_kinds[i]);
    hash ^= input->symbol_public[i] ? 0xa5a5a5a5u : 0x5a5a5a5au;
  }
  return hash;
}

static uint64_t source_dependency_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("dependencies");
  if (!input) return hash;
  hash = mix_hash_text(hash, input->package_name);
  hash = mix_hash_text(hash, input->package_version);
  hash = mix_hash_text(hash, input->manifest_path);
  hash ^= input->manifest_hash;
  hash *= 1099511628211ull;
  hash ^= input->dependency_graph_hash;
  hash *= 1099511628211ull;
  hash ^= input->lockfile_hash;
  hash *= 1099511628211ull;
  for (size_t i = 0; i < input->source_file_count; i++) hash = mix_hash_text(hash, input->source_files[i]);
  for (size_t i = 0; i < input->import_edge_count; i++) {
    hash = mix_hash_text(hash, input->import_from[i]);
    hash = mix_hash_text(hash, input->import_to[i]);
    hash = mix_hash_text(hash, input->import_paths[i]);
  }
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash = mix_hash_text(hash, dep->resolved_name);
    hash = mix_hash_text(hash, dep->resolved_version);
    hash = mix_hash_text(hash, dep->resolved_manifest);
    hash = mix_hash_text(hash, dep->status);
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t source_imported_package_metadata_hash(const SourceInput *input) {
  uint64_t hash = fnv1a_text("imported-package-metadata");
  if (!input) return hash;
  hash = mix_hash_text(hash, input->package_name);
  hash = mix_hash_text(hash, input->package_version);
  hash = mix_hash_text(hash, input->manifest_path);
  hash ^= input->manifest_hash;
  hash *= 1099511628211ull;
  hash ^= input->dependency_graph_hash;
  hash *= 1099511628211ull;
  hash ^= input->lockfile_hash;
  hash *= 1099511628211ull;
  for (size_t i = 0; i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    hash = mix_hash_text(hash, dep->name);
    hash = mix_hash_text(hash, dep->resolved_name);
    hash = mix_hash_text(hash, dep->resolved_version);
    hash = mix_hash_text(hash, dep->resolved_manifest);
    hash = mix_hash_text(hash, dep->targets_json);
    hash ^= dep->fingerprint;
    hash *= 1099511628211ull;
  }
  return hash;
}

static uint64_t source_target_facts_hash(const ZTargetInfo *target) {
  uint64_t hash = fnv1a_text("target-facts");
  hash = mix_hash_text(hash, target ? target->name : z_host_target());
  hash = mix_hash_text(hash, target ? target->arch : "");
  hash = mix_hash_text(hash, target ? target->os : "");
  hash = mix_hash_text(hash, target ? target->abi : "");
  hash = mix_hash_text(hash, target ? target->libc_mode : "");
  hash = mix_hash_text(hash, target ? target->object_format : "");
  hash = mix_hash_text(hash, target ? target->capabilities : "");
  return hash;
}

static uint64_t source_file_hash_for_path(const SourceInput *input, const char *path) {
  char *source = read_optional_file(path);
  const char *text = source ? source : ((input && input->source_file && path && strcmp(path, input->source_file) == 0 && input->source) ? input->source : path);
  uint64_t hash = fnv1a_text(text ? text : "");
  free(source);
  return hash;
}

static size_t module_public_symbol_count(const SourceInput *input, const char *module_name) {
  size_t count = 0;
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (!input->symbol_modules || strcmp(input->symbol_modules[i], module_name) != 0) continue;
    count++;
  }
  return count;
}

static uint64_t module_public_interface_hash(const SourceInput *input, const char *module_name) {
  uint64_t hash = fnv1a_text("module-public-interface");
  hash = mix_hash_text(hash, module_name);
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (!input->symbol_modules || strcmp(input->symbol_modules[i], module_name) != 0) continue;
    hash = mix_hash_text(hash, input->symbol_names[i]);
    hash = mix_hash_text(hash, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
  }
  return hash;
}

static void append_interface_fingerprints_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"schemaVersion\":1,\"algorithm\":\"fnv1a64-zero-interface-v1\",\"targetFactsHash\":\"");
  zbuf_appendf(buf, "%016llx", (unsigned long long)source_target_facts_hash(target));
  zbuf_append(buf, "\",\"importedPackageMetadataHash\":\"");
  zbuf_appendf(buf, "%016llx", (unsigned long long)source_imported_package_metadata_hash(input));
  zbuf_append(buf, "\",\"modules\":[");
  for (size_t i = 0; input && i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ",");
    const char *module_name = input->module_names[i];
    const char *module_path = input->module_paths[i];
    uint64_t source_hash = source_file_hash_for_path(input, module_path);
    uint64_t interface_hash = module_public_interface_hash(input, module_name);
    size_t public_count = module_public_symbol_count(input, module_name);
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, module_name);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, module_path);
    zbuf_appendf(buf, ",\"sourceHash\":\"%016llx\",\"publicInterfaceHash\":\"%016llx\",\"publicSymbolCount\":%zu,\"publicSymbols\":[",
                 (unsigned long long)source_hash,
                 (unsigned long long)interface_hash,
                 public_count);
    bool wrote_symbol = false;
    for (size_t j = 0; j < input->symbol_count; j++) {
      if (!input->symbol_public[j]) continue;
      if (!input->symbol_modules || strcmp(input->symbol_modules[j], module_name) != 0) continue;
      if (wrote_symbol) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, input->symbol_names[j]);
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[j] : "unknown");
      zbuf_append(buf, "}");
      wrote_symbol = true;
    }
    zbuf_append(buf, "],\"imports\":[");
    bool wrote_import = false;
    for (size_t j = 0; j < input->import_edge_count; j++) {
      if (strcmp(input->import_from[j], module_name) != 0) continue;
      if (wrote_import) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"module\":");
      append_json_string(buf, input->import_to[j]);
      zbuf_append(buf, ",\"path\":");
      append_json_string(buf, input->import_paths[j]);
      zbuf_append(buf, "}");
      wrote_import = true;
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "]}");
}

static uint64_t compile_cache_key(const SourceInput *input, const ZTargetInfo *target, const char *profile, const char *kind) {
  uint64_t hash = fnv1a_text(kind);
  hash = mix_hash_text(hash, ZERO_VERSION);
  hash = mix_hash_text(hash, input ? input->source : "");
  hash = mix_hash_text(hash, target ? target->name : z_host_target());
  hash = mix_hash_text(hash, profile ? profile : "release");
  hash ^= source_dependency_hash(input);
  return hash;
}

static bool json_array_contains_string_literal(const char *json, const char *value) {
  if (!json || !value || !value[0]) return false;
  ZBuf needle;
  zbuf_init(&needle);
  zbuf_append_char(&needle, '"');
  zbuf_append(&needle, value);
  zbuf_append_char(&needle, '"');
  bool found = needle.data && strstr(json, needle.data) != NULL;
  zbuf_free(&needle);
  return found;
}

static bool json_array_nonempty_literal(const char *json) {
  if (!json) return false;
  for (const char *cursor = json; *cursor; cursor++) {
    if (*cursor == '"') return true;
  }
  return false;
}

static bool compiler_cache_touch(const char *kind, uint64_t key) {
  zero_mkdir(".zero");
  zero_mkdir(".zero/cache");
  zero_mkdir(".zero/cache/native");
  char path[256];
  snprintf(path, sizeof(path), ".zero/cache/native/%s-%016llx.cache", kind, (unsigned long long)key);
  bool hit = path_exists(path);
  FILE *file = fopen(path, "wb");
  if (file) {
    fprintf(file, "%s %016llx\n", kind, (unsigned long long)key);
    fclose(file);
  }
  return hit;
}

static void append_compiler_phases_json(ZBuf *buf, const SourceInput *input) {
  zbuf_append(buf, "[");
#define APPEND_PHASE(name, field, cacheable) do { \
    if (strcmp(name, "resolve") != 0) zbuf_append(buf, ", "); \
    zbuf_append(buf, "{\"name\":"); \
    append_json_string(buf, name); \
    zbuf_appendf(buf, ",\"elapsedMs\":%lld,\"cacheable\":%s}", input ? input->field : 0, cacheable ? "true" : "false"); \
  } while (0)
  APPEND_PHASE("resolve", resolve_ms, true);
  APPEND_PHASE("parse", parse_ms, true);
  APPEND_PHASE("interface", interface_ms, true);
  APPEND_PHASE("check", check_ms, true);
  APPEND_PHASE("lower", lower_ms, true);
  APPEND_PHASE("codegen", codegen_ms, true);
  APPEND_PHASE("object", object_ms, true);
  APPEND_PHASE("link", link_ms, false);
#undef APPEND_PHASE
  zbuf_append(buf, "]");
}

static void append_compiler_caches_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) {
  uint64_t parse_key = compile_cache_key(input, NULL, NULL, "parse-tree");
  uint64_t interface_key = source_interface_hash(input);
  uint64_t check_key = compile_cache_key(input, target, NULL, "checked-body");
  uint64_t specialization_key = compile_cache_key(input, target, profile, "specialization");
  uint64_t object_key = compile_cache_key(input, target, profile, "emitted-object");
  zbuf_append(buf, "[");
#define APPEND_CACHE(label, key, hit, invalidates) do { \
    if (wrote) zbuf_append(buf, ", "); \
    zbuf_append(buf, "{\"name\":"); \
    append_json_string(buf, label); \
    zbuf_appendf(buf, ",\"key\":\"%016llx\",\"hit\":%s,\"stored\":true,\"compilerVersion\":\"%s\",\"packageVersion\":", (unsigned long long)(key), (hit) ? "true" : "false", ZERO_VERSION); \
    append_json_string(buf, input && input->package_version ? input->package_version : ""); \
    zbuf_appendf(buf, ",\"dependencyGraphHash\":\"%016llx\",\"lockfileHash\":\"%016llx\",\"invalidatesOn\":", \
                 (unsigned long long)(input ? input->dependency_graph_hash : 0), \
                 (unsigned long long)(input ? input->lockfile_hash : 0)); \
    append_json_string(buf, invalidates); \
    zbuf_append(buf, "}"); \
    wrote = true; \
  } while (0)
  bool wrote = false;
  APPEND_CACHE("parseTree", parse_key, input && input->parse_cache_hit, "source");
  APPEND_CACHE("interface", interface_key, input && input->interface_cache_hit, "public symbols/import graph");
  APPEND_CACHE("checkedBody", check_key, input && input->check_cache_hit, "source or target");
  APPEND_CACHE("specialization", specialization_key, input && input->specialization_cache_hit, "source, target, or profile");
  APPEND_CACHE("emittedObject", object_key, input && input->emitted_object_cache_hit, "source, target, profile, or backend");
#undef APPEND_CACHE
  zbuf_append(buf, "]");
}

static void source_cache_hit_miss_counts(const SourceInput *input, size_t *hits, size_t *misses) {
  *hits = 0;
  *misses = 0;
#define COUNT_CACHE_HIT(field) do { if (input && input->field) (*hits)++; else (*misses)++; } while (0)
  COUNT_CACHE_HIT(parse_cache_hit);
  COUNT_CACHE_HIT(interface_cache_hit);
  COUNT_CACHE_HIT(check_cache_hit);
  COUNT_CACHE_HIT(specialization_cache_hit);
  COUNT_CACHE_HIT(emitted_object_cache_hit);
#undef COUNT_CACHE_HIT
}

static void append_incremental_invalidations_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) {
  size_t hits = 0;
  size_t misses = 0;
  source_cache_hit_miss_counts(input, &hits, &misses);
  zbuf_append(buf, "{\"moduleDependencies\":");
  zbuf_append(buf, "[");
  for (size_t i = 0; input && i < input->import_edge_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, input->import_from[i]);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, input->import_to[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->import_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\"targetDependency\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"profileDependency\":");
  append_json_string(buf, profile ? profile : "release");
  zbuf_appendf(buf, ",\"affectedModules\":%zu,\"recheckStrategy\":\"fingerprint changed modules and dependent bodies\"", input ? input->module_count : 0);
  zbuf_append(buf, ",\"changedInputs\":{\"sourceFiles\":[");
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "],\"manifestPath\":");
  append_json_string(buf, input && input->manifest_path ? input->manifest_path : "");
  zbuf_append(buf, ",\"packageLockfile\":");
  append_json_string(buf, input && input->lockfile_path ? input->lockfile_path : "");
  zbuf_append(buf, "},\"cacheHits\":");
  zbuf_appendf(buf, "%zu", hits);
  zbuf_append(buf, ",\"cacheMisses\":");
  zbuf_appendf(buf, "%zu", misses);
  zbuf_append(buf, ",\"partialDiagnosticsStable\":true,\"interfaceFingerprints\":");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, "}");
}

static bool package_dependency_target_compatible(const SourceDependency *dep, const ZTargetInfo *target) {
  if (!dep || !json_array_nonempty_literal(dep->targets_json)) return true;
  return json_array_contains_string_literal(dep->targets_json, target ? target->name : z_host_target()) ||
         json_array_contains_string_literal(dep->targets_json, target && target->zig_target ? target->zig_target : "") ||
         json_array_contains_string_literal(dep->targets_json, z_host_target());
}

static void append_package_metadata_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, input && input->package_name ? input->package_name : "");
  zbuf_append(buf, ",\"version\":");
  append_json_string(buf, input && input->package_version ? input->package_version : "");
  zbuf_append(buf, ",\"root\":");
  append_json_string(buf, input && input->package_root ? input->package_root : "");
  zbuf_append(buf, ",\"manifestPath\":");
  append_json_string(buf, input && input->manifest_path ? input->manifest_path : "");
  zbuf_appendf(buf, ",\"manifestHash\":\"%016llx\",\"dependencyGraphHash\":\"%016llx\"",
               (unsigned long long)(input ? input->manifest_hash : 0),
               (unsigned long long)(input ? input->dependency_graph_hash : 0));
  zbuf_append(buf, ",\"lockfile\":{\"format\":\"zero-lock-v1\",\"path\":");
  append_json_string(buf, input && input->lockfile_path ? input->lockfile_path : "");
  zbuf_appendf(buf, ",\"hash\":\"%016llx\",\"generated\":%s}",
               (unsigned long long)(input ? input->lockfile_hash : 0),
               input && input->lockfile_path ? "true" : "false");
  zbuf_append(buf, ",\"resolver\":{\"deterministic\":true,\"registryReadyMetadata\":true,\"remoteFetch\":false,\"versionSolver\":\"exact-version-or-path\"}");
  zbuf_append(buf, ",\"dependencies\":[");
  for (size_t i = 0; input && i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, dep->name);
    zbuf_append(buf, ",\"version\":");
    append_json_string(buf, dep->version);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, dep->path);
    zbuf_append(buf, ",\"resolvedManifest\":");
    append_json_string(buf, dep->resolved_manifest);
    zbuf_append(buf, ",\"resolvedName\":");
    append_json_string(buf, dep->resolved_name);
    zbuf_append(buf, ",\"resolvedVersion\":");
    append_json_string(buf, dep->resolved_version);
    zbuf_append(buf, ",\"status\":");
    append_json_string(buf, dep->status);
    zbuf_appendf(buf, ",\"direct\":%s,\"fingerprint\":\"%016llx\",\"targetCompatible\":%s,\"targets\":",
                 dep->direct ? "true" : "false",
                 (unsigned long long)dep->fingerprint,
                 package_dependency_target_compatible(dep, target) ? "true" : "false");
    zbuf_append(buf, dep->targets_json ? dep->targets_json : "[]");
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]}");
}

static void append_package_cache_audit_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const char *profile) {
  zbuf_append(buf, "{\"cacheKeyInputs\":{\"compilerVersion\":");
  append_json_string(buf, ZERO_VERSION);
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"packageVersion\":");
  append_json_string(buf, input && input->package_version ? input->package_version : "");
  zbuf_appendf(buf, ",\"dependencyGraphHash\":\"%016llx\",\"manifestHash\":\"%016llx\",\"lockfileHash\":\"%016llx\"}",
               (unsigned long long)(input ? input->dependency_graph_hash : 0),
               (unsigned long long)(input ? input->manifest_hash : 0),
               (unsigned long long)(input ? input->lockfile_hash : 0));
  zbuf_append(buf, ",\"invalidationReasons\":[\"source changed\",\"manifest changed\",\"dependency graph changed\",\"target changed\",\"profile changed\",\"compiler version changed\"],\"profile\":");
  append_json_string(buf, profile ? profile : "release");
  zbuf_append(buf, "}");
}

static void append_self_host_routing_json(ZBuf *buf, const char *command_name, const char *emit_kind, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target);
static void append_target_capability_facts_json(ZBuf *buf, const ZTargetInfo *target, const CapabilitySummary *caps);
static void append_target_readiness_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command);
static const char *emit_kind_name(EmitKind emit);
static void append_backend_blocker_json(ZBuf *buf, const ZBackendBlocker *blocker);
static void complete_backend_blocker_diag(ZDiag *diag, const ZTargetInfo *target, const Command *command, const char *emit_kind, const char *stage);
static void init_direct_backend_diag(ZDiag *diag, const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason);
static void init_lowering_backend_diag(ZDiag *diag, const SourceInput *input, const ZTargetInfo *target, const Command *command, const IrProgram *ir);
static const char *direct_emit_emitter(const ZTargetInfo *target, const Command *command, const char *emit_kind);
static void append_used_stdlib_helpers_json(ZBuf *buf, const HelperUseSummary *helpers);
static void append_runtime_shims_json(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps);
static const Function *find_program_function(const Program *program, const char *name);
static void append_function_effects_json(ZBuf *buf, const Function *fun);
static void append_function_ownership_json(ZBuf *buf, const Function *fun);

static const char *static_param_kind_json(const Program *program, const char *type) {
  if (type && strcmp(type, "Bool") == 0) return "bool";
  if (type) {
    for (size_t i = 0; program && i < program->enums.len; i++) {
      if (strcmp(program->enums.items[i].name, type) == 0) return "enum";
    }
  }
  return "integer";
}

static void append_type_param_names_json(ZBuf *buf, const ParamVec *params) {
  zbuf_append(buf, "[");
  for (size_t i = 0; params && i < params->len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, params->items[i].name);
  }
  zbuf_append(buf, "]");
}

static void append_static_params_json(ZBuf *buf, const Program *program, const ParamVec *params) {
  zbuf_append(buf, "[");
  bool wrote_static_param = false;
  for (size_t i = 0; params && i < params->len; i++) {
    const Param *type_param = &params->items[i];
    if (!type_param->is_static) continue;
    if (wrote_static_param) zbuf_append(buf, ",");
    wrote_static_param = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, type_param->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, type_param->type ? type_param->type : "usize");
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, static_param_kind_json(program, type_param->type));
    zbuf_append(buf, ",\"staticDispatch\":true}");
  }
  zbuf_append(buf, "]");
}

static void append_type_param_constraints_json(ZBuf *buf, const ParamVec *params) {
  zbuf_append(buf, "[");
  bool wrote_constraint = false;
  for (size_t i = 0; params && i < params->len; i++) {
    const Param *type_param = &params->items[i];
    if (type_param->is_static) continue;
    if (!type_param->type || strcmp(type_param->type, "Type") == 0) continue;
    if (wrote_constraint) zbuf_append(buf, ",");
    wrote_constraint = true;
    zbuf_append(buf, "{\"typeParam\":");
    append_json_string(buf, type_param->name);
    zbuf_append(buf, ",\"interface\":");
    append_json_string(buf, type_param->type);
    zbuf_append(buf, ",\"staticDispatch\":true}");
  }
  zbuf_append(buf, "]");
}

static void append_compile_time_json(ZBuf *buf, const Program *program, const SourceInput *input, const ZTargetInfo *target) {
  char *manifest = read_optional_file(input && input->manifest_path ? input->manifest_path : "zero.json");
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"deterministic\":true,\"releaseMetadataDefault\":false");
  zbuf_append(buf, ",\"sandbox\":{\"filesystem\":\"denied\",\"network\":\"denied\",\"ambientEnv\":\"denied\",\"process\":\"denied\"}");
  zbuf_append(buf, ",\"limits\":{\"maxDepth\":64,\"maxSteps\":1024,\"stringBytes\":127,\"memory\":\"bounded-evaluator-state-only\",\"time\":\"step-counted\"}");
  zbuf_append(buf, ",\"cacheKeyInputs\":{\"algorithm\":\"fnv1a64-zero-meta-v1\"");
  append_hash_json(buf, "sourceHash", fnv1a_text(input ? input->source : ""));
  append_hash_json(buf, "targetHash", fnv1a_text(target ? target->name : z_host_target()));
  append_hash_json(buf, "manifestHash", fnv1a_text(manifest ? manifest : ""));
  zbuf_append(buf, ",\"compilerVersion\":");
  append_json_string(buf, ZERO_VERSION);
  zbuf_append(buf, ",\"declaredInputs\":[\"source\",\"target\",\"compilerVersion\",\"manifest\",\"targetFacts\"]}");
  zbuf_append(buf, ",\"meta\":{\"supportedFacts\":[\"literal arithmetic\",\"literal comparisons\",\"Bool logic\",\"target.os\",\"target.arch\",\"target.abi\",\"target.libc\",\"target.objectFormat\",\"target.endian\",\"target.pointerWidth\",\"target.hasCapability\",\"fieldCount\",\"hasField\",\"fieldType\",\"enumCaseCount\",\"hasEnumCase\",\"choiceCaseCount\",\"hasChoiceCase\"],\"unsupportedEffects\":[\"filesystem\",\"network\",\"process\",\"ambient environment\"],\"cycleDiagnostic\":\"MET001\",\"safetyLimitDiagnostic\":\"MET001\"}");
  zbuf_append(buf, ",\"reflection\":{\"typed\":true,\"compileTimeOnly\":true,\"releaseMetadataRetained\":false,\"facts\":[\"target\",\"shape fields\",\"enum cases\",\"choice cases\"]}");
  zbuf_append(buf, ",\"staticValues\":{\"supported\":[\"integer\",\"Bool\",\"enum\"],\"concreteSpecializations\":true,\"runtimeRegistries\":false,\"reflectionTables\":false,\"mismatchesDiagnostic\":\"STC003\"}");
  zbuf_append(buf, ",\"typedBuilders\":{\"status\":\"limited-v1\",\"supported\":[\"shape literals\",\"enum case values\",\"choice constructors\"],\"rawTokenStrings\":false,\"compileTimeOnly\":true,\"runtimeBuilderRegistry\":false}");
  zbuf_append(buf, ",\"programSurface\":{\"shapeCount\":");
  zbuf_appendf(buf, "%zu", program ? program->shapes.len : 0);
  zbuf_append(buf, ",\"enumCount\":");
  zbuf_appendf(buf, "%zu", program ? program->enums.len : 0);
  zbuf_append(buf, ",\"choiceCount\":");
  zbuf_appendf(buf, "%zu", program ? program->choices.len : 0);
  zbuf_append(buf, "}}");
  free(manifest);
}

static void print_check_json_success(const char *path, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"sourceFile\": ");
  append_json_string(&buf, path);
  CapabilitySummary caps = program_capabilities(program);
  ZMetaCacheStats meta = z_meta_cache_stats();
  char *manifest = read_optional_file("zero.json");
  zbuf_append(&buf, ",\n  \"package\": ");
  append_package_metadata_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&buf, input, target, "release");
  zbuf_append(&buf, ",\n  \"diagnostics\": [],\n  \"metaCache\": {");
  zbuf_appendf(&buf, "\n    \"hits\": %zu,\n    \"misses\": %zu,\n    \"entries\": %zu", meta.hits, meta.misses, meta.entries);
  append_hash_json(&buf, "sourceHash", fnv1a_text(input ? input->source : ""));
  append_hash_json(&buf, "targetHash", fnv1a_text(target ? target->name : z_host_target()));
  append_hash_json(&buf, "manifestHash", fnv1a_text(manifest ? manifest : ""));
  zbuf_append(&buf, "\n  },\n  \"compileTime\": ");
  append_compile_time_json(&buf, program, input, target);
  zbuf_append(&buf, ",\n  \"targetReadiness\": ");
  append_target_readiness_json(&buf, input, program, target, command);
  zbuf_append(&buf, ",\n  \"compilerPhases\": ");
  append_compiler_phases_json(&buf, input);
  zbuf_append(&buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(&buf, input, target, "release");
  zbuf_append(&buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(&buf, input, target, "release");
  zbuf_append(&buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(&buf, "check", NULL, program, &caps, target);
  zbuf_append(&buf, "\n}\n");
  free(manifest);
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void append_public_docs_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  CapabilitySummary caps = program_capabilities(program);
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, "release");
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"packageSurface\": {\"sourceFileCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->source_file_count : 0);
  zbuf_append(buf, ", \"moduleCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, "},\n  \"modules\": [");
  for (size_t i = 0; input && i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->module_names[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->module_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"symbols\": [");
  bool wrote_symbol = false;
  for (size_t i = 0; input && i < input->symbol_count; i++) {
    if (!input->symbol_public[i]) continue;
    if (wrote_symbol) zbuf_append(buf, ", ");
    wrote_symbol = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->symbol_names[i]);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, input->symbol_modules[i]);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
    zbuf_append(buf, ",\"doc\":\"\",\"examples\":[],\"targetSupport\":");
    const Function *fun = (input->symbol_kinds && strcmp(input->symbol_kinds[i], "function") == 0) ? find_program_function(program, input->symbol_names[i]) : NULL;
    if (fun) {
      CapabilitySummary fun_caps = function_capabilities(fun);
      append_target_capability_facts_json(buf, target, &fun_caps);
      zbuf_append(buf, ",\"effects\":");
      append_function_effects_json(buf, fun);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, fun);
    } else {
      append_target_capability_facts_json(buf, target, &caps);
      zbuf_append(buf, ",\"effects\":[],\"ownership\":{\"params\":[],\"returnOwnership\":\"value\",\"movesOwnership\":false}");
    }
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"publicationGate\": {\"releaseGrade\": false, \"requiresExamplesForPublicApi\": true, \"missingExampleCount\": ");
  zbuf_appendf(buf, "%zu", wrote_symbol ? (size_t)1 : (size_t)0);
  zbuf_append(buf, ", \"message\": \"public package APIs must carry examples and docs metadata before registry publication\"}");
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "doc", NULL, program, &caps, target);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, "release");
  zbuf_append(buf, "\n}\n");
}

static size_t program_test_count(const Program *program, const char *filter) {
  size_t count = 0;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->is_test) continue;
    if (filter && filter[0] && (!fun->test_name || !strstr(fun->test_name, filter))) continue;
    count++;
  }
  return count;
}

static void append_c_header_inputs_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    append_json_string(buf, program->c_imports.items[i].header);
  }
  zbuf_append(buf, "]");
}

static void append_json_string_array_item(ZBuf *buf, bool *first, const char *name) {
  if (!buf || !first || !name) return;
  if (!*first) zbuf_append(buf, ", ");
  *first = false;
  append_json_string(buf, name);
}

static const char *portable_runtime_kind(const ZTargetInfo *target) {
  (void)target;
  return "native";
}

static bool append_capability_imports_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  (void)caps;
  (void)target;
  zbuf_append(buf, "[");
  zbuf_append(buf, "]");
  return false;
}

static void append_portable_capability_restrictions_json(ZBuf *buf, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"filesystem\":");
  append_json_string(buf, z_target_has_capability(target, "fs") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"environment\":");
  append_json_string(buf, z_target_has_capability(target, "env") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"arguments\":");
  append_json_string(buf, z_target_has_capability(target, "args") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"stdio\":");
  append_json_string(buf, z_target_has_capability(target, "stdio") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"dom\":");
  append_json_string(buf, "unavailable");
  zbuf_append(buf, ",\"network\":");
  append_json_string(buf, z_target_has_capability(target, "net") ? "target-capability" : "unavailable");
  zbuf_append(buf, ",\"process\":");
  append_json_string(buf, z_target_has_capability(target, "proc") ? "target-capability" : "unavailable");
  zbuf_append(buf, "}");
}

static void append_local_runtime_plan_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  ZBuf imports;
  zbuf_init(&imports);
  append_capability_imports_json(&imports, caps, target);
  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"runtimeKind\":");
  append_json_string(buf, portable_runtime_kind(target));
  zbuf_append(buf, ",\"productionLikeImports\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"providerSpecificDeployment\":false,\"hostedDeployment\":\"out-of-scope\",\"command\":");
  append_json_string(buf, "zero dev");
  zbuf_append(buf, ",\"imports\":{\"explicit\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"module\":");
  zbuf_append(buf, "null");
  zbuf_append(buf, ",\"functions\":");
  zbuf_append(buf, imports.data);
  zbuf_append(buf, ",\"adapter\":");
  append_json_string(buf, "native-target-runtime");
  zbuf_append(buf, "},\"capabilityRestrictions\":");
  append_portable_capability_restrictions_json(buf, target);
  zbuf_append(buf, ",\"memoryFloorBudgetBytes\":");
  zbuf_append(buf, "0");
  zbuf_append(buf, ",\"frameworkTaxBytes\":0}");
  zbuf_free(&imports);
}

static void append_dev_plan_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  size_t test_count = program_test_count(program, command ? command->filter : NULL);
  const Function *main_fun = find_program_function(program, "main");
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"mode\": \"watch-plan\",\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"watch\": {\"strategy\":\"fingerprint changed modules and dependent tests\",\"persistent\": false, \"planOnly\": true, \"sourceFileCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->source_file_count : 0);
  zbuf_append(buf, ", \"moduleCount\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, ", \"files\": [");
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "], \"manifest\": ");
  append_json_string(buf, "zero.json");
  zbuf_append(buf, ", \"packageLocks\": [");
  if (input && input->lockfile_path) append_json_string(buf, input->lockfile_path);
  zbuf_append(buf, "], \"generatedBindingInputs\": ");
  append_c_header_inputs_json(buf, program);
  zbuf_append(buf, ", \"rerun\": [\"check\", \"test\", \"examples\"], \"restartOnSuccess\": ");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, "},\n  \"affected\": {\"tests\": ");
  zbuf_appendf(buf, "%zu", test_count);
  zbuf_append(buf, ", \"examples\": ");
  zbuf_appendf(buf, "%zu", input && input->package_root && strstr(input->package_root, "examples") ? (size_t)1 : (size_t)0);
  zbuf_append(buf, ", \"modules\": ");
  zbuf_appendf(buf, "%zu", input ? input->module_count : 0);
  zbuf_append(buf, "},\n  \"actions\": [{\"kind\":\"check\",\"when\":\"source-or-manifest-changed\"}, {\"kind\":\"test\",\"selectedTests\":");
  zbuf_appendf(buf, "%zu", test_count);
  zbuf_append(buf, "}, {\"kind\":\"examples\",\"selectedExamples\":");
  zbuf_appendf(buf, "%zu", input && input->package_root && strstr(input->package_root, "examples") ? (size_t)1 : (size_t)0);
  zbuf_append(buf, "}, {\"kind\":\"restart\",\"enabled\":");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, "}],\n  \"restart\": {\"runnableCli\":");
  zbuf_append(buf, main_fun ? "true" : "false");
  zbuf_append(buf, ",\"strategy\":\"rebuild-and-restart-after-successful-checks\",\"binaryOut\":\".zero/dev/app\"},\n  \"partialDiagnostics\": {\"stable\":true,\"whileCodegenPending\":true,\"sourceOfTruth\":\"zero check --json\",\"schemaVersion\":1},\n  \"trace\": {\"enabled\":true,\"requested\":");
  zbuf_append(buf, command && command->trace ? "true" : "false");
  zbuf_append(buf, ",\"phaseTiming\":true,\"cacheFacts\":true,\"diagnosticsPassthrough\":true");
  zbuf_append(buf, "},\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, command ? command->profile : "dev");
  zbuf_append(buf, ",\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"localRuntime\": ");
  append_local_runtime_plan_json(buf, &caps, target);
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "dev", NULL, program, &caps, target);
  zbuf_append(buf, "\n}\n");
}

static void append_time_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  size_t hits = 0;
  size_t misses = 0;
  source_cache_hit_miss_counts(input, &hits, &misses);
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"package\": ");
  append_package_metadata_json(buf, input, target);
  zbuf_append(buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"cacheSummary\": {\"hits\": ");
  zbuf_appendf(buf, "%zu", hits);
  zbuf_append(buf, ", \"misses\": ");
  zbuf_appendf(buf, "%zu", misses);
  zbuf_append(buf, ", \"entries\": 5, \"warmRebuildExpected\": ");
  zbuf_append(buf, hits > 0 ? "true" : "false");
  zbuf_append(buf, "},\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "time", NULL, program, &caps, target);
  zbuf_append(buf, "\n}\n");
}

static const char *diag_fix_safety(int code) {
  switch (code) {
    case 7001:
    case 7002:
    case 7003:
    case 2002:
    case 2003:
    case 6002:
    case 8003:
    case 9001:
    case 9002:
    case 9003:
    case 9004:
      return "requires-human-review";
    case 1001:
    case 1002:
    case 1003:
      return "api-changing";
    case 3004:
    case 3005:
    case 3006:
    case 3010:
    case 3011:
    case 3012:
    case 3028:
    case 3029:
    case 3032:
    case 3033:
    case 3034:
    case 3036:
    case 3050:
    case 3037:
    case 3038:
    case 3039:
    case 3040:
    case 3041:
    case 3042:
    case 3043:
    case 3044:
    case 3045:
    case 3046:
    case 3047:
    case 3048:
    case 3049:
      return "behavior-preserving";
    default:
      return "requires-human-review";
  }
}

static const char *diag_repair_id(int code) {
  switch (code) {
    case 100: return "repair-syntax";
    case 1001: return "add-fallible-marker-or-rescue";
    case 1002: return "add-missing-error-name";
    case 1003: return "check-or-rescue-fallible-call";
    case 7001: return "fix-import-path";
    case 7002: return "break-import-cycle";
    case 3003: return "declare-missing-symbol";
    case 3007: return "match-return-type";
    case 3010: return "make-binding-mutable";
    case 3011: return "use-known-stdlib-helper";
    case 3012: return "match-stdlib-argument-type";
    case 3013: return "avoid-use-after-move";
    case 3014: return "fix-drop-signature";
    case 3015: return "add-memory-type-argument";
    case 3029: return "end-conflicting-borrow";
    case 3030: return "return-owned-value";
    case 3031: return "make-c-abi-safe";
    case 2003: return "use-direct-emitter";
    case 2004: return "choose-buildable-direct-target";
    case 3032: return "match-generic-type-arguments";
    case 3033: return "make-generic-argument-types-match";
    case 3034: return "add-explicit-generic-type-arguments";
    case 3035: return "remove-unsupported-meta-expression";
    case 3036: return "fix-type-alias-declaration";
    case 3050: return "keep-recursive-generic-arguments-stable";
    case 3037: return "add-public-api-type";
    case 3038: return "declare-or-use-static-interface";
    case 3039: return "add-required-interface-method";
    case 3040: return "match-interface-method-arity";
    case 3041: return "match-interface-return-type";
    case 3042: return "match-interface-parameter-type";
    case 3043: return "use-supported-static-value-type";
    case 3044: return "pass-constant-static-value";
    case 3045: return "match-static-value-argument";
    case 3046: return "bind-generic-shape-method";
    case 3047: return "match-shape-method-self";
    case 3048: return "call-declared-receiver-method";
    case 3049: return "make-receiver-addressable-or-mutable";
    case 3102: return "initialize-missing-field";
    case 4004: return "choose-supported-direct-backend";
    case 6002: return "choose-target-with-required-capability";
    case 8003: return "configure-target-c-dependency";
    case 9001: return "fix-package-dependency-path";
    case 9002: return "break-package-dependency-cycle";
    case 9003: return "choose-one-package-version";
    case 9004: return "choose-target-compatible-package";
    default: return code == 0 ? "repair-syntax" : "manual-review";
  }
}

static const char *diag_repair_summary(int code) {
  switch (code) {
    case 100: return "Repair the syntax at the reported parser span, then rerun zero check.";
    case 1001: return "Add `!` or an explicit error set to the function signature, or handle the fallible expression with rescue.";
    case 1002: return "Add the missing error name to the `![...]` set or rescue the call locally.";
    case 1003: return "Wrap the fallible call in check or rescue so error flow is explicit.";
    case 7001: return "Change the import to a package-local module path that resolves under src/.";
    case 7002: return "Break the import cycle by moving shared declarations into a third module or removing one import edge.";
    case 3003: return "Declare the referenced symbol, import the module that provides it, or correct the identifier spelling.";
    case 3007: return "Change the returned expression or the function return annotation so both types agree.";
    case 3010: return "Change the root binding to `mut` before passing it to a mutable API.";
    case 3011: return "Use one of the supported std helpers or add compiler support for the new helper.";
    case 3012: return "Pass the expected stdlib argument type, such as MutSpan<u8> for writable byte APIs.";
    case 3013: return "Stop using the moved binding, or keep ownership in the original binding until the final use.";
    case 3014: return "Use the canonical non-raising drop signature: `fn drop Void self mutref<Self>`.";
    case 3015: return "Add the missing type argument, for example Maybe<u8> or Span<const u8>.";
    case 3029: return "End the active lexical borrow before taking a conflicting borrow or assigning to the borrowed root.";
    case 3030: return "Return an owned value, or keep references to local bindings inside the current function.";
    case 3031: return "Use explicit scalar, ref, or mutref types at C ABI boundaries and keep exported C functions non-raising.";
    case 2003: return "Use a direct emitter such as --emit exe or --emit obj.";
    case 2004: return "Choose a direct-supported target and artifact kind, or simplify the program to the target backend buildability subset.";
    case 3032: return "Pass one type argument for each generic parameter, or remove type arguments from non-generic calls.";
    case 3033: return "Make all values that bind the same generic parameter use the same concrete type.";
    case 3034: return "Add explicit generic type arguments when the compiler cannot infer them from runtime arguments.";
    case 3035: return "Use deterministic meta expressions within the bounded evaluator: literal arithmetic, Bool logic, target facts, or typed reflection facts.";
    case 3036: return "Make the alias name unique and point it at a non-cyclic concrete type.";
    case 3050: return "Call recursively with the same generic type parameters or use a concrete helper.";
    case 3037: return "Add an explicit public type annotation so graph and docs metadata stay stable.";
    case 3038: return "Declare the referenced interface or pass a concrete shape that satisfies the constraint.";
    case 3039: return "Add the required static method to the concrete shape.";
    case 3040: return "Make the concrete shape method take the same number of parameters as the interface method.";
    case 3041: return "Change the concrete shape method return type to match the interface.";
    case 3042: return "Change the concrete shape method parameter type to match the interface.";
    case 3043: return "Use a concrete integer, Bool, or enum type for static value parameters.";
    case 3044: return "Pass a literal, top-level const, or supported meta value with the static parameter type.";
    case 3045: return "Use the same static value in the explicit call and the value's annotated type.";
    case 3046: return "Pass a concrete self value or explicit shape arguments so the method can specialize.";
    case 3047: return "Make every argument agree with the same generic shape instantiation.";
    case 3048: return "Call a declared receiver method, or use namespace syntax for static methods without self.";
    case 3049: return "Store the receiver in an addressable binding and use `mut` for mutating methods.";
    case 3102: return "Initialize the missing shape field or add a default to the shape declaration.";
    case 4004: return "Use zero targets --json to choose a direct-supported target, or request --emit obj when only object emission exists.";
    case 6002: return "Build for a target that provides the required capability, or move that capability behind a target-specific entry point.";
    case 8003: return "Use package-relative vendored headers/libraries or set the target sysroot instead of relying on host include, lib, or pkg-config discovery.";
    case 9001: return "Create the local dependency package or update the path in zero.json.";
    case 9002: return "Remove the package cycle or move shared code into an acyclic dependency.";
    case 9003: return "Resolve the graph to one version of each package name.";
    case 9004: return "Select a target supported by the dependency or gate the dependency behind a compatible target.";
    default: return code == 0 ? "Repair the syntax at the reported parser span, then rerun zero check." : "Inspect the diagnostic fields and choose a repair manually.";
  }
}

static bool diag_has_borrow_trace(const ZDiag *diag) {
  return diag && diag->code == 3029 && diag->borrow_trace_count > 0;
}

static void append_diag_borrow_trace_json(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{\"rule\":\"lexical\",\"activeBorrows\":[");
  for (size_t i = 0; i < diag->borrow_trace_count; i++) {
    const ZBorrowTrace *trace = &diag->borrow_traces[i];
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"root\":");
    append_json_string(buf, trace->root);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, trace->path);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, trace->kind);
    zbuf_append(buf, ",\"binding\":");
    append_json_string_or_null(buf, trace->binding);
    zbuf_append(buf, ",\"bindingDecl\":");
    if (trace->binding_line > 0) {
      zbuf_append(buf, "{\"path\":");
      append_json_string(buf, trace->binding_decl_path ? trace->binding_decl_path : (diag->path ? diag->path : path));
      zbuf_appendf(buf, ",\"line\":%d,\"column\":%d}", trace->binding_line, trace->binding_column > 0 ? trace->binding_column : 1);
    } else {
      zbuf_append(buf, "null");
    }
    zbuf_append(buf, ",\"scopeExit\":null}");
  }
  zbuf_append(buf, "],\"truncated\":");
  zbuf_append(buf, diag->borrow_trace_truncated ? "true" : "false");
  zbuf_append(buf, ",\"repair\":");
  append_json_string(buf, diag->borrow_repair[0] ? diag->borrow_repair : diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
}

typedef struct {
  const char *code;
  const char *category;
  const char *title;
  const char *summary;
  const char *why;
  const char *canonical_repair;
  const char *bad_example;
  const char *good_example;
} ExplainInfo;

static void print_diag_json(const char *path, const ZDiag *diag);

static const ExplainInfo explain_infos[] = {
  {
    "TAR001",
    "target",
    "Unknown target",
    "The requested target name is not in the native bootstrap target table.",
    "Zero keeps target names explicit so cross builds do not silently use host assumptions.",
    "Run `zero targets` and choose one of the listed names.",
    "zero check --target not-a-target examples/hello.0",
    "zero check --target linux-musl-x64 examples/hello.0",
  },
  {
    "TAR002",
    "target",
    "Target capability unavailable",
    "The selected target does not provide a capability required by this program.",
    "Zero checks required standard-library capabilities against the target manifest so native, web, and embedded builds do not silently inherit host behavior.",
    "Build for a target that provides the required capability, or move that capability behind a target-specific entry point.",
    "zero check --target linux-musl-x64 conformance/native/fail/std-fs-target-unsupported.0",
    "zero build --target host examples/memory-package --out .zero/out/memory-package",
  },
  {
    "BLD003",
    "build",
    "Generated C backend removed",
    "Generated C output is no longer part of the supported Zero toolchain.",
    "`--emit c` and `--legacy-backend` were removed once direct artifacts became the product path.",
    "Use a direct emitter such as `--emit exe` or `--emit obj`.",
    "zero build --emit c examples/hello.0",
    "zero build --emit exe examples/hello.0",
  },
  {
    "TYP009",
    "type",
    "Mutable storage required",
    "A mutable API was given storage rooted in an immutable binding.",
    "`MutSpan<T>` and `mutref<T>` must come from storage that the source explicitly marks mutable.",
    "Change the root binding to `mut`, or pass a writable `MutSpan<T>`/`mutref<T>` from another mutable owner.",
    "let dst [4]u8 [0, 0, 0, 0]\nlet _copied std.mem.copy dst src",
    "mut dst [4]u8 [0, 0, 0, 0]\nlet _copied std.mem.copy dst src",
  },
  {
    "ERR002",
    "fallibility",
    "Error set mismatch",
    "A caller with an explicit `![...]` set checked a callee that may raise an unlisted error.",
    "Public and explicit error sets are part of the function contract, so propagation must keep the set complete.",
    "Add the missing error name to the caller's `![...]` set or handle the call locally with `rescue`.",
    "pub fn main Void world World ![NotFound]\n  check std.fs.createOrRaise fs path",
    "pub fn main Void world World ![NotFound TooLarge Io]\n  check std.fs.createOrRaise fs path",
  },
  {
    "ERR003",
    "fallibility",
    "Unchecked fallible call",
    "A fallible function or named-error stdlib helper was called without `check` or `rescue`.",
    "Zero keeps error flow visible in source and in function signatures.",
    "Wrap the call in `check`, or handle it locally with `rescue`.",
    "let file std.fs.createOrRaise fs path",
    "let file check std.fs.createOrRaise fs path",
  },
  {
    "STD003",
    "stdlib",
    "Stdlib argument mismatch",
    "A supported stdlib helper was called with an argument that does not match its implemented contract.",
    "The bootstrap stdlib surface is intentionally narrow, so helpers reject implicit conversions and unsupported capability shapes.",
    "Pass the exact expected argument type shown in the diagnostic, such as `MutSpan<u8>` for writable byte APIs.",
    "std.mem.fill(bytes, 0_u8) // when bytes is immutable",
    "mut bytes [4]u8 [0, 0, 0, 0]\nstd.mem.fill bytes 0_u8",
  },
  {
    "TYP023",
    "type",
    "Generic type argument mismatch",
    "A generic call provided the wrong number of type arguments, or a non-generic function was called with type arguments.",
    "Zero's first generic slice monomorphizes calls directly, so the concrete type argument list must be explicit and unambiguous.",
    "Pass one type argument for each generic parameter, or remove `<...>` from non-generic calls.",
    "identity<i32, u8>(value)",
    "identity<i32>(value)",
  },
  {
    "TYP024",
    "type",
    "Conflicting generic inference",
    "A single generic parameter was inferred as more than one concrete type.",
    "Generic inference is intentionally local: every occurrence of the same generic parameter in one call must resolve to the same type.",
    "Make the argument types match or pass explicit type arguments with compatible values.",
    "first(1, 2_u8)",
    "first(1, 2)",
  },
  {
    "TYP025",
    "type",
    "Generic type cannot be inferred",
    "A generic function call did not provide enough value context to infer every type parameter.",
    "The bootstrap compiler avoids broad inference across function bodies or return targets to keep public APIs and direct metadata predictable.",
    "Add explicit type arguments such as `identity<i32>(value)`.",
    "makeDefault()",
    "makeDefault<i32>()",
  },
  {
    "MET001",
    "meta",
    "Unsupported meta expression",
    "The compile-time evaluator rejected a `meta` expression because it was unsupported, effectful, cyclic, or outside the safety limits.",
    "Zero keeps compile-time execution sandboxed, deterministic, and bounded, so filesystem, network, process, and ambient environment access fail before code generation.",
    "Use literal arithmetic, Bool logic, target facts, or typed reflection facts within the bounded evaluator.",
    "const bad: usize = meta std.fs.host()",
    "const page: usize = meta target.pointerWidth * 64",
  },
  {
    "TYP026",
    "type",
    "Invalid type alias",
    "A type alias is duplicated, malformed, or cyclic.",
    "Aliases are graph metadata and compile-time spelling only; they must resolve to concrete existing type forms without introducing runtime identity.",
    "Rename the alias or point it at a concrete non-cyclic type.",
    "type A = B\ntype B = A",
    "type Bytes = Span<u8>",
  },
  {
    "TYP027",
    "type",
    "Recursive generic call changes type arguments",
    "A recursive generic function call or call cycle instantiated itself with a type argument that still references the current generic parameter.",
    "Zero monomorphizes generic calls directly, so growing recursive instantiations such as Maybe<T>, Maybe<Maybe<T>>, and deeper forms are rejected instead of expanding without a bound.",
    "Call recursively with the same generic type parameters or move the growing case into a concrete helper.",
    "grow<Maybe<T>>(nested)",
    "grow<T>(value)",
  },
  {
    "PUB001",
    "public-api",
    "Public API type required",
    "A public declaration omitted a concrete type annotation.",
    "Public surfaces must stay explicit so docs, graph JSON, and agents can repair uses without whole-body inference.",
    "Add the missing public type annotation.",
    "pub const answer 42",
    "pub const answer i32 42",
  },
  {
    "IFC001",
    "interface",
    "Interface constraint not satisfied",
    "A generic constraint references an unknown interface, or a concrete type argument does not provide a shape for static checking.",
    "Static interfaces are erased before codegen, so the checker must prove each constrained specialization from concrete shape methods.",
    "Declare the interface or pass a concrete shape that implements the required static methods.",
    "describe<i32>(1)",
    "describe<Person>(&person)",
  },
  {
    "IFC002",
    "interface",
    "Missing interface method",
    "A concrete shape used for a constrained generic call is missing a required static method.",
    "Zero does not create runtime interface values or vtables; satisfying an interface means the concrete shape already has the matching static method.",
    "Add the missing static method to the shape.",
    "type Person\n  name String",
    "type Person\n  name String\n\n  fn describe String self ref<Self>\n    ret self.name",
  },
  {
    "IFC003",
    "interface",
    "Interface method arity mismatch",
    "A concrete static method has a different parameter count from the interface requirement.",
    "Static method calls are monomorphized directly, so the required signature must match before emission.",
    "Make the shape method use the same parameter count as the interface method.",
    "fn describe String",
    "fn describe String self ref<Self>",
  },
  {
    "IFC004",
    "interface",
    "Interface return type mismatch",
    "A concrete static method returns a type that does not match the interface requirement.",
    "Constrained generic bodies rely on the interface return type without runtime adaptation.",
    "Change the concrete method return type to match the interface.",
    "fn describe i32 self ref<Self>",
    "fn describe String self ref<Self>",
  },
  {
    "IFC005",
    "interface",
    "Interface parameter type mismatch",
    "A concrete static method parameter does not match the interface requirement.",
    "Interface checks are compile-time signature checks over concrete static methods.",
    "Change the concrete method parameter type to match the interface.",
    "fn describe String self i32",
    "fn describe String self ref<Self>",
  },
	  {
	    "STC001",
	    "static",
	    "Unsupported static value parameter type",
	    "A static value parameter uses a type outside the concrete V1 set.",
	    "Static values are erased by monomorphization, so only integer, Bool, and enum values are accepted.",
	    "Change the static parameter type to a concrete integer, Bool, or enum type.",
	    "type Buf<static N: String>\n  len usize",
	    "type Buf<static N: usize>\n  len usize",
	  },
	  {
	    "STC002",
	    "static",
	    "Static value argument is not constant",
	    "A static value argument is not a deterministic literal, top-level const, or supported meta result.",
	    "Static value parameters are erased by monomorphization, so their values must be known before code generation.",
	    "Pass a value with the static parameter type, such as an integer literal, Bool literal, enum case, top-level const, or bounded meta result.",
	    "make<u8, runtime_len>()",
	    "make<u8, 16>()",
	  },
  {
    "STC003",
    "static",
    "Static value argument mismatch",
    "An explicit static argument conflicts with the value carried by an annotated type.",
    "Zero does not infer through whole function bodies; static values must agree at the call boundary.",
    "Use the same static value in the explicit call and the annotated value.",
    "first<u8, 8>(&vec4)",
    "first<u8, 4>(&vec4)",
  },
  {
    "BOR001",
    "borrow",
    "Active lexical borrow conflict",
    "A read, assignment, or borrow conflicts with a reference that remains live until the end of its lexical scope.",
    "Zero tracks borrows lexically so agents can repair by moving code or introducing inner blocks without relying on hidden lifetime inference.",
    "End the active lexical borrow before the conflicting operation by moving the operation after the borrow scope or putting the borrow in a narrower block.",
    "let shared = &data\nupdate(&mut data)",
    "{\n  let shared = &data\n  let observed = shared.value\n}\nupdate(&mut data)",
  },
  {
    "SHM001",
    "shape-method",
    "Generic shape method cannot be specialized",
    "A method on a generic shape was called without enough information to bind the inherited shape parameters.",
    "Generic shape methods lower to concrete C functions, so Self, type parameters, and static values must be known at the call boundary.",
    "Pass a concrete self argument or explicit method type arguments matching the shape declaration.",
    "FixedVec.init()",
    "FixedVec.init<u8, 4>()",
  },
  {
    "SHM002",
    "shape-method",
    "Shape method Self instantiation mismatch",
    "Arguments to a generic shape method imply different Self/type/static values.",
    "A specialized method has one concrete layout, so all parameters tied to Self must agree before code generation.",
    "Use one concrete shape instantiation for the method call.",
    "FixedVec.push<u8, 8>(&vec4, 1)",
    "FixedVec.push<u8, 4>(&vec4, 1)",
  },
  {
    "RCV001",
    "receiver",
    "Unknown or non-receiver method",
    "A value-style method call does not name a method with an explicit self parameter on the receiver shape.",
    "Receiver syntax is static sugar for a shape method whose first parameter is self: ref<Self> or self: mutref<Self>.",
    "Call a declared receiver method, or use namespace syntax for static methods without self.",
    "vec.capacity()",
    "FixedVec.capacity<u8, 4>()",
  },
  {
    "RCV002",
    "receiver",
    "Receiver is not addressable or mutable enough",
    "A receiver-style method call needs an addressable value, and mutating methods need a mutable receiver.",
    "The compiler lowers receiver calls by passing an explicit ref<Self> or mutref<Self> argument to a direct C function.",
    "Store the receiver in a binding and use `mut` before calling mutating methods.",
    "vec.push(1)",
    "mut vec FixedVec<u8,4> FixedVec . len 0 items [0, 0, 0, 0]\nvec.push 1",
  },
  {"BLD004", "build", "Direct backend target not buildable", "The selected direct backend cannot build this source, target, object format, architecture, or artifact kind.", "Target-aware buildability runs before object or executable emission and reports ordinary direct-backend limitations with structured blocker facts.", "Choose a target whose `zero targets --json` directBackend facts advertise the requested artifact, or simplify the program to the backend-supported subset.", "zero check --json --emit obj --target linux-arm64 examples/direct-call-add.0", "zero check --json --emit obj --target linux-x64 examples/direct-call-add.0"},
  {"CGEN004", "codegen", "Direct code generation invariant failed", "A direct emitter reached an internal code generation invariant after target buildability accepted the program.", "Ordinary unsupported targets and source features should be reported before emission with BLD004.", "Report this compiler bug with the source program and target that produced it.", "zero build --json --emit obj --target linux-musl-x64 examples/direct-call-add.0", "zero build --json --emit obj --target linux-x64 examples/direct-call-add.0"},
  {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
};

static const ExplainInfo *find_explain_info(const char *code) {
  if (!code) return NULL;
  for (size_t i = 0; explain_infos[i].code; i++) {
    if (strcmp(explain_infos[i].code, code) == 0) return &explain_infos[i];
  }
  return NULL;
}

static void print_explain_json(const ExplainInfo *info) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"code\": ");
  append_json_string(&buf, info->code);
  zbuf_append(&buf, ",\n  \"category\": ");
  append_json_string(&buf, info->category);
  zbuf_append(&buf, ",\n  \"title\": ");
  append_json_string(&buf, info->title);
  zbuf_append(&buf, ",\n  \"summary\": ");
  append_json_string(&buf, info->summary);
  zbuf_append(&buf, ",\n  \"why\": ");
  append_json_string(&buf, info->why);
  zbuf_append(&buf, ",\n  \"repair\": {\"id\": ");
  append_json_string(&buf, diag_repair_id(strcmp(info->code, "TAR001") == 0 ? 6001 :
                                         strcmp(info->code, "TAR002") == 0 ? 6002 :
                                         strcmp(info->code, "BLD003") == 0 ? 2003 :
                                         strcmp(info->code, "BLD004") == 0 ? 2004 :
                                         strcmp(info->code, "TYP009") == 0 ? 3010 :
                                         strcmp(info->code, "TYP023") == 0 ? 3032 :
                                         strcmp(info->code, "TYP024") == 0 ? 3033 :
                                         strcmp(info->code, "TYP025") == 0 ? 3034 :
                                         strcmp(info->code, "MET001") == 0 ? 3035 :
                                         strcmp(info->code, "TYP026") == 0 ? 3036 :
                                         strcmp(info->code, "TYP027") == 0 ? 3050 :
                                         strcmp(info->code, "PUB001") == 0 ? 3037 :
                                         strcmp(info->code, "IFC001") == 0 ? 3038 :
                                         strcmp(info->code, "IFC002") == 0 ? 3039 :
                                         strcmp(info->code, "IFC003") == 0 ? 3040 :
                                         strcmp(info->code, "IFC004") == 0 ? 3041 :
                                         strcmp(info->code, "IFC005") == 0 ? 3042 :
                                         strcmp(info->code, "STC001") == 0 ? 3043 :
                                         strcmp(info->code, "STC002") == 0 ? 3044 :
                                         strcmp(info->code, "STC003") == 0 ? 3045 :
                                         strcmp(info->code, "SHM001") == 0 ? 3046 :
                                         strcmp(info->code, "SHM002") == 0 ? 3047 :
                                         strcmp(info->code, "RCV001") == 0 ? 3048 :
                                         strcmp(info->code, "RCV002") == 0 ? 3049 :
                                         strcmp(info->code, "BOR001") == 0 ? 3029 :
                                         strcmp(info->code, "ERR002") == 0 ? 1002 :
                                         strcmp(info->code, "ERR003") == 0 ? 1003 :
                                         strcmp(info->code, "STD003") == 0 ? 3012 :
                                         strcmp(info->code, "CGEN004") == 0 ? 4004 : 0));
  zbuf_append(&buf, ", \"summary\": ");
  append_json_string(&buf, info->canonical_repair);
  zbuf_append(&buf, "},\n  \"examples\": {\"bad\": ");
  append_json_string(&buf, info->bad_example);
  zbuf_append(&buf, ", \"good\": ");
  append_json_string(&buf, info->good_example);
  zbuf_append(&buf, "}\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void print_explain_text(const ExplainInfo *info) {
  printf("%s: %s\n\n", info->code, info->title);
  printf("%s\n\n", info->summary);
  printf("Why: %s\n\n", info->why);
  printf("Repair: %s\n\n", info->canonical_repair);
  printf("Bad:\n%s\n\nGood:\n%s\n", info->bad_example, info->good_example);
}

static int explain_command(const Command *command) {
  const ExplainInfo *info = find_explain_info(command->input);
  if (!info) {
    ZDiag diag = {0};
    diag.code = 3003;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown diagnostic code '%s'", command->input ? command->input : "");
    snprintf(diag.expected, sizeof(diag.expected), "known diagnostic code");
    snprintf(diag.actual, sizeof(diag.actual), "%s", command->input ? command->input : "");
    snprintf(diag.help, sizeof(diag.help), "try TAR002, TYP009, TYP023, ERR002, ERR003, or STD003");
    if (command->json) print_diag_json(command->input, &diag);
    else print_diag(command->input, &diag);
    return 1;
  }
  if (command->json) print_explain_json(info);
  else print_explain_text(info);
  return 0;
}

static void append_backend_blocker_json(ZBuf *buf, const ZBackendBlocker *blocker) {
  zbuf_append(buf, "{\"target\":");
  append_json_string(buf, blocker && blocker->target[0] ? blocker->target : "unknown");
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, blocker && blocker->object_format[0] ? blocker->object_format : "unknown");
  zbuf_append(buf, ",\"backend\":");
  append_json_string(buf, blocker && blocker->backend[0] ? blocker->backend : "unknown");
  zbuf_append(buf, ",\"stage\":");
  append_json_string(buf, blocker && blocker->stage[0] ? blocker->stage : "unknown");
  zbuf_append(buf, ",\"unsupportedFeature\":");
  append_json_string(buf, blocker && blocker->unsupported_feature[0] ? blocker->unsupported_feature : "unsupported construct");
  zbuf_append(buf, "}");
}

static void print_diag_json(const char *path, const ZDiag *diag) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": false,\n  \"diagnostics\": [\n    {\n");
  zbuf_append(&buf, "      \"severity\": \"error\",\n      \"code\": ");
  append_json_string(&buf, diag_code(diag->code));
  zbuf_append(&buf, ",\n      \"message\": ");
  append_json_string(&buf, diag->message);
  zbuf_append(&buf, ",\n      \"path\": ");
  append_json_string(&buf, diag->path ? diag->path : path);
  zbuf_appendf(&buf, ",\n      \"line\": %d,\n      \"column\": %d,\n      \"length\": %d,\n", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(&buf, "      \"expected\": ");
  append_json_string(&buf, diag->expected);
  zbuf_append(&buf, ",\n      \"actual\": ");
  append_json_string(&buf, diag->actual);
  zbuf_append(&buf, ",\n      \"help\": ");
  append_json_string(&buf, diag->help);
  zbuf_append(&buf, ",\n      \"fixSafety\": ");
  append_json_string(&buf, diag_fix_safety(diag->code));
  zbuf_append(&buf, ",\n      \"repair\": {\"id\": ");
  append_json_string(&buf, diag_repair_id(diag->code));
  zbuf_append(&buf, ", \"summary\": ");
  append_json_string(&buf, diag_repair_summary(diag->code));
  zbuf_append(&buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(&buf, ",\n      \"backendBlocker\": ");
    append_backend_blocker_json(&buf, &diag->backend_blocker);
  }
  if (diag_has_borrow_trace(diag)) {
    zbuf_append(&buf, ",\n      \"borrowTrace\": ");
    append_diag_borrow_trace_json(&buf, path, diag);
  }
  zbuf_append(&buf, ",\n      \"related\": [");
  if ((diag->code == 7001 || diag->code == 7002 || diag->code == 7003 || diag->code == 1002 || diag->code == 1003 ||
       diag->code == 6001 || diag->code == 6002 ||
       diag->code == 3010 || diag->code == 3011 || diag->code == 3012 || diag->code == 3028 || diag->code == 3029) && diag->actual[0]) {
    zbuf_append(&buf, "{\"path\":");
    append_json_string(&buf, diag->path ? diag->path : path);
    zbuf_appendf(&buf, ",\"line\":%d,\"column\":%d,\"message\":", diag->line, diag->column);
    append_json_string(&buf, diag->actual);
    zbuf_append(&buf, "}");
  }
  zbuf_append(&buf, "]");
  zbuf_append(&buf, "\n    }\n  ]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void append_fix_plan_diagnostic(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{");
  zbuf_append(buf, "\"code\": ");
  append_json_string(buf, diag_code(diag->code));
  zbuf_append(buf, ", \"message\": ");
  append_json_string(buf, diag->message);
  zbuf_append(buf, ", \"path\": ");
  append_json_string(buf, diag->path ? diag->path : path);
  zbuf_appendf(buf, ", \"line\": %d, \"column\": %d, \"length\": %d", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(buf, ", \"expected\": ");
  append_json_string(buf, diag->expected);
  zbuf_append(buf, ", \"actual\": ");
  append_json_string(buf, diag->actual);
  zbuf_append(buf, ", \"help\": ");
  append_json_string(buf, diag->help);
  zbuf_append(buf, ", \"fixSafety\": ");
  append_json_string(buf, diag_fix_safety(diag->code));
  zbuf_append(buf, ", \"repair\": {\"id\": ");
  append_json_string(buf, diag_repair_id(diag->code));
  zbuf_append(buf, ", \"summary\": ");
  append_json_string(buf, diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(buf, ", \"backendBlocker\": ");
    append_backend_blocker_json(buf, &diag->backend_blocker);
  }
  if (diag_has_borrow_trace(diag)) {
    zbuf_append(buf, ", \"borrowTrace\": ");
    append_diag_borrow_trace_json(buf, path, diag);
  }
  zbuf_append(buf, "}");
}

static void print_fix_plan_json(const char *path, const ZDiag *diag) {
  bool has_diag = diag && diag->code != 0;
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(&buf, has_diag ? "false" : "true");
  zbuf_append(&buf, ",\n  \"mode\": \"plan\",\n  \"appliesEdits\": false,\n  \"safetyLevels\": [\"format-only\", \"behavior-preserving\", \"api-changing\", \"target-changing\", \"requires-human-review\"],\n  \"input\": ");
  append_json_string(&buf, path);
  zbuf_append(&buf, ",\n  \"selfHostRepairPolicy\": {\"unsupportedFeatureSafety\":\"requires-human-review\",\"compatibilityFallback\":\"removed\",\"directFallback\":\"never-c-bridge\"}");
  zbuf_append(&buf, ",\n  \"diagnostics\": [");
  if (has_diag) append_fix_plan_diagnostic(&buf, path, diag);
  zbuf_append(&buf, "],\n  \"fixes\": [");
  if (has_diag) {
    zbuf_append(&buf, "{\"id\": ");
    append_json_string(&buf, diag_repair_id(diag->code));
    zbuf_append(&buf, ", \"diagnosticCode\": ");
    append_json_string(&buf, diag_code(diag->code));
    zbuf_append(&buf, ", \"safety\": ");
    append_json_string(&buf, diag_fix_safety(diag->code));
    zbuf_append(&buf, ", \"summary\": ");
    append_json_string(&buf, diag_repair_summary(diag->code));
    zbuf_append(&buf, ", \"appliesEdits\": false}");
  }
  zbuf_append(&buf, "]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static bool find_make_binding_mutable_edit(const char *source, int *line_out, char **old_line_out, char **new_line_out) {
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) line_end = line_start + strlen(line_start);
    size_t line_len = (size_t)(line_end - line_start);
    char *line_text = z_strndup(line_start, line_len);
    char *let_pos = strstr(line_text, "let ");
    if (let_pos && !strstr(line_text, "mut ") && strchr(line_text, '[')) {
      ZBuf replacement;
      zbuf_init(&replacement);
      size_t prefix_len = (size_t)(let_pos - line_text);
      char *prefix = z_strndup(line_text, prefix_len);
      zbuf_append(&replacement, prefix);
      zbuf_append(&replacement, "mut ");
      zbuf_append(&replacement, let_pos + strlen("let "));
      free(prefix);
      *line_out = line;
      *old_line_out = line_text;
      *new_line_out = replacement.data;
      return true;
    }
    free(line_text);
    if (!*line_end) break;
    cursor = line_end + 1;
    line++;
  }
  return false;
}

static char *apply_single_line_edit(const char *source, int target_line, const char *new_line) {
  ZBuf out;
  zbuf_init(&out);
  const char *cursor = source ? source : "";
  int line = 1;
  while (*cursor) {
    const char *line_start = cursor;
    const char *line_end = strchr(line_start, '\n');
    bool has_newline = line_end != NULL;
    if (!line_end) line_end = line_start + strlen(line_start);
    if (line == target_line) {
      zbuf_append(&out, new_line);
    } else {
      char *line_text = z_strndup(line_start, (size_t)(line_end - line_start));
      zbuf_append(&out, line_text);
      free(line_text);
    }
    if (has_newline) zbuf_append_char(&out, '\n');
    if (!has_newline) break;
    cursor = line_end + 1;
    line++;
  }
  return out.data;
}

static bool diagnostic_can_apply_edits(const ZDiag *diag) {
  return diag && diag->code == 3010 && strcmp(diag_fix_safety(diag->code), "behavior-preserving") == 0;
}

static int print_or_apply_fix_json(const char *path, SourceInput *input, const ZDiag *diag, bool apply) {
  bool has_diag = diag && diag->code != 0;
  bool can_apply = diagnostic_can_apply_edits(diag);
  const char *edit_path = input ? input->source_file : NULL;
  const char *edit_source = NULL;
  char *loaded_edit_source = NULL;
  if (can_apply && diag && diag->path && diag->path[0]) {
    edit_path = diag->path;
  }
  if (can_apply && edit_path && edit_path[0]) {
    ZDiag read_diag = {0};
    loaded_edit_source = z_read_file(edit_path, &read_diag);
    edit_source = loaded_edit_source;
  }
  int edit_line = 0;
  char *old_line = NULL;
  char *new_line = NULL;
  bool has_edit = can_apply && edit_source && find_make_binding_mutable_edit(edit_source, &edit_line, &old_line, &new_line);
  bool applied = false;
  if (apply && has_edit) {
    char *updated = apply_single_line_edit(edit_source, edit_line, new_line);
    ZDiag write_diag = {0};
    applied = edit_path && z_write_file(edit_path, updated, &write_diag);
    free(updated);
  }

  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
  zbuf_append(&buf, has_diag ? "false" : "true");
  zbuf_append(&buf, ",\n  \"mode\": ");
  append_json_string(&buf, apply ? "apply" : "patch");
  zbuf_appendf(&buf, ",\n  \"appliesEdits\": %s,\n  \"applied\": %s,\n  \"input\": ", apply ? "true" : "false", applied ? "true" : "false");
  append_json_string(&buf, path);
  zbuf_append(&buf, ",\n  \"selfHostRepairPolicy\": {\"unsupportedFeatureSafety\":\"requires-human-review\",\"compatibilityFallback\":\"removed\",\"directFallback\":\"never-c-bridge\"}");
  zbuf_append(&buf, ",\n  \"diagnostics\": [");
  if (has_diag) append_fix_plan_diagnostic(&buf, path, diag);
  zbuf_append(&buf, "],\n  \"fixes\": [");
  if (has_diag) {
    zbuf_append(&buf, "{\"id\": ");
    append_json_string(&buf, diag_repair_id(diag->code));
    zbuf_append(&buf, ", \"diagnosticCode\": ");
    append_json_string(&buf, diag_code(diag->code));
    zbuf_append(&buf, ", \"safety\": ");
    append_json_string(&buf, diag_fix_safety(diag->code));
    zbuf_append(&buf, ", \"summary\": ");
    append_json_string(&buf, diag_repair_summary(diag->code));
    zbuf_appendf(&buf, ", \"appliesEdits\": %s}", has_edit ? "true" : "false");
  }
  zbuf_append(&buf, "],\n  \"patches\": [");
  if (has_edit) {
    zbuf_append(&buf, "{\"path\":");
    append_json_string(&buf, edit_path);
    zbuf_appendf(&buf, ",\"line\":%d,\"old\":", edit_line);
    append_json_string(&buf, old_line);
    zbuf_append(&buf, ",\"new\":");
    append_json_string(&buf, new_line);
    zbuf_append(&buf, "}");
  }
  zbuf_append(&buf, "]\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
  free(old_line);
  free(new_line);
  free(loaded_edit_source);
  return apply && has_edit && !applied ? 1 : 0;
}

static void print_help(void) {
  printf("zero %s native bootstrap\n\n", ZERO_VERSION);
  printf("Usage:\n");
  printf("  zero --version [--json]\n");
  printf("  zero skills [list|get] [--json]\n");
  printf("  zero new cli|lib|package <name>\n");
  printf("  zero check <file.0|file.row|project|zero.json>\n");
  printf("  zero test <file.0|file.row|project|zero.json>\n");
  printf("  zero fmt <file.0|file.row|project|zero.json>\n");
  printf("  zero build [--json] [--emit exe|obj] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <file.0|file.row|project|zero.json>\n");
  printf("  zero run [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <file.0|file.row|project|zero.json> [-- args...]\n");
  printf("  zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <file.0|file.row|project|zero.json>\n");
  printf("  zero tokens --json <file.0|file.row|project|zero.json>\n");
  printf("  zero parse --json <file.0|file.row|project|zero.json>\n");
  printf("  zero graph [--json] <file.0|file.row|project|zero.json>\n");
  printf("  zero doc [--json] <file.0|file.row|project|zero.json>\n");
  printf("  zero size [--json] [--out <artifact>] <file.0|file.row|project|zero.json>\n");
  printf("  zero mem [--json] [--target <target>] <file.0|file.row|project|zero.json>\n");
  printf("  zero dev [--json] [--trace] <file.0|file.row|project|zero.json>\n");
  printf("  zero time --json <file.0|file.row|project|zero.json>\n");
  printf("  zero abi check|dump [--json] [--target <target>] <file.0|file.row|project|zero.json>\n");
  printf("  zero explain [--json] <code>\n");
  printf("  zero fix --plan --json <file.0|file.row|project|zero.json>\n");
  printf("  zero doctor [--json]\n");
  printf("  zero clean [--all]\n");
  printf("  zero targets\n");
  printf("\nExamples:\n");
  printf("  zero new cli hello\n");
  printf("  zero run examples/add.0\n");
  printf("  zero build --emit exe examples/hello.0 --out .zero/out/hello\n");
  printf("  zero ship --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello\n");
  printf("  zero check --json examples/hello.0\n");
  printf("  zero build --target linux-musl-x64 examples/memory-package\n");
}

static void print_command_help(const char *command) {
  if (strcmp(command, "new") == 0) {
    printf("Usage: zero new cli|lib|package <name>\n\n");
    printf("Create a small Zero project template.\n\n");
    printf("Examples:\n");
    printf("  zero new cli hello\n");
    printf("  zero new lib math-kit\n");
    printf("  zero new package demo\n");
  } else if (strcmp(command, "skills") == 0) {
    printf("Usage: zero skills [list|get] [--json]\n\n");
    printf("List and retrieve version-matched skill content for agents.\n\n");
    printf("Subcommands:\n");
    printf("  list                 list available skills (default)\n");
    printf("  get <name> [--full]  print bundled skill content\n");
    printf("  get --all            print every visible skill\n");
  } else if (strcmp(command, "doctor") == 0) {
    printf("Usage: zero doctor [--json]\n\n");
    printf("Check host, compiler, target toolchain, and docs/example readiness.\n");
  } else if (strcmp(command, "clean") == 0) {
    printf("Usage: zero clean [--all]\n\n");
    printf("Remove generated output while preserving compiler caches by default.\n\n");
    printf("Flags:\n");
    printf("  --all    remove broader .zero generated state while preserving .zero/bin\n");
  } else if (strcmp(command, "check") == 0) {
    printf("Usage: zero check [--json] [--target <target>] [--emit exe|obj] <file.0|file.row|project|zero.json>\n\n");
    printf("Parse and typecheck Zero source without emitting artifacts. JSON output includes target readiness for the selected emit kind.\n");
  } else if (strcmp(command, "build") == 0) {
    printf("Usage: zero build [--json] [--emit exe|obj] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <input>\n\n");
    printf("Build direct native executable or object artifacts.\n\n");
    printf("Example: zero build --release tiny --emit exe examples/hello.0 --out .zero/out/hello\n");
  } else if (strcmp(command, "run") == 0) {
    printf("Usage: zero run [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <file>] <input> [-- args...]\n\n");
    printf("Build a host executable with the direct backend and run it. Program stdout and stderr are passed through unchanged.\n\n");
    printf("Example: zero run examples/add.0\n");
  } else if (strcmp(command, "ship") == 0) {
    printf("Usage: zero ship [--json] [--target <target>] [--profile release-small|tiny|audit] [--out <file>] <input>\n\n");
    printf("Produce a deterministic release preview with a direct binary, stripped binary copy, checksum, archive manifest, debug-symbol metadata, size report, and SBOM placeholder.\n\n");
    printf("Example: zero ship --target linux-musl-x64 examples/hello.0 --out .zero/ship/hello\n");
  } else if (strcmp(command, "test") == 0) {
    printf("Usage: zero test [--json] [--filter <name>] [--target <target>] [--cc <path>] [--out <file>] <file.0|file.row|project|zero.json>\n\n");
    printf("Build and run inline `test` blocks.\n");
  } else if (strcmp(command, "fmt") == 0) {
    printf("Usage: zero fmt [--check] <file.0|file.row|project|zero.json>\n\n");
    printf("Print deterministic bootstrap formatting for Zero source.\n");
  } else if (strcmp(command, "targets") == 0) {
    printf("Usage: zero targets\n\n");
    printf("Print supported target facts as JSON.\n");
  } else if (strcmp(command, "tokens") == 0) {
    printf("Usage: zero tokens --json <file.0|file.row|project|zero.json>\n\n");
    printf("Emit source token JSON for oracle comparisons.\n");
  } else if (strcmp(command, "parse") == 0) {
    printf("Usage: zero parse --json <file.0|file.row|project|zero.json>\n\n");
    printf("Emit normalized source parse JSON for oracle comparisons.\n");
  } else if (strcmp(command, "abi") == 0) {
    printf("Usage: zero abi check|dump [--json] [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Check ABI-safe declarations or dump target-aware source layout facts.\n");
  } else if (strcmp(command, "graph") == 0) {
    printf("Usage: zero graph [--json] [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Inspect modules, symbols, capabilities, static metadata, and stdlib helpers.\n");
  } else if (strcmp(command, "doc") == 0) {
    printf("Usage: zero doc [--json] [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Emit package API documentation facts without emitting artifacts.\n");
  } else if (strcmp(command, "size") == 0) {
    printf("Usage: zero size [--json] [--target <target>] [--profile debug|dev|release-fast|release-small|tiny|audit] [--release <profile>] [--out <artifact>] <file.0|file.row|project|zero.json>\n\n");
    printf("Report direct IR size, optional artifact bytes, capabilities, and stdlib helper metadata.\n");
  } else if (strcmp(command, "mem") == 0) {
    printf("Usage: zero mem [--json] [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Report direct stack, static, heap, buffer, and runtime memory facts.\n");
  } else if (strcmp(command, "dev") == 0) {
    printf("Usage: zero dev [--json] [--trace] [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Emit a direct incremental watch plan, interface fingerprints, and affected-test summary.\n");
  } else if (strcmp(command, "time") == 0) {
    printf("Usage: zero time --json [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Emit compiler phase, cache, and invalidation timing facts.\n");
  } else if (strcmp(command, "explain") == 0) {
    printf("Usage: zero explain [--json] <diagnostic-code>\n\n");
    printf("Explain a diagnostic and its repair metadata.\n");
  } else if (strcmp(command, "fix") == 0) {
    printf("Usage: zero fix (--plan|--patch|--apply) --json [--target <target>] <file.0|file.row|project|zero.json>\n\n");
    printf("Print repair plans, reviewable patches, or apply behavior-preserving edits.\n");
  } else if (strcmp(command, "version") == 0 || strcmp(command, "--version") == 0) {
    printf("Usage: zero --version [--json]\n\n");
    printf("Print version, commit, host target, compiler backend, and target toolchain availability.\n");
  } else {
    print_help();
  }
}

static bool parse_common_option(int argc, char **argv, int *index, Command *command) {
  const char *arg = argv[*index];
  if (strcmp(arg, "--emit") == 0) {
    if (*index + 1 >= argc) {
      command->unknown_flag = arg;
      return true;
    }
    (*index)++;
    if (strcmp(argv[*index], "exe") == 0) command->emit = EMIT_EXE;
    else if (strcmp(argv[*index], "obj") == 0) command->emit = EMIT_OBJ;
    else if (strcmp(argv[*index], "c") == 0) command->emit = EMIT_C;
    else command->invalid_emit = argv[*index];
    return true;
  } else if (strcmp(arg, "--out") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->out = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--target") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->target = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--profile") == 0 || strcmp(arg, "--release") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->profile = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--cc") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->cc = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--backend") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->backend = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--filter") == 0) {
    if (*index + 1 >= argc) command->unknown_flag = arg;
    else command->filter = argv[++(*index)];
    return true;
  } else if (strcmp(arg, "--json") == 0) {
    command->json = true;
    return true;
  } else if (strcmp(arg, "--plan") == 0) {
    command->plan = true;
    return true;
  } else if (strcmp(arg, "--apply") == 0) {
    command->apply = true;
    return true;
  } else if (strcmp(arg, "--patch") == 0) {
    command->patch = true;
    return true;
  } else if (strcmp(arg, "--all") == 0) {
    command->all = true;
    return true;
  } else if (strcmp(arg, "--check") == 0) {
    command->fmt_check = true;
    return true;
  } else if (strcmp(arg, "--trace") == 0) {
    command->trace = true;
    return true;
  } else if (strcmp(arg, "--legacy-backend") == 0) {
    command->legacy_backend = true;
    return true;
  } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
    command->kind = "help";
    return true;
  } else if (strncmp(arg, "--", 2) == 0) {
    command->unknown_flag = arg;
    return true;
  }
  return false;
}

static bool parse_command(int argc, char **argv, Command *command) {
  if (argc < 2) return false;
  command->command = argv[1];
  command->emit = EMIT_EXE;
  command->target = "host";
  command->profile = "release";
  if (strcmp(command->command, "new") == 0) {
    if (argc >= 3) {
      if (strcmp(argv[2], "--help") == 0 || strcmp(argv[2], "-h") == 0) command->kind = "help";
      else command->kind = argv[2];
    }
    for (int i = 3; i < argc; i++) {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) return true;
      else if (!command->input) command->input = argv[i];
      else return false;
    }
    return true;
  }
  if (strcmp(command->command, "skills") == 0) {
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) command->kind = "help";
      else if (strcmp(argv[i], "--json") == 0) command->json = true;
      else if (strcmp(argv[i], "--all") == 0) command->all = true;
      else if (strcmp(argv[i], "--full") == 0) {
        continue;
      } else if (!command->kind) {
        command->kind = argv[i];
      } else if (!command->input) {
        command->input = argv[i];
      }
    }
    return true;
  }
  int arg_start = 2;
  if (strcmp(command->command, "abi") == 0 && argc >= 3 && strncmp(argv[2], "--", 2) != 0) {
    command->kind = argv[2];
    arg_start = 3;
  }
  if (strcmp(command->command, "run") == 0) {
    for (int i = arg_start; i < argc; i++) {
      if (command->input) {
        if (strcmp(argv[i], "--") == 0) {
          command->run_argc = argc - i - 1;
          command->run_argv = &argv[i + 1];
        } else {
          command->run_argc = argc - i;
          command->run_argv = &argv[i];
        }
        return true;
      }
      if (parse_common_option(argc, argv, &i, command)) continue;
      command->input = argv[i];
    }
    return true;
  }
  for (int i = arg_start; i < argc; i++) {
    if (parse_common_option(argc, argv, &i, command)) {
      continue;
    } else {
      command->input = argv[i];
    }
  }
  return strcmp(command->command, "--version") == 0 ||
         strcmp(command->command, "version") == 0 ||
         strcmp(command->command, "skills") == 0 ||
         strcmp(command->command, "check") == 0 ||
         strcmp(command->command, "test") == 0 ||
         strcmp(command->command, "fmt") == 0 ||
         strcmp(command->command, "build") == 0 ||
         strcmp(command->command, "run") == 0 ||
         strcmp(command->command, "ship") == 0 ||
         strcmp(command->command, "tokens") == 0 ||
         strcmp(command->command, "parse") == 0 ||
         strcmp(command->command, "graph") == 0 ||
         strcmp(command->command, "doc") == 0 ||
         strcmp(command->command, "size") == 0 ||
         strcmp(command->command, "mem") == 0 ||
         strcmp(command->command, "dev") == 0 ||
         strcmp(command->command, "time") == 0 ||
         strcmp(command->command, "abi") == 0 ||
         strcmp(command->command, "explain") == 0 ||
         strcmp(command->command, "fix") == 0 ||
         strcmp(command->command, "doctor") == 0 ||
         strcmp(command->command, "clean") == 0 ||
         strcmp(command->command, "targets") == 0;
}

static const char *emit_kind_name(EmitKind emit) {
  switch (emit) {
    case EMIT_OBJ: return "obj";
    case EMIT_C: return "c";
    case EMIT_EXE:
    default:
      return "exe";
  }
}

static long long now_ms(void);
static bool has_suffix(const char *path, const char *suffix);

static bool is_row_source_path(const char *path) {
  return path && (has_suffix(path, ".0") || has_suffix(path, ".row"));
}

static void direct_input_push_string(char ***items, size_t *len, const char *value) {
  *items = z_checked_reallocarray(*items, *len + 1, sizeof(char *));
  (*items)[(*len)++] = z_strdup(value ? value : "");
}

static void direct_input_push_source_line(SourceInput *input, const char *path, int line) {
  input->source_line_paths = z_checked_reallocarray(input->source_line_paths, input->source_line_count + 1, sizeof(char *));
  input->source_line_numbers = z_checked_reallocarray(input->source_line_numbers, input->source_line_count + 1, sizeof(int));
  input->source_line_paths[input->source_line_count] = z_strdup(path ? path : "");
  input->source_line_numbers[input->source_line_count] = line > 0 ? line : 1;
  input->source_line_count++;
}

static char *direct_module_name_from_path(const char *path) {
  const char *name = path ? strrchr(path, '/') : NULL;
  name = name ? name + 1 : (path ? path : "main");
  size_t len = strlen(name);
  if (len > 4 && strcmp(name + len - 4, ".row") == 0) len -= 4;
  else if (len > 2 && strcmp(name + len - 2, ".0") == 0) len -= 2;
  return len > 0 ? z_strndup(name, len) : z_strdup("main");
}

static char *direct_dirname_of(const char *path) {
  const char *slash = path ? strrchr(path, '/') : NULL;
  if (!slash) return z_strdup(".");
  return z_strndup(path, (size_t)(slash - path));
}

static char *direct_join_path(const char *left, const char *right) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, left && left[0] ? left : ".");
  if (buf.len == 0 || buf.data[buf.len - 1] != '/') zbuf_append_char(&buf, '/');
  zbuf_append(&buf, right ? right : "");
  return buf.data;
}

static bool direct_file_exists(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) return false;
  fclose(file);
  return true;
}

static bool direct_input_has_file(const SourceInput *input, const char *path) {
  for (size_t i = 0; input && i < input->source_file_count; i++) {
    if (strcmp(input->source_files[i], path) == 0) return true;
  }
  return false;
}

static char *direct_row_module_name_from_path(const char *root, const char *path) {
  const char *relative = path ? path : "main";
  size_t root_len = root ? strlen(root) : 0;
  if (root && strncmp(relative, root, root_len) == 0) {
    relative += root_len;
    if (*relative == '/') relative++;
  } else {
    const char *slash = strrchr(relative, '/');
    relative = slash ? slash + 1 : relative;
  }
  size_t len = strlen(relative);
  if (len > 4 && strcmp(relative + len - 4, ".row") == 0) len -= 4;
  else if (len > 2 && strcmp(relative + len - 2, ".0") == 0) len -= 2;
  if (len >= 4 && strcmp(relative + len - 4, "/mod") == 0) len -= 4;
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, relative[i] == '/' ? '.' : relative[i]);
  if (buf.len == 0) zbuf_append(&buf, "main");
  return buf.data;
}

static void direct_input_push_module(SourceInput *input, const char *module, const char *path) {
  input->module_names = z_checked_reallocarray(input->module_names, input->module_count + 1, sizeof(char *));
  input->module_paths = z_checked_reallocarray(input->module_paths, input->module_count + 1, sizeof(char *));
  input->module_names[input->module_count] = z_strdup(module ? module : "");
  input->module_paths[input->module_count] = z_strdup(path ? path : "");
  input->module_count++;
}

static void direct_input_push_import(SourceInput *input, const char *module) {
  direct_input_push_string(&input->imports, &input->import_count, module);
}

static void direct_input_push_import_edge(SourceInput *input, const char *from, const char *to, const char *path, const char *source_path, int line, int column, int length) {
  input->import_from = z_checked_reallocarray(input->import_from, input->import_edge_count + 1, sizeof(char *));
  input->import_to = z_checked_reallocarray(input->import_to, input->import_edge_count + 1, sizeof(char *));
  input->import_paths = z_checked_reallocarray(input->import_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_source_paths = z_checked_reallocarray(input->import_source_paths, input->import_edge_count + 1, sizeof(char *));
  input->import_lines = z_checked_reallocarray(input->import_lines, input->import_edge_count + 1, sizeof(int));
  input->import_columns = z_checked_reallocarray(input->import_columns, input->import_edge_count + 1, sizeof(int));
  input->import_lengths = z_checked_reallocarray(input->import_lengths, input->import_edge_count + 1, sizeof(int));
  input->import_from[input->import_edge_count] = z_strdup(from ? from : "");
  input->import_to[input->import_edge_count] = z_strdup(to ? to : "");
  input->import_paths[input->import_edge_count] = z_strdup(path ? path : "");
  input->import_source_paths[input->import_edge_count] = z_strdup(source_path ? source_path : "");
  input->import_lines[input->import_edge_count] = line > 0 ? line : 1;
  input->import_columns[input->import_edge_count] = column > 0 ? column : 1;
  input->import_lengths[input->import_edge_count] = length > 0 ? length : 1;
  input->import_edge_count++;
}

static void direct_input_add_source_metadata(SourceInput *input) {
  direct_input_push_string(&input->source_files, &input->source_file_count, input->source_file);
  char *module_name = direct_module_name_from_path(input->source_file);
  direct_input_push_module(input, module_name, input->source_file);
  free(module_name);

  int line = 1;
  direct_input_push_source_line(input, input->source_file, line);
  for (const char *cursor = input->source ? input->source : ""; *cursor; cursor++) {
    if (*cursor == '\n') direct_input_push_source_line(input, input->source_file, ++line);
  }
}

static bool load_direct_row_source(const char *path, SourceInput *input, ZDiag *diag) {
  input->source_file = z_strdup(path);
  input->source = z_read_file(path, diag);
  if (!input->source) return false;
  direct_input_add_source_metadata(input);
  return true;
}

static char *format_row_source_text(const char *source, ZDiag *diag) {
  ZRowTokenVec tokens = z_row_tokenize(source, diag);
  if (diag->code != 0) {
    z_free_row_tokens(&tokens);
    return NULL;
  }
  ZRowTree tree = {0};
  if (!z_row_parse_layout(&tokens, &tree, diag)) {
    z_free_row_tree(&tree);
    z_free_row_tokens(&tokens);
    return NULL;
  }
  char *formatted = z_format_row_layout(&tokens, &tree);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
  return formatted;
}

static bool direct_input_push_symbol(SourceInput *input, const char *module, const char *kind, const char *name, bool is_public, ZDiag *diag) {
  if (is_public) {
    for (size_t i = 0; input && i < input->symbol_count; i++) {
      if (input->symbol_public[i] && strcmp(input->symbol_names[i], name ? name : "") == 0 && strcmp(input->symbol_modules[i], module ? module : "") != 0) {
        diag->code = 7003;
        diag->path = z_strdup(input->source_file ? input->source_file : "");
        diag->line = 1;
        diag->column = 1;
        diag->length = 1;
        snprintf(diag->message, sizeof(diag->message), "duplicate public symbol '%s'", name ? name : "");
        snprintf(diag->expected, sizeof(diag->expected), "unique public symbol names across imported modules");
        snprintf(diag->actual, sizeof(diag->actual), "%s also exported by %s", name ? name : "", input->symbol_modules[i]);
        snprintf(diag->help, sizeof(diag->help), "rename one symbol or keep it private inside its module");
        return false;
      }
    }
  }
  input->symbol_names = z_checked_reallocarray(input->symbol_names, input->symbol_count + 1, sizeof(char *));
  input->symbol_modules = z_checked_reallocarray(input->symbol_modules, input->symbol_count + 1, sizeof(char *));
  input->symbol_kinds = z_checked_reallocarray(input->symbol_kinds, input->symbol_count + 1, sizeof(char *));
  input->symbol_public = z_checked_reallocarray(input->symbol_public, input->symbol_count + 1, sizeof(bool));
  input->symbol_names[input->symbol_count] = z_strdup(name ? name : "");
  input->symbol_modules[input->symbol_count] = z_strdup(module ? module : "");
  input->symbol_kinds[input->symbol_count] = z_strdup(kind ? kind : "");
  input->symbol_public[input->symbol_count++] = is_public;
  return true;
}

static bool direct_row_token_text(const ZRowTokenVec *tokens, size_t index, const char *text) {
  return tokens && index < tokens->len && tokens->items[index].text && strcmp(tokens->items[index].text, text) == 0;
}

static const char *direct_row_symbol_kind(const ZRowTokenVec *tokens, size_t index) {
  if (direct_row_token_text(tokens, index, "alias")) return "type-alias";
  if (direct_row_token_text(tokens, index, "const")) return "const";
  if (direct_row_token_text(tokens, index, "fn")) return "function";
  if (direct_row_token_text(tokens, index, "interface")) return "interface";
  if (direct_row_token_text(tokens, index, "type")) return "shape";
  if (direct_row_token_text(tokens, index, "enum")) return "enum";
  if (direct_row_token_text(tokens, index, "choice")) return "choice";
  return NULL;
}

static bool direct_input_add_row_symbols(SourceInput *input, const ZRowTokenVec *tokens, const ZRowTree *tree, const char *module, ZDiag *diag) {
  for (size_t i = 0; input && tokens && tree && i < tree->len; i++) {
    const ZRowNode *node = &tree->items[i];
    if (node->parent != Z_ROW_NO_PARENT) continue;
    size_t pos = node->first_token;
    size_t end = node->first_token + node->token_count;
    bool is_public = false;
    if (direct_row_token_text(tokens, pos, "pub")) {
      is_public = true;
      pos++;
    }
    if (direct_row_token_text(tokens, pos, "export")) {
      pos++;
      if (direct_row_token_text(tokens, pos, "c")) pos++;
    }
    if (direct_row_token_text(tokens, pos, "extern") || direct_row_token_text(tokens, pos, "packed")) {
      pos++;
      if (direct_row_token_text(tokens, pos, "c")) continue;
    }
    if (direct_row_token_text(tokens, pos, "test")) continue;
    const char *kind = direct_row_symbol_kind(tokens, pos);
    if (!kind) continue;
    size_t name_index = pos + 1;
    if (name_index >= end || tokens->items[name_index].kind != Z_ROW_TOKEN_WORD) continue;
    if (!direct_input_push_symbol(input, module, kind, tokens->items[name_index].text, is_public, diag)) return false;
  }
  return !diag || diag->code == 0;
}

static bool parse_row_source_text(const char *source, Program *program, ZDiag *diag) {
  ZRowTokenVec tokens = z_row_tokenize(source, diag);
  if (diag->code != 0) {
    z_free_row_tokens(&tokens);
    return false;
  }
  ZRowTree tree = {0};
  if (!z_row_parse_layout(&tokens, &tree, diag)) {
    z_free_row_tree(&tree);
    z_free_row_tokens(&tokens);
    return false;
  }
  *program = z_parse_row(&tokens, &tree, diag);
  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
  return diag->code == 0;
}

static char *direct_row_join_module(const ZRowTokenVec *tokens, size_t start, size_t end) {
  ZBuf buf;
  zbuf_init(&buf);
  for (size_t i = start; tokens && i < end && i < tokens->len; i++) zbuf_append(&buf, tokens->items[i].text);
  return buf.data ? buf.data : z_strdup("");
}

static bool direct_row_reserved_word(const char *text) {
  const char *keywords[] = {
    "as", "break", "check", "choice", "const", "continue", "defer", "else", "enum", "export", "extern", "false",
    "fn", "for", "fun", "if", "import", "in", "let", "match", "meta", "mut", "null", "packed", "pub",
    "raise", "raises", "rescue", "ret", "return", "shape", "static", "test", "true", "type",
    "use", "var", "while", NULL
  };
  for (int i = 0; keywords[i]; i++) {
    if (text && strcmp(text, keywords[i]) == 0) return true;
  }
  return false;
}

static void direct_row_parse_diag(ZDiag *diag, const ZRowToken *token, const char *message, const char *expected) {
  diag->code = 100;
  diag->line = token ? token->line : 1;
  diag->column = token ? token->column : 1;
  diag->length = token && token->length > 0 ? (int)token->length : 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "row syntax error");
  snprintf(diag->expected, sizeof(diag->expected), "%s", expected ? expected : "");
}

static bool direct_row_validate_use(const ZRowTokenVec *tokens, size_t module_start, size_t module_end, size_t row_end, ZDiag *diag) {
  if (module_start >= module_end) {
    const ZRowToken *token = tokens && module_start < tokens->len ? &tokens->items[module_start] : (tokens && module_start > 0 ? &tokens->items[module_start - 1] : NULL);
    direct_row_parse_diag(diag, token, "expected import module name", "import module name");
    return false;
  }

  bool expect_segment = true;
  for (size_t i = module_start; tokens && i < module_end; i++) {
    const ZRowToken *token = &tokens->items[i];
    if (expect_segment) {
      if (token->kind != Z_ROW_TOKEN_WORD || direct_row_reserved_word(token->text)) {
        direct_row_parse_diag(diag,
                              token,
                              i == module_start ? "expected import module name" : "expected import module segment",
                              i == module_start ? "import module name" : "import module segment");
        return false;
      }
      expect_segment = false;
      continue;
    }
    if (!direct_row_token_text(tokens, i, ".")) {
      direct_row_parse_diag(diag, token, "expected '.' between import module segments", "'.' or import alias");
      return false;
    }
    expect_segment = true;
  }

  if (expect_segment) {
    const ZRowToken *token = tokens && module_end > 0 ? &tokens->items[module_end - 1] : NULL;
    direct_row_parse_diag(diag, token, "expected import module segment", "import module segment");
    return false;
  }

  if (direct_row_token_text(tokens, module_end, "as")) {
    size_t alias_pos = module_end + 1;
    const ZRowToken *alias = tokens && alias_pos < row_end ? &tokens->items[alias_pos] : (tokens && module_end < tokens->len ? &tokens->items[module_end] : NULL);
    if (!alias || alias->kind != Z_ROW_TOKEN_WORD) {
      direct_row_parse_diag(diag, alias, "expected import alias", "identifier");
      return false;
    }
    if (direct_row_reserved_word(alias->text)) {
      direct_row_parse_diag(diag, alias, "reserved word cannot be used as an identifier", "identifier");
      snprintf(diag->help, sizeof(diag->help), "choose a non-keyword name");
      return false;
    }
    alias_pos++;
    if (alias_pos < row_end) {
      direct_row_parse_diag(diag, &tokens->items[alias_pos], "expected end of row after import alias", "end of row");
      return false;
    }
  }
  return true;
}

static char *direct_row_module_path(const char *root, const char *module) {
  ZBuf relative;
  zbuf_init(&relative);
  for (const char *cursor = module ? module : ""; *cursor; cursor++) {
    zbuf_append_char(&relative, *cursor == '.' ? '/' : *cursor);
  }
  zbuf_append(&relative, ".0");
  char *file_path = direct_join_path(root, relative.data);
  if (direct_file_exists(file_path)) {
    zbuf_free(&relative);
    return file_path;
  }
  free(file_path);
  if (relative.len >= 2 && strcmp(relative.data + relative.len - 2, ".0") == 0) {
    relative.data[relative.len - 2] = 0;
    relative.len -= 2;
  }
  char *dir_path = direct_join_path(root, relative.data);
  char *mod_path = direct_join_path(dir_path, "mod.0");
  free(dir_path);
  if (direct_file_exists(mod_path)) {
    zbuf_free(&relative);
    return mod_path;
  }
  free(mod_path);
  zbuf_append(&relative, ".row");
  file_path = direct_join_path(root, relative.data);
  if (direct_file_exists(file_path)) {
    zbuf_free(&relative);
    return file_path;
  }
  free(file_path);
  if (relative.len >= 4 && strcmp(relative.data + relative.len - 4, ".row") == 0) {
    relative.data[relative.len - 4] = 0;
    relative.len -= 4;
  }
  dir_path = direct_join_path(root, relative.data);
  mod_path = direct_join_path(dir_path, "mod.row");
  free(dir_path);
  zbuf_free(&relative);
  if (direct_file_exists(mod_path)) return mod_path;
  free(mod_path);
  return NULL;
}

static bool direct_row_stack_contains(char **stack, size_t len, const char *path) {
  for (size_t i = 0; i < len; i++) {
    if (strcmp(stack[i], path) == 0) return true;
  }
  return false;
}

static void direct_row_format_cycle_chain(const char *root, char **stack, size_t stack_len, const char *path, ZBuf *out) {
  bool started = false;
  for (size_t i = 0; i < stack_len; i++) {
    if (!started && strcmp(stack[i], path) != 0) continue;
    started = true;
    if (out->len > 0) zbuf_append(out, " -> ");
    char *module = direct_row_module_name_from_path(root, stack[i]);
    zbuf_append(out, module);
    free(module);
  }
  if (out->len > 0) zbuf_append(out, " -> ");
  char *module = direct_row_module_name_from_path(root, path);
  zbuf_append(out, module);
  free(module);
}

static void direct_row_set_cycle_diag(ZDiag *diag, const char *root, char **stack, size_t stack_len, const char *cycle_path, const char *diag_path, int line, int column, int length) {
  ZBuf chain;
  zbuf_init(&chain);
  direct_row_format_cycle_chain(root, stack, stack_len, cycle_path, &chain);
  diag->code = 7002;
  diag->path = z_strdup(diag_path ? diag_path : "");
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = length > 0 ? length : 1;
  snprintf(diag->message, sizeof(diag->message), "import cycle detected");
  snprintf(diag->actual, sizeof(diag->actual), "%s", chain.data ? chain.data : "");
  snprintf(diag->help, sizeof(diag->help), "break the module cycle by moving shared declarations into a third module");
  zbuf_free(&chain);
}

static void direct_row_append_source(SourceInput *input, ZBuf *combined, const char *path, const char *source) {
  const char *line = source ? source : "";
  int original_line = 1;
  if (!*line) {
    zbuf_append_char(combined, '\n');
    direct_input_push_source_line(input, path, original_line);
    return;
  }
  while (*line) {
    const char *end = strchr(line, '\n');
    size_t len = end ? (size_t)(end - line) : strlen(line);
    zbuf_appendf(combined, "%.*s\n", (int)len, line);
    direct_input_push_source_line(input, path, original_line++);
    if (!end) break;
    line = end + 1;
  }
  zbuf_append_char(combined, '\n');
  direct_input_push_source_line(input, path, original_line);
}

static bool direct_row_resolve_file(const char *path, const char *root, SourceInput *input, ZBuf *combined, ZDiag *diag, char ***stack, size_t *stack_len) {
  if (direct_input_has_file(input, path)) return true;
  if (direct_row_stack_contains(*stack, *stack_len, path)) {
    direct_row_set_cycle_diag(diag, root, *stack, *stack_len, path, path, 1, 1, 1);
    return false;
  }

  char *source = z_read_file(path, diag);
  if (!source) return false;
  char *module = direct_row_module_name_from_path(root, path);
  *stack = z_checked_reallocarray(*stack, *stack_len + 1, sizeof(char *));
  (*stack)[(*stack_len)++] = z_strdup(path);

  ZRowTokenVec tokens = z_row_tokenize(source, diag);
  ZRowTree tree = {0};
  if (diag->code == 0) z_row_parse_layout(&tokens, &tree, diag);
  if (diag->code != 0) {
    if (!diag->path) diag->path = z_strdup(path);
    z_free_row_tree(&tree);
    z_free_row_tokens(&tokens);
    free((*stack)[--(*stack_len)]);
    free(module);
    free(source);
    return false;
  }

  for (size_t i = 0; i < tree.len && diag->code == 0; i++) {
    const ZRowNode *node = &tree.items[i];
    if (node->parent != Z_ROW_NO_PARENT) continue;
    size_t pos = node->first_token;
    size_t end = node->first_token + node->token_count;
    if (!direct_row_token_text(&tokens, pos, "use")) continue;
    size_t module_start = pos + 1;
    size_t module_end = module_start;
    while (module_end < end && !direct_row_token_text(&tokens, module_end, "as")) module_end++;
    if (!direct_row_validate_use(&tokens, module_start, module_end, end, diag)) {
      if (!diag->path) diag->path = z_strdup(path);
      break;
    }
    char *import_name = direct_row_join_module(&tokens, module_start, module_end);
    if (strncmp(import_name, "std.", 4) == 0) {
      free(import_name);
      continue;
    }
    direct_input_push_import(input, import_name);
    char *import_path = direct_row_module_path(root, import_name);
    if (!import_path) {
      const ZRowToken *token = module_start < end ? &tokens.items[module_start] : &tokens.items[pos];
      diag->code = 7001;
      diag->path = z_strdup(path);
      diag->line = token->line;
      diag->column = token->column;
      diag->length = module_end > module_start ? (int)(tokens.items[module_end - 1].column + tokens.items[module_end - 1].length - token->column) : 1;
      snprintf(diag->message, sizeof(diag->message), "unknown package-local import '%s'", import_name);
      snprintf(diag->expected, sizeof(diag->expected), "%s.0 or %s/mod.0", import_name, import_name);
      snprintf(diag->actual, sizeof(diag->actual), "missing source file");
      snprintf(diag->help, sizeof(diag->help), "create the module source file or remove the import");
      free(import_name);
      break;
    }
    int import_length = module_end > module_start
      ? (int)(tokens.items[module_end - 1].column + tokens.items[module_end - 1].length - tokens.items[pos].column)
      : (int)tokens.items[pos].length;
    if (direct_row_stack_contains(*stack, *stack_len, import_path)) {
      direct_row_set_cycle_diag(diag, root, *stack, *stack_len, import_path, path, tokens.items[pos].line, tokens.items[pos].column, import_length);
      free(import_path);
      free(import_name);
      break;
    }
    direct_input_push_import_edge(input, module, import_name, import_path, path, tokens.items[pos].line, tokens.items[pos].column, import_length);
    bool ok = direct_row_resolve_file(import_path, root, input, combined, diag, stack, stack_len);
    free(import_path);
    free(import_name);
    if (!ok) break;
  }

  if (diag->code == 0) {
    direct_input_push_string(&input->source_files, &input->source_file_count, path);
    direct_input_push_module(input, module, path);
    if (direct_input_add_row_symbols(input, &tokens, &tree, module, diag)) {
      direct_row_append_source(input, combined, path, source);
    }
  }

  z_free_row_tree(&tree);
  z_free_row_tokens(&tokens);
  free((*stack)[--(*stack_len)]);
  free(module);
  free(source);
  return diag->code == 0;
}

static bool resolve_direct_row_source_at_root(const char *path, const char *root, SourceInput *input, ZDiag *diag) {
  input->source_file = z_strdup(path);
  ZBuf combined;
  zbuf_init(&combined);
  char **stack = NULL;
  size_t stack_len = 0;
  bool ok = direct_row_resolve_file(path, root, input, &combined, diag, &stack, &stack_len);
  free(stack);
  input->source = ok ? combined.data : NULL;
  if (!ok) zbuf_free(&combined);
  return ok;
}

static bool resolve_direct_row_source(const char *path, SourceInput *input, ZDiag *diag) {
  char *root = direct_dirname_of(path);
  bool ok = resolve_direct_row_source_at_root(path, root, input, diag);
  free(root);
  return ok;
}

static char *direct_manifest_path_for_input(const char *input_path) {
  if (strcmp(input_path, "zero.json") == 0 || has_suffix(input_path, "/zero.json")) return z_strdup(input_path);
  char *manifest_path = direct_join_path(input_path, "zero.json");
  if (direct_file_exists(manifest_path)) return manifest_path;
  free(manifest_path);
  return NULL;
}

static bool resolve_direct_row_package_source(const char *input_path, SourceInput *input, ZDiag *diag, bool *handled) {
  *handled = false;
  char *manifest_path = direct_manifest_path_for_input(input_path);
  if (!manifest_path) return false;
  *handled = true;

  char *manifest = z_read_file(manifest_path, diag);
  if (!manifest) {
    diag->path = z_strdup(manifest_path);
    free(manifest_path);
    return false;
  }
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, diag)) {
    diag->path = z_strdup(manifest_path);
    free(manifest);
    free(manifest_path);
    return false;
  }
  if (!parsed_manifest.main_path || !is_row_source_path(parsed_manifest.main_path)) {
    diag->code = 2002;
    diag->path = z_strdup(manifest_path);
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "target main source must use current Zero syntax");
    snprintf(diag->expected, sizeof(diag->expected), ".0 or .row source file");
    snprintf(diag->actual, sizeof(diag->actual), "%s", parsed_manifest.main_path ? parsed_manifest.main_path : "missing targets.cli.main");
    snprintf(diag->help, sizeof(diag->help), "set targets.cli.main to a current Zero source file");
    z_free_manifest(&parsed_manifest);
    free(manifest);
    free(manifest_path);
    return false;
  }

  bool ok = z_resolve_package_metadata(manifest_path, manifest, &parsed_manifest, input, diag);
  if (ok) {
    char *source_file = direct_join_path(input->package_root, parsed_manifest.main_path);
    if (!direct_file_exists(source_file)) {
      diag->code = 2002;
      diag->path = input->manifest_path;
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "target main source does not exist");
      snprintf(diag->expected, sizeof(diag->expected), "%s", source_file);
      snprintf(diag->actual, sizeof(diag->actual), "missing source file");
      snprintf(diag->help, sizeof(diag->help), "create the main source file or update targets.cli.main");
      ok = false;
    } else {
      char *src_root = direct_join_path(input->package_root, "src");
      ok = resolve_direct_row_source_at_root(source_file, src_root, input, diag);
      free(src_root);
    }
    free(source_file);
  }

  z_free_manifest(&parsed_manifest);
  free(manifest);
  free(manifest_path);
  return ok;
}

static void set_source_input_diag(const char *input_path, ZDiag *diag) {
  diag->code = 2002;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "expected Zero source file or package");
  snprintf(diag->expected, sizeof(diag->expected), ".0 source file, .row source file, zero.json, or package directory");
  snprintf(diag->actual, sizeof(diag->actual), "%s", input_path ? input_path : "");
  snprintf(diag->help, sizeof(diag->help), "pass a current Zero source file or a package with targets.cli.main");
}

static bool load_command_source(const char *input_path, SourceInput *input, ZDiag *diag) {
  if (is_row_source_path(input_path)) {
    return load_direct_row_source(input_path, input, diag);
  }
  bool handled_row_package = false;
  if (resolve_direct_row_package_source(input_path, input, diag, &handled_row_package)) {
    return true;
  }
  if (handled_row_package) return false;
  set_source_input_diag(input_path, diag);
  return false;
}

static bool check_row_input(const ZTargetInfo *target, SourceInput *input, Program *program, ZDiag *diag, long long resolve_started_ms) {
  input->resolve_ms = now_ms() - resolve_started_ms;
  diag->path = input->source_file;
  long long phase_started = now_ms();
  if (!parse_row_source_text(input->source, program, diag)) {
    z_map_source_diag(input, diag);
    return false;
  }
  input->parse_ms = now_ms() - phase_started;
  input->interface_ms = input->resolve_ms;
  input->parse_cache_hit = compiler_cache_touch("parse-tree", compile_cache_key(input, NULL, NULL, "parse-tree"));
  input->interface_cache_hit = compiler_cache_touch("interface", source_interface_hash(input));
  input->check_cache_hit = compiler_cache_touch("checked-body", compile_cache_key(input, target, NULL, "checked-body"));
  input->specialization_cache_hit = compiler_cache_touch("specialization", compile_cache_key(input, target, "release", "specialization"));
  z_set_check_target(target);
  phase_started = now_ms();
  if (!z_check_program(program, diag)) {
    z_map_source_diag(input, diag);
    return false;
  }
  input->check_ms = now_ms() - phase_started;
  return true;
}

static bool compile_input(const char *input_path, const ZTargetInfo *target, SourceInput *input, Program *program, ZDiag *diag) {
  long long phase_started = now_ms();
  bool handled_row_package = false;
  if (resolve_direct_row_package_source(input_path, input, diag, &handled_row_package)) {
    return check_row_input(target, input, program, diag, phase_started);
  }
  if (handled_row_package) return false;

  if (is_row_source_path(input_path)) {
    if (!resolve_direct_row_source(input_path, input, diag)) return false;
    return check_row_input(target, input, program, diag, phase_started);
  }

  set_source_input_diag(input_path, diag);
  return false;
}

static bool has_suffix(const char *path, const char *suffix) {
  size_t path_len = strlen(path);
  size_t suffix_len = strlen(suffix);
  return path_len >= suffix_len && strcmp(path + path_len - suffix_len, suffix) == 0;
}

static char *apply_target_suffix(const char *path, const ZTargetInfo *target) {
  const char *suffix = target ? target->exe_suffix : "";
  if (suffix && suffix[0] && !has_suffix(path, suffix)) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, path);
    zbuf_append(&buf, suffix);
    return buf.data;
  }
  return z_strdup(path);
}

static const char *backend_blocker_backend_name(const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  if (emit_kind && strcmp(emit_kind, "obj") == 0) return z_direct_object_emitter(target);
  if (emit_kind && strcmp(emit_kind, "exe") == 0) {
    if (command && command->backend && command->backend[0]) return command->backend;
    return z_direct_exe_emitter(target);
  }
  return "none";
}

static void complete_backend_blocker_diag(ZDiag *diag, const ZTargetInfo *target, const Command *command, const char *emit_kind, const char *stage) {
  if (!diag || (diag->code != 4004 && diag->code != 2004)) return;
  const char *blocker_stage = diag->backend_blocker.present && diag->backend_blocker.stage[0] ? diag->backend_blocker.stage : stage;
  const char *unsupported_feature = diag->backend_blocker.present && diag->backend_blocker.unsupported_feature[0] ? diag->backend_blocker.unsupported_feature : diag->actual;
  ZBackendBlocker blocker;
  z_backend_blocker_set(&blocker,
                        target && target->name ? target->name : "unknown",
                        target && target->object_format ? target->object_format : "unknown",
                        backend_blocker_backend_name(target, command, emit_kind),
                        blocker_stage && blocker_stage[0] ? blocker_stage : "emit",
                        unsupported_feature && unsupported_feature[0] ? unsupported_feature : "unsupported construct");
  z_diag_set_backend_blocker(diag, &blocker);
}

static void init_direct_backend_diag(ZDiag *diag, const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason) {
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->path = input ? input->source_file : NULL;
  diag->line = 1;
  diag->column = 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "direct backend does not support target '%s' for --emit %s",
           target && target->name ? target->name : "unknown",
           emit_kind ? emit_kind : "unknown");
  snprintf(diag->expected, sizeof(diag->expected), "direct target with matching object format and architecture");
  snprintf(diag->actual, sizeof(diag->actual), "target=%s objectFormat=%s arch=%s abi=%s status=%s",
           target && target->name ? target->name : "unknown",
           target && target->object_format ? target->object_format : "unknown",
           target && target->arch ? target->arch : "unknown",
           target && target->abi ? target->abi : "",
           z_direct_backend_status(target));
  snprintf(diag->help, sizeof(diag->help), "%s", reason ? reason : z_direct_backend_reason(target));
  complete_backend_blocker_diag(diag, target, command, emit_kind, "select");
}

static int return_direct_backend_error(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const char *reason, IrProgram *ir, Program *program) {
  ZDiag diag;
  init_direct_backend_diag(&diag, command, input, target, emit_kind, reason);
  if (command && command->json) print_diag_json(input ? input->source_file : NULL, &diag);
  else print_diag(input ? input->source_file : NULL, &diag);
  if (ir) z_free_ir_program(ir);
  if (program) z_free_program(program);
  return 1;
}

static int return_buildability_error(const Command *command, const SourceInput *input, ZDiag *diag, IrProgram *ir, Program *program) {
  if (input && diag) {
    z_map_source_diag(input, diag);
    if (!diag->path) diag->path = input->source_file;
  }
  if (command && command->json) print_diag_json(input ? input->source_file : NULL, diag);
  else print_diag(input ? input->source_file : NULL, diag);
  if (ir) z_free_ir_program(ir);
  if (program) z_free_program(program);
  return 1;
}

static bool direct_buildability_preflight(const Command *command, const SourceInput *input, const ZTargetInfo *target, const char *emit_kind, const IrProgram *ir, ZDiag *diag) {
  if (ir && !ir->mir_valid) {
    init_lowering_backend_diag(diag, input, target, command, ir);
    return false;
  }
  return z_direct_buildability_check(ir, target, emit_kind, diag);
}

static void format_file_size(long long bytes, char *out, size_t out_len) {
  const char *units[] = {"B", "KiB", "MiB", "GiB"};
  double size = (double)bytes;
  size_t unit = 0;
  while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
    size /= 1024.0;
    unit++;
  }
  if (unit == 0) snprintf(out, out_len, "%lld %s", bytes, units[unit]);
  else snprintf(out, out_len, "%.1f %s", size, units[unit]);
}

static long long now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void format_duration(long long elapsed_ms, char *out, size_t out_len) {
  if (elapsed_ms < 1000) snprintf(out, out_len, "%lld ms", elapsed_ms);
  else snprintf(out, out_len, "%.1f s", (double)elapsed_ms / 1000.0);
}

static void print_artifact(const char *path, long long elapsed_ms) {
  char duration[32];
  format_duration(elapsed_ms, duration, sizeof(duration));

  struct stat st;
  if (stat(path, &st) == 0) {
    char size[32];
    format_file_size((long long)st.st_size, size, sizeof(size));
    printf("%s (%s, %s)\n", path, size, duration);
  } else {
    printf("%s (%s)\n", path, duration);
  }
}

static int run_executable_artifact(const char *exe_file, const Command *command) {
  int run_argc = command ? command->run_argc : 0;
  if (run_argc < 0) run_argc = 0;
  char **child_argv = (char **)z_checked_calloc((size_t)run_argc + 2, sizeof(char *));
  child_argv[0] = (char *)exe_file;
  for (int i = 0; i < run_argc; i++) child_argv[i + 1] = command->run_argv[i];
  child_argv[run_argc + 1] = NULL;

  fflush(NULL);
#if defined(_WIN32)
  intptr_t status = _spawnv(_P_WAIT, exe_file, (const char *const *)child_argv);
  free(child_argv);
  if (status < 0) {
    perror("zero run");
    return 1;
  }
  return (int)status;
#else
  pid_t pid = fork();
  if (pid == 0) {
    execv(exe_file, child_argv);
    perror("zero run");
    _exit(127);
  }
  free(child_argv);
  if (pid < 0) {
    perror("zero run");
    return 1;
  }

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno == EINTR) continue;
    perror("zero run");
    return 1;
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
#endif
}

static const char *build_compiler_label(const Command *command, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  return plan.compiler;
}

static const char *profile_canonical_name(const char *profile) {
  if (!profile || strcmp(profile, "release") == 0 || strcmp(profile, "release-small") == 0 || strcmp(profile, "small") == 0) return "release-small";
  if (strcmp(profile, "release-fast") == 0 || strcmp(profile, "fast") == 0) return "release-fast";
  if (strcmp(profile, "tiny") == 0) return "tiny";
  if (strcmp(profile, "debug") == 0) return "debug";
  if (strcmp(profile, "dev") == 0) return "dev";
  if (strcmp(profile, "audit") == 0) return "audit";
  return "release-small";
}

static const char *profile_overflow_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0) return "checked";
  if (strcmp(canonical, "tiny") == 0) return "abort-on-trap";
  return "profile-defined-trap";
}

static const char *profile_bounds_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "release-fast") == 0) return "checked-with-optimizer-elision";
  if (strcmp(canonical, "tiny") == 0) return "checked-minimal-trap";
  return "checked";
}

static const char *profile_panic_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0) return "diagnostic-trap";
  return "abort";
}

static bool profile_debug_info(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  return strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0;
}

static const char *profile_runtime_metadata(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "audit") == 0) return "maximum";
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "debug";
  if (strcmp(canonical, "tiny") == 0) return "minimum";
  return "pay-as-used";
}

static const char *profile_key_name(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "release-fast") == 0) return "fast";
  if (strcmp(canonical, "release-small") == 0) return "small";
  return canonical;
}

static const char *profile_optimization_goal(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0) return "observability";
  if (strcmp(canonical, "dev") == 0) return "edit-latency";
  if (strcmp(canonical, "release-fast") == 0) return "throughput";
  if (strcmp(canonical, "tiny") == 0) return "minimum-binary-size";
  if (strcmp(canonical, "audit") == 0) return "release-auditability";
  return "small-binary-size";
}

static const char *profile_codegen_optimization(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "none";
  if (strcmp(canonical, "release-fast") == 0) return "speed";
  if (strcmp(canonical, "tiny") == 0) return "size-min";
  if (strcmp(canonical, "audit") == 0) return "size-with-audit-metadata";
  return "size";
}

static const char *profile_link_optimization(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "keep-debug-names";
  if (strcmp(canonical, "tiny") == 0) return "section-gc-strip-minimal-metadata";
  if (strcmp(canonical, "audit") == 0) return "section-gc-keep-audit-metadata";
  return "section-gc-strip";
}

static const char *profile_unwind_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0 || strcmp(canonical, "audit") == 0) return "no-unwind-diagnostic-trap";
  return "no-unwind-abort";
}

static const char *profile_symbol_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "keep-local-and-public-names";
  if (strcmp(canonical, "audit") == 0) return "keep-public-and-audit-names";
  if (strcmp(canonical, "tiny") == 0) return "strip-all-unrequested-names";
  return "keep-public-names";
}

static int profile_max_hello_bytes(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "audit") == 0) return 65536;
  if (strcmp(canonical, "dev") == 0) return 32768;
  if (strcmp(canonical, "release-fast") == 0) return 24576;
  if (strcmp(canonical, "tiny") == 0) return 10240;
  return 12288;
}

static const char *profile_helper_budget_policy(const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  if (strcmp(canonical, "debug") == 0 || strcmp(canonical, "dev") == 0) return "pay-as-used-debug-metadata-allowed";
  if (strcmp(canonical, "audit") == 0) return "pay-as-used-audit-metadata-allowed";
  if (strcmp(canonical, "tiny") == 0) return "pay-as-used-minimum-runtime";
  return "pay-as-used";
}

static void append_profile_aliases_json(ZBuf *buf, const char *profile) {
  const char *canonical = profile_canonical_name(profile);
  zbuf_append(buf, "[");
  if (strcmp(canonical, "release-small") == 0) {
    append_json_string(buf, "small");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release-small");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release");
  } else if (strcmp(canonical, "release-fast") == 0) {
    append_json_string(buf, "fast");
    zbuf_append(buf, ", ");
    append_json_string(buf, "release-fast");
  } else {
    append_json_string(buf, canonical);
  }
  zbuf_append(buf, "]");
}

static void append_profile_budget_json(ZBuf *buf, const char *profile) {
  zbuf_append(buf, "{\"maxHelloArtifactBytes\":");
  zbuf_appendf(buf, "%d", profile_max_hello_bytes(profile));
  zbuf_append(buf, ",\"helperBudgetPolicy\":");
  append_json_string(buf, profile_helper_budget_policy(profile));
  zbuf_append(buf, ",\"debugMetadataAllowed\":");
  zbuf_append(buf, profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"metadataRetention\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"deterministicArtifacts\":true,\"generatedCBytes\":0,\"cBridgeFallback\":false}");
}

static void append_profile_semantics_json(ZBuf *buf, const char *profile) {
  zbuf_append(buf, "{\"requested\":");
  append_json_string(buf, profile ? profile : "release");
  zbuf_append(buf, ",\"canonical\":");
  append_json_string(buf, profile_canonical_name(profile));
  zbuf_append(buf, ",\"profileKey\":");
  append_json_string(buf, profile_key_name(profile));
  zbuf_append(buf, ",\"aliases\":");
  append_profile_aliases_json(buf, profile);
  zbuf_append(buf, ",\"optimizationGoal\":");
  append_json_string(buf, profile_optimization_goal(profile));
  zbuf_append(buf, ",\"codegenOptimization\":");
  append_json_string(buf, profile_codegen_optimization(profile));
  zbuf_append(buf, ",\"linkOptimization\":");
  append_json_string(buf, profile_link_optimization(profile));
  zbuf_append(buf, ",\"overflowPolicy\":");
  append_json_string(buf, profile_overflow_policy(profile));
  zbuf_append(buf, ",\"boundsPolicy\":");
  append_json_string(buf, profile_bounds_policy(profile));
  zbuf_append(buf, ",\"panicPolicy\":");
  append_json_string(buf, profile_panic_policy(profile));
  zbuf_append(buf, ",\"unwindPolicy\":");
  append_json_string(buf, profile_unwind_policy(profile));
  zbuf_appendf(buf, ",\"debugInfo\":%s", profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"runtimeMetadataPolicy\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"symbolPolicy\":");
  append_json_string(buf, profile_symbol_policy(profile));
  zbuf_append(buf, ",\"panicFormatting\":");
  zbuf_append(buf, profile_debug_info(profile) ? "true" : "false");
  zbuf_append(buf, ",\"profileBudget\":");
  append_profile_budget_json(buf, profile);
  zbuf_append(buf, "}");
}

static void append_profile_catalog_json(ZBuf *buf) {
  const char *profiles[] = {"debug", "dev", "fast", "small", "tiny", "audit", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; profiles[i]; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_profile_semantics_json(buf, profiles[i]);
  }
  zbuf_append(buf, "]");
}

static const char *public_driver_kind(const char *driver_kind) {
  if (driver_kind && strcmp(driver_kind, "zig-cc") == 0) return "target-cc";
  return driver_kind ? driver_kind : "";
}

static const char *public_compiler_label(const ZToolchainPlan *plan) {
  if (plan && strcmp(plan->driver_kind, "zig-cc") == 0) return "target-capable C compiler";
  return plan && plan->compiler ? plan->compiler : "";
}

static const char *public_linker_label(const char *linker) {
  if (linker && strcmp(linker, "zig cc") == 0) return "target-cc";
  return linker ? linker : "";
}

static void append_toolchain_plan_json(ZBuf *buf, const Command *command, const ZTargetInfo *target) {
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  zbuf_append(buf, "{\"driverKind\":");
  append_json_string(buf, public_driver_kind(plan.driver_kind));
  zbuf_append(buf, ",\"selectionSource\":");
  append_json_string(buf, plan.selection_source);
  zbuf_append(buf, ",\"compiler\":");
  append_json_string(buf, public_compiler_label(&plan));
  zbuf_append(buf, ",\"targetTriple\":");
  append_json_string(buf, plan.target_triple);
  zbuf_append(buf, ",\"linkerFlavor\":");
  append_json_string(buf, public_linker_label(plan.linker_flavor));
  zbuf_append(buf, ",\"libcMode\":");
  append_json_string(buf, plan.libc_mode);
  zbuf_appendf(buf, ",\"requiresSysroot\":%s", plan.requires_sysroot ? "true" : "false");
  zbuf_append(buf, ",\"sysrootEnv\":");
  append_json_string(buf, plan.sysroot_env);
  zbuf_append(buf, ",\"sysrootStatus\":");
  append_json_string(buf, plan.sysroot_status);
  zbuf_appendf(buf, ",\"usesTargetFlag\":%s", plan.uses_target_flag ? "true" : "false");
  zbuf_appendf(buf, ",\"usesToolchainCache\":%s", plan.uses_zig_cache ? "true" : "false");
  zbuf_appendf(buf, ",\"stripArtifact\":%s", plan.strip_artifact ? "true" : "false");
  zbuf_append(buf, "}");
}

static void append_direct_backend_facts_json(ZBuf *buf, const SourceInput *input) {
  bool uses_allocator = input && input->direct_allocator_helper_count > 0;
  bool uses_buffers = input && input->direct_buffer_helper_count > 0;
  bool exports_memory = input && (input->direct_stack_bytes > 0 || input->direct_readonly_data_bytes > 0 || uses_allocator);
  size_t actual_export_count = input ? input->direct_export_count + (exports_memory ? 1 : 0) : 0;
  zbuf_appendf(buf, ",\"directFacts\":{\"functionCount\":%zu,\"exportCount\":%zu,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"allocatorHelperCount\":%zu,\"bufferHelperCount\":%zu,\"runtimeHelperCount\":%zu,\"sourceFileCount\":%zu,\"moduleCount\":%zu,\"runtime\":{\"linearMemory\":%s,\"memoryPageBytes\":65536,\"stackBase\":65536,\"stackBytes\":%zu,\"readonlyDataBytes\":%zu,\"heapStart\":null,\"heapEnd\":null,\"heapPolicy\":\"%s\",\"allocator\":\"%s\",\"buffer\":\"%s\",\"boundsTraps\":\"pay-as-used\"}}",
              input ? input->direct_function_count : 0,
              actual_export_count,
              input ? input->direct_stack_bytes : 0,
              input ? input->direct_max_frame_bytes : 0,
              input ? input->direct_readonly_data_bytes : 0,
              input ? input->direct_allocator_helper_count : 0,
              input ? input->direct_buffer_helper_count : 0,
              input ? input->direct_runtime_helper_count : 0,
              input ? input->source_file_count : 0,
              input ? input->module_count : 0,
              exports_memory ? "true" : "false",
              input ? input->direct_stack_bytes : 0,
              input ? input->direct_readonly_data_bytes : 0,
              uses_allocator ? "explicit-fixed-buffer-bump" : "not-yet-enabled",
              uses_allocator ? "FixedBufAlloc" : "none",
              uses_buffers ? "Vec<u8>" : "none");
}

typedef struct {
  bool fd_write;
  bool fd_read;
  bool fd_close;
  bool path_open;
  bool args_sizes_get;
  bool args_get;
  bool environ_sizes_get;
  bool environ_get;
  bool clock_time_get;
  bool random_get;
  bool fd_filestat_get;
  bool fd_readdir;
  bool path_create_directory;
  bool path_remove_directory;
  bool path_unlink_file;
  bool path_rename;
  bool zero_json_parse_bytes;
  bool zero_http_fetch_result;
  bool zero_http_result_ok;
  bool zero_http_result_status;
  bool zero_http_result_body_len;
  bool zero_http_result_error;
  bool zero_http_response_len;
  bool zero_http_response_headers_len;
  bool zero_http_response_body_offset;
  bool zero_http_header_value;
  bool zero_http_header_found;
  bool zero_http_header_offset;
  bool zero_http_header_len;
} RuntimeImportAudit;

static void runtime_import_audit_mark_fs_base(RuntimeImportAudit *audit) {
  if (!audit) return;
  audit->fd_write = true;
  audit->fd_read = true;
  audit->fd_close = true;
  audit->path_open = true;
}

static void runtime_import_audit_value(const IrValue *value, RuntimeImportAudit *audit) {
  if (!value || !audit) return;
  switch (value->kind) {
    case IR_VALUE_ARGS_LEN:
    case IR_VALUE_ARGS_GET:
      audit->args_sizes_get = true;
      audit->args_get = true;
      break;
    case IR_VALUE_ENV_GET:
      audit->environ_sizes_get = true;
      audit->environ_get = true;
      break;
    case IR_VALUE_TIME_WALL_SECONDS:
    case IR_VALUE_TIME_MONOTONIC:
      audit->clock_time_get = true;
      break;
    case IR_VALUE_RAND_ENTROPY_U32:
      audit->random_get = true;
      break;
    case IR_VALUE_FS_OPEN:
    case IR_VALUE_FS_CREATE:
    case IR_VALUE_FS_READ_PATH:
    case IR_VALUE_FS_WRITE_PATH:
    case IR_VALUE_FS_READ_BYTES_PATH:
    case IR_VALUE_FS_WRITE_BYTES_PATH:
    case IR_VALUE_FS_READ_ALL:
    case IR_VALUE_FS_READ_FILE:
    case IR_VALUE_FS_WRITE_ALL_FILE:
    case IR_VALUE_FS_CLOSE_FILE:
    case IR_VALUE_FS_EXISTS:
    case IR_VALUE_FS_REMOVE:
    case IR_VALUE_FS_RENAME:
    case IR_VALUE_FS_FILE_LEN:
    case IR_VALUE_FS_MAKE_DIR:
    case IR_VALUE_FS_REMOVE_DIR:
    case IR_VALUE_FS_IS_DIR:
    case IR_VALUE_FS_DIR_ENTRY_COUNT:
    case IR_VALUE_FS_TEMP_NAME:
    case IR_VALUE_FS_ATOMIC_WRITE:
      runtime_import_audit_mark_fs_base(audit);
      break;
    case IR_VALUE_JSON_PARSE_BYTES:
    case IR_VALUE_JSON_VALIDATE_BYTES:
    case IR_VALUE_JSON_STREAM_TOKENS_BYTES:
      audit->zero_json_parse_bytes = true;
      break;
    case IR_VALUE_HTTP_FETCH:
      audit->zero_http_fetch_result = true;
      break;
    case IR_VALUE_HTTP_RESULT_OK:
      audit->zero_http_result_ok = true;
      break;
    case IR_VALUE_HTTP_RESULT_STATUS:
      audit->zero_http_result_status = true;
      break;
    case IR_VALUE_HTTP_RESULT_BODY_LEN:
      audit->zero_http_result_body_len = true;
      break;
    case IR_VALUE_HTTP_RESULT_ERROR:
      audit->zero_http_result_error = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_LEN:
      audit->zero_http_response_len = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_HEADERS_LEN:
      audit->zero_http_response_headers_len = true;
      break;
    case IR_VALUE_HTTP_RESPONSE_BODY_OFFSET:
      audit->zero_http_response_body_offset = true;
      break;
    case IR_VALUE_HTTP_HEADER_VALUE:
      audit->zero_http_header_value = true;
      break;
    case IR_VALUE_HTTP_HEADER_FOUND:
      audit->zero_http_header_found = true;
      break;
    case IR_VALUE_HTTP_HEADER_OFFSET:
      audit->zero_http_header_offset = true;
      break;
    case IR_VALUE_HTTP_HEADER_LEN:
      audit->zero_http_header_len = true;
      break;
    default:
      break;
  }
  if (value->kind == IR_VALUE_FS_FILE_LEN ||
      (value->kind == IR_VALUE_FS_READ_ALL && value->type == IR_TYPE_I64)) {
    audit->fd_filestat_get = true;
  }
  if (value->kind == IR_VALUE_FS_DIR_ENTRY_COUNT) audit->fd_readdir = true;
  if (value->kind == IR_VALUE_FS_MAKE_DIR) audit->path_create_directory = true;
  if (value->kind == IR_VALUE_FS_REMOVE_DIR) audit->path_remove_directory = true;
  if (value->kind == IR_VALUE_FS_REMOVE) audit->path_unlink_file = true;
  if (value->kind == IR_VALUE_FS_RENAME || value->kind == IR_VALUE_FS_ATOMIC_WRITE) audit->path_rename = true;

  runtime_import_audit_value(value->index, audit);
  runtime_import_audit_value(value->left, audit);
  runtime_import_audit_value(value->right, audit);
  for (size_t i = 0; i < value->arg_len; i++) runtime_import_audit_value(value->args[i], audit);
}

static void runtime_import_audit_instrs(const IrInstr *instrs, size_t len, RuntimeImportAudit *audit) {
  if (!instrs || !audit) return;
  for (size_t i = 0; i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (instr->kind == IR_INSTR_WORLD_WRITE) audit->fd_write = true;
    runtime_import_audit_value(instr->value, audit);
    runtime_import_audit_value(instr->index, audit);
    runtime_import_audit_instrs(instr->then_instrs, instr->then_len, audit);
    runtime_import_audit_instrs(instr->else_instrs, instr->else_len, audit);
  }
}

static RuntimeImportAudit runtime_import_audit_from_ir(const IrProgram *ir) {
  RuntimeImportAudit audit = {0};
  if (!ir) return audit;
  for (size_t i = 0; i < ir->function_len; i++) {
    runtime_import_audit_instrs(ir->functions[i].instrs, ir->functions[i].instr_len, &audit);
  }
  return audit;
}

static bool ir_value_needs_zero_runtime_object(const IrValue *value) {
  if (!value) return false;
  if (value->kind == IR_VALUE_JSON_PARSE_BYTES ||
      value->kind == IR_VALUE_JSON_VALIDATE_BYTES ||
      value->kind == IR_VALUE_JSON_STREAM_TOKENS_BYTES ||
      value->kind == IR_VALUE_HTTP_FETCH ||
      value->kind == IR_VALUE_HTTP_RESULT_OK ||
      value->kind == IR_VALUE_HTTP_RESULT_STATUS ||
      value->kind == IR_VALUE_HTTP_RESULT_BODY_LEN ||
      value->kind == IR_VALUE_HTTP_RESULT_ERROR ||
      value->kind == IR_VALUE_HTTP_RESPONSE_LEN ||
      value->kind == IR_VALUE_HTTP_RESPONSE_HEADERS_LEN ||
      value->kind == IR_VALUE_HTTP_RESPONSE_BODY_OFFSET ||
      value->kind == IR_VALUE_HTTP_HEADER_VALUE ||
      value->kind == IR_VALUE_HTTP_HEADER_FOUND ||
      value->kind == IR_VALUE_HTTP_HEADER_OFFSET ||
      value->kind == IR_VALUE_HTTP_HEADER_LEN) return true;
  if (ir_value_needs_zero_runtime_object(value->index) ||
      ir_value_needs_zero_runtime_object(value->left) ||
      ir_value_needs_zero_runtime_object(value->right)) {
    return true;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (ir_value_needs_zero_runtime_object(value->args[i])) return true;
  }
  return false;
}

static bool ir_instrs_need_zero_runtime_object(const IrInstr *instrs, size_t len) {
  for (size_t i = 0; instrs && i < len; i++) {
    const IrInstr *instr = &instrs[i];
    if (ir_value_needs_zero_runtime_object(instr->value) ||
        ir_value_needs_zero_runtime_object(instr->index) ||
        ir_instrs_need_zero_runtime_object(instr->then_instrs, instr->then_len) ||
        ir_instrs_need_zero_runtime_object(instr->else_instrs, instr->else_len)) {
      return true;
    }
  }
  return false;
}

static bool ir_needs_zero_runtime_object(const IrProgram *ir) {
  for (size_t i = 0; ir && i < ir->function_len; i++) {
    if (ir_instrs_need_zero_runtime_object(ir->functions[i].instrs, ir->functions[i].instr_len)) return true;
  }
  return false;
}

static size_t native_zero_runtime_import_count(const RuntimeImportAudit *audit) {
  if (!audit) return 0;
  size_t count = 0;
  if (audit->zero_json_parse_bytes) count++;
  if (audit->zero_http_fetch_result) count++;
  if (audit->zero_http_result_ok) count++;
  if (audit->zero_http_result_status) count++;
  if (audit->zero_http_result_body_len) count++;
  if (audit->zero_http_result_error) count++;
  if (audit->zero_http_response_len) count++;
  if (audit->zero_http_response_headers_len) count++;
  if (audit->zero_http_response_body_offset) count++;
  if (audit->zero_http_header_value) count++;
  if (audit->zero_http_header_found) count++;
  if (audit->zero_http_header_offset) count++;
  if (audit->zero_http_header_len) count++;
  return count;
}

static bool runtime_import_audit_uses_zero_runtime(const RuntimeImportAudit *audit) {
  return native_zero_runtime_import_count(audit) > 0;
}

static bool runtime_import_audit_uses_http_provider(const RuntimeImportAudit *audit) {
  return audit && audit->zero_http_fetch_result;
}

static void append_runtime_import_module_json(ZBuf *buf, const RuntimeImportAudit *audit, size_t import_count) {
  if (import_count == 0) {
    zbuf_append(buf, "null");
    return;
  }
  bool uses_zero_runtime = runtime_import_audit_uses_zero_runtime(audit);
  append_json_string(buf, uses_zero_runtime ? "zero_runtime" : "native-target-runtime");
}

static bool append_runtime_import_functions_json(ZBuf *buf, const RuntimeImportAudit *audit) {
  bool first = true;
  zbuf_append(buf, "[");
  if (audit && audit->zero_json_parse_bytes) append_json_string_array_item(buf, &first, "zero_json_parse_bytes");
  if (audit && audit->zero_http_fetch_result) append_json_string_array_item(buf, &first, "zero_http_fetch_result");
  if (audit && audit->zero_http_result_ok) append_json_string_array_item(buf, &first, "zero_http_result_ok");
  if (audit && audit->zero_http_result_status) append_json_string_array_item(buf, &first, "zero_http_result_status");
  if (audit && audit->zero_http_result_body_len) append_json_string_array_item(buf, &first, "zero_http_result_body_len");
  if (audit && audit->zero_http_result_error) append_json_string_array_item(buf, &first, "zero_http_result_error");
  if (audit && audit->zero_http_response_len) append_json_string_array_item(buf, &first, "zero_http_response_len");
  if (audit && audit->zero_http_response_headers_len) append_json_string_array_item(buf, &first, "zero_http_response_headers_len");
  if (audit && audit->zero_http_response_body_offset) append_json_string_array_item(buf, &first, "zero_http_response_body_offset");
  if (audit && audit->zero_http_header_value) append_json_string_array_item(buf, &first, "zero_http_header_value");
  if (audit && audit->zero_http_header_found) append_json_string_array_item(buf, &first, "zero_http_header_found");
  if (audit && audit->zero_http_header_offset) append_json_string_array_item(buf, &first, "zero_http_header_offset");
  if (audit && audit->zero_http_header_len) append_json_string_array_item(buf, &first, "zero_http_header_len");
  zbuf_append(buf, "]");
  return !first;
}

static void append_memory_floor_json(ZBuf *buf, const SourceInput *input) {
  zbuf_append(buf, "{\"linearMemory\":");
  zbuf_append(buf, "false");
  zbuf_appendf(buf, ",\"pageBytes\":0,\"minimumPages\":0,\"floorBytes\":0,\"stackBase\":0,\"stackBytes\":%zu,\"readonlyDataBytes\":%zu}",
               input ? input->direct_stack_bytes : 0,
               input ? input->direct_readonly_data_bytes : 0);
}

static void append_runtime_import_audit_json(ZBuf *buf, const IrProgram *ir, const SourceInput *input, const ZTargetInfo *target) {
  RuntimeImportAudit audit = runtime_import_audit_from_ir(ir);
  size_t import_count = native_zero_runtime_import_count(&audit);
  zbuf_append(buf, "{\"source\":\"direct-ir-scan\",\"module\":");
  append_runtime_import_module_json(buf, &audit, import_count);
  zbuf_append(buf, ",\"functions\":");
  append_runtime_import_functions_json(buf, &audit);
  zbuf_appendf(buf, ",\"functionCount\":%zu", import_count);
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\"memoryFloor\":");
  append_memory_floor_json(buf, input);
  zbuf_append(buf, "}");
}

static void append_portable_runtime_json(ZBuf *buf, const IrProgram *ir, const SourceInput *input, const ZTargetInfo *target, const CapabilitySummary *caps) {
  RuntimeImportAudit audit = runtime_import_audit_from_ir(ir);
  bool uses_zero_runtime = runtime_import_audit_uses_zero_runtime(&audit);
  size_t import_count = native_zero_runtime_import_count(&audit);
  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"runtimeKind\":");
  append_json_string(buf, portable_runtime_kind(target));
  zbuf_append(buf, ",\"portable\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"providerSpecificDeployment\":false,\"hostedDeployment\":\"out-of-scope\"");
  zbuf_append(buf, ",\"imports\":{\"source\":\"direct-ir-scan\",\"explicit\":");
  zbuf_append(buf, uses_zero_runtime ? "true" : "false");
  zbuf_append(buf, ",\"module\":");
  append_runtime_import_module_json(buf, &audit, import_count);
  zbuf_append(buf, ",\"functions\":");
  append_runtime_import_functions_json(buf, &audit);
  zbuf_appendf(buf, ",\"functionCount\":%zu", import_count);
  zbuf_append(buf, ",\"adapter\":");
  if (uses_zero_runtime) append_json_string(buf, "native-zero-runtime-object");
  else append_json_string(buf, "native-target-runtime");
  zbuf_append(buf, "},\"localRunner\":{\"covered\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"command\":");
  append_json_string(buf, "zero dev");
  zbuf_append(buf, ",\"productionLikeImports\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, "},\"capabilityRestrictions\":");
  append_portable_capability_restrictions_json(buf, target);
  zbuf_append(buf, ",\"capabilities\":");
  append_capability_json_array(buf, caps);
  zbuf_append(buf, ",\"memoryFloor\":");
  append_memory_floor_json(buf, input);
  zbuf_append(buf, ",\"frameworkTaxBytes\":0");
  zbuf_append(buf, "}");
}

static ZDirectBackend direct_emit_backend(const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  if (emit_kind && strcmp(emit_kind, "obj") == 0) return z_direct_object_backend(target);
  if (emit_kind && strcmp(emit_kind, "exe") == 0 && command && command->backend) return z_direct_exe_backend(target);
  return Z_DIRECT_BACKEND_NONE;
}

static const char *direct_emit_emitter(const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  ZDirectBackend backend = direct_emit_backend(target, command, emit_kind);
  if (backend == Z_DIRECT_BACKEND_NONE) return "none";
  if (emit_kind && strcmp(emit_kind, "exe") == 0) return z_direct_backend_exe_emitter(backend);
  return z_direct_backend_object_emitter(backend);
}

static const char *release_artifact_kind_for_emit(const ZTargetInfo *target, const char *emit_kind) {
  const char *object_format = target && target->object_format ? target->object_format : "unknown";
  (void)object_format;
  if (emit_kind && strcmp(emit_kind, "obj") == 0) return "native-object";
  if (emit_kind && strcmp(emit_kind, "exe") == 0) return "native-executable";
  return "artifact";
}

static void append_target_capability_contract_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  zbuf_append(buf, "[");
  for (int i = 0; capabilities[i]; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, capabilities[i]);
    zbuf_appendf(buf, ",\"available\":%s,\"source\":", z_target_has_capability(target, capabilities[i]) ? "true" : "false");
    append_json_string(buf, z_target_has_capability(target, capabilities[i]) ? "target-manifest" : "unavailable");
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_target_capability_names_json(ZBuf *buf, const ZTargetInfo *target) {
  const char *capabilities[] = {"memory", "stdio", "args", "env", "fs", "net", "proc", "time", "rand", "web", NULL};
  bool first = true;
  zbuf_append(buf, "[");
  for (int i = 0; capabilities[i]; i++) {
    if (!z_target_has_capability(target, capabilities[i])) continue;
    if (!first) zbuf_append(buf, ",");
    append_json_string(buf, capabilities[i]);
    first = false;
  }
  zbuf_append(buf, "]");
}

static void append_release_target_contract_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  ZDirectBackend selected_backend = direct_emit_backend(target, command, emit_kind);
  const char *object_format = target && target->object_format ? target->object_format : "unknown";
  bool selected_executable = emit_kind && strcmp(emit_kind, "exe") == 0;
  if (selected_backend == Z_DIRECT_BACKEND_NONE && selected_executable) selected_backend = z_direct_exe_backend(target);
  const char *selected_emitter = selected_backend == Z_DIRECT_BACKEND_NONE
                                   ? "none"
                                   : (selected_executable ? z_direct_backend_exe_emitter(selected_backend) : z_direct_backend_object_emitter(selected_backend));
  bool direct_selected = selected_backend != Z_DIRECT_BACKEND_NONE;
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  bool target_requires_sysroot = z_target_requires_sysroot(target);
  bool artifact_requires_sysroot = !direct_selected && plan.requires_sysroot;
  const char *linker_flavor = direct_selected ? z_direct_backend_linker_flavor(selected_backend) : plan.linker_flavor;
  const char *artifact_libc_mode = direct_selected ? "none" : plan.libc_mode;
  const char *sysroot_status = artifact_requires_sysroot ? plan.sysroot_status : (target_requires_sysroot ? "not-used-by-direct-artifact" : "not-required");

  zbuf_append(buf, "{\"schemaVersion\":1,\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_append(buf, ",\"hostTarget\":");
  append_json_string(buf, z_host_target());
  zbuf_appendf(buf, ",\"crossCompilation\":%s", target && strcmp(target->name, z_host_target()) != 0 ? "true" : "false");
  zbuf_append(buf, ",\"emit\":");
  append_json_string(buf, emit_kind ? emit_kind : "exe");
  zbuf_append(buf, ",\"artifactKind\":");
  append_json_string(buf, release_artifact_kind_for_emit(target, emit_kind));
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, object_format);
  zbuf_append(buf, ",\"os\":");
  append_json_string(buf, target && target->os ? target->os : "unknown");
  zbuf_append(buf, ",\"arch\":");
  append_json_string(buf, target && target->arch ? target->arch : "unknown");
  zbuf_append(buf, ",\"abi\":");
  append_json_string(buf, target && target->abi ? target->abi : "");
  zbuf_append(buf, ",\"linkerFlavor\":");
  append_json_string(buf, linker_flavor);
  zbuf_append(buf, ",\"targetLinker\":");
  append_json_string(buf, target && target->linker ? target->linker : "");
  zbuf_append(buf, ",\"selectedEmitter\":");
  append_json_string(buf, selected_emitter);
  zbuf_append(buf, ",\"directObjectEmitter\":");
  append_json_string(buf, z_direct_object_emitter(target));
  zbuf_append(buf, ",\"directExeEmitter\":");
  append_json_string(buf, z_direct_exe_emitter(target));
  zbuf_append(buf, ",\"directStatus\":");
  append_json_string(buf, z_direct_backend_status(target));
  zbuf_append(buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\",\"generatedCBytes\":0,\"cBridgeFallback\":false");
  zbuf_append(buf, ",\"libc\":{\"name\":");
  append_json_string(buf, target && target->libc ? target->libc : "default");
  zbuf_append(buf, ",\"targetMode\":");
  append_json_string(buf, z_target_libc_mode(target));
  zbuf_append(buf, ",\"artifactMode\":");
  append_json_string(buf, artifact_libc_mode);
  zbuf_appendf(buf, ",\"hostReusable\":%s}", target && z_target_is_host(target) ? "true" : "false");
  zbuf_appendf(buf, ",\"sysroot\":{\"requiredByTarget\":%s,\"requiredByArtifact\":%s,\"env\":",
               target_requires_sysroot ? "true" : "false",
               artifact_requires_sysroot ? "true" : "false");
  append_json_string(buf, target_requires_sysroot || artifact_requires_sysroot ? plan.sysroot_env : "");
  zbuf_append(buf, ",\"status\":");
  append_json_string(buf, sysroot_status);
  zbuf_appendf(buf, ",\"missing\":%s}", artifact_requires_sysroot && strcmp(plan.sysroot_status, "missing") == 0 ? "true" : "false");
  zbuf_append(buf, ",\"capabilities\":");
  append_target_capability_names_json(buf, target);
  zbuf_append(buf, ",\"capabilityFacts\":");
  append_target_capability_contract_json(buf, target);
  zbuf_append(buf, ",\"readiness\":{\"status\":");
  append_json_string(buf, direct_selected ? "supported" : "unsupported");
  zbuf_appendf(buf, ",\"directArtifact\":%s,\"missingSysroot\":%s,\"unsupportedReason\":",
               direct_selected ? "true" : "false",
               artifact_requires_sysroot && strcmp(plan.sysroot_status, "missing") == 0 ? "true" : "false");
  append_json_string(buf, direct_selected ? "" : z_direct_backend_reason(target));
  zbuf_append(buf, "},\"determinism\":{\"reproducible\":true,\"stableArtifactNames\":true,\"repeatBuildHash\":\"checked-by-command-contracts\"}");
  zbuf_appendf(buf, ",\"sourceFileCount\":%zu,\"moduleCount\":%zu}", input ? input->source_file_count : 0, input ? input->module_count : 0);
}

static void append_object_backend_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target, const Command *command, const char *emit_kind) {
  ZDirectBackend direct_backend = direct_emit_backend(target, command, emit_kind);
  const char *direct_emitter = direct_emit_emitter(target, command, emit_kind);
  bool metadata_only_direct = emit_kind && (strcmp(emit_kind, "mem") == 0 || strcmp(emit_kind, "size") == 0);
  bool runtime_linked_exe = emit_kind && strcmp(emit_kind, "exe") == 0 && input && input->direct_host_runtime_import_count > 0;
  if (runtime_linked_exe) {
    direct_backend = z_direct_object_backend(target);
    direct_emitter = z_direct_backend_object_emitter(direct_backend);
  }
  if (metadata_only_direct || runtime_linked_exe || (direct_backend != Z_DIRECT_BACKEND_NONE &&
      ((emit_kind && strcmp(emit_kind, "obj") == 0) ||
       (emit_kind && strcmp(emit_kind, "exe") == 0 && command && command->backend)))) {
    if (metadata_only_direct) {
      direct_backend = z_direct_object_backend(target);
      direct_emitter = z_direct_backend_object_emitter(direct_backend);
      if (!direct_emitter || strcmp(direct_emitter, "none") == 0) direct_emitter = "metadata-only";
    }
    const char *object_format = target && target->object_format ? target->object_format : "unknown";
    const char *arch = target && target->arch ? target->arch : "unknown";
    size_t direct_symbol_count = input ? input->direct_function_count : 0;
    bool uses_zero_runtime = input && input->direct_host_runtime_import_count > 0;
    bool uses_http_runtime = input && input->direct_http_runtime_import_count > 0;
    bool links_zero_runtime = uses_zero_runtime;
    bool links_http_runtime = uses_http_runtime;
    ZToolchainPlan runtime_toolchain = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
    const char *runtime_external_toolchain = links_zero_runtime ? public_compiler_label(&runtime_toolchain) : "none";
    zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"MIR primitive value types\",\"controlFlowRepresentation\":\"MIR instruction stream lowered to target machine/module code\",\"callRepresentation\":\"same-object direct calls for supported direct subsets\",\"functionIdentity\":\"module-qualified-stable-sorted\",\"debugRepresentation\":\"source spans retained on MIR nodes\"}");
    bool direct_has_data = input && input->direct_readonly_data_bytes > 0;
    direct_symbol_count += input ? input->direct_runtime_helper_count : 0;
    direct_symbol_count += z_direct_backend_symbol_overhead(direct_backend, direct_has_data);
    bool direct_executable_artifact = emit_kind && strcmp(emit_kind, "exe") == 0 && !runtime_linked_exe && command && command->backend;
    const char *direct_artifact_path = direct_backend != Z_DIRECT_BACKEND_NONE ? z_direct_backend_artifact_path(direct_backend, direct_executable_artifact) : "unsupported";
    const char *direct_linker_flavor = direct_backend != Z_DIRECT_BACKEND_NONE ? z_direct_backend_linker_flavor(direct_backend) : "none";
    zbuf_appendf(buf, ",\"objectEmission\":{\"path\":\"%s\",\"functions\":true,\"dataSections\":%s,\"symbols\":%s,\"relocations\":\"%s\",\"symbolCount\":%zu,\"internalHelperCount\":%zu}",
                 direct_artifact_path,
                 direct_has_data ? "true" : "false",
                 "true",
                 uses_zero_runtime ? "patched-runtime-import-relocations" : (direct_has_data ? "patched-internal-calls-and-data-relocations" : "patched-internal-calls-or-none-in-mvp"),
                 direct_symbol_count,
                 input && input->direct_function_count > input->direct_export_count ? input->direct_function_count - input->direct_export_count : 0);
    zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":");
    append_json_string(buf, direct_linker_flavor);
    zbuf_append(buf, ",\"objectFormat\":");
    append_json_string(buf, object_format);
    zbuf_append(buf, ",\"targetLibraries\":");
    append_json_string(buf, links_http_runtime ? "zero-runtime,curl" : (uses_zero_runtime ? "zero-runtime" : "none"));
    zbuf_append(buf, ",\"symbolMap\":");
    append_json_string(buf, "object-symbol-table");
    zbuf_append(buf, ",\"externalToolchain\":");
    append_json_string(buf, runtime_external_toolchain);
    zbuf_append(buf, ",\"toolchainSource\":");
    append_json_string(buf, uses_zero_runtime ? "direct-backend-runtime-link-plan" : "direct-backend");
    zbuf_append(buf, ",\"stripArtifacts\":false}");
    if (links_http_runtime) {
      zbuf_append(buf, ",\"httpRuntime\":");
      z_append_http_runtime_json(buf, target);
    }
    zbuf_append(buf, ",\"linkerPlan\":{\"format\":");
    append_json_string(buf, object_format);
    zbuf_append(buf, ",\"flavor\":");
    append_json_string(buf, direct_linker_flavor);
    zbuf_append(buf, ",\"archives\":[],\"staticLibraries\":");
    zbuf_append(buf, links_http_runtime ? "[\"zero_runtime.o\",\"zero_http_curl.o\"]" : (links_zero_runtime ? "[\"zero_runtime.o\"]" : "[]"));
    zbuf_append(buf, ",\"importLibraries\":[],\"systemLibraries\":");
    zbuf_append(buf, links_http_runtime ? "[\"curl\"]" : "[]");
    zbuf_append(buf, ",\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"exported-c-and-main-only\",\"crossLinking\":true,\"externalToolchain\":");
    append_json_string(buf, runtime_external_toolchain);
    zbuf_append(buf, ",\"reproducible\":true,\"libcMode\":");
    append_json_string(buf, links_zero_runtime ? runtime_toolchain.libc_mode : "none");
    zbuf_append(buf, ",\"targetLibcMode\":");
    append_json_string(buf, z_target_libc_mode(target));
    zbuf_appendf(buf, ",\"requiresSysroot\":false,\"targetRequiresSysroot\":%s,\"sysrootStatus\":",
                 z_target_requires_sysroot(target) ? "true" : "false");
    append_json_string(buf, z_target_requires_sysroot(target) ? "not-used-by-direct-artifact" : "not-required");
    zbuf_append(buf, "}");
    zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":true,\"status\":");
    append_json_string(buf, z_direct_backend_status(target));
    zbuf_append(buf, ",\"selectedEmitter\":");
    append_json_string(buf, direct_emitter);
    zbuf_append(buf, ",\"objectFormat\":");
    append_json_string(buf, object_format);
    zbuf_append(buf, ",\"arch\":");
    append_json_string(buf, arch);
    zbuf_append(buf, ",\"abi\":");
    append_json_string(buf, target && target->abi ? target->abi : "");
    zbuf_append(buf, ",\"libcMode\":");
    append_json_string(buf, z_target_libc_mode(target));
    zbuf_appendf(buf, ",\"requiresSysroot\":%s", z_target_requires_sysroot(target) ? "true" : "false");
    zbuf_append(buf, ",\"capabilities\":");
    append_target_capability_names_json(buf, target);
    zbuf_append(buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\",\"reason\":");
    append_json_string(buf, z_direct_backend_reason(target));
    zbuf_append(buf, "}");
    zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
    append_json_string(buf, emit_kind ? emit_kind : "exe");
    append_direct_backend_facts_json(buf, input);
    zbuf_append(buf, "}");
    return;
  }
  zbuf_append(buf, "{\"internalIr\":{\"typeRepresentation\":\"MIR primitive value types\",\"controlFlowRepresentation\":\"direct-backend-only\",\"callRepresentation\":\"unsupported-direct-request\",\"debugRepresentation\":\"source spans retained on diagnostics\"}");
  zbuf_append(buf, ",\"objectEmission\":{\"path\":\"none\",\"functions\":false,\"dataSections\":false,\"symbols\":false,\"relocations\":\"none\"}");
  zbuf_append(buf, ",\"linking\":{\"linkerFlavor\":\"none\",\"objectFormat\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "native");
  zbuf_append(buf, ",\"targetLibraries\":\"none\",\"symbolMap\":\"none\",\"externalToolchain\":\"none\",\"toolchainSource\":\"direct-backend-only\",\"stripArtifacts\":false}");
  zbuf_append(buf, ",\"linkerPlan\":{\"format\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "native");
  ZToolchainPlan plan = z_plan_toolchain(command ? command->cc : NULL, command ? command->profile : NULL, target);
  zbuf_append(buf, ",\"flavor\":\"none\",\"archives\":[],\"staticLibraries\":[],\"importLibraries\":[],\"systemLibraries\":[],\"rpaths\":[],\"loadPaths\":[],\"visibility\":\"none\",\"crossLinking\":false,\"externalToolchain\":\"none\",\"reproducible\":true,\"libcMode\":");
  append_json_string(buf, plan.libc_mode);
  zbuf_appendf(buf, ",\"requiresSysroot\":%s,\"sysrootStatus\":", plan.requires_sysroot ? "true" : "false");
  append_json_string(buf, plan.sysroot_status);
  zbuf_append(buf, "}");
  zbuf_append(buf, ",\"targetFacts\":{\"directAvailable\":false,\"fallbackPolicy\":\"removed\",\"reason\":");
  append_json_string(buf, z_direct_backend_reason(target));
  zbuf_append(buf, "}");
  zbuf_appendf(buf, ",\"moduleCount\":%zu,\"emitKind\":", input ? input->module_count : 0);
  append_json_string(buf, emit_kind ? emit_kind : "exe");
  zbuf_append(buf, "}");
}

static size_t z_max_size(size_t a, size_t b) {
  return a > b ? a : b;
}

static size_t memory_budget_stack_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_stack_bytes : 0, summary ? summary->stack_estimate_bytes : 0);
}

static size_t memory_budget_max_frame_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_max_frame_bytes : 0, summary ? summary->stack_estimate_bytes : 0);
}

static size_t memory_budget_static_bytes(const SourceInput *input, const MemoryModelSummary *summary) {
  return z_max_size(input ? input->direct_readonly_data_bytes : 0, summary ? summary->readonly_literal_bytes : 0);
}

static bool memory_summary_uses_allocator(const MemoryModelSummary *summary) {
  return summary && (summary->fixed_allocator_count > 0 ||
                     summary->arena_count > 0 ||
                     summary->null_allocator_count > 0 ||
                     summary->page_allocator_count > 0 ||
                     summary->general_allocator_count > 0 ||
                     summary->alloc_bytes_calls > 0 ||
                     summary->byte_buf_calls > 0 ||
                     summary->reset_calls > 0 ||
                     summary->capacity_calls > 0);
}

static bool memory_summary_uses_collections(const MemoryModelSummary *summary) {
  return summary && (summary->vec_count > 0 ||
                     summary->vec_push_calls > 0 ||
                     summary->vec_len_calls > 0 ||
                     summary->vec_capacity_calls > 0 ||
                     summary->map_empty_count > 0 ||
                     summary->map_len_calls > 0 ||
                     summary->set_empty_count > 0 ||
                     summary->set_len_calls > 0 ||
                     summary->byte_buf_calls > 0);
}

static void append_memory_budgets_json(ZBuf *buf, const SourceInput *input, const MemoryModelSummary *summary, bool linear_memory) {
  size_t stack_bytes = memory_budget_stack_bytes(input, summary);
  size_t max_frame_bytes = memory_budget_max_frame_bytes(input, summary);
  size_t static_bytes = memory_budget_static_bytes(input, summary);
  size_t fixed_capacity = summary ? summary->fixed_allocator_capacity_bytes : 0;
  size_t arena_capacity = summary ? summary->arena_capacity_bytes : 0;
  size_t collection_capacity = summary ? summary->collection_capacity_bytes : 0;
  zbuf_appendf(buf, "{\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"staticBytes\":%zu,\"heapBytes\":0,\"arenaBytes\":%zu,\"fixedBufferBytes\":%zu,\"collectionCapacityBytes\":%zu,\"allocatorCapacityBytes\":%zu,\"allocationRequestedBytes\":%zu,\"globalHeapBytes\":0,\"linearMemoryFloorBytes\":%d,\"hiddenHeapAllocation\":false,\"unknownCapacitySites\":%zu,\"source\":\"direct-mir-and-checked-ast\"}",
               stack_bytes,
               max_frame_bytes,
               static_bytes,
               arena_capacity,
               fixed_capacity,
               collection_capacity,
               fixed_capacity + arena_capacity,
               summary ? summary->allocation_requested_bytes : 0,
               linear_memory ? 65536 : 0,
               summary ? summary->unknown_capacity_sites : 0);
}

static void append_allocator_facts_json(ZBuf *buf, const MemoryModelSummary *summary) {
  size_t fixed_capacity = summary ? summary->fixed_allocator_capacity_bytes : 0;
  size_t arena_capacity = summary ? summary->arena_capacity_bytes : 0;
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"kind\":\"NullAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"failure\":\"always-none\",\"hiddenGlobalAllocator\":false}",
               summary && summary->null_allocator_count > 0 ? "true" : "false",
               summary ? summary->null_allocator_count : 0);
  zbuf_appendf(buf, ", {\"kind\":\"FixedBufAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":%zu,\"storage\":\"caller-owned MutSpan<u8>\",\"failure\":\"Maybe.none on overflow\",\"resettable\":true,\"hiddenGlobalAllocator\":false}",
               summary && summary->fixed_allocator_count > 0 ? "true" : "false",
               summary ? summary->fixed_allocator_count : 0,
               fixed_capacity);
  zbuf_appendf(buf, ", {\"kind\":\"Arena\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":%zu,\"storage\":\"caller-owned MutSpan<u8>\",\"failure\":\"Maybe.none on overflow\",\"resettable\":true,\"hiddenGlobalAllocator\":false}",
               summary && summary->arena_count > 0 ? "true" : "false",
               summary ? summary->arena_count : 0,
               arena_capacity);
  zbuf_appendf(buf, ", {\"kind\":\"PageAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"status\":\"explicit-host-handle\",\"failure\":\"Maybe.none or target diagnostic before codegen\",\"hiddenGlobalAllocator\":false}",
               summary && summary->page_allocator_count > 0 ? "true" : "false",
               summary ? summary->page_allocator_count : 0);
  zbuf_appendf(buf, ", {\"kind\":\"GeneralAlloc\",\"used\":%s,\"instances\":%zu,\"capacityBytes\":0,\"status\":\"explicit-handle-no-default-global\",\"failure\":\"Maybe.none on failure\",\"hiddenGlobalAllocator\":false}",
               summary && summary->general_allocator_count > 0 ? "true" : "false",
               summary ? summary->general_allocator_count : 0);
  zbuf_append(buf, "]");
}

static void append_allocation_instrumentation_json(ZBuf *buf, const MemoryModelSummary *summary) {
  size_t sites = summary ? summary->alloc_bytes_calls + summary->byte_buf_calls : 0;
  zbuf_appendf(buf, "{\"source\":\"checked-ast\",\"allocationSites\":%zu,\"allocBytesCalls\":%zu,\"byteBufCalls\":%zu,\"bytesRequestedByLiteralCalls\":%zu,\"failureSignal\":\"Maybe.none\",\"hiddenGlobalAllocator\":false,\"runtimeCountersPayAsUsed\":true,\"hooks\":[\"attempts\",\"successes\",\"failures\",\"bytesRequested\",\"bytesGranted\",\"peakLiveBytes\"]}",
               sites,
               summary ? summary->alloc_bytes_calls : 0,
               summary ? summary->byte_buf_calls : 0,
               summary ? summary->allocation_requested_bytes : 0);
}

static void append_collection_facts_json(ZBuf *buf, const MemoryModelSummary *summary) {
  zbuf_appendf(buf, "{\"Vec\":{\"used\":%s,\"instances\":%zu,\"pushCalls\":%zu,\"capacityCalls\":%zu,\"capacityBytes\":%zu,\"growth\":\"fixed-capacity caller storage\",\"pushFailure\":\"returns false\",\"cleanup\":\"caller owns storage\",\"hiddenAllocation\":false}",
               summary && summary->vec_count > 0 ? "true" : "false",
               summary ? summary->vec_count : 0,
               summary ? summary->vec_push_calls : 0,
               summary ? summary->vec_capacity_calls : 0,
               summary ? summary->collection_capacity_bytes : 0);
  zbuf_appendf(buf, ",\"ByteBuf\":{\"used\":%s,\"allocationCalls\":%zu,\"ownership\":\"owned ByteBuf backed by explicit allocator storage\",\"cleanup\":\"owned value releases logical ownership; allocator storage remains explicit\",\"hiddenAllocation\":false}",
               summary && summary->byte_buf_calls > 0 ? "true" : "false",
               summary ? summary->byte_buf_calls : 0);
  zbuf_appendf(buf, ",\"Map\":{\"used\":%s,\"instances\":%zu,\"lenCalls\":%zu,\"status\":\"empty fixed metadata\",\"growth\":\"not enabled without explicit allocator/capacity contract\",\"hiddenAllocation\":false}",
               summary && summary->map_empty_count > 0 ? "true" : "false",
               summary ? summary->map_empty_count : 0,
               summary ? summary->map_len_calls : 0);
  zbuf_appendf(buf, ",\"Set\":{\"used\":%s,\"instances\":%zu,\"lenCalls\":%zu,\"status\":\"empty fixed metadata\",\"growth\":\"not enabled without explicit allocator/capacity contract\",\"hiddenAllocation\":false}}",
               summary && summary->set_empty_count > 0 ? "true" : "false",
               summary ? summary->set_empty_count : 0,
               summary ? summary->set_len_calls : 0);
}

static void append_memory_regions_json(ZBuf *buf, const SourceInput *input, const MemoryModelSummary *summary, bool linear_memory) {
  (void)linear_memory;
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"name\":\"stack\",\"kind\":\"stack\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"direct-mir-frame-layout\"}",
               memory_budget_stack_bytes(input, summary));
  zbuf_appendf(buf, ", {\"name\":\"max-frame\",\"kind\":\"stack-frame\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"largest-direct-function-frame\"}",
               memory_budget_max_frame_bytes(input, summary));
  zbuf_appendf(buf, ", {\"name\":\"readonly-data\",\"kind\":\"static\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"direct-data-segments\"}",
               memory_budget_static_bytes(input, summary));
  zbuf_append(buf, ", {\"name\":\"heap\",\"kind\":\"heap\",\"bytes\":0,\"payAsUsed\":true,\"source\":\"no hidden global heap\"}");
  zbuf_appendf(buf, ", {\"name\":\"arena-storage\",\"kind\":\"arena\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned arena buffers\"}",
               summary ? summary->arena_capacity_bytes : 0);
  zbuf_appendf(buf, ", {\"name\":\"fixed-buffer-storage\",\"kind\":\"allocator-capacity\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned FixedBufAlloc buffers\"}",
               summary ? summary->fixed_allocator_capacity_bytes : 0);
  zbuf_appendf(buf, ", {\"name\":\"collection-storage\",\"kind\":\"collection-capacity\",\"bytes\":%zu,\"payAsUsed\":true,\"source\":\"caller-owned collection storage\"}",
               summary ? summary->collection_capacity_bytes : 0);
  zbuf_append(buf, "]");
}

static void append_memory_capability_facts_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  zbuf_append(buf, "{\"requiredCapabilities\":");
  append_capability_json_array(buf, caps);
  zbuf_append(buf, ",\"targetSupport\":");
  append_target_capability_facts_json(buf, target, caps);
  zbuf_append(buf, ",\"filesystem\":");
  append_json_string(buf, caps && caps->fs
                        ? (z_target_has_capability(target, "fs") ? "available" : "target-denied")
                        : "not-required");
  zbuf_append(buf, ",\"worldStdio\":");
  append_json_string(buf, caps && caps->world
                        ? (z_target_has_capability(target, "stdio") ? "available" : "target-denied")
                        : "not-required");
  zbuf_append(buf, "}");
}

static void append_direct_memory_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const IrProgram *ir) {
  CapabilitySummary caps = program_capabilities(program);
  HelperUseSummary used_helpers = program_used_helpers(program);
  MemoryModelSummary memory_summary = memory_model_summary_from_program(program);
  bool uses_allocator = (input && input->direct_allocator_helper_count > 0) || memory_summary_uses_allocator(&memory_summary);
  bool uses_buffers = (input && input->direct_buffer_helper_count > 0) || memory_summary_uses_collections(&memory_summary);
  bool linear_memory = input && (input->direct_stack_bytes > 0 ||
                                 input->direct_readonly_data_bytes > 0 ||
                                 input->direct_runtime_helper_count > 0);
  linear_memory = linear_memory ||
                  memory_summary.stack_estimate_bytes > 0 ||
                  memory_summary.readonly_literal_bytes > 0 ||
                  uses_allocator ||
                  uses_buffers;
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  append_json_string(buf, input ? input->source_file : NULL);
  zbuf_append(buf, ",\n  \"target\": ");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\n  \"hostTarget\": ");
  append_json_string(buf, z_host_target());
  zbuf_append(buf, ",\n  \"profile\": ");
  append_json_string(buf, command && command->profile ? command->profile : "release");
  zbuf_append(buf, ",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"loweredIrBytes\": ");
  zbuf_appendf(buf, "%zu", input ? input->lowered_ir_bytes : 0);
  zbuf_append(buf, ",\n  \"directFacts\": {\"functionCount\":");
  zbuf_appendf(buf, "%zu,\"exportCount\":%zu,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"allocatorHelperCount\":%zu,\"bufferHelperCount\":%zu,\"runtimeHelperCount\":%zu,\"moduleCount\":%zu}",
               input ? input->direct_function_count : 0,
               input ? input->direct_export_count : 0,
               input ? input->direct_stack_bytes : 0,
               input ? input->direct_max_frame_bytes : 0,
               input ? input->direct_readonly_data_bytes : 0,
               input ? input->direct_allocator_helper_count : 0,
               input ? input->direct_buffer_helper_count : 0,
               input ? input->direct_runtime_helper_count : 0,
               input ? input->module_count : 0);
  zbuf_append(buf, ",\n  \"memory\": {\"linearMemory\":");
  zbuf_append(buf, linear_memory ? "true" : "false");
  zbuf_appendf(buf, ",\"memoryPageBytes\":65536,\"stackBase\":65536,\"stackBytes\":%zu,\"maxFrameBytes\":%zu,\"readonlyDataBytes\":%zu,\"heapStart\":null,\"heapEnd\":null,\"heapPolicy\":",
               memory_budget_stack_bytes(input, &memory_summary),
               memory_budget_max_frame_bytes(input, &memory_summary),
               memory_budget_static_bytes(input, &memory_summary));
  append_json_string(buf, uses_allocator ? "explicit-allocator-no-global-heap" : "not-required");
  zbuf_append(buf, ",\"allocator\":");
  append_json_string(buf, uses_allocator ? "explicit" : "none");
  zbuf_append(buf, ",\"buffer\":");
  append_json_string(buf, uses_buffers ? "explicit collections" : "none");
  zbuf_append(buf, ",\"hiddenHeapAllocation\":false,\"boundsTraps\":\"pay-as-used\"},\n  \"memoryBudgets\": ");
  append_memory_budgets_json(buf, input, &memory_summary, linear_memory);
  zbuf_append(buf, ",\n  \"allocatorFacts\": ");
  append_allocator_facts_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"allocationInstrumentation\": ");
  append_allocation_instrumentation_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"collectionFacts\": ");
  append_collection_facts_json(buf, &memory_summary);
  zbuf_append(buf, ",\n  \"regions\": ");
  append_memory_regions_json(buf, input, &memory_summary, linear_memory);
  zbuf_append(buf, ",\n  \"capabilityFacts\": ");
  append_memory_capability_facts_json(buf, &caps, target);
  zbuf_append(buf, ",\n  \"usedStdlibHelpers\": ");
  append_used_stdlib_helpers_json(buf, &used_helpers);
  zbuf_append(buf, ",\n  \"runtimeShims\": ");
  append_runtime_shims_json(buf, NULL, &caps);
  zbuf_append(buf, ",\n  \"objectBackend\": ");
  append_object_backend_json(buf, input, target, command, "mem");
  zbuf_append(buf, ",\n  \"mir\": {\"valid\":");
  zbuf_append(buf, ir && ir->mir_valid ? "true" : "false");
  zbuf_append(buf, ",\"expected\":");
  append_json_string(buf, ir ? ir->mir_expected : "");
  zbuf_append(buf, ",\"actual\":");
  append_json_string(buf, ir ? ir->mir_actual : "");
  zbuf_append(buf, ",\"message\":");
  append_json_string(buf, ir ? ir->mir_message : "");
  zbuf_append(buf, "},\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, command ? command->profile : "release");
  zbuf_append(buf, "\n}\n");
}

static void print_build_json(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target, const char *emit_kind, const char *artifact_path, long long artifact_bytes, long long generated_c_bytes, long long elapsed_ms) {
  printf("{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  print_json_string(input->source_file);
  printf(",\n  \"emit\": ");
  print_json_string(emit_kind);
  printf(",\n  \"hostTarget\": ");
  print_json_string(z_host_target());
  printf(",\n  \"target\": ");
  print_json_string(target->name);
  printf(",\n  \"profile\": ");
  print_json_string(command->profile);
  printf(",\n  \"legacy\": false");
  printf(",\n  \"legacyBackend\": null");
  printf(",\n  \"warnings\": []");
  printf(",\n  \"compiler\": ");
  ZDirectBackend direct_backend = direct_emit_backend(target, command, emit_kind);
  const char *driver_kind = z_direct_backend_object_emitter(direct_backend);
  if (direct_backend != Z_DIRECT_BACKEND_NONE) print_json_string(driver_kind);
  else print_json_string(build_compiler_label(command, target));
  printf(",\n  \"toolchain\": ");
  if (direct_backend != Z_DIRECT_BACKEND_NONE) {
    ZBuf direct_toolchain;
    zbuf_init(&direct_toolchain);
    zbuf_append(&direct_toolchain, "{\"driverKind\":");
    append_json_string(&direct_toolchain, driver_kind);
    zbuf_append(&direct_toolchain, ",\"selectionSource\":\"direct-backend\",\"compiler\":");
    append_json_string(&direct_toolchain, driver_kind);
    zbuf_append(&direct_toolchain, ",\"targetTriple\":");
    append_json_string(&direct_toolchain, target && target->zig_target ? target->zig_target : "");
    zbuf_append(&direct_toolchain, ",\"linkerFlavor\":");
    append_json_string(&direct_toolchain, z_direct_backend_linker_flavor(direct_backend));
    zbuf_append(&direct_toolchain, ",\"libcMode\":\"none\",\"requiresSysroot\":false,\"sysrootEnv\":\"\",\"sysrootStatus\":\"not-required\",\"usesTargetFlag\":false,\"usesToolchainCache\":false,\"stripArtifact\":false}");
    fputs(direct_toolchain.data, stdout);
    zbuf_free(&direct_toolchain);
  } else {
    ZBuf toolchain_json;
    zbuf_init(&toolchain_json);
    append_toolchain_plan_json(&toolchain_json, command, target);
    fputs(toolchain_json.data, stdout);
    zbuf_free(&toolchain_json);
  }
  printf(",\n  \"artifactPath\": ");
  if (artifact_path) print_json_string(artifact_path);
  else printf("null");
  printf(",\n  \"artifactBytes\": ");
  if (artifact_bytes >= 0) printf("%lld", artifact_bytes);
  else printf("null");
  printf(",\n  \"generatedCBytes\": %lld,\n  \"loweredIrBytes\": %zu,\n  \"elapsedMs\": %lld,\n  \"targetSupport\": {\"fsAvailable\": %s, \"directStatus\": ", generated_c_bytes, input->lowered_ir_bytes, elapsed_ms, z_target_has_capability(target, "fs") ? "true" : "false");
  print_json_string(z_direct_backend_status(target));
  printf(", \"directObjectEmitter\": ");
  print_json_string(z_direct_object_emitter(target));
  printf(", \"directExeEmitter\": ");
  print_json_string(z_direct_exe_emitter(target));
  printf(", \"fallbackPolicy\": \"explicit-direct-never-c-bridge\"}");
  ZBuf extra;
  zbuf_init(&extra);
  zbuf_append(&extra, ",\n  \"compilerPhases\": ");
  append_compiler_phases_json(&extra, input);
  zbuf_append(&extra, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"package\": ");
  append_package_metadata_json(&extra, input, target);
  zbuf_append(&extra, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(&extra, input, target, command->profile);
  zbuf_append(&extra, ",\n  \"profileSemantics\": ");
  append_profile_semantics_json(&extra, command->profile);
  zbuf_append(&extra, ",\n  \"profileCatalog\": ");
  append_profile_catalog_json(&extra);
  zbuf_append(&extra, ",\n  \"profileBudget\": ");
  append_profile_budget_json(&extra, command->profile);
  zbuf_append(&extra, ",\n  \"releaseTargetContract\": ");
  append_release_target_contract_json(&extra, input, target, command, emit_kind);
  zbuf_append(&extra, ",\n  \"objectBackend\": ");
  append_object_backend_json(&extra, input, target, command, emit_kind);
  CapabilitySummary routing_caps = program_capabilities(program);
  zbuf_append(&extra, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(&extra, "build", emit_kind, program, &routing_caps, target);
  fputs(extra.data, stdout);
  zbuf_free(&extra);
  printf("\n}\n");
}

static long long file_size_or_negative(const char *path) {
  struct stat st;
  if (!path || stat(path, &st) != 0) return -1;
  return (long long)st.st_size;
}

static char *path_with_suffix(const char *path, const char *suffix) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, path ? path : "");
  zbuf_append(&buf, suffix ? suffix : "");
  return buf.data;
}

static char *ship_default_out_path(const char *source_file, const ZTargetInfo *target) {
  const char *slash = strrchr(source_file ? source_file : "app", '/');
  const char *base = slash ? slash + 1 : (source_file ? source_file : "app");
  const char *dot = strrchr(base, '.');
  size_t len = dot ? (size_t)(dot - base) : strlen(base);
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, ".zero/ship/");
  for (size_t i = 0; i < len; i++) zbuf_append_char(&buf, base[i]);
  zbuf_append_char(&buf, '-');
  zbuf_append(&buf, target && target->name ? target->name : z_host_target());
  return buf.data;
}

static void ship_artifacts_free(ShipArtifacts *artifacts) {
  if (!artifacts) return;
  free(artifacts->binary_path);
  free(artifacts->stripped_path);
  free(artifacts->checksum_path);
  free(artifacts->archive_path);
  free(artifacts->debug_path);
  free(artifacts->size_path);
  free(artifacts->sbom_path);
  memset(artifacts, 0, sizeof(*artifacts));
}

static bool write_ship_artifacts(const Command *command, const SourceInput *input, const ZTargetInfo *target, const ZBuf *exe, const char *exe_file, ShipArtifacts *artifacts, ZDiag *diag) {
  memset(artifacts, 0, sizeof(*artifacts));
  artifacts->binary_path = z_strdup(exe_file);
  artifacts->stripped_path = path_with_suffix(exe_file, ".stripped");
  artifacts->checksum_path = path_with_suffix(exe_file, ".checksum");
  artifacts->archive_path = path_with_suffix(exe_file, ".zeroar");
  artifacts->debug_path = path_with_suffix(exe_file, ".debug.json");
  artifacts->size_path = path_with_suffix(exe_file, ".size.json");
  artifacts->sbom_path = path_with_suffix(exe_file, ".sbom.json");
  artifacts->checksum = fnv1a_bytes((const unsigned char *)(exe && exe->data ? exe->data : ""), exe ? exe->len : 0);
  artifacts->binary_bytes = file_size_or_negative(exe_file);

  if (!z_write_binary_file(artifacts->stripped_path, (const unsigned char *)(exe && exe->data ? exe->data : ""), exe ? exe->len : 0, diag)) return false;
  chmod(artifacts->stripped_path, 0755);
  artifacts->stripped_bytes = file_size_or_negative(artifacts->stripped_path);

  ZBuf checksum;
  zbuf_init(&checksum);
  zbuf_appendf(&checksum, "fnv1a64 %016llx  %s\n", (unsigned long long)artifacts->checksum, artifacts->binary_path);
  if (!z_write_file(artifacts->checksum_path, checksum.data ? checksum.data : "", diag)) {
    zbuf_free(&checksum);
    return false;
  }
  zbuf_free(&checksum);

  ZBuf debug;
  zbuf_init(&debug);
  zbuf_append(&debug, "{\n  \"schemaVersion\": 1,\n  \"kind\": \"zero-debug-symbol-metadata\",\n  \"sourceFile\": ");
  append_json_string(&debug, input ? input->source_file : "");
  zbuf_append(&debug, ",\n  \"target\": ");
  append_json_string(&debug, target ? target->name : z_host_target());
  zbuf_append(&debug, ",\n  \"profile\": ");
  append_json_string(&debug, command ? command->profile : "release");
  zbuf_append(&debug, ",\n  \"debugInfoAvailable\": false,\n  \"strippedBinary\": ");
  append_json_string(&debug, artifacts->stripped_path);
  zbuf_append(&debug, ",\n  \"symbolPolicy\": \"metadata-placeholder-until-debug-info-emission\"\n}\n");
  if (!z_write_file(artifacts->debug_path, debug.data ? debug.data : "", diag)) {
    zbuf_free(&debug);
    return false;
  }
  zbuf_free(&debug);

  ZBuf size;
  zbuf_init(&size);
  zbuf_append(&size, "{\n  \"schemaVersion\": 1,\n  \"kind\": \"zero-release-size-report\",\n  \"sourceFile\": ");
  append_json_string(&size, input ? input->source_file : "");
  zbuf_append(&size, ",\n  \"target\": ");
  append_json_string(&size, target ? target->name : z_host_target());
  zbuf_appendf(&size, ",\n  \"artifactBytes\": %lld,\n  \"loweredIrBytes\": %zu,\n  \"generatedCBytes\": 0,\n  \"sections\": [{\"name\":\"binary\",\"bytes\":%lld},{\"name\":\"lowered-ir\",\"bytes\":%zu}],\n  \"directFacts\": {\"functionCount\":%zu,\"readonlyDataBytes\":%zu,\"stackBytes\":%zu}\n}\n",
               artifacts->binary_bytes,
               input ? input->lowered_ir_bytes : 0,
               artifacts->binary_bytes,
               input ? input->lowered_ir_bytes : 0,
               input ? input->direct_function_count : 0,
               input ? input->direct_readonly_data_bytes : 0,
               input ? input->direct_stack_bytes : 0);
  if (!z_write_file(artifacts->size_path, size.data ? size.data : "", diag)) {
    zbuf_free(&size);
    return false;
  }
  zbuf_free(&size);

  ZBuf sbom;
  zbuf_init(&sbom);
  zbuf_append(&sbom, "{\n  \"schemaVersion\": 1,\n  \"kind\": \"zero-sbom-placeholder\",\n  \"format\": \"spdx-json-placeholder\",\n  \"packages\": [{\"name\":\"root\",\"sourceFile\":");
  append_json_string(&sbom, input ? input->source_file : "");
  zbuf_append(&sbom, ",\"license\":\"unknown\"}],\n  \"generatedCBytes\": 0,\n  \"note\": \"full dependency license scan is not implemented yet\"\n}\n");
  if (!z_write_file(artifacts->sbom_path, sbom.data ? sbom.data : "", diag)) {
    zbuf_free(&sbom);
    return false;
  }
  zbuf_free(&sbom);

  ZBuf archive;
  zbuf_init(&archive);
  zbuf_append(&archive, "zero archive manifest v1\n");
  zbuf_appendf(&archive, "binary %s %lld %016llx\n", artifacts->binary_path, artifacts->binary_bytes, (unsigned long long)artifacts->checksum);
  zbuf_appendf(&archive, "stripped %s %lld\n", artifacts->stripped_path, artifacts->stripped_bytes);
  zbuf_appendf(&archive, "checksum %s\n", artifacts->checksum_path);
  zbuf_appendf(&archive, "debug %s\n", artifacts->debug_path);
  zbuf_appendf(&archive, "size %s\n", artifacts->size_path);
  zbuf_appendf(&archive, "sbom %s\n", artifacts->sbom_path);
  zbuf_appendf(&archive, "target %s\n", target && target->name ? target->name : z_host_target());
  zbuf_appendf(&archive, "profile %s\n", command && command->profile ? command->profile : "release");
  zbuf_append(&archive, "generatedCBytes 0\n");
  if (!z_write_file(artifacts->archive_path, archive.data ? archive.data : "", diag)) {
    zbuf_free(&archive);
    return false;
  }
  zbuf_free(&archive);

  artifacts->archive_bytes = file_size_or_negative(artifacts->archive_path);
  artifacts->debug_bytes = file_size_or_negative(artifacts->debug_path);
  artifacts->size_bytes = file_size_or_negative(artifacts->size_path);
  artifacts->sbom_bytes = file_size_or_negative(artifacts->sbom_path);
  return true;
}

static void append_ship_artifact_json(ZBuf *buf, const char *kind, const char *path, long long bytes, const char *format) {
  zbuf_append(buf, "{\"kind\":");
  append_json_string(buf, kind);
  zbuf_append(buf, ",\"path\":");
  append_json_string(buf, path);
  zbuf_appendf(buf, ",\"bytes\":%lld", bytes);
  zbuf_append(buf, ",\"format\":");
  append_json_string(buf, format);
  zbuf_append(buf, "}");
}

static void print_ship_json(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target, const ShipArtifacts *artifacts, long long elapsed_ms) {
  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"command\": \"ship\",\n  \"sourceFile\": ");
  append_json_string(&buf, input ? input->source_file : "");
  zbuf_append(&buf, ",\n  \"target\": ");
  append_json_string(&buf, target ? target->name : z_host_target());
  zbuf_append(&buf, ",\n  \"hostTarget\": ");
  append_json_string(&buf, z_host_target());
  zbuf_append(&buf, ",\n  \"profile\": ");
  append_json_string(&buf, command ? command->profile : "release");
  zbuf_append(&buf, ",\n  \"emit\": \"exe\",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"artifactPath\": ");
  append_json_string(&buf, artifacts ? artifacts->binary_path : "");
  zbuf_appendf(&buf, ",\n  \"artifactBytes\": %lld,\n  \"checksum\": {\"algorithm\":\"fnv1a64\",\"value\":\"%016llx\",\"path\":",
               artifacts ? artifacts->binary_bytes : 0,
               (unsigned long long)(artifacts ? artifacts->checksum : 0));
  append_json_string(&buf, artifacts ? artifacts->checksum_path : "");
  zbuf_append(&buf, "},\n  \"releasePreview\": {\"deterministic\": true, \"archive\": ");
  append_json_string(&buf, artifacts ? artifacts->archive_path : "");
  zbuf_append(&buf, ", \"strippedBinary\": ");
  append_json_string(&buf, artifacts ? artifacts->stripped_path : "");
  zbuf_append(&buf, ", \"sizeReport\": ");
  append_json_string(&buf, artifacts ? artifacts->size_path : "");
  zbuf_append(&buf, ", \"debugSymbols\": ");
  append_json_string(&buf, artifacts ? artifacts->debug_path : "");
  zbuf_append(&buf, ", \"sbom\": ");
  append_json_string(&buf, artifacts ? artifacts->sbom_path : "");
  zbuf_append(&buf, ", \"targetContract\": ");
  append_release_target_contract_json(&buf, input, target, command, "exe");
  zbuf_append(&buf, "},\n  \"artifacts\": [");
  append_ship_artifact_json(&buf, "binary", artifacts ? artifacts->binary_path : "", artifacts ? artifacts->binary_bytes : 0, "native-executable");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "stripped-binary", artifacts ? artifacts->stripped_path : "", artifacts ? artifacts->stripped_bytes : 0, "native-executable");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "checksum", artifacts ? artifacts->checksum_path : "", file_size_or_negative(artifacts ? artifacts->checksum_path : ""), "fnv1a64");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "archive", artifacts ? artifacts->archive_path : "", artifacts ? artifacts->archive_bytes : 0, "zero-archive-manifest-v1");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "debug-symbol-metadata", artifacts ? artifacts->debug_path : "", artifacts ? artifacts->debug_bytes : 0, "json");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "size-report", artifacts ? artifacts->size_path : "", artifacts ? artifacts->size_bytes : 0, "json");
  zbuf_append(&buf, ", ");
  append_ship_artifact_json(&buf, "sbom-placeholder", artifacts ? artifacts->sbom_path : "", artifacts ? artifacts->sbom_bytes : 0, "spdx-json-placeholder");
  zbuf_appendf(&buf, "],\n  \"elapsedMs\": %lld,\n  \"targetFacts\": {\"directStatus\":", elapsed_ms);
  append_json_string(&buf, z_direct_backend_status(target));
  zbuf_append(&buf, ",\"directExeEmitter\":");
  append_json_string(&buf, z_direct_exe_emitter(target));
  zbuf_append(&buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\"},\n  \"compilerPhases\": ");
  append_compiler_phases_json(&buf, input);
  zbuf_append(&buf, ",\n  \"releaseTargetContract\": ");
  append_release_target_contract_json(&buf, input, target, command, "exe");
  zbuf_append(&buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(&buf, input, target, command ? command->profile : "release");
  zbuf_append(&buf, ",\n  \"package\": ");
  append_package_metadata_json(&buf, input, target);
  zbuf_append(&buf, ",\n  \"packageCache\": ");
  append_package_cache_audit_json(&buf, input, target, command ? command->profile : "release");
  zbuf_append(&buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(&buf, input, target, command ? command->profile : "release");
  zbuf_append(&buf, ",\n  \"objectBackend\": ");
  append_object_backend_json(&buf, input, target, command, "exe");
  CapabilitySummary caps = program_capabilities(program);
  zbuf_append(&buf, ",\n  \"selfHostRouting\": ");
  append_self_host_routing_json(&buf, "ship", "exe", program, &caps, target);
  zbuf_append(&buf, "\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

static void print_ship_text(const ShipArtifacts *artifacts, long long elapsed_ms) {
  char duration[32];
  format_duration(elapsed_ms, duration, sizeof(duration));
  printf("release preview (%s)\n", duration);
  printf("binary: %s (%lld bytes)\n", artifacts->binary_path, artifacts->binary_bytes);
  printf("stripped: %s (%lld bytes)\n", artifacts->stripped_path, artifacts->stripped_bytes);
  printf("checksum: %s (%016llx)\n", artifacts->checksum_path, (unsigned long long)artifacts->checksum);
  printf("archive: %s\n", artifacts->archive_path);
  printf("debug symbols: %s\n", artifacts->debug_path);
  printf("size report: %s\n", artifacts->size_path);
  printf("sbom: %s\n", artifacts->sbom_path);
}

static int print_version_command(bool json) {
  if (!json) {
    printf("zero %s\n", ZERO_VERSION);
    return 0;
  }

  const char *zero_cc = getenv("ZERO_CC");
  bool target_cc_override = zero_cc && zero_cc[0];
  bool bundled_target_cc_available = command_available("zig");
  bool target_cc_available = target_cc_override || bundled_target_cc_available;
  char *target_cc_version = target_cc_override ? z_strdup("configured via ZERO_CC") : (bundled_target_cc_available ? command_first_line("zig version 2>/dev/null") : z_strdup(""));

  ZBuf buf;
  zbuf_init(&buf);
  zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"version\": ");
  append_json_string(&buf, ZERO_VERSION);
  zbuf_append(&buf, ",\n  \"commit\": ");
  append_json_string(&buf, zero_commit());
  zbuf_append(&buf, ",\n  \"host\": ");
  append_json_string(&buf, z_host_target());
  zbuf_append(&buf, ",\n  \"backend\": \"zero-c\",\n  \"targets\": ");
  z_append_target_names_json(&buf);
  zbuf_append(&buf, ",\n  \"targetCompiler\": {\"available\": ");
  zbuf_append(&buf, target_cc_available ? "true" : "false");
  zbuf_append(&buf, ", \"kind\": \"target-capable C compiler\", \"version\": ");
  append_json_string(&buf, target_cc_version);
  zbuf_append(&buf, "},\n  \"crossCompilation\": {\"ready\": ");
  zbuf_append(&buf, target_cc_available ? "true" : "false");
  zbuf_append(&buf, "}\n}\n");
  fputs(buf.data, stdout);
  zbuf_free(&buf);
  free(target_cc_version);
  return 0;
}

static bool write_project_file(const char *root, const char *relative, const char *text, ZDiag *diag) {
  char *path = join_cli_path(root, relative);
  bool ok = z_write_file(path, text, diag);
  free(path);
  return ok;
}

static bool write_project_buf(const char *root, const char *relative, ZBuf *buf, ZDiag *diag) {
  bool ok = write_project_file(root, relative, buf->data ? buf->data : "", diag);
  zbuf_free(buf);
  return ok;
}

static bool append_manifest(ZBuf *buf, const char *name, const char *main_path) {
  zbuf_append(buf, "{\n");
  zbuf_append(buf, "  \"package\": {\n");
  zbuf_append(buf, "    \"name\": ");
  append_json_string(buf, name);
  zbuf_append(buf, ",\n    \"version\": \"0.1.0\",\n    \"license\": \"MIT\"\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"targets\": {\n");
  zbuf_append(buf, "    \"cli\": {\n");
  zbuf_append(buf, "      \"kind\": \"exe\",\n");
  zbuf_append(buf, "      \"main\": ");
  append_json_string(buf, main_path);
  zbuf_append(buf, ",\n      \"defaultTarget\": \"linux-musl-x64\",\n      \"devTarget\": \"host\",\n      \"releaseProfile\": \"release-small\"\n    }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"deps\": {},\n");
  zbuf_append(buf, "  \"profiles\": {\n");
  zbuf_append(buf, "    \"dev\": { \"inherits\": \"dev\" },\n");
  zbuf_append(buf, "    \"release-small\": { \"inherits\": \"release-small\" }\n");
  zbuf_append(buf, "  },\n");
  zbuf_append(buf, "  \"docs\": {\n");
  zbuf_append(buf, "    \"readme\": \"README.md\",\n");
  zbuf_append(buf, "    \"examples\": [");
  append_json_string(buf, main_path);
  zbuf_append(buf, "]\n");
  zbuf_append(buf, "  }\n");
  zbuf_append(buf, "}\n");
  return true;
}

static bool create_cli_template(const char *root, const char *name, ZDiag *diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  append_manifest(&manifest, name, "src/main.0");
  if (!write_project_buf(root, "zero.json", &manifest, diag)) return false;
  if (!write_project_file(root, "src/lib.0",
    "pub fn greeting_code i32\n"
    "  ret 42\n",
    diag)) return false;
  if (!write_project_file(root, "src/main.0",
    "use lib\n\n"
    "pub fn main Void world World !\n"
    "  if == greeting_code() 42\n"
    "    check world.out.write \"hello from zero\\n\"\n\n"
    "test \"greeting is stable\"\n"
    "  expect (== greeting_code() 42)\n",
    diag)) return false;
  if (!write_project_file(root, "README.md",
    "# Zero CLI\n\n"
    "This project was created with `zero new cli`.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero run .\n"
    "zero dev --json .\n"
    "zero build --target linux-musl-x64 --out .zero/out/app .\n"
    "zero ship --target linux-musl-x64 --out .zero/ship/app .\n"
    "```\n\n"
    "The entry point receives `World` explicitly, so I/O is visible in the function signature. The generated output is deterministic and the manifest records the default release target.\n",
    diag)) return false;
  return write_project_file(root, ".gitignore", ".zero/\n", diag);
}

static bool create_lib_template(const char *root, const char *name, ZDiag *diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  append_manifest(&manifest, name, "src/lib.0");
  if (!write_project_buf(root, "zero.json", &manifest, diag)) return false;
  if (!write_project_file(root, "src/lib.0",
    "pub fn add_one i32 value i32\n"
    "  ret + value 1\n\n"
    "test \"public api works\"\n"
    "  expect (== (add_one 41) 42)\n",
    diag)) return false;
  if (!write_project_file(root, "README.md",
    "# Zero Library\n\n"
    "This small package exposes one public function, docs metadata in `zero.json`, and an inline test.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero dev --json .\n"
    "zero graph --json .\n"
    "zero doc --json .\n"
    "```\n",
    diag)) return false;
  return write_project_file(root, ".gitignore", ".zero/\n", diag);
}

static bool create_package_template(const char *root, const char *name, ZDiag *diag) {
  ZBuf manifest;
  zbuf_init(&manifest);
  append_manifest(&manifest, name, "src/main.0");
  if (!write_project_buf(root, "zero.json", &manifest, diag)) return false;
  if (!write_project_file(root, "src/model.0",
    "pub type Point\n"
    "  value i32\n",
    diag)) return false;
  if (!write_project_file(root, "src/math.0",
    "fn base i32 value i32\n"
    "  ret value\n\n"
    "pub fn add_one i32 value i32\n"
    "  ret + (base value) 1\n",
    diag)) return false;
  if (!write_project_file(root, "src/main.0",
    "use math\n"
    "use model\n\n"
    "pub fn main Void world World !\n"
    "  let point Point . value (add_one 41)\n"
    "  if == point.value 42\n"
    "    check world.out.write \"package ok\\n\"\n\n"
    "test \"package import works\"\n"
    "  let point Point . value (add_one 41)\n"
    "  expect (== point.value 42)\n",
    diag)) return false;
  if (!write_project_file(root, "README.md",
    "# Zero Package\n\n"
    "This template shows package-local imports, one public symbol, and one private helper.\n\n"
    "Try:\n\n"
    "```sh\n"
    "zero check .\n"
    "zero test .\n"
    "zero run .\n"
    "zero dev --json .\n"
    "zero build --target linux-musl-x64 --out .zero/out/app .\n"
    "zero ship --target linux-musl-x64 --out .zero/ship/app .\n"
    "zero graph --json .\n"
    "```\n",
    diag)) return false;
  return write_project_file(root, ".gitignore", ".zero/\n", diag);
}

static int new_command(const Command *command) {
  ZDiag diag = {0};
  if (!command->kind || !command->input) {
    fprintf(stderr, "usage: zero new cli|lib|package <name>\n");
    return 1;
  }
  if (path_exists(command->input)) {
    fprintf(stderr, "zero new: '%s' already exists\n", command->input);
    return 1;
  }
  if (!ensure_dir(command->input, &diag)) {
    print_diag(command->input, &diag);
    return 1;
  }

  bool ok = false;
  if (strcmp(command->kind, "cli") == 0) ok = create_cli_template(command->input, command->input, &diag);
  else if (strcmp(command->kind, "lib") == 0) ok = create_lib_template(command->input, command->input, &diag);
  else if (strcmp(command->kind, "package") == 0) ok = create_package_template(command->input, command->input, &diag);
  else {
    fprintf(stderr, "zero new: unknown template '%s'\n", command->kind);
    fprintf(stderr, "help: choose cli, lib, or package\n");
    return 1;
  }
  if (!ok) {
    print_diag(diag.path ? diag.path : command->input, &diag);
    return 1;
  }

  printf("created %s project %s\n", command->kind, command->input);
  if (strcmp(command->kind, "lib") == 0) printf("next: cd %s && zero check . && zero test .\n", command->input);
  else printf("next: cd %s && zero check . && zero test . && zero run .\n", command->input);
  return 0;
}

static int clean_command(const Command *command) {
  const char *default_paths[] = {".zero/out", NULL};
  const char *all_paths[] = {
    ".zero/out",
    ".zero/native-test",
    ".zero/conformance",
    ".zero/command-contracts",
    ".zero/generated-c-audit",
    ".zero/bench",
    NULL
  };
  const char **paths = command->all ? all_paths : default_paths;
  ZBuf deleted;
  zbuf_init(&deleted);
  for (size_t i = 0; paths[i]; i++) {
    if (!remove_tree(paths[i], &deleted)) {
      fprintf(stderr, "zero clean: failed to remove %s: %s\n", paths[i], strerror(errno));
      zbuf_free(&deleted);
      return 1;
    }
  }
  if (deleted.len == 0) {
    printf("nothing to clean\n");
  } else {
    printf("removed:\n%s\n", deleted.data);
    if (command->all) printf("preserved .zero/bin so the local zero wrapper still works\n");
  }
  zbuf_free(&deleted);
  return 0;
}

static const char *worse_status(const char *current, const char *next) {
  if (strcmp(current, "error") == 0 || strcmp(next, "error") == 0) return "error";
  if (strcmp(current, "warning") == 0 || strcmp(next, "warning") == 0) return "warning";
  return "ok";
}

static void append_doctor_check_json(ZBuf *buf, bool *first, const char *name, const char *status, const char *message) {
  if (!*first) zbuf_append(buf, ",\n");
  *first = false;
  zbuf_append(buf, "    {\"name\": ");
  append_json_string(buf, name);
  zbuf_append(buf, ", \"status\": ");
  append_json_string(buf, status);
  zbuf_append(buf, ", \"message\": ");
  append_json_string(buf, message);
  zbuf_append(buf, "}");
}

static char *doctor_path_message(bool *ok) {
  const char *path = getenv("PATH");
  if (!path || !path[0]) {
    *ok = false;
    return z_strdup("PATH is empty");
  }
  char separator =
#ifdef _WIN32
    ';';
#else
    ':';
#endif
  size_t bad = 0;
  size_t entries = 0;
  const char *cursor = path;
  while (true) {
    const char *end = strchr(cursor, separator);
    size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
    entries++;
    if (len == 0) {
      bad++;
    } else {
      char *entry = z_strndup(cursor, len);
      struct stat st;
      if (stat(entry, &st) != 0 || !S_ISDIR(st.st_mode)) bad++;
      free(entry);
    }
    if (!end) break;
    cursor = end + 1;
  }
  *ok = bad == 0;
  ZBuf message;
  zbuf_init(&message);
  if (bad == 0) zbuf_appendf(&message, "PATH has %zu usable entries", entries);
  else zbuf_appendf(&message, "PATH has %zu bad or empty entries out of %zu", bad, entries);
  return message.data;
}

static void doctor_target_toolchain_status(const ZTargetInfo *target, const ZToolchainPlan *plan, bool cc_ok, bool target_cc_ok, const char **status, ZBuf *message) {
  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "host-leakage") == 0) {
    *status = "error";
    zbuf_appendf(message, "target sysroot for %s points at host SDK paths; set %s to a target SDK/sysroot", target->name, plan->sysroot_env);
    return;
  }

  if (strcmp(plan->driver_kind, "host-cc") == 0 && !cc_ok) {
    *status = "error";
    zbuf_append(message, "host cc is missing; install a native C compiler or pass --cc/ZERO_CC");
    return;
  }

  if (strcmp(plan->driver_kind, "zig-cc") == 0 && !target_cc_ok) {
    *status = "warning";
    zbuf_appendf(message, "target-capable C compiler is required for %s; pass --cc/ZERO_CC or install the bundled-toolchain default", target->name);
    return;
  }

  if (plan->requires_sysroot && strcmp(plan->sysroot_status, "missing") == 0) {
    *status = "warning";
    zbuf_appendf(message, "target %s requires sysroot mode; set %s to the target SDK/sysroot", target->name, plan->sysroot_env);
    return;
  }

  *status = "ok";
  zbuf_appendf(message, "%s ready for %s", public_compiler_label(plan), target->name);
}

static void append_doctor_target_toolchains_json(ZBuf *buf, size_t *ok_count, size_t *warning_count, size_t *error_count) {
  bool cc_ok = command_available("cc");
  bool target_cc_ok = command_available("zig");
  zbuf_append(buf, ",\n  \"targetToolchains\": [\n");
  for (size_t i = 0; i < z_target_count(); i++) {
    const ZTargetInfo *target = z_target_at(i);
    ZToolchainPlan plan = z_plan_toolchain(NULL, "release", target);
    const char *status = "ok";
    ZBuf message;
    zbuf_init(&message);
    doctor_target_toolchain_status(target, &plan, cc_ok, target_cc_ok, &status, &message);
    if (strcmp(status, "error") == 0) (*error_count)++;
    else if (strcmp(status, "warning") == 0) (*warning_count)++;
    else (*ok_count)++;

    zbuf_append(buf, "    {\"target\": ");
    append_json_string(buf, target->name);
    zbuf_append(buf, ", \"status\": ");
    append_json_string(buf, status);
    zbuf_append(buf, ", \"message\": ");
    append_json_string(buf, message.data ? message.data : "");
    zbuf_append(buf, ", \"driverKind\": ");
    append_json_string(buf, public_driver_kind(plan.driver_kind));
    zbuf_append(buf, ", \"compiler\": ");
    append_json_string(buf, public_compiler_label(&plan));
    zbuf_append(buf, ", \"selectionSource\": ");
    append_json_string(buf, plan.selection_source);
    zbuf_append(buf, ", \"targetTriple\": ");
    append_json_string(buf, plan.target_triple);
    zbuf_append(buf, ", \"linkerFlavor\": ");
    append_json_string(buf, public_linker_label(plan.linker_flavor));
    zbuf_append(buf, ", \"libcMode\": ");
    append_json_string(buf, plan.libc_mode);
    zbuf_appendf(buf, ", \"requiresSysroot\": %s", plan.requires_sysroot ? "true" : "false");
    zbuf_append(buf, ", \"sysrootEnv\": ");
    append_json_string(buf, plan.sysroot_env);
    zbuf_append(buf, ", \"sysrootStatus\": ");
    append_json_string(buf, plan.sysroot_status);
    zbuf_appendf(buf, "}%s\n", i + 1 < z_target_count() ? "," : "");
    zbuf_free(&message);
  }
  zbuf_append(buf, "  ]");
}

static void doctor_target_toolchain_counts(size_t *ok_count, size_t *warning_count, size_t *error_count) {
  ZBuf unused;
  zbuf_init(&unused);
  append_doctor_target_toolchains_json(&unused, ok_count, warning_count, error_count);
  zbuf_free(&unused);
}

static int doctor_command(bool json) {
  const char *overall = "ok";
  const char *host_status = strcmp(z_host_target(), "unknown") == 0 ? "warning" : "ok";
  bool cc_ok = command_available("cc") || command_available("clang") || command_available("gcc");
  const char *zero_cc = getenv("ZERO_CC");
  bool target_cc_override = zero_cc && zero_cc[0];
  bool bundled_target_cc_ok = command_available("zig");
  bool target_cc_ok = target_cc_override || bundled_target_cc_ok;
  bool zero_dir_ok = zero_mkdir(".zero") == 0 || errno == EEXIST;
  bool write_ok = false;
  if (zero_dir_ok) {
    ZDiag diag = {0};
    write_ok = z_write_file(".zero/doctor.tmp", "ok\n", &diag);
    unlink(".zero/doctor.tmp");
  }
  bool docs_ok = path_exists("examples/hello.0") && path_exists("docs/articles/getting-started.md");
  bool path_ok = false;
  char *path_message = doctor_path_message(&path_ok);
  bool target_ok = z_find_target(z_host_target()) != NULL;
  size_t target_toolchains_ok = 0;
  size_t target_toolchains_warning = 0;
  size_t target_toolchains_error = 0;
  doctor_target_toolchain_counts(&target_toolchains_ok, &target_toolchains_warning, &target_toolchains_error);
  const char *target_toolchains_status = target_toolchains_error > 0 ? "error" : (target_toolchains_warning > 0 ? "warning" : "ok");
  const char *sdk_status = target_cc_ok ? "ok" : "warning";
  const char *sdk_message = target_cc_ok ? "target-capable C compiler available for non-host linker/sysroot support" : "configure a target-capable C compiler/sysroot before building non-host executables";
  overall = worse_status(overall, host_status);
  overall = worse_status(overall, cc_ok ? "ok" : "error");
  overall = worse_status(overall, target_cc_ok ? "ok" : "warning");
  overall = worse_status(overall, path_ok ? "ok" : "warning");
  overall = worse_status(overall, target_ok ? "ok" : "warning");
  overall = worse_status(overall, sdk_status);
  overall = worse_status(overall, target_toolchains_status);
  overall = worse_status(overall, (zero_dir_ok && write_ok) ? "ok" : "error");
  overall = worse_status(overall, docs_ok ? "ok" : "warning");

  char *cc_message = cc_ok ? command_first_line("cc --version 2>/dev/null") : z_strdup("no native C compiler found on PATH");
  char *target_cc_message = target_cc_override ? z_strdup("configured via ZERO_CC") : (bundled_target_cc_ok ? command_first_line("zig version 2>/dev/null") : z_strdup("target-capable C compiler not found; cross-target executable builds may be unavailable"));

  if (json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"status\": ");
    append_json_string(&buf, overall);
    zbuf_append(&buf, ",\n  \"host\": ");
    append_json_string(&buf, z_host_target());
    zbuf_append(&buf, ",\n  \"checks\": [\n");
    bool first = true;
    append_doctor_check_json(&buf, &first, "host", host_status, z_host_target());
    append_doctor_check_json(&buf, &first, "native-c-compiler", cc_ok ? "ok" : "error", cc_message);
    append_doctor_check_json(&buf, &first, "target-c-compiler", target_cc_ok ? "ok" : "warning", target_cc_message);
    append_doctor_check_json(&buf, &first, "cross-executable-builds", target_cc_ok ? "ok" : "warning", target_cc_ok ? "target-capable C compiler available for non-host executable builds" : "pass --cc/ZERO_CC or install a target-capable C compiler for non-host executable builds");
    append_doctor_check_json(&buf, &first, "path", path_ok ? "ok" : "warning", path_message);
    append_doctor_check_json(&buf, &first, "host-target", target_ok ? "ok" : "warning", target_ok ? "host target is supported" : "host target is not in the bundled target list");
    append_doctor_check_json(&buf, &first, "target-sdk-sysroot", sdk_status, sdk_message);
    append_doctor_check_json(&buf, &first, ".zero-write", (zero_dir_ok && write_ok) ? "ok" : "error", (zero_dir_ok && write_ok) ? ".zero is writable" : ".zero is not writable");
    append_doctor_check_json(&buf, &first, "targets", target_ok ? "ok" : "warning", "run `zero targets` for the supported target list");
    append_doctor_check_json(&buf, &first, "docs-examples", docs_ok ? "ok" : "warning", docs_ok ? "docs and examples found" : "docs or examples are missing from this checkout");
    zbuf_append(&buf, "\n  ]");
    target_toolchains_ok = 0;
    target_toolchains_warning = 0;
    target_toolchains_error = 0;
    append_doctor_target_toolchains_json(&buf, &target_toolchains_ok, &target_toolchains_warning, &target_toolchains_error);
    zbuf_append(&buf, "\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else {
    ZBuf target_toolchain_message;
    zbuf_init(&target_toolchain_message);
    zbuf_appendf(&target_toolchain_message, "%zu ready, %zu warnings, %zu errors; run `zero doctor --json` for per-target details", target_toolchains_ok, target_toolchains_warning, target_toolchains_error);
    printf("zero doctor: %s\n", overall);
    printf("host: %s (%s)\n", host_status, z_host_target());
    printf("native C compiler: %s (%s)\n", cc_ok ? "ok" : "error", cc_message);
    printf("target C compiler: %s (%s)\n", target_cc_ok ? "ok" : "warning", target_cc_message);
    printf("cross executable builds: %s (%s)\n", target_cc_ok ? "ok" : "warning", target_cc_ok ? "target-capable C compiler available" : "pass --cc/ZERO_CC or install a target-capable C compiler");
    printf("PATH: %s (%s)\n", path_ok ? "ok" : "warning", path_message);
    printf("host target: %s (%s)\n", target_ok ? "ok" : "warning", target_ok ? "supported" : "unsupported host/target combination");
    printf("target SDK/sysroot: %s (%s)\n", sdk_status, sdk_message);
    printf("target toolchains: %s (%s)\n", target_toolchains_status, target_toolchain_message.data ? target_toolchain_message.data : "no target data");
    printf(".zero write: %s\n", (zero_dir_ok && write_ok) ? "ok" : "error");
    printf("targets: %s (run `zero targets`)\n", target_ok ? "ok" : "warning");
    printf("docs/examples: %s\n", docs_ok ? "ok" : "warning");
    zbuf_free(&target_toolchain_message);
  }

  free(cc_message);
  free(target_cc_message);
  free(path_message);
  return strcmp(overall, "error") == 0 ? 1 : 0;
}

static void append_function_effects_json(ZBuf *buf, const Function *fun) {
  CapabilitySummary caps = function_capabilities(fun);
  append_capability_json_array(buf, &caps);
}

static void append_function_error_json(ZBuf *buf, const Function *fun) {
  zbuf_append(buf, "\"errorSetKind\":");
  append_json_string(buf, !fun || !fun->raises ? "none" : (fun->has_error_set ? "explicit" : (fun->is_public ? "open" : "inferred")));
  zbuf_append(buf, ",\"errorNames\":[");
  if (fun && fun->has_error_set) {
    for (size_t i = 0; i < fun->errors.len; i++) {
      if (i > 0) zbuf_append(buf, ",");
      append_json_string(buf, fun->errors.items[i].name);
    }
  }
  zbuf_append(buf, "]");
}

static bool text_contains(const char *text, const char *needle) {
  return text && needle && strstr(text, needle) != NULL;
}

static const char *type_ownership_kind(const char *type) {
  if (!type) return "unknown";
  if (text_contains(type, "owned<")) return "owned";
  if (text_contains(type, "mutref<") || text_contains(type, "MutSpan<")) return "mutable-borrow";
  if (text_contains(type, "ref<") || text_contains(type, "Span<") || strcmp(type, "String") == 0) return "borrow";
  return "value";
}

static const char *function_allocation_behavior(const Function *fun) {
  CapabilitySummary caps = function_capabilities(fun);
  if (caps.alloc) return "uses explicit allocator";
  return "no heap allocation";
}

static bool capability_available_on_target(const ZTargetInfo *target, const char *capability) {
  if (!capability) return true;
  if (strcmp(capability, "alloc") == 0 || strcmp(capability, "path") == 0 || strcmp(capability, "codec") == 0 ||
      strcmp(capability, "parse") == 0) {
    return true;
  }
  if (strcmp(capability, "world") == 0) return z_target_has_capability(target, "stdio");
  return z_target_has_capability(target, capability);
}

static bool append_missing_capabilities_json(ZBuf *buf, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool wrote = false;
  zbuf_append(buf, "[");
#define APPEND_MISSING_CAP(name, enabled) do { \
    if ((enabled) && !capability_available_on_target(target, name)) { \
      if (wrote) zbuf_append(buf, ","); \
      append_json_string(buf, name); \
      wrote = true; \
    } \
  } while (0)
  APPEND_MISSING_CAP("args", caps && caps->args);
  APPEND_MISSING_CAP("env", caps && caps->env);
  APPEND_MISSING_CAP("fs", caps && caps->fs);
  APPEND_MISSING_CAP("memory", caps && caps->memory);
  APPEND_MISSING_CAP("alloc", caps && caps->alloc);
  APPEND_MISSING_CAP("path", caps && caps->path);
  APPEND_MISSING_CAP("codec", caps && caps->codec);
  APPEND_MISSING_CAP("parse", caps && caps->parse);
  APPEND_MISSING_CAP("time", caps && caps->time);
  APPEND_MISSING_CAP("rand", caps && caps->rand);
  APPEND_MISSING_CAP("net", caps && caps->net);
  APPEND_MISSING_CAP("proc", caps && caps->proc);
  APPEND_MISSING_CAP("web", caps && caps->web);
  APPEND_MISSING_CAP("world", caps && caps->world);
#undef APPEND_MISSING_CAP
  zbuf_append(buf, "]");
  return wrote;
}

static void append_target_capability_facts_json(ZBuf *buf, const ZTargetInfo *target, const CapabilitySummary *caps) {
  zbuf_append(buf, "{\"status\":");
  ZBuf missing;
  zbuf_init(&missing);
  bool has_missing = append_missing_capabilities_json(&missing, caps, target);
  append_json_string(buf, has_missing ? "unsupported" : "supported");
  zbuf_append(buf, ",\"missingCapabilities\":");
  zbuf_append(buf, missing.data ? missing.data : "[]");
  zbuf_append(buf, "}");
  zbuf_free(&missing);
}

static void append_function_ownership_json(ZBuf *buf, const Function *fun) {
  zbuf_append(buf, "{\"params\":[");
  bool moves = false;
  for (size_t i = 0; fun && i < fun->params.len; i++) {
    Param *param = &fun->params.items[i];
    if (i > 0) zbuf_append(buf, ",");
    const char *ownership = type_ownership_kind(param->type);
    if (strcmp(ownership, "owned") == 0) moves = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, param->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, param->type);
    zbuf_append(buf, ",\"ownership\":");
    append_json_string(buf, ownership);
    zbuf_append(buf, "}");
  }
  const char *return_ownership = type_ownership_kind(fun ? fun->return_type : NULL);
  if (strcmp(return_ownership, "owned") == 0) moves = true;
  zbuf_append(buf, "],\"returnOwnership\":");
  append_json_string(buf, return_ownership);
  zbuf_appendf(buf, ",\"movesOwnership\":%s}", moves ? "true" : "false");
}

static const Function *find_program_function(const Program *program, const char *name) {
  if (!program || !name) return NULL;
  for (size_t i = 0; i < program->functions.len; i++) {
    if (strcmp(program->functions.items[i].name, name) == 0) return &program->functions.items[i];
  }
  return NULL;
}

typedef struct TestValue TestValue;

typedef struct {
  char *name;
  TestValue *value;
} TestFieldValue;

struct TestValue {
  enum { TEST_VALUE_VOID, TEST_VALUE_BOOL, TEST_VALUE_INT, TEST_VALUE_STRING, TEST_VALUE_SHAPE } kind;
  bool bool_value;
  long long int_value;
  char *string_value;
  TestFieldValue *fields;
  size_t field_len;
};

typedef struct {
  char *name;
  TestValue value;
} TestBinding;

typedef struct {
  TestBinding *items;
  size_t len;
  size_t cap;
} TestEnv;

typedef struct {
  const char *current_test;
  int line;
  int column;
  char message[256];
} TestRunFailure;

static void test_value_free(TestValue *value) {
  if (!value) return;
  free(value->string_value);
  for (size_t i = 0; i < value->field_len; i++) {
    free(value->fields[i].name);
    test_value_free(value->fields[i].value);
    free(value->fields[i].value);
  }
  free(value->fields);
  memset(value, 0, sizeof(*value));
}

static TestValue test_value_copy(const TestValue *value) {
  TestValue copy = {0};
  if (!value) return copy;
  copy.kind = value->kind;
  copy.bool_value = value->bool_value;
  copy.int_value = value->int_value;
  copy.string_value = value->string_value ? z_strdup(value->string_value) : NULL;
  copy.field_len = value->field_len;
  if (copy.field_len > 0) {
    copy.fields = z_checked_calloc(copy.field_len, sizeof(TestFieldValue));
    for (size_t i = 0; i < copy.field_len; i++) {
      copy.fields[i].name = z_strdup(value->fields[i].name);
      copy.fields[i].value = z_checked_calloc(1, sizeof(TestValue));
      *copy.fields[i].value = test_value_copy(value->fields[i].value);
    }
  }
  return copy;
}

static void test_env_free(TestEnv *env) {
  for (size_t i = 0; env && i < env->len; i++) {
    free(env->items[i].name);
    test_value_free(&env->items[i].value);
  }
  free(env ? env->items : NULL);
  if (env) memset(env, 0, sizeof(*env));
}

static bool test_env_set(TestEnv *env, const char *name, const TestValue *value) {
  if (!env || !name || !value) return false;
  for (size_t i = 0; i < env->len; i++) {
    if (strcmp(env->items[i].name, name) == 0) {
      test_value_free(&env->items[i].value);
      env->items[i].value = test_value_copy(value);
      return true;
    }
  }
  if (env->len + 1 > env->cap) {
    env->cap = z_grow_capacity(env->cap, env->len + 1, 8);
    env->items = z_checked_reallocarray(env->items, env->cap, sizeof(TestBinding));
  }
  env->items[env->len].name = z_strdup(name);
  env->items[env->len].value = test_value_copy(value);
  env->len++;
  return true;
}

static bool test_env_get(const TestEnv *env, const char *name, TestValue *out) {
  for (size_t i = 0; env && i < env->len; i++) {
    if (strcmp(env->items[i].name, name) == 0) {
      *out = test_value_copy(&env->items[i].value);
      return true;
    }
  }
  return false;
}

static void test_fail(TestRunFailure *failure, const Expr *expr, const char *message) {
  if (!failure || failure->message[0]) return;
  if (expr) {
    failure->line = expr->line;
    failure->column = expr->column;
  }
  snprintf(failure->message, sizeof(failure->message), "%s%s%s",
           message ? message : "zero test expectation failed",
           failure->current_test ? ": " : "",
           failure->current_test ? failure->current_test : "");
  (void)expr;
}

static bool test_expected_failure(const Function *fun) {
  const char *name = fun && fun->test_name ? fun->test_name : "";
  return strncmp(name, "xfail:", strlen("xfail:")) == 0 ||
         strncmp(name, "expected fail:", strlen("expected fail:")) == 0 ||
         strstr(name, "[xfail]") != NULL;
}

static bool test_expr_name(const Expr *expr, ZBuf *out) {
  if (!expr) return false;
  if (expr->kind == EXPR_IDENT) {
    zbuf_append(out, expr->text ? expr->text : "");
    return true;
  }
  if (expr->kind == EXPR_MEMBER) {
    if (!test_expr_name(expr->left, out)) return false;
    zbuf_append_char(out, '.');
    zbuf_append(out, expr->text ? expr->text : "");
    return true;
  }
  return false;
}

static bool test_value_truthy(const TestValue *value) {
  if (!value) return false;
  if (value->kind == TEST_VALUE_BOOL) return value->bool_value;
  if (value->kind == TEST_VALUE_INT) return value->int_value != 0;
  return false;
}

static bool test_value_equals(const TestValue *left, const TestValue *right) {
  if (!left || !right) return false;
  if (left->kind == TEST_VALUE_STRING && right->kind == TEST_VALUE_STRING) {
    return strcmp(left->string_value ? left->string_value : "", right->string_value ? right->string_value : "") == 0;
  }
  if (left->kind == TEST_VALUE_BOOL && right->kind == TEST_VALUE_BOOL) return left->bool_value == right->bool_value;
  if ((left->kind == TEST_VALUE_INT || left->kind == TEST_VALUE_BOOL) &&
      (right->kind == TEST_VALUE_INT || right->kind == TEST_VALUE_BOOL)) {
    long long a = left->kind == TEST_VALUE_BOOL ? (left->bool_value ? 1 : 0) : left->int_value;
    long long b = right->kind == TEST_VALUE_BOOL ? (right->bool_value ? 1 : 0) : right->int_value;
    return a == b;
  }
  return false;
}

static bool test_eval_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure);

static bool test_eval_stmt_vec(const Program *program, TestEnv *env, const StmtVec *body, TestValue *return_value, bool *returned, TestRunFailure *failure) {
  for (size_t i = 0; body && i < body->len; i++) {
    Stmt *stmt = body->items[i];
    if (!stmt) continue;
    if (stmt->kind == STMT_LET) {
      TestValue value = {0};
      if (!test_eval_expr(program, env, stmt->expr, &value, failure)) return false;
      test_env_set(env, stmt->name, &value);
      test_value_free(&value);
    } else if (stmt->kind == STMT_RETURN) {
      if (stmt->expr) {
        if (!test_eval_expr(program, env, stmt->expr, return_value, failure)) return false;
      } else {
        return_value->kind = TEST_VALUE_VOID;
      }
      *returned = true;
      return true;
    } else if (stmt->kind == STMT_EXPR) {
      if (stmt->expr && stmt->expr->kind == EXPR_CALL) {
        ZBuf name;
        zbuf_init(&name);
        bool named = test_expr_name(stmt->expr->left, &name);
        if (named && strcmp(name.data ? name.data : "", "expect") == 0 && stmt->expr->args.len == 1) {
          TestValue condition = {0};
          bool ok = test_eval_expr(program, env, stmt->expr->args.items[0], &condition, failure);
          if (!ok) {
            zbuf_free(&name);
            return false;
          }
          bool passed = test_value_truthy(&condition);
          test_value_free(&condition);
          zbuf_free(&name);
          if (!passed) {
            test_fail(failure, stmt->expr, "zero test expectation failed");
            return false;
          }
          continue;
        }
        zbuf_free(&name);
      }
      TestValue ignored = {0};
      bool ok = test_eval_expr(program, env, stmt->expr, &ignored, failure);
      test_value_free(&ignored);
      if (!ok) return false;
    } else if (stmt->kind == STMT_IF) {
      TestValue condition = {0};
      if (!test_eval_expr(program, env, stmt->expr, &condition, failure)) return false;
      bool branch = test_value_truthy(&condition);
      test_value_free(&condition);
      if (!test_eval_stmt_vec(program, env, branch ? &stmt->then_body : &stmt->else_body, return_value, returned, failure)) return false;
      if (*returned) return true;
    } else {
      test_fail(failure, NULL, "zero test direct runner does not support this statement yet");
      return false;
    }
  }
  return true;
}

static bool test_eval_function(const Program *program, const Function *fun, const TestValue *args, size_t arg_len, TestValue *out, TestRunFailure *failure) {
  if (!fun) {
    test_fail(failure, NULL, "zero test unknown function");
    return false;
  }
  if (fun->params.len != arg_len) {
    test_fail(failure, NULL, "zero test function argument count mismatch");
    return false;
  }
  TestEnv local = {0};
  for (size_t i = 0; i < arg_len; i++) test_env_set(&local, fun->params.items[i].name, &args[i]);
  bool returned = false;
  bool ok = test_eval_stmt_vec(program, &local, &fun->body, out, &returned, failure);
  test_env_free(&local);
  if (!ok) return false;
  if (!returned) out->kind = TEST_VALUE_VOID;
  return true;
}

static bool test_eval_expr(const Program *program, TestEnv *env, const Expr *expr, TestValue *out, TestRunFailure *failure) {
  if (!expr || !out) return false;
  switch (expr->kind) {
    case EXPR_BOOL:
      out->kind = TEST_VALUE_BOOL;
      out->bool_value = expr->bool_value;
      return true;
    case EXPR_NUMBER:
      out->kind = TEST_VALUE_INT;
      out->int_value = strtoll(expr->text ? expr->text : "0", NULL, 0);
      return true;
    case EXPR_STRING:
      out->kind = TEST_VALUE_STRING;
      out->string_value = z_strdup(expr->text ? expr->text : "");
      return true;
    case EXPR_IDENT:
      if (test_env_get(env, expr->text, out)) return true;
      test_fail(failure, expr, "zero test unknown identifier");
      return false;
    case EXPR_SHAPE_LITERAL:
      out->kind = TEST_VALUE_SHAPE;
      out->field_len = expr->fields.len;
      out->fields = z_checked_calloc(out->field_len ? out->field_len : 1, sizeof(TestFieldValue));
      for (size_t i = 0; i < expr->fields.len; i++) {
        out->fields[i].name = z_strdup(expr->fields.items[i].name);
        out->fields[i].value = z_checked_calloc(1, sizeof(TestValue));
        if (!test_eval_expr(program, env, expr->fields.items[i].value, out->fields[i].value, failure)) return false;
      }
      return true;
    case EXPR_MEMBER: {
      TestValue base = {0};
      if (!test_eval_expr(program, env, expr->left, &base, failure)) return false;
      if (base.kind == TEST_VALUE_SHAPE) {
        for (size_t i = 0; i < base.field_len; i++) {
          if (strcmp(base.fields[i].name, expr->text ? expr->text : "") == 0) {
            *out = test_value_copy(base.fields[i].value);
            test_value_free(&base);
            return true;
          }
        }
      }
      test_value_free(&base);
      test_fail(failure, expr, "zero test unknown field");
      return false;
    }
    case EXPR_BINARY: {
      TestValue left = {0};
      TestValue right = {0};
      if (!test_eval_expr(program, env, expr->left, &left, failure) ||
          !test_eval_expr(program, env, expr->right, &right, failure)) {
        test_value_free(&left);
        test_value_free(&right);
        return false;
      }
      const char *op = expr->text ? expr->text : "";
      if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
        out->kind = TEST_VALUE_BOOL;
        out->bool_value = test_value_equals(&left, &right);
        if (strcmp(op, "!=") == 0) out->bool_value = !out->bool_value;
      } else if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
        out->kind = TEST_VALUE_BOOL;
        out->bool_value = strcmp(op, "&&") == 0 ? (test_value_truthy(&left) && test_value_truthy(&right)) : (test_value_truthy(&left) || test_value_truthy(&right));
      } else {
        long long a = left.kind == TEST_VALUE_BOOL ? (left.bool_value ? 1 : 0) : left.int_value;
        long long b = right.kind == TEST_VALUE_BOOL ? (right.bool_value ? 1 : 0) : right.int_value;
        out->kind = TEST_VALUE_INT;
        if (strcmp(op, "+") == 0) out->int_value = a + b;
        else if (strcmp(op, "-") == 0) out->int_value = a - b;
        else if (strcmp(op, "*") == 0) out->int_value = a * b;
        else if (strcmp(op, "/") == 0) out->int_value = b == 0 ? 0 : a / b;
        else if (strcmp(op, "%") == 0) out->int_value = b == 0 ? 0 : a % b;
        else if (strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 || strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
          out->kind = TEST_VALUE_BOOL;
          if (strcmp(op, "<") == 0) out->bool_value = a < b;
          else if (strcmp(op, "<=") == 0) out->bool_value = a <= b;
          else if (strcmp(op, ">") == 0) out->bool_value = a > b;
          else out->bool_value = a >= b;
        } else {
          test_fail(failure, expr, "zero test unsupported operator");
          test_value_free(&left);
          test_value_free(&right);
          return false;
        }
      }
      test_value_free(&left);
      test_value_free(&right);
      return true;
    }
    case EXPR_CALL: {
      ZBuf name;
      zbuf_init(&name);
      if (!test_expr_name(expr->left, &name)) {
        zbuf_free(&name);
        test_fail(failure, expr, "zero test unsupported callee");
        return false;
      }
      const char *callee_name = name.data ? name.data : "";
      TestValue *args = z_checked_calloc(expr->args.len ? expr->args.len : 1, sizeof(TestValue));
      for (size_t i = 0; i < expr->args.len; i++) {
        if (!test_eval_expr(program, env, expr->args.items[i], &args[i], failure)) {
          for (size_t j = 0; j <= i; j++) test_value_free(&args[j]);
          free(args);
          zbuf_free(&name);
          return false;
        }
      }
      bool ok = true;
      if (strcmp(callee_name, "std.mem.eql") == 0 && expr->args.len == 2) {
        out->kind = TEST_VALUE_BOOL;
        out->bool_value = test_value_equals(&args[0], &args[1]);
      } else {
        const Function *fun = find_program_function(program, callee_name);
        ok = test_eval_function(program, fun, args, expr->args.len, out, failure);
      }
      for (size_t i = 0; i < expr->args.len; i++) test_value_free(&args[i]);
      free(args);
      zbuf_free(&name);
      return ok;
    }
    default:
      test_fail(failure, expr, "zero test direct runner does not support this expression yet");
      return false;
  }
}

static int run_tests_direct(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target) {
  long long started_ms = now_ms();
  size_t discovered = program_test_count(program, NULL);
  size_t selected = 0;
  size_t passed = 0;
  size_t failed = 0;
  size_t expected_failures = 0;
  size_t unexpected_passes = 0;
  TestRunFailure first_failure = {0};
  ZBuf results;
  zbuf_init(&results);
  zbuf_append(&results, "[");
  bool wrote_result = false;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (!fun->is_test) continue;
    if (command && command->filter && command->filter[0] && (!fun->test_name || !strstr(fun->test_name, command->filter))) continue;
    selected++;
    TestRunFailure failure = {0};
    failure.current_test = fun->test_name ? fun->test_name : fun->name;
    failure.line = fun->line;
    failure.column = fun->column;
    bool expected_failure = test_expected_failure(fun);
    long long test_started_ms = now_ms();
    TestValue ignored = {0};
    bool ok = test_eval_function(program, fun, NULL, 0, &ignored, &failure);
    long long test_duration_ms = now_ms() - test_started_ms;
    test_value_free(&ignored);

    const char *status = "passed";
    const char *failure_message = NULL;
    if (expected_failure && ok) {
      status = "unexpected-pass";
      failure_message = "expected failure unexpectedly passed";
      failed++;
      unexpected_passes++;
      if (!first_failure.message[0]) {
        first_failure.current_test = failure.current_test;
        first_failure.line = fun->line;
        first_failure.column = fun->column;
        snprintf(first_failure.message, sizeof(first_failure.message), "zero test expected failure unexpectedly passed: %s", failure.current_test ? failure.current_test : fun->name);
      }
    } else if (expected_failure && !ok) {
      status = "expected-fail";
      expected_failures++;
      failure_message = failure.message[0] ? failure.message : "expected failure";
    } else if (ok) {
      passed++;
    } else {
      status = "failed";
      failed++;
      failure_message = failure.message[0] ? failure.message : "zero test failed";
      if (!first_failure.message[0]) first_failure = failure;
    }

    if (wrote_result) zbuf_append(&results, ", ");
    wrote_result = true;
    zbuf_append(&results, "{\"name\":");
    append_json_string(&results, failure.current_test ? failure.current_test : fun->name);
    zbuf_append(&results, ",\"status\":");
    append_json_string(&results, status);
    zbuf_appendf(&results, ",\"expectedFailure\":%s,\"durationMs\":%lld,\"location\":{\"sourceFile\":", expected_failure ? "true" : "false", test_duration_ms);
    append_json_string(&results, input ? input->source_file : "");
    zbuf_appendf(&results, ",\"line\":%d,\"column\":%d}", fun->line, fun->column);
    zbuf_append(&results, ",\"failure\":");
    if (failure_message) {
      zbuf_append(&results, "{\"message\":");
      append_json_string(&results, failure_message);
      zbuf_appendf(&results, ",\"line\":%d,\"column\":%d}", failure.line ? failure.line : fun->line, failure.column ? failure.column : fun->column);
    } else {
      zbuf_append(&results, "null");
    }
    zbuf_append(&results, "}");
  }
  zbuf_append(&results, "]");

  char stdout_text[64];
  snprintf(stdout_text, sizeof(stdout_text), "%zu test(s) ok\n", selected);
  const char *stderr_text = first_failure.message[0] ? first_failure.message : "";
  bool ok = failed == 0;
  long long duration_ms = now_ms() - started_ms;
  if (command && command->json) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "{\n  \"schemaVersion\": 1,\n  \"ok\": ");
    zbuf_append(&buf, ok ? "true" : "false");
    zbuf_append(&buf, ",\n  \"sourceFile\": ");
    append_json_string(&buf, input ? input->source_file : "");
    zbuf_append(&buf, ",\n  \"target\": ");
    append_json_string(&buf, target ? target->name : z_host_target());
    zbuf_append(&buf, ",\n  \"testBackend\": \"direct-frontend\",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"selectedTests\": ");
    zbuf_appendf(&buf, "%zu", selected);
    zbuf_append(&buf, ",\n  \"discoveredTests\": ");
    zbuf_appendf(&buf, "%zu", discovered);
    zbuf_append(&buf, ",\n  \"passedTests\": ");
    zbuf_appendf(&buf, "%zu", passed);
    zbuf_append(&buf, ",\n  \"failedTests\": ");
    zbuf_appendf(&buf, "%zu", failed);
    zbuf_append(&buf, ",\n  \"expectedFailures\": ");
    zbuf_appendf(&buf, "%zu", expected_failures);
    zbuf_append(&buf, ",\n  \"unexpectedPasses\": ");
    zbuf_appendf(&buf, "%zu", unexpected_passes);
    zbuf_append(&buf, ",\n  \"durationMs\": ");
    zbuf_appendf(&buf, "%lld", duration_ms);
    zbuf_append(&buf, ",\n  \"exitCode\": ");
    zbuf_append(&buf, ok ? "0" : "1");
    zbuf_append(&buf, ",\n  \"stdout\": ");
    append_json_string(&buf, ok ? stdout_text : "");
    zbuf_append(&buf, ",\n  \"stderr\": ");
    append_json_string(&buf, stderr_text);
    zbuf_append(&buf, ",\n  \"testDiscovery\": {\"mode\":");
    append_json_string(&buf, input && input->package_root ? "package" : "single-file");
    zbuf_append(&buf, ",\"filter\":");
    if (command && command->filter) append_json_string(&buf, command->filter);
    else zbuf_append(&buf, "null");
    zbuf_append(&buf, ",\"packageRoot\":");
    append_json_string(&buf, input && input->package_root ? input->package_root : "");
    zbuf_append(&buf, ",\"manifestPath\":");
    append_json_string(&buf, input && input->manifest_path ? input->manifest_path : "");
    zbuf_appendf(&buf, ",\"sourceFileCount\":%zu,\"moduleCount\":%zu,\"discoveredTests\":%zu,\"selectedTests\":%zu}",
                 input ? input->source_file_count : 0,
                 input ? input->module_count : 0,
                 discovered,
                 selected);
    zbuf_append(&buf, ",\n  \"fixtures\": {\"sourceFiles\":[");
    for (size_t i = 0; input && i < input->source_file_count; i++) {
      if (i > 0) zbuf_append(&buf, ", ");
      append_json_string(&buf, input->source_files[i]);
    }
    zbuf_append(&buf, "],\"goldenOutput\":");
    append_json_string(&buf, stdout_text);
    zbuf_append(&buf, ",\"snapshotKey\":\"zero-test-direct-frontend-v1\"}");
    CapabilitySummary caps = program_capabilities(program);
    zbuf_append(&buf, ",\n  \"targetFacts\": {\"hostTarget\":");
    append_json_string(&buf, z_host_target());
    zbuf_append(&buf, ",\"capabilitySupport\":");
    append_target_capability_facts_json(&buf, target, &caps);
    zbuf_append(&buf, "}");
    zbuf_append(&buf, ",\n  \"results\": ");
    zbuf_append(&buf, results.data ? results.data : "[]");
    zbuf_append(&buf, "\n}\n");
    fputs(buf.data, stdout);
    zbuf_free(&buf);
  } else if (!ok) {
    fprintf(stderr, "%s\n", first_failure.message);
  } else {
    fputs(stdout_text, stdout);
  }
  zbuf_free(&results);
  return ok ? 0 : 1;
}

static void append_std_helper_object_json(ZBuf *buf, const ZStdHelperInfo *helper, bool include_estimate);

static void append_stdlib_helpers_json(ZBuf *buf) {
  zbuf_append(buf, "[");
  for (size_t i = 0; z_std_helpers[i].name; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[i], false);
  }
  zbuf_append(buf, "]");
}

static int helper_estimated_direct_bytes(const ZStdHelperInfo *helper) {
  if (!helper || !helper->emits_runtime_helper) return 0;
  return 96 + (int)strlen(helper->name) * 4 + helper->arg_count * 16;
}

static const char *helper_module_name(const ZStdHelperInfo *helper) {
  const char *name = helper && helper->name ? helper->name : "";
  if (strncmp(name, "std.args.", strlen("std.args.")) == 0) return "std.args";
  if (strncmp(name, "std.env.", strlen("std.env.")) == 0) return "std.env";
  if (strncmp(name, "std.path.", strlen("std.path.")) == 0) return "std.path";
  if (strncmp(name, "std.io.", strlen("std.io.")) == 0) return "std.io";
  if (strncmp(name, "std.codec.", strlen("std.codec.")) == 0) return "std.codec";
  if (strncmp(name, "std.mem.", strlen("std.mem.")) == 0) return "std.mem";
  if (strncmp(name, "std.parse.", strlen("std.parse.")) == 0) return "std.parse";
  if (strncmp(name, "std.json.", strlen("std.json.")) == 0) return "std.json";
  if (strncmp(name, "std.time.", strlen("std.time.")) == 0) return "std.time";
  if (strncmp(name, "std.rand.", strlen("std.rand.")) == 0) return "std.rand";
  if (strncmp(name, "std.proc.", strlen("std.proc.")) == 0) return "std.proc";
  if (strncmp(name, "std.crypto.", strlen("std.crypto.")) == 0) return "std.crypto";
  if (strncmp(name, "std.net.", strlen("std.net.")) == 0) return "std.net";
  if (strncmp(name, "std.http.", strlen("std.http.")) == 0) return "std.http";
  if (strncmp(name, "std.fs.", strlen("std.fs.")) == 0) return "std.fs";
  return "std";
}

static const char *helper_error_behavior(const ZStdHelperInfo *helper) {
  if (!helper) return "unknown";
  if (strstr(helper->name, "OrRaise")) return "![NotFound TooLarge Io]";
  if (helper->return_type && strncmp(helper->return_type, "Maybe<", strlen("Maybe<")) == 0) return "returns null on failure";
  if (strcmp(helper->name, "std.proc.spawn") == 0) return "returns ProcStatus";
  return "infallible";
}

static const char *helper_ownership_notes(const ZStdHelperInfo *helper) {
  if (!helper) return "unknown";
  if (helper->return_type && strstr(helper->return_type, "owned<File>")) return "returns owned file handle; close or deterministic cleanup";
  if (helper->return_type && strstr(helper->return_type, "owned<ByteBuf>")) return "returns owned byte buffer backed by the explicit allocator";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "caller")) return "borrows or writes caller-owned storage";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "allocator")) return "uses explicit allocator state only";
  if (helper->allocation_behavior && strstr(helper->allocation_behavior, "borrows")) return helper->allocation_behavior;
  return "no ownership transfer";
}

static const char *helper_example_path(const ZStdHelperInfo *helper) {
  const char *module = helper_module_name(helper);
  if (strcmp(module, "std.mem") == 0) return "examples/memory-primitives.0";
  if (strcmp(module, "std.io") == 0 || strcmp(module, "std.path") == 0) return "examples/std-path-io.0";
  if (strcmp(module, "std.args") == 0 || strcmp(module, "std.env") == 0) return "examples/cli-file.0";
  if (strcmp(module, "std.fs") == 0) return "examples/zero-hash/";
  if (strcmp(module, "std.codec") == 0 || strcmp(module, "std.json") == 0) return "examples/std-data-formats.0";
  if (strcmp(module, "std.parse") == 0) return "examples/parse-cursor.0";
  if (strcmp(module, "std.time") == 0 || strcmp(module, "std.rand") == 0 || strcmp(module, "std.proc") == 0 || strcmp(module, "std.crypto") == 0) return "examples/std-platform.0";
  if (strcmp(module, "std.net") == 0) return "conformance/native/pass/std-net-http-breadth.0";
  if (strcmp(module, "std.http") == 0) return "conformance/native/pass/std-http-fetch.0";
  return "examples/README.md";
}

static void append_std_helper_object_json(ZBuf *buf, const ZStdHelperInfo *helper, bool include_estimate) {
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, helper->name);
  zbuf_append(buf, ",\"module\":");
  append_json_string(buf, helper_module_name(helper));
  zbuf_append(buf, ",\"returnType\":");
  append_json_string(buf, helper->return_type);
  zbuf_appendf(buf, ",\"args\":%d,\"capability\":", helper->arg_count);
  append_json_string(buf, helper->capability);
  zbuf_append(buf, ",\"effects\":");
  if (strcmp(helper->capability, "none") == 0) {
    zbuf_append(buf, "[]");
  } else {
    zbuf_append(buf, "[");
    append_json_string(buf, helper->capability);
    zbuf_append(buf, "]");
  }
  zbuf_append(buf, ",\"targetSupport\":");
  append_json_string(buf, helper->target_support);
  zbuf_append(buf, ",\"allocationBehavior\":");
  append_json_string(buf, helper->allocation_behavior);
  zbuf_append(buf, ",\"errorBehavior\":");
  append_json_string(buf, helper_error_behavior(helper));
  zbuf_append(buf, ",\"ownershipNotes\":");
  append_json_string(buf, helper_ownership_notes(helper));
  zbuf_append(buf, ",\"example\":");
  append_json_string(buf, helper_example_path(helper));
  zbuf_append(buf, ",\"apiStability\":\"bootstrap-stable\"");
  zbuf_appendf(buf, ",\"emitsRuntimeHelper\":%s", helper->emits_runtime_helper ? "true" : "false");
  if (include_estimate) zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d", helper_estimated_direct_bytes(helper));
  zbuf_append(buf, "}");
}

static void append_used_stdlib_helpers_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
    if (!helpers || !helpers->used[i]) continue;
    if (wrote) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[i], true);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_top_emitted_helpers_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool emitted[STD_HELPER_MAX] = {0};
  bool wrote = false;
  for (int rank = 0; rank < 5; rank++) {
    int best_index = -1;
    int best_bytes = -1;
    for (size_t i = 0; z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
      if (!helpers || !helpers->used[i] || emitted[i] || !z_std_helpers[i].emits_runtime_helper) continue;
      int bytes = helper_estimated_direct_bytes(&z_std_helpers[i]);
      if (bytes > best_bytes) {
        best_bytes = bytes;
        best_index = (int)i;
      }
    }
    if (best_index < 0) break;
    emitted[best_index] = true;
    if (wrote) zbuf_append(buf, ", ");
    append_std_helper_object_json(buf, &z_std_helpers[best_index], true);
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static bool program_uses_bounds_checked_access(const Program *program);
static void append_runtime_shims_json_ex(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps, bool direct_bounds_checks);

static size_t estimated_function_bytes(const Function *fun) {
  if (!fun) return 0;
  return 48 + fun->params.len * 12 + fun->body.len * 32 + (fun->raises ? 16 : 0);
}

static const char *function_retention_reason(const Function *fun) {
  if (!fun) return "unknown";
  if (fun->is_test) return "zero test discovery";
  if (fun->export_c) return "exported C ABI";
  if (fun->name && strcmp(fun->name, "main") == 0) return "entry point";
  if (fun->is_public) return "public API";
  return "reachable from checked module";
}

static size_t profile_debug_metadata_bytes(const Program *program, const char *profile) {
  if (!profile_debug_info(profile)) return 0;
  size_t functions = program ? program->functions.len : 0;
  return 64 + functions * 24;
}

static void append_size_functions_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, fun->is_test ? (fun->test_name ? fun->test_name : "test") : fun->name);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, fun->is_test ? "test" : "function");
    zbuf_appendf(buf, ",\"public\":%s,\"exportC\":%s,\"raises\":%s,\"estimatedDirectBytes\":%zu,\"retained\":true,\"retainedBy\":",
                 fun->is_public ? "true" : "false",
                 fun->export_c ? "true" : "false",
                 fun->raises ? "true" : "false",
                 estimated_function_bytes(fun));
    append_json_string(buf, function_retention_reason(fun));
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_size_sections_json(ZBuf *buf, const SourceInput *input, const Program *program, const char *profile, long long artifact_bytes) {
  size_t function_bytes = 0;
  for (size_t i = 0; program && i < program->functions.len; i++) function_bytes += estimated_function_bytes(&program->functions.items[i]);
  size_t readonly = input ? input->direct_readonly_data_bytes : 0;
  size_t stack = input ? input->direct_stack_bytes : 0;
  size_t helper_bytes = 0;
  if (input) {
    helper_bytes += input->direct_runtime_helper_count * 96;
    helper_bytes += input->direct_allocator_helper_count * 128;
    helper_bytes += input->direct_buffer_helper_count * 112;
  }
  size_t debug_bytes = profile_debug_metadata_bytes(program, profile);
  zbuf_append(buf, "[");
  zbuf_appendf(buf, "{\"name\":\"text\",\"kind\":\"code\",\"bytes\":%zu,\"retainedBy\":\"retained functions and direct runtime helpers\"}", function_bytes + helper_bytes);
  zbuf_appendf(buf, ", {\"name\":\"rodata\",\"kind\":\"literals\",\"bytes\":%zu,\"retainedBy\":\"string and byte literals referenced by code\"}", readonly);
  zbuf_appendf(buf, ", {\"name\":\"stack\",\"kind\":\"memory\",\"bytes\":%zu,\"retainedBy\":\"fixed locals and direct stack layout\"}", stack);
  zbuf_appendf(buf, ", {\"name\":\"debug-metadata\",\"kind\":\"debug\",\"bytes\":%zu,\"retainedBy\":", debug_bytes);
  append_json_string(buf, debug_bytes ? "profile keeps debug metadata" : "profile strips unrequested debug metadata");
  zbuf_append(buf, "}");
  if (artifact_bytes >= 0) zbuf_appendf(buf, ", {\"name\":\"artifact\",\"kind\":\"output\",\"bytes\":%lld,\"retainedBy\":\"written output artifact\"}", artifact_bytes);
  zbuf_append(buf, "]");
}

static void append_size_literals_json(ZBuf *buf, const SourceInput *input) {
  size_t readonly = input ? input->direct_readonly_data_bytes : 0;
  zbuf_append(buf, "{\"readonlyDataBytes\":");
  zbuf_appendf(buf, "%zu", readonly);
  zbuf_append(buf, ",\"items\":[");
  if (readonly > 0) {
    zbuf_append(buf, "{\"kind\":\"string-or-byte-literal\",\"bytes\":");
    zbuf_appendf(buf, "%zu", readonly);
    zbuf_append(buf, ",\"retainedBy\":\"referenced by retained function bodies\"}");
  }
  zbuf_append(buf, "]}");
}

static void append_size_helper_breakdown_json(ZBuf *buf, const HelperUseSummary *helpers) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
    if (!helpers || !helpers->used[i]) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, z_std_helpers[i].name);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, helper_module_name(&z_std_helpers[i]));
    zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d,\"emitsRuntimeHelper\":%s,\"retainedBy\":\"source helper call\",\"capability\":",
                 helper_estimated_direct_bytes(&z_std_helpers[i]),
                 z_std_helpers[i].emits_runtime_helper ? "true" : "false");
    append_json_string(buf, z_std_helpers[i].capability);
    zbuf_append(buf, "}");
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_size_imports_json(ZBuf *buf, const CapabilitySummary *caps) {
  typedef struct { const char *name; bool enabled; const char *reason; } ImportFact;
  const ImportFact facts[] = {
    {"world.out", caps && caps->world, "World capability requested by entry point"},
    {"std.args", caps && caps->args, "argument capability helper used"},
    {"std.env", caps && caps->env, "environment capability helper used"},
    {"std.fs", caps && caps->fs, "filesystem helper used"},
    {"std.net", caps && caps->net, "network helper used"},
    {"std.proc", caps && caps->proc, "process helper used"},
    {NULL, false, NULL},
  };
  zbuf_append(buf, "[");
  bool wrote = false;
  for (int i = 0; facts[i].name; i++) {
    if (!facts[i].enabled) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, facts[i].name);
    zbuf_append(buf, ",\"retainedBy\":");
    append_json_string(buf, facts[i].reason);
    zbuf_append(buf, "}");
    wrote = true;
  }
  zbuf_append(buf, "]");
}

static void append_retention_reasons_json(ZBuf *buf, const SourceInput *input, const Program *program, const HelperUseSummary *helpers, const char *profile) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; program && i < program->functions.len; i++) {
    const Function *fun = &program->functions.items[i];
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"function\",\"name\":");
    append_json_string(buf, fun->is_test ? (fun->test_name ? fun->test_name : "test") : fun->name);
    zbuf_append(buf, ",\"reason\":");
    append_json_string(buf, function_retention_reason(fun));
    zbuf_append(buf, "}");
    wrote = true;
  }
  for (size_t i = 0; z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
    if (!helpers || !helpers->used[i]) continue;
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"stdlibHelper\",\"name\":");
    append_json_string(buf, z_std_helpers[i].name);
    zbuf_append(buf, ",\"reason\":\"source helper call\",\"estimatedDirectBytes\":");
    zbuf_appendf(buf, "%d}", helper_estimated_direct_bytes(&z_std_helpers[i]));
    wrote = true;
  }
  if (input && input->direct_readonly_data_bytes > 0) {
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"literal\",\"name\":\"readonly-data\",\"reason\":\"referenced by retained code\",\"bytes\":");
    zbuf_appendf(buf, "%zu}", input->direct_readonly_data_bytes);
    wrote = true;
  }
  if (profile_debug_metadata_bytes(program, profile) > 0) {
    if (wrote) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"kind\":\"debugMetadata\",\"name\":\"profile-debug-metadata\",\"reason\":\"profile keeps debug or audit metadata\",\"bytes\":");
    zbuf_appendf(buf, "%zu}", profile_debug_metadata_bytes(program, profile));
  }
  zbuf_append(buf, "]");
}

static void append_optimization_hints_json(ZBuf *buf, const SourceInput *input, const HelperUseSummary *helpers, const CapabilitySummary *caps, const char *profile) {
  zbuf_append(buf, "[");
  bool wrote = false;
#define APPEND_HINT(ID, MESSAGE) do { \
  if (wrote) zbuf_append(buf, ", "); \
  zbuf_append(buf, "{\"id\":"); \
  append_json_string(buf, ID); \
  zbuf_append(buf, ",\"message\":"); \
  append_json_string(buf, MESSAGE); \
  zbuf_append(buf, "}"); \
  wrote = true; \
} while (0)
  if (profile_debug_info(profile)) APPEND_HINT("profile-debug-metadata", "debug/dev/audit profiles retain extra metadata; use --profile small or --profile tiny for size work");
  if (caps && caps->fs) APPEND_HINT("hosted-fs-runtime", "std.fs retains hosted filesystem capability shims; move file I/O out of target-neutral paths for smaller cross artifacts");
  if (input && input->direct_readonly_data_bytes > 0) APPEND_HINT("readonly-literals", "string and byte literals are retained by referenced code; inspect literal-heavy output before optimizing code");
  if (helpers) {
    for (size_t i = 0; z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
      if (helpers->used[i] && z_std_helpers[i].emits_runtime_helper) {
        APPEND_HINT("pay-as-used-helper", "topLargestEmittedHelpers identifies the retained stdlib helper budget");
        break;
      }
    }
  }
  if (!wrote) APPEND_HINT("no-action", "no size-specific hints for this direct subset");
#undef APPEND_HINT
  zbuf_append(buf, "]");
}

static size_t helper_count_used(const HelperUseSummary *helpers) {
  size_t count = 0;
  for (size_t i = 0; helpers && z_std_helpers[i].name && i < STD_HELPER_MAX; i++) {
    if (helpers->used[i]) count++;
  }
  return count;
}

static void append_size_breakdown_json(ZBuf *buf, const SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command, const HelperUseSummary *helpers, const CapabilitySummary *caps, long long artifact_bytes) {
  const char *profile = command && command->profile ? command->profile : "release";
  zbuf_append(buf, "{\"schemaVersion\":1,\"profile\":");
  append_json_string(buf, profile);
  zbuf_append(buf, ",\"profileKey\":");
  append_json_string(buf, profile_key_name(profile));
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target ? target->name : z_host_target());
  zbuf_appendf(buf, ",\"summary\":{\"functionCount\":%zu,\"stdlibHelperCount\":%zu,\"runtimeImportCount\":%d,\"debugMetadataBytes\":%zu},\"functions\":",
               program ? program->functions.len : 0,
               helpers ? helper_count_used(helpers) : 0,
               caps ? ((caps->world ? 1 : 0) + (caps->args ? 1 : 0) + (caps->env ? 1 : 0) + (caps->fs ? 1 : 0) + (caps->net ? 1 : 0) + (caps->proc ? 1 : 0)) : 0,
               profile_debug_metadata_bytes(program, profile));
  append_size_functions_json(buf, program);
  zbuf_append(buf, ",\"sections\":");
  append_size_sections_json(buf, input, program, profile, artifact_bytes);
  zbuf_append(buf, ",\"literals\":");
  append_size_literals_json(buf, input);
  zbuf_append(buf, ",\"stdlibHelpers\":");
  append_size_helper_breakdown_json(buf, helpers);
  zbuf_append(buf, ",\"imports\":");
  append_size_imports_json(buf, caps);
  zbuf_append(buf, ",\"runtimeShims\":");
  append_runtime_shims_json_ex(buf, NULL, caps, program_uses_bounds_checked_access(program));
  zbuf_append(buf, ",\"debugMetadata\":{\"bytes\":");
  zbuf_appendf(buf, "%zu", profile_debug_metadata_bytes(program, profile));
  zbuf_append(buf, ",\"policy\":");
  append_json_string(buf, profile_runtime_metadata(profile));
  zbuf_append(buf, ",\"retainedBy\":");
  append_json_string(buf, profile_debug_info(profile) ? "profile metadata policy" : "stripped by profile");
  zbuf_append(buf, "}}");
}

static void append_compiler_runtime_helpers_json_ex(ZBuf *buf, const char *emitted_symbol_text, bool seed_compiler_runtime) {
  zbuf_append(buf, "[");
  for (size_t i = 0; compiler_runtime_helpers[i].name; i++) {
    const CompilerRuntimeHelperInfo *helper = &compiler_runtime_helpers[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, helper->name);
    zbuf_append(buf, ",\"symbol\":");
    append_json_string(buf, helper->symbol);
    zbuf_append(buf, ",\"category\":");
    append_json_string(buf, helper->category);
    zbuf_appendf(buf, ",\"estimatedDirectBytes\":%d,\"emitted\":%s,\"payAsUsed\":true}",
                 helper->estimated_direct_bytes,
                 (emitted_symbol_text && strstr(emitted_symbol_text, helper->symbol)) || seed_compiler_runtime ? "true" : "false");
  }
  zbuf_append(buf, "]");
}

typedef struct {
  char names[128][192];
  size_t len;
  bool wrote;
} DirectGenericJsonState;

typedef struct {
  const char *name;
  char *type;
  bool is_static;
} DirectGenericBinding;

static bool append_direct_generic_specialization_item(ZBuf *buf, DirectGenericJsonState *state, const char *name) {
  if (!buf || !state || !name || !name[0]) return false;
  for (size_t i = 0; i < state->len; i++) {
    if (strcmp(state->names[i], name) == 0) return false;
  }
  if (state->len >= sizeof(state->names) / sizeof(state->names[0])) return false;
  snprintf(state->names[state->len++], sizeof(state->names[0]), "%s", name);
  if (state->wrote) zbuf_append(buf, ", ");
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, name);
  zbuf_append(buf, ",\"kind\":\"generic-specialization\",\"estimatedDirectBytes\":128,\"source\":\"direct-size-metadata\"}");
  state->wrote = true;
  return true;
}

static char *direct_trim_copy(const char *text) {
  if (!text) return z_strdup("");
  const char *start = text;
  while (*start && isspace((unsigned char)*start)) start++;
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static const char *direct_resolve_alias_type(const Program *program, const char *type) {
  if (!program || !type) return type;
  const char *current = type;
  for (size_t depth = 0; depth < program->aliases.len; depth++) {
    const TypeAlias *next = NULL;
    for (size_t i = 0; i < program->aliases.len; i++) {
      if (strcmp(program->aliases.items[i].name, current) == 0) {
        next = &program->aliases.items[i];
        break;
      }
    }
    if (!next || !next->target) return current;
    current = next->target;
  }
  return current;
}

static char *direct_resolved_type_copy(const Program *program, const char *type) {
  char *trimmed = direct_trim_copy(type);
  const char *resolved = direct_resolve_alias_type(program, trimmed);
  if (resolved == trimmed) return trimmed;
  char *copy = direct_trim_copy(resolved);
  free(trimmed);
  return copy;
}

static bool direct_split_generic_args(const char *inner, size_t inner_len, char ***out_items, size_t *out_len) {
  char **items = NULL;
  size_t len = 0;
  size_t cap = 0;
  size_t start = 0;
  int depth = 0;
  for (size_t i = 0; i <= inner_len; i++) {
    char c = i < inner_len ? inner[i] : ',';
    if (c == '<') depth++;
    else if (c == '>') depth--;
    if ((c == ',' && depth == 0) || i == inner_len) {
      size_t end = i;
      while (start < end && isspace((unsigned char)inner[start])) start++;
      while (end > start && isspace((unsigned char)inner[end - 1])) end--;
      if (end == start) {
        for (size_t j = 0; j < len; j++) free(items[j]);
        free(items);
        return false;
      }
      if (len + 1 > cap) {
        cap = z_grow_capacity(cap, len + 1, 4);
        items = z_checked_reallocarray(items, cap, sizeof(char *));
      }
      items[len++] = z_strndup(inner + start, end - start);
      start = i + 1;
    }
    if (depth < 0) {
      for (size_t j = 0; j < len; j++) free(items[j]);
      free(items);
      return false;
    }
  }
  if (depth != 0 || len == 0) {
    for (size_t j = 0; j < len; j++) free(items[j]);
    free(items);
    return false;
  }
  *out_items = items;
  *out_len = len;
  return true;
}

static bool direct_type_generic_arg_list(const char *type, const char *name, char ***out_items, size_t *out_len) {
  if (!type || !name) return false;
  size_t name_len = strlen(name);
  size_t type_len = strlen(type);
  if (type_len <= name_len + 2 || strncmp(type, name, name_len) != 0 || type[name_len] != '<' || type[type_len - 1] != '>') return false;
  return direct_split_generic_args(type + name_len + 1, type_len - name_len - 2, out_items, out_len);
}

static void direct_free_type_arg_list(char **items, size_t len) {
  for (size_t i = 0; i < len; i++) free(items[i]);
  free(items);
}

static bool direct_parse_static_uint_text(const char *text, unsigned long long *out) {
  if (!text || !text[0]) return false;
  size_t text_len = strlen(text);
  size_t body_len = text_len;
  const char *last_underscore = strrchr(text, '_');
  if (last_underscore && isalpha((unsigned char)last_underscore[1])) body_len = (size_t)(last_underscore - text);
  if (body_len == 0) return false;
  unsigned radix = 10;
  size_t index = 0;
  if (body_len > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    radix = 16;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
    radix = 2;
    index = 2;
  } else if (body_len > 2 && text[0] == '0' && (text[1] == 'o' || text[1] == 'O')) {
    radix = 8;
    index = 2;
  }
  unsigned long long value = 0;
  bool saw_digit = false;
  for (; index < body_len; index++) {
    char ch = text[index];
    if (ch == '_') continue;
    int digit = -1;
    if (ch >= '0' && ch <= '9') digit = ch - '0';
    else if (ch >= 'a' && ch <= 'f') digit = 10 + (ch - 'a');
    else if (ch >= 'A' && ch <= 'F') digit = 10 + (ch - 'A');
    if (digit < 0 || (unsigned)digit >= radix) return false;
    if (value > (ULLONG_MAX - (unsigned)digit) / radix) return false;
    value = value * radix + (unsigned)digit;
    saw_digit = true;
  }
  if (!saw_digit) return false;
  if (out) *out = value;
  return true;
}

static bool direct_eval_static_const_expr(const Program *program, const Expr *expr, unsigned long long *out, size_t depth) {
  if (!expr || depth > 32) return false;
  switch (expr->kind) {
    case EXPR_NUMBER:
      return direct_parse_static_uint_text(expr->text, out);
    case EXPR_IDENT:
      for (size_t i = 0; program && i < program->consts.len; i++) {
        if (strcmp(program->consts.items[i].name, expr->text) == 0) {
          return direct_eval_static_const_expr(program, program->consts.items[i].expr, out, depth + 1);
        }
      }
      return false;
    case EXPR_BINARY: {
      unsigned long long left = 0;
      unsigned long long right = 0;
      if (!direct_eval_static_const_expr(program, expr->left, &left, depth + 1) ||
          !direct_eval_static_const_expr(program, expr->right, &right, depth + 1)) return false;
      if (strcmp(expr->text, "+") == 0) *out = left + right;
      else if (strcmp(expr->text, "-") == 0) {
        if (right > left) return false;
        *out = left - right;
      } else if (strcmp(expr->text, "*") == 0) *out = left * right;
      else if (strcmp(expr->text, "/") == 0) {
        if (right == 0) return false;
        *out = left / right;
      } else if (strcmp(expr->text, "%") == 0) {
        if (right == 0) return false;
        *out = left % right;
      } else return false;
      return true;
    }
    default:
      return false;
  }
}

static char *direct_canonical_static_arg(const Program *program, const char *text) {
  unsigned long long value = 0;
  if (direct_parse_static_uint_text(text, &value)) {
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_appendf(&buf, "%llu", value);
    return buf.data;
  }
  for (size_t i = 0; program && i < program->consts.len; i++) {
    if (strcmp(program->consts.items[i].name, text) == 0 &&
        direct_eval_static_const_expr(program, program->consts.items[i].expr, &value, 0)) {
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_appendf(&buf, "%llu", value);
      return buf.data;
    }
  }
  return NULL;
}

static const char *direct_lookup_binding(const DirectGenericBinding *bindings, size_t binding_len, const char *name) {
  if (!name) return NULL;
  for (size_t i = 0; i < binding_len; i++) {
    if (bindings[i].name && bindings[i].type && strcmp(bindings[i].name, name) == 0) return bindings[i].type;
  }
  return NULL;
}

static int direct_binding_index(DirectGenericBinding *bindings, size_t binding_len, const char *name) {
  if (!name) return -1;
  for (size_t i = 0; i < binding_len; i++) {
    if (bindings[i].name && strcmp(bindings[i].name, name) == 0) return (int)i;
  }
  return -1;
}

static void direct_set_binding(const Program *program, DirectGenericBinding *bindings, size_t binding_len, const char *name, const char *type) {
  int index = direct_binding_index(bindings, binding_len, name);
  if (index < 0 || !type || !type[0]) return;
  char *resolved = bindings[index].is_static ? direct_canonical_static_arg(program, type) : NULL;
  if (!resolved) resolved = direct_resolved_type_copy(program, type);
  if (!bindings[index].type) {
    bindings[index].type = resolved;
    return;
  }
  if (strcmp(bindings[index].type, resolved) == 0) {
    free(resolved);
    return;
  }
  free(resolved);
}

static char *direct_substitute_type(const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len) {
  char *base = direct_resolved_type_copy(program, type);
  const char *bound = direct_lookup_binding(bindings, binding_len, base);
  if (bound) {
    free(base);
    return z_strdup(bound);
  }
  if (strncmp(base, "const ", strlen("const ")) == 0) {
    char *inner = direct_substitute_type(program, base + strlen("const "), bindings, binding_len);
    ZBuf buf;
    zbuf_init(&buf);
    zbuf_append(&buf, "const ");
    zbuf_append(&buf, inner);
    free(inner);
    free(base);
    return buf.data;
  }
  if (base[0] == '[') {
    const char *close = strchr(base, ']');
    if (close && close[1]) {
      char *length = z_strndup(base + 1, (size_t)(close - base - 1));
      const char *bound_len = direct_lookup_binding(bindings, binding_len, length);
      char *inner = direct_substitute_type(program, close + 1, bindings, binding_len);
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append_char(&buf, '[');
      zbuf_append(&buf, bound_len ? bound_len : length);
      zbuf_append_char(&buf, ']');
      zbuf_append(&buf, inner);
      free(length);
      free(inner);
      free(base);
      return buf.data;
    }
  }
  const char *open = strchr(base, '<');
  const char *close = strrchr(base, '>');
  if (open && close && close[1] == 0 && open > base) {
    char *name = z_strndup(base, (size_t)(open - base));
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(base, name, &args, &arg_len)) {
      ZBuf buf;
      zbuf_init(&buf);
      zbuf_append(&buf, name);
      zbuf_append_char(&buf, '<');
      for (size_t i = 0; i < arg_len; i++) {
        if (i > 0) zbuf_append(&buf, ",");
        char *arg = direct_substitute_type(program, args[i], bindings, binding_len);
        zbuf_append(&buf, arg);
        free(arg);
      }
      zbuf_append_char(&buf, '>');
      direct_free_type_arg_list(args, arg_len);
      free(name);
      free(base);
      return buf.data;
    }
    free(name);
  }
  return base;
}

static void direct_append_mangled_component(ZBuf *buf, const char *text) {
  if (!text || !text[0]) {
    zbuf_append(buf, "Unknown");
    return;
  }
  bool wrote = false;
  bool last_separator = false;
  for (const char *cursor = text; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    if (isalnum(ch) || ch == '_') {
      zbuf_append_char(buf, (char)ch);
      wrote = true;
      last_separator = false;
    } else if (!last_separator && wrote) {
      zbuf_append_char(buf, '_');
      last_separator = true;
    }
  }
  if (!wrote) zbuf_append(buf, "Unknown");
}

static char *direct_shape_specialized_name(const Shape *shape, char **args, size_t arg_len) {
  ZBuf name;
  zbuf_init(&name);
  zbuf_append(&name, "z_");
  direct_append_mangled_component(&name, shape ? shape->name : "Unknown");
  zbuf_append_char(&name, '_');
  for (size_t i = 0; i < arg_len; i++) {
    direct_append_mangled_component(&name, args[i]);
    zbuf_append_char(&name, '_');
  }
  return name.data;
}

static char *direct_function_specialized_name(const Function *fun, const DirectGenericBinding *bindings, size_t binding_len) {
  ZBuf name;
  zbuf_init(&name);
  zbuf_append(&name, "z_");
  direct_append_mangled_component(&name, fun ? fun->name : "unknown");
  for (size_t i = 0; i < binding_len; i++) {
    zbuf_append(&name, "__");
    direct_append_mangled_component(&name, bindings[i].type ? bindings[i].type : "Unknown");
  }
  return name.data;
}

static void direct_bind_type_pattern(const Program *program, const char *pattern, const char *actual, DirectGenericBinding *bindings, size_t binding_len) {
  char *pattern_type = direct_resolved_type_copy(program, pattern);
  char *actual_type = direct_resolved_type_copy(program, actual);
  if (!pattern_type[0] || !actual_type[0]) {
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (direct_binding_index(bindings, binding_len, pattern_type) >= 0) {
    direct_set_binding(program, bindings, binding_len, pattern_type, actual_type);
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (strncmp(pattern_type, "const ", strlen("const ")) == 0) {
    const char *actual_inner = strncmp(actual_type, "const ", strlen("const ")) == 0 ? actual_type + strlen("const ") : actual_type;
    direct_bind_type_pattern(program, pattern_type + strlen("const "), actual_inner, bindings, binding_len);
    free(pattern_type);
    free(actual_type);
    return;
  }
  if (pattern_type[0] == '[') {
    const char *pattern_close = strchr(pattern_type, ']');
    const char *actual_close = actual_type[0] == '[' ? strchr(actual_type, ']') : NULL;
    if (pattern_close && actual_close && pattern_close[1] && actual_close[1]) {
      char *pattern_len = z_strndup(pattern_type + 1, (size_t)(pattern_close - pattern_type - 1));
      char *actual_len = z_strndup(actual_type + 1, (size_t)(actual_close - actual_type - 1));
      if (direct_binding_index(bindings, binding_len, pattern_len) >= 0) {
        direct_set_binding(program, bindings, binding_len, pattern_len, actual_len);
      }
      direct_bind_type_pattern(program, pattern_close + 1, actual_close + 1, bindings, binding_len);
      free(pattern_len);
      free(actual_len);
    }
    free(pattern_type);
    free(actual_type);
    return;
  }
  const char *pattern_open = strchr(pattern_type, '<');
  const char *pattern_close = strrchr(pattern_type, '>');
  const char *actual_open = strchr(actual_type, '<');
  const char *actual_close = strrchr(actual_type, '>');
  if (pattern_open && pattern_close && pattern_close[1] == 0 &&
      actual_open && actual_close && actual_close[1] == 0 &&
      pattern_open > pattern_type && actual_open > actual_type) {
    char *pattern_name = z_strndup(pattern_type, (size_t)(pattern_open - pattern_type));
    char *actual_name = z_strndup(actual_type, (size_t)(actual_open - actual_type));
    char **pattern_args = NULL;
    char **actual_args = NULL;
    size_t pattern_arg_len = 0;
    size_t actual_arg_len = 0;
    bool same_owner = strcmp(pattern_name, actual_name) == 0;
    bool pattern_ok = direct_type_generic_arg_list(pattern_type, pattern_name, &pattern_args, &pattern_arg_len);
    bool actual_ok = direct_type_generic_arg_list(actual_type, actual_name, &actual_args, &actual_arg_len);
    if (same_owner && pattern_ok && actual_ok && pattern_arg_len == actual_arg_len) {
      for (size_t i = 0; i < pattern_arg_len; i++) {
        direct_bind_type_pattern(program, pattern_args[i], actual_args[i], bindings, binding_len);
      }
    }
    direct_free_type_arg_list(pattern_args, pattern_arg_len);
    direct_free_type_arg_list(actual_args, actual_arg_len);
    free(pattern_name);
    free(actual_name);
  }
  free(pattern_type);
  free(actual_type);
}

static void direct_free_bindings(DirectGenericBinding *bindings, size_t binding_len) {
  for (size_t i = 0; i < binding_len; i++) free(bindings[i].type);
}

static bool direct_bindings_complete(const DirectGenericBinding *bindings, size_t binding_len) {
  for (size_t i = 0; i < binding_len; i++) {
    if (!bindings[i].type || !bindings[i].type[0]) return false;
  }
  return true;
}

static const TypeArgVec *direct_call_type_args(const Expr *call) {
  if (!call) return NULL;
  if (call->checked_type_args.len > 0) return &call->checked_type_args;
  if (call->type_args.len > 0) return &call->type_args;
  if (call->kind == EXPR_CALL && call->left && call->left->checked_type_args.len > 0) return &call->left->checked_type_args;
  if (call->left && call->left->type_args.len > 0) return &call->left->type_args;
  return NULL;
}

static void direct_collect_shape_specializations_from_type(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_shape_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_shape_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len);
static void direct_collect_generic_function_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len);

static void direct_collect_shape_specializations_from_type(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const char *type, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!program || !type || !type[0]) return;
  char *resolved = direct_substitute_type(program, type, bindings, binding_len);
  for (size_t i = 0; i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    if (shape->type_params.len == 0) continue;
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(resolved, shape->name, &args, &arg_len) && arg_len == shape->type_params.len) {
      char **specialized_args = z_checked_calloc(arg_len, sizeof(char *));
      for (size_t arg_index = 0; arg_index < arg_len; arg_index++) {
        specialized_args[arg_index] = shape->type_params.items[arg_index].is_static
          ? direct_canonical_static_arg(program, args[arg_index])
          : NULL;
        if (!specialized_args[arg_index]) specialized_args[arg_index] = z_strdup(args[arg_index]);
        direct_collect_shape_specializations_from_type(buf, state, program, specialized_args[arg_index], NULL, 0);
      }
      char *name = direct_shape_specialized_name(shape, specialized_args, arg_len);
      append_direct_generic_specialization_item(buf, state, name);
      free(name);
      direct_free_type_arg_list(specialized_args, arg_len);
    }
    direct_free_type_arg_list(args, arg_len);
  }
  const char *open = strchr(resolved, '<');
  const char *close = strrchr(resolved, '>');
  if (open && close && close[1] == 0 && open > resolved) {
    char *name = z_strndup(resolved, (size_t)(open - resolved));
    char **args = NULL;
    size_t arg_len = 0;
    if (direct_type_generic_arg_list(resolved, name, &args, &arg_len)) {
      for (size_t i = 0; i < arg_len; i++) {
        direct_collect_shape_specializations_from_type(buf, state, program, args[i], NULL, 0);
      }
    }
    direct_free_type_arg_list(args, arg_len);
    free(name);
  } else if (resolved[0] == '[') {
    const char *close_bracket = strchr(resolved, ']');
    if (close_bracket && close_bracket[1]) {
      direct_collect_shape_specializations_from_type(buf, state, program, close_bracket + 1, NULL, 0);
    }
  }
  free(resolved);
}

static void direct_collect_shape_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!expr) return;
  direct_collect_shape_specializations_from_type(buf, state, program, expr->resolved_type, bindings, binding_len);
  direct_collect_shape_specializations_from_expr(buf, state, program, expr->left, bindings, binding_len);
  direct_collect_shape_specializations_from_expr(buf, state, program, expr->right, bindings, binding_len);
  for (size_t i = 0; i < expr->args.len; i++) {
    direct_collect_shape_specializations_from_expr(buf, state, program, expr->args.items[i], bindings, binding_len);
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    direct_collect_shape_specializations_from_expr(buf, state, program, expr->fields.items[i].value, bindings, binding_len);
  }
}

static void direct_collect_shape_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!body) return;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    direct_collect_shape_specializations_from_type(buf, state, program, stmt->type, bindings, binding_len);
    direct_collect_shape_specializations_from_type(buf, state, program, stmt->resolved_type, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->target, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->expr, bindings, binding_len);
    direct_collect_shape_specializations_from_expr(buf, state, program, stmt->range_end, bindings, binding_len);
    direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->then_body, bindings, binding_len);
    direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->else_body, bindings, binding_len);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      direct_collect_shape_specializations_from_expr(buf, state, program, stmt->match_arms.items[arm_index].guard, bindings, binding_len);
      direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &stmt->match_arms.items[arm_index].body, bindings, binding_len);
    }
  }
}

static void direct_collect_generic_function_specializations_from_expr(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const Expr *expr, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!expr) return;
  if (expr->kind == EXPR_CALL && expr->left && expr->left->kind == EXPR_IDENT) {
    const Function *fun = find_program_function(program, expr->left->text);
    if (fun && fun->type_params.len > 0) {
      size_t specialized_len = fun->type_params.len;
      DirectGenericBinding *specialized = z_checked_calloc(specialized_len, sizeof(DirectGenericBinding));
      for (size_t i = 0; i < specialized_len; i++) {
        specialized[i].name = fun->type_params.items[i].name;
        specialized[i].is_static = fun->type_params.items[i].is_static;
      }
      const TypeArgVec *type_args = direct_call_type_args(expr);
      if (type_args && type_args->len == specialized_len) {
        for (size_t i = 0; i < specialized_len; i++) {
          char *arg = direct_substitute_type(program, type_args->items[i].type, bindings, binding_len);
          direct_set_binding(program, specialized, specialized_len, specialized[i].name, arg);
          free(arg);
        }
      } else {
        size_t arg_len = expr->args.len < fun->params.len ? expr->args.len : fun->params.len;
        for (size_t i = 0; i < arg_len; i++) {
          char *actual = direct_substitute_type(program, expr->args.items[i]->resolved_type, bindings, binding_len);
          direct_bind_type_pattern(program, fun->params.items[i].type, actual, specialized, specialized_len);
          free(actual);
        }
      }
      if (expr->resolved_type && fun->return_type) {
        char *actual_return = direct_substitute_type(program, expr->resolved_type, bindings, binding_len);
        direct_bind_type_pattern(program, fun->return_type, actual_return, specialized, specialized_len);
        free(actual_return);
      }
      if (direct_bindings_complete(specialized, specialized_len)) {
        char *name = direct_function_specialized_name(fun, specialized, specialized_len);
        bool wrote = append_direct_generic_specialization_item(buf, state, name);
        if (wrote) {
          direct_collect_shape_specializations_from_stmt_vec(buf, state, program, &fun->body, specialized, specialized_len);
          direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &fun->body, specialized, specialized_len);
        }
        free(name);
      }
      direct_free_bindings(specialized, specialized_len);
      free(specialized);
    }
  }
  direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->left, bindings, binding_len);
  direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->right, bindings, binding_len);
  for (size_t i = 0; i < expr->args.len; i++) {
    direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->args.items[i], bindings, binding_len);
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    direct_collect_generic_function_specializations_from_expr(buf, state, program, expr->fields.items[i].value, bindings, binding_len);
  }
}

static void direct_collect_generic_function_specializations_from_stmt_vec(ZBuf *buf, DirectGenericJsonState *state, const Program *program, const StmtVec *body, const DirectGenericBinding *bindings, size_t binding_len) {
  if (!body) return;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->target, bindings, binding_len);
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->expr, bindings, binding_len);
    direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->range_end, bindings, binding_len);
    direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->then_body, bindings, binding_len);
    direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->else_body, bindings, binding_len);
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      direct_collect_generic_function_specializations_from_expr(buf, state, program, stmt->match_arms.items[arm_index].guard, bindings, binding_len);
      direct_collect_generic_function_specializations_from_stmt_vec(buf, state, program, &stmt->match_arms.items[arm_index].body, bindings, binding_len);
    }
  }
}

static void append_direct_generic_specializations_json(ZBuf *buf, const Program *program) {
  DirectGenericJsonState state = {0};
  zbuf_append(buf, "[");
  if (program) {
    for (size_t i = 0; i < program->shapes.len; i++) {
      const Shape *shape = &program->shapes.items[i];
      for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
        direct_collect_shape_specializations_from_expr(buf, &state, program, shape->fields.items[field_index].default_value, NULL, 0);
      }
    }
    for (size_t i = 0; i < program->functions.len; i++) {
      const Function *fun = &program->functions.items[i];
      if (fun->is_test) continue;
      if (fun->type_params.len == 0) {
        direct_collect_shape_specializations_from_type(buf, &state, program, fun->return_type, NULL, 0);
        for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
          direct_collect_shape_specializations_from_type(buf, &state, program, fun->params.items[param_index].type, NULL, 0);
        }
        direct_collect_shape_specializations_from_stmt_vec(buf, &state, program, &fun->body, NULL, 0);
        direct_collect_generic_function_specializations_from_stmt_vec(buf, &state, program, &fun->body, NULL, 0);
      }
    }
  }
  zbuf_append(buf, "]");
}

static bool expr_uses_bounds_checked_access(const Expr *expr) {
  if (!expr) return false;
  if (expr->kind == EXPR_INDEX || expr->kind == EXPR_SLICE) return true;
  if (expr_uses_bounds_checked_access(expr->left) || expr_uses_bounds_checked_access(expr->right)) return true;
  for (size_t i = 0; i < expr->args.len; i++) {
    if (expr_uses_bounds_checked_access(expr->args.items[i])) return true;
  }
  for (size_t i = 0; i < expr->fields.len; i++) {
    if (expr_uses_bounds_checked_access(expr->fields.items[i].value)) return true;
  }
  return false;
}

static bool stmt_vec_uses_bounds_checked_access(const StmtVec *body) {
  if (!body) return false;
  for (size_t i = 0; i < body->len; i++) {
    Stmt *stmt = body->items[i];
    if (expr_uses_bounds_checked_access(stmt->target) ||
        expr_uses_bounds_checked_access(stmt->expr) ||
        expr_uses_bounds_checked_access(stmt->range_end)) return true;
    if (stmt_vec_uses_bounds_checked_access(&stmt->then_body) ||
        stmt_vec_uses_bounds_checked_access(&stmt->else_body)) return true;
    for (size_t arm_index = 0; arm_index < stmt->match_arms.len; arm_index++) {
      if (expr_uses_bounds_checked_access(stmt->match_arms.items[arm_index].guard) ||
          stmt_vec_uses_bounds_checked_access(&stmt->match_arms.items[arm_index].body)) return true;
    }
  }
  return false;
}

static bool program_uses_bounds_checked_access(const Program *program) {
  if (!program) return false;
  for (size_t i = 0; i < program->shapes.len; i++) {
    const Shape *shape = &program->shapes.items[i];
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      if (expr_uses_bounds_checked_access(shape->fields.items[field_index].default_value)) return true;
    }
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      if (stmt_vec_uses_bounds_checked_access(&shape->methods.items[method_index].body)) return true;
    }
  }
  for (size_t i = 0; i < program->functions.len; i++) {
    if (stmt_vec_uses_bounds_checked_access(&program->functions.items[i].body)) return true;
  }
  return false;
}

static void append_runtime_shims_json_ex(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps, bool direct_bounds_checks) {
  zbuf_append(buf, "[");
  bool wrote = false;
#define APPEND_SHIM(name, reason, enabled) do { \
    if (enabled) { \
      if (wrote) zbuf_append(buf, ", "); \
      zbuf_append(buf, "{\"name\":"); \
      append_json_string(buf, name); \
      zbuf_append(buf, ",\"reason\":"); \
      append_json_string(buf, reason); \
      zbuf_append(buf, ",\"payAsUsed\":true}"); \
      wrote = true; \
    } \
  } while (0)
  APPEND_SHIM("bounds-checks", "indexed and sliced access traps instead of silent memory corruption", (emitted_symbol_text && strstr(emitted_symbol_text, "z_bounds_fail")) || direct_bounds_checks);
  APPEND_SHIM("stdio-world", "World output lowers to hosted stdout/stderr calls", (caps && caps->world) || (emitted_symbol_text && strstr(emitted_symbol_text, "fputs")));
  APPEND_SHIM("hosted-fs", "host filesystem helpers are emitted only for std.fs users", caps && caps->fs);
#undef APPEND_SHIM
  zbuf_append(buf, "]");
}

static void append_runtime_shims_json(ZBuf *buf, const char *emitted_symbol_text, const CapabilitySummary *caps) {
  append_runtime_shims_json_ex(buf, emitted_symbol_text, caps, false);
}

static size_t count_text_occurrences(const char *text, const char *needle) {
  size_t count = 0;
  const char *cursor = text ? text : "";
  while ((cursor = strstr(cursor, needle)) != NULL) {
    count++;
    cursor += strlen(needle);
  }
  return count;
}

static const char *skip_c_space(const char *cursor) {
  while (cursor && *cursor && isspace((unsigned char)*cursor)) cursor++;
  return cursor;
}

static bool c_ident_char(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_';
}

static char *trim_span_copy(const char *start, const char *end) {
  if (!start || !end || end < start) return z_strdup("");
  while (start < end && isspace((unsigned char)*start)) start++;
  while (end > start && isspace((unsigned char)end[-1])) end--;
  return z_strndup(start, (size_t)(end - start));
}

static const char *last_ident_start_before(const char *start, const char *end) {
  const char *cursor = end;
  while (cursor > start && !c_ident_char(cursor[-1])) cursor--;
  const char *ident_end = cursor;
  while (cursor > start && c_ident_char(cursor[-1])) cursor--;
  return ident_end > cursor ? cursor : NULL;
}

static void append_c_params_json(ZBuf *buf, const char *start, const char *end) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *comma = cursor;
    while (comma < end && *comma != ',') comma++;
    char *param_text = trim_span_copy(cursor, comma);
    if (param_text[0] && strcmp(param_text, "void") != 0) {
      const char *param_end = param_text + strlen(param_text);
      const char *name_start = last_ident_start_before(param_text, param_end);
      if (name_start) {
        const char *name_end = name_start;
        while (*name_end && c_ident_char(*name_end)) name_end++;
        char *name = z_strndup(name_start, (size_t)(name_end - name_start));
        char *type = trim_span_copy(param_text, name_start);
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, type);
        zbuf_append(buf, "}");
        free(name);
        free(type);
      }
    }
    free(param_text);
    cursor = comma < end ? comma + 1 : end;
  }
  zbuf_append(buf, "]");
}

static void append_c_function_model_json(ZBuf *buf, const char *line) {
  const char *paren = strchr(line, '(');
  const char *close = paren ? strstr(paren, ");") : NULL;
  const char *name_start = paren ? last_ident_start_before(line, paren) : NULL;
  const char *name_end = name_start;
  while (name_end && *name_end && c_ident_char(*name_end)) name_end++;
  char *name = name_start ? z_strndup(name_start, (size_t)(name_end - name_start)) : z_strdup("");
  char *return_type = name_start ? trim_span_copy(line, name_start) : z_strdup("");
  zbuf_append(buf, "{\"name\":");
  append_json_string(buf, name);
  zbuf_append(buf, ",\"returnType\":");
  append_json_string(buf, return_type);
  zbuf_append(buf, ",\"params\":");
  append_c_params_json(buf, paren ? paren + 1 : line, close ? close : line);
  zbuf_append(buf, "}");
  free(name);
  free(return_type);
}

static void append_c_header_functions_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while (*cursor) {
    const char *line_end = strchr(cursor, '\n');
    if (!line_end) line_end = cursor + strlen(cursor);
    char *line = trim_span_copy(cursor, line_end);
    if (line[0] && line[0] != '#' && !strstr(line, "typedef") && strchr(line, '(') && strstr(line, ");")) {
      if (wrote) zbuf_append(buf, ",");
      wrote = true;
      append_c_function_model_json(buf, line);
    }
    free(line);
    cursor = *line_end ? line_end + 1 : line_end;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_constants_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "#define")) != NULL) {
    cursor += strlen("#define");
    cursor = skip_c_space(cursor);
    const char *name_start = cursor;
    while (c_ident_char(*cursor)) cursor++;
    const char *name_end = cursor;
    const char *line_end = strchr(cursor, '\n');
    if (!line_end) line_end = cursor + strlen(cursor);
    char *name = z_strndup(name_start, (size_t)(name_end - name_start));
    char *value = trim_span_copy(cursor, line_end);
    if (name[0]) {
      if (wrote) zbuf_append(buf, ",");
      wrote = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, name);
      zbuf_append(buf, ",\"value\":");
      append_json_string(buf, value);
      zbuf_append(buf, ",\"type\":\"int\"}");
    }
    free(name);
    free(value);
  }
  zbuf_append(buf, "]");
}

static void append_c_header_typedefs_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "typedef ")) != NULL) {
    const char *line_end = strchr(cursor, ';');
    if (!line_end) break;
    char *line = trim_span_copy(cursor + strlen("typedef "), line_end);
    if (strncmp(line, "struct ", 7) != 0) {
      const char *line_text_end = line + strlen(line);
      const char *name_start = last_ident_start_before(line, line_text_end);
      if (name_start) {
        const char *name_end = name_start;
        while (*name_end && c_ident_char(*name_end)) name_end++;
        char *name = z_strndup(name_start, (size_t)(name_end - name_start));
        char *target = trim_span_copy(line, name_start);
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"target\":");
        append_json_string(buf, target);
        zbuf_append(buf, "}");
        free(name);
        free(target);
      }
    }
    free(line);
    cursor = line_end + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_struct_fields_json(ZBuf *buf, const char *start, const char *end) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *semi = cursor;
    while (semi < end && *semi != ';') semi++;
    char *field = trim_span_copy(cursor, semi);
    if (field[0]) {
      const char *field_end = field + strlen(field);
      const char *name_start = last_ident_start_before(field, field_end);
      if (name_start) {
        const char *name_end = name_start;
        while (*name_end && c_ident_char(*name_end)) name_end++;
        char *name = z_strndup(name_start, (size_t)(name_end - name_start));
        char *type = trim_span_copy(field, name_start);
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, type);
        zbuf_append(buf, "}");
        free(name);
        free(type);
      }
    }
    free(field);
    cursor = semi < end ? semi + 1 : end;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_structs_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "typedef struct")) != NULL) {
    const char *tag_start = skip_c_space(cursor + strlen("typedef struct"));
    const char *tag_end = tag_start;
    while (c_ident_char(*tag_end)) tag_end++;
    const char *open = strchr(tag_end, '{');
    const char *close = open ? strchr(open, '}') : NULL;
    const char *semi = close ? strchr(close, ';') : NULL;
    if (!open || !close || !semi) break;
    char *tag = trim_span_copy(tag_start, tag_end);
    char *alias = trim_span_copy(close + 1, semi);
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, alias[0] ? alias : tag);
    zbuf_append(buf, ",\"tag\":");
    append_json_string(buf, tag);
    zbuf_append(buf, ",\"fields\":");
    append_c_struct_fields_json(buf, open + 1, close);
    zbuf_append(buf, "}");
    free(tag);
    free(alias);
    cursor = semi + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_enum_cases_json(ZBuf *buf, const char *start, const char *end) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = start;
  while (cursor && cursor < end) {
    const char *comma = cursor;
    while (comma < end && *comma != ',') comma++;
    char *item = trim_span_copy(cursor, comma);
    if (item[0]) {
      char *eq = strchr(item, '=');
      char *name = eq ? trim_span_copy(item, eq) : trim_span_copy(item, item + strlen(item));
      char *value = eq ? trim_span_copy(eq + 1, item + strlen(item)) : z_strdup("");
      if (name[0]) {
        if (wrote) zbuf_append(buf, ",");
        wrote = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, name);
        zbuf_append(buf, ",\"value\":");
        append_json_string(buf, value);
        zbuf_append(buf, "}");
      }
      free(name);
      free(value);
    }
    free(item);
    cursor = comma < end ? comma + 1 : end;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_enums_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "[");
  bool wrote = false;
  const char *cursor = header ? header : "";
  while ((cursor = strstr(cursor, "enum ")) != NULL) {
    const char *name_start = skip_c_space(cursor + strlen("enum "));
    const char *name_end = name_start;
    while (c_ident_char(*name_end)) name_end++;
    const char *open = strchr(name_end, '{');
    const char *close = open ? strchr(open, '}') : NULL;
    if (!open || !close) break;
    char *name = trim_span_copy(name_start, name_end);
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, name);
    zbuf_append(buf, ",\"backing\":\"int\",\"cases\":");
    append_c_enum_cases_json(buf, open + 1, close);
    zbuf_append(buf, "}");
    free(name);
    cursor = close + 1;
  }
  zbuf_append(buf, "]");
}

static void append_c_header_model_json(ZBuf *buf, const char *header) {
  zbuf_append(buf, "{\"functions\":");
  append_c_header_functions_json(buf, header);
  zbuf_append(buf, ",\"constants\":");
  append_c_header_constants_json(buf, header);
  zbuf_append(buf, ",\"structs\":");
  append_c_header_structs_json(buf, header);
  zbuf_append(buf, ",\"enums\":");
  append_c_header_enums_json(buf, header);
  zbuf_append(buf, ",\"typedefs\":");
  append_c_header_typedefs_json(buf, header);
  zbuf_append(buf, "}");
}

static void append_c_imports_json(ZBuf *buf, const Program *program, const ZTargetInfo *target) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->c_imports.len; i++) {
    const CImport *import = &program->c_imports.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    ZDiag read_diag = {0};
    char *header = z_read_file(import->header, &read_diag);
    uint64_t header_hash = fnv1a_text(header ? header : "");
    zbuf_append(buf, "{\"header\":");
    append_json_string(buf, import->header);
    zbuf_append(buf, ",\"alias\":");
    append_json_string(buf, import->alias);
    zbuf_appendf(buf, ",\"cacheKey\":\"%016llx-%s\"", (unsigned long long)header_hash, target && target->zig_target ? target->zig_target : "host");
    zbuf_append(buf, ",\"cache\":{\"headerHash\":\"");
    zbuf_appendf(buf, "%016llx", (unsigned long long)header_hash);
    zbuf_append(buf, "\",\"target\":");
    append_json_string(buf, target && target->name ? target->name : z_host_target());
    zbuf_append(buf, ",\"abi\":");
    append_json_string(buf, target && target->abi ? target->abi : "host");
    zbuf_appendf(buf, ",\"flagsHash\":\"%016llx\",\"sysrootFingerprint\":\"%016llx\"}", (unsigned long long)fnv1a_text(""), (unsigned long long)fnv1a_text(target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "not-required"));
    zbuf_append(buf, ",\"imports\":{\"functions\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, ");"));
    zbuf_append(buf, ",\"constants\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "#define"));
    zbuf_append(buf, ",\"structs\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "struct "));
    zbuf_append(buf, ",\"enums\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "enum "));
    zbuf_append(buf, ",\"typedefs\":");
    zbuf_appendf(buf, "%zu", count_text_occurrences(header, "typedef "));
    zbuf_append(buf, "},\"typedModel\":");
    append_c_header_model_json(buf, header);
    zbuf_appendf(buf, ",\"target\":\"%s\"}", target ? target->name : "host");
    free(header);
  }
  zbuf_append(buf, "]");
}

static bool json_array_has_entries(const char *json) {
  return json && strcmp(json, "[]") != 0 && strchr(json, '"') != NULL;
}

static bool c_lib_has_host_paths(const ZManifestCLib *lib) {
  const char *include_json = lib && lib->include_json ? lib->include_json : "";
  const char *lib_json = lib && lib->lib_json ? lib->lib_json : "";
  return strstr(include_json, "/usr/include") || strstr(include_json, "/usr/local/include") ||
         strstr(lib_json, "/usr/lib") || strstr(lib_json, "/usr/local/lib");
}

static bool c_lib_has_vendored_headers(const ZManifestCLib *lib) {
  return lib && json_array_has_entries(lib->headers_json) && json_array_has_entries(lib->include_json) && !c_lib_has_host_paths(lib);
}

static bool c_lib_has_vendored_libraries(const ZManifestCLib *lib) {
  return lib && json_array_has_entries(lib->lib_json) && !c_lib_has_host_paths(lib);
}

static bool c_lib_uses_unsafe_foreign_discovery(const ZManifestCLib *lib, const ZTargetInfo *target) {
  if (!target || z_target_is_host(target)) return false;
  if (c_lib_has_host_paths(lib)) return true;
  bool uses_pkg_config = lib && lib->pkg_config && lib->pkg_config[0];
  return uses_pkg_config && (!c_lib_has_vendored_headers(lib) || !c_lib_has_vendored_libraries(lib));
}

static void manifest_path_for_input(const SourceInput *input, char *manifest_path, size_t manifest_path_len) {
  if (!manifest_path || manifest_path_len == 0) return;
  manifest_path[0] = 0;
  if (input && input->manifest_path && input->manifest_path[0]) {
    snprintf(manifest_path, manifest_path_len, "%s", input->manifest_path);
    return;
  }
  const char *src = input && input->source_file ? input->source_file : "";
  const char *src_dir = strstr(src, "/src/");
  if (src_dir) snprintf(manifest_path, manifest_path_len, "%.*s/zero.json", (int)(src_dir - src), src);
  else snprintf(manifest_path, manifest_path_len, "zero.json");
}

static void append_c_libraries_json(ZBuf *buf, const SourceInput *input, const ZTargetInfo *target) {
  char manifest_path[512];
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path));
  ZDiag read_diag = {0};
  char *manifest = z_read_file(manifest_path, &read_diag);
  if (!manifest) {
    zbuf_append(buf, "[]");
    return;
  }
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, &read_diag)) {
    free(manifest);
    zbuf_append(buf, "[]");
    return;
  }
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; i < parsed_manifest.c_lib_count; i++) {
    ZManifestCLib *lib = &parsed_manifest.c_libs[i];
    if (wrote) zbuf_append(buf, ", ");
    wrote = true;
    bool host_leakage = c_lib_has_host_paths(lib);
    bool vendored_headers = c_lib_has_vendored_headers(lib);
    bool vendored_libraries = c_lib_has_vendored_libraries(lib);
    bool pkg_unsafe = lib->pkg_config && lib->pkg_config[0] && !z_target_is_host(target) && (!vendored_headers || !vendored_libraries);
    bool implicit_discovery = c_lib_uses_unsafe_foreign_discovery(lib, target);
    bool windows_import_ready = !(target && target->abi && strcmp(target->abi, "msvc") == 0) || strstr(lib->lib_json ? lib->lib_json : "", ".lib") != NULL;
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, lib->name);
    zbuf_append(buf, ",\"headers\":");
    zbuf_append(buf, lib->headers_json ? lib->headers_json : "[]");
    zbuf_append(buf, ",\"includePaths\":");
    zbuf_append(buf, lib->include_json ? lib->include_json : "[]");
    zbuf_append(buf, ",\"libraryPaths\":");
    zbuf_append(buf, lib->lib_json ? lib->lib_json : "[]");
    zbuf_append(buf, ",\"link\":");
    zbuf_append(buf, lib->link_json ? lib->link_json : "[]");
    zbuf_append(buf, ",\"linkMode\":");
    append_json_string(buf, lib->mode && strstr(lib->mode, "dynamic") ? "dynamic" : "static");
    zbuf_append(buf, ",\"pkgConfig\":");
    append_json_string(buf, lib->pkg_config ? lib->pkg_config : "");
    zbuf_append(buf, ",\"linkPlan\":{\"target\":");
    append_json_string(buf, target && target->name ? target->name : z_host_target());
    zbuf_append(buf, ",\"sysrootEnv\":");
    append_json_string(buf, target && z_target_requires_sysroot(target) ? z_target_sysroot_env_name(target) : "");
    zbuf_append(buf, ",\"includePaths\":");
    zbuf_append(buf, lib->include_json ? lib->include_json : "[]");
    zbuf_append(buf, ",\"libraryPaths\":");
    zbuf_append(buf, lib->lib_json ? lib->lib_json : "[]");
    zbuf_append(buf, ",\"hostDiscovery\":");
    append_json_string(buf, implicit_discovery ? "blocked" : "none");
    zbuf_appendf(buf, "},\"targetValidation\":{\"pkgConfigTargetSafe\":%s,\"vendoredHeaders\":%s,\"vendoredLibraries\":%s,\"windowsImportLibraries\":%s,\"hostHeaderLeakage\":%s,\"implicitHostDiscovery\":%s,\"status\":",
                 pkg_unsafe ? "false" : "true",
                 vendored_headers ? "true" : "false",
                 vendored_libraries ? "true" : "false",
                 windows_import_ready ? "true" : "false",
                 host_leakage ? "true" : "false",
                 implicit_discovery ? "true" : "false");
    append_json_string(buf, implicit_discovery ? "blocked" : "ok");
    zbuf_append(buf, "}}");
  }
  zbuf_append(buf, "]");
  z_free_manifest(&parsed_manifest);
  free(manifest);
}

static bool validate_c_libraries_for_target(const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  char manifest_path[512];
  manifest_path_for_input(input, manifest_path, sizeof(manifest_path));
  ZDiag read_diag = {0};
  char *manifest = z_read_file(manifest_path, &read_diag);
  if (!manifest) return true;
  ZManifest parsed_manifest = {0};
  if (!z_parse_manifest_json(manifest, &parsed_manifest, &read_diag)) {
    free(manifest);
    return true;
  }
  for (size_t i = 0; i < parsed_manifest.c_lib_count; i++) {
    ZManifestCLib *lib = &parsed_manifest.c_libs[i];
    if (c_lib_uses_unsafe_foreign_discovery(lib, target)) {
      diag->code = 8003;
      diag->path = z_strdup(manifest_path);
      diag->line = 1;
      diag->column = 1;
      diag->length = 1;
      snprintf(diag->message, sizeof(diag->message), "foreign target C dependency would use host discovery");
      snprintf(diag->expected, sizeof(diag->expected), "package-relative vendored headers/libraries or target sysroot");
      snprintf(diag->actual, sizeof(diag->actual), "c.libs.%s uses %s%s", lib->name ? lib->name : "<unknown>", c_lib_has_host_paths(lib) ? "host include/lib paths" : "", (lib->pkg_config && lib->pkg_config[0]) ? " pkg-config" : "");
      snprintf(diag->help, sizeof(diag->help), "add target-tagged vendored include/lib entries or set the target sysroot; host /usr paths and host pkg-config are not reused for foreign targets");
      z_free_manifest(&parsed_manifest);
      free(manifest);
      return false;
    }
  }
  z_free_manifest(&parsed_manifest);
  free(manifest);
  return true;
}

static bool validate_package_dependencies_for_target(const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  for (size_t i = 0; input && i < input->dependency_count; i++) {
    const SourceDependency *dep = &input->dependencies[i];
    if (package_dependency_target_compatible(dep, target)) continue;
    diag->code = 9004;
    diag->path = input->manifest_path ? input->manifest_path : input->source_file;
    diag->line = 1;
    diag->column = 1;
    diag->length = 1;
    snprintf(diag->message, sizeof(diag->message), "package dependency is not compatible with target");
    snprintf(diag->expected, sizeof(diag->expected), "dependency target list including %s", target ? target->name : z_host_target());
    snprintf(diag->actual, sizeof(diag->actual), "%s targets %s", dep->name ? dep->name : "<dependency>", dep->targets_json ? dep->targets_json : "[]");
    snprintf(diag->help, sizeof(diag->help), "choose a supported target or gate the dependency behind target-specific package metadata");
    return false;
  }
  return true;
}

static bool self_host_caps_allowed(const CapabilitySummary *caps) {
  if (!caps) return true;
  return !caps->fs && !caps->time && !caps->rand && !caps->net && !caps->proc && !caps->web;
}

static bool self_host_subset_compatible(const Program *program, const CapabilitySummary *caps) {
  bool caps_allowed = self_host_caps_allowed(caps);
  (void)program;
  return caps_allowed;
}

static void append_self_host_subset_json(ZBuf *buf, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool compatible = self_host_subset_compatible(program, caps);
  zbuf_append(buf, "{\"contractVersion\":1,\"stage\":\"native-bootstrap\"");
  zbuf_appendf(buf, ",\"compatible\":%s", compatible ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : "host");
  zbuf_append(buf, ",\"backend\":\"native-direct\"");
  zbuf_append(buf, ",\"selectedTarget\":");
  append_json_string(buf, target && target->name ? target->name : "host");
  zbuf_append(buf, ",\"allowedCapabilities\":[\"memory\",\"alloc\",\"path\",\"codec\",\"parse\",\"args\",\"env\",\"World.stdout\",\"World.stderr\"]");
  zbuf_appendf(buf, ",\"cInterop\":{\"headerImports\":%s,\"typedBindings\":%s,\"generatedCRequired\":false,\"externalCallBoundary\":\"direct-abi-audit\"}", program && program->c_imports.len > 0 ? "true" : "false", program && program->c_imports.len > 0 ? "true" : "false");
  zbuf_append(buf, ",\"blockedReasons\":[");
  bool wrote = false;
#define APPEND_BLOCKED_CAP(field, label) do { \
    if (caps && caps->field) { \
      if (wrote) zbuf_append(buf, ","); \
      append_json_string(buf, label); \
      wrote = true; \
    } \
  } while (0)
  APPEND_BLOCKED_CAP(fs, "fs");
  APPEND_BLOCKED_CAP(time, "time");
  APPEND_BLOCKED_CAP(rand, "rand");
  APPEND_BLOCKED_CAP(net, "net");
  APPEND_BLOCKED_CAP(proc, "proc");
  APPEND_BLOCKED_CAP(web, "web");
#undef APPEND_BLOCKED_CAP
  zbuf_append(buf, "],\"sourceForms\":[\"package-local-modules\",\"functions\",\"while\",\"if\",\"return\",\"fixed-arrays\",\"primitive-locals\",\"shape\",\"enum\",\"minimal-choice\",\"strings\",\"byte-spans\",\"fallibility\",\"explicit-alloc\"]");
  zbuf_append(buf, ",\"featureFacts\":{");
  zbuf_append(buf, "\"strings\":{\"status\":\"native-direct-lowered\",\"representation\":\"readonly-data-ptr-len\",\"indexing\":\"bounds-checked-u8\",\"slicing\":\"bounds-checked-byte-view\",\"dataSegments\":true}");
  zbuf_append(buf, ",\"spans\":{\"status\":\"native-direct-lowered\",\"readonlyRepresentation\":\"ptr-len\",\"mutableRepresentation\":\"ptr-len-mutspan-u8\",\"helpers\":[\"std.mem.len\",\"std.mem.eqlBytes\",\"std.mem.copy\",\"std.mem.fill\"]}");
  zbuf_append(buf, ",\"aggregates\":{\"shape\":\"staged\",\"enum\":\"staged\",\"choice\":\"staged\",\"directLowering\":false}");
  zbuf_append(buf, ",\"fallibility\":{\"status\":\"staged\",\"directLowering\":false}");
  zbuf_append(buf, ",\"containers\":{\"status\":\"staged-explicit-storage\",\"directLowering\":false}");
  zbuf_append(buf, ",\"generics\":{\"status\":\"staged-concrete-specializations-only\",\"runtimeMetadata\":false}");
  zbuf_append(buf, "},\"targetLimitations\":[\"no-host-fs\",\"no-subprocesses\",\"hosted-runtime-limited-to-world-stdio\",\"external-c-calls-require-target-library-audit\"]}");
}

static void append_self_host_routing_json(ZBuf *buf, const char *command_name, const char *emit_kind, const Program *program, const CapabilitySummary *caps, const ZTargetInfo *target) {
  bool compatible = self_host_subset_compatible(program, caps);
  (void)command_name;
  (void)emit_kind;
  (void)target;
  zbuf_append(buf, "{\"contractVersion\":1,\"subsetCompatible\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, ",\"mode\":\"native-bootstrap\"");
  zbuf_append(buf, ",\"phases\":{\"parse\":\"zero-c\",\"check\":\"zero-c\",\"lower\":\"zero-c\",\"emit\":\"zero-c\"}");
  zbuf_append(buf, ",\"removed\":{\"seedCompiler\":true,\"browserCompiler\":true,\"portableEmitter\":true}");
  zbuf_append(buf, ",\"metadata\":{\"graphJson\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, ",\"sizeJson\":");
  zbuf_append(buf, compatible ? "true" : "false");
  zbuf_append(buf, "},\"cBridge\":{\"required\":");
  zbuf_append(buf, "false");
  zbuf_append(buf, ",\"policy\":\"removed\",\"explicitDirectFallback\":\"never-c-bridge\"}}");
}

static void apply_ir_metrics_to_input(SourceInput *input, const IrProgram *ir, const ZTargetInfo *target) {
  if (!input || !ir) return;
  input->lowered_ir_bytes = ir->mir_bytes;
  input->direct_function_count = ir->direct_function_count;
  input->direct_export_count = ir->direct_export_count;
  input->direct_stack_bytes = ir->direct_stack_bytes;
  input->direct_max_frame_bytes = ir->direct_max_frame_bytes;
  if (z_direct_object_backend(target) == Z_DIRECT_BACKEND_MACHO64 ||
      z_direct_exe_backend(target) == Z_DIRECT_BACKEND_MACHO64) {
    input->direct_stack_bytes = z_macho64_stack_bytes_from_ir(ir);
    input->direct_max_frame_bytes = z_macho64_max_frame_bytes_from_ir(ir);
  }
  input->direct_readonly_data_bytes = ir->direct_readonly_data_bytes;
  input->direct_allocator_helper_count = ir->direct_allocator_helper_count;
  input->direct_buffer_helper_count = ir->direct_buffer_helper_count;
  input->direct_runtime_helper_count = ir->direct_runtime_helper_count;
  input->direct_host_runtime_import_count = ir->direct_host_runtime_import_count;
  input->direct_http_runtime_import_count = ir->direct_http_runtime_import_count;
}

static void init_lowering_backend_diag(ZDiag *diag, const SourceInput *input, const ZTargetInfo *target, const Command *command, const IrProgram *ir) {
  memset(diag, 0, sizeof(*diag));
  diag->code = (ir && strcmp(ir->mir_expected, "direct backend MIR contract") == 0) ? 4004 : 2004;
  diag->path = input ? input->source_file : NULL;
  diag->line = ir && ir->mir_line > 0 ? ir->mir_line : 1;
  diag->column = ir && ir->mir_column > 0 ? ir->mir_column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", ir && ir->mir_message[0] ? ir->mir_message : "direct backend lowering failed");
  snprintf(diag->expected, sizeof(diag->expected), "%s", z_direct_backend_expected(target));
  snprintf(diag->actual, sizeof(diag->actual), "%s", ir && ir->mir_actual[0] ? ir->mir_actual : "unsupported construct");
  snprintf(diag->help, sizeof(diag->help), "%s", z_direct_backend_help(target));
  if (ir) z_diag_set_backend_blocker(diag, &ir->backend_blocker);
  complete_backend_blocker_diag(diag, target, command, emit_kind_name(command ? command->emit : EMIT_EXE), "lower");
}

static bool target_readiness_select_emit_target(const Command *command, const SourceInput *input, const ZTargetInfo *target, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (emit == EMIT_OBJ) {
    const char *emitter = z_direct_object_emitter(target);
    if (emitter && strcmp(emitter, "none") != 0) return true;
    init_direct_backend_diag(diag, command, input, target, emit_kind, z_direct_backend_reason(target));
    return false;
  }
  if (emit == EMIT_C) {
    init_direct_backend_diag(diag, command, input, target, emit_kind, "use --emit exe or --emit obj for target readiness");
    return false;
  }
  return true;
}

static bool target_readiness_buildability_check(const Command *command, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = (emit == EMIT_EXE && ir && ir_needs_zero_runtime_object(ir)) ? "obj" : emit_kind_name(emit);
  if (z_direct_buildability_check(ir, target, emit_kind, diag)) return true;
  complete_backend_blocker_diag(diag, target, command, emit_kind, diag && diag->backend_blocker.present ? diag->backend_blocker.stage : "buildability");
  return false;
}

static bool target_readiness_select_diag(const Command *command, const SourceInput *input, const Program *program, const ZTargetInfo *target, const IrProgram *ir, ZDiag *diag) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  const char *emit_kind = emit_kind_name(emit);
  if (emit == EMIT_OBJ) return target_readiness_buildability_check(command, target, ir, diag);
  if (emit == EMIT_C) {
    init_direct_backend_diag(diag, command, input, target, emit_kind, "use --emit exe or --emit obj for target readiness");
    return false;
  }

  if (ir && ir_needs_zero_runtime_object(ir)) {
    RuntimeImportAudit audit = runtime_import_audit_from_ir(ir);
    bool needs_http_runtime = runtime_import_audit_uses_http_provider(&audit);
    ZDirectBackend object_backend = z_direct_object_backend(target);
    if (!z_direct_backend_supports_runtime_object(object_backend)) {
      init_direct_backend_diag(diag, command, input, target, emit_kind, "runtime helpers currently require the Mach-O or ELF64 object link plan");
      return false;
    }
    if (needs_http_runtime && !z_target_is_host(target)) {
      init_direct_backend_diag(diag, command, input, target, emit_kind, "HTTP runtime provider is host-only for direct executable links");
      return false;
    }
    return target_readiness_buildability_check(command, target, ir, diag);
  }

  ZDirectBackend exe_backend = z_direct_exe_backend(target);
  bool supported_exe = exe_backend != Z_DIRECT_BACKEND_NONE && z_direct_requested_backend_matches(command ? command->backend : NULL, exe_backend);
  CapabilitySummary caps = program_capabilities(program);
  bool default_direct_exe = supported_exe && (!command || !command->backend) && self_host_subset_compatible(program, &caps);
  bool requested_direct_exe = supported_exe && command && command->backend && command->backend[0];
  if (default_direct_exe || requested_direct_exe) return target_readiness_buildability_check(command, target, ir, diag);
  init_direct_backend_diag(diag, command, input, target, emit_kind, "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target");
  return false;
}

static const char *target_readiness_backend(const ZTargetInfo *target, const Command *command) {
  EmitKind emit = command ? command->emit : EMIT_EXE;
  if (emit == EMIT_OBJ) return z_direct_object_emitter(target);
  if (emit == EMIT_EXE) {
    if (command && command->backend && command->backend[0]) return command->backend;
    return z_direct_exe_emitter(target);
  }
  return "none";
}

static void append_target_readiness_diagnostic_json(ZBuf *buf, const char *path, const ZDiag *diag) {
  zbuf_append(buf, "{\"severity\":\"error\",\"code\":");
  append_json_string(buf, diag_code(diag->code));
  zbuf_append(buf, ",\"message\":");
  append_json_string(buf, diag->message);
  zbuf_append(buf, ",\"path\":");
  append_json_string(buf, diag->path ? diag->path : path);
  zbuf_appendf(buf, ",\"line\":%d,\"column\":%d,\"length\":%d", diag->line, diag->column, diag->length > 0 ? diag->length : 1);
  zbuf_append(buf, ",\"expected\":");
  append_json_string(buf, diag->expected);
  zbuf_append(buf, ",\"actual\":");
  append_json_string(buf, diag->actual);
  zbuf_append(buf, ",\"help\":");
  append_json_string(buf, diag->help);
  zbuf_append(buf, ",\"fixSafety\":");
  append_json_string(buf, diag_fix_safety(diag->code));
  zbuf_append(buf, ",\"repair\":{\"id\":");
  append_json_string(buf, diag_repair_id(diag->code));
  zbuf_append(buf, ",\"summary\":");
  append_json_string(buf, diag_repair_summary(diag->code));
  zbuf_append(buf, "}");
  if (diag->backend_blocker.present) {
    zbuf_append(buf, ",\"backendBlocker\":");
    append_backend_blocker_json(buf, &diag->backend_blocker);
  }
  zbuf_append(buf, ",\"related\":[]}");
}

static void append_target_readiness_json(ZBuf *buf, SourceInput *input, const Program *program, const ZTargetInfo *target, const Command *command) {
  ZDiag diag = {0};
  IrProgram ir = {0};
  bool ready = true;
  if (!validate_c_libraries_for_target(input, target, &diag)) {
    ready = false;
  } else {
    long long phase_started = now_ms();
    ir = z_lower_program_with_source(program, input);
    if (input) input->lower_ms = now_ms() - phase_started;
    apply_ir_metrics_to_input(input, &ir, target);
  }

  if (ready) {
    if (!target_readiness_select_emit_target(command, input, target, &diag)) {
      ready = false;
    } else if (!ir.mir_valid) {
      init_lowering_backend_diag(&diag, input, target, command, &ir);
      ready = false;
    } else if (!target_readiness_select_diag(command, input, program, target, &ir, &diag)) {
      ready = false;
    }
  }
  if (!ready && input) {
    /* C library validation points at zero.json; source mapping would erase that. */
    if (diag.code != 8003) z_map_source_diag(input, &diag);
    if (!diag.path) diag.path = input->source_file;
  }

  const char *emit_kind = emit_kind_name(command ? command->emit : EMIT_EXE);
  zbuf_append(buf, "{\"schemaVersion\":1,\"ok\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"languageOk\":true,\"buildable\":");
  zbuf_append(buf, ready ? "true" : "false");
  zbuf_append(buf, ",\"target\":");
  append_json_string(buf, target && target->name ? target->name : z_host_target());
  zbuf_append(buf, ",\"emit\":");
  append_json_string(buf, emit_kind);
  zbuf_append(buf, ",\"objectFormat\":");
  append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\"backend\":");
  append_json_string(buf, ready || !diag.backend_blocker.present ? target_readiness_backend(target, command) : diag.backend_blocker.backend);
  zbuf_append(buf, ",\"stage\":");
  append_json_string(buf, ready ? "ready" : (diag.backend_blocker.present && diag.backend_blocker.stage[0] ? diag.backend_blocker.stage : "select"));
  zbuf_append(buf, ",\"diagnostics\":[");
  if (!ready) append_target_readiness_diagnostic_json(buf, input ? input->source_file : NULL, &diag);
  zbuf_append(buf, "]}");
  z_free_ir_program(&ir);
}

static void append_release_matrix_target_support_json(ZBuf *buf) {
  const char *targets[] = {"linux-musl-x64", "linux-arm64", "darwin-arm64", "win32-x64.exe", NULL};
  zbuf_append(buf, "[");
  for (size_t i = 0; targets[i]; i++) {
    if (i > 0) zbuf_append(buf, ",");
    const ZTargetInfo *matrix_target = z_find_target(targets[i]);
    zbuf_append(buf, "{\"target\":");
    append_json_string(buf, targets[i]);
    zbuf_append(buf, ",\"directStatus\":");
    append_json_string(buf, z_direct_backend_status(matrix_target));
    zbuf_append(buf, ",\"directObjectEmitter\":");
    append_json_string(buf, z_direct_object_emitter(matrix_target));
    zbuf_append(buf, ",\"directExeEmitter\":");
    append_json_string(buf, z_direct_exe_emitter(matrix_target));
    zbuf_append(buf, ",\"fallbackPolicy\":\"explicit-direct-never-c-bridge\"}");
  }
  zbuf_append(buf, "]");
}

static size_t source_line_count(const char *source) {
  if (!source || !source[0]) return 0;
  size_t lines = 1;
  for (size_t i = 0; source[i]; i++) {
    if (source[i] == '\n' && source[i + 1]) lines++;
  }
  return lines;
}

static const char *source_path_for_parser_line(const SourceInput *input, int parser_line) {
  if (!input || parser_line <= 0) return NULL;
  size_t index = (size_t)parser_line - 1;
  if (index >= input->source_line_count) return NULL;
  return input->source_line_paths[index];
}

static int source_original_line_for_parser_line(const SourceInput *input, int parser_line) {
  if (!input || parser_line <= 0) return parser_line > 0 ? parser_line : 1;
  size_t index = (size_t)parser_line - 1;
  if (index >= input->source_line_count) return parser_line;
  return input->source_line_numbers[index] > 0 ? input->source_line_numbers[index] : parser_line;
}

static const char *module_name_for_source_path(const SourceInput *input, const char *path) {
  if (!input || !path) return (input && input->module_count == 1) ? input->module_names[0] : "main";
  for (size_t i = 0; i < input->module_count; i++) {
    if (strcmp(input->module_paths[i], path) == 0) return input->module_names[i];
  }
  return input->module_count == 1 ? input->module_names[0] : "main";
}

static bool import_module_is_stdlib(const char *module) {
  return module && strncmp(module, "std.", 4) == 0;
}

static const char *resolved_path_for_import(const SourceInput *input, const char *from, const char *to) {
  if (!input || !from || !to) return NULL;
  for (size_t i = 0; i < input->import_edge_count; i++) {
    if (strcmp(input->import_from[i], from) == 0 && strcmp(input->import_to[i], to) == 0) {
      return input->import_paths[i];
    }
  }
  return NULL;
}

static void append_source_range_json(ZBuf *buf, const char *path, int line, int column, int length) {
  int start_line = line > 0 ? line : 1;
  int start_column = column > 0 ? column : 1;
  int end_column = start_column + (length > 0 ? length : 1);
  zbuf_append(buf, "{\"path\":");
  append_json_string(buf, path ? path : "");
  zbuf_appendf(buf, ",\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"columnUnit\":\"utf8-byte\"}",
               start_line,
               start_column,
               start_line,
               end_column);
}

static void append_use_imports_json(ZBuf *buf, const SourceInput *input, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->use_imports.len; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    UseImport *item = &program->use_imports.items[i];
    const char *path = source_path_for_parser_line(input, item->line);
    const char *from = module_name_for_source_path(input, path);
    int line = source_original_line_for_parser_line(input, item->line);
    const char *kind = import_module_is_stdlib(item->module) ? "stdlib" : "package-local";
    const char *resolved_path = import_module_is_stdlib(item->module) ? NULL : resolved_path_for_import(input, from, item->module);
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, from);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, item->module);
    zbuf_append(buf, ",\"alias\":");
    append_json_string_or_null(buf, item->alias);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, kind);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, path ? path : "");
    zbuf_appendf(buf, ",\"line\":%d,\"column\":%d", line, item->column);
    zbuf_append(buf, ",\"sourceRange\":{\"path\":");
    append_json_string(buf, path ? path : "");
    zbuf_appendf(buf, ",\"start\":{\"line\":%d,\"column\":%d},\"end\":{\"line\":%d,\"column\":%d},\"columnUnit\":\"utf8-byte\"}",
                 line,
                 item->column,
                 line,
                 item->end_column > 0 ? item->end_column : item->column);
    zbuf_append(buf, ",\"resolvedPath\":");
    append_json_string_or_null(buf, resolved_path);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "]");
}

static void append_graph_json(ZBuf *buf, SourceInput *input, Program *program, const ZTargetInfo *target, const Command *command) {
  CapabilitySummary caps = program_capabilities(program);
  size_t public_count = 0;
  size_t private_count = 0;
  for (size_t i = 0; i < input->symbol_count; i++) {
    if (input->symbol_public[i]) public_count++;
    else private_count++;
  }
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n");
  zbuf_appendf(buf, "  \"sourceFile\": \"%s\",\n", input->source_file);
  zbuf_append(buf, "  \"targets\": [{\"name\":\"cli\",\"kind\":\"exe\",\"main\":");
  append_json_string(buf, input->source_file);
  zbuf_append(buf, "}],\n");
  zbuf_append(buf, "  \"package\": "); append_package_metadata_json(buf, input, target); zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"packageCache\": ");
  append_package_cache_audit_json(buf, input, target, "release");
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"targetSupport\": {\"target\":");
  append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\"hostTarget\":");
  append_json_string(buf, z_host_target());
  zbuf_appendf(buf, ",\"hosted\":%s,\"fsAvailable\":%s,\"capabilities\":", target_is_host(target) ? "true" : "false", z_target_has_capability(target, "fs") ? "true" : "false");
  CapabilitySummary target_caps = {0};
  target_caps.memory = true;
  target_caps.args = z_target_has_capability(target, "args");
  target_caps.env = z_target_has_capability(target, "env");
  target_caps.fs = z_target_has_capability(target, "fs");
  target_caps.time = z_target_has_capability(target, "time");
  target_caps.rand = z_target_has_capability(target, "rand");
  target_caps.net = z_target_has_capability(target, "net");
  target_caps.proc = z_target_has_capability(target, "proc");
  target_caps.web = z_target_has_capability(target, "web");
  append_capability_json_array(buf, &target_caps);
  zbuf_append(buf, ",\"requiredCapabilitySupport\":");
  append_target_capability_facts_json(buf, target, &caps);
  zbuf_append(buf, ",\"httpRuntime\":");
  z_append_http_runtime_json(buf, target);
  zbuf_append(buf, "},\n");
  zbuf_append(buf, "  \"targetReadiness\": "); append_target_readiness_json(buf, input, program, target, command); zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"requiresCapabilities\": ");
  append_capability_json_array(buf, &caps);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"selfHostSubset\": ");
  append_self_host_subset_json(buf, program, &caps, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"selfHostRouting\": ");
  append_self_host_routing_json(buf, "graph", NULL, program, &caps, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"compileTime\": ");
  append_compile_time_json(buf, program, input, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"releaseMatrixTargetSupport\": ");
  append_release_matrix_target_support_json(buf);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"stdlibHelpers\": ");
  append_stdlib_helpers_json(buf);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"cImports\": ");
  append_c_imports_json(buf, program, target);
  zbuf_append(buf, ",\n");
  zbuf_append(buf, "  \"cLibraries\": ");
  append_c_libraries_json(buf, input, target);
  zbuf_append(buf, ",\n");
  zbuf_appendf(buf, "  \"symbolCounts\": {\"public\": %zu, \"private\": %zu, \"total\": %zu},\n", public_count, private_count, input->symbol_count);
  zbuf_append(buf, "  \"sourceFiles\": [");
  for (size_t i = 0; i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->source_files[i]);
  }
  zbuf_append(buf, "],\n  \"sourceMaps\": [");
  for (size_t i = 0; i < input->source_file_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    char *map_source = read_optional_file(input->source_files[i]);
    const char *map_text = map_source ? map_source : ((strcmp(input->source_files[i], input->source_file) == 0 && input->source) ? input->source : input->source_files[i]);
    zbuf_append(buf, "{\"path\":");
    append_json_string(buf, input->source_files[i]);
    zbuf_appendf(buf, ",\"sourceHash\":\"%016llx\",\"lineCount\":%zu,\"columnUnit\":\"utf8-byte\"}", (unsigned long long)fnv1a_text(map_text), source_line_count(map_text));
    free(map_source);
  }
  zbuf_append(buf, "],\n  \"imports\": [");
  for (size_t i = 0; i < input->import_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    append_json_string(buf, input->imports[i]);
  }
  zbuf_append(buf, "],\n  \"useImports\": ");
  append_use_imports_json(buf, input, program);
  zbuf_append(buf, ",\n  \"modules\": [");
  for (size_t i = 0; i < input->module_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->module_names[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->module_paths[i]);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"interfaceFingerprints\": ");
  append_interface_fingerprints_json(buf, input, target);
  zbuf_append(buf, ",\n  \"importEdges\": [");
  for (size_t i = 0; i < input->import_edge_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"from\":");
    append_json_string(buf, input->import_from[i]);
    zbuf_append(buf, ",\"to\":");
    append_json_string(buf, input->import_to[i]);
    zbuf_append(buf, ",\"path\":");
    append_json_string(buf, input->import_paths[i]);
    zbuf_append(buf, ",\"sourceRange\":");
    append_source_range_json(buf,
                             input->import_source_paths ? input->import_source_paths[i] : "",
                             input->import_lines ? input->import_lines[i] : 1,
                             input->import_columns ? input->import_columns[i] : 1,
                             input->import_lengths ? input->import_lengths[i] : 1);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n");
  zbuf_append(buf, "  \"symbols\": [");
  for (size_t i = 0; i < input->symbol_count; i++) {
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, input->symbol_names[i]);
    zbuf_append(buf, ",\"module\":");
    append_json_string(buf, input->symbol_modules[i]);
    zbuf_append(buf, ",\"kind\":");
    append_json_string(buf, input->symbol_kinds ? input->symbol_kinds[i] : "unknown");
    zbuf_appendf(buf, ",\"public\":%s", input->symbol_public[i] ? "true" : "false");
    const Function *symbol_fun = (input->symbol_kinds && strcmp(input->symbol_kinds[i], "function") == 0) ? find_program_function(program, input->symbol_names[i]) : NULL;
    if (symbol_fun) {
      zbuf_append(buf, ",\"effects\":");
      append_function_effects_json(buf, symbol_fun);
      zbuf_append(buf, ",\"allocationBehavior\":");
      append_json_string(buf, function_allocation_behavior(symbol_fun));
      zbuf_append(buf, ",\"targetSupport\":");
      CapabilitySummary symbol_caps = function_capabilities(symbol_fun);
      append_target_capability_facts_json(buf, target, &symbol_caps);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, symbol_fun);
    }
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n");
  zbuf_append(buf, "  \"functions\": [");
  for (size_t i = 0; i < program->functions.len; i++) {
    Function *fun = &program->functions.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"function\",\"public\":%s,\"params\":%zu,\"typeParams\":[", fun->name, fun->is_public ? "true" : "false", fun->params.len);
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, fun->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticParams\":[");
    bool wrote_static_param = false;
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      Param *type_param = &fun->type_params.items[type_param_index];
      if (!type_param->is_static) continue;
      if (wrote_static_param) zbuf_append(buf, ",");
      wrote_static_param = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, type_param->type ? type_param->type : "usize");
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, static_param_kind_json(program, type_param->type));
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "],\"constraints\":[");
    bool wrote_constraint = false;
    for (size_t type_param_index = 0; type_param_index < fun->type_params.len; type_param_index++) {
      Param *type_param = &fun->type_params.items[type_param_index];
      if (type_param->is_static) continue;
      if (!type_param->type || strcmp(type_param->type, "Type") == 0) continue;
      if (wrote_constraint) zbuf_append(buf, ",");
      wrote_constraint = true;
      zbuf_append(buf, "{\"typeParam\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"interface\":");
      append_json_string(buf, type_param->type);
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_appendf(buf, "],\"generic\":%s,\"returnType\":\"%s\",\"raises\":%s,", fun->type_params.len > 0 ? "true" : "false", fun->return_type ? fun->return_type : "Void", fun->raises ? "true" : "false");
    append_function_error_json(buf, fun);
    zbuf_append(buf, ",\"requiresCapabilities\":");
    append_function_effects_json(buf, fun);
    zbuf_append(buf, ",\"effects\":");
    append_function_effects_json(buf, fun);
    zbuf_append(buf, ",\"allocationBehavior\":");
    append_json_string(buf, function_allocation_behavior(fun));
    zbuf_append(buf, ",\"targetSupport\":");
    CapabilitySummary fun_caps = function_capabilities(fun);
    append_target_capability_facts_json(buf, target, &fun_caps);
    zbuf_append(buf, ",\"ownership\":");
    append_function_ownership_json(buf, fun);
    zbuf_append(buf, "}");
  }
  zbuf_append(buf, "],\n  \"shapes\": [");
  for (size_t i = 0; i < program->shapes.len; i++) {
    Shape *shape = &program->shapes.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"shape\",\"public\":%s,\"layout\":\"%s\",\"typeParams\":[", shape->name, shape->is_public ? "true" : "false", shape->layout);
    for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, shape->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticParams\":[");
    bool wrote_shape_static_param = false;
    for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
      Param *type_param = &shape->type_params.items[type_param_index];
      if (!type_param->is_static) continue;
      if (wrote_shape_static_param) zbuf_append(buf, ",");
      wrote_shape_static_param = true;
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, type_param->name);
      zbuf_append(buf, ",\"type\":");
      append_json_string(buf, type_param->type ? type_param->type : "usize");
      zbuf_append(buf, ",\"kind\":");
      append_json_string(buf, static_param_kind_json(program, type_param->type));
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "],\"generic\":");
    zbuf_append(buf, shape->type_params.len > 0 ? "true" : "false");
    zbuf_append(buf, ",\"fields\":[");
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      Param *field = &shape->fields.items[field_index];
      if (field_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "{\"name\":\"%s\",\"type\":\"%s\",\"hasDefault\":%s}", field->name, field->type, field->default_value ? "true" : "false");
    }
    zbuf_append(buf, "],\"methods\":[");
    for (size_t method_index = 0; method_index < shape->methods.len; method_index++) {
      Function *method = &shape->methods.items[method_index];
      if (method_index > 0) zbuf_append(buf, ", ");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, method->name);
      zbuf_append(buf, ",\"doc\":\"\"");
      zbuf_appendf(buf, ",\"public\":%s,\"params\":%zu,\"typeParams\":[", method->is_public ? "true" : "false", method->params.len);
      for (size_t type_param_index = 0; type_param_index < method->type_params.len; type_param_index++) {
        if (type_param_index > 0) zbuf_append(buf, ",");
        append_json_string(buf, method->type_params.items[type_param_index].name);
      }
      zbuf_append(buf, "],\"staticParams\":");
      append_static_params_json(buf, program, &method->type_params);
      zbuf_append(buf, ",\"constraints\":");
      append_type_param_constraints_json(buf, &method->type_params);
      zbuf_append(buf, ",\"inheritedShapeParams\":");
      zbuf_append(buf, shape->type_params.len > 0 ? "true" : "false");
      zbuf_append(buf, ",\"shapeTypeParams\":[");
      for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
        if (type_param_index > 0) zbuf_append(buf, ",");
        append_json_string(buf, shape->type_params.items[type_param_index].name);
      }
      zbuf_append(buf, "],\"shapeStaticParams\":[");
      bool wrote_method_shape_static_param = false;
      for (size_t type_param_index = 0; type_param_index < shape->type_params.len; type_param_index++) {
        Param *type_param = &shape->type_params.items[type_param_index];
        if (!type_param->is_static) continue;
        if (wrote_method_shape_static_param) zbuf_append(buf, ",");
        wrote_method_shape_static_param = true;
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, type_param->name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, type_param->type ? type_param->type : "usize");
        zbuf_append(buf, ",\"kind\":");
        append_json_string(buf, static_param_kind_json(program, type_param->type));
        zbuf_append(buf, ",\"staticDispatch\":true}");
      }
      zbuf_append(buf, "],\"returnType\":");
      append_json_string(buf, method->return_type ? method->return_type : "Void");
      zbuf_appendf(buf, ",\"raises\":%s,", method->raises ? "true" : "false");
      append_function_error_json(buf, method);
      zbuf_append(buf, ",\"staticDispatch\":true,\"effects\":");
      append_function_effects_json(buf, method);
      zbuf_append(buf, ",\"allocationBehavior\":");
      append_json_string(buf, function_allocation_behavior(method));
      zbuf_append(buf, ",\"targetSupport\":");
      CapabilitySummary method_caps = function_capabilities(method);
      append_target_capability_facts_json(buf, target, &method_caps);
      zbuf_append(buf, ",\"ownership\":");
      append_function_ownership_json(buf, method);
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"interfaces\": [");
  for (size_t i = 0; i < program->interfaces.len; i++) {
    InterfaceDecl *interface = &program->interfaces.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"kind\":\"interface\",\"public\":%s,\"typeParams\":[", interface->name, interface->is_public ? "true" : "false");
    for (size_t type_param_index = 0; type_param_index < interface->type_params.len; type_param_index++) {
      if (type_param_index > 0) zbuf_append(buf, ",");
      append_json_string(buf, interface->type_params.items[type_param_index].name);
    }
    zbuf_append(buf, "],\"staticOnly\":true,\"methods\":[");
    for (size_t method_index = 0; method_index < interface->methods.len; method_index++) {
      Function *method = &interface->methods.items[method_index];
      if (method_index > 0) zbuf_append(buf, ", ");
      zbuf_append(buf, "{\"name\":");
      append_json_string(buf, method->name);
      zbuf_append(buf, ",\"doc\":\"\"");
      zbuf_append(buf, ",\"typeParams\":");
      append_type_param_names_json(buf, &method->type_params);
      zbuf_append(buf, ",\"staticParams\":");
      append_static_params_json(buf, program, &method->type_params);
      zbuf_append(buf, ",\"constraints\":");
      append_type_param_constraints_json(buf, &method->type_params);
      zbuf_append(buf, ",\"params\":[");
      for (size_t param_index = 0; param_index < method->params.len; param_index++) {
        Param *param = &method->params.items[param_index];
        if (param_index > 0) zbuf_append(buf, ",");
        zbuf_append(buf, "{\"name\":");
        append_json_string(buf, param->name);
        zbuf_append(buf, ",\"type\":");
        append_json_string(buf, param->type);
        zbuf_append(buf, "}");
      }
      zbuf_append(buf, "],\"returnType\":");
      append_json_string(buf, method->return_type ? method->return_type : "Void");
      zbuf_appendf(buf, ",\"raises\":%s,", method->raises ? "true" : "false");
      append_function_error_json(buf, method);
      zbuf_append(buf, ",\"staticDispatch\":true}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"aliases\": [");
  for (size_t i = 0; i < program->aliases.len; i++) {
    TypeAlias *item = &program->aliases.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, item->name);
    zbuf_append(buf, ",\"target\":");
    append_json_string(buf, item->target);
    zbuf_appendf(buf, ",\"public\":%s}", item->is_public ? "true" : "false");
  }
  zbuf_append(buf, "],\n  \"consts\": [");
  for (size_t i = 0; i < program->consts.len; i++) {
    ConstDecl *item = &program->consts.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_append(buf, "{\"name\":");
    append_json_string(buf, item->name);
    zbuf_append(buf, ",\"type\":");
    append_json_string(buf, item->type ? item->type : (item->expr ? item->expr->resolved_type : "Unknown"));
    zbuf_appendf(buf, ",\"public\":%s}", item->is_public ? "true" : "false");
  }
  zbuf_append(buf, "],\n  \"enums\": [");
  for (size_t i = 0; i < program->enums.len; i++) {
    EnumDecl *item = &program->enums.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"cases\":[", item->name);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      if (case_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "\"%s\"", item->cases.items[case_index].name);
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"choices\": [");
  for (size_t i = 0; i < program->choices.len; i++) {
    Choice *item = &program->choices.items[i];
    if (i > 0) zbuf_append(buf, ", ");
    zbuf_appendf(buf, "{\"name\":\"%s\",\"cases\":[", item->name);
    for (size_t case_index = 0; case_index < item->cases.len; case_index++) {
      if (case_index > 0) zbuf_append(buf, ", ");
      zbuf_appendf(buf, "{\"name\":\"%s\",\"type\":", item->cases.items[case_index].name);
      if (item->cases.items[case_index].type) zbuf_appendf(buf, "\"%s\"", item->cases.items[case_index].type);
      else zbuf_append(buf, "null");
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "],\n  \"compilerPhases\": ");
  append_compiler_phases_json(buf, input);
  zbuf_append(buf, ",\n  \"compilerCaches\": ");
  append_compiler_caches_json(buf, input, target, "release");
  zbuf_append(buf, ",\n  \"incrementalInvalidation\": ");
  append_incremental_invalidations_json(buf, input, target, "release");
  zbuf_append(buf, "\n}\n");
}

int main(int argc, char **argv) {
  if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0)) {
    print_help();
    return 0;
  }
  Command command = {0};
  if (!parse_command(argc, argv, &command)) {
    print_help();
    return 1;
  }
  if (command.kind && strcmp(command.kind, "help") == 0) {
    print_command_help(command.command);
    return 0;
  }
  if (command.unknown_flag) {
    fprintf(stderr, "unknown flag: %s\n", command.unknown_flag);
    fprintf(stderr, "help: run `zero %s --help`", command.command);
    if (strncmp(command.unknown_flag, "--js", 4) == 0) fprintf(stderr, " or use --json");
    fprintf(stderr, "\n");
    return 1;
  }
  if (command.invalid_emit) {
    ZDiag diag = {0};
    diag.code = 2002;
    diag.line = 1;
    diag.column = 1;
    diag.length = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown emit kind '%s'", command.invalid_emit);
    snprintf(diag.expected, sizeof(diag.expected), "one of exe, obj");
    snprintf(diag.actual, sizeof(diag.actual), "--emit %s", command.invalid_emit);
    snprintf(diag.help, sizeof(diag.help), "use --emit exe or --emit obj");
    if (command.json) print_diag_json(command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }
  if (strcmp(command.command, "--version") == 0 || strcmp(command.command, "version") == 0) {
    return print_version_command(command.json);
  }
  if (strcmp(command.command, "targets") == 0) {
    ZBuf targets;
    zbuf_init(&targets);
    z_append_targets_json(&targets);
    fputs(targets.data, stdout);
    zbuf_free(&targets);
    return 0;
  }
  if (strcmp(command.command, "new") == 0) {
    return new_command(&command);
  }
  if (strcmp(command.command, "doctor") == 0) {
    return doctor_command(command.json);
  }
  if (strcmp(command.command, "clean") == 0) {
    return clean_command(&command);
  }
  if (strcmp(command.command, "skills") == 0) {
    return embedded_skills_command(argc, argv, command.json);
  }
  if (!command.input) {
    print_command_help(command.command);
    return 1;
  }

  long long command_started_ms = now_ms();
  ZDiag diag = {0};
  const ZTargetInfo *target = z_find_target(command.target);
  if (!z_is_known_target(command.target) || !target) {
    diag.code = 6001;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "unknown target '%s'", command.target);
    snprintf(diag.expected, sizeof(diag.expected), "one of zero targets");
    snprintf(diag.actual, sizeof(diag.actual), "%s", command.target);
    snprintf(diag.help, sizeof(diag.help), "run zero targets and choose a supported target name");
    if (command.json) print_diag_json(command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  if (command.legacy_backend || command.emit == EMIT_C) {
    diag.code = 2003;
    diag.line = 1;
    diag.column = 1;
    diag.length = 1;
    snprintf(diag.message, sizeof(diag.message), "C backend output is not supported");
    snprintf(diag.expected, sizeof(diag.expected), "zero build --emit exe|obj <input>");
    snprintf(diag.actual, sizeof(diag.actual), command.legacy_backend ? "--legacy-backend" : "--emit c");
    snprintf(diag.help, sizeof(diag.help), "use direct emitters; C backend output is not a compatibility or debug path");
    if (command.json) print_diag_json(command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  if (strcmp(command.command, "run") == 0) {
    if (command.json) {
      diag.code = 2002;
      diag.line = 1;
      diag.column = 1;
      diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run does not support --json");
      snprintf(diag.expected, sizeof(diag.expected), "zero run <input>");
      snprintf(diag.actual, sizeof(diag.actual), "zero run --json");
      snprintf(diag.help, sizeof(diag.help), "program stdout belongs to the program; use zero build --json to inspect the artifact before running it");
      print_diag_json(command.input, &diag);
      return 1;
    }
    if (command.emit != EMIT_EXE) {
      diag.code = 2002;
      diag.line = 1;
      diag.column = 1;
      diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run only supports executable output");
      snprintf(diag.expected, sizeof(diag.expected), "zero run <input>");
      snprintf(diag.actual, sizeof(diag.actual), "--emit obj");
      snprintf(diag.help, sizeof(diag.help), "use zero build --emit obj when you need a non-executable artifact");
      print_diag(command.input, &diag);
      return 1;
    }
    if (!z_target_is_host(target)) {
      diag.code = 2002;
      diag.line = 1;
      diag.column = 1;
      diag.length = 1;
      snprintf(diag.message, sizeof(diag.message), "zero run requires the host target");
      snprintf(diag.expected, sizeof(diag.expected), "target %s", z_host_target());
      snprintf(diag.actual, sizeof(diag.actual), "target %s", target && target->name ? target->name : "unknown");
      snprintf(diag.help, sizeof(diag.help), "use zero build --target %s for cross-target artifacts, then run them on a matching host", target && target->name ? target->name : "<target>");
      print_diag(command.input, &diag);
      return 1;
    }
  }

  if (strcmp(command.command, "explain") == 0) {
    return explain_command(&command);
  }

  if (strcmp(command.command, "fix") == 0 && !command.plan && !command.apply && !command.patch) {
    diag.code = 2002;
    diag.line = 1;
    diag.column = 1;
    snprintf(diag.message, sizeof(diag.message), "fix requires a mode");
    snprintf(diag.expected, sizeof(diag.expected), "zero fix --plan, --patch, or --apply --json <input>");
    snprintf(diag.actual, sizeof(diag.actual), "zero fix without a mode");
    snprintf(diag.help, sizeof(diag.help), "rerun with --plan to inspect repairs, --patch to preview edits, or --apply for behavior-preserving edits");
    if (command.json) print_diag_json(command.input, &diag);
    else print_diag(command.input, &diag);
    return 1;
  }

  if (strcmp(command.command, "fmt") == 0) {
    SourceInput fmt_input = {0};
    if (!load_command_source(command.input, &fmt_input, &diag)) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!fmt_input.source) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&fmt_input);
      return 1;
    }
    diag.path = fmt_input.source_file;
    char *formatted = format_row_source_text(fmt_input.source, &diag);
    if (!formatted || diag.code != 0) {
      z_map_source_diag(&fmt_input, &diag);
      if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      free(formatted);
      z_free_source(&fmt_input);
      return 1;
    }
    if (command.fmt_check) {
      bool matches = strcmp(formatted, fmt_input.source) == 0;
      if (matches) {
        printf("fmt ok\n");
      } else {
        fprintf(stderr, "format differs: %s\n", fmt_input.source_file ? fmt_input.source_file : command.input);
      }
      free(formatted);
      z_free_source(&fmt_input);
      return matches ? 0 : 1;
    }
    fputs(formatted, stdout);
    free(formatted);
    z_free_source(&fmt_input);
    return 0;
  }

  if (strcmp(command.command, "tokens") == 0) {
    SourceInput token_input = {0};
    if (!load_command_source(command.input, &token_input, &diag)) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!token_input.source) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&token_input);
      return 1;
    }
    diag.path = token_input.source_file;
    ZRowTokenVec tokens = z_row_tokenize(token_input.source, &diag);
    if (diag.code != 0) {
      z_map_source_diag(&token_input, &diag);
      if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_row_tokens(&tokens);
      z_free_source(&token_input);
      return 1;
    }
    if (command.json) {
      ZBuf buf;
      zbuf_init(&buf);
      append_row_tokens_json(&buf, token_input.source_file, &tokens);
      fputs(buf.data, stdout);
      zbuf_free(&buf);
    } else {
      for (size_t i = 0; i < tokens.len; i++) {
        printf("%s %s %d:%d\n", row_token_kind_name(tokens.items[i].kind), tokens.items[i].text, tokens.items[i].line, tokens.items[i].column);
      }
    }
    z_free_row_tokens(&tokens);
    z_free_source(&token_input);
    return 0;
  }

  if (strcmp(command.command, "parse") == 0) {
    SourceInput parse_input = {0};
    if (!load_command_source(command.input, &parse_input, &diag)) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      return 1;
    }
    if (!parse_input.source) {
      if (command.json) print_diag_json(command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_source(&parse_input);
      return 1;
    }
    diag.path = parse_input.source_file;
    Program parsed = {0};
    parse_row_source_text(parse_input.source, &parsed, &diag);
    if (diag.code != 0) {
      z_map_source_diag(&parse_input, &diag);
      if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
      else print_diag(diag.path ? diag.path : command.input, &diag);
      z_free_program(&parsed);
      z_free_source(&parse_input);
      return 1;
    }
    if (command.json) {
      ZBuf buf;
      zbuf_init(&buf);
      append_parse_json(&buf, parse_input.source_file, &parsed);
      fputs(buf.data, stdout);
      zbuf_free(&buf);
    } else {
      printf("parse ok: %s\n", parse_input.source_file ? parse_input.source_file : command.input);
    }
    z_free_program(&parsed);
    z_free_source(&parse_input);
    return 0;
  }

  SourceInput input = {0};
  Program program = {0};
  if (!compile_input(command.input, target, &input, &program, &diag)) {
    if (strcmp(command.command, "fix") == 0) {
      if (command.apply || command.patch) {
        int rc = print_or_apply_fix_json(diag.path ? diag.path : command.input, &input, &diag, command.apply);
        z_free_program(&program);
        z_free_source(&input);
        return rc;
      }
      print_fix_plan_json(diag.path ? diag.path : command.input, &diag);
      z_free_program(&program);
      z_free_source(&input);
      return 0;
    }
    if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    z_free_program(&program);
    z_free_source(&input);
    return 1;
  }

  if (strcmp(command.command, "graph") != 0 && !validate_target_capabilities(&program, target, &diag, input.source_file)) {
    if (strcmp(command.command, "fix") == 0) {
      print_fix_plan_json(input.source_file, &diag);
      z_free_program(&program);
      z_free_source(&input);
      return 0;
    }
    if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    z_free_program(&program);
    z_free_source(&input);
    return 1;
  }

  if (strcmp(command.command, "graph") != 0 && !validate_package_dependencies_for_target(&input, target, &diag)) {
    if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    z_free_program(&program);
    z_free_source(&input);
    return 1;
  }

  if ((strcmp(command.command, "build") == 0 || strcmp(command.command, "run") == 0 || strcmp(command.command, "ship") == 0) &&
      !validate_c_libraries_for_target(&input, target, &diag)) {
    if (command.json) print_diag_json(diag.path ? diag.path : command.input, &diag);
    else print_diag(diag.path ? diag.path : command.input, &diag);
    z_free_program(&program);
    z_free_source(&input);
    return 1;
  }

  if (strcmp(command.command, "fix") == 0) {
    if (command.apply || command.patch) print_or_apply_fix_json(input.source_file, &input, NULL, command.apply);
    else print_fix_plan_json(input.source_file, NULL);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "check") == 0) {
    if (command.json) print_check_json_success(input.source_file, &input, &program, target, &command);
    else printf("ok\n");
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "doc") == 0) {
    ZBuf doc;
    zbuf_init(&doc);
    append_public_docs_json(&doc, &input, &program, target);
    fputs(doc.data, stdout);
    zbuf_free(&doc);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "dev") == 0) {
    ZBuf dev;
    zbuf_init(&dev);
    append_dev_plan_json(&dev, &input, &program, target, &command);
    fputs(dev.data, stdout);
    zbuf_free(&dev);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "time") == 0) {
    ZBuf time_json;
    zbuf_init(&time_json);
    append_time_json(&time_json, &input, &program, target, &command);
    fputs(time_json.data, stdout);
    zbuf_free(&time_json);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "abi") == 0) {
    const char *mode = command.kind ? command.kind : "check";
    if (strcmp(mode, "dump") == 0) {
      if (command.json) {
        ZBuf abi;
        zbuf_init(&abi);
        append_abi_dump_json(&abi, &input, &program, target);
        fputs(abi.data, stdout);
        zbuf_free(&abi);
      } else {
        printf("abi dump ok\n");
      }
    } else if (strcmp(mode, "check") == 0) {
      if (command.json) {
        printf("{\n  \"schemaVersion\": 1,\n  \"ok\": true,\n  \"sourceFile\": ");
        print_json_string(input.source_file);
        printf(",\n  \"target\": ");
        print_json_string(target->name);
        printf(",\n  \"diagnostics\": []\n}\n");
      } else {
        printf("abi ok\n");
      }
    } else {
      fprintf(stderr, "unknown abi mode: %s\n", mode);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  if (strcmp(command.command, "test") == 0) {
    int rc = run_tests_direct(&command, &input, &program, target);
    z_free_program(&program);
    z_free_source(&input);
    return rc;
  }

  if (strcmp(command.command, "graph") == 0) {
    ZBuf graph;
    zbuf_init(&graph);
    append_graph_json(&graph, &input, &program, target, &command);
    fputs(graph.data, stdout);
    zbuf_free(&graph);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }

  long long phase_started = now_ms();
  IrProgram ir = z_lower_program_with_source(&program, &input);
  input.lower_ms = now_ms() - phase_started;
  apply_ir_metrics_to_input(&input, &ir, target);
  if (strcmp(command.command, "mem") == 0) {
    ZBuf mem_json;
    zbuf_init(&mem_json);
    append_direct_memory_json(&mem_json, &input, &program, target, &command, &ir);
    fputs(mem_json.data, stdout);
    zbuf_free(&mem_json);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  if (command.emit == EMIT_OBJ) {
    if (strcmp(command.command, "build") != 0) {
      fprintf(stderr, "--emit obj is currently supported only by zero build\n");
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    ZDirectBackend object_backend = z_direct_object_backend(target);
    if (object_backend == Z_DIRECT_BACKEND_NONE) {
      int rc = return_direct_backend_error(&command, &input, target, "obj", z_direct_backend_reason(target), &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_buildability_preflight(&command, &input, target, "obj", &ir, &diag)) { int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc; }
    ZBuf object;
    phase_started = now_ms();
    bool emitted_object = false;
    switch (object_backend) {
      case Z_DIRECT_BACKEND_ELF64: emitted_object = z_emit_elf64_object_from_ir(&ir, &object, &diag); break;
      case Z_DIRECT_BACKEND_ELF_AARCH64: emitted_object = z_emit_elf_aarch64_object_from_ir(&ir, &object, &diag); break;
      case Z_DIRECT_BACKEND_MACHO64: emitted_object = z_emit_macho64_object_from_ir(&ir, &object, &diag); break;
      case Z_DIRECT_BACKEND_COFF_X64: emitted_object = z_emit_coff_x64_object_from_ir(&ir, &object, &diag); break;
      case Z_DIRECT_BACKEND_NONE: emitted_object = false; break;
    }
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_object) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "obj", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }
    char *base_object_file = command.out ? z_strdup(command.out) : z_default_out_path(input.source_file);
    char *object_file = base_object_file;
    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", compile_cache_key(&input, target, command.profile, z_direct_backend_artifact_path(object_backend, false)));
    bool wrote_object = z_write_binary_file(object_file, (const unsigned char *)object.data, object.len, &diag);
    input.object_ms = now_ms() - phase_started;
    input.link_ms = 0;
    if (!wrote_object) {
      print_diag(object_file, &diag);
      free(object_file);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) print_build_json(&command, &input, &program, target, "obj", object_file, file_size_or_negative(object_file), 0, elapsed_ms);
    else print_artifact(object_file, elapsed_ms);
    free(object_file);
    zbuf_free(&object);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  CapabilitySummary direct_exe_caps = program_capabilities(&program);
  bool build_command = strcmp(command.command, "build") == 0;
  bool run_command = strcmp(command.command, "run") == 0;
  bool ship_command = strcmp(command.command, "ship") == 0;
  bool artifact_command = build_command || run_command || ship_command;
  bool needs_zero_runtime = artifact_command && command.emit == EMIT_EXE && ir_needs_zero_runtime_object(&ir);
  if (needs_zero_runtime) {
    RuntimeImportAudit runtime_audit = runtime_import_audit_from_ir(&ir);
    bool needs_http_runtime = runtime_import_audit_uses_http_provider(&runtime_audit);
    ZDirectBackend object_backend = z_direct_object_backend(target);
    bool runtime_object_emitter_supported = z_direct_backend_supports_runtime_object(object_backend);
    if (ship_command) {
      int rc = return_direct_backend_error(&command, &input, target, "exe", "host runtime link plan is not wired into ship yet; use zero build or zero run", &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!runtime_object_emitter_supported) {
      int rc = return_direct_backend_error(&command, &input, target, "exe", "runtime helpers currently require the Mach-O or ELF64 object link plan", &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (needs_http_runtime && !z_target_is_host(target)) {
      int rc = return_direct_backend_error(&command, &input, target, "exe", "HTTP runtime provider is host-only for direct executable links", &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!direct_buildability_preflight(&command, &input, target, "obj", &ir, &diag)) { int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc; }
    ZBuf object;
    zbuf_init(&object);
    phase_started = now_ms();
    bool emitted_object = object_backend == Z_DIRECT_BACKEND_MACHO64
                            ? z_emit_macho64_object_from_ir(&ir, &object, &diag)
                            : z_emit_elf64_object_from_ir(&ir, &object, &diag);
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_object) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "exe", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    char *base_exe_file = command.out ? z_strdup(command.out) : z_default_out_path(input.source_file);
    char *exe_file = apply_target_suffix(base_exe_file, target);
    free(base_exe_file);
    char *object_file = path_with_suffix(exe_file, ".zero.o");
    char *runtime_object_file = path_with_suffix(exe_file, ".zero-runtime.o");
    char *http_object_file = needs_http_runtime ? path_with_suffix(exe_file, ".zero-http-curl.o") : NULL;
    ZToolchainPlan runtime_toolchain = z_plan_toolchain(command.cc, command.profile, target);

    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", compile_cache_key(&input, target, command.profile, z_direct_backend_runtime_object_cache_key(object_backend)));
    bool wrote_object = z_write_binary_file(object_file, (const unsigned char *)object.data, object.len, &diag);
    if (wrote_object) wrote_object = compile_zero_runtime_object(runtime_object_file, &runtime_toolchain, &command, target, &diag);
    if (wrote_object && needs_http_runtime) wrote_object = compile_zero_http_curl_object(http_object_file, &runtime_toolchain, &command, target, &diag);
    input.object_ms = now_ms() - phase_started;
    if (!wrote_object) {
      if (command.json) print_diag_json(diag.path ? diag.path : input.source_file, &diag);
      else print_diag(diag.path ? diag.path : input.source_file, &diag);
      free(http_object_file);
      free(runtime_object_file);
      free(object_file);
      free(exe_file);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    phase_started = now_ms();
    bool linked = link_zero_runtime_executable(object_file, runtime_object_file, http_object_file, exe_file, &runtime_toolchain, target, &diag);
    if (linked) chmod(exe_file, 0755);
    input.link_ms = now_ms() - phase_started;
    remove(object_file);
    remove(runtime_object_file);
    if (http_object_file) remove(http_object_file);
    if (!linked) {
      if (command.json) print_diag_json(diag.path ? diag.path : input.source_file, &diag);
      else print_diag(diag.path ? diag.path : input.source_file, &diag);
      free(http_object_file);
      free(runtime_object_file);
      free(object_file);
      free(exe_file);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    if (run_command) {
      int rc = run_executable_artifact(exe_file, &command);
      free(http_object_file);
      free(runtime_object_file);
      free(object_file);
      free(exe_file);
      zbuf_free(&object);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return rc;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (command.json) print_build_json(&command, &input, &program, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
    else print_artifact(exe_file, elapsed_ms);
    free(http_object_file);
    free(runtime_object_file);
    free(object_file);
    free(exe_file);
    zbuf_free(&object);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  ZDirectBackend direct_exe_backend = command.emit == EMIT_EXE ? z_direct_exe_backend(target) : Z_DIRECT_BACKEND_NONE;
  bool direct_exe_available = direct_exe_backend != Z_DIRECT_BACKEND_NONE;
  bool default_direct_exe = artifact_command && command.emit == EMIT_EXE && direct_exe_available && !command.backend && self_host_subset_compatible(&program, &direct_exe_caps);
  bool requested_direct_exe = artifact_command && command.emit == EMIT_EXE && command.backend &&
                              z_direct_backend_is_request_name(command.backend);
  if (default_direct_exe || requested_direct_exe) {
    ZDirectBackend exe_backend = direct_exe_backend;
    bool supported_exe = direct_exe_available && z_direct_requested_backend_matches(command.backend, exe_backend);
    if (!supported_exe) {
      int rc = return_direct_backend_error(&command, &input, target, "exe", "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target", &ir, &program);
      z_free_source(&input);
      return rc;
    }
    if (!command.backend) command.backend = z_direct_backend_object_emitter(exe_backend);
    if (!direct_buildability_preflight(&command, &input, target, "exe", &ir, &diag)) { int rc = return_buildability_error(&command, &input, &diag, &ir, &program); z_free_source(&input); return rc; }
    ZBuf exe;
    phase_started = now_ms();
    bool emitted_exe = false;
    switch (exe_backend) {
      case Z_DIRECT_BACKEND_ELF_AARCH64: emitted_exe = z_emit_elf_aarch64_exe_from_ir(&ir, &exe, &diag); break;
      case Z_DIRECT_BACKEND_MACHO64: emitted_exe = z_emit_macho64_exe_from_ir(&ir, &exe, &diag); break;
      case Z_DIRECT_BACKEND_COFF_X64: emitted_exe = z_emit_coff_x64_exe_from_ir(&ir, &exe, &diag); break;
      case Z_DIRECT_BACKEND_ELF64: emitted_exe = z_emit_elf64_exe_from_ir(&ir, &exe, &diag); break;
      case Z_DIRECT_BACKEND_NONE: emitted_exe = false; break;
    }
    input.codegen_ms = now_ms() - phase_started;
    if (!emitted_exe) {
      z_map_source_diag(&input, &diag);
      if (!diag.path) diag.path = input.source_file;
      complete_backend_blocker_diag(&diag, target, &command, "exe", "emit");
      if (command.json) print_diag_json(input.source_file, &diag);
      else print_diag(input.source_file, &diag);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    char *base_exe_file = command.out ? z_strdup(command.out) : (ship_command ? ship_default_out_path(input.source_file, target) : z_default_out_path(input.source_file));
    char *exe_file = apply_target_suffix(base_exe_file, target);
    free(base_exe_file);
    phase_started = now_ms();
    input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", compile_cache_key(&input, target, command.profile, z_direct_backend_artifact_path(exe_backend, true)));
    bool wrote_exe = z_write_binary_file(exe_file, (const unsigned char *)exe.data, exe.len, &diag);
    if (wrote_exe) chmod(exe_file, 0755);
    input.object_ms = now_ms() - phase_started;
    input.link_ms = 0;
    if (!wrote_exe) {
      print_diag(exe_file, &diag);
      free(exe_file);
      zbuf_free(&exe);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return 1;
    }

    if (run_command) {
      int rc = run_executable_artifact(exe_file, &command);
      free(exe_file);
      zbuf_free(&exe);
      z_free_ir_program(&ir);
      z_free_program(&program);
      z_free_source(&input);
      return rc;
    }

    long long elapsed_ms = now_ms() - command_started_ms;
    if (ship_command) {
      ShipArtifacts ship = {0};
      if (!write_ship_artifacts(&command, &input, target, &exe, exe_file, &ship, &diag)) {
        if (command.json) print_diag_json(diag.path ? diag.path : exe_file, &diag);
        else print_diag(diag.path ? diag.path : exe_file, &diag);
        ship_artifacts_free(&ship);
        free(exe_file);
        zbuf_free(&exe);
        z_free_ir_program(&ir);
        z_free_program(&program);
        z_free_source(&input);
        return 1;
      }
      if (command.json) print_ship_json(&command, &input, &program, target, &ship, elapsed_ms);
      else print_ship_text(&ship, elapsed_ms);
      ship_artifacts_free(&ship);
    } else if (command.json) {
      print_build_json(&command, &input, &program, target, "exe", exe_file, file_size_or_negative(exe_file), 0, elapsed_ms);
    } else {
      print_artifact(exe_file, elapsed_ms);
    }
    free(exe_file);
    zbuf_free(&exe);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  if (strcmp(command.command, "size") == 0) {
    CapabilitySummary caps = program_capabilities(&program);
    HelperUseSummary used_helpers = program_used_helpers(&program);
    long long artifact_bytes = -1;
    char *artifact_path = NULL;
    if (command.out) {
      char *base_artifact_path = z_strdup(command.out);
      artifact_path = apply_target_suffix(base_artifact_path, target);
      free(base_artifact_path);
      ZBuf artifact;
      zbuf_init(&artifact);
      zbuf_append(&artifact, "{\n  \"schemaVersion\": 1,\n  \"kind\": \"zero-size-metadata\",\n  \"sourceFile\": ");
      append_json_string(&artifact, input.source_file);
      zbuf_append(&artifact, ",\n  \"target\": ");
      append_json_string(&artifact, target ? target->name : z_host_target());
      zbuf_appendf(&artifact, ",\n  \"generatedCBytes\": 0,\n  \"loweredIrBytes\": %zu\n}\n", input.lowered_ir_bytes);
      phase_started = now_ms();
      input.emitted_object_cache_hit = compiler_cache_touch("emitted-object", compile_cache_key(&input, target, command.profile, "direct-size-metadata"));
      if (!z_write_file(artifact_path, artifact.data, &diag)) {
        print_diag(artifact_path, &diag);
        zbuf_free(&artifact);
        free(artifact_path);
        z_free_ir_program(&ir);
        z_free_program(&program);
        z_free_source(&input);
        return 1;
      }
      input.object_ms = now_ms() - phase_started;
      input.link_ms = 0;
      artifact_bytes = file_size_or_negative(artifact_path);
      zbuf_free(&artifact);
    }

    printf("{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
    print_json_string(input.source_file);
    printf(",\n  \"package\": ");
    ZBuf size_package_json;
    zbuf_init(&size_package_json);
    append_package_metadata_json(&size_package_json, &input, target);
    fputs(size_package_json.data, stdout);
    zbuf_free(&size_package_json);
    printf(",\n  \"packageCache\": ");
    ZBuf size_package_cache_json;
    zbuf_init(&size_package_cache_json);
    append_package_cache_audit_json(&size_package_cache_json, &input, target, command.profile);
    fputs(size_package_cache_json.data, stdout);
    zbuf_free(&size_package_cache_json);
    printf(",\n  \"target\": ");
    print_json_string(target ? target->name : z_host_target());
    printf(",\n  \"hostTarget\": ");
    print_json_string(z_host_target());
    printf(",\n  \"profile\": ");
    print_json_string(command.profile);
    printf(",\n  \"targetSupport\": {\"fsAvailable\": %s},\n  \"requiresCapabilities\": ", z_target_has_capability(target, "fs") ? "true" : "false");
    ZBuf cap_json;
    zbuf_init(&cap_json);
    append_capability_json_array(&cap_json, &caps);
    fputs(cap_json.data, stdout);
    zbuf_free(&cap_json);
    printf(",\n  \"runtimeImportAudit\": ");
    ZBuf runtime_import_audit_json;
    zbuf_init(&runtime_import_audit_json);
    append_runtime_import_audit_json(&runtime_import_audit_json, &ir, &input, target);
    fputs(runtime_import_audit_json.data, stdout);
    zbuf_free(&runtime_import_audit_json);
    printf(",\n  \"portableRuntime\": ");
    ZBuf portable_runtime_json;
    zbuf_init(&portable_runtime_json);
    append_portable_runtime_json(&portable_runtime_json, &ir, &input, target, &caps);
    fputs(portable_runtime_json.data, stdout);
    zbuf_free(&portable_runtime_json);
    printf(",\n  \"stdlibHelpers\": ");
    ZBuf helper_json;
    zbuf_init(&helper_json);
    append_stdlib_helpers_json(&helper_json);
    fputs(helper_json.data, stdout);
    zbuf_free(&helper_json);
    printf(",\n  \"usedStdlibHelpers\": ");
    ZBuf used_helper_json;
    zbuf_init(&used_helper_json);
    append_used_stdlib_helpers_json(&used_helper_json, &used_helpers);
    fputs(used_helper_json.data, stdout);
    zbuf_free(&used_helper_json);
    printf(",\n  \"stdlibHelperAttribution\": ");
    ZBuf helper_attribution_json;
    zbuf_init(&helper_attribution_json);
    append_used_stdlib_helpers_json(&helper_attribution_json, &used_helpers);
    fputs(helper_attribution_json.data, stdout);
    zbuf_free(&helper_attribution_json);
    printf(",\n  \"selfHostRouting\": ");
    ZBuf size_self_host_routing_json;
    zbuf_init(&size_self_host_routing_json);
    append_self_host_routing_json(&size_self_host_routing_json, "size", NULL, &program, &caps, target);
    fputs(size_self_host_routing_json.data, stdout);
    zbuf_free(&size_self_host_routing_json);
    printf(",\n  \"compilerRuntimeHelpers\": ");
    ZBuf compiler_runtime_helpers_json;
    zbuf_init(&compiler_runtime_helpers_json);
    append_compiler_runtime_helpers_json_ex(&compiler_runtime_helpers_json, NULL, false);
    fputs(compiler_runtime_helpers_json.data, stdout);
    zbuf_free(&compiler_runtime_helpers_json);
    printf(",\n  \"genericSpecializations\": ");
    ZBuf direct_generic_specializations_json;
    zbuf_init(&direct_generic_specializations_json);
    append_direct_generic_specializations_json(&direct_generic_specializations_json, &program);
    fputs(direct_generic_specializations_json.data, stdout);
    zbuf_free(&direct_generic_specializations_json);
    printf(",\n  \"runtimeShims\": ");
    ZBuf runtime_shims_json;
    zbuf_init(&runtime_shims_json);
    append_runtime_shims_json_ex(&runtime_shims_json, NULL, &caps, program_uses_bounds_checked_access(&program));
    fputs(runtime_shims_json.data, stdout);
    zbuf_free(&runtime_shims_json);
    printf(",\n  \"sections\": [{\"name\":\"lowered-ir\",\"kind\":\"ir\",\"bytes\":%zu}, {\"name\":\"direct-size-metadata\",\"kind\":\"metadata\",\"bytes\":0}", input.lowered_ir_bytes);
    if (artifact_bytes >= 0) printf(", {\"name\":\"artifact\",\"kind\":\"metadata\",\"bytes\":%lld}", artifact_bytes);
    printf("],\n  \"topLargestEmittedHelpers\": ");
    ZBuf top_helper_json;
    zbuf_init(&top_helper_json);
    append_top_emitted_helpers_json(&top_helper_json, &used_helpers);
    fputs(top_helper_json.data, stdout);
    zbuf_free(&top_helper_json);
    printf(",\n  \"sizeBreakdown\": ");
    ZBuf size_breakdown_json;
    zbuf_init(&size_breakdown_json);
    append_size_breakdown_json(&size_breakdown_json, &input, &program, target, &command, &used_helpers, &caps, artifact_bytes);
    fputs(size_breakdown_json.data, stdout);
    zbuf_free(&size_breakdown_json);
    printf(",\n  \"retentionReasons\": ");
    ZBuf retention_reasons_json;
    zbuf_init(&retention_reasons_json);
    append_retention_reasons_json(&retention_reasons_json, &input, &program, &used_helpers, command.profile);
    fputs(retention_reasons_json.data, stdout);
    zbuf_free(&retention_reasons_json);
    printf(",\n  \"optimizationHints\": ");
    ZBuf optimization_hints_json;
    zbuf_init(&optimization_hints_json);
    append_optimization_hints_json(&optimization_hints_json, &input, &used_helpers, &caps, command.profile);
    fputs(optimization_hints_json.data, stdout);
    zbuf_free(&optimization_hints_json);
    printf(",\n  \"profileBudget\": ");
    ZBuf profile_budget_json;
    zbuf_init(&profile_budget_json);
    append_profile_budget_json(&profile_budget_json, command.profile);
    fputs(profile_budget_json.data, stdout);
    zbuf_free(&profile_budget_json);
    printf(",\n  \"generatedCBytes\": 0,\n  \"cBridgeFallback\": false,\n  \"loweredIrBytes\": %zu,\n  \"artifactPath\": ", input.lowered_ir_bytes);
    if (artifact_path) {
      print_json_string(artifact_path);
    } else {
      printf("null");
    }
    printf(",\n  \"artifactBytes\": ");
    if (artifact_bytes >= 0) printf("%lld", artifact_bytes);
    else printf("null");
    printf(",\n  \"compilerPhases\": ");
    ZBuf size_phases_json;
    zbuf_init(&size_phases_json);
    append_compiler_phases_json(&size_phases_json, &input);
    fputs(size_phases_json.data, stdout);
    zbuf_free(&size_phases_json);
    printf(",\n  \"compilerCaches\": ");
    ZBuf size_caches_json;
    zbuf_init(&size_caches_json);
    append_compiler_caches_json(&size_caches_json, &input, target, command.profile);
    fputs(size_caches_json.data, stdout);
    zbuf_free(&size_caches_json);
    printf(",\n  \"incrementalInvalidation\": ");
    ZBuf size_invalidation_json;
    zbuf_init(&size_invalidation_json);
    append_incremental_invalidations_json(&size_invalidation_json, &input, target, command.profile);
    fputs(size_invalidation_json.data, stdout);
    zbuf_free(&size_invalidation_json);
    printf(",\n  \"profileSemantics\": ");
    ZBuf size_profile_json;
    zbuf_init(&size_profile_json);
    append_profile_semantics_json(&size_profile_json, command.profile);
    fputs(size_profile_json.data, stdout);
    zbuf_free(&size_profile_json);
    printf(",\n  \"profileCatalog\": ");
    ZBuf size_profile_catalog_json;
    zbuf_init(&size_profile_catalog_json);
    append_profile_catalog_json(&size_profile_catalog_json);
    fputs(size_profile_catalog_json.data, stdout);
    zbuf_free(&size_profile_catalog_json);
    printf(",\n  \"objectBackend\": ");
    ZBuf size_object_json;
    zbuf_init(&size_object_json);
    append_object_backend_json(&size_object_json, &input, target, &command, "size");
    fputs(size_object_json.data, stdout);
    zbuf_free(&size_object_json);
    printf("\n}\n");
    free(artifact_path);
    z_free_ir_program(&ir);
    z_free_program(&program);
    z_free_source(&input);
    return 0;
  }
  int rc = return_direct_backend_error(&command, &input, target, "exe", "direct executable backend is not implemented for this target/backend pair; use --emit obj for direct target objects or choose a supported direct executable target", &ir, &program);
  z_free_source(&input);
  return rc;
}
