#include "zero.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  IrTypeKind type;
  char text[96];
} LlvmValue;

typedef struct {
  const IrProgram *program;
  const IrFunction *fun;
  ZBuf *out;
  unsigned temp_index;
  unsigned label_index;
  unsigned current_label;
} LlvmEmit;

static bool llvm_type_supported(IrTypeKind type) {
  return type == IR_TYPE_VOID || type == IR_TYPE_BOOL || type == IR_TYPE_U8 ||
         type == IR_TYPE_U16 || type == IR_TYPE_I32 || type == IR_TYPE_U32 ||
         type == IR_TYPE_I64 || type == IR_TYPE_U64 || type == IR_TYPE_USIZE;
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

static void llvm_set_diag(ZDiag *diag, const IrProgram *program, int line, int column, const char *message, const char *actual, const char *stage) {
  if (!diag) return;
  memset(diag, 0, sizeof(*diag));
  diag->code = 2004;
  diag->line = line > 0 ? line : 1;
  diag->column = column > 0 ? column : 1;
  diag->length = 1;
  snprintf(diag->message, sizeof(diag->message), "%s", message ? message : "LLVM IR backend cannot lower MIR");
  snprintf(diag->expected, sizeof(diag->expected), "LLVM IR scalar MIR subset");
  snprintf(diag->actual, sizeof(diag->actual), "%s", actual ? actual : "unsupported MIR");
  snprintf(diag->help, sizeof(diag->help), "use --backend llvm --emit llvm-ir only for scalar functions, direct calls, branches, loops, and readonly string writes");
  z_backend_blocker_set(&diag->backend_blocker,
                        program && program->target && program->target->name ? program->target->name : "unknown",
                        program && program->target && program->target->object_format ? program->target->object_format : "unknown",
                        "llvm",
                        stage ? stage : "lower",
                        actual ? actual : "unsupported MIR");
}

static void llvm_temp(LlvmEmit *emit, LlvmValue *value, IrTypeKind type) {
  value->type = type;
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

static const char *llvm_target_triple(const ZTargetInfo *target) {
  if (!target || !target->name) return "unknown-unknown-unknown";
  if (strcmp(target->name, "darwin-arm64") == 0) return "arm64-apple-darwin";
  if (strcmp(target->name, "darwin-x64") == 0) return "x86_64-apple-darwin";
  if (strcmp(target->name, "linux-arm64") == 0) return "aarch64-unknown-linux-gnu";
  if (strcmp(target->name, "linux-x64") == 0) return "x86_64-unknown-linux-gnu";
  if (strcmp(target->name, "linux-musl-arm64") == 0) return "aarch64-unknown-linux-musl";
  if (strcmp(target->name, "linux-musl-x64") == 0) return "x86_64-unknown-linux-musl";
  if (strcmp(target->name, "win32-arm64.exe") == 0) return "aarch64-pc-windows-msvc";
  if (strcmp(target->name, "win32-x64.exe") == 0) return "x86_64-pc-windows-msvc";
  return "unknown-unknown-unknown";
}

static bool llvm_emit_value(LlvmEmit *emit, const IrValue *value, LlvmValue *out, ZDiag *diag);

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
    llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend supports only scalar values in this pass", "non-scalar MIR value", "lower");
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
      if (!local || !llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
        llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend local load uses unsupported type", "unsupported local", "lower");
        return false;
      }
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
    default:
      llvm_set_diag(diag, emit->program, value->line, value->column, "LLVM IR backend does not lower this MIR value yet", "unsupported MIR value", "lower");
      return false;
  }
}

static bool llvm_emit_instrs(LlvmEmit *emit, const IrInstr *instrs, size_t len, bool *terminated, ZDiag *diag);

static bool llvm_emit_world_write(LlvmEmit *emit, const IrInstr *instr, ZDiag *diag) {
  const IrValue *view = instr->value;
  if (!view || view->kind != IR_VALUE_STRING_LITERAL) {
    llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend world writes currently require a readonly string literal", "non-literal world write", "lower");
    return false;
  }
  size_t segment_index = 0;
  unsigned delta = 0;
  if (!llvm_find_data_segment(emit->program, view->data_offset, view->data_len, &segment_index, &delta)) {
    llvm_set_diag(diag, emit->program, instr->line, instr->column, "LLVM IR backend string data segment is missing", "missing readonly data", "emit");
    return false;
  }
  LlvmValue ptr; llvm_temp(emit, &ptr, IR_TYPE_U64);
  const IrDataSegment *segment = &emit->program->data_segments[segment_index];
  zbuf_appendf(emit->out, "  %s = getelementptr inbounds [%u x i8], ptr @.zero.data.%zu, i64 0, i64 %u\n", ptr.text, segment->len, segment_index, delta);
  LlvmValue status, ok; llvm_temp(emit, &status, IR_TYPE_I32);
  zbuf_appendf(emit->out, "  %s = call i32 @zero_world_write(i32 %u, ptr %s, i64 %u)\n", status.text, instr->field_offset == 2 ? 2u : 1u, ptr.text, view->data_len);
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
      if (!local || !llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
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
    case IR_INSTR_RAISE:
    case IR_INSTR_INDEX_STORE:
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
    if (local->is_array || local->is_record || !llvm_type_supported(local->type) || local->type == IR_TYPE_VOID) {
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
    zbuf_appendf(out, "  %%slot%u = alloca %s, align %u\n", local->index, llvm_type_name(local->type), local->alignment ? local->alignment : 1);
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
  zbuf_appendf(out, "target triple = \"%s\"\n\n", llvm_target_triple(program->target));
  llvm_append_data_globals(out, program);
  if (program->data_segment_len > 0) zbuf_append(out, "declare i32 @zero_world_write(i32, ptr, i64)\ndeclare void @llvm.trap()\n\n");
  for (unsigned i = 0; i < program->function_len; i++) {
    if (!llvm_emit_function(program, i, out, diag)) {
      zbuf_free(out);
      return false;
    }
  }
  return true;
}
