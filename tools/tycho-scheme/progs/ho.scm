; Higher-order: map/filter/append/reverse/length/sum from the prelude.
(define xs (list 1 2 3 4 5))

(display (sum xs)) (newline)
(display (length xs)) (newline)
(display (map (lambda (x) (* x x)) xs)) (newline)
(display (filter (lambda (x) (even? x)) xs)) (newline)
(display (append xs (list 6 7))) (newline)
(display (reverse xs)) (newline)
