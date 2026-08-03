; Upward closures: make-adder returns a function that captured `n`. This is the
; first program in the tree to use closures this way.
(define (make-adder n) (lambda (x) (+ x n)))

(define add5 (make-adder 5))
(define add1 (make-adder 1))

(display (add5 3)) (newline)
(display (add1 41)) (newline)
(display (add5 (add1 10))) (newline)

; counters: each call to make-counter returns an independent captured cell
(define (make-counter)
  (define count 0)
  (lambda () (set! count (+ count 1)) count))

(define c1 (make-counter))
(define c2 (make-counter))
(display (c1)) (display " ") (display (c1)) (display " ") (display (c2)) (newline)
