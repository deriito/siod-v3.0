(define-struct MyStruct ("field1" "field2" "field3"))

(define myStruct1 (new-struct-instance MyStruct) 3)

(define myStruct2 (new-struct-instance MyStruct) 5)

(define myStruct3 (new-struct-instance MyStruct) 7)

(define myStruct4 (new-struct-instance MyStruct) 9)

(define myStruct5 (new-struct-instance MyStruct) 11)

(set-field myStruct1 "field1" myStruct2 13)

(set-field myStruct1 "field2" myStruct3 15)

(set-field myStruct1 "field3" myStruct4 17)

(set-field myStruct2 "field1" myStruct5 19)

(assert-dead myStruct5 21)
(gc)
