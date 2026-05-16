const PREC = {
  call: 9,
  member: 8,
  unary: 7,
  multiplicative: 6,
  additive: 5,
  comparison: 4,
  logical: 3,
  assign: 1,
};

module.exports = grammar({
  name: "zero",

  extras: ($) => [/\s/, $.line_comment, $.block_comment],

  word: ($) => $.identifier,

  rules: {
    source_file: ($) => repeat(choice($.declaration, $.statement)),

    declaration: ($) =>
      choice(
        $.function_declaration,
        $.shape_declaration,
        $.choice_declaration,
        $.enum_declaration,
        $.impl_declaration,
        $.import_declaration,
        $.use_declaration,
        $.const_declaration,
        $.extern_declaration,
        $.type_declaration,
        $.test_declaration,
      ),

    visibility: () => choice("pub", "export"),

    modifier: () => choice("meta", "static"),

    function_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "fun",
        field("name", $.identifier),
        optional($.type_parameters),
        $.parameter_list,
        optional(seq("raises", $.type_expression)),
        optional(seq("->", $.type_expression)),
        choice(field("body", $.block), ";"),
      ),

    test_declaration: ($) => seq("test", field("name", $.string_literal), field("body", $.block)),

    shape_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        optional("packed"),
        "shape",
        field("name", $.identifier),
        optional($.type_parameters),
        field("body", $.declaration_block),
      ),

    choice_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "choice",
        field("name", $.identifier),
        optional($.type_parameters),
        field("body", $.declaration_block),
      ),

    enum_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "enum",
        field("name", $.identifier),
        optional($.type_parameters),
        field("body", $.declaration_block),
      ),

    impl_declaration: ($) =>
      seq("impl", field("target", $.type_expression), field("body", $.declaration_block)),

    declaration_block: ($) =>
      seq("{", repeat(choice($.declaration, $.field_declaration, $.statement)), "}"),

    field_declaration: ($) =>
      seq(
        optional($.visibility),
        field("name", $.identifier),
        ":",
        field("type", $.type_expression),
        optional(seq("=", $._expression)),
        optional(","),
      ),

    variant_declaration: ($) =>
      seq(field("name", $.identifier), optional($.parameter_list), optional(seq("=", $.number_literal)), optional(",")),

    import_declaration: ($) => seq("import", $.path, optional(";")),

    use_declaration: ($) => seq("use", $.path, optional(";")),

    const_declaration: ($) =>
      seq(repeat($.modifier), optional($.visibility), "const", field("name", $.identifier), optional(seq(":", $.type_expression)), "=", $._expression, optional(";")),

    extern_declaration: ($) => seq(optional($.visibility), "extern", choice($.function_declaration, $.declaration_block, ";")),

    type_declaration: ($) => seq(repeat($.modifier), optional($.visibility), "type", field("name", $.identifier), "=", $.type_expression, optional(";")),

    statement: ($) =>
      choice(
        $.let_statement,
        $.var_statement,
        $.return_statement,
        $.break_statement,
        $.continue_statement,
        $.raise_statement,
        $.if_statement,
        $.match_statement,
        $.while_statement,
        $.for_statement,
        $.defer_statement,
        $.block,
        $.expression_statement,
      ),

    let_statement: ($) => seq("let", field("name", $.identifier), optional(seq(":", $.type_expression)), "=", $._expression, optional(";")),

    var_statement: ($) => seq("var", field("name", $.identifier), optional(seq(":", $.type_expression)), "=", $._expression, optional(";")),

    return_statement: ($) => prec.right(seq("return", optional($._expression), optional(";"))),

    break_statement: () => seq("break", optional(";")),

    continue_statement: () => seq("continue", optional(";")),

    raise_statement: ($) => seq("raise", $._expression, optional(";")),

    defer_statement: ($) => seq("defer", $.statement),

    if_statement: ($) => seq("if", $._expression, $.block, optional(seq("else", choice($.block, $.if_statement)))),

    match_statement: ($) => seq("match", $._expression, $.declaration_block),

    while_statement: ($) => seq("while", $._expression, $.block),

    for_statement: ($) => seq("for", $.identifier, "in", $._expression, $.block),

    expression_statement: ($) => seq($._expression, optional(";")),

    block: ($) => seq("{", repeat(choice($.declaration, $.statement)), "}"),

    parameter_list: ($) => seq("(", optional(commaSep($.parameter)), optional(","), ")"),

    parameter: ($) => seq(optional(choice("let", "mut", "var")), field("name", $.identifier), optional(seq(":", $.type_expression)), optional(seq("=", $._expression))),

    type_parameters: ($) => seq("<", commaSep1(choice($.identifier, seq("static", $.identifier))), optional(","), ">"),

    type_expression: ($) =>
      choice(
        $.primitive_type,
        $.container_type,
        $.type_identifier,
        $.generic_type,
        $.pointer_type,
        $.function_type,
      ),

    generic_type: ($) => prec(1, seq(choice($.type_identifier, $.container_type), "<", commaSep1($.type_expression), optional(","), ">")),

    pointer_type: ($) => seq(choice("owned", "ref", "mutref"), $.type_expression),

    function_type: ($) => prec.right(seq("fun", $.parameter_list, optional(seq("->", $.type_expression)))),

    primitive_type: () =>
      token(choice("Bool", "Void", "Type", "char", "usize", "isize", /u[0-9]+/, /i[0-9]+/, /f(16|32|64)/, /c_(int|long|size|char)/, "World", "Alloc", "Fs", "Net", "Env", "Args", "Clock", "Rand", "Proc", "Sync", "Vercel", "Request", "Response")),

    container_type: () => token(choice("Maybe", "Span", "Vec", "String")),

    type_identifier: ($) => alias($.identifier, $.type_identifier),

    _expression: ($) =>
      choice(
        $.literal,
        $.identifier,
        $.call_expression,
        $.member_expression,
        $.shape_literal,
        $.array_literal,
        $.unary_expression,
        $.binary_expression,
        $.assignment_expression,
        $.parenthesized_expression,
        $.check_expression,
        $.rescue_expression,
      ),

    literal: ($) => choice($.string_literal, $.char_literal, $.number_literal, "true", "false", "null"),

    call_expression: ($) => prec(PREC.call, seq(field("function", choice($.identifier, $.member_expression)), $.argument_list)),

    argument_list: ($) => seq("(", optional(commaSep(choice($.argument, $._expression))), optional(","), ")"),

    argument: ($) => seq(field("name", $.identifier), ":", $._expression),

    member_expression: ($) => prec(PREC.member, seq($._expression, ".", field("property", $.identifier))),

    shape_literal: ($) => prec(PREC.call, seq($.identifier, "{", optional(commaSep($.argument)), optional(","), "}")),

    array_literal: ($) => seq("[", optional(commaSep($._expression)), optional(","), "]"),

    unary_expression: ($) => prec(PREC.unary, seq(choice("!", "-", "&", "*"), $._expression)),

    binary_expression: ($) =>
      choice(
        prec.left(PREC.multiplicative, seq($._expression, choice("*", "/", "%"), $._expression)),
        prec.left(PREC.additive, seq($._expression, choice("+", "-", "+%", "+|"), $._expression)),
        prec.left(PREC.comparison, seq($._expression, choice("==", "!=", "<", ">", "<=", ">="), $._expression)),
        prec.left(PREC.logical, seq($._expression, choice("&&", "||", "=>"), $._expression)),
        prec.left(PREC.logical, seq($._expression, "as", $.type_expression)),
      ),

    assignment_expression: ($) => prec.right(PREC.assign, seq($._expression, "=", $._expression)),

    parenthesized_expression: ($) => seq("(", $._expression, ")"),

    check_expression: ($) => prec(PREC.unary, seq("check", $._expression)),

    rescue_expression: ($) => prec.left(PREC.logical, seq($._expression, "rescue", choice($.block, $._expression))),

    path: ($) => seq($.identifier, repeat(seq(".", $.identifier))),

    identifier: () => /[A-Za-z_][A-Za-z0-9_]*/,

    number_literal: () => token(choice(
      /[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?/,
      /0x[0-9A-Fa-f_]+/,
      /0b[01_]+/,
      /0o[0-7_]+/,
      /[0-9][0-9_]*(_[A-Za-z][A-Za-z0-9_]*)?/,
    )),

    string_literal: ($) => seq("\"", repeat(choice($.escape_sequence, token.immediate(/[^"\\\n]+/))), "\""),

    char_literal: ($) => seq("'", choice($.escape_sequence, token.immediate(/[^'\\\n]/)), "'"),

    escape_sequence: () => token.immediate(/\\(n|r|t|0|"|'|\\|x[0-9A-Fa-f]{2}|u\{[0-9A-Fa-f]+\})/),

    line_comment: () => token(seq("//", /.*/)),

    block_comment: () => token(seq("/*", /[^*]*\*+([^/*][^*]*\*+)*/, "/")),
  },
});

function commaSep1(rule) {
  return seq(rule, repeat(seq(",", rule)));
}

function commaSep(rule) {
  return optional(commaSep1(rule));
}
