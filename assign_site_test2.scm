;; 構造体の定義
(define-struct A ("field1" "field2"))
(define-struct B ("field1" "field2"))
(define-struct C ("field1" "field2"))

(define loop
  (lambda (n func)
  (if (> n 0)
    (begin
      (print n)
      (func)
      (loop (- n 1) func))
    99) 7) 6)

(define a '() 15)

(print
  (loop 4
    (lambda ()
      (begin
      (set! a (new-struct-instance A) 21)
      (let ((b (new-struct-instance B))
            (c (new-struct-instance C)))
        (begin
          (set-field a "field1" b 25)
          (set-field b "field2" c 26)
          (assert-dead c 27)
          ))
       (gc)) 19)))

