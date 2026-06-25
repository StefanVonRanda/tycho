// tree-sitter grammar for Tycho — FLAT (token-level). It lexes a .ty file into a
// flat stream of tokens (keywords, types, builtins, literals, identifiers,
// operators, punctuation) without modelling block structure. That's enough to
// drive syntax highlighting and to register the language in Zed so the LSP
// (tycho-lsp) attaches for diagnostics. A full structural grammar would need a C
// external scanner for INDENT/DEDENT (tycho is indentation-significant); this
// deliberately skips that. Keyword-vs-identifier is resolved by `word`.
module.exports = grammar({
  name: "tycho",

  extras: ($) => [/[ \t\r\n]/, $.comment],
  word: ($) => $.identifier,

  rules: {
    source_file: ($) => repeat($._token),

    _token: ($) =>
      choice(
        $.keyword,
        $.constant,
        $.type,
        $.builtin,
        $.fstring,
        $.string,
        $.char,
        $.number,
        $.typaram,
        $.identifier,
        $.operator,
        $.punctuation,
      ),

    comment: ($) => token(seq("#", /.*/)),

    keyword: ($) =>
      choice(
        "fn", "if", "elif", "else", "for", "while", "in", "match", "select",
        "parallel", "spawn", "return", "break", "continue", "or_return",
        "struct", "enum", "type", "import", "package", "extern", "mut", "soa",
        "and", "or", "not",
      ),

    constant: ($) => choice("true", "false", "null"),

    type: ($) => choice("int", "float", "string", "bool", "char", "ptr", "void"),

    builtin: ($) =>
      choice(
        "print", "println", "str", "len", "push", "pop", "range", "split",
        "substr", "find", "read_file", "write_file", "read_all", "list_dir",
        "args", "getenv", "input", "chr", "die", "is_null", "sqrt", "pow",
        "floor", "fabs", "map_get", "map_set", "reserve",
      ),

    // generic type parameter sigil: `$T`, `$K`, … (a lone `$`, e.g. the
    // explicit-type-arg form `name$(int)`, is handled by `operator` below).
    typaram: ($) => /\$[A-Za-z_][A-Za-z0-9_]*/,

    identifier: ($) => /[A-Za-z_][A-Za-z0-9_]*/,

    number: ($) => /[0-9]+(\.[0-9]+)?/,

    // token(...) makes each literal ONE atomic lexer token, so `extras`
    // (whitespace / # comments) are never applied to the characters inside it.
    string: ($) => token(seq('"', repeat(choice(/[^"\\]/, /\\./)), '"')),

    fstring: ($) => token(seq('f"', repeat(choice(/[^"\\]/, /\\./)), '"')),

    char: ($) => token(seq("'", choice(/[^'\\]/, /\\./), "'")),

    operator: ($) =>
      choice(
        ":=", "->", "==", "!=", "<=", ">=", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "+", "-", "*", "/", "%", "<", ">", "=", "&", "|", "^", "~", ".", "$",
      ),

    punctuation: ($) => choice("(", ")", "[", "]", "{", "}", ",", ":"),
  },
});
