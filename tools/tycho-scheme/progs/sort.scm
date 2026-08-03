; insertion sort -- recursion, list construction, and the prelude's range.
(define (insert x xs)
  (if (null? xs)
      (list x)
      (if (< x (car xs))
          (cons x xs)
          (cons (car xs) (insert x (cdr xs))))))

(define (isort xs)
  (if (null? xs) '() (insert (car xs) (isort (cdr xs)))))

(display (isort (list 5 3 8 1 9 2 7 4 6))) (newline)
(display (isort (reverse (range 0 10)))) (newline)
(display (isort '())) (newline)
(display (map (lambda (x) (* x x)) (isort (list 3 1 2)))) (newline)
