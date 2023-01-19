(define loop
  (lambda (n func)
  (if (> n 0)
    (begin
      (func)
      (loop (- n 1) func))
    999) 2) 1)

;; 構造体の定義
(define-struct A ("field1" "field2"))
(define-struct B ("field1" "field2"))
(define-struct C ("field1" "field2"))

(define a '() 14)

(loop 4
    (lambda ()
      (begin
        (set! a (new-struct-instance A) 19)
        (let ((b (new-struct-instance B))
              (c (new-struct-instance C)))
          (begin
            (set-field a "field1" b 23)
            (set-field b "field2" c 24)
            (assert-dead c 25)
          ))
       (gc)) 17))
