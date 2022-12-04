;; 構造体の定義
(define-struct A ("field1" "field2"))
(define-struct B ("field1" "field2"))
(define-struct C ("field1" "field2"))


(define a1 (new-struct-instance A) 7)
(define b1 (new-struct-instance B) 8)
(define c1 (new-struct-instance C) 9)

(set-field a1 "field1" b1 11)
(set-field b1 "field2" c1 12)

(assert-dead c1 14)
(gc)


(define a2 (new-struct-instance A) 18)
(define b2 (new-struct-instance B) 19)
(define c2 (new-struct-instance C) 20)

(set-field a2 "field1" b2 22)
(set-field b2 "field2" c2 23)

(gc)
