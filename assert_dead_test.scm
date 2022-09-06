(let ((func (lambda (x) (+ x 1))))
  (begin
    (assert_dead func)
    (gc)
    (func 1)))

