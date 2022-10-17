(define func1 (lambda (x) (+ x 1)) 1)
(define func2 (lambda (x) (+ x 2)) 2)
(define cons1 (cons 1 2 "MyNumCons" 3) 3)

(assert-dead func1)
(assert-dead func2)

(gc)

(set-car! cons1 func1 10)
(set-cdr! cons1 func2 11)

(gc)
