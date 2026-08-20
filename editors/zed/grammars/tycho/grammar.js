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
        "parallel", "spawn", "return", "break", "continue", "or_return", "pass",
        "struct", "enum", "type", "import", "package", "extern", "inout", "soa",
        "and", "or", "not", "is",
      ),

    constant: ($) => choice("true", "false", "null"),

    type: ($) => choice("int", "float", "string", "bool", "char", "ptr", "void"),

    builtin: ($) =>
      choice(
        // `range` was a builtin until 2026-07-29 and is NOT one now (the
        // counting `for x in range(n):` form was deleted from the language);
        // highlighting it as one paints a name the compiler refuses.
        "print", "println", "str", "len", "push", "pop", "split",
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
    // Both spellings are ONE `string` node, so highlights.scm's `(string) @string`
    // covers the raw form with no query change. The backtick form interprets no
    // escapes (hence no /\\./ alternative — a backslash is an ordinary byte) and
    // /[^`]/ matches a newline, so it spans lines like the compiler's scanner
    // (src/tychoc.c:492-568).
    string: ($) =>
      token(
        choice(
          seq('"', repeat(choice(/[^"\\]/, /\\./)), '"'),
          seq("`", repeat(/[^`]/), "`"),
        ),
      ),

    fstring: ($) => token(seq('f"', repeat(choice(/[^"\\]/, /\\./)), '"')),

    // `\xNN` is exactly two hex digits and is listed FIRST so it wins over the
    // one-character `/\\./` alternative. It is a CHAR-literal escape only — a
    // string literal must not accept it, because adjacent string pieces join on
    // their escaped source text and `"\x4" "1"` would join into `\x41`
    // (docs/spec/01-lexical.md §3.9.4). Added 2026-07-30 with the compiler support.
    char: ($) => token(seq("'", choice(/\\x[0-9a-fA-F]{2}/, /[^'\\]/, /\\./), "'")),

    // `..<` is the half-open counting range of `parallel for i in 0..<N:`
    // (src/tychoc.c:705). It is listed even though `.` `.` `<` would already
    // lex: tree-sitter takes the longest match, so naming it makes the range one
    // `operator` node instead of three, which is what highlights.scm colours.
    operator: ($) =>
      choice(
        "..<",
        ":=", "->", "==", "!=", "<=", ">=", "<<", ">>",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "+", "-", "*", "/", "%", "<", ">", "=", "&", "|", "^", "~", ".", "$",
      ),

    // `;` separates the three clauses of `for init; cond; post:`
    // (src/tychoc.c:620). Without it 204 corpus files failed to lex.
    punctuation: ($) => choice("(", ")", "[", "]", "{", "}", ",", ":", ";"),
  },
});
