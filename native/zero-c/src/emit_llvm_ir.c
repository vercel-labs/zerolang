#include "zero.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  IrTypeKind type;
  IrTypeKind element_type;
  char text[96];
  char ptr[96];
  char len[96];
} LlvmValue;

typedef struct {
  const IrProgram *program;
  const IrFunction *fun;
  ZBuf *out;
  unsigned temp_index;
  unsigned label_index;
  unsigned current_label;
} LlvmEmit;

static bool llvm_scalar_type_supported(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 ||
         type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 ||
         type == IR_TYPE_I64 || type == IR_TYPE_U64 || type == IR_TYPE_USIZE;
}

static bool llvm_type_supported(IrTypeKind type) {
  return llvm_scalar_type_supported(type) || type == IR_TYPE_BYTE_VIEW;
}

static bool llvm_array_element_supported(IrTypeKind type) {
  return type == IR_TYPE_BOOL || type == IR_TYPE_U8 || type == IR_TYPE_U16 ||
         type == IR_TYPE_I32 || type == IR_TYPE_U32 || type == IR_TYPE_I64 ||
         type == IR_TYPE_U64 || type == IR_TYPE_USIZE;
}

static const char *llvm_type_name(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_VOID: return "void";
    case IR_TYPE_BOOL: return "i1";
    case IR_TYPE_U8: return "i8";
    case IR_TYPE_U16: return "i16";
    case IR_TYPE_I32:
    case IR_TYPE_U32: return "i32";
    case IR_TYPE_I64:
    case IR_TYPE_U64:
    case IR_TYPE_USIZE: return "i64";
    case IR_TYPE_BYTE_VIEW: return "{ ptr, i64 }";
    default: return "unsupported";
  }
}

static unsigned llvm_type_bits(IrTypeKind type) {
  switch (type) {
    case IR_TYPE_BOOL: return 1;
    case IR_TYPE_U8: return 8;
    case IR_TYPE_U16: return 16;
    case IR_TYPE_I32:
    case IR_TYPE_U32: return 32;
    case IR_TYPE_I64:
    case IR_TYPE_U64:
    case IR_TYPE_USIZE: return 64;
    default: return 0;
  }
}

static bool llvm_type_signed(IrTypeKind type) {
  return type == IR_TYPE_I32 || type == IR_TYPE_I64;
}

static IrTypeKind llvm_value_element_type(const IrValue *value) {
  return value && value->element_type != IR_TYPE_UNSUPPORTED ? value->element_type : IR_TYPE_U8;
}

static IrTypeKind llvm_local_element_type(const IrLocal *local) {
  return local && local->element_type != IR_TYPE_UNSUPPORTED ? local->element_type : IR_TYPE_U8;
}

static void llvm_set_diag(ZDiag *diag, const IrProgram *program, int line, int column, const char *message, const char *actual, const char *stage) {
  if (!diag) return;
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "LLVM IR backend cannot lower MIR");
  snprintf(diag->expected, sizeof(diag->expected), "LLVM IR scalar, fixed-array, and byte-view MIR subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported MIR");
  snprintf(diag->help, sizeof(diag->help), "use --backend llvm --emit llvm-ir for scalar code, fixed arrays, byte views, readonly strings, and primitive std.mem helpers");
  z_backend_blocker_set(&diag->backend_blocker,
                        program && program->target && program->target->name ? program->target->name : "unknown",
                        program && program->target && program->target->object_format ? program->target->object_format : "unknown",
                        "llvm",
                        stage ? stage : "lower",
                        actual ? actual : "unsupported MIR");
}

static void llvm_temp(LlvmEmit *emit, LlvmValue *value, IrTypeKind type) {
  memset(value, 0, sizeof(*value));
  value->type = type;
  value->element_type = IR_TYPE_UNSUPPORTED;
  snprintf(value->text, sizeof(value->text), "%%v%u", emit->temp_index++);
}

static unsigned llvm_label(LlvmEmit *emit) {
  return emit->label_index++;
}

static void llvm_emit_label(LlvmEmit *emit, unsigned label) {
  zbuf_appendf(emit->out, "L%u:\n", label);
  emit->current_label = label;
}

static bool llvm_name_is_ident(const char *name) {
  if (!name || !name[0] || !(isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
  for (size_t i = 1; name[i]; i++) {
    if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) return false;
  }
  return true;
}

static bool llvm_name_is_reserved_runtime_symbol(const char *name) {
  return name && strcmp(name, "zero_world_write") == 0;
}

static char *llvm_symbol_for_function(const IrProgram *program, unsigned index) {
  const IrFunction *fun = program && index < program->function_len ? &program->functions[index] : NULL;
  const char *name = fun && fun->name ? fun->name : "function";
  ZBuf buf; zbuf_init(&buf);
  if (fun && fun->is_exported && llvm_name_is_ident(name)) zbuf_append(&buf, name);
  else {
    zbuf_appendf(&buf, ".zero.fn.%u.", index);
    for (size_t i = 0; name[i]; i++) {
      unsigned char ch = (unsigned char)name[i];
      zbuf_append_char(&buf, (char)((isalnum(ch) || ch == '_') ? ch : '_'));
    }
  }
  return buf.data;
}

static const IrLocal *llvm_local(const IrFunction *fun, unsigned index) {
  return fun && index < fun->local_len ? &fun->locals[index] : NULL;
}

static bool llvm_find_data_segment(const IrProgram *program, unsigned offset, unsigned len, size_t *out_index, unsigned *out_delta) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    if (offset >= segment->offset && offset + len <= segment->offset + segment->len) {
      if (out_index) *out_index = i;
      if (out_delta) *out_delta = offset - segment->offset;
      return true;
    }
  }
  return false;
}

static void llvm_format_local_storage_type(const IrLocal *local, char *buf, size_t len) {
  if (local && local->is_array) snprintf(buf, len, "[%u x %s]", local->array_len, llvm_type_name(local->element_type));
  else snprintf(buf, len, "%s", llvm_type_name(local ? local->type : IR_TYPE_UNSUPPORTED));
}

static void llvm_append_escaped_byte(ZBuf *buf, unsigned char ch) {
  if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') {
    zbuf_append_char(buf, (char)ch);
  } else {
    zbuf_appendf(buf, "\\%02X", (unsigned)ch);
  }
}

static void llvm_append_data_globals(ZBuf *buf, const IrProgram *program) {
  for (size_t i = 0; program && i < program->data_segment_len; i++) {
    const IrDataSegment *segment = &program->data_segments[i];
    zbuf_appendf(buf, "@.zero.data.%zu = private unnamed_addr constant [%u x i8] c\"", i, segment->len);
    for (unsigned j = 0; j < segment->len; j++) llvm_append_escaped_byte(buf, segment->bytes[j]);
    zbuf_append(buf, "\", align 1\n");
  }
  if (program && program->data_segment_len > 0) zbuf_append_char(buf, '\n');
}

static bool llvm_emit_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag);
static bool llvm_emit_call(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag);

static bool llvm_emit_trap_if_false(LlvmEmit *emit, const char *cond) {
  unsigned ok_label = llvm_label(emit), trap_label = llvm_label(emit);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", cond, ok_label, trap_label);
  llvm_emit_label(emit, trap_label);
  zbuf_append(emit->out, "  call void @llvm.trap()\n  unreachable\n");
  llvm_emit_label(emit, ok_label);
  return true;
}

static bool llvm_cast_value(LlvmEmit *emit, const IrValue *source, LlvmValue inner, IrTypeKind target, LlvmValue *out, ZDiag *diag) {
  if (inner.type == target) {
    *out = inner;
    return true;
  }
  unsigned from_bits = llvm_type_bits(inner.type);
  unsigned to_bits = llvm_type_bits(target);
  if (!from_bits || !to_bits) {
    llvm_set_diag(diag, emit->program, source ? source->line : 1, source ? source->column : 1, "LLVM IR cast uses unsupported type", "unsupported cast type", "lower");
    return false;
  }
  llvm_temp(emit, out, target);
  if (from_bits < to_bits) {
    zbuf_appendf(emit->out, "  %s = %s %s %s to %s\n", out->text, llvm_type_signed(inner.type) ? "sext" : "zext", llvm_type_name(inner.type), inner.text, llvm_type_name(target));
  } else if (from_bits > to_bits) {
    zbuf_appendf(emit->out, "  %s = trunc %s %s to %s\n", out->text, llvm_type_name(inner.type), inner.text, llvm_type_name(target));
  } else {
    zbuf_appendf(emit->out, "  %s = bitcast %s %s to %s\n", out->text, llvm_type_name(inner.type), inner.text, llvm_type_name(target));
  }
  return true;
}

static bool llvm_emit_usize_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue inner;
  if (!llvm_emit_value(emit, value, &inner, diag)) return false;
  if (!llvm_scalar_type_supported(inner.type) || inner.type == IR_TYPE_VOID) {
    llvm_set_diag(diag, emit->program, value ? value->line : 1, value ? value->column : 1, "LLVM IR backend index must be an integer scalar", "non-integer index", "lower");
    return false;
  }
  if (inner.type == IR_TYPE_USIZE) {
    *out = inner;
    return true;
  }
  return llvm_cast_value(emit, value, inner, IR_TYPE_USIZE, out, diag);
}

static bool llvm_make_byte_view(LlvmEmit *emit, const char *ptr, const char *len, IrTypeKind element_type, LlvmValue *out) {
  LlvmValue partial;
  llvm_temp(emit, &partial, IR_TYPE_BYTE_VIEW);
  zbuf_appendf(emit->out, "  %s = insertvalue { ptr, i64 } poison, ptr %s, 0\n", partial.text, ptr);
  llvm_temp(emit, out, IR_TYPE_BYTE_VIEW);
  zbuf_appendf(emit->out, "  %s = insertvalue { ptr, i64 } %s, i64 %s, 1\n", out->text, partial.text, len);
  out->element_type = element_type == IR_TYPE_UNSUPPORTED ? IR_TYPE_U8 : element_type;
  snprintf(out->ptr, sizeof(out->ptr), "%s", ptr);
  snprintf(out->len, sizeof(out->len), "%s", len);
  return true;
}

static bool llvm_ensure_byte_view_pair(LlvmEmit *emit, LlvmValue *value, const IrValue *source, ZDiag *diag) {
  if (!value || value->type != IR_TYPE_BYTE_VIEW) {
    llvm_set_diag(diag, emit->program, source ? source->line : 1, source ? source->column : 1, "LLVM IR backend expected a byte view", "non-byte-view value", "lower");
    return false;
  }
  if (value->ptr[0] && value->len[0]) return true;
  if (!value->text[0]) {
    llvm_set_diag(diag, emit->program, source ? source->line : 1, source ? source->column : 1, "LLVM IR backend byte view is missing an aggregate value", "missing byte-view aggregate", "emit");
    return false;
  }
  LlvmValue ptr, len;
  llvm_temp(emit, &ptr, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = extractvalue { ptr, i64 } %s, 0\n", ptr.text, value->text);
  llvm_temp(emit, &len, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = extractvalue { ptr, i64 } %s, 1\n", len.text, value->text);
  snprintf(value->ptr, sizeof(value->ptr), "%s", ptr.text);
  snprintf(value->len, sizeof(value->len), "%s", len.text);
  if (value->element_type == IR_TYPE_UNSUPPORTED) value->element_type = IR_TYPE_U8;
  return true;
}

static bool llvm_emit_byte_view(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  if (!value) {
    llvm_set_diag(diag, emit->program, 1, 1, "LLVM IR backend byte view is missing", "missing byte view", "emit");
    return false;
  }
  switch (value->kind) {
    case IR_VALUE_STRING_LITERAL: {
      size_t segment_index = 0;
      unsigned delta = 0;
      if (!llvm_find_data_segment(emit->program, value->data_offset, value->data_len, &segment_index, &delta)) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend string data segment is missing", "missing readonly data", "emit");
        return false;
      }
      const IrDataSegment *segment = &emit->program->data_segments[segment_index];
      LlvmValue ptr;
      llvm_temp(emit, &ptr, IR_TYPE_USIZE);
      zbuf_appendf(emit->out, "  %s = getelementptr inbounds [%u x i8], ptr @.zero.data.%zu, i64 0, i64 %u\n", ptr.text, segment->len, segment_index, delta);
      char len_text[32];
      snprintf(len_text, sizeof(len_text), "%u", value->data_len);
      return llvm_make_byte_view(emit, ptr.text, len_text, llvm_value_element_type(value), out);
    }
    case IR_VALUE_ARRAY_BYTE_VIEW: {
      if (value->array_index >= emit->fun->local_len) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend byte-view array local is out of range", "invalid array local", "emit");
        return false;
      }
      const IrLocal *local = &emit->fun->locals[value->array_index];
      if (!local->is_array || value->field_offset != 0 || !llvm_array_element_supported(local->element_type)) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend byte-view array requires a supported fixed-array local", "unsupported array view", "lower");
        return false;
      }
      LlvmValue ptr;
      llvm_temp(emit, &ptr, IR_TYPE_USIZE);
      zbuf_appendf(emit->out, "  %s = getelementptr inbounds [%u x %s], ptr %%slot%u, i64 0, i64 0\n", ptr.text, local->array_len, llvm_type_name(local->element_type), local->index);
      char len_text[32];
      snprintf(len_text, sizeof(len_text), "%u", value->data_len);
      return llvm_make_byte_view(emit, ptr.text, len_text, llvm_local_element_type(local), out);
    }
    case IR_VALUE_BYTE_SLICE: {
      LlvmValue base;
      if (!llvm_emit_byte_view(emit, value->left, &base, diag) || !llvm_ensure_byte_view_pair(emit, &base, value->left, diag)) return false;
      LlvmValue start = {.type = IR_TYPE_USIZE, .element_type = IR_TYPE_UNSUPPORTED, .text = "0"};
      LlvmValue end = {.type = IR_TYPE_USIZE, .element_type = IR_TYPE_UNSUPPORTED};
      if (value->index && !llvm_emit_usize_value(emit, value->index, &start, diag)) return false;
      if (value->right) {
        if (!llvm_emit_usize_value(emit, value->right, &end, diag)) return false;
      } else {
        snprintf(end.text, sizeof(end.text), "%s", base.len);
      }
      LlvmValue ordered, in_bounds, ok;
      llvm_temp(emit, &ordered, IR_TYPE_BOOL);
      zbuf_appendf(emit->out, "  %s = icmp ule i64 %s, %s\n", ordered.text, start.text, end.text);
      llvm_temp(emit, &in_bounds, IR_TYPE_BOOL);
      zbuf_appendf(emit->out, "  %s = icmp ule i64 %s, %s\n", in_bounds.text, end.text, base.len);
      llvm_temp(emit, &ok, IR_TYPE_BOOL);
      zbuf_appendf(emit->out, "  %s = and i1 %s, %s\n", ok.text, ordered.text, in_bounds.text);
      llvm_emit_trap_if_false(emit, ok.text);
      LlvmValue ptr, len;
      llvm_temp(emit, &ptr, IR_TYPE_USIZE);
      zbuf_appendf(emit->out, "  %s = getelementptr inbounds %s, ptr %s, i64 %s\n", ptr.text, llvm_type_name(base.element_type), base.ptr, start.text);
      llvm_temp(emit, &len, IR_TYPE_USIZE);
      zbuf_appendf(emit->out, "  %s = sub i64 %s, %s\n", len.text, end.text, start.text);
      return llvm_make_byte_view(emit, ptr.text, len.text, base.element_type, out);
    }
    case IR_VALUE_LOCAL: {
      const IrLocal *local = llvm_local(emit->fun, value->local_index);
      if (!local || local->type != IR_TYPE_BYTE_VIEW) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend byte-view local load uses unsupported type", "unsupported byte-view local", "lower");
        return false;
      }
      llvm_temp(emit, out, IR_TYPE_BYTE_VIEW);
      out->element_type = llvm_local_element_type(local);
      zbuf_appendf(emit->out, "  %s = load { ptr, i64 }, ptr %%slot%u, align %u\n", out->text, local->index, local->alignment ? local->alignment : 8);
      return true;
    }
    case IR_VALUE_CALL: {
      if (!llvm_emit_call(emit, value, out, diag)) return false;
      if (out->type != IR_TYPE_BYTE_VIEW) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend byte-view call returned a non-byte-view value", "non-byte-view call", "lower");
        return false;
      }
      if (out->element_type == IR_TYPE_UNSUPPORTED) out->element_type = llvm_value_element_type(value);
      return true;
    }
    default:
      llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend does not lower this byte-view source yet", "unsupported byte view", "lower");
      return false;
  }
}

static bool llvm_emit_array_element_ptr(LlvmEmit *emit, const IrLocal *local, const IrValue *index, int line, int column, LlvmValue *out, ZDiag *diag) {
  if (!local || !local->is_array || !llvm_array_element_supported(local->element_type)) {
    llvm_set_diag(diag, emit->program, line, column, "LLVM IR backend indexed access requires a supported fixed-array local", "unsupported array local", "lower");
    return false;
  }
  LlvmValue idx;
  if (!llvm_emit_usize_value(emit, index, &idx, diag)) return false;
  LlvmValue ok;
  llvm_temp(emit, &ok, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp ult i64 %s, %u\n", ok.text, idx.text, local->array_len);
  llvm_emit_trap_if_false(emit, ok.text);
  llvm_temp(emit, out, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = getelementptr inbounds [%u x %s], ptr %%slot%u, i64 0, i64 %s\n", out->text, local->array_len, llvm_type_name(local->element_type), local->index, idx.text);
  return true;
}

static bool llvm_emit_byte_view_element_ptr(LlvmEmit *emit, LlvmValue *view, const IrValue *index, int line, int column, LlvmValue *out, ZDiag *diag) {
  if (!llvm_ensure_byte_view_pair(emit, view, index, diag)) return false;
  LlvmValue idx;
  if (!llvm_emit_usize_value(emit, index, &idx, diag)) return false;
  LlvmValue ok;
  llvm_temp(emit, &ok, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp ult i64 %s, %s\n", ok.text, idx.text, view->len);
  llvm_emit_trap_if_false(emit, ok.text);
  llvm_temp(emit, out, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = getelementptr inbounds %s, ptr %s, i64 %s\n", out->text, llvm_type_name(view->element_type), view->ptr, idx.text);
  (void)line;
  (void)column;
  return true;
}

static bool llvm_emit_call(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  if (value->external_call) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend does not lower extern C calls yet", "extern C call", "lower");
    return false;
  }
  if (value->callee_index >= emit->program->function_len) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend call target is missing", "missing call target", "emit");
    return false;
  }
  LlvmValue args[32];
  if (value->arg_len > sizeof(args) / sizeof(args[0])) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend call has too many arguments", "call argument count", "lower");
    return false;
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (!llvm_emit_value(emit, value->args[i], &args[i], diag)) return false;
  }
  char *symbol = llvm_symbol_for_function(emit->program, value->callee_index);
  if (value->type == IR_TYPE_VOID) {
    zbuf_appendf(emit->out, "  call void @%s(", symbol);
  } else {
    llvm_temp(emit, out, value->type);
    zbuf_appendf(emit->out, "  %s = call %s @%s(", out->text, llvm_type_name(value->type), symbol);
  }
  for (size_t i = 0; i < value->arg_len; i++) {
    if (i > 0) zbuf_append(emit->out, ", ");
    zbuf_appendf(emit->out, "%s %s", llvm_type_name(args[i].type), args[i].text);
  }
  zbuf_append(emit->out, ")\n");
  if (value->type == IR_TYPE_VOID) {
    out->type = IR_TYPE_VOID;
    snprintf(out->text, sizeof(out->text), "void");
  }
  free(symbol);
  return true;
}

static bool llvm_emit_byte_view_len_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue view;
  if (!llvm_emit_byte_view(emit, value->left, &view, diag) || !llvm_ensure_byte_view_pair(emit, &view, value->left, diag)) return false;
  LlvmValue len = {.type = IR_TYPE_USIZE, .element_type = IR_TYPE_UNSUPPORTED};
  snprintf(len.text, sizeof(len.text), "%s", view.len);
  if (value->type == IR_TYPE_USIZE) {
    *out = len;
    return true;
  }
  return llvm_cast_value(emit, value, len, value->type, out, diag);
}

static bool llvm_emit_byte_view_index_load_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue view;
  if (!llvm_emit_byte_view(emit, value->left, &view, diag)) return false;
  view.element_type = llvm_value_element_type(value);
  LlvmValue ptr;
  if (!llvm_emit_byte_view_element_ptr(emit, &view, value->index, value->line, value->column, &ptr, diag)) return false;
  llvm_temp(emit, out, value->type);
  zbuf_appendf(emit->out, "  %s = load %s, ptr %s, align %u\n", out->text, llvm_type_name(value->type), ptr.text, llvm_type_bits(value->type) >= 8 ? llvm_type_bits(value->type) / 8 : 1);
  return true;
}

static bool llvm_emit_byte_copy_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue src, dst;
  if (!llvm_emit_byte_view(emit, value->left, &src, diag) ||
      !llvm_emit_byte_view(emit, value->right, &dst, diag) ||
      !llvm_ensure_byte_view_pair(emit, &src, value->left, diag) ||
      !llvm_ensure_byte_view_pair(emit, &dst, value->right, diag)) {
    return false;
  }
  LlvmValue src_shorter, len;
  llvm_temp(emit, &src_shorter, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp ult i64 %s, %s\n", src_shorter.text, src.len, dst.len);
  llvm_temp(emit, &len, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = select i1 %s, i64 %s, i64 %s\n", len.text, src_shorter.text, src.len, dst.len);
  zbuf_appendf(emit->out, "  call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %s, i64 %s, i1 false)\n", dst.ptr, src.ptr, len.text);
  if (value->type == IR_TYPE_USIZE) {
    *out = len;
    return true;
  }
  return llvm_cast_value(emit, value, len, value->type, out, diag);
}

static bool llvm_emit_byte_fill_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue fill, dst;
  if (!llvm_emit_value(emit, value->left, &fill, diag) ||
      !llvm_emit_byte_view(emit, value->right, &dst, diag) ||
      !llvm_ensure_byte_view_pair(emit, &dst, value->right, diag)) {
    return false;
  }
  if (fill.type != IR_TYPE_U8) {
    if (!llvm_cast_value(emit, value->left, fill, IR_TYPE_U8, &fill, diag)) return false;
  }
  zbuf_appendf(emit->out, "  call void @llvm.memset.p0.i64(ptr %s, i8 %s, i64 %s, i1 false)\n", dst.ptr, fill.text, dst.len);
  LlvmValue len = {.type = IR_TYPE_USIZE, .element_type = IR_TYPE_UNSUPPORTED};
  snprintf(len.text, sizeof(len.text), "%s", dst.len);
  if (value->type == IR_TYPE_USIZE) {
    *out = len;
    return true;
  }
  return llvm_cast_value(emit, value, len, value->type, out, diag);
}

static bool llvm_emit_byte_view_eq_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  LlvmValue left, right;
  if (!llvm_emit_byte_view(emit, value->left, &left, diag) ||
      !llvm_emit_byte_view(emit, value->right, &right, diag) ||
      !llvm_ensure_byte_view_pair(emit, &left, value->left, diag) ||
      !llvm_ensure_byte_view_pair(emit, &right, value->right, diag)) {
    return false;
  }
  if (left.element_type != right.element_type || !llvm_array_element_supported(left.element_type)) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend byte-view equality requires matching primitive element views", "mixed byte-view element types", "lower");
    return false;
  }
  unsigned loop_entry = llvm_label(emit), loop_label = llvm_label(emit), body_label = llvm_label(emit), true_label = llvm_label(emit), false_label = llvm_label(emit), end_label = llvm_label(emit);
  LlvmValue same_len;
  llvm_temp(emit, &same_len, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp eq i64 %s, %s\n", same_len.text, left.len, right.len);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", same_len.text, loop_entry, false_label);
  llvm_emit_label(emit, loop_entry);
  zbuf_appendf(emit->out, "  br label %%L%u\n", loop_label);
  llvm_emit_label(emit, loop_label);
  LlvmValue index, next_index, done;
  llvm_temp(emit, &index, IR_TYPE_USIZE);
  llvm_temp(emit, &next_index, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = phi i64 [0, %%L%u], [%s, %%L%u]\n", index.text, loop_entry, next_index.text, body_label);
  llvm_temp(emit, &done, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp eq i64 %s, %s\n", done.text, index.text, left.len);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", done.text, true_label, body_label);
  llvm_emit_label(emit, body_label);
  LlvmValue left_ptr, right_ptr, left_value, right_value, equal;
  llvm_temp(emit, &left_ptr, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = getelementptr inbounds %s, ptr %s, i64 %s\n", left_ptr.text, llvm_type_name(left.element_type), left.ptr, index.text);
  llvm_temp(emit, &right_ptr, IR_TYPE_USIZE);
  zbuf_appendf(emit->out, "  %s = getelementptr inbounds %s, ptr %s, i64 %s\n", right_ptr.text, llvm_type_name(right.element_type), right.ptr, index.text);
  llvm_temp(emit, &left_value, left.element_type);
  zbuf_appendf(emit->out, "  %s = load %s, ptr %s, align %u\n", left_value.text, llvm_type_name(left.element_type), left_ptr.text, llvm_type_bits(left.element_type) >= 8 ? llvm_type_bits(left.element_type) / 8 : 1);
  llvm_temp(emit, &right_value, right.element_type);
  zbuf_appendf(emit->out, "  %s = load %s, ptr %s, align %u\n", right_value.text, llvm_type_name(right.element_type), right_ptr.text, llvm_type_bits(right.element_type) >= 8 ? llvm_type_bits(right.element_type) / 8 : 1);
  llvm_temp(emit, &equal, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = icmp eq %s %s, %s\n", equal.text, llvm_type_name(left.element_type), left_value.text, right_value.text);
  zbuf_appendf(emit->out, "  %s = add i64 %s, 1\n", next_index.text, index.text);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", equal.text, loop_label, false_label);
  llvm_emit_label(emit, true_label);
  zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
  llvm_emit_label(emit, false_label);
  zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
  llvm_emit_label(emit, end_label);
  llvm_temp(emit, out, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = phi i1 [1, %%L%u], [0, %%L%u]\n", out->text, true_label, false_label);
  return true;
}

static bool llvm_emit_short_circuit_binary(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  bool is_and = value->binary_op == IR_BIN_AND;
  LlvmValue left;
  if (!llvm_emit_value(emit, value->left, &left, diag)) return false;
  if (left.type != IR_TYPE_BOOL) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend Boolean operator left operand must be Bool", "non-Bool short-circuit operand", "lower");
    return false;
  }
  unsigned rhs_label = llvm_label(emit), const_label = llvm_label(emit), end_label = llvm_label(emit);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", left.text, is_and ? rhs_label : const_label, is_and ? const_label : rhs_label);
  llvm_emit_label(emit, rhs_label);
  LlvmValue right;
  if (!llvm_emit_value(emit, value->right, &right, diag)) return false;
  if (right.type != IR_TYPE_BOOL) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend Boolean operator right operand must be Bool", "non-Bool short-circuit operand", "lower");
    return false;
  }
  unsigned rhs_end_label = emit->current_label;
  zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
  llvm_emit_label(emit, const_label);
  zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
  llvm_emit_label(emit, end_label);
  llvm_temp(emit, out, IR_TYPE_BOOL);
  zbuf_appendf(emit->out, "  %s = phi i1 [%s, %%L%u], [%u, %%L%u]\n", out->text, right.text, rhs_end_label, is_and ? 0u : 1u, const_label);
  return true;
}

static bool llvm_emit_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag) {
  if (!value) {
    llvm_set_diag(diag, emit->program, 1, 1, "LLVM IR backend value is missing", "missing value", "emit");
    return false;
  }
  if (!llvm_type_supported(value->type)) {
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend does not support this MIR value type", "unsupported MIR value type", "lower");
    return false;
  }
  switch (value->kind) {
    case IR_VALUE_INT:
      out->type = value->type;
      snprintf(out->text, sizeof(out->text), "%llu", value->int_value);
      return true;
    case IR_VALUE_BOOL:
      out->type = IR_TYPE_BOOL;
      snprintf(out->text, sizeof(out->text), "%u", value->int_value ? 1u : 0u);
      return true;
    case IR_VALUE_LOCAL: {
      const IrLocal *local = llvm_local(emit->fun, value->local_index);
      if (!local || local->is_array || !llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend local load uses unsupported type", "unsupported local", "lower");
        return false;
      }
      if (local->type == IR_TYPE_BYTE_VIEW) return llvm_emit_byte_view(emit, value, out, diag);
      llvm_temp(emit, out, local->type);
      zbuf_appendf(emit->out, "  %s = load %s, ptr %%slot%u, align %u\n", out->text, llvm_type_name(local->type), local->index, local->alignment ? local->alignment : 1);
      return true;
    }
    case IR_VALUE_CAST: {
      LlvmValue inner;
      if (!llvm_emit_value(emit, value->left, &inner, diag)) return false;
      return llvm_cast_value(emit, value, inner, value->type, out, diag);
    }
    case IR_VALUE_BINARY: {
      LlvmValue left, right;
      if (value->binary_op == IR_BIN_AND || value->binary_op == IR_BIN_OR) return llvm_emit_short_circuit_binary(emit, value, out, diag);
      if (!llvm_emit_value(emit, value->left, &left, diag)) return false;
      if (!llvm_emit_value(emit, value->right, &right, diag)) return false;
      if (left.type != right.type) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend binary operands must have matching types", "mixed operand types", "lower");
        return false;
      }
      const char *op = NULL;
      switch (value->binary_op) {
        case IR_BIN_ADD: op = "add"; break;
        case IR_BIN_SUB: op = "sub"; break;
        case IR_BIN_MUL: op = "mul"; break;
        case IR_BIN_DIV: op = llvm_type_signed(left.type) ? "sdiv" : "udiv"; break;
        case IR_BIN_MOD: op = llvm_type_signed(left.type) ? "srem" : "urem"; break;
        case IR_BIN_AND:
        case IR_BIN_OR: break;
      }
      llvm_temp(emit, out, value->type);
      zbuf_appendf(emit->out, "  %s = %s %s %s, %s\n", out->text, op, llvm_type_name(left.type), left.text, right.text);
      return true;
    }
    case IR_VALUE_COMPARE: {
      LlvmValue left, right;
      if (!llvm_emit_value(emit, value->left, &left, diag)) return false;
      if (!llvm_emit_value(emit, value->right, &right, diag)) return false;
      if (left.type != right.type) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend compare operands must have matching types", "mixed compare operand types", "lower");
        return false;
      }
      const char *pred = NULL;
      switch (value->compare_op) {
        case IR_CMP_EQ: pred = "eq"; break;
        case IR_CMP_NE: pred = "ne"; break;
        case IR_CMP_LT: pred = llvm_type_signed(left.type) ? "slt" : "ult"; break;
        case IR_CMP_LE: pred = llvm_type_signed(left.type) ? "sle" : "ule"; break;
        case IR_CMP_GT: pred = llvm_type_signed(left.type) ? "sgt" : "ugt"; break;
        case IR_CMP_GE: pred = llvm_type_signed(left.type) ? "sge" : "uge"; break;
      }
      llvm_temp(emit, out, IR_TYPE_BOOL);
      zbuf_appendf(emit->out, "  %s = icmp %s %s %s, %s\n", out->text, pred, llvm_type_name(left.type), left.text, right.text);
      return true;
    }
    case IR_VALUE_CALL:
      return llvm_emit_call(emit, value, out, diag);
    case IR_VALUE_INDEX_LOAD: {
      if (value->array_index >= emit->fun->local_len) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend indexed load array is out of range", "invalid array local", "emit");
        return false;
      }
      const IrLocal *local = &emit->fun->locals[value->array_index];
      LlvmValue ptr;
      if (!llvm_emit_array_element_ptr(emit, local, value->index, value->line, value->column, &ptr, diag)) return false;
      llvm_temp(emit, out, value->type);
      zbuf_appendf(emit->out, "  %s = load %s, ptr %s, align %u\n", out->text, llvm_type_name(value->type), ptr.text, llvm_type_bits(value->type) >= 8 ? llvm_type_bits(value->type) / 8 : 1);
      return true;
    }
    case IR_VALUE_STRING_LITERAL:
    case IR_VALUE_ARRAY_BYTE_VIEW:
    case IR_VALUE_BYTE_SLICE:
      return llvm_emit_byte_view(emit, value, out, diag);
    case IR_VALUE_BYTE_VIEW_LEN:
      return llvm_emit_byte_view_len_value(emit, value, out, diag);
    case IR_VALUE_BYTE_VIEW_INDEX_LOAD:
      return llvm_emit_byte_view_index_load_value(emit, value, out, diag);
    case IR_VALUE_BYTE_COPY:
      return llvm_emit_byte_copy_value(emit, value, out, diag);
    case IR_VALUE_BYTE_FILL:
      return llvm_emit_byte_fill_value(emit, value, out, diag);
    case IR_VALUE_BYTE_VIEW_EQ:
      return llvm_emit_byte_view_eq_value(emit, value, out, diag);
    default:
      llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend does not lower this MIR value yet", "unsupported MIR value", "lower");
      return false;
  }
}

static bool llvm_emit_instrs(LlvmEmit *emit, const IrInstr *instrs, size_t len, bool *terminated, ZDiag *diag);

static bool llvm_emit_world_write(LlvmEmit *emit, const IrInstr *instr, ZDiag *diag) {
  LlvmValue view;
  if (!llvm_emit_byte_view(emit, instr->value, &view, diag) || !llvm_ensure_byte_view_pair(emit, &view, instr->value, diag)) return false;
  LlvmValue len32_src = {.type = IR_TYPE_USIZE, .element_type = IR_TYPE_UNSUPPORTED};
  snprintf(len32_src.text, sizeof(len32_src.text), "%s", view.len);
  LlvmValue len32;
  if (!llvm_cast_value(emit, instr->value, len32_src, IR_TYPE_U32, &len32, diag)) return false;
  LlvmValue status, ok; llvm_temp(emit, &status, IR_TYPE_I32);
  zbuf_appendf(emit->out, "  %s = call i32 @zero_world_write(i32 %u, ptr %s, i32 %s)\n", status.text, instr->field_offset == 2 ? 2u : 1u, view.ptr, len32.text);
  llvm_temp(emit, &ok, IR_TYPE_BOOL); unsigned ok_label = llvm_label(emit), trap_label = llvm_label(emit);
  zbuf_appendf(emit->out, "  %s = icmp eq i32 %s, 0\n", ok.text, status.text);
  zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", ok.text, ok_label, trap_label);
  llvm_emit_label(emit, trap_label);
  zbuf_append(emit->out, "  call void @llvm.trap()\n  unreachable\n");
  llvm_emit_label(emit, ok_label);
  return true;
}

static bool llvm_emit_instr(LlvmEmit *emit, const IrInstr *instr, bool *terminated, ZDiag *diag) {
  if (*terminated) return true;
  switch (instr->kind) {
    case IR_INSTR_LOCAL_SET: {
      const IrLocal *local = llvm_local(emit->fun, instr->local_index);
      LlvmValue value;
      if (!local || local->is_array || !llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
        llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend local store uses unsupported type", "unsupported local store", "lower");
        return false;
      }
      if (!llvm_emit_value(emit, instr->value, &value, diag)) return false;
      if (value.type != local->type) {
        if (!llvm_cast_value(emit, instr->value, value, local->type, &value, diag)) return false;
      }
      zbuf_appendf(emit->out, "  store %s %s, ptr %%slot%u, align %u\n", llvm_type_name(local->type), value.text, local->index, local->alignment ? local->alignment : 1);
      return true;
    }
    case IR_INSTR_EXPR: {
      LlvmValue ignored;
      return llvm_emit_value(emit, instr->value, &ignored, diag);
    }
    case IR_INSTR_RETURN: {
      if (emit->fun->return_type == IR_TYPE_VOID) {
        zbuf_append(emit->out, "  ret void\n");
      } else {
        LlvmValue value;
        if (!llvm_emit_value(emit, instr->value, &value, diag)) return false;
        if (value.type != emit->fun->return_type) {
          if (!llvm_cast_value(emit, instr->value, value, emit->fun->return_type, &value, diag)) return false;
        }
        zbuf_appendf(emit->out, "  ret %s %s\n", llvm_type_name(emit->fun->return_type), value.text);
      }
      *terminated = true;
      return true;
    }
    case IR_INSTR_IF: {
      LlvmValue cond;
      if (!llvm_emit_value(emit, instr->value, &cond, diag)) return false;
      if (cond.type != IR_TYPE_BOOL) {
        llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend branch condition must be Bool", "non-Bool branch condition", "lower");
        return false;
      }
      unsigned then_label = llvm_label(emit), else_label = llvm_label(emit), end_label = llvm_label(emit);
      zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", cond.text, then_label, else_label);
      llvm_emit_label(emit, then_label);
      bool then_term = false;
      if (!llvm_emit_instrs(emit, instr->then_instrs, instr->then_len, &then_term, diag)) return false;
      if (!then_term) zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
      llvm_emit_label(emit, else_label);
      bool else_term = false;
      if (!llvm_emit_instrs(emit, instr->else_instrs, instr->else_len, &else_term, diag)) return false;
      if (!else_term) zbuf_appendf(emit->out, "  br label %%L%u\n", end_label);
      if (!then_term || !else_term) llvm_emit_label(emit, end_label);
      *terminated = then_term && else_term;
      return true;
    }
    case IR_INSTR_WHILE: {
      unsigned cond_label = llvm_label(emit), body_label = llvm_label(emit), end_label = llvm_label(emit);
      zbuf_appendf(emit->out, "  br label %%L%u\n", cond_label);
      llvm_emit_label(emit, cond_label);
      LlvmValue cond;
      if (!llvm_emit_value(emit, instr->value, &cond, diag)) return false;
      if (cond.type != IR_TYPE_BOOL) {
        llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend loop condition must be Bool", "non-Bool loop condition", "lower");
        return false;
      }
      zbuf_appendf(emit->out, "  br i1 %s, label %%L%u, label %%L%u\n", cond.text, body_label, end_label);
      llvm_emit_label(emit, body_label);
      bool body_term = false;
      if (!llvm_emit_instrs(emit, instr->then_instrs, instr->then_len, &body_term, diag)) return false;
      if (!body_term) zbuf_appendf(emit->out, "  br label %%L%u\n", cond_label);
      llvm_emit_label(emit, end_label);
      return true;
    }
    case IR_INSTR_WORLD_WRITE:
      return llvm_emit_world_write(emit, instr, diag);
    case IR_INSTR_INDEX_STORE: {
      if (instr->array_index >= emit->fun->local_len) {
        llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend indexed store array is out of range", "invalid array local", "emit");
        return false;
      }
      const IrLocal *local = &emit->fun->locals[instr->array_index];
      LlvmValue ptr, value;
      if (local->type == IR_TYPE_BYTE_VIEW) {
        LlvmValue view;
        IrValue view_ref = {.kind = IR_VALUE_LOCAL, .type = IR_TYPE_BYTE_VIEW, .local_index = local->index, .element_type = llvm_local_element_type(local), .line = instr->line, .column = instr->column};
        if (!llvm_emit_byte_view(emit, &view_ref, &view, diag) ||
            !llvm_emit_byte_view_element_ptr(emit, &view, instr->index, instr->line, instr->column, &ptr, diag)) return false;
      } else if (!llvm_emit_array_element_ptr(emit, local, instr->index, instr->line, instr->column, &ptr, diag)) {
        return false;
      }
      IrTypeKind element_type = local->type == IR_TYPE_BYTE_VIEW ? llvm_local_element_type(local) : local->element_type;
      if (!llvm_emit_value(emit, instr->value, &value, diag)) return false;
      if (value.type != element_type) {
        if (!llvm_cast_value(emit, instr->value, value, element_type, &value, diag)) return false;
      }
      zbuf_appendf(emit->out, "  store %s %s, ptr %s, align %u\n", llvm_type_name(element_type), value.text, ptr.text, llvm_type_bits(element_type) >= 8 ? llvm_type_bits(element_type) / 8 : 1);
      return true;
    }
    case IR_INSTR_RAISE:
    case IR_INSTR_FIELD_STORE:
      llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend does not lower this MIR instruction yet", "unsupported MIR instruction", "lower");
      return false;
  }
  llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend instruction kind is unknown", "unknown MIR instruction", "emit");
  return false;
}

static bool llvm_emit_instrs(LlvmEmit *emit, const IrInstr *instrs, size_t len, bool *terminated, ZDiag *diag) {
  *terminated = false;
  for (size_t i = 0; i < len; i++) {
    if (!llvm_emit_instr(emit, &instrs[i], terminated, diag)) return false;
  }
  return true;
}

static bool llvm_validate_function(const IrProgram *program, const IrFunction *fun, ZDiag *diag) {
  if (fun->is_exported && llvm_name_is_reserved_runtime_symbol(fun->name)) {
    llvm_set_diag(diag, program, fun->line, fun->column, "LLVM IR backend export collides with a reserved runtime symbol", fun->name ? fun->name : "reserved runtime symbol", "lower");
    return false;
  }
  if (!llvm_type_supported(fun->return_type)) {
    llvm_set_diag(diag, program, fun->line, fun->column, "LLVM IR backend return type is unsupported", "unsupported return type", "lower");
    return false;
  }
  if (fun->raises) {
    llvm_set_diag(diag, program, fun->line, fun->column, "LLVM IR backend does not lower fallible function ABI yet", "fallible function", "lower");
    return false;
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (local->is_record || local->type == IR_TYPE_ALLOC || local->type == IR_TYPE_VEC ||
        local->type == IR_TYPE_MAYBE_BYTE_VIEW || local->type == IR_TYPE_MAYBE_SCALAR) {
      llvm_set_diag(diag, program, local->line, local->column, "LLVM IR backend local type is unsupported", "unsupported local type", "lower");
      return false;
    }
    if (local->is_array) {
      if (local->is_param || !llvm_array_element_supported(local->element_type)) {
        llvm_set_diag(diag, program, local->line, local->column, "LLVM IR backend fixed-array local type is unsupported", "unsupported fixed-array local", "lower");
        return false;
      }
      continue;
    }
    if (!llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
      llvm_set_diag(diag, program, local->line, local->column, "LLVM IR backend local type is unsupported", "unsupported local type", "lower");
      return false;
    }
  }
  return true;
}

static bool llvm_emit_function(const IrProgram *program, unsigned index, ZBuf *out, ZDiag *diag) {
  const IrFunction *fun = &program->functions[index];
  if (!llvm_validate_function(program, fun, diag)) return false;
  char *symbol = llvm_symbol_for_function(program, index);
  zbuf_appendf(out, "define %s @%s(", llvm_type_name(fun->return_type), symbol);
  free(symbol);
  size_t written_params = 0;
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (!local->is_param) continue;
    if (written_params++ > 0) zbuf_append(out, ", ");
    zbuf_appendf(out, "%s %%p%u", llvm_type_name(local->type), local->index);
  }
  zbuf_append(out, ") {\n");
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    char storage_type[64];
    llvm_format_local_storage_type(local, storage_type, sizeof(storage_type));
    zbuf_appendf(out, "  %%slot%u = alloca %s, align %u\n", local->index, storage_type, local->alignment ? local->alignment : 1);
  }
  for (size_t i = 0; i < fun->local_len; i++) {
    const IrLocal *local = &fun->locals[i];
    if (!local->is_param) continue;
    zbuf_appendf(out, "  store %s %%p%u, ptr %%slot%u, align %u\n", llvm_type_name(local->type), local->index, local->index, local->alignment ? local->alignment : 1);
  }
  LlvmEmit emit = {.program = program, .fun = fun, .out = out};
  bool terminated = false;
  if (!llvm_emit_instrs(&emit, fun->instrs, fun->instr_len, &terminated, diag)) return false;
  if (!terminated) {
    if (fun->return_type == IR_TYPE_VOID) zbuf_append(out, "  ret void\n");
    else {
      llvm_set_diag(diag, program, fun->line, fun->column, "LLVM IR backend function did not terminate with a return", "missing return", "emit");
      return false;
    }
  }
  zbuf_append(out, "}\n\n");
  return true;
}

bool z_emit_llvm_ir_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag) {
  if (!program || !out) {
    llvm_set_diag(diag, program, 1, 1, "LLVM IR backend input is missing", "missing MIR", "emit");
    return false;
  }
  zbuf_init(out);
  zbuf_append(out, "; zero llvm-ir v1\n");
  zbuf_appendf(out, "target triple = \"%s\"\n\n", z_llvm_target_triple(program->target));
  llvm_append_data_globals(out, program);
  zbuf_append(out, "declare void @llvm.trap()\n");
  zbuf_append(out, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
  zbuf_append(out, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
  zbuf_append(out, "declare i32 @zero_world_write(i32, ptr, i32)\n\n");
  for (unsigned i = 0; i < program->function_len; i++) {
    if (!llvm_emit_function(program, i, out, diag)) {
      zbuf_free(out);
      return false;
    }
  }
  return true;
}
