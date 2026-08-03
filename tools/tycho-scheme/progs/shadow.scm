; lexical shadowing and captured locals: a param shadows a global; a local
; define captured by a closure outlives its function.
(define x 10)
(define (f x) (+ x 1))
(define (g) (define y 100) (lambda () y))

(display (f 5)) (newline)      ; 6: the param, not the global
(display x) (newline)          ; 10: the global is untouched
