(line_comment) @comment
(block_comment) @comment

(string_literal) @string
(escape_sequence) @string.escape
(char_literal) @string.special
(number_literal) @number

[
  "break"
  "check"
  "continue"
  "defer"
  "else"
  "for"
  "if"
  "match"
  "raise"
  "raises"
  "rescue"
  "return"
  "while"
] @keyword

[
  "choice"
  "const"
  "enum"
  "export"
  "extern"
  "fun"
  "impl"
  "import"
  "let"
  "meta"
  "mut"
  "packed"
  "pub"
  "shape"
  "static"
  "test"
  "type"
  "use"
  "var"
] @keyword

[
  "as"
  "false"
  "null"
  "true"
] @constant.builtin

(primitive_type) @type.builtin
(container_type) @type.builtin
(type_identifier) @type

(function_declaration name: (identifier) @function)
(call_expression function: (identifier) @function)
(call_expression function: (member_expression property: (identifier) @function.method))

[
  "->"
  "=>"
  "+"
  "-"
  "*"
  "/"
  "%"
  "="
  "=="
  "!="
  "<"
  ">"
  "<="
  ">="
  "&&"
  "||"
  "."
  ":"
] @operator
