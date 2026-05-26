#include "zdn_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Low-level helpers ──

void zdn_append_string(ZBuf *buf, const char *value) {
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

void zdn_append_null(ZBuf *buf) {
  zbuf_append(buf, "null");
}

static void indent(ZBuf *buf, int level) {
  for (int i = 0; i < level; i++) {
    zbuf_append(buf, "  ");
  }
}

// ── Field helpers ──

void zdn_field_string(ZBuf *buf, const char *name, const char *value, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zdn_append_string(buf, value);
  zbuf_append(buf, "\n");
}

void zdn_field_int(ZBuf *buf, const char *name, long long value, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zbuf_appendf(buf, "%lld", value);
  zbuf_append(buf, "\n");
}

void zdn_field_bool(ZBuf *buf, const char *name, bool value, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zbuf_append(buf, value ? "true" : "false");
  zbuf_append(buf, "\n");
}

void zdn_field_nullable_string(ZBuf *buf, const char *name, const char *value, int indent_level) {
  if (value && value[0]) {
    zdn_field_string(buf, name, value, indent_level);
  } else {
    indent(buf, indent_level);
    zbuf_append(buf, name);
    zbuf_append(buf, " null\n");
  }
}

void zdn_field_nullable_int(ZBuf *buf, const char *name, long long value, int indent_level) {
  if (value >= 0) {
    zdn_field_int(buf, name, value, indent_level);
  } else {
    indent(buf, indent_level);
    zbuf_append(buf, name);
    zbuf_append(buf, " null\n");
  }
}

// ── Object helpers ──

void zdn_object_start(ZBuf *buf, const char *name, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, name);
  zbuf_append(buf, "\n");
}

void zdn_object_end(ZBuf *buf, int indent_level) {
  (void)buf;
  (void)indent_level;
  // No closing delimiter needed — dedentation implies end.
}

// ── Array helpers ──

void zdn_array_start(ZBuf *buf, const char *name, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, name);
  zbuf_append(buf, "\n");
}

void zdn_array_end(ZBuf *buf, int indent_level) {
  (void)buf;
  (void)indent_level;
  // No closing delimiter needed — dedentation implies end.
}

void zdn_array_item_string(ZBuf *buf, const char *value, int indent_level) {
  indent(buf, indent_level);
  zdn_append_string(buf, value);
  zbuf_append(buf, "\n");
}

void zdn_array_item_int(ZBuf *buf, long long value, int indent_level) {
  indent(buf, indent_level);
  zbuf_appendf(buf, "%lld", value);
  zbuf_append(buf, "\n");
}

// ── Local diagnostic code helpers (duplicated from main.c for linker independence) ──

static const char *code_to_string(int code) {
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
    case 3011: return "TYP010";
    case 3012: return "TYP011";
    case 3013: return "TYP012";
    case 3014: return "TYP013";
    case 3015: return "TYP014";
    case 3016: return "TYP015";
    case 3017: return "TYP016";
    case 3018: return "TYP017";
    case 3019: return "TYP018";
    case 3020: return "TYP019";
    case 3021: return "TYP020";
    case 3022: return "TYP021";
    case 3024: return "TYP023";
    case 3025: return "TYP024";
    case 3026: return "TYP025";
    case 3027: return "TYP026";
    case 3028: return "TYP027";
    case 3029: return "TYP028";
    case 3030: return "TYP029";
    case 3031: return "TYP030";
    case 3032: return "TYP031";
    case 3033: return "TYP032";
    case 3034: return "TYP033";
    case 3035: return "TYP034";
    case 3036: return "TYP035";
    case 4001: return "BND001";
    case 4002: return "BND002";
    case 4003: return "BND003";
    case 4004: return "BND004";
    case 4005: return "BND005";
    case 4006: return "BND006";
    case 4007: return "BND007";
    case 4008: return "BND008";
    case 4009: return "BND009";
    case 4010: return "BND010";
    case 4011: return "BND011";
    case 5001: return "DUP001";
    case 5002: return "DUP002";
    case 5003: return "DUP003";
    case 5004: return "DUP004";
    case 6001: return "CAP001";
    case 6002: return "CAP002";
    case 7001: return "BRW001";
    case 7002: return "BRW002";
    case 7003: return "BRW003";
    case 8001: return "TRG001";
    case 8002: return "TRG002";
    case 9001: return "DEP001";
    default: return "UNK000";
  }
}

static const char *repair_id(int code) {
  switch (code) {
    case 1001: return "add-missing-block-expression";
    case 2001: return "add-type-annotation";
    case 2002: case 2003: case 2004: return "update-emit-flag";
    case 3002: return "rename-symbol";
    case 3003: return "remove-duplicate-definition";
    case 3004: case 3008: return "rename-or-remove";
    case 3005: case 3006: case 3007: case 3009: case 3010: case 3011:
    case 3012: case 3013: case 3014: case 3015: case 3016: case 3017:
    case 3018: case 3019: case 3020: case 3021: case 3022: case 3024:
    case 3025: case 3026: case 3027: case 3028: case 3029: case 3030:
    case 3031: case 3032: case 3033: case 3034: case 3035: case 3036:
      return "fix-type-mismatch";
    case 4001: case 4002: case 4003: case 4004: case 4005:
    case 4006: case 4007: case 4008: case 4009: case 4010:
    case 4011:
      return "fix-binding";
    case 5001: case 5002: case 5003: case 5004:
      return "remove-duplicate";
    case 6001: case 6002:
      return "add-capability";
    case 7001: case 7002: case 7003:
      return "fix-borrow";
    case 8001: case 8002:
      return "check-target";
    case 9001:
      return "install-dependency";
    default:
      return "unknown";
  }
}

static const char *repair_summary(int code) {
  switch (code) {
    case 1001: return "Add missing block expression";
    case 2001: return "Add explicit type annotation";
    case 2002: return "Update --emit to exe";
    case 2003: return "Update --emit to exe";
    case 2004: return "Update --emit to exe";
    case 3002: return "Rename to avoid name collision";
    case 3003: return "Remove duplicate definition";
    case 3004: return "Rename or remove conflicting name";
    case 3005: case 3006: case 3007: case 3009:
      return "Fix type mismatch";
    case 4001: return "Add missing binding declaration";
    default: return "Automated fix available";
  }
}

// ── Diagnostics ZDN output ──

void zdn_print_diag(const char *path, const ZDiag *diag) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "CheckResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_bool(&buf, "ok", false, 1);

  zdn_array_start(&buf, "diagnostics", 1);
  {
    zdn_object_start(&buf, "", 2);
    zdn_field_string(&buf, "severity", "error", 3);
    zdn_field_string(&buf, "code", code_to_string(diag->code), 3);
    zdn_field_string(&buf, "message", diag->message, 3);
    zdn_field_string(&buf, "path", diag->path ? diag->path : (path ? path : "<input>"), 3);
    zdn_field_int(&buf, "line", diag->line, 3);
    zdn_field_int(&buf, "column", diag->column, 3);
    zdn_field_int(&buf, "length", diag->length > 0 ? diag->length : 1, 3);
    zdn_field_string(&buf, "expected", diag->expected, 3);
    zdn_field_string(&buf, "actual", diag->actual, 3);
    zdn_field_string(&buf, "help", diag->help, 3);
    zdn_field_string(&buf, "fixSafety", "unknown", 3); // simplified

    // repair sub-object
    zdn_object_start(&buf, "repair", 3);
    zdn_field_string(&buf, "id", repair_id(diag->code), 4);
    zdn_field_string(&buf, "summary", repair_summary(diag->code), 4);

    if (diag->backend_blocker.present) {
      zdn_object_start(&buf, "backendBlocker", 3);
      zdn_field_string(&buf, "target",
        diag->backend_blocker.target[0] ? diag->backend_blocker.target : "unknown", 4);
      zdn_field_string(&buf, "objectFormat",
        diag->backend_blocker.object_format[0] ? diag->backend_blocker.object_format : "unknown", 4);
      zdn_field_string(&buf, "backend",
        diag->backend_blocker.backend[0] ? diag->backend_blocker.backend : "unknown", 4);
      zdn_field_string(&buf, "stage",
        diag->backend_blocker.stage[0] ? diag->backend_blocker.stage : "unknown", 4);
      zdn_field_string(&buf, "unsupportedFeature",
        diag->backend_blocker.unsupported_feature[0] ? diag->backend_blocker.unsupported_feature : "unsupported construct", 4);
    }

    if (diag->borrow_trace_count > 0) {
      zdn_array_start(&buf, "borrowTrace", 3);
      for (size_t i = 0; i < diag->borrow_trace_count; i++) {
        const ZBorrowTrace *bt = &diag->borrow_traces[i];
        zdn_object_start(&buf, "", 4);
        zdn_field_string(&buf, "root", bt->root, 5);
        zdn_field_string(&buf, "path", bt->path, 5);
        zdn_field_string(&buf, "kind", bt->kind, 5);
        zdn_field_string(&buf, "binding", bt->binding, 5);
      }
      if (diag->borrow_trace_truncated) {
        zdn_array_item_string(&buf, "<truncated>", 4);
      }
    }

    // related array
    zdn_array_start(&buf, "related", 3);
    if ((diag->code == 7001 || diag->code == 7002 || diag->code == 7003 ||
         diag->code == 1002 || diag->code == 1003 ||
         diag->code == 6001 || diag->code == 6002 ||
         diag->code == 3010 || diag->code == 3011 || diag->code == 3012 ||
         diag->code == 3028 || diag->code == 3029) && diag->actual[0]) {
      zdn_field_string(&buf, "path", diag->path ? diag->path : (path ? path : ""), 4);
      zdn_field_int(&buf, "line", diag->line, 4);
      zdn_field_int(&buf, "column", diag->column, 4);
      zdn_field_string(&buf, "message", diag->actual, 4);
    }
  }

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Check success ──

void zdn_print_check_success(const char *path, const SourceInput *input,
                              const Program *program, const ZTargetInfo *target) {
  ZBuf buf;
  zbuf_init(&buf);
  (void)program;

  zbuf_append(&buf, "CheckResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_bool(&buf, "ok", true, 1);
  zdn_field_string(&buf, "sourceFile", path ? path : (input && input->source_file ? input->source_file : "<input>"), 1);
  zdn_field_string(&buf, "hostTarget", z_host_target(), 1);
  zdn_field_string(&buf, "target", target && target->name ? target->name : "host", 1);
  zdn_array_start(&buf, "diagnostics", 1);
  // empty diagnostics array — no items
  zdn_array_end(&buf, 1);
  zdn_object_start(&buf, "targetReadiness", 1);
  zdn_field_bool(&buf, "buildable", true, 2);
  zdn_field_string(&buf, "backend", target && target->name ? z_direct_backend_name_for_emit_kind(target, "exe", NULL) : "unknown", 2);
  zdn_field_string(&buf, "objectFormat", target && target->object_format ? target->object_format : "unknown", 2);
  zdn_object_end(&buf, 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Build result ──

void zdn_print_build(const char *source_file, const char *emit_kind,
                     const char *target_name, const char *profile,
                     const char *artifact_path, long long artifact_bytes,
                     long long elapsed_ms) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "BuildResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "emit", emit_kind ? emit_kind : "exe", 1);
  zdn_field_string(&buf, "hostTarget", z_host_target(), 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_string(&buf, "profile", profile ? profile : "release", 1);
  zdn_field_nullable_string(&buf, "artifactPath", artifact_path, 1);
  zdn_field_nullable_int(&buf, "artifactBytes", artifact_bytes, 1);
  zdn_field_int(&buf, "elapsedMs", elapsed_ms, 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Size result ──

void zdn_print_size(const char *source_file, const char *target_name,
                    const char *profile, const char *host_target,
                    size_t lowered_ir_bytes, long long artifact_bytes,
                    const char *artifact_path) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "SizeResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_string(&buf, "profile", profile ? profile : "release", 1);
  zdn_field_string(&buf, "hostTarget", host_target ? host_target : z_host_target(), 1);
  zdn_field_int(&buf, "loweredIrBytes", (long long)lowered_ir_bytes, 1);
  zdn_field_nullable_int(&buf, "artifactBytes", artifact_bytes, 1);
  zdn_field_nullable_string(&buf, "artifactPath", artifact_path, 1);

  // sections array
  zdn_array_start(&buf, "sections", 1);
  {
    zdn_object_start_inline(&buf, 2);
    zdn_inline_field_string(&buf, "name", "lowered-ir");
    zdn_inline_field_string(&buf, "kind", "ir");
    zdn_inline_field_int(&buf, "bytes", (long long)lowered_ir_bytes);
    zdn_object_end_inline(&buf);
    zbuf_append(&buf, "\n");
  }

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Ship result ──

void zdn_print_ship(const char *source_file, const char *target_name,
                    const char *host_target, const char *profile,
                    const char *artifact_path, long long artifact_bytes,
                    long long elapsed_ms, const char *checksum_value) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "ShipResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_string(&buf, "hostTarget", host_target ? host_target : z_host_target(), 1);
  zdn_field_string(&buf, "profile", profile ? profile : "release", 1);
  zdn_field_nullable_string(&buf, "artifactPath", artifact_path, 1);
  zdn_field_nullable_int(&buf, "artifactBytes", artifact_bytes, 1);
  zdn_field_int(&buf, "elapsedMs", elapsed_ms, 1);
  if (checksum_value && checksum_value[0]) {
    zdn_object_start(&buf, "checksum", 1);
    zdn_field_string(&buf, "algorithm", "fnv1a64", 2);
    zdn_field_string(&buf, "path", checksum_value, 2);
  }

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Doc result ──

void zdn_print_doc(const char *source_file, const char *target_name) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "DocResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Dev result ──

void zdn_print_dev(const char *source_file, const char *target_name,
                   const char *profile) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "DevResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_string(&buf, "profile", profile ? profile : "release", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Time result ──

void zdn_print_time(const char *source_file, const char *target_name,
                    size_t elapsed_ms) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "TimeResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_int(&buf, "elapsedMs", (long long)elapsed_ms, 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Tokens result ──

void zdn_print_tokens(const char *source_file) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "TokensResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Parse result ──

void zdn_print_parse(const char *source_file) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "ParseResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Mem result ──

void zdn_print_mem(const char *source_file, const char *target_name,
                   const char *profile) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "MemResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);
  zdn_field_string(&buf, "profile", profile ? profile : "release", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Graph result ──

void zdn_print_graph(const char *source_file, const char *target_name) {
  ZBuf buf;
  zbuf_init(&buf);

  zbuf_append(&buf, "GraphResult\n");
  zdn_field_int(&buf, "schemaVersion", 1, 1);
  zdn_field_string(&buf, "sourceFile", source_file ? source_file : "<input>", 1);
  zdn_field_string(&buf, "target", target_name ? target_name : "host", 1);

  fputs(buf.data, stdout);
  zbuf_free(&buf);
}

// ── Inline object support ──

void zdn_object_start_inline(ZBuf *buf, int indent_level) {
  indent(buf, indent_level);
  zbuf_append_char(buf, '{');
  zbuf_append_char(buf, ' ');
}

void zdn_object_end_inline(ZBuf *buf) {
  zbuf_append_char(buf, '}');
}

void zdn_inline_field_string(ZBuf *buf, const char *name, const char *value) {
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zdn_append_string(buf, value);
  zbuf_append_char(buf, ' ');
}

void zdn_inline_field_int(ZBuf *buf, const char *name, long long value) {
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zbuf_appendf(buf, "%lld", value);
  zbuf_append_char(buf, ' ');
}

void zdn_inline_field_bool(ZBuf *buf, const char *name, bool value) {
  zbuf_append(buf, name);
  zbuf_append(buf, " ");
  zbuf_append(buf, value ? "true " : "false ");
}

// ── Patch format ──

void zdn_print_patch_header(ZBuf *buf, const char *record_name, int indent_level) {
  indent(buf, indent_level);
  zbuf_append(buf, "patch ");
  zbuf_append(buf, record_name);
  zbuf_append(buf, "\n");
}
