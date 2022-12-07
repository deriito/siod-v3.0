;; 構造体の定義
(define-struct A ("field1" "field2"))
(define-struct B ("field1" "field2"))
(define-struct C ("field1" "field2"))


;; 1回目
(define a1 (new-struct-instance A) 8)

(let ((b1 (new-struct-instance B))
      (c1 (new-struct-instance C)))
     (begin
       (set-field a1 "field1" b1 13)
       (set-field b1 "field2" c1 14)
       (assert-dead c1 15)))

(gc)


;; 2回目
(define a2 (new-struct-instance A) 21)

(let ((b2 (new-struct-instance B))
      (c2 (new-struct-instance C)))
     (begin
       (set-field a2 "field1" b2 26)
       (set-field b2 "field2" c2 27)))

(gc)


;; 3回目
(define a3 (new-struct-instance A) 33)

(let ((b3 (new-struct-instance B))
      (c3 (new-struct-instance C)))
     (begin
       (set-field a3 "field1" b3 38)
       (set-field b3 "field2" c3 39)))

(gc)


;; 4回目
(define a4 (new-struct-instance A) 45)

(let ((b4 (new-struct-instance B))
      (c4 (new-struct-instance C)))
     (begin
       (set-field a4 "field1" b4 50)
       (set-field b4 "field2" c4 51)))

(gc)
