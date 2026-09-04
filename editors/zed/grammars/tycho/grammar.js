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
        // `packed` is the struct layout modifier (spec §17.1b). It is a keyword
        // in the lexer's word set (surface.lock), and without this line
        // it lexed as an ordinary identifier — the corpus still parsed, so no lane
        // could see it. editors/keyword-coverage.tsv is what sees it now.
        "struct", "packed", "enum", "type", "import", "package", "extern", "inout", "soa",
        "and", "or", "not", "is",
        // Contextual declaration words, plus `handle` which is hard-reserved in
        // src/tychoc.c@keyword. Each was measured to compile before being added.
        "handle", "const", "delete", "bounded", "vector", "sink", "subscript", "where", "yield",
      ),

    constant: ($) => choice("true", "false", "null"),

    // `bytes` is TK_KW_BYTES in src/tychoc.c@keyword -- a reserved type name.
    // The nine sized names are as hard-reserved as `int` (each returns its own
    // TK_KW_* from src/tychoc.c@keyword) and were in neither grammar until V2l,
    // because scripts/surface_lock.py's extractor held no digit and the coverage
    // table could not name a word the lock had never seen.
    type: ($) =>
      choice(
        "int", "float", "string", "bool", "char", "ptr", "void", "bytes",
        "u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64", "f32",
      ),

    builtin: ($) =>
      choice(
        // `range` was a builtin until 2026-07-29 and is NOT one now (the
        // counting `for x in range(n):` form was deleted from the language);
        // highlighting it as one paints a name the compiler refuses.
        "print", "println", "str", "len", "push", "pop", "split",
        "substr", "find", "read_file", "write_file", "read_all", "list_dir",
        "args", "getenv", "input", "chr", "die", "is_null", "sqrt", "pow",
        "floor", "fabs", "reserve",
        // `map_get`/`map_set` were here and are NOT builtins: src/tychoc.c:3086-3089
        // rejects a user-typed call outright, so this painted names the compiler
        // refuses -- the same defect the `range` note above records.
        "channel", "char_at", "clock", "close", "eprint", "exit", "from_bytes",
        "get", "hash", "keys", "ncpu", "now", "recv", "send", "size_of",
        "to_bool", "to_bytes", "to_char", "to_float", "to_i32", "to_int",
        "to_ptr", "to_u32",
        "to_str", "to_under", "wait", "zero",
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
    // (src/tychoc.c:497-573).
    string: ($) =>
      token(
        choice(
          seq('"', repeat(choice(/[^"\\]/, /\\./)), '"'),
          seq("`", repeat(/[^`]/), "`"),
        ),
      ),

    // An INTERPOLATION may contain a double-quoted string --
    // `f"{io.append(ap, \"!\")}"` is valid Tycho and corelib/test/io/main.ty:485
    // has one. The old rule ended the fstring at that inner quote, so the file
    // stopped parsing under this grammar while ./tychoc accepted it.
    fstring: ($) =>
      token(
        seq(
          'f"',
          repeat(
            choice(
              seq(
                '{',
                repeat(
                  choice(
                    /\\./,
                    seq('"', repeat(choice(/[^"\\]/, /\\./)), '"'),
                    /[^}"\\]/,
                  ),
                ),
                '}',
              ),
              /[^"\\{]/,
              /\\./,
            ),
          ),
          '"',
        ),
      ),

    // `\xNN` is exactly two hex digits and is listed FIRST so it wins over the
    // one-character `/\\./` alternative. It is a CHAR-literal escape only — a
    // string literal must not accept it, because adjacent string pieces join on
    // their escaped source text and `"\x4" "1"` would join into `\x41`
    // (docs/spec/01-lexical.md §3.9.4). Added 2026-07-30 with the compiler support.
    char: ($) => token(seq("'", choice(/\\x[0-9a-fA-F]{2}/, /[^'\\]/, /\\./), "'")),

    // `..<` is the half-open counting range of `parallel for i in 0..<N:`
    // (src/tychoc.c:710). It is listed even though `.` `.` `<` would already
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
    // (src/tychoc.c:625). Without it 204 corpus files failed to lex.
    punctuation: ($) => choice("(", ")", "[", "]", "{", "}", ",", ":", ";"),
  },
});
