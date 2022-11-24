(define a (cons 1 2 1) 1)

(assert-dead a 3)
(gc)


