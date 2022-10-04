(let ((func1 (lambda (x) (+ x 1))) (func2 (lambda (x) (+ x 2))))
  (begin
    (assert_dead func1)
    (assert_dead func2)
    (gc)

    (define cons1 (cons 1 2 "MyNumCons" 7) 7)
    (set-car! cons1 func1 8)
    (set-cdr! cons1 func2 9)
    (gc)))
