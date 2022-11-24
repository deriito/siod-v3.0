(define global-val-a 1 1)
(define global-val-b 2 2)

(define func
  (lambda (x)
    (let ((my-cons (cons x x 6)))
      (begin
        (assert-dead my-cons 8)
        (if (eqv? x 1)
          (set! global-val-a my-cons 10)
          (set! global-val-b my-cons 11)))) 5) 4)


(func 1)
(gc)

(func 1)
(func 2)
(gc)
