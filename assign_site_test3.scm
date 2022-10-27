(define x 1 1)

(let ((y (cons 1 2 "MyCons" 3)))
  (let ((func1 (lambda (x) (+ x y) 4)))
    (begin
      (assert-dead y)
      (set! x func1 7))))

(gc)