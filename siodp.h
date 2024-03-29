/* Scheme In One Defun, but in C this time.

 *                        COPYRIGHT (c) 1988-1992 BY                        *
 *        PARADIGM ASSOCIATES INCORPORATED, CAMBRIDGE, MASSACHUSETTS.       *
 *        See the source file SLIB.C for more information.                  *

Declarations which are private to SLIB.C internals.

*/


extern char *tkbuffer;
extern LISP heap, heap_end, heap_org;
extern LISP truth;

struct user_type_hooks {
    LISP (*gc_relocate)(LISP);

    void (*gc_scan)(LISP);

    LISP (*gc_mark)(LISP);

    void (*gc_free)(LISP);

    void (*prin1)(LISP, FILE *);

    LISP (*leval)(LISP, LISP *, LISP *);

    long (*c_sxhash)(LISP, long);

    LISP (*fast_print)(LISP, LISP);

    LISP (*fast_read)(int, LISP);

    LISP (*equal)(LISP, LISP);
};

struct catch_frame {
    LISP tag;
    LISP retval;
    jmp_buf cframe;
    struct catch_frame *next;
};

struct gc_protected {
    LISP *location;
    long length;
    struct gc_protected *next;
};

#define NEWCELL(_into, _type)          \
{if (gc_kind_copying == 1)            \
   {if ((_into = heap) >= heap_end)   \
      gc_fatal_error();               \
    heap = _into+1;}                  \
 else                                 \
   {if NULLP(freelist)                \
      gc_for_newcell();               \
    _into = freelist;                 \
    freelist = CDR(freelist);         \
    ++gc_cells_allocated;}            \
 (*_into).gc_mark = 0;                \
 (*_into).type = (short) _type;}

#ifdef THINK_C
extern int ipoll_counter;
void full_interrupt_poll(int *counter);
#define INTERRUPT_CHECK() if (--ipoll_counter < 0) full_interrupt_poll(&ipoll_counter)
#else
#define INTERRUPT_CHECK()
#endif

extern char *stack_limit_ptr;

#define STACK_LIMIT(_ptr, _amt) (((char *)_ptr) - (_amt))

#define STACK_CHECK(_ptr) \
  if (((char *) (_ptr)) < stack_limit_ptr) err_stack((char *) _ptr);

void err_stack(char *);

#if defined(VMS) && defined(VAX)
#define SIG_restargs ,...
#else
#define SIG_restargs
#endif

void handle_sigfpe(int sig SIG_restargs);

void handle_sigint(int sig SIG_restargs);

void err_ctrl_c(void);

double myruntime(void);

void fput_st(FILE *f, char *st);

void put_st(char *st);

void grepl_puts(char *, void (*)(char *));

void gc_fatal_error(void);

char *must_malloc(unsigned long size);

LISP gen_intern(char *name, long copyp);

void scan_registers(void);

void init_storage_1(void);

struct user_type_hooks *get_user_type_hooks(long type);

LISP get_newspace(void);

void scan_newspace(LISP newspace);

void free_oldspace(LISP space, LISP end);

void gc_stop_and_copy(void);

void gc_for_newcell(void);

void gc_mark_and_sweep(void);

void gc_ms_stats_start(void);

void gc_ms_stats_end(void);

void gc_mark(LISP ptr, long last_index_of_gc_traced_objs, long previous_obj_selected_field);

void mark_protected_registers(void);

void mark_locations(LISP *start, LISP *end);

void mark_locations_array(LISP *x, long n);

void gc_sweep(void);

LISP leval_args(LISP l, LISP env);

LISP extend_env(LISP actuals, LISP formals, LISP env);

LISP envlookup(LISP var, LISP env);

LISP setvar(LISP var, LISP val, LISP env);

LISP leval_setq(LISP args, LISP env);

LISP syntax_define(LISP args);

LISP leval_define(LISP args, LISP env);

LISP leval_if(LISP *pform, LISP *penv);

LISP leval_lambda(LISP args, LISP env);

LISP leval_progn(LISP *pform, LISP *penv);

LISP leval_or(LISP *pform, LISP *penv);

LISP leval_and(LISP *pform, LISP *penv);

LISP leval_catch_1(LISP forms, LISP env);

LISP leval_catch(LISP args, LISP env);

LISP lthrow(LISP tag, LISP value);

LISP leval_let(LISP *pform, LISP *penv);

LISP reverse(LISP l);

LISP let_macro(LISP form);

LISP leval_quote(LISP args, LISP env);

LISP leval_tenv(LISP args, LISP env);

int flush_ws(struct gen_readio *f, char *eoferr);

int f_getc(FILE *f);

void f_ungetc(int c, FILE *f);

LISP readtl(struct gen_readio *f);

LISP lreadr(struct gen_readio *f);

LISP lreadparen(struct gen_readio *f);

LISP arglchk(LISP x);

void init_storage_a1(long type);

void init_storage_a(void);

LISP array_gc_relocate(LISP ptr);

void array_gc_scan(LISP ptr);

LISP array_gc_mark(LISP ptr);

void array_gc_free(LISP ptr);

void array_prin1(LISP ptr, FILE *f);

long array_sxhaxh(LISP, long);

LISP array_fast_print(LISP, LISP);

LISP array_fast_read(int, LISP);

LISP array_equal(LISP, LISP);

long array_sxhash(LISP, long);

int rfs_getc(unsigned char **p);

void rfs_ungetc(unsigned char c, unsigned char **p);

void err1_aset1(LISP i);

void err2_aset1(LISP v);

LISP lreadstring(struct gen_readio *f);

void file_gc_free(LISP ptr);

void file_prin1(LISP ptr, FILE *f);

LISP fopen_c(char *name, char *how);

LISP fopen_l(LISP name, LISP how);

LISP fclose_l(LISP p);

FILE *get_c_file(LISP p, FILE *deflt);

LISP lgetc(LISP p);

LISP lputc(LISP c, LISP p);

LISP lputs(LISP str, LISP p);

LISP lftell(LISP file);

LISP lfseek(LISP file, LISP offset, LISP direction);

LISP lfread(LISP size, LISP file);

LISP lfwrite(LISP string, LISP file);


LISP leval_while(LISP args, LISP env);

void init_subrs_a(void);

void init_subrs_1(void);

long href_index(LISP table, LISP key);

void put_long(long, FILE *);

long get_long(FILE *);

long fast_print_table(LISP obj, LISP table);

LISP stack_limit(LISP, LISP);

void err0(void);

void pr(LISP);

void prp(LISP *);

LISP closure_code(LISP exp);

LISP closure_env(LISP exp);

LISP lwhile(LISP form, LISP env);

LISP llength(LISP obj);

void gc_kind_check(void);

LISP mark_clock_start(void);

LISP mark_clock_end(void);

LISP print_clock_time_cost(void);

void mark_gc_clock_start(void);

void mark_gc_clock_end(void);

/**
 * macros for gc assertion with new_struct_instance features
 */

// stages of "assert-dead" assertion
#define ASSERT_DEAD_STAGE_ROUGH 1
#define ASSERT_DEAD_STAGE_DETAILED 2

// assert-dead mark
#define HAS_BEEN_ASSERTED 1
#define HAD_BEEN_ASSERTED (-1)

#define TYPE_STR_CONS "CONS"
#define TYPE_STR_FLONUM "FLONUM"
#define TYPE_STR_SYMBOL "SYMBOL"
#define TYPE_STR_CLOSURE "CLOSURE"
#define TYPE_STR_STRING "STRING"
#define TYPE_STR_FILE "FILE"
#define TYPE_STR_NO_SUCH_TYPE "NO SUCH TYPE: "

// data structure field type ids
#define CONS_CAR_TYPE_ID 0
#define CONS_CDR_TYPE_ID 1
#define SYMBOL_VCELL_TYPE_ID 0
#define CLOSURE_CODE_TYPE_ID 0
#define CLOSURE_ENV_TYPE_ID 1
