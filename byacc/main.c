/* $Id: main.c,v 1.74 2023/05/11 07:51:36 tom Exp $ */

#include <signal.h>
#if !defined(_WIN32) || defined(__MINGW32__)
#include <unistd.h>		/* for _exit() */
#else
#include <stdlib.h>		/* for _exit() */
#endif

#include "defs.h"

#ifdef HAVE_MKSTEMP
# define USE_MKSTEMP 1
#elif defined(HAVE_FCNTL_H)
# define USE_MKSTEMP 1
# include <fcntl.h>		/* for open(), O_EXCL, etc. */
#else
# define USE_MKSTEMP 0
#endif

#if USE_MKSTEMP
#include <sys/types.h>
#include <sys/stat.h>

typedef struct _my_tmpfiles
{
    struct _my_tmpfiles *next;
    char *name;
}
MY_TMPFILES;

static MY_TMPFILES *my_tmpfiles;
#endif /* USE_MKSTEMP */

static char oflag;

/*
 * Since fclose() is called via the signal handler, it might die.  Don't loop
 * if there is a problem closing a file.
 */
#define DO_CLOSE(fp) \
	if (fp != 0) { \
	    FILE *use = fp; \
	    fp = 0; \
	    fclose(use); \
	}

static int got_intr = 0;

void
done(byacc_t* S, int k)
{
    DO_CLOSE(S->input_file);
    DO_CLOSE(S->output_file);
    DO_CLOSE(S->ebnf_file);
    DO_CLOSE(S->lemon_file);
    DO_CLOSE(S->naked_file);
    if (S->iflag)
	DO_CLOSE(S->externs_file);
    if (S->rflag)
	DO_CLOSE(S->code_file);

    DO_CLOSE(S->action_file);
    DO_CLOSE(S->defines_file);
    DO_CLOSE(S->graph_file);
    DO_CLOSE(S->text_file);
    DO_CLOSE(S->union_file);
    DO_CLOSE(S->verbose_file);

    if (got_intr)
	_exit(EXIT_FAILURE);

#ifdef NO_LEAKS
    DO_FREE(S->input_file_name);

    if (S->rflag)
	DO_FREE(S->code_file_name);

    if (S->dflag && !S->dflag2)
	DO_FREE(S->defines_file_name);

    if (S->iflag)
	DO_FREE(S->externs_file_name);

    if (oflag)
	DO_FREE(S->output_file_name);

    if (S->vflag)
	DO_FREE(S->verbose_file_name);

    if (S->ebnf_flag)
	DO_FREE(S->ebnf_file_name);

    if (S->lemon_flag)
	DO_FREE(S->lemon_file_name);

    if (S->naked_flag)
	DO_FREE(S->naked_file_name);

    if (S->nakedq_flag)
	DO_FREE(S->nakedq_file_name);

    if (S->unicc_flag)
	DO_FREE(S->unicc_file_name);

    if (S->carburetta_flag)
	DO_FREE(S->carburetta_file_name);

    if (S->sql_flag)
	DO_FREE(S->sql_file_name);

    if (S->gflag)
	DO_FREE(S->graph_file_name);

    lr0_leaks(S);
    lalr_leaks(S);
    mkpar_leaks(S);
    mstring_leaks(S);
    output_leaks(S);
    reader_leaks(S);
#endif

    exit(k);
}

static void
onintr(int sig GCC_UNUSED)
{
    got_intr = 1;
    //done(S, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

static void
set_signals(void)
{
#ifdef SIGINT
    if (signal(SIGINT, SIG_IGN) != SIG_IGN)
	signal(SIGINT, onintr);
#endif
#ifdef SIGTERM
    if (signal(SIGTERM, SIG_IGN) != SIG_IGN)
	signal(SIGTERM, onintr);
#endif
#ifdef SIGHUP
    if (signal(SIGHUP, SIG_IGN) != SIG_IGN)
	signal(SIGHUP, onintr);
#endif
}

#define SIZEOF(v) (sizeof(v) / sizeof((v)[0]))

/*
 * Long options are provided only as a compatibility aid for scripters.
 */
/* *INDENT-OFF* */
static const struct {
    const char long_opt[16];
    const char yacc_arg;
    const char yacc_opt;
} long_opts[] = {
    { "defines",     1, 'H' },
    { "file-prefix", 1, 'b' },
    { "carburetta",  0, 'c' },
    { "graph",       0, 'g' },
    { "ebnf",        0, 'e' },
    { "lemon",       0, 'E' },
    { "help",        0, 'h' },
    { "naked",       0, 'n' },
    { "nakedq",      0, 'N' },
    { "sql",         0, 'S' },
    { "name-prefix", 1, 'p' },
    { "no-lines",    0, 'l' },
    { "output",      1, 'o' },
    { "version",     0, 'V' }
};
/* *INDENT-ON* */

/*
 * Usage-message is designed for 80 columns, with some unknowns.  Account for
 * those in the maximum width so that the usage message uses no relocatable
 * pointers.
 */
#define USAGE_COLS (80 + sizeof(DEFINES_SUFFIX) + sizeof(OUTPUT_SUFFIX))

static void
usage(byacc_t* S)
{
    /* *INDENT-OFF* */
    static const char msg[][USAGE_COLS] =
    {
	{ "  -b file_prefix        set filename prefix (default \"y.\")" },
	{ "  -B                    create a backtracking parser" },
	{ "  -c                    write carburetta grammar" },
	{ "  -C                    write unicc grammar" },
	{ "  -d                    write definitions (" DEFINES_SUFFIX ")" },
	{ "  -e                    write ebnf grammar" },
	{ "  -E                    write lemon grammar" },
	{ "  -h                    print this help-message" },
	{ "  -H defines_file       write definitions to defines_file" },
	{ "  -i                    write interface (y.tab.i)" },
	{ "  -g                    write a graphical description" },
	{ "  -l                    suppress #line directives" },
	{ "  -L                    enable position processing, e.g., \"%locations\"" },
	{ "  -n                    write naked grammar" },
	{ "  -N                    write naked quoted grammar" },
	{ "  -o output_file        (default \"" OUTPUT_SUFFIX "\")" },
	{ "  -p symbol_prefix      set symbol prefix (default \"yy\")" },
	{ "  -P                    create a reentrant parser, e.g., \"%pure-parser\"" },
	{ "  -r                    produce separate code and table files (y.code.c)" },
	{ "  -s                    suppress #define's for quoted names in %token lines" },
	{ "  -S                    write grammar as sql" },
	{ "  -t                    add debugging support" },
	{ "  -v                    write description (y.output)" },
	{ "  -V                    show version information and exit" },
	{ "  -u                    ignore precedences" },
	{ "  -z                    use leftmost token for rule precedence" },
    };
    /* *INDENT-ON* */
    unsigned n;

    fflush(stdout);
    fprintf(stderr, "Usage: %s [options] filename\n", S->myname);

    fprintf(stderr, "\nOptions:\n");
    for (n = 0; n < SIZEOF(msg); ++n)
    {
	fprintf(stderr, "%s\n", msg[n]);
    }

    fprintf(stderr, "\nLong options:\n");
    for (n = 0; n < SIZEOF(long_opts); ++n)
    {
	fprintf(stderr, "  --%-20s-%c\n",
		long_opts[n].long_opt,
		long_opts[n].yacc_opt);
    }

    exit(EXIT_FAILURE);
}

static void
invalid_option(const char *option, byacc_t* S)
{
    fprintf(stderr, "invalid option: %s\n", option);
    usage(S);
}

static void
setflag(byacc_t* S, int ch)
{
    switch (ch)
    {
    case 'B':
#if defined(YYBTYACC)
	S->backtrack = 1;
#else
	unsupported_flag_warning(S, "-B", "reconfigure with --enable-btyacc");
#endif
	break;

    case 'c':
	S->carburetta_flag = 1;
	break;

    case 'C':
	S->unicc_flag = 1;
	break;

    case 'd':
	S->dflag = 1;
	S->dflag2 = 0;
	break;

    case 'e':
	S->ebnf_flag = 1;
	break;

    case 'E':
	S->lemon_flag = 1;
	break;

    case 'g':
	S->gflag = 1;
	break;

    case 'i':
	S->iflag = 1;
	break;

    case 'l':
	S->lflag = 1;
	break;

    case 'L':
#if defined(YYBTYACC)
	S->locations = 1;
#else
	unsupported_flag_warning(S, "-L", "reconfigure with --enable-btyacc");
#endif
	break;

    case 'n':
	S->naked_flag = 1;
	break;

    case 'N':
	S->nakedq_flag = 1;
	break;

    case 'S':
	S->sql_flag = 1;
	break;

    case 'P':
	S->pure_parser = 1;
	break;

    case 'r':
	S->rflag = 1;
	break;

    case 's':
	S->sflag = 1;
	break;

    case 't':
	S->tflag = 1;
	break;

    case 'v':
	S->vflag = 1;
	break;

    case 'V':
	printf("%s - %s\n", S->myname, VERSION);
	exit(EXIT_SUCCESS);

    case 'y':
	/* noop for bison compatibility. byacc is already designed to be posix
	 * yacc compatible. */
	break;

    case 'u':
        S->ignore_prec_flag = 1;
	break;

    case 'z':
        S->lemon_prec_flag = 1;
	break;

    default:
	usage(S);
    }
}

static void
getargs(byacc_t* S, int argc, char *argv[])
{
    int i;
#ifdef HAVE_GETOPT
    int ch;
#endif

    /*
     * Map bison's long-options into yacc short options.
     */
    for (i = 1; i < argc; ++i)
    {
	char *a = argv[i];

	if (!strncmp(a, "--", 2))
	{
	    char *eqls;
	    size_t lc;
	    size_t len;

	    if ((len = strlen(a)) == 2)
		break;

	    if ((eqls = strchr(a, '=')) != NULL)
	    {
		len = (size_t)(eqls - a);
		if (len == 0 || eqls[1] == '\0')
		    invalid_option(a, S);
	    }

	    for (lc = 0; lc < SIZEOF(long_opts); ++lc)
	    {
		if (!strncmp(long_opts[lc].long_opt, a + 2, len - 2))
		{
		    if (eqls != NULL && !long_opts[lc].yacc_arg)
			invalid_option(a, S);
		    *a++ = '-';
		    *a++ = long_opts[lc].yacc_opt;
		    *a = '\0';
		    if (eqls)
		    {
			while ((*a++ = *++eqls) != '\0') /* empty */ ;
		    }
		    break;
		}
	    }
	    if (!strncmp(a, "--", 2))
		invalid_option(a, S);
	}
    }

#ifdef HAVE_GETOPT
    if (argc > 0)
	S->myname = argv[0];

    while ((ch = getopt(argc, argv, "Bb:cCdEeghH:ilLnNo:Pp:rsStVvyuz")) != -1)
    {
	switch (ch)
	{
	case 'b':
	    S->file_prefix = optarg;
	    break;
	case 'h':
	    usage(S);
	    break;
	case 'H':
	    S->dflag = S->dflag2 = 1;
	    S->defines_file_name = optarg;
	    break;
	case 'o':
	    S->output_file_name = optarg;
	    break;
	case 'p':
	    S->symbol_prefix = optarg;
	    break;
	default:
	    setflag(S, ch);
	    break;
	}
    }
    if ((i = optind) < argc)
    {
	/* getopt handles "--" specially, while we handle "-" specially */
	if (!strcmp(argv[i], "-"))
	{
	    if ((i + 1) < argc)
		usage(S);
	    S->input_file = stdin;
	    return;
	}
    }
#else
    char *s;
    int ch;

    if (argc > 0)
	myname = argv[0];

    for (i = 1; i < argc; ++i)
    {
	s = argv[i];
	if (*s != '-')
	    break;
	switch (ch = *++s)
	{
	case '\0':
	    input_file = stdin;
	    if (i + 1 < argc)
		usage(S);
	    return;

	case '-':
	    ++i;
	    goto no_more_options;

	case 'b':
	    if (*++s)
		S->file_prefix = s;
	    else if (++i < argc)
		S->file_prefix = argv[i];
	    else
		usage(S);
	    continue;

	case 'H':
	    dflag = dflag2 = 1;
	    if (*++s)
		defines_file_name = s;
	    else if (++i < argc)
		defines_file_name = argv[i];
	    else
		usage(S);
	    continue;

	case 'o':
	    if (*++s)
		output_file_name = s;
	    else if (++i < argc)
		output_file_name = argv[i];
	    else
		usage(S);
	    continue;

	case 'p':
	    if (*++s)
		symbol_prefix = s;
	    else if (++i < argc)
		symbol_prefix = argv[i];
	    else
		usage(S);
	    continue;

	default:
	    setflag(S, ch);
	    break;
	}

	for (;;)
	{
	    switch (ch = *++s)
	    {
	    case '\0':
		goto end_of_option;

	    default:
		setflag(S, ch);
		break;
	    }
	}
      end_of_option:;
    }

  no_more_options:

#endif /* HAVE_GETOPT */
    if (i + 1 != argc)
	usage(S);
    S->input_file_name_len = strlen(argv[i]);
    S->input_file_name = TMALLOC(char, S->input_file_name_len + 1);
    NO_SPACE(S->input_file_name);
    strcpy(S->input_file_name, argv[i]);
}

void *
allocate(byacc_t* S, size_t n)
{
    void *p;

    p = NULL;
    if (n)
    {
	p = CALLOC(1, n);
	NO_SPACE(p);
    }
    return (p);
}

#define CREATE_FILE_NAME(dest, suffix) \
	dest = alloc_file_name(S, len, suffix)

static char *
alloc_file_name(byacc_t* S, size_t len, const char *suffix)
{
    char *result = TMALLOC(char, len + strlen(suffix) + 1);
    if (result == NULL)
	on_error(S);
    strcpy(result, S->file_prefix);
    strcpy(result + len, suffix);
    return result;
}

static char *
find_suffix(char *name, const char *suffix)
{
    size_t len = strlen(name);
    size_t slen = strlen(suffix);
    if (len >= slen)
    {
	name += len - slen;
	if (strcmp(name, suffix) == 0)
	    return name;
    }
    return NULL;
}

static void
create_file_names(byacc_t* S)
{
    size_t len;
    const char *defines_suffix;
    const char *externs_suffix;
    char *suffix;

    suffix = NULL;
    defines_suffix = DEFINES_SUFFIX;
    externs_suffix = EXTERNS_SUFFIX;

    /* compute the file_prefix from the user provided output_file_name */
    if (S->output_file_name != 0)
    {
	if (!(suffix = find_suffix(S->output_file_name, OUTPUT_SUFFIX))
	    && (suffix = find_suffix(S->output_file_name, ".c")))
	{
	    defines_suffix = ".h";
	    externs_suffix = ".i";
	}
    }

    if (suffix != NULL)
    {
	len = (size_t)(suffix - S->output_file_name);
	S->file_prefix = TMALLOC(char, len + 1);
	NO_SPACE(S->file_prefix);
	strncpy(S->file_prefix, S->output_file_name, len)[len] = 0;
    }
    else
	len = strlen(S->file_prefix);

    /* if "-o filename" was not given */
    if (S->output_file_name == 0)
    {
	oflag = 1;
	CREATE_FILE_NAME(S->output_file_name, OUTPUT_SUFFIX);
    }

    if (S->rflag)
    {
	CREATE_FILE_NAME(S->code_file_name, CODE_SUFFIX);
    }
    else
	S->code_file_name = S->output_file_name;

    if (S->dflag && !S->dflag2)
    {
	CREATE_FILE_NAME(S->defines_file_name, defines_suffix);
    }

    if (S->iflag)
    {
	CREATE_FILE_NAME(S->externs_file_name, externs_suffix);
    }

    if (S->vflag)
    {
	CREATE_FILE_NAME(S->verbose_file_name, VERBOSE_SUFFIX);
    }

    if (S->gflag)
    {
	CREATE_FILE_NAME(S->graph_file_name, GRAPH_SUFFIX);
    }

    if (S->ebnf_flag)
    {
	CREATE_FILE_NAME(S->ebnf_file_name, EBNF_SUFFIX);
    }

    if (S->lemon_flag)
    {
	CREATE_FILE_NAME(S->lemon_file_name, LEMON_SUFFIX);
    }

    if (S->naked_flag)
    {
	CREATE_FILE_NAME(S->naked_file_name, NAKED_SUFFIX);
    }

    if (S->nakedq_flag)
    {
	CREATE_FILE_NAME(S->nakedq_file_name, NAKED_SUFFIX);
    }

    if (S->unicc_flag)
    {
	CREATE_FILE_NAME(S->unicc_file_name, UNICC_SUFFIX);
    }

    if (S->carburetta_flag)
    {
	CREATE_FILE_NAME(S->carburetta_file_name, CARBURETTA_SUFFIX);
    }

    if (S->sql_flag)
    {
	CREATE_FILE_NAME(S->sql_file_name, SQL_SUFFIX);
    }

    if (suffix != NULL)
    {
	FREE(S->file_prefix);
    }
}

#if USE_MKSTEMP
static void
close_tmpfiles(void)
{
    while (my_tmpfiles != 0)
    {
	MY_TMPFILES *next = my_tmpfiles->next;

	(void)chmod(my_tmpfiles->name, 0644);
	(void)unlink(my_tmpfiles->name);

	free(my_tmpfiles->name);
	free(my_tmpfiles);

	my_tmpfiles = next;
    }
}

#ifndef HAVE_MKSTEMP
static int
my_mkstemp(char *temp)
{
    int fd;
    char *dname;
    char *fname;
    char *name;

    /*
     * Split-up to use tempnam, rather than tmpnam; the latter (like
     * mkstemp) is unusable on Windows.
     */
    if ((fname = strrchr(temp, '/')) != 0)
    {
	dname = strdup(temp);
	dname[++fname - temp] = '\0';
    }
    else
    {
	dname = 0;
	fname = temp;
    }
    if ((name = tempnam(dname, fname)) != 0)
    {
	fd = open(name, O_CREAT | O_EXCL | O_RDWR);
	strcpy(temp, name);
    }
    else
    {
	fd = -1;
    }

    if (dname != 0)
	free(dname);

    return fd;
}
#define mkstemp(s) my_mkstemp(s)
#endif

#endif

/*
 * tmpfile() should be adequate, except that it may require special privileges
 * to use, e.g., MinGW and Windows 7 where it tries to use the root directory.
 */
static FILE *
open_tmpfile(byacc_t* S, const char *label)
{
#define MY_FMT "%s/%.*sXXXXXX"
    FILE *result;
#if USE_MKSTEMP
    const char *tmpdir;
    char *name;

    if (((tmpdir = getenv("TMPDIR")) == 0 || access(tmpdir, W_OK) != 0) ||
	((tmpdir = getenv("TEMP")) == 0 || access(tmpdir, W_OK) != 0))
    {
#ifdef P_tmpdir
	tmpdir = P_tmpdir;
#else
	tmpdir = "/tmp";
#endif
	if (access(tmpdir, W_OK) != 0)
	    tmpdir = ".";
    }

    /* The size of the format is guaranteed to be longer than the result from
     * printing empty strings with it; this calculation accounts for the
     * string-lengths as well.
     */
    name = malloc(strlen(tmpdir) + sizeof(MY_FMT) + strlen(label));

    result = 0;
    if (name != 0)
    {
	int fd;
	const char *mark;

	mode_t save_umask = umask(0177);

	if ((mark = strrchr(label, '_')) == 0)
	    mark = label + strlen(label);

	sprintf(name, MY_FMT, tmpdir, (int)(mark - label), label);
	fd = mkstemp(name);
	if (fd >= 0
	    && (result = fdopen(fd, "w+")) != 0)
	{
	    MY_TMPFILES *item;

	    if (my_tmpfiles == 0)
	    {
		atexit(close_tmpfiles);
	    }

	    item = NEW(MY_TMPFILES);
	    NO_SPACE(item);

	    item->name = name;
	    NO_SPACE(item->name);

	    item->next = my_tmpfiles;
	    my_tmpfiles = item;
	}
	else
	{
	    FREE(name);
	}
	(void)umask(save_umask);
    }
#else
    result = tmpfile();
#endif

    if (result == 0)
	open_error(S, label);
    return result;
#undef MY_FMT
}

static void
open_files(byacc_t* S)
{
    create_file_names(S);

    if (S->input_file == 0)
    {
	S->input_file = fopen(S->input_file_name, "r");
	if (S->input_file == 0)
	    open_error(S, S->input_file_name);
    }

    S->action_file = open_tmpfile(S, "action_file");
    S->text_file = open_tmpfile(S, "text_file");

    if (S->vflag)
    {
	S->verbose_file = fopen(S->verbose_file_name, "w");
	if (S->verbose_file == 0)
	    open_error(S, S->verbose_file_name);
    }

    if (S->gflag)
    {
	S->graph_file = fopen(S->graph_file_name, "w");
	if (S->graph_file == 0)
	    open_error(S, S->graph_file_name);
	fprintf(S->graph_file, "digraph %s {\n", S->file_prefix);
	fprintf(S->graph_file, "\tedge [fontsize=10];\n");
	fprintf(S->graph_file, "\tnode [shape=box,fontsize=10];\n");
	fprintf(S->graph_file, "\torientation=landscape;\n");
	fprintf(S->graph_file, "\trankdir=LR;\n");
	fprintf(S->graph_file, "\t/*\n");
	fprintf(S->graph_file, "\tmargin=0.2;\n");
	fprintf(S->graph_file, "\tpage=\"8.27,11.69\"; // for A4 printing\n");
	fprintf(S->graph_file, "\tratio=auto;\n");
	fprintf(S->graph_file, "\t*/\n");
    }

    if (S->ebnf_flag)
    {
	S->ebnf_file = fopen(S->ebnf_file_name, "w");
	if (S->ebnf_file == 0)
	    open_error(S, S->ebnf_file_name);
    }

    if (S->lemon_flag)
    {
	S->lemon_file = fopen(S->lemon_file_name, "w");
	if (S->lemon_file == 0)
	    open_error(S, S->lemon_file_name);
    }

    if (S->naked_flag)
    {
	S->naked_file = fopen(S->naked_file_name, "w");
	if (S->naked_file == 0)
	    open_error(S, S->naked_file_name);
    }

    if (S->nakedq_flag)
    {
	S->nakedq_file = fopen(S->nakedq_file_name, "w");
	if (S->nakedq_file == 0)
	    open_error(S, S->nakedq_file_name);
    }

    if (S->unicc_flag)
    {
	S->unicc_file = fopen(S->unicc_file_name, "w");
	if (S->unicc_file == 0)
	    open_error(S, S->unicc_file_name);
    }

    if (S->carburetta_flag)
    {
	S->carburetta_file = fopen(S->carburetta_file_name, "w");
	if (S->carburetta_file == 0)
	    open_error(S, S->carburetta_file_name);
    }

    if (S->sql_flag)
    {
	S->sql_file = fopen(S->sql_file_name, "w");
	if (S->sql_file == 0)
	    open_error(S, S->sql_file_name);
    }

    if (S->dflag || S->dflag2)
    {
	S->defines_file = fopen(S->defines_file_name, "w");
	if (S->defines_file == 0)
	    open_error(S, S->defines_file_name);
	S->union_file = open_tmpfile(S, "union_file");
    }

    if (S->iflag)
    {
	S->externs_file = fopen(S->externs_file_name, "w");
	if (S->externs_file == 0)
	    open_error(S, S->externs_file_name);
    }

    S->output_file = fopen(S->output_file_name, "w");
    if (S->output_file == 0)
	open_error(S, S->output_file_name);

    if (S->rflag)
    {
	S->code_file = fopen(S->code_file_name, "w");
	if (S->code_file == 0)
	    open_error(S, S->code_file_name);
    }
    else
	S->code_file = S->output_file;
}

int
main(int argc, char *argv[])
{
    byacc_t S;
    memset(&S, 0, sizeof(byacc_t));
    S.SRexpect = -1;
    S.RRexpect = -1;
    S.myname = "yacc";
    S.file_prefix = "y";
    S.exit_code = EXIT_SUCCESS;
    S.line_format = ygv_line_format;

    set_signals();
    getargs(&S, argc, argv);
    open_files(&S);
    reader(&S);
    lr0(&S);
    lalr(&S);
    make_parser(&S);
    graph(&S);
    finalize_closure(&S);
    verbose(&S);
    output(&S);
    done(&S, S.exit_code);
    /*NOTREACHED */
}
