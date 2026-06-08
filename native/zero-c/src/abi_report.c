#include "abi_report.h"

#include "program_graph_query_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void abi_append_json_string(ZBuf *buf, const char *s) {
  zbuf_append_char(buf, '"');
  const unsigned char *p = (const unsigned char *)(s ? s : "");
  while (*p) {
    unsigned char ch = *p++;
    switch (ch) {
      case '"': zbuf_append(buf, "\\\""); break;
      case '\\': zbuf_append(buf, "\\\\"); break;
      case '\n': zbuf_append(buf, "\\n"); break;
      case '\r': zbuf_append(buf, "\\r"); break;
      case '\t': zbuf_append(buf, "\\t"); break;
      default:
        if (ch < 0x20) zbuf_appendf(buf, "\\u%04x", ch);
        else zbuf_append_char(buf, (char)ch);
        break;
    }
  }
  zbuf_append_char(buf, '"');
}

static int abi_report_pointer_size(const ZTargetInfo *target) {
  (void)target;
  return 8;
}

typedef struct {
  const char *zero_type;
  const char *c_type;
  int fixed_size;
  bool pointer_sized;
  bool include_in_layout;
} ZAbiPrimitive;

static const ZAbiPrimitive abi_primitives[] = {
  {"Void", "void", 0, false, false},
  {"Bool", "bool", 1, false, true},
  {"u8", "uint8_t", 1, false, true},
  {"i8", "int8_t", 1, false, true},
  {"char", "void *", 1, false, false},
  {"u16", "uint16_t", 2, false, true},
  {"i16", "int16_t", 2, false, true},
  {"u32", "uint32_t", 4, false, true},
  {"i32", "int32_t", 4, false, true},
  {"u64", "uint64_t", 8, false, true},
  {"i64", "int64_t", 8, false, true},
  {"usize", "uintptr_t", 0, true, true},
  {"isize", "intptr_t", 0, true, true},
  {"f32", "float", 4, false, true},
  {"f64", "double", 8, false, true},
};

static const size_t abi_primitives_len = sizeof(abi_primitives) / sizeof(abi_primitives[0]);

static const ZAbiPrimitive *abi_report_primitive_for_type(const char *type) {
  if (!type) return NULL;
  for (size_t i = 0; i < abi_primitives_len; i++) {
    if (strcmp(abi_primitives[i].zero_type, type) == 0) return &abi_primitives[i];
  }
  return NULL;
}

static bool abi_report_type_has_prefix(const char *type, const char *prefix) {
  if (!type || !prefix) return false;
  size_t prefix_len = strlen(prefix);
  return strncmp(type, prefix, prefix_len) == 0;
}

static bool abi_report_type_is_ref_like(const char *type) {
  return abi_report_type_has_prefix(type, "ref<") || abi_report_type_has_prefix(type, "mutref<");
}

static int abi_report_type_size(const char *type, const ZTargetInfo *target) {
  if (!type) return 0;
  const ZAbiPrimitive *primitive = abi_report_primitive_for_type(type);
  if (primitive) {
    return primitive->pointer_sized ? abi_report_pointer_size(target) : primitive->fixed_size;
  }
  if (abi_report_type_is_ref_like(type)) return abi_report_pointer_size(target);
  return 0;
}

static int abi_report_type_align(const char *type, const ZTargetInfo *target) {
  int size = abi_report_type_size(type, target);
  int pointer = abi_report_pointer_size(target);
  if (size > pointer) return pointer;
  return size > 0 ? size : 1;
}

static int abi_report_align_to_int(int value, int align) {
  if (align <= 1) return value;
  int remainder = value % align;
  return remainder == 0 ? value : value + (align - remainder);
}

static const char *abi_report_c_type_name(const char *zero_type) {
  if (!zero_type) return "void";
  const ZAbiPrimitive *primitive = abi_report_primitive_for_type(zero_type);
  if (primitive) return primitive->c_type;
  return "void *";
}

static void append_abi_primitives_json(ZBuf *buf, const ZTargetInfo *target) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; i < abi_primitives_len; i++) {
    const ZAbiPrimitive *primitive = &abi_primitives[i];
    if (!primitive->include_in_layout) continue;
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    abi_append_json_string(buf, primitive->zero_type);
    zbuf_appendf(buf, ",\"size\":%d,\"align\":%d}", abi_report_type_size(primitive->zero_type, target), abi_report_type_align(primitive->zero_type, target));
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
    abi_append_json_string(buf, shape->name);
    zbuf_append(buf, ",\"layout\":\"extern\",\"fields\":[");
    for (size_t field_index = 0; field_index < shape->fields.len; field_index++) {
      const Param *field = &shape->fields.items[field_index];
      int align = abi_report_type_align(field->type, target);
      int size = abi_report_type_size(field->type, target);
      offset = abi_report_align_to_int(offset, align);
      if (align > max_align) max_align = align;
      if (field_index > 0) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      abi_append_json_string(buf, field->name);
      zbuf_append(buf, ",\"type\":");
      abi_append_json_string(buf, field->type);
      zbuf_appendf(buf, ",\"offset\":%d,\"size\":%d,\"align\":%d}", offset, size, align);
      offset += size;
    }
    zbuf_appendf(buf, "],\"size\":%d,\"align\":%d}", abi_report_align_to_int(offset, max_align), max_align);
  }
  zbuf_append(buf, "]");
}

static void append_abi_enums_json(ZBuf *buf, const Program *program) {
  zbuf_append(buf, "[");
  for (size_t i = 0; program && i < program->enums.len; i++) {
    if (i > 0) zbuf_append(buf, ",");
    zbuf_append(buf, "{\"name\":");
    abi_append_json_string(buf, program->enums.items[i].name);
    zbuf_appendf(buf, ",\"size\":4,\"align\":4,\"cases\":%zu}", program->enums.items[i].cases.len);
  }
  zbuf_append(buf, "]");
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
    abi_append_json_string(buf, fun->name);
    zbuf_append(buf, ",\"returnType\":");
    abi_append_json_string(buf, fun->return_type ? fun->return_type : "Void");
    zbuf_append(buf, ",\"cReturnType\":");
    abi_append_json_string(buf, abi_report_c_type_name(fun->return_type));
    zbuf_append(buf, ",\"raises\":");
    zbuf_append(buf, fun->raises ? "true" : "false");
    zbuf_append(buf, ",\"params\":[");
    for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
      const Param *param = &fun->params.items[param_index];
      if (param_index > 0) zbuf_append(buf, ",");
      zbuf_append(buf, "{\"name\":");
      abi_append_json_string(buf, param->name);
      zbuf_append(buf, ",\"type\":");
      abi_append_json_string(buf, param->type);
      zbuf_append(buf, ",\"cType\":");
      abi_append_json_string(buf, abi_report_c_type_name(param->type));
      zbuf_append(buf, "}");
    }
    zbuf_append(buf, "]}");
  }
  zbuf_append(buf, "]");
}

static size_t graph_c_export_count(const ZProgramGraph *graph) {
  size_t count = 0;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    if (graph->nodes[i].kind == Z_PROGRAM_GRAPH_NODE_FUNCTION && graph->nodes[i].export_c) count++;
  }
  return count;
}

static void append_c_exports_json_from_graph(ZBuf *buf, const ZProgramGraph *graph) {
  zbuf_append(buf, "[");
  bool wrote = false;
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *fun = &graph->nodes[i];
    if (fun->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !fun->export_c) continue;
    if (wrote) zbuf_append(buf, ",");
    wrote = true;
    zbuf_append(buf, "{\"name\":");
    abi_append_json_string(buf, fun->name);
    zbuf_append(buf, ",\"returnType\":");
    abi_append_json_string(buf, fun->type && fun->type[0] ? fun->type : "Void");
    zbuf_append(buf, ",\"cReturnType\":");
    abi_append_json_string(buf, abi_report_c_type_name(fun->type));
    zbuf_append(buf, ",\"raises\":");
    zbuf_append(buf, fun->fallible ? "true" : "false");
    zbuf_append(buf, ",\"params\":[");
    size_t param_count = z_program_graph_query_child_count(graph, fun->id, "param");
    bool wrote_param = false;
    for (size_t param_order = 0; param_order < param_count; param_order++) {
      const ZProgramGraphNode *param = z_program_graph_query_child_node(graph, fun->id, "param", param_order);
      if (!param) continue;
      if (wrote_param) zbuf_append(buf, ",");
      wrote_param = true;
      zbuf_append(buf, "{\"name\":");
      abi_append_json_string(buf, param->name);
      zbuf_append(buf, ",\"type\":");
      abi_append_json_string(buf, param->type);
      zbuf_append(buf, ",\"cType\":");
      abi_append_json_string(buf, abi_report_c_type_name(param->type));
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
    zbuf_append(header, abi_report_c_type_name(fun->return_type));
    zbuf_append_char(header, ' ');
    zbuf_append(header, fun->name);
    zbuf_append_char(header, '(');
    if (fun->params.len == 0) {
      zbuf_append(header, "void");
    } else {
      for (size_t param_index = 0; param_index < fun->params.len; param_index++) {
        const Param *param = &fun->params.items[param_index];
        if (param_index > 0) zbuf_append(header, ", ");
        zbuf_append(header, abi_report_c_type_name(param->type));
        zbuf_append_char(header, ' ');
        zbuf_append(header, param->name);
      }
    }
    zbuf_append(header, ");\n");
  }
}

static void append_c_export_header_text_from_graph(ZBuf *header, const ZProgramGraph *graph) {
  zbuf_append(header, "#pragma once\n#include <stdbool.h>\n#include <stdint.h>\n\n");
  for (size_t i = 0; graph && i < graph->node_len; i++) {
    const ZProgramGraphNode *fun = &graph->nodes[i];
    if (fun->kind != Z_PROGRAM_GRAPH_NODE_FUNCTION || !fun->export_c) continue;
    zbuf_append(header, abi_report_c_type_name(fun->type));
    zbuf_append_char(header, ' ');
    zbuf_append(header, fun->name ? fun->name : "");
    zbuf_append_char(header, '(');
    size_t param_count = z_program_graph_query_child_count(graph, fun->id, "param");
    if (param_count == 0) {
      zbuf_append(header, "void");
    } else {
      bool wrote_param = false;
      for (size_t param_order = 0; param_order < param_count; param_order++) {
        const ZProgramGraphNode *param = z_program_graph_query_child_node(graph, fun->id, "param", param_order);
        if (!param) continue;
        if (wrote_param) zbuf_append(header, ", ");
        wrote_param = true;
        zbuf_append(header, abi_report_c_type_name(param->type));
        zbuf_append_char(header, ' ');
        zbuf_append(header, param->name ? param->name : "");
      }
      if (!wrote_param) zbuf_append(header, "void");
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
  abi_append_json_string(buf, header.data ? header.data : "");
  zbuf_append(buf, "}");
  zbuf_free(&header);
}

static void append_c_export_header_json_from_graph(ZBuf *buf, const ZProgramGraph *graph) {
  ZBuf header;
  zbuf_init(&header);
  append_c_export_header_text_from_graph(&header, graph);
  size_t export_count = graph_c_export_count(graph);
  zbuf_appendf(buf, "{\"available\":%s,\"format\":\"c-header\",\"exportCount\":%zu,\"text\":", export_count > 0 ? "true" : "false", export_count);
  abi_append_json_string(buf, header.data ? header.data : "");
  zbuf_append(buf, "}");
  zbuf_free(&header);
}

void z_append_abi_dump_json(
  ZBuf *buf,
  const SourceInput *input,
  const Program *program,
  const ZTargetInfo *target,
  const ZProgramGraph *graph,
  ZAbiCImportsJsonFn append_c_imports_json
) {
  zbuf_append(buf, "{\n  \"schemaVersion\": 1,\n  \"sourceFile\": ");
  abi_append_json_string(buf, input ? input->source_file : "");
  zbuf_append(buf, ",\n  \"target\": ");
  abi_append_json_string(buf, target ? target->name : "host");
  zbuf_append(buf, ",\n  \"pointerSize\": ");
  zbuf_appendf(buf, "%d", abi_report_pointer_size(target));
  zbuf_append(buf, ",\n  \"objectFormat\": ");
  abi_append_json_string(buf, target && target->object_format ? target->object_format : "unknown");
  zbuf_append(buf, ",\n  \"callingConvention\": ");
  abi_append_json_string(buf, target && target->abi ? target->abi : "host");
  zbuf_append(buf, ",\n  \"primitiveLayouts\": ");
  append_abi_primitives_json(buf, target);
  zbuf_append(buf, ",\n  \"externShapes\": ");
  append_abi_shapes_json(buf, program, target);
  zbuf_append(buf, ",\n  \"enums\": ");
  append_abi_enums_json(buf, program);
  zbuf_append(buf, ",\n  \"cImports\": ");
  if (append_c_imports_json) append_c_imports_json(buf, program, target);
  else zbuf_append(buf, "[]");
  zbuf_append(buf, ",\n  \"cExports\": ");
  if (graph) append_c_exports_json_from_graph(buf, graph);
  else append_c_exports_json(buf, program);
  zbuf_append(buf, ",\n  \"generatedHeader\": ");
  if (graph) append_c_export_header_json_from_graph(buf, graph);
  else append_c_export_header_json(buf, program);
  zbuf_append(buf, "\n}\n");
}
