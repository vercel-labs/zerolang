#ifndef ZERO_C_ZERO_H
#define ZERO_C_ZERO_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} ZBuf;

#define Z_BORROW_TRACE_MAX 16

typedef struct {
  char root[128];
  char path[256];
  char kind[16];
  char binding[128];
  const char *binding_decl_path;
  int binding_line;
  int binding_column;
} ZBorrowTrace;

typedef struct {
  bool present;
  char target[64];
  char object_format[32];
  char backend[64];
  char stage[32];
  char unsupported_feature[128];
} ZBackendBlocker;

typedef struct {
  int code;
  char code_text[16];
  char message[256];
  char expected[128];
  char actual[128];
  char help[256];
  ZBorrowTrace borrow_traces[Z_BORROW_TRACE_MAX];
  size_t borrow_trace_count;
  bool borrow_trace_truncated;
  char borrow_repair[256];
  ZBackendBlocker backend_blocker;
  const char *path;
  int line;
  int column;
  int length;
} ZDiag;

typedef enum {
  TOK_IDENT,
  TOK_KEYWORD,
  TOK_STRING,
  TOK_CHAR,
  TOK_NUMBER,
  TOK_SYMBOL,
  TOK_EOF
} TokenKind;

typedef struct {
  TokenKind kind;
  char *text;
  int line;
  int column;
  size_t offset;
  size_t length;
} Token;

typedef struct {
  Token *items;
  size_t len;
  size_t cap;
} TokenVec;

typedef enum {
  Z_ROW_TOKEN_WORD,
  Z_ROW_TOKEN_STRING,
  Z_ROW_TOKEN_CHAR,
  Z_ROW_TOKEN_NUMBER,
  Z_ROW_TOKEN_SYMBOL,
  Z_ROW_TOKEN_COMMENT,
  Z_ROW_TOKEN_NEWLINE,
  Z_ROW_TOKEN_INDENT,
  Z_ROW_TOKEN_DEDENT,
  Z_ROW_TOKEN_EOF
} ZRowTokenKind;

typedef struct {
  ZRowTokenKind kind;
  char *text;
  int line;
  int column;
  size_t offset;
  size_t length;
} ZRowToken;

typedef struct {
  ZRowToken *items;
  size_t len;
  size_t cap;
} ZRowTokenVec;

typedef struct {
  size_t row_count;
  size_t comment_count;
  size_t blank_line_count;
  size_t max_indent_depth;
} ZRowSyntaxFacts;

#define Z_ROW_NO_PARENT ((size_t)-1)

typedef enum {
  Z_ROW_TRIVIA_LEADING_COMMENT,
  Z_ROW_TRIVIA_TRAILING_COMMENT,
  Z_ROW_TRIVIA_BLOCK_COMMENT,
  Z_ROW_TRIVIA_BLANK_LINE
} ZRowTriviaKind;

typedef struct {
  ZRowTriviaKind kind;
  size_t row;
  size_t parent;
  size_t token;
  size_t indent_depth;
  int line;
  int column;
} ZRowTrivia;

typedef struct {
  size_t parent;
  size_t first_token;
  size_t token_count;
  size_t indent_depth;
  int line;
  int column;
} ZRowNode;

typedef struct {
  ZRowNode *items;
  size_t len;
  size_t cap;
  ZRowTrivia *trivia;
  size_t trivia_len;
  size_t trivia_cap;
} ZRowTree;

typedef enum {
  EXPR_IDENT,
  EXPR_STRING,
  EXPR_CHAR,
  EXPR_NUMBER,
  EXPR_BOOL,
  EXPR_NULL,
  EXPR_MEMBER,
  EXPR_INDEX,
  EXPR_SLICE,
  EXPR_CALL,
  EXPR_BINARY,
  EXPR_CAST,
  EXPR_BORROW,
  EXPR_CHECK,
  EXPR_RESCUE,
  EXPR_META,
  EXPR_SHAPE_LITERAL,
  EXPR_ARRAY_LITERAL
} ExprKind;

typedef struct Expr Expr;
typedef struct TypeArg TypeArg;

typedef struct {
  char *name;
  Expr *value;
  int line;
  int column;
} FieldInit;

typedef struct {
  FieldInit *items;
  size_t len;
  size_t cap;
} FieldInitVec;

typedef struct {
  Expr **items;
  size_t len;
  size_t cap;
} ExprVec;

struct TypeArg {
  char *type;
  int line;
  int column;
};

typedef struct {
  TypeArg *items;
  size_t len;
  size_t cap;
} TypeArgVec;

struct Expr {
  ExprKind kind;
  char *text;
  char *resolved_type;
  bool moves_ownership;
  bool mutable_borrow;
  bool bool_value;
  bool array_repeat;
  Expr *left;
  Expr *right;
  ExprVec args;
  TypeArgVec type_args;
  TypeArgVec checked_type_args;
  FieldInitVec fields;
  int line;
  int column;
};

typedef enum {
  STMT_LET,
  STMT_ASSIGN,
  STMT_DEFER,
  STMT_CHECK,
  STMT_RETURN,
  STMT_EXPR,
  STMT_IF,
  STMT_WHILE,
  STMT_FOR,
  STMT_BREAK,
  STMT_CONTINUE,
  STMT_MATCH,
  STMT_RAISE
} StmtKind;

typedef struct Stmt Stmt;
typedef struct MatchArm MatchArm;

typedef struct {
  char *name;
  char *type;
  Expr *default_value;
  bool is_static;
  int line;
  int column;
} Param;

typedef struct {
  Param *items;
  size_t len;
  size_t cap;
} ParamVec;

typedef struct {
  Stmt **items;
  size_t len;
  size_t cap;
} StmtVec;

struct MatchArm {
  char *case_name;
  char *range_end;
  char *payload_name;
  Expr *guard;
  StmtVec body;
  int line;
  int column;
};

typedef struct {
  MatchArm *items;
  size_t len;
  size_t cap;
} MatchArmVec;

struct Stmt {
  StmtKind kind;
  char *name;
  char *type;
  char *resolved_type;
  bool mutable_binding;
  Expr *target;
  Expr *expr;
  Expr *range_end;
  StmtVec then_body;
  StmtVec else_body;
  MatchArmVec match_arms;
  int line;
  int column;
};

typedef struct {
  char *name;
  char *test_name;
  char *return_type;
  ParamVec type_params;
  ParamVec params;
  bool is_public;
  bool raises;
  bool has_error_set;
  ParamVec errors;
  bool is_test;
  bool export_c;
  StmtVec body;
  int line;
  int column;
} Function;

typedef struct {
  Function *items;
  size_t len;
  size_t cap;
} FunctionVec;

typedef struct {
  char *name;
  char *layout;
  ParamVec type_params;
  ParamVec fields;
  FunctionVec methods;
  bool is_public;
  int line;
  int column;
} Shape;

typedef struct {
  Shape *items;
  size_t len;
  size_t cap;
} ShapeVec;

typedef struct {
  char *name;
  ParamVec type_params;
  FunctionVec methods;
  bool is_public;
  int line;
  int column;
} InterfaceDecl;

typedef struct {
  InterfaceDecl *items;
  size_t len;
  size_t cap;
} InterfaceVec;

typedef struct {
  char *name;
  char *type;
  ParamVec cases;
  int line;
  int column;
} EnumDecl;

typedef struct {
  EnumDecl *items;
  size_t len;
  size_t cap;
} EnumVec;

typedef struct {
  char *name;
  ParamVec cases;
  int line;
  int column;
} Choice;

typedef struct {
  Choice *items;
  size_t len;
  size_t cap;
} ChoiceVec;

typedef struct {
  char *name;
  char *type;
  Expr *expr;
  bool is_public;
  int line;
  int column;
} ConstDecl;

typedef struct {
  ConstDecl *items;
  size_t len;
  size_t cap;
} ConstVec;

typedef struct {
  char *name;
  char *target;
  bool is_public;
  int line;
  int column;
} TypeAlias;

typedef struct {
  TypeAlias *items;
  size_t len;
  size_t cap;
} TypeAliasVec;

typedef struct {
  char *header;
  char *alias;
  int line;
  int column;
} CImport;

typedef struct {
  CImport *items;
  size_t len;
  size_t cap;
} CImportVec;

typedef struct {
  char *module;
  char *alias;
  int line;
  int column;
  int end_column;
} UseImport;

typedef struct {
  UseImport *items;
  size_t len;
  size_t cap;
} UseImportVec;

typedef struct {
  UseImportVec use_imports;
  CImportVec c_imports;
  ConstVec consts;
  TypeAliasVec aliases;
  InterfaceVec interfaces;
  ShapeVec shapes;
  EnumVec enums;
  ChoiceVec choices;
  FunctionVec functions;
} Program;

typedef enum {
  IR_TYPE_UNSUPPORTED,
  IR_TYPE_VOID,
  IR_TYPE_BOOL,
  IR_TYPE_U8,
  IR_TYPE_U16,
  IR_TYPE_USIZE,
  IR_TYPE_I32,
  IR_TYPE_U32,
  IR_TYPE_I64,
  IR_TYPE_U64,
  IR_TYPE_BYTE_VIEW,
  IR_TYPE_ALLOC,
  IR_TYPE_VEC,
  IR_TYPE_MAYBE_BYTE_VIEW,
  IR_TYPE_MAYBE_SCALAR,
  IR_TYPE_RECORD
} IrTypeKind;

typedef enum {
  IR_ERROR_NONE = 0,
  IR_ERROR_UNKNOWN = 1,
  IR_ERROR_NOT_FOUND = 2,
  IR_ERROR_TOO_LARGE = 3,
  IR_ERROR_IO = 4
} IrErrorCode;

typedef enum {
  IR_VALUE_INT,
  IR_VALUE_BOOL,
  IR_VALUE_LOCAL,
  IR_VALUE_BINARY,
  IR_VALUE_COMPARE,
  IR_VALUE_CALL,
  IR_VALUE_INDEX_LOAD,
  IR_VALUE_STRING_LITERAL,
  IR_VALUE_ARRAY_BYTE_VIEW,
  IR_VALUE_BYTE_SLICE,
  IR_VALUE_BYTE_VIEW_LEN,
  IR_VALUE_BYTE_VIEW_INDEX_LOAD,
  IR_VALUE_BYTE_VIEW_EQ,
  IR_VALUE_BYTE_COPY,
  IR_VALUE_BYTE_FILL,
  IR_VALUE_CRC32_BYTES,
  IR_VALUE_FIXED_BUF_ALLOC,
  IR_VALUE_VEC_INIT,
  IR_VALUE_VEC_PUSH,
  IR_VALUE_VEC_LEN,
  IR_VALUE_VEC_CAPACITY,
  IR_VALUE_ALLOC_BYTES,
  IR_VALUE_MAYBE_HAS,
  IR_VALUE_MAYBE_VALUE,
  IR_VALUE_MAYBE_SCALAR_LITERAL,
  IR_VALUE_ARGS_LEN,
  IR_VALUE_ARGS_GET,
  IR_VALUE_ENV_GET,
  IR_VALUE_TIME_WALL_SECONDS,
  IR_VALUE_TIME_MONOTONIC,
  IR_VALUE_TIME_AS_MS,
  IR_VALUE_RAND_NEXT_U32,
  IR_VALUE_RAND_ENTROPY_U32,
  IR_VALUE_FS_HOST,
  IR_VALUE_FS_OPEN,
  IR_VALUE_FS_CREATE,
  IR_VALUE_FS_READ_PATH,
  IR_VALUE_FS_WRITE_PATH,
  IR_VALUE_FS_READ_BYTES_PATH,
  IR_VALUE_FS_WRITE_BYTES_PATH,
  IR_VALUE_FS_READ_ALL,
  IR_VALUE_FS_READ_FILE,
  IR_VALUE_FS_WRITE_ALL_FILE,
  IR_VALUE_FS_CLOSE_FILE,
  IR_VALUE_FS_EXISTS,
  IR_VALUE_FS_REMOVE,
  IR_VALUE_FS_RENAME,
  IR_VALUE_FS_FILE_LEN,
  IR_VALUE_FS_MAKE_DIR,
  IR_VALUE_FS_REMOVE_DIR,
  IR_VALUE_FS_IS_DIR,
  IR_VALUE_FS_DIR_ENTRY_COUNT,
  IR_VALUE_FS_TEMP_NAME,
  IR_VALUE_FS_ATOMIC_WRITE,
  IR_VALUE_JSON_PARSE_BYTES,
  IR_VALUE_JSON_VALIDATE_BYTES,
  IR_VALUE_JSON_STREAM_TOKENS_BYTES,
  IR_VALUE_HTTP_FETCH,
  IR_VALUE_HTTP_RESULT_OK,
  IR_VALUE_HTTP_RESULT_STATUS,
  IR_VALUE_HTTP_RESULT_BODY_LEN,
  IR_VALUE_HTTP_RESULT_ERROR,
  IR_VALUE_HTTP_RESPONSE_LEN,
  IR_VALUE_HTTP_RESPONSE_HEADERS_LEN,
  IR_VALUE_HTTP_RESPONSE_BODY_OFFSET,
  IR_VALUE_HTTP_HEADER_VALUE,
  IR_VALUE_HTTP_HEADER_FOUND,
  IR_VALUE_HTTP_HEADER_OFFSET,
  IR_VALUE_HTTP_HEADER_LEN,
  IR_VALUE_FIELD_LOAD,
  IR_VALUE_CHECK,
  IR_VALUE_RESCUE
} IrValueKind;

typedef enum {
  IR_BIN_ADD,
  IR_BIN_SUB,
  IR_BIN_MUL,
  IR_BIN_DIV,
  IR_BIN_MOD,
  IR_BIN_AND,
  IR_BIN_OR
} IrBinaryOp;

typedef enum {
  IR_CMP_EQ,
  IR_CMP_NE,
  IR_CMP_LT,
  IR_CMP_LE,
  IR_CMP_GT,
  IR_CMP_GE
} IrCompareOp;

typedef struct IrValue IrValue;
typedef struct IrInstr IrInstr;

struct IrValue {
  IrValueKind kind;
  IrTypeKind type;
  unsigned long long int_value;
  unsigned local_index;
  unsigned callee_index;
  unsigned array_index;
  unsigned field_offset;
  unsigned data_offset;
  unsigned data_len;
  IrTypeKind element_type;
  unsigned error_code;
  IrBinaryOp binary_op;
  IrCompareOp compare_op;
  IrValue **args;
  size_t arg_len;
  size_t arg_cap;
  IrValue *index;
  IrValue *left;
  IrValue *right;
  int line;
  int column;
};

typedef enum {
  IR_INSTR_LOCAL_SET,
  IR_INSTR_INDEX_STORE,
  IR_INSTR_FIELD_STORE,
  IR_INSTR_WORLD_WRITE,
  IR_INSTR_RAISE,
  IR_INSTR_EXPR,
  IR_INSTR_RETURN,
  IR_INSTR_IF,
  IR_INSTR_WHILE
} IrInstrKind;

struct IrInstr {
  IrInstrKind kind;
  unsigned local_index;
  unsigned array_index;
  unsigned field_offset;
  unsigned error_code;
  IrValue *value;
  IrValue *index;
  IrInstr *then_instrs;
  size_t then_len;
  size_t then_cap;
  IrInstr *else_instrs;
  size_t else_len;
  size_t else_cap;
  int line;
  int column;
};

typedef struct {
  char *name;
  IrTypeKind type;
  IrTypeKind element_type;
  unsigned index;
  unsigned frame_offset;
  unsigned array_len;
  unsigned field_offset;
  unsigned byte_size;
  unsigned alignment;
  bool is_param;
  bool is_array;
  bool is_record;
  bool is_mutable;
  char *shape_name;
  int line;
  int column;
} IrLocal;

typedef struct {
  unsigned offset;
  unsigned len;
  unsigned char *bytes;
} IrDataSegment;

typedef struct {
  char *name;
  char *stable_id;
  char *world_param_name;
  IrTypeKind return_type;
  IrTypeKind value_return_type;
  IrLocal *locals;
  size_t local_len;
  size_t local_cap;
  size_t param_count;
  IrInstr *instrs;
  size_t instr_len;
  size_t instr_cap;
  size_t frame_bytes;
  bool is_exported;
  bool raises;
  int line;
  int column;
} IrFunction;

typedef struct {
  Program program;
  IrFunction *functions;
  size_t function_len;
  size_t function_cap;
  IrDataSegment *data_segments;
  size_t data_segment_len;
  size_t data_segment_cap;
  size_t readonly_data_bytes;
  bool mir_valid;
  char mir_expected[128];
  char mir_actual[128];
  char mir_message[256];
  char mir_help[256];
  ZBackendBlocker backend_blocker;
  int mir_line;
  int mir_column;
  size_t mir_bytes;
  size_t direct_function_count;
  size_t direct_export_count;
  size_t direct_stack_bytes;
  size_t direct_max_frame_bytes;
  size_t direct_readonly_data_bytes;
  size_t direct_allocator_helper_count;
  size_t direct_buffer_helper_count;
  size_t direct_runtime_helper_count;
  size_t direct_host_runtime_import_count;
  size_t direct_http_runtime_import_count;
} IrProgram;

typedef struct {
  char *name;
  char *version;
  char *path;
  char *resolved_manifest;
  char *resolved_name;
  char *resolved_version;
  char *targets_json;
  char *status;
  unsigned long long fingerprint;
  bool direct;
} SourceDependency;

typedef struct {
  char *source_file;
  char *source;
  char *package_root;
  char *manifest_path;
  char *package_name;
  char *package_version;
  char *lockfile_path;
  unsigned long long manifest_hash;
  unsigned long long dependency_graph_hash;
  unsigned long long lockfile_hash;
  char **source_files;
  char **imports;
  char **module_names;
  char **module_paths;
  char **import_from;
  char **import_to;
  char **import_paths;
  char **import_source_paths;
  int *import_lines;
  int *import_columns;
  int *import_lengths;
  char **symbol_names;
  char **symbol_modules;
  char **symbol_kinds;
  char **source_line_paths;
  int *source_line_numbers;
  SourceDependency *dependencies;
  bool *symbol_public;
  size_t source_file_count;
  size_t import_count;
  size_t module_count;
  size_t import_edge_count;
  size_t symbol_count;
  size_t source_line_count;
  size_t dependency_count;
  long long resolve_ms;
  long long parse_ms;
  long long interface_ms;
  long long check_ms;
  long long lower_ms;
  long long codegen_ms;
  long long object_ms;
  long long link_ms;
  size_t lowered_ir_bytes;
  size_t direct_function_count;
  size_t direct_export_count;
  size_t direct_stack_bytes;
  size_t direct_max_frame_bytes;
  size_t direct_readonly_data_bytes;
  size_t direct_allocator_helper_count;
  size_t direct_buffer_helper_count;
  size_t direct_runtime_helper_count;
  size_t direct_host_runtime_import_count;
  size_t direct_http_runtime_import_count;
  bool parse_cache_hit;
  bool interface_cache_hit;
  bool check_cache_hit;
  bool specialization_cache_hit;
  bool emitted_object_cache_hit;
} SourceInput;

typedef struct {
  char *name;
  char *version;
  char *path;
  char *targets_json;
} ZManifestDependency;

typedef struct {
  char *name;
  char *headers_json;
  char *include_json;
  char *lib_json;
  char *link_json;
  char *mode;
  char *pkg_config;
} ZManifestCLib;

typedef struct {
  char *package_name;
  char *package_version;
  char *main_path;
  char *kind;
  ZManifestDependency *dependencies;
  ZManifestCLib *c_libs;
  size_t dependency_count;
  size_t c_lib_count;
} ZManifest;

typedef struct {
  const char *name;
  const char *aliases;
  const char *os;
  const char *arch;
  const char *abi;
  const char *libc;
  const char *libc_mode;
  const char *exe_suffix;
  const char *zig_target;
  const char *object_format;
  const char *linker;
  const char *capabilities;
} ZTargetInfo;

typedef struct {
  const char *driver_kind;
  const char *selection_source;
  const char *compiler;
  const char *target_triple;
  const char *linker_flavor;
  const char *libc_mode;
  const char *sysroot_env;
  const char *sysroot_path;
  const char *sysroot_status;
  bool requires_sysroot;
  bool uses_target_flag;
  bool uses_zig_cache;
  bool strip_artifact;
} ZToolchainPlan;

typedef struct {
  size_t hits;
  size_t misses;
  size_t entries;
} ZMetaCacheStats;

void zbuf_init(ZBuf *buf);
void zbuf_append(ZBuf *buf, const char *text);
void zbuf_append_char(ZBuf *buf, char ch);
void zbuf_appendf(ZBuf *buf, const char *fmt, ...);
void zbuf_free(ZBuf *buf);

void *z_checked_malloc(size_t size);
void *z_checked_calloc(size_t count, size_t item_size);
void *z_checked_reallocarray(void *ptr, size_t count, size_t item_size);
size_t z_grow_capacity(size_t current, size_t required, size_t initial);
char *z_strdup(const char *text);
char *z_strndup(const char *text, size_t len);
char *z_read_file(const char *path, ZDiag *diag);
bool z_write_file(const char *path, const char *text, ZDiag *diag);
bool z_write_binary_file(const char *path, const unsigned char *data, size_t len, ZDiag *diag);
bool z_resolve_source(const char *input, SourceInput *out, ZDiag *diag);
bool z_map_source_diag(const SourceInput *input, ZDiag *diag);
void z_free_source(SourceInput *input);
bool z_parse_manifest_json(const char *manifest, ZManifest *out, ZDiag *diag);
void z_free_manifest(ZManifest *manifest);
char *z_default_out_path(const char *source_file);
ZToolchainPlan z_plan_toolchain(const char *cc, const char *profile, const ZTargetInfo *target);
bool z_toolchain_compile_c_object(const ZToolchainPlan *plan, const char *profile, const ZTargetInfo *target, const char *c_file, const char *object_file, const char *include_dir, const char *extra_c_flags);
bool z_toolchain_link_objects(const ZToolchainPlan *plan, const ZTargetInfo *target, const char *const *object_files, size_t object_count, const char *exe_file, const char *pre_link_flags, const char *post_object_flags);
bool z_run_cc(const char *c_file, const char *exe_file, const char *cc, const char *profile, const ZTargetInfo *target);

TokenVec z_tokenize(const char *source, ZDiag *diag);
void z_free_tokens(TokenVec *tokens);
ZRowTokenVec z_row_tokenize(const char *source, ZDiag *diag);
bool z_row_analyze_layout(const ZRowTokenVec *tokens, ZRowSyntaxFacts *facts, ZDiag *diag);
bool z_row_parse_layout(const ZRowTokenVec *tokens, ZRowTree *tree, ZDiag *diag);
Program z_parse_row(const ZRowTokenVec *tokens, const ZRowTree *tree, ZDiag *diag);
char *z_format_row_layout(const ZRowTokenVec *tokens, const ZRowTree *tree);
void z_free_row_tree(ZRowTree *tree);
void z_free_row_tokens(ZRowTokenVec *tokens);

Program z_parse(TokenVec *tokens, ZDiag *diag);
void z_free_program(Program *program);

bool z_check_program(const Program *program, ZDiag *diag);
void z_set_check_target(const ZTargetInfo *target);
ZMetaCacheStats z_meta_cache_stats(void);
void z_backend_blocker_set(ZBackendBlocker *blocker, const char *target, const char *object_format, const char *backend, const char *stage, const char *unsupported_feature);
void z_diag_set_backend_blocker(ZDiag *diag, const ZBackendBlocker *blocker);
IrProgram z_lower_program(const Program *program);
IrProgram z_lower_program_with_source(const Program *program, const SourceInput *input);
void z_free_ir_program(IrProgram *program);
bool z_emit_elf64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_elf64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_elf_aarch64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_elf_aarch64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
size_t z_macho64_stack_bytes_from_ir(const IrProgram *program);
size_t z_macho64_max_frame_bytes_from_ir(const IrProgram *program);
bool z_emit_macho64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_macho64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_coff_x64_object_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);
bool z_emit_coff_x64_exe_from_ir(const IrProgram *program, ZBuf *out, ZDiag *diag);

const char *z_host_target(void);
size_t z_target_count(void);
const ZTargetInfo *z_target_at(size_t index);
const ZTargetInfo *z_find_target(const char *target);
bool z_is_known_target(const char *target);
bool z_target_is_host(const ZTargetInfo *target);
bool z_target_has_capability(const ZTargetInfo *target, const char *capability);
const char *z_target_libc_mode(const ZTargetInfo *target);
const char *z_target_sysroot_env_name(const ZTargetInfo *target);
bool z_target_requires_sysroot(const ZTargetInfo *target);
const char *z_direct_backend_status(const ZTargetInfo *target);
const char *z_direct_object_emitter(const ZTargetInfo *target);
const char *z_direct_exe_emitter(const ZTargetInfo *target);
const char *z_direct_backend_reason(const ZTargetInfo *target);
void z_append_http_runtime_json(ZBuf *buf, const ZTargetInfo *target);
void z_append_targets_json(ZBuf *buf);
void z_append_target_names_json(ZBuf *buf);

#endif
