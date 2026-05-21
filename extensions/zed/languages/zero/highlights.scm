; Syntax highlights for the Zero programming language.

; ---------- Comments ----------

(line_comment) @comment

; ---------- Literals ----------

(string_literal) @string
(char_literal) @string.special
(number_literal) @number
(integer_literal) @number
(bool_literal) @constant.builtin

; ---------- Keywords ----------

[
  "if"
  "else"
  "match"
  "while"
  "for"
  "in"
  "return"
  "defer"
  "check"
  "raise"
  "raises"
  "rescue"
] @keyword

(break_statement) @keyword
(continue_statement) @keyword

[
  "fun"
  "shape"
  "choice"
  "enum"
  "interface"
  "type"
  "const"
  "let"
  "use"
  "test"
  "extern"
  "export"
] @keyword

[
  "pub"
  "mut"
  "static"
  "packed"
  "meta"
  "c"
] @keyword.modifier

"as" @keyword.operator

; ---------- Built-in types ----------

(scalar_type) @type.builtin

[
  "Bool"
  "Self"
  "String"
  "Void"
  "World"
] @type.builtin

; ---------- Type names ----------

(shape_declaration name: (identifier) @type.definition)
(choice_declaration name: (identifier) @type.definition)
(enum_declaration name: (identifier) @type.definition)
(interface_declaration name: (identifier) @type.definition)
(type_alias_declaration name: (identifier) @type.definition)
(extern_shape_declaration name: (identifier) @type.definition)

(named_type (identifier) @type)
(uppercase_identifier) @type

; ---------- Functions ----------

(function_declaration name: (identifier) @function)
(interface_method name: (identifier) @function.method)

(call_expression
  callee: (member_expression
    name: (identifier) @function.method))

(call_expression
  callee: (identifier) @function.call)

; ---------- Members and fields ----------

(member_expression name: (identifier) @property)
(field_init name: (identifier) @property)
(field_declaration name: (identifier) @property)

; ---------- Bindings ----------

(parameter name: (identifier) @variable.parameter)
(let_statement name: (identifier) @variable)
(const_declaration name: (identifier) @constant)
(generic_param (identifier) @type.parameter)

; ---------- Match patterns ----------

(choice_pattern (identifier) @constructor)
(wildcard_pattern) @keyword

; ---------- Operators ----------

[
  "="
  "+" "-" "*" "/" "%"
  "+%" "+|"
  "==" "!=" "<" "<=" ">" ">="
  "&&" "||"
  "&"
] @operator

[
  "->"
  "=>"
  ".."
] @punctuation.special

; ---------- Punctuation ----------

[
  "("
  ")"
  "["
  "]"
  "{"
  "}"
] @punctuation.bracket

[
  "."
  ","
  ":"
  ";"
] @punctuation.delimiter

; ---------- Imports ----------

(use_declaration (dotted_path) @namespace)
