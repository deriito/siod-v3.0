/* Scheme In One Defun, but in C this time.
 
 *                      COPYRIGHT (c) 1988-1994 BY                          *
 *        PARADIGM ASSOCIATES INCORPORATED, CAMBRIDGE, MASSACHUSETTS.       *
 *			   ALL RIGHTS RESERVED                              *

Permission to use, copy, modify, distribute and sell this software
and its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appear in all copies
and that both that copyright notice and this permission notice appear
in supporting documentation, and that the name of Paradigm Associates
Inc not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

PARADIGM DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
PARADIGM BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

*/

/*

gjc@paradigm.com, gjc@mitech.com

Paradigm Associates Inc          Phone: 617-492-6079
29 Putnam Ave, Suite 6
Cambridge, MA 02138


   Release 1.0: 24-APR-88
   Release 1.1: 25-APR-88, added: macros, predicates, load. With additions by
    Barak.Pearlmutter@DOGHEN.BOLTZ.CS.CMU.EDU: Full flonum recognizer,
    cleaned up uses of NULL/0. Now distributed with siod.scm.
   Release 1.2: 28-APR-88, name changes as requested by JAR@AI.AI.MIT.EDU,
    plus some bug fixes.
   Release 1.3: 1-MAY-88, changed env to use frames instead of alist.
    define now works properly. vms specific function edit.
   Release 1.4 20-NOV-89. Minor Cleanup and remodularization.
    Now in 3 files, siod.h, slib.c, siod.c. Makes it easier to write your
    own main loops. Some short-int changes for lightspeed C included.
   Release 1.5 29-NOV-89. Added startup flag -g, select stop and copy
    or mark-and-sweep garbage collection, which assumes that the stack/register
    marking code is correct for your architecture. 
   Release 2.0 1-DEC-89. Added repl_hooks, Catch, Throw. This is significantly
    different enough (from 1.3) now that I'm calling it a major release.
   Release 2.1 4-DEC-89. Small reader features, dot, backquote, comma.
   Release 2.2 5-DEC-89. gc,read,print,eval, hooks for user defined datatypes.
   Release 2.3 6-DEC-89. save_forms, obarray intern mechanism. comment char.
   Release 2.3a......... minor speed-ups. i/o interrupt considerations.
   Release 2.4 27-APR-90 gen_readr, for read-from-string.
   Release 2.5 18-SEP-90 arrays added to SIOD.C by popular demand. inums.
   Release 2.6 11-MAR-92 function prototypes, some remodularization.
   Release 2.7 20-MAR-92 hash tables, fasload. Stack check.
   Release 2.8  3-APR-92 Bug fixes, \n syntax in string reading.
   Release 2.9 28-AUG-92 gc sweep bug fix. fseek, ftell, etc. Change to
    envlookup to allow (a . rest) suggested by bowles@is.s.u-tokyo.ac.jp.
   Release 2.9a 10-AUG-93. Minor changes for Windows NT.
   Release 3.0  12-JAN-94. Release it, include changes/cleanup recommended by
    andreasg@nynexst.com for the OS2 C++ compiler. Compilation and running
    tested using DEC C, VAX C. WINDOWS NT. GNU C on SPARC.
  */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <signal.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include "siod.h"
#include "siodp.h"

#include "uthash.h"
#include "utarray.h"

char *siod_version(void) { return ("3.0 FIELD TEST 9-MAR-94"); }

long nheaps = 2;
LISP *heaps;
LISP heap, heap_end, heap_org;
long heap_size = 5000; // at least 683 cells to launch
long old_heap_used;
long gc_status_flag = 1;
char *init_file = (char *) NULL;
char *tkbuffer = NULL;
long gc_kind_copying = 0;
long gc_cells_allocated = 0;
double gc_time_taken;
LISP *stack_start_ptr;
LISP freelist;
jmp_buf errjmp;
long errjmp_ok = 0;
long nointerrupt = 1;
long interrupt_differed = 0;
LISP oblistvar = NIL;
LISP truth = NIL;
LISP eof_val = NIL;
LISP sym_errobj = NIL;
LISP sym_progn = NIL;
LISP sym_lambda = NIL;
LISP sym_quote = NIL;
LISP sym_dot = NIL;
LISP sym_after_gc = NIL;
LISP unbound_marker = NIL;
LISP *obarray;
long obarray_dim = 100;
struct catch_frame *catch_framep = (struct catch_frame *) NULL;

void (*repl_puts)(char *) = NULL;

LISP (*repl_read)(void) = NULL;

LISP (*repl_eval)(LISP) = NULL;

void (*repl_print)(LISP) = NULL;

LISP *inums;
long inums_dim = 256;
struct user_type_hooks *user_types = NULL;
struct gc_protected *protected_registers = NULL;
jmp_buf save_regs_gc_mark;
double gc_rt;
long gc_cells_collected;
char *user_ch_readm = "";
char *user_te_readm = "";

LISP (*user_readm)(int, struct gen_readio *) = NULL;

LISP (*user_readt)(char *, long, int *) = NULL;

void (*fatal_exit_hook)(void) = NULL;

#ifdef THINK_C
int ipoll_counter = 0;
#endif

char *stack_limit_ptr = NULL;
long stack_size =
#ifdef THINK_C
        10000;
#else
        50000;
#endif

// クラスオブジェクトを格納
LISP *class_objs = NULL; // TODO

// 行号记录功能是否开启的总开关
short recording_assign_site_on = 1;

// 用于记录标记过的对象
static LISP *gc_traced_objs = NULL;
// 与gc_traced_objs的元素一一对应，用于记录具体是哪个field指向了下一个对象
static long *gc_traced_obj_fields = NULL;

// 参照パスの各要素のデータ構造
typedef struct {
    short lisp_obj_type;
    long assign_slot_type;
    LISP struct_def_obj; // if it has
    short checked; // 该类型的obj被记录过行号信息
} ObjLinkElement;

typedef struct {
    UT_array *link_data;
    long active_ele_idx; // 用于标记激活的type
} ObjectLink;

// 当前所存在的object links
static UT_array *object_links = NULL;

// 各个位置的assert-dead的调用信息
typedef struct {
    long line_num;
    short stage;
    UT_hash_handle hh; /* makes this structure hashtable */
} AssertDeadCallInfo;

// assert-dead的相关信息
static AssertDeadCallInfo *assert_dead_call_info_table = NULL;

// 実行のかかるCPU時間の計算のため
static clock_t clock_start = 0;
static clock_t clock_end = 0;
static double gc_clock_time_taken = 0.0;
static clock_t gc_clock_start = 0;
static clock_t gc_clock_end = 0;

LISP get_struct_def_obj(LISP ptr) {
    if (tc_struct_instance == ptr->type) {
        return (ptr->storage_as.struct_instance.struct_def_obj);
    }

    if (tc_struct_instance_with_rec == ptr->type) {
        return (ptr->storage_as.struct_instance_with_rec.struct_def_obj);
    }

    return (NIL);
}

short struct_def_eqp(LISP ptr, ObjLinkElement *obj_path_ele) {
    if (ptr->type != tc_struct_instance && ptr->type != tc_struct_instance_with_rec) {
        return 0;
    }

    if (obj_path_ele->lisp_obj_type != tc_struct_instance && obj_path_ele->lisp_obj_type != tc_struct_instance_with_rec) {
        return 0;
    }

    LISP struct_def = get_struct_def_obj(ptr);

    LISP ptr_class_name_sym_obj = struct_def->storage_as.struct_def.class_name_sym;
    LISP ref_path_ele_class_name_sym_obj = obj_path_ele->struct_def_obj->storage_as.struct_def.class_name_sym;
    if (0 != strcmp(ptr_class_name_sym_obj->storage_as.symbol.pname, ref_path_ele_class_name_sym_obj->storage_as.symbol.pname)) {
        return 0;
    }

    return 1;
}

short compare_traced_objs_and_obj_link(long last_index_of_gc_traced_objs, ObjectLink *obj_link) {
    UT_array *link_data = obj_link->link_data;
    if (NULL == link_data) {
        return 0;
    }

    long len = utarray_len(link_data);
    if (len - 1 != last_index_of_gc_traced_objs) {
        return 0;
    }

    for (long i = 0; i < len; ++i) {
        ObjLinkElement *tmp_ol_ele = utarray_eltptr(link_data, i);
        LISP tmp_ptr = gc_traced_objs[i];

        if ((i != len - 1) && (gc_traced_obj_fields[i] != tmp_ol_ele->assign_slot_type)) {
            return 0;
        }

        if (tmp_ptr->type != tmp_ol_ele->lisp_obj_type) {
            if (struct_def_eqp(tmp_ptr, tmp_ol_ele)) {
                continue;
            }
            return 0;
        }
    }

    return 1;
}

ObjectLink *find_the_obj_link(long last_index_of_gc_traced_objs) {
    if (NULL == object_links) {
        return NULL;
    }

    for (long i = 0; i < utarray_len(object_links); ++i) {
        ObjectLink *obj_link = utarray_eltptr(object_links, i);
        if (NULL == obj_link) {
            continue;
        }
        if (!compare_traced_objs_and_obj_link(last_index_of_gc_traced_objs, obj_link)) {
            continue;
        }
        return obj_link;
    }

    return NULL;
}

short judge_ref_pattern(LISP assign_obj, long field_idx, LISP assigned_obj, ObjLinkElement *assign_ele, ObjLinkElement *assigned_ele) {
    if (field_idx != assign_ele->assign_slot_type) {
        return 0;
    }
    if (assign_obj->type != assign_ele->lisp_obj_type && !struct_def_eqp(assign_obj, assign_ele)) {
        return 0;
    }
    if (assigned_obj->type != assigned_ele->lisp_obj_type && !struct_def_eqp(assigned_obj, assigned_ele)) {
        return 0;
    }
    return 1;
}

ObjLinkElement *is_focusing_ref_pattern(LISP assign_obj, long field_idx, LISP assigned_obj) {
    if (NULL == object_links) {
        return NULL;
    }

    long link_num = utarray_len(object_links);
    for (long i = 0; i < link_num; ++i) {
        ObjectLink *tmp_link = utarray_eltptr(object_links, i);
        UT_array *link_data = tmp_link->link_data;
        if (NULL == link_data) {
            continue;
        }
        long active_ele_idx = tmp_link->active_ele_idx;
        if (active_ele_idx < 0) {
            continue;
        }
        long link_len = utarray_len(link_data);
        if (active_ele_idx >= link_len - 1L) {
            continue;
        }
        ObjLinkElement *assign_ele = utarray_eltptr(link_data, active_ele_idx);
        ObjLinkElement *assigned_ele = utarray_eltptr(tmp_link->link_data, active_ele_idx + 1L);
        if (!judge_ref_pattern(assign_obj, field_idx, assigned_obj, assign_ele, assigned_ele)) {
            continue;
        }
        return assign_ele;
    }
    return NULL;
}

void try_record_assign_site(LISP assign_obj, LISP assigned_obj, long assign_site, long field_index) {
    if (!recording_assign_site_on) {
        return;
    }

    if (NULL == assign_obj || NULL == assigned_obj) {
        return;
    }

    // 内部的CONSを無視する
    while (NULL != assigned_obj && assigned_obj->type == tc_cons && !assigned_obj->storage_as.cons.is_external_cons) {
        assigned_obj = CAR(assigned_obj);
    }

    ObjLinkElement *assign_element = is_focusing_ref_pattern(assign_obj, field_index, assigned_obj);
    if (NULL == assign_element) {
        return;
    }

    if (tc_cons == assign_obj->type) {
        if (field_index) {
            assign_obj->storage_as.cons.car_assign_site = assign_site;
        } else {
            assign_obj->storage_as.cons.cdr_assign_site = assign_site;
        }
    } else if (tc_symbol == assign_obj->type) {
        assign_obj->storage_as.symbol.vcell_assign_site = assign_site;
    } else if (tc_closure == assign_obj->type) {
        if (field_index) {
            assign_obj->storage_as.closure.env_assign_site = assign_site;
        } else {
            assign_obj->storage_as.closure.code_assign_site = assign_site;
        }
    } else if (tc_struct_instance_with_rec == assign_obj->type) {
        LISP sd = assign_obj->storage_as.struct_instance_with_rec.struct_def_obj;
        long fi = -1L;
        for (long i = 0; i < sd->storage_as.struct_def.length; ++i) {
            long tmp_index = sd->storage_as.struct_def.assign_field_indexes[i];
            if (field_index == tmp_index) {
                fi = i;
                break;
            }
        }
        if (fi < 0) {
            return;
        }
        assign_obj->storage_as.struct_instance_with_rec.assign_sites[fi] = assign_site;
    } else {
        return;
    }

    assign_obj->is_assign_info_recorded = 1;
    assign_element->checked = 1;
}

void process_cla(int argc, char **argv, int warnflag) {
    int k;
    char *ptr;
    for (k = 1; k < argc; ++k) {
        if (strlen(argv[k]) < 2) continue;
        if (argv[k][0] != '-') {
            if (warnflag) printf("bad arg: %s\n", argv[k]);
            continue;
        }
        switch (argv[k][1]) {
            case 'h':
                heap_size = atol(&(argv[k][2]));
                if (ptr = strchr(&(argv[k][2]), ':'))
                    nheaps = atol(&ptr[1]);
                break;
            case 'o':
                obarray_dim = atol(&(argv[k][2]));
                break;
            case 'i':
                init_file = &(argv[k][2]);
                break;
            case 'n':
                inums_dim = atol(&(argv[k][2]));
                break;
            case 'g':
                gc_kind_copying = atol(&(argv[k][2]));
                break;
            case 's':
                stack_size = atol(&(argv[k][2]));
                break;
            default:
                if (warnflag) printf("bad arg: %s\n", argv[k]);
        }
    }
}

void print_welcome(void) {
    printf("Welcome to SIOD, Scheme In One Defun, Version %s\n",
           siod_version());
    printf("(C) Copyright 1988-1994 Paradigm Associates Inc.\n");
}

void print_hs_1(void) {
    printf("%ld heaps. size = %ld cells, %ld bytes. %ld inums. GC is %s\n",
           nheaps,
           heap_size, heap_size * sizeof(struct obj),
           inums_dim,
           (gc_kind_copying == 1) ? "stop and copy" : "mark and sweep");
}

void print_hs_2(void) {
    if (gc_kind_copying == 1)
        printf("heaps[0] at %p, heaps[1] at %p\n", heaps[0], heaps[1]);
    else
        printf("heaps[0] at %p\n", heaps[0]);
}

long no_interrupt(long n) {
    long x;
    x = nointerrupt;
    nointerrupt = n;
    if ((nointerrupt == 0) && (interrupt_differed == 1)) {
        interrupt_differed = 0;
        err_ctrl_c();
    }
    return (x);
}

void handle_sigfpe(int sig SIG_restargs) {
    signal(SIGFPE, handle_sigfpe);
    err("floating point exception", NIL);
}

void handle_sigint(int sig SIG_restargs) {
    signal(SIGINT, handle_sigint);
    if (nointerrupt == 1)
        interrupt_differed = 1;
    else
        err_ctrl_c();
}

void err_ctrl_c(void) { err("control-c interrupt", NIL); }

LISP get_eof_val(void) { return (eof_val); }

long repl_driver(long want_sigint, long want_init, struct repl_hooks *h) {
    int k;
    struct repl_hooks hd;
    LISP stack_start;
    stack_start_ptr = &stack_start;
    stack_limit_ptr = STACK_LIMIT(stack_start_ptr, stack_size);
    k = setjmp(errjmp);
    if (k == 2) return (2);
    if (want_sigint) signal(SIGINT, handle_sigint);
    signal(SIGFPE, handle_sigfpe);
    catch_framep = (struct catch_frame *) NULL;
    errjmp_ok = 1;
    interrupt_differed = 0;
    nointerrupt = 0;
    if (want_init && init_file && (k == 0)) vload(init_file, 0);
    if (!h) {
        hd.repl_puts = repl_puts;
        hd.repl_read = repl_read;
        hd.repl_eval = repl_eval;
        hd.repl_print = repl_print;
        return (repl(&hd));
    } else
        return (repl(h));
}

static void ignore_puts(char *st) {}

static void noprompt_puts(char *st) {
    if (strcmp(st, "> ") != 0)
        put_st(st);
}

static char *repl_c_string_arg = NULL;
static long repl_c_string_flag = 0;

static LISP repl_c_string_read(void) {
    LISP s;
    if (repl_c_string_arg == NULL)
        return (get_eof_val());
    s = strcons(strlen(repl_c_string_arg), repl_c_string_arg);
    repl_c_string_arg = NULL;
    return (read_from_string(s));
}

static void ignore_print(LISP x) { repl_c_string_flag = 1; }

static void not_ignore_print(LISP x) {
    repl_c_string_flag = 1;
    lprint(x);
}

long repl_c_string(char *str,
                   long want_sigint, long want_init, long want_print) {
    struct repl_hooks h;
    long retval;
    if (want_print)
        h.repl_puts = noprompt_puts;
    else
        h.repl_puts = ignore_puts;
    h.repl_read = repl_c_string_read;
    h.repl_eval = NULL;
    if (want_print)
        h.repl_print = not_ignore_print;
    else
        h.repl_print = ignore_print;
    repl_c_string_arg = str;
    repl_c_string_flag = 0;
    retval = repl_driver(want_sigint, want_init, &h);
    if (retval != 0)
        return (retval);
    else if (repl_c_string_flag == 1)
        return (0);
    else
        return (2);
}

#ifdef unix
                                                                                                                        #include <sys/types.h>
#include <sys/times.h>
double myruntime(void)
{double total;
 struct tms b;
 times(&b);
 total = b.tms_utime;
 total += b.tms_stime;
 return(total / 60.0);}
#else
#if defined(THINK_C) | defined(WIN32) | defined(VMS)
                                                                                                                        #ifndef CLOCKS_PER_SEC
#define CLOCKS_PER_SEC CLK_TCK
#endif
double myruntime(void)
{return(((double) clock()) / ((double) CLOCKS_PER_SEC));}
#else

double myruntime(void) {
    time_t x;
    time(&x);
    return ((double) x);
}

#endif
#endif

void set_repl_hooks(void (*puts_f)(char *),
                    LISP (*read_f)(void),
                    LISP (*eval_f)(LISP),
                    void (*print_f)(LISP)) {
    repl_puts = puts_f;
    repl_read = read_f;
    repl_eval = eval_f;
    repl_print = print_f;
}

void fput_st(FILE *f, char *st) {
    long flag;
    flag = no_interrupt(1);
    fprintf(f, "%s", st);
    no_interrupt(flag);
}

void put_st(char *st) { fput_st(stdout, st); }

void grepl_puts(char *st, void (*repl_puts)(char *)) {
    if (repl_puts == NULL) {
        put_st(st);
        fflush(stdout);
    } else
        (*repl_puts)(st);
}

long repl(struct repl_hooks *h) {
    LISP x, cw = 0;
    double rt;
    while (1) {
        if ((gc_kind_copying == 1) && ((gc_status_flag) || heap >= heap_end)) {
            rt = myruntime();
            gc_stop_and_copy();
            sprintf(tkbuffer,
                    "GC took %g seconds, %ld compressed to %ld, %ld free\n",
                    myruntime() - rt, old_heap_used, heap - heap_org, heap_end - heap);
            grepl_puts(tkbuffer, h->repl_puts);
        }
        grepl_puts("> ", h->repl_puts);
        if (h->repl_read == NULL)
            x = lread();
        else
            x = (*h->repl_read)();
        if EQ(x, eof_val) break;
        rt = myruntime();
        if (gc_kind_copying == 1)
            cw = heap;
        else {
            gc_cells_allocated = 0;
            gc_time_taken = 0.0;
        }
        if (h->repl_eval == NULL)
            x = leval(x, NIL);
        else
            x = (*h->repl_eval)(x);
        if (gc_kind_copying == 1)
            sprintf(tkbuffer,
                    "Evaluation took %g seconds %ld cons work\n",
                    myruntime() - rt,
                    heap - cw);
        else
            sprintf(tkbuffer,
                    "Evaluation took %g seconds (%g in gc) %ld cons work\n",
                    myruntime() - rt,
                    gc_time_taken,
                    gc_cells_allocated);
        grepl_puts(tkbuffer, h->repl_puts);
        if (h->repl_print == NULL)
            lprint(x);
        else
            (*h->repl_print)(x);
    }
    return (0);
}

void set_fatal_exit_hook(void (*fcn)(void)) { fatal_exit_hook = fcn; }

LISP err(char *message, LISP x) {
    nointerrupt = 1;
    if NNULLP(x)
        printf("ERROR: %s (see errobj)\n", message);
    else
        printf("ERROR: %s\n", (message) ? message : "?");
    if (errjmp_ok == 1) {
        setvar(sym_errobj, x, NIL);
        longjmp(errjmp, 1);
    }
    printf("FATAL ERROR DURING STARTUP OR CRITICAL CODE SECTION\n");
    if (fatal_exit_hook)
        (*fatal_exit_hook)();
    else
        exit(1);
    return (NIL);
}

LISP errswitch(void) { return (err("BUG. Reached impossible case", NIL)); }

void err_stack(char *ptr)
/* The user could be given an option to continue here */
{ err("the currently assigned stack limit has been exceded", NIL); }

LISP stack_limit(LISP amount, LISP silent) {
    if NNULLP(amount) {
        stack_size = get_c_long(amount);
        stack_limit_ptr = STACK_LIMIT(stack_start_ptr, stack_size);
    }
    if NULLP(silent) {
        sprintf(tkbuffer, "Stack_size = %ld bytes, [%p,%p]\n",
                stack_size, stack_start_ptr, stack_limit_ptr);
        put_st(tkbuffer);
        return (NIL);
    } else
        return (flocons(stack_size));
}

char *get_c_string(LISP x) {
    if TYPEP(x, tc_symbol)
        return (PNAME(x));
    else if TYPEP(x, tc_string)
        return (x->storage_as.string.data);
    else
        err("not a symbol or string", x);
    return (NULL);
}

LISP lerr(LISP message, LISP x) {
    err(get_c_string(message), x);
    return (NIL);
}

void gc_fatal_error(void) { err("ran out of storage", NIL); }

LISP newcell(long type) {
    LISP z;
    NEWCELL(z, type);
    return (z);
}

LISP cons(LISP x, LISP y) {
    LISP z;
    NEWCELL(z, tc_cons);
    CAR(z) = x;
    CDR(z) = y;
    return (z);
}

LISP external_cons(LISP x, LISP y, LISP line_num) {
    LISP new_cons = cons(x, y);
    new_cons->storage_as.cons.is_external_cons = 1;
    if (recording_assign_site_on) {
        long ln = (long) line_num->storage_as.flonum.data;
        try_record_assign_site(new_cons, x, ln, 1);
        try_record_assign_site(new_cons, y, ln, 0);
    }
    return (new_cons);
}

LISP consp(LISP x) { if CONSP(x) return (truth); else return (NIL); }

LISP car(LISP x) {
    switch TYPE(x) {
        case tc_nil:
            return (NIL);
        case tc_cons:
            return (CAR(x));
        default:
            return (err("wta to car", x));
    }
}

LISP cdr(LISP x) {
    switch TYPE(x) {
        case tc_nil:
            return (NIL);
        case tc_cons:
            return (CDR(x));
        default:
            return (err("wta to cdr", x));
    }
}

LISP setcar(LISP cell, LISP value) {
    if NCONSP(cell) err("wta to setcar", cell);
    return (CAR(cell) = value);
}

LISP setcar_exteral(LISP cell, LISP value, LISP line_num) {
    LISP result = setcar(cell, value);
    if (recording_assign_site_on) {
        try_record_assign_site(cell, value, (long) line_num->storage_as.flonum.data, 1);
    }
    return (result);
}

LISP setcdr(LISP cell, LISP value) {
    if NCONSP(cell) err("wta to setcdr", cell);
    return (CDR(cell) = value);
}

LISP setcdr_exteral(LISP cell, LISP value, LISP line_num) {
    LISP result = setcdr(cell, value);
    if (recording_assign_site_on) {
        try_record_assign_site(cell, value, (long) line_num->storage_as.flonum.data, 0);
    }
    return (result);
}

LISP flocons(double x) {
    LISP z;
    long n;
    if ((inums_dim > 0) &&
        ((x - (n = (long) x)) == 0) &&
        (x >= 0) &&
        (n < inums_dim))
        return (inums[n]);
    NEWCELL(z, tc_flonum);
    FLONM(z) = x;
    return (z);
}

LISP numberp(LISP x) { if FLONUMP(x) return (truth); else return (NIL); }

LISP plus(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to plus", x);
    if NFLONUMP(y) err("wta(2nd) to plus", y);
    return (flocons(FLONM(x) + FLONM(y)));
}

LISP ltimes(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to times", x);
    if NFLONUMP(y) err("wta(2nd) to times", y);
    return (flocons(FLONM(x) * FLONM(y)));
}

LISP difference(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to difference", x);
    if NFLONUMP(y) err("wta(2nd) to difference", y);
    return (flocons(FLONM(x) - FLONM(y)));
}

LISP quotient(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to quotient", x);
    if NFLONUMP(y) err("wta(2nd) to quotient", y);
    return (flocons(FLONM(x) / FLONM(y)));
}

LISP greaterp(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to greaterp", x);
    if NFLONUMP(y) err("wta(2nd) to greaterp", y);
    if (FLONM(x) > FLONM(y)) return (truth);
    return (NIL);
}

LISP lessp(LISP x, LISP y) {
    if NFLONUMP(x) err("wta(1st) to lessp", x);
    if NFLONUMP(y) err("wta(2nd) to lessp", y);
    if (FLONM(x) < FLONM(y)) return (truth);
    return (NIL);
}

LISP eq(LISP x, LISP y) { if EQ(x, y) return (truth); else return (NIL); }

LISP eql(LISP x, LISP y) {
    if EQ(x, y) return (truth);
    else if NFLONUMP(x) return (NIL);
    else if NFLONUMP(y) return (NIL); else if (FLONM(x) == FLONM(y)) return (truth);
    return (NIL);
}

LISP symcons(char *pname, LISP vcell) {
    LISP z;
    NEWCELL(z, tc_symbol);
    PNAME(z) = pname;
    VCELL(z) = vcell;
    return (z);
}

LISP symbolp(LISP x) { if SYMBOLP(x) return (truth); else return (NIL); }

LISP symbol_boundp(LISP x, LISP env) {
    LISP tmp;
    if NSYMBOLP(x) err("not a symbol", x);
    tmp = envlookup(x, env);
    if NNULLP(tmp) return (truth);
    if EQ(VCELL(x), unbound_marker) return (NIL); else return (truth);
}

LISP symbol_value(LISP x, LISP env) {
    LISP tmp;
    if NSYMBOLP(x) err("not a symbol", x);
    tmp = envlookup(x, env);
    if NNULLP(tmp) return (CAR(tmp));
    tmp = VCELL(x);
    if EQ(tmp, unbound_marker) err("unbound variable", x);
    return (tmp);
}

char *must_malloc(unsigned long size) {
    char *tmp;
    tmp = (char *) malloc(size);
    if (tmp == (char *) NULL) err("failed to allocate storage from system", NIL);
    return (tmp);
}

LISP gen_intern(char *name, long copyp) {
    LISP l, sym, sl;
    char *cname;
    long hash = 0, n, c, flag;
    flag = no_interrupt(1);
    if (obarray_dim > 1) {
        hash = 0;
        n = obarray_dim;
        cname = name;
        while ((c = *cname++)) hash = ((hash * 17) ^ c) % n;
        sl = obarray[hash];
    } else
        sl = oblistvar;
    for (l = sl; NNULLP(l); l = CDR(l))
        if (strcmp(name, PNAME(CAR(l))) == 0) {
            no_interrupt(flag);
            return (CAR(l));
        }
    if (copyp == 1) {
        cname = (char *) must_malloc(strlen(name) + 1);
        strcpy(cname, name);
    } else
        cname = name;
    sym = symcons(cname, unbound_marker);
    if (obarray_dim > 1) obarray[hash] = cons(sym, sl);
    oblistvar = cons(sym, oblistvar);
    no_interrupt(flag);
    return (sym);
}

LISP cintern(char *name) { return (gen_intern(name, 0)); }

LISP rintern(char *name) { return (gen_intern(name, 1)); }

LISP intern(LISP name) { return (rintern(get_c_string(name))); }

LISP subrcons(long type, char *name, SUBR_FUNC f) {
    LISP z;
    NEWCELL(z, type);
    (*z).storage_as.subr.name = name;
    (*z).storage_as.subr0.f = f;
    return (z);
}

LISP closure(LISP env, LISP code) {
    LISP z;
    NEWCELL(z, tc_closure);
    (*z).storage_as.closure.env = env;
    (*z).storage_as.closure.code = code;
    return (z);
}

void gc_protect(LISP *location) {
    gc_protect_n(location, 1);
}

void gc_protect_n(LISP *location, long n) {
    struct gc_protected *reg;
    reg = (struct gc_protected *) must_malloc(sizeof(struct gc_protected));
    (*reg).location = location;
    (*reg).length = n;
    (*reg).next = protected_registers;
    protected_registers = reg;
}

void gc_protect_sym(LISP *location, char *st) {
    *location = cintern(st);
    gc_protect(location);
}

void scan_registers(void) {
    struct gc_protected *reg;
    LISP *location;
    long j, n;
    for (reg = protected_registers; reg; reg = (*reg).next) {
        location = (*reg).location;
        n = (*reg).length;
        for (j = 0; j < n; ++j)
            location[j] = gc_relocate(location[j]);
    }
}

void init_storage(void) {
    long j;
    init_storage_1();
    init_storage_a();
    set_gc_hooks(tc_c_file, 0, 0, 0, file_gc_free, &j);
    set_print_hooks(tc_c_file, file_prin1);
}

void init_storage_1(void) {
    LISP ptr, next, end;
    long j;
    tkbuffer = (char *) must_malloc(TKBUFFERN + 1);
    if (((gc_kind_copying == 1) && (nheaps != 2)) || (nheaps < 1))
        err("invalid number of heaps", NIL);
    heaps = (LISP *) must_malloc(sizeof(LISP) * nheaps);
    for (j = 0; j < nheaps; ++j) heaps[j] = NULL;
    heaps[0] = (LISP) must_malloc(sizeof(struct obj) * heap_size);
    heap = heaps[0];
    heap_org = heap;
    heap_end = heap + heap_size;
    if (gc_kind_copying == 1)
        heaps[1] = (LISP) must_malloc(sizeof(struct obj) * heap_size);
    else {
        ptr = heap_org;
        end = heap_end;
        while (1) {
            (*ptr).type = tc_free_cell;
            next = ptr + 1;
            if (next < end) {
                CDR(ptr) = next;
                ptr = next;
            } else {
                CDR(ptr) = NIL;
                break;
            }
        }
        freelist = heap_org;
    }
    gc_protect(&oblistvar);
    if (obarray_dim > 1) {
        obarray = (LISP *) must_malloc(sizeof(LISP) * obarray_dim);
        for (j = 0; j < obarray_dim; ++j)
            obarray[j] = NIL;
        gc_protect_n(obarray, obarray_dim);
    }
    unbound_marker = cons(cintern("**unbound-marker**"), NIL);
    gc_protect(&unbound_marker);
    eof_val = cons(cintern("eof"), NIL);
    gc_protect(&eof_val);
    gc_protect_sym(&truth, "t");
    setvar(truth, truth, NIL);
    setvar(cintern("nil"), NIL, NIL);
    // 是这里将let与let-internal-macro相关联
    setvar(cintern("let"), cintern("let-internal-macro"), NIL);
    gc_protect_sym(&sym_errobj, "errobj");
    setvar(sym_errobj, NIL, NIL);
    gc_protect_sym(&sym_progn, "begin");
    gc_protect_sym(&sym_lambda, "lambda");
    gc_protect_sym(&sym_quote, "quote");
    gc_protect_sym(&sym_dot, ".");
    gc_protect_sym(&sym_after_gc, "*after-gc*");
    setvar(sym_after_gc, NIL, NIL);
    if (inums_dim > 0) {
        inums = (LISP *) must_malloc(sizeof(LISP) * inums_dim);
        for (j = 0; j < inums_dim; ++j) {
            NEWCELL(ptr, tc_flonum);
            FLONM(ptr) = j;
            inums[j] = ptr;
        }
        gc_protect_n(inums, inums_dim);
    }
}

void init_subr(char *name, long type, SUBR_FUNC fcn) { setvar(cintern(name), subrcons(type, name, fcn), NIL); }

void init_subr_0(char *name, LISP (*fcn)(void)) { init_subr(name, tc_subr_0, (SUBR_FUNC) fcn); }

void init_subr_1(char *name, LISP (*fcn)(LISP)) { init_subr(name, tc_subr_1, (SUBR_FUNC) fcn); }

void init_subr_2(char *name, LISP (*fcn)(LISP, LISP)) { init_subr(name, tc_subr_2, (SUBR_FUNC) fcn); }

void init_subr_3(char *name, LISP (*fcn)(LISP, LISP, LISP)) { init_subr(name, tc_subr_3, (SUBR_FUNC) fcn); }

void init_subr_4(char *name, LISP (*fcn)(LISP, LISP, LISP, LISP)) { init_subr(name, tc_subr_4, (SUBR_FUNC) fcn); }

void init_lsubr(char *name, LISP (*fcn)(LISP)) { init_subr(name, tc_lsubr, (SUBR_FUNC) fcn); }

void init_fsubr(char *name, LISP (*fcn)(LISP, LISP)) { init_subr(name, tc_fsubr, (SUBR_FUNC) fcn); }

void init_msubr(char *name, LISP (*fcn)(LISP *, LISP *)) { init_subr(name, tc_msubr, (SUBR_FUNC) fcn); }

LISP assq(LISP x, LISP alist) {
    LISP l, tmp;
    for (l = alist; CONSP(l); l = CDR(l)) {
        tmp = CAR(l);
        if (CONSP(tmp) && EQ(CAR(tmp), x)) return (tmp);
        INTERRUPT_CHECK();
    }
    if EQ(l, NIL) return (NIL);
    return (err("improper list to assq", alist));
}

struct user_type_hooks *get_user_type_hooks(long type) {
    long n;
    if (user_types == NULL) {
        n = sizeof(struct user_type_hooks) * tc_table_dim;
        user_types = (struct user_type_hooks *) must_malloc(n);
        memset(user_types, 0, n);
    }
    if ((type >= 0) && (type < tc_table_dim))
        return (&user_types[type]);
    else
        err("type number out of range", NIL);
    return (NULL);
}

void set_gc_hooks(long type,
                  LISP (*rel)(LISP),
                  LISP (*mark)(LISP),
                  void (*scan)(LISP),
                  void (*free)(LISP),
                  long *kind) {
    struct user_type_hooks *p;
    p = get_user_type_hooks(type);
    p->gc_relocate = rel;
    p->gc_scan = scan;
    p->gc_mark = mark;
    p->gc_free = free;
    *kind = gc_kind_copying;
}

LISP gc_relocate(LISP x) {
    LISP nw;
    struct user_type_hooks *p;
    if EQ(x, NIL) return (NIL);
    if ((*x).gc_mark == 1) return (CAR(x));
    switch TYPE(x) {
        case tc_flonum:
        case tc_cons:
        case tc_symbol:
        case tc_closure:
        case tc_subr_0:
        case tc_subr_1:
        case tc_subr_2:
        case tc_subr_3:
        case tc_subr_4:
        case tc_lsubr:
        case tc_fsubr:
        case tc_msubr:
            if ((nw = heap) >= heap_end) gc_fatal_error();
            heap = nw + 1;
            memcpy(nw, x, sizeof(struct obj));
            break;
        default:
            p = get_user_type_hooks(TYPE(x));
            if (p->gc_relocate)
                nw = (*p->gc_relocate)(x);
            else {
                if ((nw = heap) >= heap_end) gc_fatal_error();
                heap = nw + 1;
                memcpy(nw, x, sizeof(struct obj));
            }
    }
    (*x).gc_mark = 1;
    CAR(x) = nw;
    return (nw);
}

LISP get_newspace(void) {
    LISP newspace;
    if (heap_org == heaps[0])
        newspace = heaps[1];
    else
        newspace = heaps[0];
    heap = newspace;
    heap_org = heap;
    heap_end = heap + heap_size;
    return (newspace);
}

void scan_newspace(LISP newspace) {
    LISP ptr;
    struct user_type_hooks *p;
    for (ptr = newspace; ptr < heap; ++ptr) {
        switch TYPE(ptr) {
            case tc_cons:
            case tc_closure:
                CAR(ptr) = gc_relocate(CAR(ptr));
                CDR(ptr) = gc_relocate(CDR(ptr));
                break;
            case tc_symbol:
                VCELL(ptr) = gc_relocate(VCELL(ptr));
                break;
            case tc_flonum:
            case tc_subr_0:
            case tc_subr_1:
            case tc_subr_2:
            case tc_subr_3:
            case tc_subr_4:
            case tc_lsubr:
            case tc_fsubr:
            case tc_msubr:
                break;
            default:
                p = get_user_type_hooks(TYPE(ptr));
                if (p->gc_scan) (*p->gc_scan)(ptr);
        }
    }
}

void free_oldspace(LISP space, LISP end) {
    LISP ptr;
    struct user_type_hooks *p;
    for (ptr = space; ptr < end; ++ptr)
        if (ptr->gc_mark == 0)
            switch TYPE(ptr) {
                case tc_cons:
                case tc_closure:
                case tc_symbol:
                case tc_flonum:
                case tc_subr_0:
                case tc_subr_1:
                case tc_subr_2:
                case tc_subr_3:
                case tc_subr_4:
                case tc_lsubr:
                case tc_fsubr:
                case tc_msubr:
                    break;
                default:
                    p = get_user_type_hooks(TYPE(ptr));
                    if (p->gc_free) (*p->gc_free)(ptr);
            }
}

void gc_stop_and_copy(void) {
    LISP newspace, oldspace, end;
    long flag;
    flag = no_interrupt(1);
    errjmp_ok = 0;
    oldspace = heap_org;
    end = heap;
    old_heap_used = end - oldspace;
    newspace = get_newspace();
    scan_registers();
    scan_newspace(newspace);
    free_oldspace(oldspace, end);
    errjmp_ok = 1;
    no_interrupt(flag);
}

LISP allocate_aheap(void) {
    long j, flag;
    LISP ptr, end, next;
    gc_kind_check();
    for (j = 0; j < nheaps; ++j)
        if (!heaps[j]) {
            flag = no_interrupt(1);
            if (gc_status_flag)
                printf("[allocating heap %ld]\n", j);
            heaps[j] = (LISP) must_malloc(sizeof(struct obj) * heap_size);
            ptr = heaps[j];
            end = heaps[j] + heap_size;
            while (1) {
                (*ptr).type = tc_free_cell;
                next = ptr + 1;
                if (next < end) {
                    CDR(ptr) = next;
                    ptr = next;
                } else {
                    CDR(ptr) = freelist;
                    break;
                }
            }
            freelist = heaps[j];
            flag = no_interrupt(flag);
            return (truth);
        }
    return (NIL);
}

void gc_for_newcell(void) {
    long flag, n;
    LISP l;
    if (errjmp_ok == 0) gc_fatal_error();
    flag = no_interrupt(1);
    errjmp_ok = 0;
    gc_mark_and_sweep();
    errjmp_ok = 1;
    no_interrupt(flag);
    for (n = 0, l = freelist; (n < 100) && NNULLP(l); ++n) l = CDR(l);
    if (n == 0) {
        if NULLP(allocate_aheap())
            gc_fatal_error();
    } else if (n == 100)
        leval(leval(sym_after_gc, NIL), NIL);
    else
        allocate_aheap();
}

void gc_mark_and_sweep(void) {
    LISP stack_end;
    gc_ms_stats_start();
    setjmp(save_regs_gc_mark);

    mark_locations((LISP *) save_regs_gc_mark,
                   (LISP *) (((char *) save_regs_gc_mark) + sizeof(save_regs_gc_mark)));
    mark_protected_registers();
    mark_locations((LISP *) stack_start_ptr,
                   (LISP *) &stack_end);
#ifdef THINK_C
                                                                                                                            mark_locations((LISP *) ((char *) stack_start_ptr + 2),
		(LISP *) ((char *) &stack_end + 2));
#endif
    gc_sweep();
    gc_ms_stats_end();
}

void gc_ms_stats_start(void) {
    gc_rt = myruntime();
    gc_cells_collected = 0;
    if (gc_status_flag)
        printf("[starting GC]\n");
}

void gc_ms_stats_end(void) {
    gc_rt = myruntime() - gc_rt;
    gc_time_taken = gc_time_taken + gc_rt;
    if (gc_status_flag)
        printf("[GC took %g cpu seconds, %ld cells collected]\n",
               gc_rt,
               gc_cells_collected);
}

void translate_type_detail(char *res, LISP ptr) {
    short type = ptr->type;
    switch (type) {
        case tc_cons:
            strcpy(res, TYPE_STR_CONS);
            return;
        case tc_flonum:
            strcpy(res, TYPE_STR_FLONUM);
            return;
        case tc_symbol:
            strcpy(res, TYPE_STR_SYMBOL);
            strcat(res, "(");
            strcat(res, ptr->storage_as.symbol.pname);
            strcat(res, ")");
            return;
        case tc_closure:
            strcpy(res, TYPE_STR_CLOSURE);
            return;
        case tc_string:
            strcpy(res, TYPE_STR_STRING);
            strcat(res, "(");
            strcat(res, ptr->storage_as.string.data);
            strcat(res, ")");
            return;
        case tc_c_file:
            strcpy(res, TYPE_STR_FILE);
            strcat(res, "(");
            strcat(res, ptr->storage_as.c_file.name);
            strcat(res, ")");
            return;
        case tc_struct_instance:
        case tc_struct_instance_with_rec:
            strcpy(res, get_struct_def_obj(ptr)->storage_as.struct_def.class_name_sym->storage_as.symbol.pname);
            return;
        default:
            strcpy(res, TYPE_STR_NO_SUCH_TYPE);
            char tp[5] = "";
            sprintf(tp, "%d", type);
            strcat(res, tp);
    }
}

char *gen_type_str(ObjLinkElement *obj_link_ele) {
    short type = obj_link_ele->lisp_obj_type;
    if (tc_cons == type) {
        return TYPE_STR_CONS;
    } else if (tc_flonum == type) {
        return TYPE_STR_FLONUM;
    }  else if (tc_symbol == type) {
        return TYPE_STR_SYMBOL;
    } else if (tc_closure == type) {
        return TYPE_STR_CLOSURE;
    } else if (tc_string == type) {
        return TYPE_STR_STRING;
    } else if (tc_c_file == type) {
        return TYPE_STR_FILE;
    } else if (tc_struct_instance == type || tc_struct_instance_with_rec == type) {
        return obj_link_ele->struct_def_obj->storage_as.struct_def.class_name_sym->storage_as.symbol.pname;
    } else {
        err("see gen_type_str", NIL);
    }
}

short has_same_elementsp(long *a, long *b, long len) {
    for (long i = 0; i < len; ++i) {
        long tmp_a = a[i];
        for (long j = 0; j < len; ++j) {
            if (tmp_a != b[i]) {
                return 0;
            }
        }
    }
    return 1;
}

LISP get_first_struct_def_obj(LISP struct_def) {
    if (NTYPEP(struct_def, tc_struct_def)) {
        err("Wrong type! (see get_first_struct_def_obj)", NIL);
    }

    if (NULLP(struct_def->storage_as.struct_def.pre)) {
        return struct_def;
    }

    while (NNULLP(struct_def->storage_as.struct_def.pre)) {
        struct_def = struct_def->storage_as.struct_def.pre;
    }

    return struct_def;
}

LISP get_last_struct_def_obj(LISP struct_def) {
    if (NTYPEP(struct_def, tc_struct_def)) {
        err("Wrong type! (see get_last_struct_def_obj)", NIL);
    }

    if (NULLP(struct_def->storage_as.struct_def.next)) {
        return struct_def;
    }

    while (NNULLP(struct_def->storage_as.struct_def.next)) {
        struct_def = struct_def->storage_as.struct_def.next;
    }

    return struct_def;
}

LISP get_active_struct_def_obj(LISP struct_def) {
    if (NTYPEP(struct_def, tc_struct_def)) {
        err("wrong struct_def obj!(see get_active_struct_def_obj)", struct_def);
    }

    if (struct_def->storage_as.struct_def.active_mark) {
        return struct_def;
    }

    struct_def = get_first_struct_def_obj(struct_def);

    if (struct_def->storage_as.struct_def.active_mark) {
        return struct_def;
    }

    while (NNULLP(struct_def->storage_as.struct_def.next)) {
        struct_def = struct_def->storage_as.struct_def.next;
        if (struct_def->storage_as.struct_def.active_mark) {
            return struct_def;
        }
    }

    err("Why? (see get_active_struct_def_obj)", NIL);
}

void try_update_all_struct_defs() {
    if (NULL == object_links) {
        return;
    }

    long link_num = utarray_len(object_links);
    if (link_num <= 0) {
        return;
    }

    // 時間的データ構造切り替えの情報
    typedef struct {
        const char *type_str; /* key */
        LISP struct_def_obj;
        long *record_slot_idx;
        long len;
        UT_hash_handle hh; /* makes this structure hashtable */
    } TmpInfo;

    TmpInfo *tmp_infos = NULL;

    // 增量
    for (long i = 0; i < link_num; ++i) {
        ObjectLink *tmp_obj_link = utarray_eltptr(object_links, i);
        long active_ele_idx = tmp_obj_link->active_ele_idx;
        if (active_ele_idx < 0) {
            continue;
        }

        UT_array *tmp_link_data = tmp_obj_link->link_data;
        long tmp_link_data_len = utarray_len(tmp_link_data);
        if (tmp_link_data_len <= 0) {
            continue;
        }

        if (active_ele_idx >= tmp_link_data_len - 1) {
            continue;
        }

        ObjLinkElement *active_link_ele = utarray_eltptr(tmp_link_data, active_ele_idx);
        const char *type_key = gen_type_str(active_link_ele);

        TmpInfo *active_tmp_info = NULL;
        HASH_FIND_STR(tmp_infos, type_key, active_tmp_info);

        if (NULL == active_tmp_info) {
            active_tmp_info = (TmpInfo *) malloc(sizeof(TmpInfo));
            active_tmp_info->type_str = type_key;
            active_tmp_info->struct_def_obj = active_link_ele->struct_def_obj;
            active_tmp_info->len = 0;
            HASH_ADD_KEYPTR(hh, tmp_infos, active_tmp_info->type_str, strlen(active_tmp_info->type_str), active_tmp_info);
        }

        long len = active_tmp_info->len;
        if (len <= 0) {
            active_tmp_info->record_slot_idx = (long *)malloc(sizeof(long));
            active_tmp_info->record_slot_idx[0] = active_link_ele->assign_slot_type;
            active_tmp_info->len += 1L;
        } else {
            short need_updatep = 1;
            for (long j = 0; j < len; ++j) {
                if (active_tmp_info->record_slot_idx[j] == active_link_ele->assign_slot_type) {
                    need_updatep = 0;
                    break;
                }
            }

            if (need_updatep) {
                long *tmp_long_arr = (long *)malloc(sizeof(long) * (len + 1L));
                for (long j = 0; j < len; ++j) {
                    tmp_long_arr[j] = active_tmp_info->record_slot_idx[j];
                }
                tmp_long_arr[len] = active_link_ele->assign_slot_type;
                free(active_tmp_info->record_slot_idx);
                active_tmp_info->record_slot_idx = tmp_long_arr;
                active_tmp_info->len += 1L;
            }
        }
    }

    for (TmpInfo *tmp = tmp_infos; NULL != tmp; tmp = tmp->hh.next) {
        // TODO 当前只实现了lisp struct的类型变换
        if (NULL != tmp->struct_def_obj || NNULLP(tmp->struct_def_obj)) {
            LISP struct_def_obj = tmp->struct_def_obj;

            // 检查是否已经有完全一样的了
            LISP selected_one = NIL;
            for (LISP tmp_selected_one = get_first_struct_def_obj(struct_def_obj); NNULLP(tmp_selected_one->storage_as.struct_def.next); tmp_selected_one = tmp_selected_one->storage_as.struct_def.next) {
                if (tmp->len != tmp_selected_one->storage_as.struct_def.length) {
                    continue;
                }
                if (has_same_elementsp(tmp_selected_one->storage_as.struct_def.assign_field_indexes, tmp->record_slot_idx, tmp->len)) {
                    selected_one = tmp_selected_one;
                    break;
                }
            }

            LISP cur_active_obj = get_active_struct_def_obj(struct_def_obj);
            cur_active_obj->storage_as.struct_def.active_mark = 0;

            if (NNULLP(selected_one)) {
                selected_one->storage_as.struct_def.active_mark = 1;
            } else {
                LISP new_struct_obj = newcell(tc_struct_def);
                new_struct_obj->storage_as.struct_def.class_name_sym = struct_def_obj->storage_as.struct_def.class_name_sym;
                new_struct_obj->storage_as.struct_def.dim = struct_def_obj->storage_as.struct_def.dim;
                new_struct_obj->storage_as.struct_def.field_name_strs = struct_def_obj->storage_as.struct_def.field_name_strs;
                new_struct_obj->storage_as.struct_def.length = tmp->len;
                new_struct_obj->storage_as.struct_def.assign_field_indexes = tmp->record_slot_idx;
                new_struct_obj->storage_as.struct_def.active_mark = 1;
                new_struct_obj->gc_mark = 1;

                struct_def_obj = get_last_struct_def_obj(struct_def_obj);

                new_struct_obj->storage_as.struct_def.pre = struct_def_obj;
                new_struct_obj->storage_as.struct_def.next = NIL;
                struct_def_obj->storage_as.struct_def.next = new_struct_obj;
            }
        }
    }

    free(tmp_infos);
}

void process_assert_dead_stage_one(LISP ptr, long last_index_of_gc_traced_objs) {
    if (NULL == gc_traced_objs) {
        printf("\033[31mRuntime Exception: Why the traced_obj is NULL? (see \"process_assert_dead_stage_one()\")\n\033[0m");
        exit(-1);
        return;
    }

    AssertDeadCallInfo *tmp_assert_dead_info;
    HASH_FIND(hh, assert_dead_call_info_table, &(ptr->asserted_dead_at), sizeof(long), tmp_assert_dead_info);
    if (NULL == tmp_assert_dead_info) {
        printf("\033[31mRuntime Exception: Why the tmpAssertDeadInfo is NULL? (see \"process_assert_dead_stage_one()\")\n\033[0m");
        exit(-1);
        return;
    }

    ObjectLink *object_link = find_the_obj_link(last_index_of_gc_traced_objs);
    if (NULL == object_link) {
        object_link = (ObjectLink *) malloc(sizeof(ObjectLink));
        object_link->link_data = NULL;
        object_link->active_ele_idx = 0;

        for (long i = 0; i <= last_index_of_gc_traced_objs; ++i) {
            LISP current_traced_obj = gc_traced_objs[i];

            // 参照パスを記録しておく
            ObjLinkElement *new_obj_link_element = (ObjLinkElement *) malloc(sizeof(ObjLinkElement));

            new_obj_link_element->lisp_obj_type = current_traced_obj->type;
            new_obj_link_element->assign_slot_type = gc_traced_obj_fields[i];
            new_obj_link_element->struct_def_obj = get_struct_def_obj(current_traced_obj);
            new_obj_link_element->checked = 0;

            if (NULL == object_link->link_data) {
                UT_icd obj_link_elements_icd = {sizeof(ObjLinkElement), NULL, NULL, NULL};
                utarray_new(object_link->link_data, &obj_link_elements_icd);
            }

            utarray_push_back(object_link->link_data, new_obj_link_element);
        }

        if (NULL == object_links) {
            UT_icd obj_links_icd = {sizeof(ObjectLink), NULL, NULL, NULL};
            utarray_new(object_links, &obj_links_icd);
        }
        utarray_push_back(object_links, object_link);
    }

    ptr->assert_dead = HAD_BEEN_ASSERTED;
    tmp_assert_dead_info->stage = ASSERT_DEAD_STAGE_DETAILED;

    try_update_all_struct_defs();

    printf("\033[31mAssert-Dead Stage One Done.\n\n\033[0m");
}

void translate_to_line_num_str(char* dst, long line_num) {
    if (line_num <= 0) {
        return;
    }
    sprintf(dst, "@ln%ld", line_num);
}

void try_update_obj_link_active_info(long last_index_of_gc_traced_objs) {
    ObjectLink *object_link = find_the_obj_link(last_index_of_gc_traced_objs);
    if (NULL == object_link) {
        return;
    }

    long link_data_len = utarray_len(object_link->link_data);
    if (object_link->active_ele_idx >= link_data_len - 1) {
        return;
    }

    ObjLinkElement *cur_ele = utarray_eltptr(object_link->link_data, object_link->active_ele_idx);
    if (!cur_ele->checked) {
        return;
    }

    object_link->active_ele_idx += 1L;
}

void process_assert_dead_stage_two(LISP ptr, long last_index_of_gc_traced_objs) {
    if (NULL == gc_traced_objs) {
        printf("\033[31mRuntime Exception: Why the traced_obj is NULL? (see \"process_assert_dead_stage_two()\")\n\033[0m");
        return;
    }

    char suspect_type_str[40] = "";
    translate_type_detail(suspect_type_str, ptr);

    long path_info_length = 1L;
    path_info_length += last_index_of_gc_traced_objs < 0 ? 0 : last_index_of_gc_traced_objs;

    /**
     * e.g.
     * TYPE0; ->
     * TYPE1; @ln10->
     * ...
     */
    char path_info[(path_info_length + 1) * (40 + 10 + 10)];
    memset(path_info, 0, sizeof(path_info));

    for (long i = 0; i <= last_index_of_gc_traced_objs; ++i) {
        LISP current_traced_obj = gc_traced_objs[i];

        char tmp_type_str[40] = "";
        translate_type_detail(tmp_type_str, current_traced_obj);
        strcat(path_info, tmp_type_str);
        strcat(path_info, "; ");

        if (i != last_index_of_gc_traced_objs) {
            LISP next_traced_obj = gc_traced_objs[i + 1];
            if (recording_assign_site_on && current_traced_obj->is_assign_info_recorded) {
                char line_num_str[15] = "";

                if (tc_cons == current_traced_obj->type) {
                    if (current_traced_obj->storage_as.cons.car == next_traced_obj) {
                        translate_to_line_num_str(line_num_str, current_traced_obj->storage_as.cons.car_assign_site);
                        strcat(path_info, line_num_str);
                    }
                    if (current_traced_obj->storage_as.cons.cdr == next_traced_obj) {
                        translate_to_line_num_str(line_num_str, current_traced_obj->storage_as.cons.cdr_assign_site);
                        strcat(path_info, line_num_str);
                    }
                } else if (tc_symbol == current_traced_obj->type) {
                    if (current_traced_obj->storage_as.symbol.vcell == next_traced_obj) {
                        translate_to_line_num_str(line_num_str,
                                                  current_traced_obj->storage_as.symbol.vcell_assign_site);
                        strcat(path_info, line_num_str);
                    }
                } else if (tc_closure == current_traced_obj->type) {
                    if (current_traced_obj->storage_as.closure.env == next_traced_obj) {
                        translate_to_line_num_str(line_num_str,
                                                  current_traced_obj->storage_as.closure.env_assign_site);
                        strcat(path_info, line_num_str);
                    }
                    if (current_traced_obj->storage_as.closure.code == next_traced_obj) {
                        translate_to_line_num_str(line_num_str,
                                                  current_traced_obj->storage_as.closure.code_assign_site);
                        strcat(path_info, line_num_str);
                    }
                } else if (tc_struct_instance_with_rec == current_traced_obj->type) {
                    LISP struct_def_obj = current_traced_obj->storage_as.struct_instance_with_rec.struct_def_obj;
                    long *assign_field_indexes = struct_def_obj->storage_as.struct_def.assign_field_indexes;
                    long length = struct_def_obj->storage_as.struct_def.length;
                    for (long j = 0; j < length; ++j) {
                        if (current_traced_obj->storage_as.struct_instance_with_rec.data[assign_field_indexes[j]] == next_traced_obj) {
                            translate_to_line_num_str(line_num_str,
                                                      current_traced_obj->storage_as.struct_instance_with_rec.assign_sites[j]);
                            strcat(path_info, line_num_str);
                        }
                    }
                }
            }

            strcat(path_info, "->\n");
        }
    }

    try_update_obj_link_active_info(last_index_of_gc_traced_objs);
    try_update_all_struct_defs();

    printf("\033[31mWarning: an object that was asserted dead is reachable.\n"
           "Type: %s;\nPath to object: %s\n\n\033[0m",
           suspect_type_str, path_info);
}

short can_process_assert_dead_stage_two(long last_index_of_gc_traced_objs) {
    ObjectLink *object_link = find_the_obj_link(last_index_of_gc_traced_objs);
    if (NULL == object_link) {
        return 0;
    }
    return 1;
}

// assert-dead with new_struct_instance features
LISP assert_dead(LISP ptr, LISP ln) {
    if (NULLP(ptr)) {
        return err("Null Pointer!", ptr);
    }

    if (ptr->assert_dead || ptr->assert_dead == HAD_BEEN_ASSERTED) {
        return (NIL);
    }

    long line_num = (long) ln->storage_as.flonum.data;

    AssertDeadCallInfo *assert_dead_info;
    HASH_FIND(hh, assert_dead_call_info_table, &line_num, sizeof(long), assert_dead_info);
    if (NULL != assert_dead_info) {
        return (NIL);
    }

    AssertDeadCallInfo *new_assert_dead_info = (AssertDeadCallInfo *) malloc(sizeof(AssertDeadCallInfo));
    new_assert_dead_info->line_num = line_num;
    new_assert_dead_info->stage = ASSERT_DEAD_STAGE_ROUGH;

    HASH_ADD(hh, assert_dead_call_info_table, line_num, sizeof(long), new_assert_dead_info);

    ptr->assert_dead = HAS_BEEN_ASSERTED;
    ptr->asserted_dead_at = line_num;
    return (NIL);
}

short can_process_assert_dead_stage_one(LISP ptr) {
    if (HAS_BEEN_ASSERTED != ptr->assert_dead) {
        return 0;
    }

    long line_num = ptr->asserted_dead_at;
    AssertDeadCallInfo *tmp_assert_dead_info;
    HASH_FIND(hh, assert_dead_call_info_table, &line_num, sizeof(long), tmp_assert_dead_info);
    if (NULL == tmp_assert_dead_info) {
        return 0;
    }

    if (ASSERT_DEAD_STAGE_ROUGH != tmp_assert_dead_info->stage) {
        return 0;
    }

    return 1;
}

void gc_mark(LISP ptr, long last_index_of_gc_traced_objs, long previous_obj_selected_field) {
    struct user_type_hooks *p;

    gc_mark_loop:
    if NULLP(ptr) {
        return;
    }

    if ((*ptr).gc_mark) {
        return;
    }

    (*ptr).gc_mark = 1;

    if (NULL == gc_traced_objs || NULL == gc_traced_obj_fields) {
        gc_traced_objs = (LISP *) malloc(sizeof(LISP *) * heap_size);
        gc_traced_obj_fields = (long *) malloc(sizeof(long) * heap_size);
    }

    if (!(tc_cons == ptr->type && !ptr->storage_as.cons.is_external_cons)) {
        ++last_index_of_gc_traced_objs;
        gc_traced_objs[last_index_of_gc_traced_objs] = ptr;
        if (previous_obj_selected_field >= 0) {
            long last_index_of_gc_traced_obj_fields = last_index_of_gc_traced_objs - 1L;
            gc_traced_obj_fields[last_index_of_gc_traced_obj_fields] = previous_obj_selected_field;
        }
    }

    // assert-dead check
    if (can_process_assert_dead_stage_one(ptr)) {
        process_assert_dead_stage_one(ptr, last_index_of_gc_traced_objs);
    } else if (can_process_assert_dead_stage_two(last_index_of_gc_traced_objs)) {
        process_assert_dead_stage_two(ptr, last_index_of_gc_traced_objs);
    }

    switch ((*ptr).type) {
        case tc_flonum:
            break;
        case tc_cons:
            gc_mark(CAR(ptr), last_index_of_gc_traced_objs, CONS_CAR_TYPE_ID);
            previous_obj_selected_field = CONS_CDR_TYPE_ID;
            ptr = CDR(ptr);
            goto gc_mark_loop;
        case tc_symbol:
            previous_obj_selected_field = SYMBOL_VCELL_TYPE_ID;
            ptr = VCELL(ptr);
            goto gc_mark_loop;
        case tc_closure:
            gc_mark((*ptr).storage_as.closure.code, last_index_of_gc_traced_objs, CLOSURE_CODE_TYPE_ID);
            previous_obj_selected_field = CLOSURE_ENV_TYPE_ID;
            ptr = (*ptr).storage_as.closure.env;
            goto gc_mark_loop;
        case tc_struct_instance:
            for (long i = 0; i < (*ptr).storage_as.struct_instance.struct_def_obj->storage_as.struct_def.dim; ++i) {
                gc_mark((*ptr).storage_as.struct_instance.data[i], last_index_of_gc_traced_objs, i);
            }
            previous_obj_selected_field = (*ptr).storage_as.struct_instance.struct_def_obj->storage_as.struct_def.dim + 1L;
            ptr = (*ptr).storage_as.struct_instance.struct_def_obj;
            break;
        case tc_struct_instance_with_rec:
            for (long i = 0; i < (*ptr).storage_as.struct_instance_with_rec.struct_def_obj->storage_as.struct_def.dim; ++i) {
                gc_mark((*ptr).storage_as.struct_instance_with_rec.data[i], last_index_of_gc_traced_objs, i);
            }
            previous_obj_selected_field = (*ptr).storage_as.struct_instance_with_rec.struct_def_obj->storage_as.struct_def.dim + 1L;
            break;
        case tc_struct_def:
            for (long i = 0; i < (*ptr).storage_as.struct_def.dim; ++i) {
                gc_mark((*ptr).storage_as.struct_def.field_name_strs[i], last_index_of_gc_traced_objs, i);
            }
            previous_obj_selected_field = (*ptr).storage_as.struct_def.dim + 1L;
            ptr = (*ptr).storage_as.struct_def.next;
            goto gc_mark_loop;
        case tc_subr_0:
        case tc_subr_1:
        case tc_subr_2:
        case tc_subr_3:
        case tc_subr_4:
        case tc_lsubr:
        case tc_fsubr:
        case tc_msubr:
            break;
        default:
            p = get_user_type_hooks(TYPE(ptr));
            if (p->gc_mark)
                ptr = (*p->gc_mark)(ptr);
    }
}

void mark_protected_registers(void) {
    struct gc_protected *reg;
    LISP *location;
    long j, n;
    for (reg = protected_registers; reg; reg = (*reg).next) {
        location = (*reg).location;
        n = (*reg).length;
        for (j = 0; j < n; ++j) {
            gc_mark(location[j], -1L, -1);
        }
    }
}

void mark_locations(LISP *start, LISP *end) {
    LISP *tmp;
    long n;
    if (start > end) {
        tmp = start;
        start = end;
        end = tmp;
    }
    n = end - start;
    mark_locations_array(start, n);
}

long looks_pointerp(LISP p) {
    long j;
    LISP h;
    for (j = 0; j < nheaps; ++j)
        if ((h = heaps[j]) &&
            (p >= h) &&
            (p < (h + heap_size)) &&
            (((((char *) p) - ((char *) h)) % sizeof(struct obj)) == 0) &&
            NTYPEP(p, tc_free_cell))
            return (1);
    return (0);
}

void mark_locations_array(LISP *x, long n) {
    int j;
    LISP p;
    for (j = 0; j < n; ++j) {
        p = x[j];
        if (looks_pointerp(p)) {
            gc_mark(p, -1L, -1);
        }
    }
}

void gc_sweep(void) {
    LISP ptr, end, nfreelist, org;
    long n, k;
    struct user_type_hooks *p;
    end = heap_end;
    n = 0;
    nfreelist = NIL;
    for (k = 0; k < nheaps; ++k)
        if (heaps[k]) {
            org = heaps[k];
            end = org + heap_size;
            for (ptr = org; ptr < end; ++ptr)
                if (((*ptr).gc_mark == 0)) {
                    switch ((*ptr).type) {
                        case tc_free_cell:
                        case tc_cons:
                        case tc_closure:
                        case tc_symbol:
                        case tc_flonum:
                        case tc_subr_0:
                        case tc_subr_1:
                        case tc_subr_2:
                        case tc_subr_3:
                        case tc_subr_4:
                        case tc_lsubr:
                        case tc_fsubr:
                        case tc_msubr:
                        case tc_struct_instance:
                        case tc_struct_instance_with_rec:
                        case tc_struct_def:
                            break;
                        default:
                            p = get_user_type_hooks(TYPE(ptr));
                            if (p->gc_free)
                                (*p->gc_free)(ptr);
                    }
                    ++n;

                    (*ptr).type = tc_free_cell;
                    CDR(ptr) = nfreelist;
                    nfreelist = ptr;
                } else {
                    (*ptr).gc_mark = 0;
                }
        }
    gc_cells_collected = n;
    freelist = nfreelist;
}

void gc_kind_check(void) {
    if (gc_kind_copying == 1)
        err("cannot perform operation with stop-and-copy GC mode. Use -g0\n",
            NIL);
}

LISP user_gc(LISP args) {
    long old_status_flag, flag;
    gc_kind_check();
    flag = no_interrupt(1);
    errjmp_ok = 0;
    old_status_flag = gc_status_flag;
    if NNULLP(args)
        if NULLP(car(args)) gc_status_flag = 0; else gc_status_flag = 1;
    gc_mark_and_sweep();
    gc_status_flag = old_status_flag;
    errjmp_ok = 1;
    no_interrupt(flag);
    return (NIL);
}

long nactive_heaps(void) {
    long m;
    for (m = 0; (m < nheaps) && heaps[m]; ++m);
    return (m);
}

long freelist_length(void) {
    long n;
    LISP l;
    for (n = 0, l = freelist; NNULLP(l); ++n) l = CDR(l);
    return (n);
}

LISP gc_status(LISP args) {
    int n, m;
    if NNULLP(args)
        if NULLP(car(args)) gc_status_flag = 0; else gc_status_flag = 1;
    if (gc_kind_copying == 1) {
        if (gc_status_flag)
            put_st("garbage collection is on\n");
        else
            put_st("garbage collection is off\n");
        sprintf(tkbuffer, "%ld allocated %ld free\n",
                heap - heap_org, heap_end - heap);
        put_st(tkbuffer);
    } else {
        if (gc_status_flag)
            put_st("garbage collection verbose\n");
        else
            put_st("garbage collection silent\n");
        {
            m = nactive_heaps();
            n = freelist_length();
            sprintf(tkbuffer, "%ld/%ld heaps, %ld allocated %ld free\n",
                    m, nheaps, m * heap_size - n, n);
            put_st(tkbuffer);
        }
    }
    return (NIL);
}

LISP gc_info(LISP arg) {
    switch (get_c_long(arg)) {
        case 0:
            return ((gc_kind_copying == 1) ? truth : NIL);
        case 1:
            return (flocons(nactive_heaps()));
        case 2:
            return (flocons(nheaps));
        case 3:
            return (flocons(heap_size));
        case 4:
            return (flocons((gc_kind_copying == 1)
                            ? (long) (heap_end - heap)
                            : freelist_length()));
        default:
            return (NIL);
    }
}

LISP leval_args(LISP l, LISP env) {
    LISP result, v1, v2, tmp;
    if NULLP(l) {
        return (NIL);
    }
    if NCONSP(l) {
        err("bad syntax argument list", l);
    }
    result = cons(leval(CAR(l), env), NIL);
    for (v1 = result, v2 = CDR(l); CONSP(v2); v1 = tmp, v2 = CDR(v2)) {
        tmp = cons(leval(CAR(v2), env), NIL);
        CDR(v1) = tmp;
    }
    if NNULLP(v2) {
        err("bad syntax argument list", l);
    }
    return (result);
}

LISP extend_env(LISP actuals, LISP formals, LISP env) {
    if SYMBOLP(formals) {
        return (cons(cons(cons(formals, NIL), cons(actuals, NIL)), env));
    }
    return (cons(cons(formals, actuals), env));
}

#define ENVLOOKUP_TRICK 1

LISP envlookup(LISP var, LISP env) {
    LISP frame, al, fl, tmp;
    for (frame = env; CONSP(frame); frame = CDR(frame)) {
        tmp = CAR(frame);
        if NCONSP(tmp) {
            err("damaged frame", tmp);
        }

        for (fl = CAR(tmp), al = CDR(tmp); CONSP(fl); fl = CDR(fl), al = CDR(al)) {
            if NCONSP(al) {
                err("too few arguments", tmp);
            }
            if EQ(CAR(fl), var) {
                return (al);
            }
        }
        /* suggested by a user. It works for reference (although conses)
       but doesn't allow for set! to work properly... */
#if (ENVLOOKUP_TRICK)
        if (SYMBOLP(fl) && EQ(fl, var)) {
            return (cons(al, NIL));
        }
#endif
    }
    if NNULLP(frame) {
        err("damaged env", env);
    }
    return (NIL);
}

void set_eval_hooks(long type, LISP (*fcn)(LISP, LISP *, LISP *)) {
    struct user_type_hooks *p;
    p = get_user_type_hooks(type);
    p->leval = fcn;
}

LISP leval(LISP x, LISP env) {
    LISP tmp, arg1;
    struct user_type_hooks *p;
    STACK_CHECK(&x);
    loop:
    INTERRUPT_CHECK();
    switch TYPE(x) {
        case tc_symbol:
            tmp = envlookup(x, env);
            if NNULLP(tmp) return (CAR(tmp));
            tmp = VCELL(x);
            if EQ(tmp, unbound_marker) err("unbound variable", x);
            return (tmp);
        case tc_cons:
            tmp = CAR(x);
            switch TYPE(tmp) {
                case tc_symbol:
                    tmp = envlookup(tmp, env);
                    if NNULLP(tmp) {
                        tmp = CAR(tmp);
                        break;
                    }
                    tmp = VCELL(CAR(x));
                    if EQ(tmp, unbound_marker) err("unbound variable", CAR(x));
                    break;
                case tc_cons:
                    tmp = leval(tmp, env);
                    break;
            }
            switch TYPE(tmp) {
                case tc_subr_0:
                    return (SUBR0(tmp)());
                case tc_subr_1:
                    return (SUBR1(tmp)(leval(car(CDR(x)), env)));
                case tc_subr_2:
                    x = CDR(x);
                    arg1 = leval(car(x), env);
                    x = NULLP(x) ? NIL : CDR(x);
                    return (SUBR2(tmp)(arg1,
                                       leval(car(x), env)));
                case tc_subr_3:
                    x = CDR(x);
                    arg1 = leval(car(x), env);
                    x = NULLP(x) ? NIL : CDR(x);
                    return (SUBR3(tmp)(arg1,
                                       leval(car(x), env),
                                       leval(car(cdr(x)), env)));
                case tc_subr_4:
                    x = CDR(x);
                    arg1 = leval(car(x), env);
                    x = NULLP(x) ? NIL : CDR(x);
                    return (SUBR4(tmp)(arg1,
                                       leval(car(x), env),
                                       leval(car(cdr(x)), env),
                                       leval(car(cdr(cdr(x))), env)));
                case tc_lsubr:
                    return (SUBR1(tmp)(leval_args(CDR(x), env)));
                case tc_fsubr:
                    return (SUBR2(tmp)(CDR(x), env));
                case tc_msubr:
                    if NULLP(SUBRM(tmp)(&x, &env)) return (x);
                    goto loop;
                case tc_closure:
                    env = extend_env(leval_args(CDR(x), env),
                                     car((*tmp).storage_as.closure.code),
                                     (*tmp).storage_as.closure.env);
                    x = cdr((*tmp).storage_as.closure.code);
                    goto loop;
                case tc_symbol:
                    x = cons(tmp, cons(cons(sym_quote, cons(x, NIL)), NIL));
                    x = leval(x, NIL);
                    goto loop;
                default:
                    p = get_user_type_hooks(TYPE(tmp));
                    if (p->leval) { if NULLP((*p->leval)(tmp, &x, &env)) return (x); else goto loop; }
                    err("bad function", tmp);
            }
        default:
            return (x);
    }
}

LISP setvar(LISP var, LISP val, LISP env) {
    LISP tmp;
    if NSYMBOLP(var) err("wta(non-symbol) to setvar", var);
    tmp = envlookup(var, env);
    if NULLP(tmp) return (VCELL(var) = val);
    return (CAR(tmp) = val);
}

LISP leval_setq(LISP args, LISP env) {
    LISP var = car(args);
    LISP val = leval(car(cdr(args)), env);
    if (recording_assign_site_on) {
        // for recording assign site
        long assign_site = (long) car(cdr(cdr(args)))->storage_as.flonum.data;
        try_record_assign_site(var, val, assign_site, 0);
    }
    return (setvar(var, val, env));
}

LISP syntax_define(LISP args) {
    if SYMBOLP(car(args)) {
        return (args);
    }

    // (define (sum3 a b c) (+ a b c))のような関数定義式の処理
    return (syntax_define(
            cons(car(car(args)),
                 cons(cons(sym_lambda,
                           cons(cdr(car(args)),
                                cdr(args))),
                      NIL))));
}

// 将最后一个代表line_num的参数排除在外
LISP process_args_with_ln(LISP args) {
    return cons(car(args), cons(car(cdr(args)), NIL));
}

LISP leval_define(LISP args, LISP env) {
    LISP tmp, var, val;
    tmp = syntax_define(process_args_with_ln(args));
    var = car(tmp);

    if NSYMBOLP(var) {
        err("wta(non-symbol) to define", var);
    }

    val = leval(car(cdr(tmp)), env);
    tmp = envlookup(var, env);

    // for recording assign_site
    long assign_site = (long) car(cdr(cdr(args)))->storage_as.flonum.data;

    if NNULLP(tmp) {
        if (recording_assign_site_on) {
            try_record_assign_site(tmp, val, assign_site, 0);
        }
        return (CAR(tmp) = val);
    }

    if NULLP(env) {
        if (recording_assign_site_on) {
            try_record_assign_site(var, val, assign_site, 0);
        }
        return (VCELL(var) = val);
    }

    tmp = car(env);
    setcar(tmp, cons(var, car(tmp)));
    setcdr(tmp, cons(val, cdr(tmp)));

    if (recording_assign_site_on) {
        // cdr(tmp) 为最新frame的实参列表
        try_record_assign_site(cdr(tmp), val, assign_site, 0);
    }
    return (val);
}

LISP leval_if(LISP *pform, LISP *penv) {
    LISP args, env;
    args = cdr(*pform);
    env = *penv;
    if NNULLP(leval(car(args), env))
        *pform = car(cdr(args));
    else *pform = car(cdr(cdr(args)));
    return (truth);
}

LISP leval_lambda(LISP args, LISP env) {
    // 将最后一个元素取出，因为最后一个元素是assign site
    args = reverse(args);
    LISP assign_site_obj = car(args);
    args = reverse(cdr(args));

    LISP body;
    if NULLP(cdr(cdr(args))) {
        body = car(cdr(args));
    } else {
        body = cons(sym_progn, cdr(args));
    }

    LISP code = cons(arglchk(car(args)), body);
    LISP result = closure(env, code);

    if (recording_assign_site_on) {
        long assign_site = (long) assign_site_obj->storage_as.flonum.data;
        try_record_assign_site(result, env, assign_site, 1);
        try_record_assign_site(result, code, assign_site, 0);
    }
    return (result);
}

LISP leval_progn(LISP *pform, LISP *penv) {
    LISP env, l, next;
    env = *penv;
    l = cdr(*pform);
    next = cdr(l);
    while (NNULLP(next)) {
        leval(car(l), env);
        l = next;
        next = cdr(next);
    }
    *pform = car(l);
    return (truth);
}

LISP leval_or(LISP *pform, LISP *penv) {
    LISP env, l, next, val;
    env = *penv;
    l = cdr(*pform);
    next = cdr(l);
    while (NNULLP(next)) {
        val = leval(car(l), env);
        if NNULLP(val) {
            *pform = val;
            return (NIL);
        }
        l = next;
        next = cdr(next);
    }
    *pform = car(l);
    return (truth);
}

LISP leval_and(LISP *pform, LISP *penv) {
    LISP env, l, next;
    env = *penv;
    l = cdr(*pform);
    if NULLP(l) {
        *pform = truth;
        return (NIL);
    }
    next = cdr(l);
    while (NNULLP(next)) {
        if NULLP(leval(car(l), env)) {
            *pform = NIL;
            return (NIL);
        }
        l = next;
        next = cdr(next);
    }
    *pform = car(l);
    return (truth);
}

LISP leval_catch_1(LISP forms, LISP env) {
    LISP l, val = NIL;
    for (l = forms; NNULLP(l); l = cdr(l))
        val = leval(car(l), env);
    catch_framep = catch_framep->next;
    return (val);
}

LISP leval_catch(LISP args, LISP env) {
    struct catch_frame frame;
    int k;
    frame.tag = leval(car(args), env);
    frame.next = catch_framep;
    k = setjmp(frame.cframe);
    catch_framep = &frame;
    if (k == 2) {
        catch_framep = frame.next;
        return (frame.retval);
    }
    return (leval_catch_1(cdr(args), env));
}

LISP lthrow(LISP tag, LISP value) {
    struct catch_frame *l;
    for (l = catch_framep; l; l = (*l).next)
        if EQ((*l).tag, tag) {
            (*l).retval = value;
            longjmp((*l).cframe, 2);
        }
    err("no *catch found with this tag", tag);
    return (NIL);
}

LISP leval_let(LISP *pform, LISP *penv) {
    LISP env, l;
    l = cdr(*pform);
    env = *penv;
    *penv = extend_env(leval_args(car(cdr(l)), env), car(l), env);
    *pform = car(cdr(cdr(l)));
    return (truth);
}

LISP reverse(LISP l) {
    LISP n, p;
    n = NIL;
    for (p = l; NNULLP(p); p = cdr(p)) {
        n = cons(car(p), n);
    }
    return (n);
}

// 仅仅是处理let的AST
// e.g. (let ((x 1) (y 2)) (+ x y))的内部表示为:
// (let-internal ((y x) ((2 1) ((+ x y) NIL))))
LISP let_macro(LISP form) {
    LISP p, fl, al, tmp;
    fl = NIL; // 形参列表
    al = NIL; // 实参列表

    for (p = car(cdr(form)); NNULLP(p); p = cdr(p)) {
        tmp = car(p); // 取得一个形参实参的pair, e.g. (x (1 NIL))
        if SYMBOLP(tmp) {
            fl = cons(tmp, fl);
            al = cons(NIL, al);
        } else {
            fl = cons(car(tmp), fl);
            al = cons(car(cdr(tmp)), al);
        }
    }
    p = cdr(cdr(form));
    if NULLP(cdr(p)) {
        p = car(p);
    } else {
        p = cons(sym_progn, p);
    }
    setcdr(form, cons(reverse(fl), cons(reverse(al), cons(p, NIL))));
    setcar(form, cintern("let-internal"));
    return (form);
}

LISP leval_quote(LISP args, LISP env) {
    return (car(args));
}

LISP leval_tenv(LISP args, LISP env) { return (env); }

LISP leval_while(LISP args, LISP env) {
    LISP l;
    while NNULLP(leval(car(args), env))
        for (l = cdr(args); NNULLP(l); l = cdr(l))
            leval(car(l), env);
    return (NIL);
}

LISP symbolconc(LISP args) {
    long size;
    LISP l, s;
    size = 0;
    tkbuffer[0] = 0;
    for (l = args; NNULLP(l); l = cdr(l)) {
        s = car(l);
        if NSYMBOLP(s) err("wta(non-symbol) to symbolconc", s);
        size = size + strlen(PNAME(s));
        if (size > TKBUFFERN) err("symbolconc buffer overflow", NIL);
        strcat(tkbuffer, PNAME(s));
    }
    return (rintern(tkbuffer));
}

void set_print_hooks(long type, void (*fcn)(LISP, FILE *)) {
    struct user_type_hooks *p;
    p = get_user_type_hooks(type);
    p->prin1 = fcn;
}

LISP lprin1f(LISP exp, FILE *f) {
    LISP tmp;
    struct user_type_hooks *p;
    STACK_CHECK(&exp);
    INTERRUPT_CHECK();
    switch TYPE(exp) {
        case tc_nil:
            fput_st(f, "()");
            break;
        case tc_cons:
            fput_st(f, "(");
            lprin1f(car(exp), f);
            for (tmp = cdr(exp); CONSP(tmp); tmp = cdr(tmp)) {
                fput_st(f, " ");
                lprin1f(car(tmp), f);
            }
            if NNULLP(tmp) {
                fput_st(f, " . ");
                lprin1f(tmp, f);
            }
            fput_st(f, ")");
            break;
        case tc_flonum:
            sprintf(tkbuffer, "%g", FLONM(exp));
            fput_st(f, tkbuffer);
            break;
        case tc_symbol:
            fput_st(f, PNAME(exp));
            break;
        case tc_subr_0:
        case tc_subr_1:
        case tc_subr_2:
        case tc_subr_3:
        case tc_subr_4:
        case tc_lsubr:
        case tc_fsubr:
        case tc_msubr:
            sprintf(tkbuffer, "#<SUBR(%d) ", TYPE(exp));
            fput_st(f, tkbuffer);
            fput_st(f, (*exp).storage_as.subr.name);
            fput_st(f, ">");
            break;
        case tc_closure:
            fput_st(f, "#<CLOSURE ");
            lprin1f(car((*exp).storage_as.closure.code), f);
            fput_st(f, " ");
            lprin1f(cdr((*exp).storage_as.closure.code), f);
            fput_st(f, ">");
            break;
        case tc_struct_instance:
        case tc_struct_instance_with_rec:
            sprintf(tkbuffer, "#<STRUCT %p>", exp);
            fput_st(f, tkbuffer);
            break;
        case tc_struct_def:
            fput_st(f, "#<STRUCT-DEF ");
            lprin1f((*exp).storage_as.struct_def.class_name_sym, f);
            fput_st(f, " (");
            for (long i = 0; i < exp->storage_as.struct_def.dim; ++i) {
                lprin1f((*exp).storage_as.struct_def.field_name_strs[i]->storage_as.string.data, f);
                if (i != exp->storage_as.struct_def.dim - 1) {
                    fput_st(f, " ");
                }
            }
            fput_st(f, ")>");
            break;
        default:
            p = get_user_type_hooks(TYPE(exp));
            if (p->prin1)
                (*p->prin1)(exp, f);
            else {
                sprintf(tkbuffer, "#<UNKNOWN %d %p>", TYPE(exp), exp);
                fput_st(f, tkbuffer);
            }
    }
    return (NIL);
}

LISP lprint(LISP exp) {
    lprin1f(exp, stdout);
    put_st("\n");
    return (NIL);
}

LISP lread(void) { return (lreadf(stdin)); }

int f_getc(FILE *f) {
    long iflag, dflag;
    int c;
    iflag = no_interrupt(1);
    dflag = interrupt_differed;
    c = getc(f);
#ifdef VMS
   if ((dflag == 0) & interrupt_differed & (f == stdin))
   while((c != 0) & (c != EOF)) c = getc(f);
#endif
    no_interrupt(iflag);
    return (c);
}

void f_ungetc(int c, FILE *f) { ungetc(c, f); }

// 忽略注释换行等 返回一个具体字符
int flush_ws(struct gen_readio *f, char *eoferr) {
    int c, commentp;
    commentp = 0; // 是否是注释
    while (1) {
        c = GETC_FCN(f);
        if (c == EOF) {
            if (eoferr) {
                err(eoferr, NIL);
            } else {
                return (c);
            }
        }
        if (commentp) {
            if (c == '\n') {
                commentp = 0;
            }
        } else if (c == ';') {
            commentp = 1;
        } else if (!isspace(c)) {
            return (c);
        }
    }
}

// 构造读取文件的结构体
LISP lreadf(FILE *f) {
    struct gen_readio s;
    s.getc_fcn = (int (*)(char *)) f_getc;
    s.ungetc_fcn = (void (*)(int, char *)) f_ungetc;
    s.cb_argument = (char *) f;
    return (readtl(&s));
}

LISP readtl(struct gen_readio *f) {
    int c;
    c = flush_ws(f, (char *) NULL);
    if (c == EOF) return (eof_val);
    UNGETC_FCN(c, f);
    return (lreadr(f));
}

void set_read_hooks(char *all_set, char *end_set,
                    LISP (*fcn1)(int, struct gen_readio *),
                    LISP (*fcn2)(char *, long, int *)) {
    user_ch_readm = all_set;
    user_te_readm = end_set;
    user_readm = fcn1;
    user_readt = fcn2;
}

LISP lreadr(struct gen_readio *f) {
    int c, j;
    char *p;
    STACK_CHECK(&f);
    p = tkbuffer;
    c = flush_ws(f, "end of file inside read");
    switch (c) {
        case '(':
            return (lreadparen(f));
        case ')':
            err("unexpected close paren", NIL);
        case '\'':
            return (cons(sym_quote, cons(lreadr(f), NIL)));
        case '`':
            return (cons(cintern("+internal-backquote"), lreadr(f)));
        case ',':
            c = GETC_FCN(f);
            switch (c) {
                case '@':
                    p = "+internal-comma-atsign";
                    break;
                case '.':
                    p = "+internal-comma-dot";
                    break;
                default:
                    p = "+internal-comma";
                    UNGETC_FCN(c, f);
            }
            return (cons(cintern(p), lreadr(f)));
        case '"':
            return (lreadstring(f));
        default:
            if ((user_readm != NULL) && strchr(user_ch_readm, c))
                return ((*user_readm)(c, f));
    }
    *p++ = c;
    for (j = 1; j < TKBUFFERN; ++j) {
        c = GETC_FCN(f);
        if (c == EOF) return (lreadtk(j));
        if (isspace(c)) return (lreadtk(j));
        if (strchr("()'`,;\"", c) || strchr(user_te_readm, c)) {
            UNGETC_FCN(c, f);
            return (lreadtk(j));
        }
        *p++ = c;
    }
    return (err("token larger than TKBUFFERN", NIL));
}

LISP lreadparen(struct gen_readio *f) {
    int c;
    LISP tmp;
    c = flush_ws(f, "end of file inside list");
    if (c == ')') return (NIL);
    UNGETC_FCN(c, f);
    tmp = lreadr(f);
    if EQ(tmp, sym_dot) {
        tmp = lreadr(f);
        c = flush_ws(f, "end of file inside list");
        if (c != ')') err("missing close paren", NIL);
        return (tmp);
    }
    return (cons(tmp, lreadparen(f)));
}

LISP lreadtk(long j) {
    int flag;
    char *p;
    LISP tmp;
    int adigit;
    p = tkbuffer;
    p[j] = 0;
    if (user_readt != NULL) {
        tmp = (*user_readt)(p, j, &flag);
        if (flag) return (tmp);
    }
    if (*p == '-') p += 1;
    adigit = 0;
    while (isdigit(*p)) {
        p += 1;
        adigit = 1;
    }
    if (*p == '.') {
        p += 1;
        while (isdigit(*p)) {
            p += 1;
            adigit = 1;
        }
    }
    if (!adigit) goto a_symbol;
    if (*p == 'e') {
        p += 1;
        if (*p == '-' || *p == '+') p += 1;
        if (!isdigit(*p)) goto a_symbol; else p += 1;
        while (isdigit(*p)) p += 1;
    }
    if (*p) goto a_symbol;
    return (flocons(atof(tkbuffer)));
    a_symbol:
    return (rintern(tkbuffer));
}

LISP copy_list(LISP x) {
    if NULLP(x) return (NIL);
    STACK_CHECK(&x);
    return (cons(car(x), copy_list(cdr(x))));
}

LISP apropos(LISP matchl) {
    LISP result = NIL, l, ml;
    char *pname;
    for (l = oblistvar; CONSP(l); l = CDR(l)) {
        pname = get_c_string(CAR(l));
        ml = matchl;
        while (CONSP(ml) && strstr(pname, get_c_string(CAR(ml))))
            ml = CDR(ml);
        if NULLP(ml)
            result = cons(CAR(l), result);
    }
    return (result);
}

LISP fopen_c(char *name, char *how) {
    LISP sym;
    long flag;
    flag = no_interrupt(1);
    sym = newcell(tc_c_file);
    sym->storage_as.c_file.f = (FILE *) NULL;
    sym->storage_as.c_file.name = (char *) NULL;
    if (!(sym->storage_as.c_file.f = fopen(name, how))) {
        perror(name);
        put_st("\n");
        err("could not open file", NIL);
    }
    sym->storage_as.c_file.name = (char *) must_malloc(strlen(name) + 1);
    strcpy(sym->storage_as.c_file.name, name);
    no_interrupt(flag);
    return (sym);
}

LISP fopen_l(LISP name, LISP how) { return (fopen_c(get_c_string(name), NULLP(how) ? "r" : get_c_string(how))); }

LISP delq(LISP elem, LISP l) {
    if NULLP(l) return (l);
    STACK_CHECK(&elem);
    if EQ(elem, car(l)) return (cdr(l));
    setcdr(l, delq(elem, cdr(l)));
    return (l);
}

LISP fclose_l(LISP p) {
    long flag;
    flag = no_interrupt(1);
    if NTYPEP(p, tc_c_file) err("not a file", p);
    file_gc_free(p);
    no_interrupt(flag);
    return (NIL);
}

LISP vload(char *fname, long cflag) {
    LISP form, result, tail, lf;
    FILE *f;
    put_st("loading ");
    put_st(fname);
    put_st("\n");
    lf = fopen_c(fname, "r");
    f = lf->storage_as.c_file.f;
    result = NIL;
    tail = NIL;
    while (1) {
        form = lreadf(f);
        if EQ(form, eof_val) break;
        if (cflag) {
            form = cons(form, NIL);
            if NULLP(result)
                result = tail = form;
            else
                tail = setcdr(tail, form);
        } else
            leval(form, NIL);
    }
    fclose_l(lf);
    put_st("done.\n");
    return (result);
}

LISP load(LISP fname, LISP cflag) { return (vload(get_c_string(fname), NULLP(cflag) ? 0 : 1)); }

LISP save_forms(LISP fname, LISP forms, LISP how) {
    char *cname, *chow = NULL;
    LISP l, lf;
    FILE *f;
    cname = get_c_string(fname);
    if EQ(how, NIL) chow = "w";
    else if EQ(how, cintern("a")) chow = "a";
    else err("bad argument to save-forms", how);
    put_st((*chow == 'a') ? "appending" : "saving");
    put_st(" forms to ");
    put_st(cname);
    put_st("\n");
    lf = fopen_c(cname, chow);
    f = lf->storage_as.c_file.f;
    for (l = forms; NNULLP(l); l = cdr(l)) {
        lprin1f(car(l), f);
        putc('\n', f);
    }
    fclose_l(lf);
    put_st("done.\n");
    return (truth);
}

LISP quit(void) {
    longjmp(errjmp, 2);
    return (NIL);
}

LISP nullp(LISP x) { if EQ(x, NIL) return (truth); else return (NIL); }

LISP arglchk(LISP x) {
#if (!ENVLOOKUP_TRICK)
                                                                                                                            LISP l;
 if SYMBOLP(x) return(x);
 for(l=x;CONSP(l);l=CDR(l));
 if NNULLP(l) err("improper formal argument list",x);
#endif
    return (x);
}

void file_gc_free(LISP ptr) {
    if (ptr->storage_as.c_file.f) {
        fclose(ptr->storage_as.c_file.f);
        ptr->storage_as.c_file.f = (FILE *) NULL;
    }
    if (ptr->storage_as.c_file.name) {
        free(ptr->storage_as.c_file.name);
        ptr->storage_as.c_file.name = NULL;
    }
}

void file_prin1(LISP ptr, FILE *f) {
    char *name;
    name = ptr->storage_as.c_file.name;
    fput_st(f, "#<FILE ");
    sprintf(tkbuffer, " %p", ptr->storage_as.c_file.f);
    fput_st(f, tkbuffer);
    if (name) {
        fput_st(f, " ");
        fput_st(f, name);
    }
    fput_st(f, ">");
}

FILE *get_c_file(LISP p, FILE *deflt) {
    if (NULLP(p) && deflt) return (deflt);
    if NTYPEP(p, tc_c_file) err("not a file", p);
    if (!p->storage_as.c_file.f) err("file is closed", p);
    return (p->storage_as.c_file.f);
}

LISP lgetc(LISP p) {
    int i;
    i = f_getc(get_c_file(p, stdin));
    return ((i == EOF) ? NIL : flocons((double) i));
}

LISP lputc(LISP c, LISP p) {
    long flag;
    int i;
    FILE *f;
    f = get_c_file(p, stdout);
    if FLONUMP(c)
        i = (int) FLONM(c);
    else
        i = *get_c_string(c);
    flag = no_interrupt(1);
    putc(i, f);
    no_interrupt(flag);
    return (NIL);
}

LISP lputs(LISP str, LISP p) {
    fput_st(get_c_file(p, stdout), get_c_string(str));
    return (NIL);
}

LISP lftell(LISP file) { return (flocons((double) ftell(get_c_file(file, NULL)))); }

LISP lfseek(LISP file, LISP offset, LISP direction) {
    return ((fseek(get_c_file(file, NULL), get_c_long(offset), get_c_long(direction)))
            ? NIL : truth);
}

LISP parse_number(LISP x) {
    char *c;
    c = get_c_string(x);
    return (flocons(atof(c)));
}

void init_subrs(void) {
    init_subrs_1();
    init_subrs_a();
}

LISP closure_code(LISP exp) { return (exp->storage_as.closure.code); }

LISP closure_env(LISP exp) { return (exp->storage_as.closure.env); }

LISP lwhile(LISP form, LISP env) {
    LISP l;
    while (NNULLP(leval(car(form), env)))
        for (l = cdr(form); NNULLP(l); l = cdr(l))
            leval(car(l), env);
    return (NIL);
}

LISP nreverse(LISP x) {
    LISP newp, oldp, nextp;
    newp = NIL;
    for (oldp = x; CONSP(oldp); oldp = nextp) {
        nextp = CDR(oldp);
        CDR(oldp) = newp;
        newp = oldp;
    }
    return (newp);
}

/**
 * (define-struct class_name ("field1" "field2" "field3" ...))
 */
LISP define_struct(LISP args, LISP env) {
    LISP class_name = car(args);

    if NSYMBOLP(class_name) {
        err("wta(non-symbol) to define_struct", class_name);
    }

    // 计数
    long num = 0;
    LISP other_args = car(cdr(args));
    for (; NULL != other_args; other_args = cdr(other_args)) {
        ++num;
    }

    if (num < 0) {
        err("why num < 0? (see define_struct)", NIL);
    }

    LISP class_obj = newcell(tc_struct_def);
    class_obj->storage_as.struct_def.class_name_sym = class_name;
    class_obj->storage_as.struct_def.dim = num;
    class_obj->storage_as.struct_def.field_name_strs = (LISP *) must_malloc(sizeof(LISP) * num);
    args = car(cdr(args));
    for (long i = 0; i < num; i++) {
        class_obj->storage_as.struct_def.field_name_strs[i] = car(args);
        args = cdr(args);
    }
    class_obj->storage_as.struct_def.length = 0;
    class_obj->storage_as.struct_def.assign_field_indexes = NULL;
    class_obj->storage_as.struct_def.pre = NIL;
    class_obj->storage_as.struct_def.next = NIL;
    class_obj->storage_as.struct_def.active_mark = 1;

    // automatically create constructor, accessor and mutator procedures for the new struct
    // LISP tmp_exp;

    // 定义的struct为全局的
    return (VCELL(class_name) = class_obj);
}

// create instance of a struct def
LISP new_struct_instance(LISP struct_def) {
    if (struct_def->type != tc_struct_def) {
        err("wrong struct_def obj!(see new_struct_instance)", struct_def);
    }

    struct_def = get_active_struct_def_obj(struct_def);

    long length = struct_def->storage_as.struct_def.length;
    long type = length <= 0 ? tc_struct_instance : tc_struct_instance_with_rec;
    LISP instance = newcell(type);

    long dim = struct_def->storage_as.struct_def.dim;
    if (tc_struct_instance == type) {
        instance->storage_as.struct_instance.struct_def_obj = struct_def;
        instance->storage_as.struct_instance.data = (LISP *) must_malloc(dim * sizeof(LISP));
        for (long i = 0; i < dim; ++i) {
            instance->storage_as.struct_instance.data[i] = NIL;
        }
    } else {
        instance->storage_as.struct_instance_with_rec.struct_def_obj = struct_def;
        instance->storage_as.struct_instance_with_rec.data = (LISP *) must_malloc(dim * sizeof(LISP));
        for (long i = 0; i < dim; ++i) {
            instance->storage_as.struct_instance_with_rec.data[i] = NIL;
        }
        instance->storage_as.struct_instance_with_rec.assign_sites = (long *) must_malloc(length * sizeof(long));
        memset(instance->storage_as.struct_instance_with_rec.assign_sites, 0, length);
    }

    return (instance);
}

// (set-field struct-instance "field1" val1 line_num)
LISP set_field(LISP struct_instance, LISP field_name, LISP val, LISP line_num) {
    if (NULL == struct_instance) {
        err("why struct_instance is null? (see set_field)", struct_instance);
    }

    if (!(tc_struct_instance <= struct_instance->type && struct_instance->type <= tc_struct_instance_with_rec)) {
        err("wrong struct_instance obj! (see set_field)", struct_instance);
    }

    LISP struct_def = get_struct_def_obj(struct_instance);
    if (NULL == struct_def) {
        err("why struct_def is null! (see set_field)", struct_def);
    }

    long field_index = -1L;
    for (long i = 0; i < struct_def->storage_as.struct_def.dim; ++i) {
        LISP tmp_str = struct_def->storage_as.struct_def.field_name_strs[i];
        if (0 == strcmp(tmp_str->storage_as.string.data, field_name->storage_as.string.data)) {
            field_index = i;
            break;
        }
    }

    if (field_index < 0) {
        err("wrong field name! (see set_field)", field_name);
    }

    if (tc_struct_instance == struct_instance->type) {
        struct_instance->storage_as.struct_instance.data[field_index] = val;
    } else {
        struct_instance->storage_as.struct_instance_with_rec.data[field_index] = val;
    }

    if (recording_assign_site_on) {
        long ln = (long) line_num->storage_as.flonum.data;
        try_record_assign_site(struct_instance, val, ln, field_index);
    }

    return (NIL);
}

// (get-field struct-instance "field1")
LISP get_field(LISP struct_instance, LISP field_name) {
    if (NULL == struct_instance) {
        err("why struct_instance is null? (see get_field)", struct_instance);
    }

    if (!(tc_struct_instance <= struct_instance->type && struct_instance->type <= tc_struct_instance_with_rec)) {
        err("wrong struct_instance obj! (see get_field)", struct_instance);
    }

    LISP struct_def = get_struct_def_obj(struct_instance);
    if (NULL == struct_def) {
        err("why struct_def is null! (see get_field)", struct_def);
    }

    long field_index = -1L;
    for (long i = 0; i < struct_def->storage_as.struct_def.dim; ++i) {
        LISP tmp_str = struct_def->storage_as.struct_def.field_name_strs[i];
        if (0 == strcmp(tmp_str->storage_as.string.data, field_name->storage_as.string.data)) {
            field_index = i;
            break;
        }
    }

    if (field_index < 0) {
        err("wrong field name! (see get_field)", field_name);
    }

    if (tc_struct_instance == struct_instance->type) {
        return (struct_instance->storage_as.struct_instance.data[field_index]);
    } else {
        return (struct_instance->storage_as.struct_instance_with_rec.data[field_index]);
    }
}

void init_subrs_1(void) {
    init_subr_3("cons", external_cons);
    init_subr_1("car", car);
    init_subr_1("cdr", cdr);
    init_subr_3("set-car!", setcar_exteral);
    init_subr_3("set-cdr!", setcdr_exteral);
    init_subr_2("+", plus);
    init_subr_2("-", difference);
    init_subr_2("*", ltimes);
    init_subr_2("/", quotient);
    init_subr_2(">", greaterp);
    init_subr_2("<", lessp);
    init_subr_2("eq?", eq);
    init_subr_2("eqv?", eql);
    init_subr_2("assq", assq);
    init_subr_2("delq", delq);
    init_subr_0("read", lread);
    init_subr_0("eof-val", get_eof_val);
    init_subr_1("print", lprint);
    init_subr_2("eval", leval);
    init_fsubr("define", leval_define);
    init_fsubr("lambda", leval_lambda);
    init_msubr("if", leval_if);
    init_fsubr("while", leval_while);
    init_msubr("begin", leval_progn);
    init_fsubr("set!", leval_setq);
    init_msubr("or", leval_or);
    init_msubr("and", leval_and);
    init_fsubr("*catch", leval_catch);
    init_subr_2("*throw", lthrow);
    init_fsubr("quote", leval_quote);
    init_lsubr("apropos", apropos);
    init_subr_1("copy-list", copy_list);
    init_lsubr("gc-status", gc_status);
    init_lsubr("gc", user_gc);
    init_subr_2("load", load);
    init_subr_1("pair?", consp);
    init_subr_1("symbol?", symbolp);
    init_subr_1("number?", numberp);
    init_msubr("let-internal", leval_let);
    init_subr_1("let-internal-macro", let_macro);
    init_subr_2("symbol-bound?", symbol_boundp);
    init_subr_2("symbol-value", symbol_value);
    init_subr_3("set-symbol-value!", setvar);
    init_fsubr("the-environment", leval_tenv);
    init_subr_2("error", lerr);
    init_subr_0("quit", quit);
    init_subr_1("not", nullp);
    init_subr_1("null?", nullp);
    init_subr_2("env-lookup", envlookup);
    init_subr_1("reverse", reverse);
    init_lsubr("symbolconc", symbolconc);
    init_subr_3("save-forms", save_forms);
    init_subr_2("fopen", fopen_l);
    init_subr_1("fclose", fclose_l);
    init_subr_1("getc", lgetc);
    init_subr_2("putc", lputc);
    init_subr_2("puts", lputs);
    init_subr_1("ftell", lftell);
    init_subr_3("fseek", lfseek);
    init_subr_1("parse-number", parse_number);
    init_subr_2("%%stack-limit", stack_limit);
    init_subr_1("intern", intern);
    init_subr_2("%%closure", closure);
    init_subr_1("%%closure-code", closure_code);
    init_subr_1("%%closure-env", closure_env);
    init_fsubr("while", lwhile);
    init_subr_1("nreverse", nreverse);
    init_subr_0("allocate-heap", allocate_aheap);
    init_subr_1("gc-info", gc_info);
    init_subr_2("assert-dead", assert_dead);
    init_fsubr("define-struct", define_struct);
    init_subr_1("new-struct-instance", new_struct_instance);
    init_subr_4("set-field", set_field);
    init_subr_2("get-field", get_field);
    init_subr_0("mark-clock-start", mark_clock_start);
    init_subr_0("mark-clock-end", mark_clock_end);
    init_subr_0("print-clock-time-cost", print_clock_time_cost);
}

/* err0,pr,prp are convenient to call from the C-language debugger */

void err0(void) { err("0", NIL); }

void pr(LISP p) {
    if (looks_pointerp(p))
        lprint(p);
    else
        put_st("invalid\n");
}

void prp(LISP *p) {
    if (!p) return;
    pr(*p);
}

LISP mark_clock_start() {
    clock_start = clock();
    return (NIL);
}

LISP mark_clock_end() {
    clock_end = clock();
    return (NIL);
}

LISP print_clock_time_cost() {
    double duration = (double) (clock_end - clock_start) / CLOCKS_PER_SEC;
    printf("%f sec(s) cpu time cost.\n", duration - gc_clock_time_taken);
    clock_start = 0;
    clock_end = 0;
    gc_clock_time_taken = 0.0;
    return (NIL);
}

void mark_gc_clock_start() {
    gc_clock_start = clock();
}

void mark_gc_clock_end() {
    gc_clock_end = clock();
    double duration = (double) (gc_clock_end - gc_clock_start) / CLOCKS_PER_SEC;
    gc_clock_time_taken = gc_clock_time_taken + duration;
    gc_clock_start = 0;
    gc_clock_end = 0;
}
