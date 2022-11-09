(define global-val-a 1 1)
(define global-val-b 2 2)

(define func
  (lambda (x)
    (let ((my-cons (cons x x "MyCons" 6)))
      (begin
        (assert-dead my-cons 8)
        (if (eqv? x 1)
          (set! global-val-a my-cons 10)
          (set! global-val-b my-cons 11)))) 5) 4)


(func 1)
(gc)

(func 2)
(gc)



;; Type: CONS(MyCons);
;; Path to object: SYMBOL(func); ->
;; CLOSURE; ->
;; SYMBOL(global-val-a); ->
;; CONS(MyCons);

;; 上記のテストプログラムでは，
;; SYMBOL(func) -> CLOSUREの代入操作が1回だけ実行するため，代入操作が発生する場所は記録不能，
;; 同じように，CLOSURE; -> SYMBOL(global-val-a) / SYMBOL(global-val-b)も記録不能
