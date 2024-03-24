/* $Id: defs.h,v 1.74 2023/05/18 21:28:05 tom Exp $ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

//#define TRACE
//#define DEBUG
//#define YYBTYACC
#define NO_LEAKS

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#if defined(__cplusplus)	/* __cplusplus, etc. */
#define class myClass
#endif

#define YYMAJOR 2
#define YYMINOR 0

#define CONCAT(first,second)    first #second
#define CONCAT1(string,number)  CONCAT(string, number)
#define CONCAT2(first,second)   #first "." #second

#ifdef YYPATCH
#define VSTRING(a,b) CONCAT2(a,b) CONCAT1(" ",YYPATCH)
#else
#define VSTRING(a,b) CONCAT2(a,b)
#endif

#define VERSION VSTRING(YYMAJOR, YYMINOR)

/*  machine-dependent definitions:			*/

/*  MAXCHAR is the largest unsigned character value	*/
/*  MAXTABLE is the maximum table size			*/
/*  YYINT is the smallest C integer type that can be	*/
/*	used to address a table of size MAXTABLE	*/
/*  MAXYYINT is the largest value of a YYINT		*/
/*  MINYYINT is the most negative value of a YYINT	*/
/*  BITS_PER_WORD is the number of bits in a C unsigned	*/
/*  WORDSIZE computes the number of words needed to	*/
/*	store n bits					*/
/*  BIT returns the value of the n-th bit starting	*/
/*	from r (0-indexed)				*/
/*  SETBIT sets the n-th bit starting from r		*/

#define	MAXCHAR		UCHAR_MAX
#ifndef MAXTABLE
#define MAXTABLE	INT_MAX
#endif
#if MAXTABLE <= SHRT_MAX
#define YYINT		short
#define MAXYYINT	SHRT_MAX
#define MINYYINT	SHRT_MIN
#elif MAXTABLE <= INT_MAX
#define YYINT		int
#define MAXYYINT	INT_MAX
#define MINYYINT	INT_MIN
#elif MAXTABLE <= LONG_MAX
#define YYINT		long
#define MAXYYINT	LONG_MAX
#define MINYYINT	LONG_MIN
#else
#error "MAXTABLE is too large for this machine architecture!"
#endif

#ifndef BITWORD_T
//#define BITWORD_T unsigned
#define BITWORD_T size_t
#endif

typedef BITWORD_T bitword_t;

#define ONE_AS_BITWORD  ((BITWORD_T)1)
#define BITS_PER_WORD	((int) sizeof (bitword_t) * CHAR_BIT)
#define WORDSIZE(n)	(((n)+(BITS_PER_WORD-1))/BITS_PER_WORD)
#define BIT(r, n)	((((r)[(n)/BITS_PER_WORD])>>((n)&(BITS_PER_WORD-1)))&1)
#define SETBIT(r, n)	((r)[(n)/BITS_PER_WORD]|=(ONE_AS_BITWORD<<((n)&(BITS_PER_WORD-1))))

/*  character names  */

#define	NUL		'\0'	/*  the null character  */
#define	NEWLINE		'\n'	/*  line feed  */
#define	SP		' '	/*  space  */
#define	BS		'\b'	/*  backspace  */
#define	HT		'\t'	/*  horizontal tab  */
#define	VT		'\013'	/*  vertical tab  */
#define	CR		'\r'	/*  carriage return  */
#define	FF		'\f'	/*  form feed  */
#define	QUOTE		'\''	/*  single quote  */
#define	DOUBLE_QUOTE	'\"'	/*  double quote  */
#define	BACKSLASH	'\\'	/*  backslash  */

#define UCH(c)          (unsigned char)(c)

/* defines for constructing filenames */

#if defined(VMS)
#define CODE_SUFFIX	"_code.c"
#define	DEFINES_SUFFIX	"_tab.h"
#define	EXTERNS_SUFFIX	"_tab.i"
#define	OUTPUT_SUFFIX	"_tab.c"
#else
#define CODE_SUFFIX	".code.c"
#define	DEFINES_SUFFIX	".tab.h"
#define	EXTERNS_SUFFIX	".tab.i"
#define	OUTPUT_SUFFIX	".tab.c"
#endif
#define	VERBOSE_SUFFIX	".output"
#define	EBNF_SUFFIX	".ebnf"
#define	LEMON_SUFFIX	".yl"
#define GRAPH_SUFFIX    ".dot"
#define NAKED_SUFFIX    ".yn"
#define UNICC_SUFFIX    ".ypar"
#define CARBURETTA_SUFFIX    ".ycbrt"
#define SQL_SUFFIX      ".sql"
#define VERBOSE_RULE_POINT_CHAR    '.'
#define RULE_NUM_OFFSET 2

/* keyword codes */

typedef enum
{
    TOKEN = 0
    ,LEFT
    ,RIGHT
    ,NONASSOC
    ,PRECEDENCE
    ,MARK
    ,TEXT
    ,TYPE
    ,START
    ,UNION
    ,IDENT

    /* trivial bison "extensions" which have POSIX equivalents */
    ,NONPOSIX_DEBUG

    /* other bison "extensions", some useful */
    ,HACK_DEFINE
    ,ERROR_VERBOSE
    ,EXPECT
    ,EXPECT_RR
    ,LEX_PARAM
    ,PARSE_PARAM
    ,POSIX_YACC
    ,PURE_PARSER
    ,TOKEN_TABLE
    ,XCODE

#if defined(YYBTYACC)
    ,DESTRUCTOR
    ,INITIAL_ACTION
    ,LOCATIONS
#endif
}
KEY_CASES;

/*  symbol classes  */

typedef enum
{
    UNKNOWN = 0
    ,TERM
    ,NONTERM
    ,ACTION
    ,ARGUMENT
}
SYM_CASES;

/*  the undefined value  */

#define UNDEFINED (-1)

/*  action codes  */

#define SHIFT 1
#define REDUCE 2

/*  character macros  */

#define IS_NAME1(c)	(isalpha(UCH(c)) || (c) == '_' || (c) == '$')
#define IS_NAME2(c)	(isalnum(UCH(c)) || (c) == '_' || (c) == '$')
#define IS_IDENT(c)	(isalnum(UCH(c)) || (c) == '_' || (c) == '.' || (c) == '$')
#define	IS_OCTAL(c)	((c) >= '0' && (c) <= '7')

/*  symbol macros  */

#define ISTOKEN(s)	((s) < S->start_symbol)
#define ISVAR(s)	((s) >= S->start_symbol)

/*  storage allocation macros  */

#define CALLOC(k,n)	(calloc((size_t)(k),(size_t)(n)))
#define	FREE(x)		(free((char*)(x)))
#define MALLOC(n)	(malloc((size_t)(n)))
#define TCMALLOC(t,n)	((t*) calloc((size_t)(n), sizeof(t)))
#define TMALLOC(t,n)	((t*) malloc((size_t)(n) * sizeof(t)))
#define	NEW(t)		((t*)allocate(S, sizeof(t)))
#define	NEW2(n,t)	((t*)allocate(S, ((size_t)(n)*sizeof(t))))
#define REALLOC(p,n)	(realloc((char*)(p),(size_t)(n)))
#define TREALLOC(t,p,n)	((t*)realloc((char*)(p), (size_t)(n) * sizeof(t)))

#define DO_FREE(x)	if (x) { FREE(x); x = 0; }

#define NO_SPACE(p)	do { if (p == 0) on_error(S); assert(p != 0); } while (0)

/* messages */
#define PLURAL(n) ((n) > 1 ? "s" : "")

/*
 * Features which depend indirectly on the btyacc configuration, but are not
 * essential.
 */
#if defined(YYBTYACC)
#define USE_HEADER_GUARDS 1
#else
#define USE_HEADER_GUARDS 0
#endif

typedef char Assoc_t;
typedef char Class_t;
typedef YYINT Index_t;
typedef YYINT Value_t;

/*  the structure of a symbol table entry  */

typedef struct bucket bucket;
struct bucket
{
    struct bucket *link;
    struct bucket *next;
    char *name;
    char *tag;
#if defined(YYBTYACC)
    char **argnames;
    char **argtags;
    int args;
    char *destructor;
#endif
    Value_t value;
    Index_t index;
    Value_t prec;
    Class_t class;
    Assoc_t assoc;
    int prec_on_decl;
};

/*  the structure of the LR(0) state machine  */

typedef struct core core;
struct core
{
    struct core *next;
    struct core *link;
    Value_t number;
    Value_t accessing_symbol;
    Value_t nitems;
    Value_t items[1];
};

/*  the structure used to record shifts  */

typedef struct shifts shifts;
struct shifts
{
    struct shifts *next;
    Value_t number;
    Value_t nshifts;
    Value_t shift[1];
};

/*  the structure used to store reductions  */

typedef struct reductions reductions;
struct reductions
{
    struct reductions *next;
    Value_t number;
    Value_t nreds;
    Value_t rules[1];
};

/*  the structure used to represent parser actions  */

typedef struct action action;
struct action
{
    struct action *next;
    Value_t symbol;
    Value_t number;
    Value_t prec;
    char action_code;
    Assoc_t assoc;
    char suppressed;
};

/*  the structure used to store parse/lex parameters  */
typedef struct param param;
struct param
{
    struct param *next;
    char *name;		/* parameter name */
    char *type;		/* everything before parameter name */
    char *type2;	/* everything after parameter name */
};

/*From lalr.c*/
typedef struct shorts
{
    struct shorts *next;
    Value_t value;
}
shorts;

/* reader.c */

typedef enum
{
    CODE_HEADER = 0
    ,CODE_REQUIRES
    ,CODE_PROVIDES
    ,CODE_TOP
    ,CODE_IMPORTS
    ,CODE_MAX		/* this must be last */
}
CODE_CASES;

typedef struct
{
    char *line_data;	/* saved input-line */
    size_t line_used;	/* position within saved input-line */
    size_t line_size;	/* length of saved input-line */
    fpos_t line_fpos;	/* pointer before reading past saved input-line */
}
SAVE_LINE;

typedef struct byacc_t {
    char dflag2;
    char dflag;
    char gflag;
    char iflag;
    char lflag;
    char rflag;
    char sflag;
    char tflag;
    char vflag;
    char ebnf_flag;
    char lemon_flag;
    char lemon_prec_flag;
    char ignore_prec_flag;
    char naked_flag;
    char nakedq_flag;
    char unicc_flag;
    char carburetta_flag;
    char sql_flag;
    const char *symbol_prefix;

    const char *myname;
    char *cptr; /* position within current input-line */
    char *line; /* current input-line */
    int lineno;
    int outline;
    int exit_code;
    int pure_parser;
    int token_table;
    int error_verbose;
#if defined(YYBTYACC)
    int locations; /* default to no position processing */
    int backtrack; /* default is no backtracking */
    int destructor; /* =1 if at least one %destructor */
    bucket *default_destructor[3];
    char *initial_action;
#endif

    char *code_file_name;
    char *input_file_name;
    size_t input_file_name_len;
    char *defines_file_name;
    char *externs_file_name;
    char *file_prefix;

    char *graph_file_name;
    char *output_file_name;
    char *verbose_file_name;
    char *ebnf_file_name;
    char *lemon_file_name;
    char *naked_file_name;
    char *nakedq_file_name;
    char *unicc_file_name;
    char *carburetta_file_name;
    char *sql_file_name;

    FILE *action_file;	/*  a temp file, used to save actions associated    */
                            /*  with rules until the parser is written          */
    FILE *code_file;	/*  y.code.c (used when the -r option is specified) */
    FILE *defines_file;	/*  y.tab.h                                         */
    FILE *externs_file;	/*  y.tab.i                                         */
    FILE *input_file;	/*  the input file                                  */
    FILE *output_file;	/*  y.tab.c                                         */
    FILE *text_file;	/*  a temp file, used to save text until all        */
                            /*  symbols have been defined                       */
    FILE *union_file;	/*  a temp file, used to save the union             */
                            /*  definition until all symbol have been           */
                            /*  defined                                         */
    FILE *verbose_file;	/*  y.output                                        */
    FILE *ebnf_file;	/*  y.ebnf                                          */
    FILE *lemon_file;	/*  lemon.y                                         */
    FILE *naked_file;	/*  naked.y                                         */
    FILE *nakedq_file;	/*  naked.y                                         */
    FILE *unicc_file;	/*  unicc.y                                         */
    FILE *carburetta_file;	/*  carburetra.cbrt                                 */
    FILE *sql_file;         /*  sql.y                                           */
    FILE *graph_file;	/*  y.dot                                           */

    Value_t nitems;
    Value_t nrules;
    Value_t nsyms;
    Value_t ntokens;
    Value_t nvars;
    int ntags;

    char unionized;
    const char *line_format;

#define fprintf_lineno(f, n, s) \
	    if (!S->lflag) \
		fprintf(f, S->line_format, (n), (s) ? (s) : "(null)")

    Value_t start_symbol;
    char **symbol_name;
    char **symbol_pname;
    Value_t *symbol_value;
    Value_t *symbol_prec;
    char *symbol_assoc;

#if defined(YYBTYACC)
    Value_t *symbol_pval;
    char **symbol_destructor;
    char **symbol_type_tag;
#endif

    Value_t *ritem;
    Value_t *rlhs;
    Value_t *rrhs;
    Value_t *rprec;
    Assoc_t *rassoc;
    bucket** rprec_bucket;

    Value_t **derives;
    char *nullable;

    bucket *first_symbol;
    bucket *last_symbol;

    Value_t nstates;
    core *first_state;
    shifts *first_shift;
    reductions *first_reduction;
    Value_t *accessing_symbol;
    core **state_table;
    shifts **shift_table;
    reductions **reduction_table;
    bitword_t *LA;
    Value_t *LAruleno;
    Value_t *lookaheads;
    Value_t *goto_base;
    Value_t *goto_map;
    Value_t *from_state;
    Value_t *to_state;

    action **parser;
    int SRexpect;
    int RRexpect;
    int SRtotal;
    int RRtotal;
    Value_t *SRconflicts;
    Value_t *RRconflicts;
    Value_t *defred;
    Value_t *rules_used;
    Value_t nunused;
    Value_t final_state;

    param *lex_param;
    param *parse_param;

    /*From closure.c*/
    Value_t *itemset;
    Value_t *itemsetend;
    bitword_t *ruleset;
    bitword_t *fs1_first_base;
    bitword_t *fs1_first_derives;
    bitword_t *fs1_EFF;

    /*From graph.c*/
    unsigned int fs2_larno;

    /*From lalr.c*/
    int fs3_tokensetsize;
    Value_t fs3_infinity;
    int fs3_maxrhs;
    Value_t fs3_ngotos;
    bitword_t *fs3_F;
    Value_t **fs3_includes;
    shorts **fs3_lookback;
    Value_t **fs3_R;
    Value_t *fs3_INDEX;
    Value_t *fs3_VERTICES;
    Value_t fs3_top;

    /*From lro.c*/
    core **fs4_state_set;
    core *fs4_this_state;
    core *fs4_last_state;
    shifts *fs4_last_shift;
    reductions *fs4_last_reduction;

    int fs4_nshifts;
    Value_t *fs4_shift_symbol;

    Value_t *fs4_rules;

    Value_t *fs4_redset;
    Value_t *fs4_shiftset;

    Value_t **fs4_kernel_base;
    Value_t **fs4_kernel_end;
    Value_t *fs4_kernel_items;

    /*From mstring.c*/
    char *fs5_buf_ptr;
    size_t fs5_buf_len;

    /*From verbose.c*/
    Value_t *fs6_null_rules;

    /*From symtab.c*/
    bucket **fs7_symbol_table;

    /*From mkpar.c*/
    Value_t fs8_SRcount;
    Value_t fs5_RRcount;

    /*From output.c*/
    int nvectors;
    int nentries;
    Value_t **froms;
    Value_t **tos;
#if defined(YYBTYACC)
    Value_t *conflicts;
    Value_t nconflicts;
#endif
    Value_t *tally;
    Value_t *width;
    Value_t *state_count;
    Value_t *order;
    Value_t *base;
    Value_t *pos;
    int maxtable;
    Value_t *table;
    Value_t *check;
    int lowzero;
    long high;

    /*From reader.c*/
    char *cache;
    int cinc, cache_size;

    int tagmax, havetags;
    char **tag_table;

    char saw_eof;
    size_t linesize; /* length of current input-line */
    SAVE_LINE save_area;
    int must_save;	/* request > 0, triggered < 0, inactive 0 */

    bucket *goal;
    Value_t prec;
    int gensym;
    char last_was_action;
#if defined(YYBTYACC)
    int trialaction;
#endif

    int maxitems;
    bucket **pitem;

    int maxrules;
    bucket **plhs;

    size_t name_pool_size;
    char *name_pool;

    struct code_lines
    {
        const char *name;
        char *lines;
        size_t num;
    } code_lines[CODE_MAX];
    int rescan_lineno;

} byacc_t;

/* global variables */
extern const char *const ygv_banner[];
extern const char *const ygv_xdecls[];
extern const char *const ygv_tables[];
extern const char *const ygv_global_vars[];
extern const char *const ygv_impure_vars[];
extern const char *const ygv_hdr_defs[];
extern const char *const ygv_hdr_vars[];
extern const char *const ygv_body_1[];
extern const char *const ygv_body_vars[];
extern const char *const ygv_init_vars[];
extern const char *const ygv_body_2[];
extern const char *const ygv_body_3[];
extern const char *const ygv_trailer[];
extern const char *ygv_line_format;

/* global functions */

#ifdef HAVE_STDNORETURN_H
#undef GCC_NORETURN
#include <stdnoreturn.h>
#define GCC_NORETURN _Noreturn
#endif

#ifndef GCC_NORETURN
#if defined(_MSC_VER)
#define GCC_NORETURN		__declspec(noreturn)
#else
#define GCC_NORETURN		/* nothing */
#endif
#endif

#if defined(NDEBUG) && defined(_MSC_VER)
#define NODEFAULT   __assume(0);
#else
#define NODEFAULT
#endif
#define NOTREACHED	NODEFAULT

#ifndef GCC_UNUSED
#if defined(__unused)
#define GCC_UNUSED		__unused
#else
#define GCC_UNUSED		/* nothing */
#endif
#endif

#ifndef GCC_PRINTFLIKE
#define GCC_PRINTFLIKE(fmt,var)	/*nothing */
#endif

/* closure.c */
extern void closure(byacc_t* S, Value_t *nucleus, int n);
extern void finalize_closure(byacc_t* S);
extern void set_first_derives(byacc_t* S);

/* error.c */
struct ainfo
{
    int a_lineno;
    char *a_line;
    char *a_cptr;
};

extern void arg_number_disagree_warning(byacc_t* S, int a_lineno, char *a_name);
extern void arg_type_disagree_warning(byacc_t* S, int a_lineno, int i, char *a_name);
extern GCC_NORETURN void at_error(byacc_t* S, int a_lineno, char *a_line, char *a_cptr);
extern void at_warning(byacc_t* S, int a_lineno, int i);
extern GCC_NORETURN void bad_formals(byacc_t* S);
extern void default_action_warning(byacc_t* S, char *s);
extern void destructor_redeclared_warning(byacc_t* S, const struct ainfo *);
extern void dislocations_warning(byacc_t* S);
extern GCC_NORETURN void dollar_error(byacc_t* S, int a_lineno, char *a_line, char *a_cptr);
extern void dollar_warning(byacc_t* S, int a_lineno, int i);
extern GCC_NORETURN void fatal(byacc_t* S, const char *msg);
extern GCC_NORETURN void illegal_character(byacc_t* S, char *c_cptr);
extern GCC_NORETURN void illegal_tag(byacc_t* S, int t_lineno, char *t_line, char *t_cptr);
extern GCC_NORETURN void missing_brace(byacc_t* S);
extern GCC_NORETURN void no_grammar(byacc_t* S);
extern GCC_NORETURN void on_error(byacc_t* S);
extern GCC_NORETURN void open_error(byacc_t* S, const char *filename);
extern GCC_NORETURN void over_unionized(byacc_t* S, char *u_cptr);
extern void prec_redeclared(byacc_t* S);
extern void reprec_warning(byacc_t* S, char *s);
extern void restarted_warning(byacc_t* S);
extern void retyped_warning(byacc_t* S, char *s);
extern void revalued_warning(byacc_t* S, char *s);
extern void start_requires_args(byacc_t* S, char *a_name);
extern GCC_NORETURN void syntax_error(byacc_t* S, int st_lineno, char *st_line, char *st_cptr);
extern GCC_NORETURN void terminal_lhs(byacc_t* S, int s_lineno);
extern GCC_NORETURN void terminal_start(byacc_t* S, char *s);
extern GCC_NORETURN void tokenized_start(byacc_t* S, char *s);
extern GCC_NORETURN void undefined_goal(byacc_t* S, char *s);
extern void undefined_symbol_warning(byacc_t* S, char *s);
extern GCC_NORETURN void unexpected_EOF(byacc_t* S);
extern void unknown_arg_warning(byacc_t* S, int d_lineno,
				const char *dlr_opt,
				const char *d_arg,
				const char *d_line,
				const char *d_cptr);
extern GCC_NORETURN void unknown_rhs(byacc_t* S, int i);
extern void unsupported_flag_warning(byacc_t* S, const char *flag, const char *details);
extern GCC_NORETURN void unexpected_value(byacc_t* S, const struct ainfo *);
extern GCC_NORETURN void unterminated_action(byacc_t* S, const struct ainfo *);
extern GCC_NORETURN void unterminated_comment(byacc_t* S, const struct ainfo *);
extern GCC_NORETURN void unterminated_string(byacc_t* S, const struct ainfo *);
extern GCC_NORETURN void unterminated_text(byacc_t* S, const struct ainfo *);
extern GCC_NORETURN void unterminated_union(byacc_t* S, const struct ainfo *);
extern void untyped_arg_warning(byacc_t* S, int a_lineno, const char *dlr_opt, const char *a_name);
extern GCC_NORETURN void untyped_lhs(byacc_t* S);
extern GCC_NORETURN void untyped_rhs(byacc_t* S, int i, char *s);
extern GCC_NORETURN void used_reserved(byacc_t* S, char *s);
extern GCC_NORETURN void unterminated_arglist(byacc_t* S, const struct ainfo *);
extern void wrong_number_args_warning(byacc_t* S, const char *which, const char *a_name);
extern void wrong_type_for_arg_warning(byacc_t* S, int i, char *a_name);

/* graph.c */
extern void graph(byacc_t* S);

/* lalr.c */
extern void lalr(byacc_t* S);

/* lr0.c */
extern void lr0(byacc_t* S);
extern void show_cores(byacc_t* S);
extern void show_ritems(byacc_t* S);
extern void show_rrhs(byacc_t* S);
extern void show_shifts(byacc_t* S);

/* main.c */
extern void *allocate(byacc_t* S, size_t n);
extern GCC_NORETURN void done(byacc_t* S, int k);

/* mkpar.c */
extern void free_parser(byacc_t* S);
extern void make_parser(byacc_t* S);

/* mstring.c */
struct mstring
{
    char *base, *ptr, *end;
};

extern void msprintf(byacc_t* S, struct mstring *, const char *, ...) GCC_PRINTFLIKE(3,4);
extern int mputchar(struct mstring *, int);
extern struct mstring *msnew(void);
extern struct mstring *msrenew(char *);
extern char *msdone(struct mstring *);
extern int strnscmp(const char *, const char *);
extern unsigned int strnshash(const char *);

#define mputc(m, ch)	(((m)->ptr == (m)->end) \
			 ? mputchar(m,ch) \
			 : (*(m)->ptr++ = (char) (ch)))

/* output.c */
extern void output(byacc_t* S);

/* reader.c */
extern void reader(byacc_t* S);

/* skeleton.c (generated by skel2c) */
extern void write_section(byacc_t* S, FILE * fp, const char *const* section);

/* symtab.c */
extern bucket *make_bucket(byacc_t* S, const char *);
extern bucket *lookup(byacc_t* S, const char *);
extern void create_symbol_table(byacc_t* S);
extern void free_symbol_table(byacc_t* S);
extern void free_symbols(byacc_t* S);

/* verbose.c */
extern void verbose(byacc_t* S);

/* warshall.c */
extern void reflexive_transitive_closure(bitword_t *R, int n);

#ifdef DEBUG
    /* closure.c */
extern void print_closure(byacc_t* S, int n);
extern void print_EFF(byacc_t* S);
extern void print_first_derives(byacc_t* S);
    /* lr0.c */
extern void print_derives(byacc_t* S);
#endif

#ifdef NO_LEAKS
extern void lr0_leaks(byacc_t* S);
extern void lalr_leaks(byacc_t* S);
extern void mkpar_leaks(byacc_t* S);
extern void output_leaks(byacc_t* S);
extern void mstring_leaks(byacc_t* S);
extern void reader_leaks(byacc_t* S);
#endif
