;;-*-mode:lisp-*-
;; For use with the DIGITAL RDB SQL SERVICES interface to SIOD.
;; 20-JAN-94 GJC.
;;
;; Loading (into siod linked with sql_rdb.obj)
;;  $siod -g0 -isql_rdb.scm -h150000
;;
;; Procedures: (rdb-sql-init "database-name")
;;             (rdb-sql-error?) => last sql error
;;             (rdb-sql "string") => result of operation.
;;             (rdb-show-table "table-name") => column information.
;;             (rdb-show-tables) => list all tables.

(define *rdb-sql-association* nil)
(define *rdb-sql-database* nil)

(define (rdb-sql-init db)
  (if (null? *rdb-sql-association*)
      (begin (set! *rdb-sql-association* (rdb-sql-associate))
	     (if db
		 (set! *rdb-sql-database* db))
	     (if *rdb-sql-database*
		 (rdb-sql-execute-immediate
		  *rdb-sql-association*
		  (string-append "declare schema filename "
				 *rdb-sql-database*))))))

(define (rdb-sql-error?)
  (rdb-sql-error-buffer *rdb-sql-association*))

(define (rdb-sql-cleanup release-associations?)
  (let ((l (rdb-sql-associations)))
    (while l
      (let ((s (rdb-sql-association-statements (car l))))
	(while s
	  (rdb-sql-release (car s))
	  (set! s (cdr s))))
      (if release-associations?
	  (rdb-sql-release (car l)))
      (set! l (cdr l)))))

(define (rdb-sql cmd)
  (rdb-sql-init nil)
  (let ((s nil)
	(p nil)
	(l nil)
	(c nil)
	(result nil)
	(row nil)
	(rowp nil)
	(x nil))
    ;; we really need an unwind-protect mechanism to make
    ;; sure the statements and cursors are closed, although the
    ;; global cleanup code can be used instead.
    (set! s (rdb-sql-prepare-cached *rdb-sql-association* cmd))
    (set! p (rdb-sql-statement-params s))
    (set! l (rdb-sql-statement-selects s))
    (if p (error "params not implemented"))
    (if (null? l)
	(set! result (rdb-sql-execute s))
      (begin (rdb-sql-declare-cursor s 'table 'read-only)
	     (set! c (rdb-sql-open-cursor s))
	     (while (rdb-sql-fetch s)
	       (set! rowp l)
	       (set! row nil)
	       (while rowp
		 (set! row (cons (rdb-sql-get-column s (car (cdr (car rowp))))
				 row))
		 (set! rowp (cdr rowp)))
	       (set! result (cons (nreverse row) result)))
	     (set! rowp l)
	     (set! row nil)
	     (while rowp
	       (set! row (cons (car (car rowp)) row))
	       (set! rowp (cdr rowp)))
	     (set! result (cons (nreverse row) (nreverse result)))))
    (if c (rdb-sql-close-cursor s))
    (if s (rdb-sql-release-cached s))
    result))


(define rdb-sql-prepare-cached rdb-sql-prepare)
(define rdb-sql-release-cached rdb-sql-release)

(define (rdb-show-tables)
  (rdb-sql "select rdb$relation_name,rdb$system_flag from rdb$relations"))

(define (rdb-show-table x)
  (let ((s (rdb-sql-prepare *rdb-sql-association*
			(string-append "select * from " x)))
	(l nil))
    (describe-statement s)
    (set! l (rdb-sql-statement-selects s))
    (rdb-sql-release s)
    l))
