#ifndef ZERO_C_ZERO_H
#define ZERO_C_ZERO_H

#include <stdbool.h>
#include <stddef.h>

#include "zero_contracts.h"

#define ZERO_VERSION "0.3.4"

#ifndef ZERO_BUILD_HASH
#define ZERO_BUILD_HASH "unknown"
#endif

typedef struct ZTargetInfo ZTargetInfo;
typedef struct ZProgramGraph ZProgramGraph;

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
  bool prefix_deref;
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
  char *body_graph_id;
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
  char *graph_id;
  char *then_graph_id;
  char *else_graph_id;
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
  bool is_public;
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
  bool is_public;
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
  char *resolved_header;
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
  IR_VALUE_CAST,
  IR_VALUE_BINARY,
  IR_VALUE_COMPARE,
  IR_VALUE_CALL,
  IR_VALUE_INDEX_LOAD,
  IR_VALUE_STRING_LITERAL,
  IR_VALUE_ARRAY_BYTE_VIEW,
  IR_VALUE_BYTE_SLICE,
  IR_VALUE_BYTE_VIEW_LEN,
  IR_VALUE_BYTE_VIEW_REMAINING,
  IR_VALUE_BYTE_VIEW_INDEX_LOAD,
  IR_VALUE_BYTE_VIEW_EQ,
  IR_VALUE_STR_CONTAINS,
  IR_VALUE_STR_RUNTIME,
  IR_VALUE_ASCII_RUNTIME,
  IR_VALUE_TEXT_RUNTIME,
  IR_VALUE_PARSE_RUNTIME,
  IR_VALUE_TIME_RUNTIME,
  IR_VALUE_MATH_RUNTIME,
  IR_VALUE_SEARCH_RUNTIME,
  IR_VALUE_SORT_RUNTIME,
  IR_VALUE_PARSE_I32,
  IR_VALUE_PARSE_U32,
  IR_VALUE_ARGS_PARSE_U32,
  IR_VALUE_FMT_BOOL,
  IR_VALUE_FMT_HEX_U32,
  IR_VALUE_FMT_I32,
  IR_VALUE_FMT_U32,
  IR_VALUE_FMT_USIZE,
  IR_VALUE_BYTE_COPY,
  IR_VALUE_BYTE_FILL,
  IR_VALUE_ITEM_COPY,
  IR_VALUE_ITEM_FILL,
  IR_VALUE_ITEM_CONTAINS,
  IR_VALUE_CRC32_BYTES,
  IR_VALUE_FIXED_BUF_ALLOC,
  IR_VALUE_VEC_INIT,
  IR_VALUE_VEC_PUSH,
  IR_VALUE_VEC_LEN,
  IR_VALUE_VEC_CAPACITY,
  IR_VALUE_ALLOC_BYTES,
  IR_VALUE_MAYBE_HAS,
  IR_VALUE_MAYBE_VALUE,
  IR_VALUE_MAYBE_BYTE_VIEW_LITERAL,
  IR_VALUE_MAYBE_SCALAR_LITERAL,
  IR_VALUE_ARGS_LEN,
  IR_VALUE_ARGS_GET,
  IR_VALUE_ARGS_EQ,
  IR_VALUE_ARGS_GET_OR,
  IR_VALUE_ARGS_FIND,
  IR_VALUE_ARGS_CONTAINS,
  IR_VALUE_ARGS_VALUE_AFTER,
  IR_VALUE_ARGS_VALUE_AFTER_OR,
  IR_VALUE_ARGS_VALUE_AFTER_PARSE_U32,
  IR_VALUE_ENV_GET,
  IR_VALUE_TIME_WALL_SECONDS,
  IR_VALUE_TIME_MONOTONIC,
  IR_VALUE_TIME_AS_MS,
  IR_VALUE_RAND_NEXT_U32,
  IR_VALUE_RAND_NEXT_BELOW,
  IR_VALUE_RAND_RANGE_U32,
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
  IR_VALUE_JSON_DIAGNOSTIC_BYTES,
  IR_VALUE_JSON_FIELD,
  IR_VALUE_JSON_LOOKUP_SCALAR,
  IR_VALUE_JSON_STRING_DECODE,
  IR_VALUE_JSON_STRING_FIELD,
  IR_VALUE_JSON_WRITE_STRING,
  IR_VALUE_JSON_WRITE_RUNTIME,
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
  IR_VALUE_HTTP_WRITE_JSON_RESPONSE,
  IR_VALUE_HTTP_REQUEST_METHOD_NAME,
  IR_VALUE_HTTP_REQUEST_PATH,
  IR_VALUE_HTTP_REQUEST_MATCHES,
  IR_VALUE_HTTP_REQUEST_BODY_WITHIN,
  IR_VALUE_HTTP_STATUS_CLASS,
  IR_VALUE_FIELD_LOAD,
  IR_VALUE_CHECK,
  IR_VALUE_RESCUE,
  IR_VALUE_RECORD_ADDR,
  /* Appended after RECORD_ADDR so persisted MIR value-kind numbering stays stable. */
  IR_VALUE_FS_READ_BYTES_AT_PATH,
  IR_VALUE_VEC_BYTES,
  IR_VALUE_VEC_GET,
  IR_VALUE_VEC_SET,
  IR_VALUE_VEC_CLEAR,
  IR_VALUE_VEC_POP,
  IR_VALUE_VEC_TRUNCATE,
  IR_VALUE_VEC_REMOVE_SWAP,
  IR_VALUE_VEC_INDEX,
  IR_VALUE_VEC_CONTAINS,
  IR_VALUE_VEC_INSERT_UNIQUE,
  IR_VALUE_VEC_REMOVE_VALUE,
  IR_VALUE_JSON_ERROR_LABEL,
  IR_VALUE_PROC_CAPTURE,
  IR_VALUE_PROC_CAPTURE_FILES,
  IR_VALUE_PROC_CHILD_SPAWN,
  IR_VALUE_PROC_CHILD_OP,
  IR_VALUE_PROC_CHILD_IO,
  IR_VALUE_PROC_PTY_RESIZE,
  IR_VALUE_TERM_RUNTIME,
  IR_VALUE_PROC_SPAWN_INHERIT,
  IR_VALUE_FS_APPEND_BYTES_PATH
} IrValueKind;

typedef enum {
  IR_JSON_WRITE_FIELD_RAW,
  IR_JSON_WRITE_FIELD_STRING,
  IR_JSON_WRITE_FIELD_U32,
  IR_JSON_WRITE_FIELD_BOOL,
  IR_JSON_WRITE_OBJECT1_STRING,
  IR_JSON_WRITE_OBJECT1_U32,
  IR_JSON_WRITE_OBJECT1_BOOL,
  IR_JSON_WRITE_OBJECT2_FIELDS,
  IR_JSON_WRITE_OBJECT2_STRING_FIELD,
  IR_JSON_WRITE_OBJECT2_U32_FIELD,
  IR_JSON_WRITE_OBJECT2_BOOL_FIELD,
  IR_JSON_WRITE_ARRAY2_STRINGS,
  IR_JSON_WRITE_ARRAY2_U32,
  IR_JSON_WRITE_ARRAY2_BOOLS
} IrJsonWriteOp;

typedef enum {
  IR_STR_OP_REVERSE,
  IR_STR_OP_COPY,
  IR_STR_OP_CONCAT,
  IR_STR_OP_REPEAT,
  IR_STR_OP_TO_LOWER_ASCII,
  IR_STR_OP_TO_UPPER_ASCII,
  IR_STR_OP_TRIM_ASCII,
  IR_STR_OP_TRIM_START_ASCII,
  IR_STR_OP_TRIM_END_ASCII,
  IR_STR_OP_COUNT_BYTE,
  IR_STR_OP_STARTS_WITH,
  IR_STR_OP_ENDS_WITH,
  IR_STR_OP_CONTAINS,
  IR_STR_OP_COUNT,
  IR_STR_OP_INDEX_OF,
  IR_STR_OP_LAST_INDEX_OF,
  IR_STR_OP_EQL_IGNORE_ASCII_CASE,
  IR_STR_OP_WORD_COUNT_ASCII,
  IR_STR_OP_PATH_BASENAME,
  IR_STR_OP_PATH_DIRNAME,
  IR_STR_OP_PATH_EXTENSION,
  IR_STR_OP_PARSE_TOKEN_ASCII,
  IR_STR_OP_CRYPTO_SHA256,
  IR_STR_OP_CRYPTO_SHA256_HEX,
  IR_STR_OP_CRYPTO_HMAC_SHA256,
  IR_STR_OP_CRYPTO_HMAC_SHA256_HEX
} IrStrOp;

typedef enum {
  IR_ASCII_OP_IS_DIGIT,
  IR_ASCII_OP_IS_LOWER,
  IR_ASCII_OP_IS_UPPER,
  IR_ASCII_OP_IS_ALPHA,
  IR_ASCII_OP_IS_ALNUM,
  IR_ASCII_OP_IS_WHITESPACE,
  IR_ASCII_OP_IS_HEX_DIGIT,
  IR_ASCII_OP_TO_LOWER,
  IR_ASCII_OP_TO_UPPER,
  IR_ASCII_OP_DIGIT_VALUE,
  IR_ASCII_OP_HEX_VALUE
} IrAsciiOp;

typedef enum {
  IR_TEXT_OP_IS_ASCII,
  IR_TEXT_OP_UTF8_VALID,
  IR_TEXT_OP_UTF8_LEN
} IrTextOp;

typedef enum {
  IR_PARSE_OP_IS_ASCII_DIGIT,
  IR_PARSE_OP_IS_ASCII_ALPHA,
  IR_PARSE_OP_IS_IDENTIFIER_START,
  IR_PARSE_OP_IS_WHITESPACE,
  IR_PARSE_OP_SCAN_DIGITS,
  IR_PARSE_OP_SCAN_IDENTIFIER,
  IR_PARSE_OP_SCAN_UNTIL_BYTE,
  IR_PARSE_OP_SCAN_WHITESPACE,
  IR_PARSE_OP_PARSE_BOOL,
  IR_PARSE_OP_PARSE_U8,
  IR_PARSE_OP_PARSE_U16,
  IR_PARSE_OP_PARSE_USIZE,
  IR_PARSE_OP_TERM_KEY_CODE,
  IR_PARSE_OP_TERM_KEY_BYTE_LEN
} IrParseOp;

typedef enum {
  IR_TIME_OP_AS_US_FLOOR,
  IR_TIME_OP_AS_MS_FLOOR,
  IR_TIME_OP_AS_SECONDS_FLOOR,
  IR_TIME_OP_MIN,
  IR_TIME_OP_MAX,
  IR_TIME_OP_CLAMP,
  IR_TIME_OP_SLEEP,
  IR_TIME_OP_WALL_SECONDS,
  IR_TIME_OP_MONOTONIC
} IrTimeOp;

typedef enum {
  IR_TERM_OP_STDIN_IS_TTY,
  IR_TERM_OP_STDOUT_IS_TTY,
  IR_TERM_OP_WIDTH_OR,
  IR_TERM_OP_HEIGHT_OR,
  IR_TERM_OP_ENTER_RAW_MODE,
  IR_TERM_OP_LEAVE_RAW_MODE,
  IR_TERM_OP_READ_INPUT
} IrTermOp;

typedef enum {
  IR_PROC_CHILD_OP_RUNNING,
  IR_PROC_CHILD_OP_WAIT,
  IR_PROC_CHILD_OP_KILL,
  IR_PROC_CHILD_OP_CLOSE,
  IR_PROC_CHILD_OP_VALID,
  IR_PROC_CHILD_OP_PID,
  IR_PROC_CHILD_OP_INTERRUPT,
  IR_PROC_CHILD_OP_CLOSE_STDIN,
  IR_PROC_CHILD_OP_PID_RUNNING,
  IR_PROC_CHILD_OP_KILL_PID,
  IR_PROC_CHILD_OP_INTERRUPT_PID
} IrProcChildOp;

typedef enum {
  IR_PROC_CHILD_IO_READ_STDOUT,
  IR_PROC_CHILD_IO_READ_STDERR,
  IR_PROC_CHILD_IO_WRITE_STDIN
} IrProcChildIoOp;

typedef enum {
  IR_MATH_OP_MIN_I32,
  IR_MATH_OP_MAX_I32,
  IR_MATH_OP_CLAMP_I32,
  IR_MATH_OP_MIN_I64,
  IR_MATH_OP_MAX_I64,
  IR_MATH_OP_CLAMP_I64,
  IR_MATH_OP_MIN_U32,
  IR_MATH_OP_MAX_U32,
  IR_MATH_OP_CLAMP_U32,
  IR_MATH_OP_MIN_U64,
  IR_MATH_OP_MAX_U64,
  IR_MATH_OP_CLAMP_U64,
  IR_MATH_OP_MIN_USIZE,
  IR_MATH_OP_MAX_USIZE,
  IR_MATH_OP_CLAMP_USIZE,
  IR_MATH_OP_ABS_I32,
  IR_MATH_OP_ABS_I64,
  IR_MATH_OP_CHECKED_ADD_U32,
  IR_MATH_OP_CHECKED_SUB_U32,
  IR_MATH_OP_CHECKED_MUL_U32,
  IR_MATH_OP_SATURATING_ADD_U32,
  IR_MATH_OP_SATURATING_SUB_U32,
  IR_MATH_OP_SATURATING_MUL_U32,
  IR_MATH_OP_CHECKED_ADD_I32,
  IR_MATH_OP_CHECKED_SUB_I32,
  IR_MATH_OP_CHECKED_MUL_I32,
  IR_MATH_OP_SATURATING_ADD_I32,
  IR_MATH_OP_SATURATING_SUB_I32,
  IR_MATH_OP_SATURATING_MUL_I32,
  IR_MATH_OP_GCD_U32,
  IR_MATH_OP_LCM_U32,
  IR_MATH_OP_CHECKED_LCM_U32,
  IR_MATH_OP_POW_U32,
  IR_MATH_OP_CHECKED_POW_U32,
  IR_MATH_OP_MOD_POW_U32,
  IR_MATH_OP_IS_PRIME_U32,
  IR_MATH_OP_SQRT_FLOOR_U32,
  IR_MATH_OP_FACTORIAL_U32,
  IR_MATH_OP_BINOMIAL_U32,
  IR_MATH_OP_DIVISOR_COUNT_U32,
  IR_MATH_OP_PROPER_DIVISOR_SUM_U32,
  IR_MATH_OP_CHECKED_ADD_USIZE,
  IR_MATH_OP_CHECKED_SUB_USIZE,
  IR_MATH_OP_CHECKED_MUL_USIZE,
  IR_MATH_OP_SATURATING_ADD_USIZE,
  IR_MATH_OP_SATURATING_SUB_USIZE,
  IR_MATH_OP_SATURATING_MUL_USIZE
} IrMathOp;

typedef enum {
  IR_SEARCH_OP_LOWER_BOUND_I32,
  IR_SEARCH_OP_BINARY_I32,
  IR_SEARCH_OP_LOWER_BOUND_U32,
  IR_SEARCH_OP_BINARY_U32,
  IR_SEARCH_OP_LOWER_BOUND_USIZE,
  IR_SEARCH_OP_BINARY_USIZE,
  IR_SEARCH_OP_UPPER_BOUND_I32,
  IR_SEARCH_OP_UPPER_BOUND_U32,
  IR_SEARCH_OP_UPPER_BOUND_USIZE
} IrSearchOp;

typedef enum {
  IR_SORT_OP_INSERTION_I32,
  IR_SORT_OP_IS_SORTED_I32,
  IR_SORT_OP_INSERTION_U32,
  IR_SORT_OP_IS_SORTED_U32,
  IR_SORT_OP_INSERTION_USIZE,
  IR_SORT_OP_IS_SORTED_USIZE
} IrSortOp;

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
  unsigned external_index;
  bool external_call;
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
  IR_INSTR_WHILE,
  IR_INSTR_BREAK,
  IR_INSTR_CONTINUE
} IrInstrKind;

typedef struct {
  size_t continue_target;
  size_t *break_patches;
  size_t break_len;
  size_t break_cap;
} ZDirectLoopFrame;

bool z_direct_loop_frame_add_break(Z_INOUT ZDirectLoopFrame *frame, size_t patch_offset);

// Describes a maximal run of consecutive constant-index, constant-value
// IR_INSTR_INDEX_STORE instructions over a single array local that writes the
// elements 0,1,2,...,count-1 with one identical scalar literal. Direct
// backends fold such runs into a single fill loop instead of emitting one
// store per element, which is the dominant source of emitted-code density for
// zero-initialized fixed arrays.
typedef struct {
  unsigned array_index;
  size_t count;             // number of stores folded (run length)
  unsigned long long fill_value;
  IrTypeKind element_type;
} ZDirectFillRun;

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
  bool is_record_ref;
  bool is_mutable;
  unsigned ref_byte_size;
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
  char **generic_param_names;
  char **generic_arg_types;
  size_t generic_binding_len;
  IrTypeKind return_type;
  IrTypeKind value_return_type;
  IrTypeKind return_element_type;
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

// Detects a fill run starting at instrs[start]. Returns true and fills *out
// when at least min_run stores form a 0..count-1 constant fill of one array
// local with a single literal value. The caller emits a fill loop and
// advances past the run.
bool z_direct_detect_fill_run(Z_IN const IrFunction *fun, Z_IN const IrInstr *instrs, size_t len, size_t start, size_t min_run, Z_OUT ZDirectFillRun *out);

typedef struct {
  char *symbol;
  char *import_header;
  char *import_resolved_header;
  IrTypeKind return_type;
  IrTypeKind *param_types;
  size_t param_len;
  size_t param_cap;
} IrExternalFunction;

typedef struct {
  Program program;
  const ZTargetInfo *target;
  IrFunction *functions;
  size_t function_len;
  size_t function_cap;
  IrExternalFunction *external_functions;
  size_t external_function_len;
  size_t external_function_cap;
  IrDataSegment *data_segments;
  size_t data_segment_len;
  size_t data_segment_cap;
  size_t readonly_data_bytes;
  bool mir_valid;
  char mir_expected[128];
  char mir_actual[128];
  char mir_message[256];
  char mir_help[256];
  char *mir_path;
  char *package_root;
  const unsigned char *mir_binary_storage;
  size_t mir_binary_storage_len;
  bool mir_binary_storage_mapped;
  bool mir_binary_storage_borrowed;
  int mir_binary_storage_fd;
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
  size_t direct_c_import_call_count;
  size_t direct_c_import_symbol_count;
  char **active_local_names;
  size_t active_local_len;
  size_t active_local_cap;
  size_t const_lower_depth;
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
  char *program_graph_hash;
  char *program_graph_module_identity;
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
  bool canonical_text_source;
  size_t source_file_count;
  size_t import_count;
  size_t module_count;
  size_t import_edge_count;
  size_t symbol_count;
  size_t source_line_count;
  size_t dependency_count;
  long long resolve_ms, parse_ms, interface_ms, check_ms;
  long long lower_ms, codegen_ms, object_ms, link_ms;
  long long graph_load_ms, graph_stdlib_merge_ms, graph_readiness_check_ms;
  long long graph_stdlib_reference_scan_ms, graph_stdlib_cleanup_ms, graph_stdlib_module_load_ms;
  long long graph_stdlib_node_merge_ms, graph_stdlib_edge_merge_ms, graph_stdlib_finalize_ms;
  long long graph_mir_cache_load_ms, graph_mir_lower_ms, graph_mir_cache_write_ms, graph_mir_cache_reload_ms;
  size_t graph_stdlib_modules_merged, graph_stdlib_nodes_merged, graph_stdlib_edges_merged;
  bool graph_stdlib_merge_cache_hit, graph_stdlib_merge_cache_stored;
  size_t lowered_ir_bytes;
  char *mapped_mir_cache_path;
  size_t mapped_mir_cache_bytes;
  bool mapped_mir_cache_hit, mapped_mir_cache_written;
  bool mapped_mir_memory_mapped, mapped_mir_borrowed_storage;
  bool mapped_mir_codegen_immediate, mapped_mir_program_reconstructed;
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
  size_t direct_c_import_call_count;
  size_t direct_c_import_symbol_count;
  char **direct_c_import_headers;
  char **direct_c_import_resolved_headers;
  size_t direct_c_import_header_count;
  bool parse_cache_hit;
  bool interface_cache_hit;
  bool check_cache_hit;
  bool specialization_cache_hit;
  bool emitted_object_cache_hit;
  bool allow_missing_main;
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
  char *graph_path;
  char *kind;
  bool repository_graph_compiler_input_present;
  bool repository_graph_compiler_input;
  ZManifestDependency *dependencies;
  ZManifestCLib *c_libs;
  size_t dependency_count;
  size_t c_lib_count;
} ZManifest;

struct ZTargetInfo {
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
};

typedef enum {
  Z_DIRECT_BACKEND_NONE,
  Z_DIRECT_BACKEND_ELF64,
  Z_DIRECT_BACKEND_ELF_AARCH64,
  Z_DIRECT_BACKEND_MACHO64,
  Z_DIRECT_BACKEND_MACHO_X64,
  Z_DIRECT_BACKEND_COFF_X64,
  Z_DIRECT_BACKEND_COFF_AARCH64
} ZDirectBackend;

typedef enum {
  Z_DIRECT_TRAP_INDEX_BOUNDS = 0,
  Z_DIRECT_TRAP_VALUE_BOUNDS,
  Z_DIRECT_TRAP_WRITE_FAILED,
  Z_DIRECT_TRAP_KIND_COUNT
} ZDirectTrapKind;

typedef struct {
  unsigned offsets[Z_DIRECT_TRAP_KIND_COUNT];
  unsigned lens[Z_DIRECT_TRAP_KIND_COUNT];
} ZDirectTrapMessages;

typedef struct {
  size_t *items;
  size_t len;
  size_t cap;
} ZDirectTrapBranchList;

Z_RET_BORROWED const char *z_direct_trap_message(ZDirectTrapKind kind);
bool z_direct_trap_branches_record(Z_INOUT ZDirectTrapBranchList *list, size_t patch_offset);
void z_direct_trap_branches_free(Z_INOUT ZDirectTrapBranchList *lists, size_t count);

typedef enum {
  Z_BACKEND_FAMILY_UNKNOWN,
  Z_BACKEND_FAMILY_DIRECT,
  Z_BACKEND_FAMILY_LLVM
} ZBackendFamily;

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
  const char *driver_kind;
  const char *selection_source;
  const char *compiler;
  const char *target_triple;
  const char *status;
  const char *reason;
  bool target_supported;
  bool tool_available;
  bool native_executable;
} ZLlvmToolchainPlan;

typedef struct {
  const char *selected_emitter;
  const char *artifact_kind;
  const char *linker_flavor;
  const char *artifact_libc_mode;
  const char *sysroot_status;
  bool direct_selected;
  bool target_requires_sysroot;
  bool artifact_requires_sysroot;
} ZDirectReleaseTargetFacts;

typedef struct {
  ZDirectBackend backend;
  const char *selected_emitter;
  const char *artifact_path;
  const char *linker_flavor;
  bool active;
} ZDirectObjectBackendFacts;

typedef struct {
  ZDirectBackend backend;
  const char *artifact_path;
  const char *unsupported_reason;
  bool available;
} ZDirectObjectTargetFacts;

typedef struct {
  ZDirectBackend backend;
  const char *cache_key;
  const char *blocker;
  bool supported;
} ZDirectRuntimeObjectFacts;

typedef struct {
  ZDirectBackend backend;
  const char *default_request_name;
  const char *artifact_path;
  bool requested;
  bool requested_name;
  bool request_supported;
} ZDirectExecutableTargetFacts;

typedef struct {
  size_t hits;
  size_t misses;
  size_t entries;
} ZMetaCacheStats;

void zbuf_init(Z_OUT ZBuf *buf);
void zbuf_append(Z_INOUT ZBuf *buf, Z_IN const char *text);
void zbuf_append_char(Z_INOUT ZBuf *buf, char ch);
void zbuf_appendf(Z_INOUT ZBuf *buf, Z_IN const char *fmt, ...);
void zbuf_free(Z_INOUT ZBuf *buf);

Z_RET_OWNED void *z_checked_malloc(size_t size);
Z_RET_OWNED void *z_checked_calloc(size_t count, size_t item_size);
Z_RET_OWNED void *z_checked_reallocarray(Z_SINK Z_OPTIONAL void *ptr, size_t count, size_t item_size);
size_t z_grow_capacity(size_t current, size_t required, size_t initial);
Z_RET_OWNED char *z_strdup(Z_IN const char *text);
Z_RET_OWNED char *z_strndup(Z_IN const char *text, size_t len);
Z_RET_OWNED Z_RET_OPTIONAL char *z_read_file(Z_IN const char *path, Z_OUT ZDiag *diag);
bool z_read_binary_file(Z_IN const char *path, Z_OUT unsigned char **out, Z_OUT size_t *out_len, Z_OUT ZDiag *diag);
bool z_read_file_prefix(Z_IN const char *path, Z_OUT void *bytes, size_t len, Z_OUT size_t *out_read, Z_OUT ZDiag *diag);
bool z_write_file(Z_IN const char *path, Z_IN const char *text, Z_OUT ZDiag *diag);
bool z_write_binary_file(Z_IN const char *path, Z_IN const unsigned char *data, size_t len, Z_OUT ZDiag *diag);
bool z_map_source_diag(Z_IN const SourceInput *input, Z_INOUT ZDiag *diag);
void z_diag_set_path_copy(Z_INOUT ZDiag *diag, Z_OPTIONAL Z_IN const char *path);
void z_free_source(Z_INOUT SourceInput *input);
bool z_parse_manifest_json(Z_IN const char *manifest, Z_OUT ZManifest *out, Z_OUT ZDiag *diag);
bool z_resolve_package_metadata(Z_IN const char *manifest_path, Z_IN const char *manifest, Z_IN const ZManifest *parsed_manifest, Z_OUT SourceInput *out, Z_OUT ZDiag *diag);
Z_RET_OWNED Z_RET_OPTIONAL char *z_manifest_path_for_input(Z_IN const char *input_path);
Z_RET_OWNED Z_RET_OPTIONAL char *z_manifest_path_for_root(Z_IN const char *root);
bool z_resolve_manifest_graph_artifact_path(Z_IN const char *input_path, Z_OUT char **out_artifact_path, Z_OUT bool *handled, bool require_graph, Z_OUT ZDiag *diag);
void z_free_manifest(Z_INOUT ZManifest *manifest);
Z_RET_OWNED char *z_default_out_path(Z_IN const char *source_file);
ZToolchainPlan z_plan_toolchain(Z_IN const char *cc, Z_IN const char *profile, Z_IN const ZTargetInfo *target);
ZToolchainPlan z_direct_backend_toolchain_plan(ZDirectBackend backend, Z_IN const ZTargetInfo *target);
bool z_direct_backend_toolchain_plan_for_emit_kind(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend, Z_OUT ZToolchainPlan *out);
size_t z_direct_target_stack_bytes(Z_IN const ZTargetInfo *target, Z_IN const IrProgram *program);
size_t z_direct_target_max_frame_bytes(Z_IN const ZTargetInfo *target, Z_IN const IrProgram *program);
bool z_toolchain_compiler_override_safe(Z_IN const char *compiler);
bool z_toolchain_compile_c_object(Z_IN const ZToolchainPlan *plan, Z_IN const char *profile, Z_IN const ZTargetInfo *target, Z_IN const char *c_file, Z_IN const char *object_file, Z_IN const char *include_dir, Z_IN const char *extra_c_flags);
bool z_toolchain_link_objects(Z_IN const ZToolchainPlan *plan, Z_IN const ZTargetInfo *target, Z_IN const char *const *object_files, size_t object_count, Z_IN const char *exe_file, Z_IN const char *pre_link_flags, Z_IN const char *post_object_flags);
bool z_run_cc(Z_IN const char *c_file, Z_IN const char *exe_file, Z_IN const char *cc, Z_IN const char *profile, Z_IN const ZTargetInfo *target);

void z_free_program(Z_INOUT Program *program);

bool z_check_program(Z_IN const Program *program, Z_OUT ZDiag *diag);
bool z_check_program_library(Z_IN const Program *program, Z_OUT ZDiag *diag);
#define Z_DIRECT_FRAME_LOCAL_LIMIT_BYTES 131072u
bool z_function_frame_locals_within_limit(Z_IN const Program *program, Z_IN const Function *fun, size_t limit, Z_OUT size_t *out_total, Z_OUT const Stmt **out_over);
void z_append_call_resolution_facts_json(Z_INOUT ZBuf *buf, Z_IN const SourceInput *input, Z_IN const Program *program);
void z_set_check_target(Z_OPTIONAL Z_IN const ZTargetInfo *target);
ZMetaCacheStats z_meta_cache_stats(void);
void z_backend_blocker_set(Z_OUT ZBackendBlocker *blocker, Z_IN const char *target, Z_IN const char *object_format, Z_IN const char *backend, Z_IN const char *stage, Z_IN const char *unsupported_feature);
void z_diag_set_backend_blocker(Z_INOUT ZDiag *diag, Z_IN const ZBackendBlocker *blocker);
IrProgram z_lower_program(Z_IN const Program *program);
IrProgram z_lower_program_with_source(Z_IN const Program *program, Z_IN const SourceInput *input, Z_IN const ZTargetInfo *target);
IrProgram z_lower_program_graph_with_source(Z_IN const ZProgramGraph *graph, Z_IN const SourceInput *input, Z_IN const ZTargetInfo *target);
void z_free_ir_program(Z_INOUT IrProgram *program);
bool z_emit_elf64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_elf64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_elf_aarch64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_elf_aarch64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
size_t z_elf_aarch64_stack_bytes_from_ir(Z_IN const IrProgram *program);
size_t z_elf_aarch64_max_frame_bytes_from_ir(Z_IN const IrProgram *program);
size_t z_macho64_stack_bytes_from_ir(Z_IN const IrProgram *program);
size_t z_macho64_max_frame_bytes_from_ir(Z_IN const IrProgram *program);
bool z_emit_macho64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_macho64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_macho_x64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_macho_x64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_coff_x64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_coff_x64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_coff_aarch64_object_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_coff_aarch64_exe_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_direct_object_from_ir(ZDirectBackend backend, Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_direct_executable_from_ir(ZDirectBackend backend, Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);
bool z_emit_llvm_ir_from_ir(Z_IN const IrProgram *program, Z_INOUT ZBuf *out, Z_OUT ZDiag *diag);

Z_RET_BORROWED const char *z_host_target(void);
size_t z_target_count(void);
Z_RET_BORROWED Z_RET_OPTIONAL const ZTargetInfo *z_target_at(size_t index);
Z_RET_BORROWED Z_RET_OPTIONAL const ZTargetInfo *z_find_target(Z_IN const char *target);
bool z_is_known_target(Z_IN const char *target);
bool z_target_is_host(Z_IN const ZTargetInfo *target);
bool z_target_has_capability(Z_IN const ZTargetInfo *target, Z_IN const char *capability);
Z_RET_BORROWED const char *z_target_libc_mode(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_target_sysroot_env_name(Z_IN const ZTargetInfo *target);
bool z_target_requires_sysroot(Z_IN const ZTargetInfo *target);
ZDirectBackend z_direct_object_backend(Z_IN const ZTargetInfo *target);
ZDirectBackend z_direct_exe_backend(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_direct_backend_object_emitter(ZDirectBackend backend);
Z_RET_BORROWED const char *z_direct_backend_exe_emitter(ZDirectBackend backend);
ZDirectBackend z_direct_backend_from_emitter(Z_IN const char *emitter);
Z_RET_BORROWED const char *z_direct_backend_linker_flavor(ZDirectBackend backend);
Z_RET_BORROWED const char *z_direct_backend_artifact_path(ZDirectBackend backend, bool executable);
Z_RET_BORROWED const char *z_direct_backend_runtime_object_cache_key(ZDirectBackend backend);
size_t z_direct_backend_symbol_overhead(ZDirectBackend backend, bool has_readonly_data);
bool z_direct_backend_supports_runtime_object(ZDirectBackend backend);
Z_RET_BORROWED Z_RET_OPTIONAL const char *z_direct_runtime_link_blocker(Z_IN const ZTargetInfo *target, bool needs_http_runtime);
bool z_direct_backend_emitter_is_executable(Z_IN const char *emitter);
bool z_direct_backend_is_request_name(Z_IN const char *requested_backend);
bool z_direct_requested_backend_matches(Z_IN const char *requested_backend, ZDirectBackend backend);
ZBackendFamily z_backend_family_from_request(Z_IN const char *requested_backend, Z_IN const char *emit_kind);
Z_RET_BORROWED const char *z_backend_family_name(ZBackendFamily family);
bool z_backend_request_is_known(Z_IN const char *requested_backend, Z_IN const char *emit_kind);
bool z_backend_request_is_llvm(Z_IN const char *requested_backend, Z_IN const char *emit_kind);
Z_RET_BORROWED Z_RET_OPTIONAL const char *z_backend_direct_request_name(Z_IN const char *requested_backend);
Z_RET_BORROWED const char *z_backend_request_expected(void);
void z_backend_init_unknown_diag(Z_OUT ZDiag *diag, Z_IN const char *requested_backend, Z_IN const char *path);
void z_backend_init_llvm_unavailable_diag(Z_OUT ZDiag *diag, Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *path);
Z_RET_BORROWED const char *z_llvm_target_triple(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_llvm_optimization_level(Z_IN const char *profile);
ZLlvmToolchainPlan z_llvm_toolchain_plan(Z_IN const ZTargetInfo *target);
ZToolchainPlan z_llvm_c_toolchain_plan(Z_IN const ZTargetInfo *target);
bool z_llvm_native_executable_ready(Z_IN const ZTargetInfo *target, Z_IN const char *path, Z_OUT ZDiag *diag);
bool z_llvm_link_executable(Z_IN const char *llvm_file, Z_IN const char *runtime_object_file, Z_IN const char *exe_file, Z_IN const ZToolchainPlan *plan, Z_IN const ZTargetInfo *target, Z_IN const char *profile, bool links_zero_runtime, Z_OUT ZDiag *diag);
Z_RET_BORROWED const char *z_llvm_backend_lifecycle_json_text(void);
void z_append_llvm_backend_lifecycle_json(Z_INOUT ZBuf *buf);
void z_append_llvm_backend_lifecycle_field_json(Z_INOUT ZBuf *buf);
void z_append_doctor_llvm_toolchain_json(Z_INOUT ZBuf *buf, Z_IN const ZTargetInfo *host_target, Z_IN const ZLlvmToolchainPlan *plan);
void z_append_llvm_toolchain_plan_json(Z_INOUT ZBuf *buf, Z_IN const ZTargetInfo *target);
void z_append_llvm_target_backend_json(Z_INOUT ZBuf *buf, Z_IN const ZTargetInfo *target);
void z_append_llvm_ir_backend_json(Z_INOUT ZBuf *buf, Z_IN const SourceInput *input, Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind);
void z_append_llvm_native_backend_json(Z_INOUT ZBuf *buf, Z_IN const SourceInput *input, Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind);
Z_RET_BORROWED const char *z_direct_backend_status(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_direct_object_emitter(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_direct_exe_emitter(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_direct_backend_reason(Z_IN const ZTargetInfo *target);
ZDirectBackend z_direct_backend_for_emit_kind(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend);
Z_RET_BORROWED const char *z_direct_backend_emitter_for_emit_kind(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend);
Z_RET_BORROWED const char *z_direct_backend_name_for_emit_kind(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend);
ZDirectReleaseTargetFacts z_direct_release_target_facts(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend, Z_IN const ZToolchainPlan *fallback_plan, bool linked_executable);
ZDirectObjectBackendFacts z_direct_object_backend_facts(Z_IN const ZTargetInfo *target, Z_IN const char *emit_kind, Z_IN const char *requested_backend, bool has_runtime_imports);
ZDirectObjectTargetFacts z_direct_object_target_facts(Z_IN const ZTargetInfo *target);
ZDirectRuntimeObjectFacts z_direct_runtime_object_facts(Z_IN const ZTargetInfo *target, bool needs_http_runtime);
ZDirectExecutableTargetFacts z_direct_executable_target_facts(Z_IN const ZTargetInfo *target, Z_IN const char *requested_backend);
Z_RET_BORROWED const char *z_direct_backend_expected(Z_IN const ZTargetInfo *target);
Z_RET_BORROWED const char *z_direct_backend_help(Z_IN const ZTargetInfo *target);
void z_append_http_runtime_json(Z_INOUT ZBuf *buf, Z_IN const ZTargetInfo *target);
void z_append_targets_json(Z_INOUT ZBuf *buf);
void z_append_target_names_json(Z_INOUT ZBuf *buf);

#endif
