; fib: the classic -- exercises define-sugar, if, arithmetic, recursion.
(define (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2)))))

(define (fib-line n) (begin (display (fib n)) (display " ") (newline)))

(fib-line 0)
(fib-line 1)
(fib-line 10)
(fib-line 20)
(fib-line 25)
