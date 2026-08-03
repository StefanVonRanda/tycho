; eq? on symbols and strings, quote of symbols, set! on a global from a closure.
(define x 1)
(define (bump!) (set! x (+ x 1)))

(display (if (eq? 'a 'a) "same" "diff")) (newline)     ; same
(display (if (eq? 'a 'b) "diff" "same")) (newline)     ; same
(display (if (eq? "ab" "ab") "eq" "ne")) (newline)     ; eq
(bump!)
(display x) (newline)          ; 2: the closure mutated the global
(bump!)
(display x) (newline)          ; 3
