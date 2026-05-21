// Tree-sitter grammar for the Zero programming language.
// Authoritative parser is native/zero-c/src/parser.c in the Zero compiler;
// this grammar is a parallel artifact and is expected to drift.

const commaSep1 = (rule) => seq(rule, repeat(seq(',', rule)), optional(','));
const commaSep = (rule) => optional(commaSep1(rule));

module.exports = grammar({
  name: 'zero',

  word: $ => $.identifier,

  externals: $ => [$._generic_lt],

  extras: $ => [/\s+/, $.line_comment],

  conflicts: $ => [
    // `raises {` may open an error set or the function body — resolve based
    // on what's inside the braces.
    [$.raises_clause],
    // `Ident<` after `as` (cast target): identifier alone vs identifier+args.
    [$.named_type],
    // `frames {` vs `Frames { ... }` — the uppercase-name constraint plus
    // GLR resolves to the right interpretation based on body content.
    [$._expression, $.shape_literal],
  ],

  rules: {
    source_file: $ => repeat($._top_level),

    line_comment: _ => token(seq('//', /[^\n]*/)),

    // ---------- top-level declarations ----------

    _top_level: $ => choice(
      $.function_declaration,
      $.use_declaration,
      $.const_declaration,
      $.shape_declaration,
      $.choice_declaration,
      $.enum_declaration,
      $.extern_c_declaration,
      $.extern_shape_declaration,
      $.type_alias_declaration,
      $.interface_declaration,
      $.test_declaration,
    ),

    test_declaration: $ => seq(
      'test',
      field('name', $.string_literal),
      field('body', $.block),
    ),

    use_declaration: $ => seq('use', $.dotted_path),

    dotted_path: $ => seq(
      $.identifier,
      repeat(seq('.', $.identifier)),
    ),

    const_declaration: $ => seq(
      optional('pub'),
      'const',
      field('name', $.identifier),
      ':',
      field('type', $._type),
      '=',
      field('value', $._expression),
    ),

    function_declaration: $ => seq(
      optional(choice('pub', seq('export', 'c'))),
      'fun',
      field('name', $.identifier),
      optional($.generic_params),
      $.parameter_list,
      optional(seq('->', field('return_type', $._type))),
      optional($.raises_clause),
      field('body', $.block),
    ),

    parameter_list: $ => seq(
      '(',
      commaSep($.parameter),
      ')',
    ),

    parameter: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
    ),

    raises_clause: $ => seq(
      'raises',
      optional($.error_set),
    ),

    error_set: $ => seq(
      '{',
      commaSep($.identifier),
      '}',
    ),

    shape_declaration: $ => seq(
      optional('pub'),
      optional('packed'),
      'shape',
      field('name', $.identifier),
      optional($.generic_params),
      '{',
      repeat($._shape_member),
      '}',
    ),

    _shape_member: $ => choice(
      seq($.field_declaration, optional(',')),
      $.function_declaration,
    ),

    field_declaration: $ => seq(
      field('name', $.identifier),
      ':',
      field('type', $._type),
      optional(seq('=', field('default', $._expression))),
    ),

    choice_declaration: $ => seq(
      optional('pub'),
      'choice',
      field('name', $.identifier),
      optional($.generic_params),
      '{',
      repeat(seq($.choice_case, optional(','))),
      '}',
    ),

    choice_case: $ => seq(
      field('name', $.identifier),
      optional(seq(':', field('payload', $._type))),
    ),

    enum_declaration: $ => seq(
      optional('pub'),
      'enum',
      field('name', $.identifier),
      optional(seq(':', field('backing', $.scalar_type))),
      '{',
      commaSep($.identifier),
      '}',
    ),

    extern_c_declaration: $ => seq(
      'extern', 'c',
      $.string_literal,
      optional(seq('as', $.identifier)),
    ),

    extern_shape_declaration: $ => seq(
      optional('pub'),
      'extern', 'shape',
      field('name', $.identifier),
      '{',
      repeat(seq($.field_declaration, optional(','))),
      '}',
    ),

    type_alias_declaration: $ => seq(
      optional('pub'),
      'type',
      field('name', $.identifier),
      optional($.generic_params),
      '=',
      field('value', $._type),
    ),

    interface_declaration: $ => seq(
      optional('pub'),
      'interface',
      field('name', $.identifier),
      optional($.generic_params),
      '{',
      repeat($.interface_method),
      '}',
    ),

    interface_method: $ => seq(
      'fun',
      field('name', $.identifier),
      $.parameter_list,
      optional(seq('->', field('return_type', $._type))),
      optional($.raises_clause),
    ),

    // ---------- generics ----------

    generic_params: $ => seq(
      '<',
      commaSep1($.generic_param),
      '>',
    ),

    generic_param: $ => choice(
      seq($.identifier, optional(seq(':', $._type))),
      seq('static', $.identifier, ':', $._type),
    ),

    generic_args: $ => prec.dynamic(20, seq(
      '<',
      commaSep1($._generic_arg),
      '>',
    )),

    // Generic args can be types or numeric static values (e.g., `[N]` array sizes).
    _generic_arg: $ => choice($._type, $.number_literal),

    // ---------- types ----------

    _type: $ => choice(
      $.scalar_type,
      $.array_type,
      $.named_type,
      $.const_type,
    ),

    const_type: $ => seq('const', $._type),

    scalar_type: _ => choice(
      'i8', 'i16', 'i32', 'i64',
      'u8', 'u16', 'u32', 'u64',
      'usize', 'isize',
      'f32', 'f64',
      'Bool', 'char', 'String', 'Void', 'World',
      'Self',
    ),

    // Size can be a literal or a name (a static value param or a const).
    array_type: $ => seq(
      '[',
      choice($.integer_literal, $.identifier),
      ']',
      $._type,
    ),

    // Named types, optionally with `<args>`. Unifies bare references like `T`
    // with parameterized ones like `Maybe<i32>`.
    named_type: $ => seq(
      $.identifier,
      optional($.generic_args),
    ),

    // ---------- statements ----------

    block: $ => seq('{', repeat($._statement), '}'),

    _statement: $ => choice(
      $.let_statement,
      $.while_statement,
      $.for_statement,
      $.if_statement,
      $.return_statement,
      $.raise_statement,
      $.defer_statement,
      $.break_statement,
      $.continue_statement,
      $.assignment_statement,
      $.expression_statement,
    ),

    for_statement: $ => seq(
      'for',
      field('variable', $.identifier),
      'in',
      field('start', $._expression),
      '..',
      field('end', $._expression),
      field('body', $.block),
    ),

    break_statement: _ => 'break',
    continue_statement: _ => 'continue',

    let_statement: $ => seq(
      'let',
      optional('mut'),
      field('name', $.identifier),
      optional(seq(':', field('type', $._type))),
      '=',
      field('value', $._expression),
    ),

    while_statement: $ => seq(
      'while',
      field('condition', $._expression),
      field('body', $.block),
    ),

    if_statement: $ => seq(
      'if',
      field('condition', $._expression),
      field('then', $.block),
      optional(seq(
        'else',
        field('else', choice($.if_statement, $.block)),
      )),
    ),

    return_statement: $ => prec.right(seq(
      'return',
      optional($._expression),
    )),

    raise_statement: $ => seq('raise', $.identifier),

    defer_statement: $ => seq('defer', $._expression),

    assignment_statement: $ => prec.dynamic(1, seq(
      field('target', $._expression),
      '=',
      field('value', $._expression),
    )),

    expression_statement: $ => $._expression,

    // ---------- expressions ----------

    _expression: $ => choice(
      $.number_literal,
      $.bool_literal,
      $.string_literal,
      $.char_literal,
      $.identifier,
      $.uppercase_identifier,
      $.binary_expression,
      $.cast_expression,
      $.index_expression,
      $.member_expression,
      $.call_expression,
      $.array_literal,
      $.parenthesized_expression,
      $.borrow_expression,
      $.check_expression,
      $.rescue_expression,
      $.match_expression,
      $.meta_expression,
      $.shape_literal,
    ),

    parenthesized_expression: $ => seq('(', $._expression, ')'),

    binary_expression: $ => {
      const table = [
        ['||', 1, 'left'],
        ['&&', 2, 'left'],
        ['==', 3, 'left'], ['!=', 3, 'left'],
        ['<', 3, 'left'], ['<=', 3, 'left'],
        ['>', 3, 'left'], ['>=', 3, 'left'],
        ['+', 4, 'left'], ['-', 4, 'left'],
        ['+%', 4, 'left'], ['+|', 4, 'left'],
        ['*', 5, 'left'], ['/', 5, 'left'], ['%', 5, 'left'],
      ];
      return choice(...table.map(([op, level, assoc]) => {
        const builder = assoc === 'left' ? prec.left : prec.right;
        return builder(level, seq(
          field('left', $._expression),
          field('operator', op),
          field('right', $._expression),
        ));
      }));
    },

    cast_expression: $ => prec.left(7, seq(
      field('value', $._expression),
      'as',
      field('type', $._type),
    )),

    borrow_expression: $ => prec(8, seq(
      '&',
      optional('mut'),
      $._expression,
    )),

    check_expression: $ => prec.right(6, seq('check', $._expression)),

    rescue_expression: $ => prec.left(0, seq(
      $._expression,
      'rescue',
      field('error', $.identifier),
      field('fallback', $.block),
    )),

    meta_expression: $ => prec.right(6, seq('meta', $._expression)),

    index_expression: $ => prec(10, seq(
      field('target', $._expression),
      '[',
      choice(field('index', $._expression), $.slice_range),
      ']',
    )),

    slice_range: $ => choice(
      seq(field('start', $._expression), '..', optional(field('end', $._expression))),
      seq('..', field('end', $._expression)),
      '..',
    ),

    member_expression: $ => prec(10, seq(
      field('target', $._expression),
      '.',
      field('name', $.identifier),
    )),

    call_expression: $ => prec(10, seq(
      field('callee', $._expression),
      optional(field('type_args', $.generic_call_args)),
      '(',
      commaSep($._expression),
      ')',
    )),

    // Call-site generics: the opening `<` is the external GENERIC_LT token.
    generic_call_args: $ => seq(
      $._generic_lt,
      commaSep1($._generic_arg),
      '>',
    ),

    array_literal: $ => seq(
      '[',
      choice(
        commaSep($._expression),
        seq(
          field('value', $._expression),
          ';',
          field('count', $._expression),
        ),
      ),
      ']',
    ),

    // ---------- shape literal ----------
    // `Foo { x: 1, y: 2 }`. Mirrors what Zero's hand-rolled parser does:
    // only an uppercase-leading identifier can be a shape-literal name. That
    // stops `while x < w { stmts }` from being parsed as `w { ... }`.

    shape_literal: $ => prec.dynamic(-1, seq(
      field('name', $.uppercase_identifier),
      '{',
      commaSep($.field_init),
      '}',
    )),

    uppercase_identifier: _ => /[A-Z][A-Za-z0-9_]*/,

    field_init: $ => seq(
      field('name', $.identifier),
      ':',
      field('value', $._expression),
    ),

    // ---------- match ----------

    match_expression: $ => seq(
      'match',
      field('scrutinee', $._expression),
      '{',
      repeat($.match_arm),
      '}',
    ),

    match_arm: $ => seq(
      field('pattern', $._match_pattern),
      optional(seq('=>', field('binding', $.identifier))),
      optional(seq('if', field('guard', $._expression))),
      field('body', $.block),
    ),

    _match_pattern: $ => choice(
      $.choice_pattern,
      $.wildcard_pattern,
      $.range_pattern,
      $.bool_literal,
      $.number_literal,
      $.identifier,
    ),

    range_pattern: $ => seq($.number_literal, '..', $.number_literal),

    choice_pattern: $ => seq('.', $.identifier),
    wildcard_pattern: _ => seq('.', '_'),

    // ---------- atoms ----------

    number_literal: _ => token(choice(
      // float
      /[0-9][0-9_]*\.[0-9][0-9_]*(?:[eE][+\-]?[0-9]+)?(?:_f(?:32|64))?/,
      // hex / binary / octal
      /0x[0-9a-fA-F][0-9a-fA-F_]*(?:_[iu](?:size|8|16|32|64))?/,
      /0b[01][01_]*(?:_[iu](?:size|8|16|32|64))?/,
      /0o[0-7][0-7_]*(?:_[iu](?:size|8|16|32|64))?/,
      // decimal int (with optional `_iN`/`_uN`/`_fN` suffix)
      /[0-9][0-9_]*(?:_[iuf](?:size|8|16|32|64))?/,
    )),
    integer_literal: _ => token(/[0-9]+/),
    bool_literal: _ => choice('true', 'false'),
    string_literal: _ => token(seq('"', /(?:[^"\\\n]|\\[^\n])*/, '"')),
    char_literal: _ => token(seq("'", /(?:[^'\\\n]|\\[^\n])+/, "'")),

    identifier: _ => /[A-Za-z_][A-Za-z0-9_]*/,
  },
});
