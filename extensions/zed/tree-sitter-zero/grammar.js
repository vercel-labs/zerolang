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

  conflicts: ($) => [[$.type_arguments, $.generic_type], [$.type_arguments, $.type_identifier]],

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
        $.interface_declaration,
        $.impl_declaration,
        $.import_declaration,
        $.use_declaration,
        $.const_declaration,
        $.extern_declaration,
        $.type_declaration,
        $.test_declaration,
      ),

    visibility: () => choice("pub", "export", seq("export", "c")),

    modifier: () => choice("meta", "static"),

    function_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "fun",
        field("name", $.identifier),
        optional($.type_parameters),
        $.parameter_list,
        optional(seq("->", $.type_expression)),
        optional("raises"),
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
        field("body", $.variant_block),
      ),

    enum_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "enum",
        field("name", $.identifier),
        optional($.type_parameters),
        optional(seq(":", field("backing", $.type_expression))),
        field("body", $.variant_block),
      ),

    interface_declaration: ($) =>
      seq(
        repeat($.modifier),
        optional($.visibility),
        "interface",
        field("name", $.identifier),
        optional($.type_parameters),
        field("body", $.interface_block),
      ),

    interface_block: ($) => seq("{", repeat($.interface_method), "}"),

    interface_method: ($) =>
      seq(
        "fun",
        field("name", $.identifier),
        optional($.type_parameters),
        $.parameter_list,
        optional(seq("->", $.type_expression)),
        optional(choice(field("body", $.block), ";")),
      ),

    impl_declaration: ($) =>
      seq("impl", field("target", $.type_expression), field("body", $.declaration_block)),

    declaration_block: ($) =>
      seq("{", repeat(choice($.declaration, $.field_declaration, $.statement)), "}"),

    variant_block: ($) => seq("{", repeat($.variant_declaration), "}"),

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

    let_statement: ($) => seq("let", optional("mut"), field("name", $.identifier), optional(seq(":", $.type_expression)), "=", $._expression, optional(";")),

    var_statement: ($) => seq("var", field("name", $.identifier), optional(seq(":", $.type_expression)), "=", $._expression, optional(";")),

    return_statement: ($) => prec.right(seq("return", optional($._expression), optional(";"))),

    break_statement: () => seq("break", optional(";")),

    continue_statement: () => seq("continue", optional(";")),

    raise_statement: ($) => seq("raise", $._expression, optional(";")),

    defer_statement: ($) => seq("defer", $.statement),

    if_statement: ($) => seq("if", $._expression, $.block, optional(seq("else", choice($.block, $.if_statement)))),

    match_statement: ($) => prec(PREC.call + 1, seq("match", field("subject", $.identifier), $.match_block)),

    match_block: ($) => seq("{", repeat($.match_arm), "}"),

    match_arm: ($) => seq(field("pattern", $.match_pattern), field("body", $.block)),

    match_pattern: ($) => choice(seq(".", field("name", $.identifier)), $._expression),

    while_statement: ($) => seq("while", $._expression, $.block),

    for_statement: ($) => seq("for", $.identifier, "in", $._expression, $.block),

    expression_statement: ($) => seq($._expression, optional(";")),

    block: ($) => seq("{", repeat(choice($.declaration, $.statement)), "}"),

    parameter_list: ($) => seq("(", optional(commaSep($.parameter)), optional(","), ")"),

    parameter: ($) => seq(optional(choice("let", "mut", "var")), field("name", $.identifier), optional(seq(":", $.type_expression)), optional(seq("=", $._expression))),

    type_parameters: ($) =>
      seq(
        "<",
        commaSep1(
          choice(
            $.identifier,
            seq("static", $.identifier),
            seq("static", field("name", $.identifier), ":", field("constraint", $.type_expression)),
            seq(field("name", $.identifier), ":", field("constraint", $.type_expression)),
          ),
        ),
        optional(","),
        ">",
      ),

    type_expression: ($) =>
      choice(
        $.primitive_type,
        $.container_type,
        $.type_identifier,
        $.generic_type,
        $.pointer_type,
        $.function_type,
        $.array_type,
      ),

    array_type: ($) => seq("[", field("length", choice($.number_literal, $.identifier)), "]", $.type_expression),

    generic_type: ($) => prec(1, seq(choice($.type_identifier, $.container_type), "<", commaSep1($.type_expression), optional(","), ">")),

    pointer_type: ($) => seq(choice("owned", "ref", "mutref"), $.type_expression),

    function_type: ($) => prec.right(seq("fun", $.parameter_list, optional(seq("->", $.type_expression)))),

    primitive_type: () =>
      token(choice("Bool", "Void", "Type", "Self", "char", "usize", "isize", /u[0-9]+/, /i[0-9]+/, /f(16|32|64)/, /c_(int|long|size|char)/, "World", "Alloc", "Fs", "Net", "Env", "Args", "Clock", "Rand", "Proc", "Sync", "Vercel", "Request", "Response")),

    container_type: () => token(choice("Maybe", "Span", "Vec", "String")),

    type_identifier: ($) => alias($.identifier, $.type_identifier),

    _expression: ($) =>
      choice(
        $.literal,
        $.identifier,
        $.call_expression,
        $.member_expression,
        $.index_expression,
        $.range_expression,
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

    call_expression: ($) =>
      prec(PREC.call, seq(field("function", choice($.identifier, $.member_expression)), optional($.type_arguments), $.argument_list)),

    type_arguments: ($) => seq("<", commaSep1(choice($.type_expression, $.number_literal, $.identifier)), optional(","), ">"),

    argument_list: ($) => seq("(", optional(commaSep(choice($.argument, $._expression))), optional(","), ")"),

    argument: ($) => seq(field("name", $.identifier), ":", $._expression),

    member_expression: ($) => prec(PREC.member, seq($._expression, ".", field("property", $.identifier))),

    index_expression: ($) => prec(PREC.member, seq($._expression, "[", $._expression, "]")),

    range_expression: ($) =>
      prec(PREC.member, seq($._expression, "[", field("start", $._expression), "..", field("end", $._expression), "]")),

    shape_literal: ($) => prec(PREC.call, seq($.identifier, "{", optional(commaSep($.argument)), optional(","), "}")),

    array_literal: ($) =>
      seq(
        "[",
        choice(
          seq(optional(commaSep($._expression)), optional(",")),
          seq(field("value", $._expression), ";", field("count", $._expression)),
        ),
        "]",
      ),

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
