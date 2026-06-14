; Tree-sitter highlight queries for Hier (flat grammar). Each grammar node type
; is already distinct (keywords/types/builtins lex as their own nodes, not as
; identifiers), so a node -> capture mapping is all that's needed.
(comment) @comment
(keyword) @keyword
(constant) @constant.builtin
(type) @type.builtin
(builtin) @function.builtin
(string) @string
(fstring) @string
(char) @string
(number) @number
(operator) @operator
(punctuation) @punctuation.bracket
(identifier) @variable
