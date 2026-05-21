(function_declaration
  "fun" @context
  name: (identifier) @name) @item

(shape_declaration
  "shape" @context
  name: (identifier) @name) @item

(choice_declaration
  "choice" @context
  name: (identifier) @name) @item

(enum_declaration
  "enum" @context
  name: (identifier) @name) @item

(interface_declaration
  "interface" @context
  name: (identifier) @name) @item

(type_alias_declaration
  "type" @context
  name: (identifier) @name) @item

(const_declaration
  "const" @context
  name: (identifier) @name) @item

(test_declaration
  "test" @context
  name: (string_literal) @name) @item
