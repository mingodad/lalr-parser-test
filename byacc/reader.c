/* $Id: reader.c,v 1.90 2021/08/08 22:11:04 tom Exp $ */

#include "defs.h"

/*  The line size must be a positive integer.  One hundred was chosen	*/
/*  because few lines in Yacc input grammars exceed 100 characters.	*/
/*  Note that if a line exceeds LINESIZE characters, the line buffer	*/
/*  will be expanded to accommodate it.					*/

#define LINESIZE 100

#define L_CURL  '{'
#define R_CURL  '}'
#define L_PAREN '('
#define R_PAREN ')'
#define L_BRAC  '['
#define R_BRAC  ']'

/* the maximum number of arguments (inherited attributes) to a non-terminal */
/* this is a hard limit, but seems more than adequate */
#define MAXARGS	20

#define begin_case(f,n) fprintf(f, "case %d:\n", (int)(n))

#define end_case(f) \
	    fprintf(f, "\n"); \
	    fprintf_lineno(f, 1, ""); \
	    fprintf(f, "break;\n")

static void start_rule(bucket *bp, int s_lineno);
#if defined(YYBTYACC)
static void copy_initial_action(void);
static void copy_destructor(void);
static char *process_destructor_XX(char *code, char *tag);
#endif

#define CACHE_SIZE 256
static char *cache;
static int cinc, cache_size;

int ntags;
static int tagmax, havetags;
static char **tag_table;

static char saw_eof;
char unionized;
char *cptr, *line;
static int linesize;

static bucket *goal;
static Value_t prec;
static int gensym;
static char last_was_action;
#if defined(YYBTYACC)
static int trialaction;
#endif

static int maxitems;
static bucket **pitem;

static int maxrules;
static bucket **plhs;

static size_t name_pool_size;
static char *name_pool;

char line_format[] = "#line %d \"%s\"\n";

param *lex_param;
param *parse_param;

static const char *code_keys[] =
{
    "", "requires", "provides", "top", "imports",
};

struct code_lines code_lines[CODE_MAX];

#if defined(YYBTYACC)
int destructor = 0;	/* =1 if at least one %destructor */

static bucket *default_destructor[3] =
{0, 0, 0};

#define UNTYPED_DEFAULT 0
#define TYPED_DEFAULT   1
#define TYPE_SPECIFIED  2

static bucket *
lookup_type_destructor(char *tag)
{
    const char fmt[] = "%.*s destructor";
    char name[1024] = "\0";
    bucket *bp, **bpp = &default_destructor[TYPE_SPECIFIED];

    while ((bp = *bpp) != NULL)
    {
	if (bp->tag == tag)
	    return (bp);
	bpp = &bp->link;
    }

    sprintf(name, fmt, (int)(sizeof(name) - sizeof(fmt)), tag);
    *bpp = bp = make_bucket(name);
    bp->tag = tag;

    return (bp);
}
#endif /* defined(YYBTYACC) */

static void
cachec(int c)
{
    assert(cinc >= 0);
    if (cinc >= cache_size)
    {
	cache_size += CACHE_SIZE;
	cache = TREALLOC(char, cache, cache_size);
	NO_SPACE(cache);
    }
    cache[cinc] = (char)c;
    ++cinc;
}

typedef enum
{
    ldSPC1,
    ldSPC2,
    ldNAME,
    ldSPC3,
    ldNUM,
    ldSPC4,
    ldFILE,
    ldOK,
    ldERR
}
LINE_DIR;

/*
 * Expect this pattern:
 *	/^[[:space:]]*#[[:space:]]*
 *	  line[[:space:]]+
 *	  [[:digit:]]+
 *	  ([[:space:]]*|[[:space:]]+"[^"]+")/
 */
static int
line_directive(void)
{
#define UNLESS(what) if (what) { ld = ldERR; break; }
    int n;
    int line_1st = -1;
    int name_1st = -1;
    int name_end = -1;
    LINE_DIR ld = ldSPC1;
    for (n = 0; (ld <= ldOK) && (line[n] != '\0'); ++n)
    {
	int ch = UCH(line[n]);
	switch (ld)
	{
	case ldSPC1:
	    if (isspace(UCH(ch)))
	    {
		break;
	    }
	    else
		UNLESS(ch != '#');
	    ld = ldSPC2;
	    break;
	case ldSPC2:
	    if (isspace(UCH(ch)))
	    {
		break;
	    }
	    /* FALLTHRU */
	case ldNAME:
	    UNLESS(strncmp(line + n, "line", 4));
	    n += 4;
	    if (line[n] == '\0')
	    {
		ld = ldOK;
		break;
	    }
	    else
		UNLESS(!isspace(UCH(line[n])));
	    ld = ldSPC3;
	    break;
	case ldSPC3:
	    if (isspace(UCH(ch)))
	    {
		break;
	    }
	    else
		UNLESS(!isdigit(UCH(ch)));
	    line_1st = n;
	    ld = ldNUM;		/* this is needed, but cppcheck says no... */
	    /* FALLTHRU */
	case ldNUM:
	    if (isdigit(UCH(ch)))
	    {
		break;
	    }
	    else
		UNLESS(!isspace(UCH(ch)));
	    ld = ldSPC4;
	    break;
	case ldSPC4:
	    if (isspace(UCH(ch)))
	    {
		break;
	    }
	    else
		UNLESS(ch != '"');
	    UNLESS(line[n + 1] == '"');
	    ld = ldFILE;
	    name_1st = n;
	    break;
	case ldFILE:
	    if (ch != '"')
	    {
		break;
	    }
	    ld = ldOK;
	    name_end = n;
	    /* FALLTHRU */
	case ldERR:
	case ldOK:
	    break;
	}
    }

    if (ld == ldOK)
    {
	size_t need = (size_t)(name_end - name_1st);
	if ((long)need > (long)input_file_name_len)
	{
	    input_file_name_len = ((need + 1) * 3) / 2;
	    input_file_name = TREALLOC(char, input_file_name, input_file_name_len);
	    NO_SPACE(input_file_name);
	}
	if ((long)need > 0)
	{
	    memcpy(input_file_name, line + name_1st + 1, need - 1);
	    input_file_name[need - 1] = '\0';
	}
	else
	{
	    input_file_name[0] = '\0';
	}
    }

    if (ld >= ldNUM && ld < ldERR)
    {
	if (line_1st >= 0)
	{
	    lineno = (int)strtol(line + line_1st, NULL, 10) - 1;
	}
	else
	{
	    lineno = 0;
	}
    }

    return (ld == ldOK);
#undef UNLESS
}

static void
get_line(void)
{
    FILE *f = input_file;

    do
    {
	int c;
	int i;

	if (saw_eof || (c = getc(f)) == EOF)
	{
	    if (line)
	    {
		FREE(line);
		line = 0;
	    }
	    cptr = 0;
	    saw_eof = 1;
	    return;
	}

	if (line == NULL || linesize != (LINESIZE + 1))
	{
	    if (line)
		FREE(line);
	    linesize = LINESIZE + 1;
	    line = TMALLOC(char, linesize);
	    NO_SPACE(line);
	}

	i = 0;
	++lineno;
	for (;;)
	{
	    line[i++] = (char)c;
	    if (c == '\n')
		break;
	    if ((i + 3) >= linesize)
	    {
		linesize += LINESIZE;
		line = TREALLOC(char, line, linesize);
		NO_SPACE(line);
	    }
	    c = getc(f);
	    if (c == EOF)
	    {
		line[i++] = '\n';
		saw_eof = 1;
		break;
	    }
	}
	line[i] = '\0';
    }
    while (line_directive());
    cptr = line;
    return;
}

static char *
dup_line(void)
{
    char *p, *s, *t;

    if (line == NULL)
	return (NULL);
    s = line;
    while (*s != '\n')
	++s;
    p = TMALLOC(char, s - line + 1);
    NO_SPACE(p);

    s = line;
    t = p;
    while ((*t++ = *s++) != '\n')
	continue;
    return (p);
}

static void
skip_comment(void)
{
    char *s;
    struct ainfo a;
    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line);

    s = cptr + 2;
    for (;;)
    {
	if (*s == '*' && s[1] == '/')
	{
	    cptr = s + 2;
	    FREE(a.a_line);
	    return;
	}
	if (*s == '\n')
	{
	    get_line();
	    if (line == NULL)
		unterminated_comment(&a);
	    s = cptr;
	}
	else
	    ++s;
    }
}

static int
next_inline(void)
{
    char *s;

    if (line == NULL)
    {
	get_line();
	if (line == NULL)
	    return (EOF);
    }

    s = cptr;
    for (;;)
    {
	switch (*s)
	{
	case '/':
	    if (s[1] == '*')
	    {
		cptr = s;
		skip_comment();
		s = cptr;
		break;
	    }
	    else if (s[1] == '/')
	    {
		get_line();
		if (line == NULL)
		    return (EOF);
		s = cptr;
		break;
	    }
	    /* FALLTHRU */

	default:
	    cptr = s;
	    return (*s);
	}
    }
}

static int
nextc(void)
{
    int ch;
    int finish = 0;

    do
    {
	switch (ch = next_inline())
	{
	case '\n':
	    get_line();
	    break;
	case ' ':
	case '\t':
	case '\f':
	case '\r':
	case '\v':
	case ',':
	case ';':
	    ++cptr;
	    break;
	case '\\':
	    ch = '%';
	    /* FALLTHRU */
	default:
	    finish = 1;
	    break;
	}
    }
    while (!finish);

    return ch;
}
/* *INDENT-OFF* */
static struct keyword
{
    char name[16];
    int token;
}
keywords[] = {
    { "binary",      NONASSOC },
    { "code",        XCODE },
    { "debug",       NONPOSIX_DEBUG },
    { "define",      SKIP_DEFINE },
#if defined(YYBTYACC)
    { "destructor",  DESTRUCTOR },
#endif
    { "error-verbose",ERROR_VERBOSE },
    { "expect",      EXPECT },
    { "expect-rr",   EXPECT_RR },
    { "ident",       IDENT },
#if defined(YYBTYACC)
    { "initial-action", INITIAL_ACTION },
#endif
    { "left",        LEFT },
    { "lex-param",   LEX_PARAM },
#if defined(YYBTYACC)
    { "locations",   LOCATIONS },
#endif
    { "nonassoc",    NONASSOC },
    { "parse-param", PARSE_PARAM },
    { "precedence",  PRECEDENCE },
    { "pure-parser", PURE_PARSER },
    { "right",       RIGHT },
    { "start",       START },
    { "term",        TOKEN },
    { "token",       TOKEN },
    { "token-table", TOKEN_TABLE },
    { "type",        TYPE },
    { "union",       UNION },
    { "yacc",        POSIX_YACC },
};
/* *INDENT-ON* */

static int
compare_keys(const void *a, const void *b)
{
    const struct keyword *p = (const struct keyword *)a;
    const struct keyword *q = (const struct keyword *)b;
    return strcmp(p->name, q->name);
}

static int
keyword(void)
{
    int c;
    char *t_cptr = cptr;

    c = *++cptr;
    if (isalpha(UCH(c)))
    {
	struct keyword *key;

	cinc = 0;
	for (;;)
	{
	    if (isalpha(UCH(c)))
	    {
		if (isupper(UCH(c)))
		    c = tolower(c);
		cachec(c);
	    }
	    else if (isdigit(UCH(c))
		     || c == '-'
		     || c == '.'
		     || c == '$')
	    {
		cachec(c);
	    }
	    else if (c == '_')
	    {
		/* treat keywords spelled with '_' as if it were '-' */
		cachec('-');
	    }
	    else
	    {
		break;
	    }
	    c = *++cptr;
	}
	cachec(NUL);

	if ((key = bsearch(cache, keywords,
			   sizeof(keywords) / sizeof(*key),
			   sizeof(*key), compare_keys)))
	    return key->token;
    }
    else
    {
	++cptr;
	if (c == L_CURL)
	    return (TEXT);
	if (c == '%' || c == '\\')
	    return (MARK);
	if (c == '<')
	    return (LEFT);
	if (c == '>')
	    return (RIGHT);
	if (c == '0')
	    return (TOKEN);
	if (c == '2')
	    return (NONASSOC);
    }
    syntax_error(lineno, line, t_cptr);
    /*NOTREACHED */
}

static void
copy_ident(void)
{
    int c;
    FILE *f = output_file;

    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (c != '"')
	syntax_error(lineno, line, cptr);
    ++outline;
    fprintf(f, "#ident \"");
    for (;;)
    {
	c = *++cptr;
	if (c == '\n')
	{
	    fprintf(f, "\"\n");
	    return;
	}
	putc(c, f);
	if (c == '"')
	{
	    putc('\n', f);
	    ++cptr;
	    return;
	}
    }
}

static char *
copy_string(int quote)
{
    struct mstring *temp = msnew();
    struct ainfo a;
    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line - 1);

    for (;;)
    {
	int c = *cptr++;

	mputc(temp, c);
	if (c == quote)
	{
	    FREE(a.a_line);
	    return msdone(temp);
	}
	if (c == '\n')
	    unterminated_string(&a);
	if (c == '\\')
	{
	    c = *cptr++;
	    mputc(temp, c);
	    if (c == '\n')
	    {
		get_line();
		if (line == NULL)
		    unterminated_string(&a);
	    }
	}
    }
}

static char *
copy_comment(void)
{
    struct mstring *temp = msnew();
    int c;

    c = *cptr;
    if (c == '/')
    {
	mputc(temp, '*');
	while ((c = *++cptr) != '\n')
	{
	    mputc(temp, c);
	    if (c == '*' && cptr[1] == '/')
		mputc(temp, ' ');
	}
	mputc(temp, '*');
	mputc(temp, '/');
    }
    else if (c == '*')
    {
	struct ainfo a;
	a.a_lineno = lineno;
	a.a_line = dup_line();
	a.a_cptr = a.a_line + (cptr - line - 1);

	mputc(temp, c);
	++cptr;
	for (;;)
	{
	    c = *cptr++;
	    mputc(temp, c);
	    if (c == '*' && *cptr == '/')
	    {
		mputc(temp, '/');
		++cptr;
		FREE(a.a_line);
		return msdone(temp);
	    }
	    if (c == '\n')
	    {
		get_line();
		if (line == NULL)
		    unterminated_comment(&a);
	    }
	}
    }
    return msdone(temp);
}

static int
check_key(int pos)
{
    const char *key = code_keys[pos];
    while (*cptr && *key)
	if (*key++ != *cptr++)
	    return 0;
    if (*key || (!isspace(UCH(*cptr)) && *cptr != L_CURL))
	return 0;
    cptr--;
    return 1;
}

static void
copy_code(void)
{
    int c;
    int curl;
    int cline;
    int on_line = 0;
    int pos = CODE_HEADER;
    struct mstring *code_mstr;

    /* read %code <keyword> { */
    for (;;)
    {
	c = *++cptr;
	if (c == EOF)
	    unexpected_EOF();
	if (isspace(UCH(c)))
	    continue;

	if (c == L_CURL)
	    break;

	if (pos == CODE_HEADER)
	{
	    switch (UCH(c))
	    {
	    case 'r':
		pos = CODE_REQUIRES;
		break;
	    case 'p':
		pos = CODE_PROVIDES;
		break;
	    case 't':
		pos = CODE_TOP;
		break;
	    case 'i':
		pos = CODE_IMPORTS;
		break;
	    default:
		break;
	    }

	    if (pos == -1 || !check_key(pos))
	    {
		syntax_error(lineno, line, cptr);
		/*NOTREACHED */
	    }
	}
    }

    cptr++;			/* skip initial curl */
    while (*cptr && isspace(UCH(*cptr)))	/* skip space */
	cptr++;
    curl = 1;			/* nesting count */

    /* gather text */
    code_lines[pos].name = code_keys[pos];
    if ((cline = (int)code_lines[pos].num) != 0)
    {
	code_mstr = msrenew(code_lines[pos].lines);
    }
    else
    {
	code_mstr = msnew();
    }
    cline++;
    if (!lflag)
	msprintf(code_mstr, line_format, lineno, input_file_name);
    for (;;)
    {
	c = *cptr++;
	switch (c)
	{
	case '\0':
	    get_line();
	    if (line == NULL)
	    {
		unexpected_EOF();
		/*NOTREACHED */
	    }
	    continue;
	case '\n':
	    cline++;
	    on_line = 0;
	    break;
	case L_CURL:
	    curl++;
	    break;
	case R_CURL:
	    if (--curl == 0)
	    {
		if (on_line > 1)
		{
		    mputc(code_mstr, '\n');
		    cline++;
		}
		code_lines[pos].lines = msdone(code_mstr);
		code_lines[pos].num = (size_t)cline;
		return;
	    }
	    break;
	default:
	    break;
	}
	mputc(code_mstr, c);
	on_line++;
    }
}

static void
copy_text(void)
{
    int c;
    FILE *f = text_file;
    int need_newline = 0;
    struct ainfo a;
    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line - 2);

    if (*cptr == '\n')
    {
	get_line();
	if (line == NULL)
	    unterminated_text(&a);
    }
    fprintf_lineno(f, lineno, input_file_name);

  loop:
    c = *cptr++;
    switch (c)
    {
    case '\n':
	putc('\n', f);
	need_newline = 0;
	get_line();
	if (line)
	    goto loop;
	unterminated_text(&a);

    case '\'':
    case '"':
	putc(c, f);
	{
	    char *s = copy_string(c);
	    fputs(s, f);
	    free(s);
	}
	need_newline = 1;
	goto loop;

    case '/':
	putc(c, f);
	{
	    char *s = copy_comment();
	    fputs(s, f);
	    free(s);
	}
	need_newline = 1;
	goto loop;

    case '%':
    case '\\':
	if (*cptr == R_CURL)
	{
	    if (need_newline)
		putc('\n', f);
	    ++cptr;
	    FREE(a.a_line);
	    return;
	}
	/* FALLTHRU */

    default:
	putc(c, f);
	need_newline = 1;
	goto loop;
    }
}

static void
puts_both(const char *s)
{
    fputs(s, text_file);
    if (dflag)
	fputs(s, union_file);
}

static void
putc_both(int c)
{
    putc(c, text_file);
    if (dflag)
	putc(c, union_file);
}

static void
copy_union(void)
{
    int c;
    int depth;
    struct ainfo a;
    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line - 6);

    if (unionized)
	over_unionized(cptr - 6);
    unionized = 1;

    puts_both("#ifdef YYSTYPE\n");
    puts_both("#undef  YYSTYPE_IS_DECLARED\n");
    puts_both("#define YYSTYPE_IS_DECLARED 1\n");
    puts_both("#endif\n");
    puts_both("#ifndef YYSTYPE_IS_DECLARED\n");
    puts_both("#define YYSTYPE_IS_DECLARED 1\n");

    fprintf_lineno(text_file, lineno, input_file_name);
    puts_both("typedef union YYSTYPE");

    depth = 0;
  loop:
    c = *cptr++;
    putc_both(c);
    switch (c)
    {
    case '\n':
	get_line();
	if (line == NULL)
	    unterminated_union(&a);
	goto loop;

    case L_CURL:
	++depth;
	goto loop;

    case R_CURL:
	if (--depth == 0)
	{
	    puts_both(" YYSTYPE;\n");
	    puts_both("#endif /* !YYSTYPE_IS_DECLARED */\n");
	    FREE(a.a_line);
	    return;
	}
	goto loop;

    case '\'':
    case '"':
	{
	    char *s = copy_string(c);
	    puts_both(s);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment();
	    puts_both(s);
	    free(s);
	}
	goto loop;

    default:
	goto loop;
    }
}

static char *
after_blanks(char *s)
{
    while (*s != '\0' && isspace(UCH(*s)))
	++s;
    return s;
}

/*
 * Trim leading/trailing blanks, and collapse multiple embedded blanks to a
 * single space.  Return index to last character in the buffer.
 */
static int
trim_blanks(char *buffer)
{
    if (*buffer != '\0')
    {
	char *d = buffer;
	char *s = after_blanks(d);

	while ((*d++ = *s++) != '\0')
	{
	    ;
	}

	--d;
	while ((--d != buffer) && isspace(UCH(*d)))
	    *d = '\0';

	for (s = d = buffer; (*d++ = *s++) != '\0';)
	{
	    if (isspace(UCH(*s)))
	    {
		*s = ' ';
		while (isspace(UCH(*s)))
		{
		    *s++ = ' ';
		}
		--s;
	    }
	}
    }

    return (int)strlen(buffer) - 1;
}

/*
 * Scan forward in the current line-buffer looking for a right-curly bracket.
 *
 * Parameters begin with a left-curly bracket, and continue until there are no
 * more interesting characters after the last right-curly bracket on the
 * current line.  Bison documents parameters as separated like this:
 *	{type param1} {type2 param2}
 * but also accepts commas (although some versions of bison mishandle this)
 *	{type param1,  type2 param2}
 */
static int
more_curly(void)
{
    char *save = cptr;
    int result = 0;
    int finish = 0;
    do
    {
	switch (next_inline())
	{
	case 0:
	case '\n':
	    finish = 1;
	    break;
	case R_CURL:
	    finish = 1;
	    result = 1;
	    break;
	}
	++cptr;
    }
    while (!finish);
    cptr = save;
    return result;
}

static void
save_param(int k, char *buffer, int name, int type2)
{
    param *head, *p;

    p = TMALLOC(param, 1);
    NO_SPACE(p);

    p->type2 = strdup(buffer + type2);
    NO_SPACE(p->type2);
    buffer[type2] = '\0';
    (void)trim_blanks(p->type2);

    p->name = strdup(buffer + name);
    NO_SPACE(p->name);
    buffer[name] = '\0';
    (void)trim_blanks(p->name);

    p->type = strdup(buffer);
    NO_SPACE(p->type);
    (void)trim_blanks(p->type);

    if (k == LEX_PARAM)
	head = lex_param;
    else
	head = parse_param;

    if (head != NULL)
    {
	while (head->next)
	    head = head->next;
	head->next = p;
    }
    else
    {
	if (k == LEX_PARAM)
	    lex_param = p;
	else
	    parse_param = p;
    }
    p->next = NULL;
}

/*
 * Keep a linked list of parameters.  This may be multi-line, if the trailing
 * right-curly bracket is absent.
 */
static void
copy_param(int k)
{
    int c;
    int name, type2;
    int curly = 0;
    char *buf = 0;
    int i = -1;
    size_t buf_size = 0;
    int st_lineno = lineno;
    char *comma;

    do
    {
	int state = curly;
	c = next_inline();
	switch (c)
	{
	case EOF:
	    unexpected_EOF();
	    break;
	case L_CURL:
	    if (curly == 1)
	    {
		goto oops;
	    }
	    curly = 1;
	    st_lineno = lineno;
	    break;
	case R_CURL:
	    if (curly != 1)
	    {
		goto oops;
	    }
	    curly = 2;
	    break;
	case '\n':
	    if (curly == 0)
	    {
		goto oops;
	    }
	    break;
	case '%':
	    if ((curly == 1) && (cptr == line))
	    {
		lineno = st_lineno;
		missing_brace();
	    }
	    /* FALLTHRU */
	case '"':
	case '\'':
	    goto oops;
	default:
	    if (curly == 0 && !isspace(UCH(c)))
	    {
		goto oops;
	    }
	    break;
	}
	if (buf == 0)
	{
	    buf_size = (size_t)linesize;
	    buf = TMALLOC(char, buf_size);
	    NO_SPACE(buf);
	}
	else if (c == '\n')
	{
	    char *tmp;

	    get_line();
	    if (line == NULL)
		unexpected_EOF();
	    --cptr;
	    buf_size += (size_t)linesize;
	    tmp = TREALLOC(char, buf, buf_size);
	    NO_SPACE(tmp);
	    buf = tmp;
	}
	if (curly)
	{
	    if ((state == 2) && (c == L_CURL))
	    {
		buf[++i] = ',';
	    }
	    else if ((state == 2) && isspace(UCH(c)))
	    {
		;
	    }
	    else if ((c != L_CURL) && (c != R_CURL))
	    {
		buf[++i] = (char)c;
	    }
	}
	cptr++;
    }
    while (curly < 2 || more_curly());

    if (i == 0)
    {
	if (curly == 1)
	{
	    lineno = st_lineno;
	    missing_brace();
	}
	goto oops;
    }

    buf[++i] = '\0';
    (void)trim_blanks(buf);

    comma = buf - 1;
    do
    {
	char *parms = (comma + 1);
	comma = strchr(parms, ',');
	if (comma != 0)
	    *comma = '\0';

	(void)trim_blanks(parms);
	i = (int)strlen(parms) - 1;
	if (i < 0)
	{
	    goto oops;
	}

	if (parms[i] == ']')
	{
	    int level = 1;
	    while (i >= 0)
	    {
		char ch = parms[i--];
		if (ch == ']')
		{
		    ++level;
		}
		else if (ch == '[')
		{
		    if (--level <= 1)
		    {
			++i;
			break;
		    }
		}
	    }
	    if (i <= 0)
		unexpected_EOF();
	    type2 = i--;
	}
	else
	{
	    type2 = i + 1;
	}

	while (i > 0 && (isalnum(UCH(parms[i])) || UCH(parms[i]) == '_'))
	    i--;

	if (!isspace(UCH(parms[i])) && parms[i] != '*')
	    goto oops;

	name = i + 1;

	save_param(k, parms, name, type2);
    }
    while (comma != 0);
    FREE(buf);
    return;

  oops:
    FREE(buf);
    syntax_error(lineno, line, cptr);
}

static int
hexval(int c)
{
    if (c >= '0' && c <= '9')
	return (c - '0');
    if (c >= 'A' && c <= 'F')
	return (c - 'A' + 10);
    if (c >= 'a' && c <= 'f')
	return (c - 'a' + 10);
    return (-1);
}

static bucket *
get_literal(void)
{
    int c, quote;
    int i;
    int n;
    char *s;
    bucket *bp;
    struct ainfo a;
    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line);

    quote = *cptr++;
    cinc = 0;
    for (;;)
    {
	c = *cptr++;
	if (c == quote)
	    break;
	if (c == '\n')
	    unterminated_string(&a);
	if (c == '\\')
	{
	    char *c_cptr = cptr - 1;

	    c = *cptr++;
	    switch (c)
	    {
	    case '\n':
		get_line();
		if (line == NULL)
		    unterminated_string(&a);
		continue;

	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
		n = c - '0';
		c = *cptr;
		if (IS_OCTAL(c))
		{
		    n = (n << 3) + (c - '0');
		    c = *++cptr;
		    if (IS_OCTAL(c))
		    {
			n = (n << 3) + (c - '0');
			++cptr;
		    }
		}
		if (n > MAXCHAR)
		    illegal_character(c_cptr);
		c = n;
		break;

	    case 'x':
		c = *cptr++;
		n = hexval(c);
		if (n < 0 || n >= 16)
		    illegal_character(c_cptr);
		for (;;)
		{
		    c = *cptr;
		    i = hexval(c);
		    if (i < 0 || i >= 16)
			break;
		    ++cptr;
		    n = (n << 4) + i;
		    if (n > MAXCHAR)
			illegal_character(c_cptr);
		}
		c = n;
		break;

	    case 'a':
		c = 7;
		break;
	    case 'b':
		c = '\b';
		break;
	    case 'f':
		c = '\f';
		break;
	    case 'n':
		c = '\n';
		break;
	    case 'r':
		c = '\r';
		break;
	    case 't':
		c = '\t';
		break;
	    case 'v':
		c = '\v';
		break;
	    }
	}
	cachec(c);
    }
    FREE(a.a_line);

    n = cinc;
    s = TMALLOC(char, n);
    NO_SPACE(s);

    for (i = 0; i < n; ++i)
	s[i] = cache[i];

    cinc = 0;
    if (n == 1)
	cachec('\'');
    else
	cachec('"');

    for (i = 0; i < n; ++i)
    {
	c = UCH(s[i]);
	if (c == '\\' || c == cache[0])
	{
	    cachec('\\');
	    cachec(c);
	}
	else if (isprint(UCH(c)))
	    cachec(c);
	else
	{
	    cachec('\\');
	    switch (c)
	    {
	    case 7:
		cachec('a');
		break;
	    case '\b':
		cachec('b');
		break;
	    case '\f':
		cachec('f');
		break;
	    case '\n':
		cachec('n');
		break;
	    case '\r':
		cachec('r');
		break;
	    case '\t':
		cachec('t');
		break;
	    case '\v':
		cachec('v');
		break;
	    default:
		cachec(((c >> 6) & 7) + '0');
		cachec(((c >> 3) & 7) + '0');
		cachec((c & 7) + '0');
		break;
	    }
	}
    }

    if (n == 1)
	cachec('\'');
    else
	cachec('"');

    cachec(NUL);
    bp = lookup(cache);
    bp->class = TERM;
    if (n == 1 && bp->value == UNDEFINED)
	bp->value = UCH(*s);
    FREE(s);

    return (bp);
}

static int
is_reserved(char *name)
{
    if (strcmp(name, ".") == 0 ||
	strcmp(name, "$accept") == 0 ||
	strcmp(name, "$end") == 0)
	return (1);

    if (name[0] == '$' && name[1] == '$' && isdigit(UCH(name[2])))
    {
	char *s = name + 3;

	while (isdigit(UCH(*s)))
	    ++s;
	if (*s == NUL)
	    return (1);
    }

    return (0);
}

static bucket *
get_name(void)
{
    int c;

    cinc = 0;
    for (c = *cptr; IS_IDENT(c); c = *++cptr)
	cachec(c);
    cachec(NUL);

    if (is_reserved(cache))
	used_reserved(cache);

    return (lookup(cache));
}

static Value_t
get_number(void)
{
    int c;
    long n;
    char *base = cptr;

    n = 0;
    for (c = *cptr; isdigit(UCH(c)); c = *++cptr)
    {
	n = (10 * n + (c - '0'));
	if (n > MAXYYINT)
	{
	    syntax_error(lineno, line, base);
	    /*NOTREACHED */
	}
    }

    return (Value_t)(n);
}

static char *
cache_tag(char *tag, size_t len)
{
    int i;
    char *s;

    for (i = 0; i < ntags; ++i)
    {
	if (strncmp(tag, tag_table[i], len) == 0 &&
	    tag_table[i][len] == NUL)
	    return (tag_table[i]);
    }

    if (ntags >= tagmax)
    {
	tagmax += 16;
	tag_table =
	    (tag_table
	     ? TREALLOC(char *, tag_table, tagmax)
	     : TMALLOC(char *, tagmax));
	NO_SPACE(tag_table);
    }

    s = TMALLOC(char, len + 1);
    NO_SPACE(s);

    strncpy(s, tag, len);
    s[len] = 0;
    tag_table[ntags++] = s;
    return s;
}

static char *
get_tag(void)
{
    int c;
    int t_lineno = lineno;
    char *t_line = dup_line();
    char *t_cptr = t_line + (cptr - line);

    ++cptr;
    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (!IS_NAME1(c))
	illegal_tag(t_lineno, t_line, t_cptr);

    cinc = 0;
    do
    {
	cachec(c);
	c = *++cptr;
    }
    while (IS_IDENT(c));
    cachec(NUL);

    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (c != '>')
	illegal_tag(t_lineno, t_line, t_cptr);
    ++cptr;

    FREE(t_line);
    havetags = 1;
    return cache_tag(cache, (size_t)cinc);
}

#if defined(YYBTYACC)
static char *
scan_id(void)
{
    char *b = cptr;

    while (IS_NAME2(UCH(*cptr)))
	cptr++;
    return cache_tag(b, (size_t)(cptr - b));
}
#endif

static void
declare_tokens(int assoc)
{
    int c;
    bucket *bp;
    Value_t value;
    char *tag = 0;

    if (assoc != TOKEN)
	++prec;

    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (c == '<')
    {
	tag = get_tag();
	c = nextc();
	if (c == EOF)
	    unexpected_EOF();
    }

    for (;;)
    {
	if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	    bp = get_name();
	else if (c == '\'' || c == '"')
	    bp = get_literal();
	else
	    return;

	if (bp == goal)
	    tokenized_start(bp->name);
	bp->class = TERM;

	if (tag)
	{
	    if (bp->tag && tag != bp->tag)
		retyped_warning(bp->name);
	    bp->tag = tag;
	}

	if (assoc != TOKEN && !ignore_prec_flag)
	{
	    if (bp->prec && prec != bp->prec)
		reprec_warning(bp->name);
	    bp->assoc = (Assoc_t)assoc;
	    bp->prec = prec;
	    bp->prec_on_decl = 1;
	}

	c = nextc();
	if (c == EOF)
	    unexpected_EOF();

	if (isdigit(UCH(c)))
	{
	    value = get_number();
	    if (bp->value != UNDEFINED && value != bp->value)
		revalued_warning(bp->name);
	    bp->value = value;
	    c = nextc();
	    if (c == EOF)
		unexpected_EOF();
	}
    }
}

/*
 * %expect requires special handling
 * as it really isn't part of the yacc
 * grammar only a flag for yacc proper.
 */
static void
declare_expect(int assoc)
{
    int c;

    if (assoc != EXPECT && assoc != EXPECT_RR)
	++prec;

    /*
     * Stay away from nextc - doesn't
     * detect EOL and will read to EOF.
     */
    c = *++cptr;
    if (c == EOF)
	unexpected_EOF();

    for (;;)
    {
	if (isdigit(UCH(c)))
	{
	    if (assoc == EXPECT)
		SRexpect = get_number();
	    else
		RRexpect = get_number();
	    break;
	}
	/*
	 * Looking for number before EOL.
	 * Spaces, tabs, and numbers are ok,
	 * words, punc., etc. are syntax errors.
	 */
	else if (c == '\n' || isalpha(UCH(c)) || !isspace(UCH(c)))
	{
	    syntax_error(lineno, line, cptr);
	}
	else
	{
	    c = *++cptr;
	    if (c == EOF)
		unexpected_EOF();
	}
    }
}

#if defined(YYBTYACC)
static void
declare_argtypes(bucket *bp)
{
    char *tags[MAXARGS];
    int args = 0;

    if (bp->args >= 0)
	retyped_warning(bp->name);
    cptr++;			/* skip open paren */
    for (;;)
    {
	int c = nextc();
	if (c == EOF)
	    unexpected_EOF();
	if (c != '<')
	    syntax_error(lineno, line, cptr);
	tags[args++] = get_tag();
	c = nextc();
	if (c == R_PAREN)
	    break;
	if (c == EOF)
	    unexpected_EOF();
    }
    cptr++;			/* skip close paren */
    bp->args = args;
    bp->argnames = TMALLOC(char *, args);
    NO_SPACE(bp->argnames);
    bp->argtags = CALLOC(sizeof(char *), args + 1);
    NO_SPACE(bp->argtags);
    while (--args >= 0)
    {
	bp->argtags[args] = tags[args];
	bp->argnames[args] = NULL;
    }
}
#endif

static void
declare_types(void)
{
    int c;
    bucket *bp = NULL;
    char *tag = NULL;

    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (c == '<')
	tag = get_tag();

    for (;;)
    {
	c = nextc();
	if (c == EOF)
	    unexpected_EOF();
	if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	{
	    bp = get_name();
#if defined(YYBTYACC)
	    if (nextc() == L_PAREN)
		declare_argtypes(bp);
	    else
		bp->args = 0;
#endif
	}
	else if (c == '\'' || c == '"')
	{
	    bp = get_literal();
#if defined(YYBTYACC)
	    bp->args = 0;
#endif
	}
	else
	    return;

	if (tag)
	{
	    if (bp->tag && tag != bp->tag)
		retyped_warning(bp->name);
	    bp->tag = tag;
	}
    }
}

static void
declare_start(void)
{
    int c;
    bucket *bp;

    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    if (!isalpha(UCH(c)) && c != '_' && c != '.' && c != '$')
	syntax_error(lineno, line, cptr);
    bp = get_name();
    if (bp->class == TERM)
	terminal_start(bp->name);
    if (goal && goal != bp)
	restarted_warning();
    goal = bp;
}

static void
read_declarations(void)
{
    cache_size = CACHE_SIZE;
    cache = TMALLOC(char, cache_size);
    NO_SPACE(cache);

    for (;;)
    {
	int k;
	int c = nextc();

	if (c == EOF)
	    unexpected_EOF();
	if (c != '%')
	    syntax_error(lineno, line, cptr);
	switch (k = keyword())
	{
	case MARK:
	    return;

	case IDENT:
	    copy_ident();
	    break;

	case XCODE:
	    copy_code();
	    break;

	case TEXT:
	    copy_text();
	    break;

	case UNION:
	    copy_union();
	    break;

	case TOKEN:
	case LEFT:
	case RIGHT:
	case NONASSOC:
	case PRECEDENCE:
	    declare_tokens(k);
	    break;

	case EXPECT:
	case EXPECT_RR:
	    declare_expect(k);
	    break;

	case TYPE:
	    declare_types();
	    break;

	case START:
	    declare_start();
	    break;

	case PURE_PARSER:
	    pure_parser = 1;
	    break;

	case PARSE_PARAM:
	case LEX_PARAM:
	    copy_param(k);
	    break;

	case TOKEN_TABLE:
	    token_table = 1;
	    break;

	case ERROR_VERBOSE:
	    error_verbose = 1;
	    break;

#if defined(YYBTYACC)
	case LOCATIONS:
	    locations = 1;
	    break;

	case DESTRUCTOR:
	    destructor = 1;
	    copy_destructor();
	    break;
	case INITIAL_ACTION:
	    copy_initial_action();
	    break;
#endif

	case NONPOSIX_DEBUG:
	    tflag = 1;
	    break;

	case POSIX_YACC:
	    /* noop for bison compatibility. byacc is already designed to be posix
	     * yacc compatible. */
	    break;

	case SKIP_DEFINE:
	    get_line();
	    break;
	}
    }
}

static void
initialize_grammar(void)
{
    nitems = 4;
    maxitems = 300;

    pitem = TMALLOC(bucket *, maxitems);
    NO_SPACE(pitem);

    pitem[0] = 0;
    pitem[1] = 0;
    pitem[2] = 0;
    pitem[3] = 0;

    nrules = 3;
    maxrules = 100;

    plhs = TMALLOC(bucket *, maxrules);
    NO_SPACE(plhs);

    plhs[0] = 0;
    plhs[1] = 0;
    plhs[2] = 0;

    rprec = TMALLOC(Value_t, maxrules);
    NO_SPACE(rprec);

    rprec[0] = 0;
    rprec[1] = 0;
    rprec[2] = 0;

    rassoc = TMALLOC(Assoc_t, maxrules);
    NO_SPACE(rassoc);

    rassoc[0] = TOKEN;
    rassoc[1] = TOKEN;
    rassoc[2] = TOKEN;

    rprec_bucket = TMALLOC(bucket*, maxrules);
    NO_SPACE(rprec_bucket);

    rprec_bucket[0] = 0;
    rprec_bucket[1] = 0;
    rprec_bucket[2] = 0;
}

static void
expand_items(void)
{
    maxitems += 300;
    pitem = TREALLOC(bucket*, pitem, maxitems);
    NO_SPACE(pitem);
}

static void
expand_rules(void)
{
    maxrules += 100;

    plhs = TREALLOC(bucket *, plhs, maxrules);
    NO_SPACE(plhs);

    rprec = TREALLOC(Value_t, rprec, maxrules);
    NO_SPACE(rprec);

    rassoc = TREALLOC(Assoc_t, rassoc, maxrules);
    NO_SPACE(rassoc);

    rprec_bucket = TREALLOC(bucket*, rprec_bucket, maxrules);
    NO_SPACE(rprec_bucket);
}

/* set immediately prior to where copy_args() could be called, and incremented by
   the various routines that will rescan the argument list as appropriate */
static int rescan_lineno;
#if defined(YYBTYACC)

static char *
copy_args(int *alen)
{
    struct mstring *s = msnew();
    int depth = 0, len = 1;
    char c, quote = 0;
    struct ainfo a;

    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line - 1);

    while ((c = *cptr++) != R_PAREN || depth || quote)
    {
	if (c == ',' && !quote && !depth)
	{
	    len++;
	    mputc(s, 0);
	    continue;
	}
	mputc(s, c);
	if (c == '\n')
	{
	    get_line();
	    if (!line)
	    {
		if (quote)
		    unterminated_string(&a);
		else
		    unterminated_arglist(&a);
	    }
	}
	else if (quote)
	{
	    if (c == quote)
		quote = 0;
	    else if (c == '\\')
	    {
		if (*cptr != '\n')
		    mputc(s, *cptr++);
	    }
	}
	else
	{
	    if (c == L_PAREN)
		depth++;
	    else if (c == R_PAREN)
		depth--;
	    else if (c == '\"' || c == '\'')
		quote = c;
	}
    }
    if (alen)
	*alen = len;
    FREE(a.a_line);
    return msdone(s);
}

static char *
parse_id(char *p, char **save)
{
    char *b;

    while (isspace(UCH(*p)))
	if (*p++ == '\n')
	    rescan_lineno++;
    if (!isalpha(UCH(*p)) && *p != '_')
	return NULL;
    b = p;
    while (IS_NAME2(UCH(*p)))
	p++;
    if (save)
    {
	*save = cache_tag(b, (size_t)(p - b));
    }
    return p;
}

static char *
parse_int(char *p, int *save)
{
    int neg = 0, val = 0;

    while (isspace(UCH(*p)))
	if (*p++ == '\n')
	    rescan_lineno++;
    if (*p == '-')
    {
	neg = 1;
	p++;
    }
    if (!isdigit(UCH(*p)))
	return NULL;
    while (isdigit(UCH(*p)))
	val = val * 10 + *p++ - '0';
    if (neg)
	val = -val;
    if (save)
	*save = val;
    return p;
}

static void
parse_arginfo(bucket *a, char *args, int argslen)
{
    char *p = args, *tmp;
    int i, redec = 0;

    if (a->args >= 0)
    {
	if (a->args != argslen)
	    arg_number_disagree_warning(rescan_lineno, a->name);
	redec = 1;
    }
    else
    {
	if ((a->args = argslen) == 0)
	    return;
	a->argnames = TMALLOC(char *, argslen);
	NO_SPACE(a->argnames);
	a->argtags = TMALLOC(char *, argslen);
	NO_SPACE(a->argtags);
    }
    if (!args)
	return;
    for (i = 0; i < argslen; i++)
    {
	while (isspace(UCH(*p)))
	    if (*p++ == '\n')
		rescan_lineno++;
	if (*p++ != '$')
	    bad_formals();
	while (isspace(UCH(*p)))
	    if (*p++ == '\n')
		rescan_lineno++;
	if (*p == '<')
	{
	    havetags = 1;
	    if (!(p = parse_id(p + 1, &tmp)))
		bad_formals();
	    while (isspace(UCH(*p)))
		if (*p++ == '\n')
		    rescan_lineno++;
	    if (*p++ != '>')
		bad_formals();
	    if (redec)
	    {
		if (a->argtags[i] != tmp)
		    arg_type_disagree_warning(rescan_lineno, i + 1, a->name);
	    }
	    else
		a->argtags[i] = tmp;
	}
	else if (!redec)
	    a->argtags[i] = NULL;
	if (!(p = parse_id(p, &a->argnames[i])))
	    bad_formals();
	while (isspace(UCH(*p)))
	    if (*p++ == '\n')
		rescan_lineno++;
	if (*p++)
	    bad_formals();
    }
    free(args);
}

static char *
compile_arg(char **theptr, char *yyvaltag)
{
    char *p = *theptr;
    struct mstring *c = msnew();
    int i, n;
    Value_t *offsets = NULL, maxoffset;
    bucket **rhs;

    maxoffset = 0;
    n = 0;
    for (i = nitems - 1; pitem[i]; --i)
    {
	n++;
	if (pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	int j;

	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < nitems; i++)
	    if (pitem[i]->class != ARGUMENT)
		offsets[++j] = (Value_t)(i - nitems + 1);
    }
    rhs = pitem + nitems - 1;

    if (yyvaltag)
	msprintf(c, "yyval.%s = ", yyvaltag);
    else
	msprintf(c, "yyval = ");
    while (*p)
    {
	if (*p == '$')
	{
	    char *tag = NULL;
	    if (*++p == '<')
		if (!(p = parse_id(++p, &tag)) || *p++ != '>')
		    illegal_tag(rescan_lineno, NULL, NULL);
	    if (isdigit(UCH(*p)) || *p == '-')
	    {
		int val;
		if (!(p = parse_int(p, &val)))
		    dollar_error(rescan_lineno, NULL, NULL);
		if (val <= 0)
		    i = val - n;
		else if (val > maxoffset)
		{
		    dollar_warning(rescan_lineno, val);
		    i = val - maxoffset;
		}
		else if (maxoffset > 0)
		{
		    i = offsets[val];
		    if (!tag && !(tag = rhs[i]->tag) && havetags)
			untyped_rhs(val, rhs[i]->name);
		}
		msprintf(c, "yystack.l_mark[%d]", i);
		if (tag)
		    msprintf(c, ".%s", tag);
		else if (havetags)
		    unknown_rhs(val);
	    }
	    else if (isalpha(UCH(*p)) || *p == '_')
	    {
		char *arg;
		if (!(p = parse_id(p, &arg)))
		    dollar_error(rescan_lineno, NULL, NULL);
		for (i = plhs[nrules]->args - 1; i >= 0; i--)
		    if (arg == plhs[nrules]->argnames[i])
			break;
		if (i < 0)
		    unknown_arg_warning(rescan_lineno, "$", arg, NULL, NULL);
		else if (!tag)
		    tag = plhs[nrules]->argtags[i];
		msprintf(c, "yystack.l_mark[%d]",
			 i - plhs[nrules]->args + 1 - n);
		if (tag)
		    msprintf(c, ".%s", tag);
		else if (havetags)
		    untyped_arg_warning(rescan_lineno, "$", arg);
	    }
	    else
		dollar_error(rescan_lineno, NULL, NULL);
	}
	else if (*p == '@')
	{
	    at_error(rescan_lineno, NULL, NULL);
	}
	else
	{
	    if (*p == '\n')
		rescan_lineno++;
	    mputc(c, *p++);
	}
    }
    *theptr = p;
    if (maxoffset > 0)
	FREE(offsets);
    return msdone(c);
}

static int
can_elide_arg(char **theptr, char *yyvaltag)
{
    char *p = *theptr;
    int rv = 0;
    int i, n = 0;
    Value_t *offsets = NULL, maxoffset = 0;
    bucket **rhs;
    char *tag = 0;

    if (*p++ != '$')
	return 0;
    if (*p == '<')
    {
	if (!(p = parse_id(++p, &tag)) || *p++ != '>')
	    return 0;
    }
    for (i = nitems - 1; pitem[i]; --i)
    {
	n++;
	if (pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	int j;

	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < nitems; i++)
	    if (pitem[i]->class != ARGUMENT)
		offsets[++j] = (Value_t)(i - nitems + 1);
    }
    rhs = pitem + nitems - 1;

    if (isdigit(UCH(*p)) || *p == '-')
    {
	int val;
	if (!(p = parse_int(p, &val)))
	    rv = 0;
	else
	{
	    if (val <= 0)
		rv = 1 - val + n;
	    else if (val > maxoffset)
		rv = 0;
	    else
	    {
		i = offsets[val];
		rv = 1 - i;
		if (!tag)
		    tag = rhs[i]->tag;
	    }
	}
    }
    else if (isalpha(UCH(*p)) || *p == '_')
    {
	char *arg;
	if (!(p = parse_id(p, &arg)))
	{
	    FREE(offsets);
	    return 0;
	}
	for (i = plhs[nrules]->args - 1; i >= 0; i--)
	    if (arg == plhs[nrules]->argnames[i])
		break;
	if (i >= 0)
	{
	    if (!tag)
		tag = plhs[nrules]->argtags[i];
	    rv = plhs[nrules]->args + n - i;
	}
    }
    if (tag && yyvaltag)
    {
	if (strcmp(tag, yyvaltag))
	    rv = 0;
    }
    else if (tag || yyvaltag)
	rv = 0;
    if (maxoffset > 0)
	FREE(offsets);
    if (p == 0 || *p || rv <= 0)
	return 0;
    *theptr = p + 1;
    return rv;
}

#define ARG_CACHE_SIZE	1024
static struct arg_cache
{
    struct arg_cache *next;
    char *code;
    int rule;
}
 *arg_cache[ARG_CACHE_SIZE];

static int
lookup_arg_cache(char *code)
{
    struct arg_cache *entry;

    entry = arg_cache[strnshash(code) % ARG_CACHE_SIZE];
    while (entry)
    {
	if (!strnscmp(entry->code, code))
	    return entry->rule;
	entry = entry->next;
    }
    return -1;
}

static void
insert_arg_cache(char *code, int rule)
{
    struct arg_cache *entry = NEW(struct arg_cache);
    int i;

    NO_SPACE(entry);
    i = strnshash(code) % ARG_CACHE_SIZE;
    entry->code = code;
    entry->rule = rule;
    entry->next = arg_cache[i];
    arg_cache[i] = entry;
}

static void
clean_arg_cache(void)
{
    struct arg_cache *e, *t;
    int i;

    for (i = 0; i < ARG_CACHE_SIZE; i++)
    {
	for (e = arg_cache[i]; (t = e); e = e->next, FREE(t))
	    free(e->code);
	arg_cache[i] = NULL;
    }
}
#endif /* defined(YYBTYACC) */

static void
advance_to_start(void)
{
    int c;
    bucket *bp;
    int s_lineno;
#if defined(YYBTYACC)
    char *args = NULL;
    int argslen = 0;
#endif

    for (;;)
    {
	char *s_cptr;

	c = nextc();
	if (c != '%')
	    break;
	s_cptr = cptr;
	switch (keyword())
	{
	case XCODE:
	    copy_code();
	    break;

	case MARK:
	    no_grammar();

	case TEXT:
	    copy_text();
	    break;

	case START:
	    declare_start();
	    break;

	default:
	    syntax_error(lineno, line, s_cptr);
	}
    }

    c = nextc();
    if (!isalpha(UCH(c)) && c != '_' && c != '.' && c != '_')
	syntax_error(lineno, line, cptr);
    bp = get_name();
    if (goal == 0)
    {
	if (bp->class == TERM)
	    terminal_start(bp->name);
	goal = bp;
    }

    s_lineno = lineno;
    c = nextc();
    if (c == EOF)
	unexpected_EOF();
    rescan_lineno = lineno;	/* line# for possible inherited args rescan */
#if defined(YYBTYACC)
    if (c == L_PAREN)
    {
	++cptr;
	args = copy_args(&argslen);
	NO_SPACE(args);
	c = nextc();
    }
#endif
    if (c != ':')
	syntax_error(lineno, line, cptr);
    start_rule(bp, s_lineno);
#if defined(YYBTYACC)
    parse_arginfo(bp, args, argslen);
#endif
    ++cptr;
}

static void
start_rule(bucket *bp, int s_lineno)
{
    if (bp->class == TERM)
	terminal_lhs(s_lineno);
    bp->class = NONTERM;
    if (!bp->index)
	bp->index = nrules;
    if (nrules >= maxrules)
	expand_rules();
    plhs[nrules] = bp;
    rprec[nrules] = UNDEFINED;
    rassoc[nrules] = TOKEN;
    rprec_bucket[nrules] = 0;
}

static void
end_rule(void)
{
    if (!last_was_action && plhs[nrules]->tag)
    {
	if (pitem[nitems - 1])
	{
	    int i;

	    for (i = nitems - 1; (i > 0) && pitem[i]; --i)
		continue;
	    if (pitem[i + 1] == 0 || pitem[i + 1]->tag != plhs[nrules]->tag)
		default_action_warning(plhs[nrules]->name);
	}
	else
	    default_action_warning(plhs[nrules]->name);
    }

    last_was_action = 0;
    if (nitems >= maxitems)
	expand_items();
    pitem[nitems] = 0;
    ++nitems;
    ++nrules;
}

static void
insert_empty_rule(void)
{
    bucket *bp, **bpp;

    assert(cache);
    assert(cache_size >= CACHE_SIZE);
    sprintf(cache, "$$%d", ++gensym);
    bp = make_bucket(cache);
    last_symbol->next = bp;
    last_symbol = bp;
    bp->tag = plhs[nrules]->tag;
    bp->class = ACTION;
#if defined(YYBTYACC)
    bp->args = 0;
#endif

    nitems = (Value_t)(nitems + 2);
    if (nitems > maxitems)
	expand_items();
    bpp = pitem + nitems - 1;
    *bpp-- = bp;
    while ((bpp[0] = bpp[-1]) != 0)
	--bpp;

    if (++nrules >= maxrules)
	expand_rules();
    plhs[nrules] = plhs[nrules - 1];
    plhs[nrules - 1] = bp;
    rprec[nrules] = rprec[nrules - 1];
    rprec[nrules - 1] = 0;
    rassoc[nrules] = rassoc[nrules - 1];
    rassoc[nrules - 1] = TOKEN;
    rprec_bucket[nrules] = rprec_bucket[nrules - 1];
    rprec_bucket[nrules - 1] = 0;
}

#if defined(YYBTYACC)
static char *
insert_arg_rule(char *arg, char *tag)
{
    int line_number = rescan_lineno;
    char *code = compile_arg(&arg, tag);
    int rule = lookup_arg_cache(code);
    FILE *f = action_file;

    if (rule < 0)
    {
	rule = nrules;
	insert_arg_cache(code, rule);
	trialaction = 1;	/* arg rules always run in trial mode */
	begin_case(f, rule - RULE_NUM_OFFSET);
	fprintf_lineno(f, line_number, input_file_name);
	fprintf(f, "%s;", code);
	end_case(f);
	insert_empty_rule();
	plhs[rule]->tag = cache_tag(tag, strlen(tag));
	plhs[rule]->class = ARGUMENT;
    }
    else
    {
	if (++nitems > maxitems)
	    expand_items();
	pitem[nitems - 1] = plhs[rule];
	free(code);
    }
    return arg + 1;
}
#endif

static void
add_symbol(void)
{
    int c;
    bucket *bp;
    int s_lineno = lineno;
#if defined(YYBTYACC)
    char *args = NULL;
    int argslen = 0;
#endif

    c = *cptr;
    if (c == '\'' || c == '"')
	bp = get_literal();
    else
	bp = get_name();

    c = nextc();
    rescan_lineno = lineno;	/* line# for possible inherited args rescan */
#if defined(YYBTYACC)
    if (c == L_PAREN)
    {
	++cptr;
	args = copy_args(&argslen);
	NO_SPACE(args);
	c = nextc();
    }
#endif
    if (c == '[') //eat name alias 
    {
        for (;;++cptr)
        {
            c = nextc();

            if (c == EOF)
                break;
            if (c == ']') {
                ++cptr;
                c = nextc();
                break;
            }
        }
    }
    if (c == ':')
    {
	end_rule();
	start_rule(bp, s_lineno);
#if defined(YYBTYACC)
	parse_arginfo(bp, args, argslen);
#endif
	++cptr;
	return;
    }

    if (last_was_action)
	insert_empty_rule();
    last_was_action = 0;

#if defined(YYBTYACC)
    if (bp->args < 0)
	bp->args = argslen;
    if (argslen == 0 && bp->args > 0 && pitem[nitems - 1] == NULL)
    {
	int i;
	if (plhs[nrules]->args != bp->args)
	    wrong_number_args_warning("default ", bp->name);
	for (i = bp->args - 1; i >= 0; i--)
	    if (plhs[nrules]->argtags[i] != bp->argtags[i])
		wrong_type_for_arg_warning(i + 1, bp->name);
    }
    else if (bp->args != argslen)
	wrong_number_args_warning("", bp->name);
    if (args != 0)
    {
	char *ap = args;
	int i = 0;
	int elide_cnt = can_elide_arg(&ap, bp->argtags[0]);

	if (elide_cnt > argslen)
	    elide_cnt = 0;
	if (elide_cnt)
	{
	    for (i = 1; i < elide_cnt; i++)
		if (can_elide_arg(&ap, bp->argtags[i]) != elide_cnt - i)
		{
		    elide_cnt = 0;
		    break;
		}
	}
	if (elide_cnt)
	{
	    assert(i == elide_cnt);
	}
	else
	{
	    ap = args;
	    i = 0;
	}
	for (; i < argslen; i++)
	    ap = insert_arg_rule(ap, bp->argtags[i]);
	free(args);
    }
#endif /* defined(YYBTYACC) */

    if (++nitems > maxitems)
	expand_items();
    pitem[nitems - 1] = bp;
}

static void
copy_action(void)
{
    int c;
    int i, j, n;
    int depth;
#if defined(YYBTYACC)
    int haveyyval = 0;
#endif
    char *tag;
    FILE *f = action_file;
    struct ainfo a;
    Value_t *offsets = NULL, maxoffset;
    bucket **rhs;

    a.a_lineno = lineno;
    a.a_line = dup_line();
    a.a_cptr = a.a_line + (cptr - line);

    if (last_was_action)
	insert_empty_rule();
    last_was_action = 1;
#if defined(YYBTYACC)
    trialaction = (*cptr == L_BRAC);
#endif

    begin_case(f, nrules - RULE_NUM_OFFSET);
#if defined(YYBTYACC)
    if (backtrack)
    {
	if (!trialaction)
	    fprintf(f, "  if (!yytrial)\n");
    }
#endif
    fprintf_lineno(f, lineno, input_file_name);
    if (*cptr == '=')
	++cptr;

    /* avoid putting curly-braces in first column, to ease editing */
    if (*after_blanks(cptr) == L_CURL)
    {
	putc('\t', f);
	cptr = after_blanks(cptr);
    }

    maxoffset = 0;
    n = 0;
    for (i = nitems - 1; pitem[i]; --i)
    {
	++n;
	if (pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < nitems; i++)
	{
	    if (pitem[i]->class != ARGUMENT)
	    {
		offsets[++j] = (Value_t)(i - nitems + 1);
	    }
	}
    }
    rhs = pitem + nitems - 1;

    depth = 0;
  loop:
    c = *cptr;
    if (c == '$')
    {
	if (cptr[1] == '<')
	{
	    int d_lineno = lineno;
	    char *d_line = dup_line();
	    char *d_cptr = d_line + (cptr - line);

	    ++cptr;
	    tag = get_tag();
	    c = *cptr;
	    if (c == '$')
	    {
		fprintf(f, "yyval.%s", tag);
		++cptr;
		FREE(d_line);
		goto loop;
	    }
	    else if (isdigit(UCH(c)))
	    {
		i = get_number();
		if (i == 0)
		    fprintf(f, "yystack.l_mark[%d].%s", -n, tag);
		else if (i > maxoffset)
		{
		    dollar_warning(d_lineno, i);
		    fprintf(f, "yystack.l_mark[%ld].%s",
			    (long)(i - maxoffset), tag);
		}
		else if (offsets)
		    fprintf(f, "yystack.l_mark[%ld].%s",
			    (long)offsets[i], tag);
		FREE(d_line);
		goto loop;
	    }
	    else if (c == '-' && isdigit(UCH(cptr[1])))
	    {
		++cptr;
		i = -get_number() - n;
		fprintf(f, "yystack.l_mark[%d].%s", i, tag);
		FREE(d_line);
		goto loop;
	    }
#if defined(YYBTYACC)
	    else if (isalpha(UCH(c)) || c == '_')
	    {
		char *arg = scan_id();
		for (i = plhs[nrules]->args - 1; i >= 0; i--)
		    if (arg == plhs[nrules]->argnames[i])
			break;
		if (i < 0)
		    unknown_arg_warning(d_lineno, "$", arg, d_line, d_cptr);
		fprintf(f, "yystack.l_mark[%d].%s",
			i - plhs[nrules]->args + 1 - n, tag);
		FREE(d_line);
		goto loop;
	    }
#endif
	    else
		dollar_error(d_lineno, d_line, d_cptr);
	}
	else if (cptr[1] == '$')
	{
	    if (havetags)
	    {
		tag = plhs[nrules]->tag;
		if (tag == 0)
		    untyped_lhs();
		fprintf(f, "yyval.%s", tag);
	    }
	    else
		fprintf(f, "yyval");
	    cptr += 2;
#if defined(YYBTYACC)
	    haveyyval = 1;
#endif
	    goto loop;
	}
	else if (isdigit(UCH(cptr[1])))
	{
	    ++cptr;
	    i = get_number();
	    if (havetags && offsets)
	    {
		if (i <= 0 || i > maxoffset)
		    unknown_rhs(i);
		tag = rhs[offsets[i]]->tag;
		if (tag == 0)
		    untyped_rhs(i, rhs[offsets[i]]->name);
		fprintf(f, "yystack.l_mark[%ld].%s", (long)offsets[i], tag);
	    }
	    else
	    {
		if (i == 0)
		    fprintf(f, "yystack.l_mark[%d]", -n);
		else if (i > maxoffset)
		{
		    dollar_warning(lineno, i);
		    fprintf(f, "yystack.l_mark[%ld]", (long)(i - maxoffset));
		}
		else if (offsets)
		    fprintf(f, "yystack.l_mark[%ld]", (long)offsets[i]);
	    }
	    goto loop;
	}
	else if (cptr[1] == '-')
	{
	    cptr += 2;
	    i = get_number();
	    if (havetags)
		unknown_rhs(-i);
	    fprintf(f, "yystack.l_mark[%d]", -i - n);
	    goto loop;
	}
#if defined(YYBTYACC)
	else if (isalpha(UCH(cptr[1])) || cptr[1] == '_')
	{
	    char *arg;
	    ++cptr;
	    arg = scan_id();
	    for (i = plhs[nrules]->args - 1; i >= 0; i--)
		if (arg == plhs[nrules]->argnames[i])
		    break;
	    if (i < 0)
		unknown_arg_warning(lineno, "$", arg, line, cptr);
	    tag = (i < 0 ? NULL : plhs[nrules]->argtags[i]);
	    fprintf(f, "yystack.l_mark[%d]", i - plhs[nrules]->args + 1 - n);
	    if (tag)
		fprintf(f, ".%s", tag);
	    else if (havetags)
		untyped_arg_warning(lineno, "$", arg);
	    goto loop;
	}
#endif
    }
#if defined(YYBTYACC)
    if (c == '@')
    {
	if (!locations)
	{
	    int l_lineno = lineno;
	    char *l_line = dup_line();
	    char *l_cptr = l_line + (cptr - line);
	    syntax_error(l_lineno, l_line, l_cptr);
	}
	if (cptr[1] == '$')
	{
	    fprintf(f, "yyloc");
	    cptr += 2;
	    goto loop;
	}
	else if (isdigit(UCH(cptr[1])))
	{
	    ++cptr;
	    i = get_number();
	    if (i == 0)
		fprintf(f, "yystack.p_mark[%d]", -n);
	    else if (i > maxoffset)
	    {
		at_warning(lineno, i);
		fprintf(f, "yystack.p_mark[%d]", i - maxoffset);
	    }
	    else if (offsets)
		fprintf(f, "yystack.p_mark[%d]", offsets[i]);
	    goto loop;
	}
	else if (cptr[1] == '-')
	{
	    cptr += 2;
	    i = get_number();
	    fprintf(f, "yystack.p_mark[%d]", -i - n);
	    goto loop;
	}
    }
#endif
    if (IS_NAME1(c))
    {
	do
	{
	    putc(c, f);
	    c = *++cptr;
	}
	while (IS_NAME2(c));
	goto loop;
    }
    ++cptr;
#if defined(YYBTYACC)
    if (backtrack)
    {
	if (trialaction && c == L_BRAC && depth == 0)
	{
	    ++depth;
	    putc(L_CURL, f);
	    goto loop;
	}
	if (trialaction && c == R_BRAC && depth == 1)
	{
	    --depth;
	    putc(R_CURL, f);
	    c = nextc();
	    if (c == L_BRAC && !haveyyval)
	    {
		goto loop;
	    }
	    if (c == L_CURL && !haveyyval)
	    {
		fprintf(f, "  if (!yytrial)\n");
		fprintf_lineno(f, lineno, input_file_name);
		trialaction = 0;
		goto loop;
	    }
	    end_case(f);
	    FREE(a.a_line);
	    if (maxoffset > 0)
		FREE(offsets);
	    return;
	}
    }
#endif
    putc(c, f);
    switch (c)
    {
    case '\n':
	get_line();
	if (line)
	    goto loop;
	unterminated_action(&a);

    case ';':
	if (depth > 0)
	    goto loop;
	end_case(f);
	free(a.a_line);
	if (maxoffset > 0)
	    FREE(offsets);
	return;

#if defined(YYBTYACC)
    case L_BRAC:
	if (backtrack)
	    ++depth;
	goto loop;

    case R_BRAC:
	if (backtrack)
	    --depth;
	goto loop;
#endif

    case L_CURL:
	++depth;
	goto loop;

    case R_CURL:
	if (--depth > 0)
	    goto loop;
#if defined(YYBTYACC)
	if (backtrack)
	{
	    c = nextc();
	    if (c == L_BRAC && !haveyyval)
	    {
		trialaction = 1;
		goto loop;
	    }
	    if (c == L_CURL && !haveyyval)
	    {
		fprintf(f, "  if (!yytrial)\n");
		fprintf_lineno(f, lineno, input_file_name);
		goto loop;
	    }
	}
#endif
	end_case(f);
	free(a.a_line);
	if (maxoffset > 0)
	    FREE(offsets);
	return;

    case '\'':
    case '"':
	{
	    char *s = copy_string(c);
	    fputs(s, f);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment();
	    fputs(s, f);
	    free(s);
	}
	goto loop;

    default:
	goto loop;
    }
}

#if defined(YYBTYACC)
static char *
get_code(struct ainfo *a, const char *loc)
{
    int c;
    int depth;
    char *tag;
    struct mstring *code_mstr = msnew();

    if (!lflag)
	msprintf(code_mstr, line_format, lineno, input_file_name);

    cptr = after_blanks(cptr);
    if (*cptr == L_CURL)
	/* avoid putting curly-braces in first column, to ease editing */
	mputc(code_mstr, '\t');
    else
	syntax_error(lineno, line, cptr);

    a->a_lineno = lineno;
    a->a_line = dup_line();
    a->a_cptr = a->a_line + (cptr - line);

    depth = 0;
  loop:
    c = *cptr;
    if (c == '$')
    {
	if (cptr[1] == '<')
	{
	    int d_lineno = lineno;
	    char *d_line = dup_line();
	    char *d_cptr = d_line + (cptr - line);

	    ++cptr;
	    tag = get_tag();
	    c = *cptr;
	    if (c == '$')
	    {
		msprintf(code_mstr, "(*val).%s", tag);
		++cptr;
		FREE(d_line);
		goto loop;
	    }
	    else
		dollar_error(d_lineno, d_line, d_cptr);
	}
	else if (cptr[1] == '$')
	{
	    /* process '$$' later; replacement is context dependent */
	    msprintf(code_mstr, "$$");
	    cptr += 2;
	    goto loop;
	}
    }
    if (c == '@' && cptr[1] == '$')
    {
	if (!locations)
	{
	    int l_lineno = lineno;
	    char *l_line = dup_line();
	    char *l_cptr = l_line + (cptr - line);
	    syntax_error(l_lineno, l_line, l_cptr);
	}
	msprintf(code_mstr, "%s", loc);
	cptr += 2;
	goto loop;
    }
    if (IS_NAME1(c))
    {
	do
	{
	    mputc(code_mstr, c);
	    c = *++cptr;
	}
	while (IS_NAME2(c));
	goto loop;
    }
    ++cptr;
    mputc(code_mstr, c);
    switch (c)
    {
    case '\n':
	get_line();
	if (line)
	    goto loop;
	unterminated_action(a);

    case L_CURL:
	++depth;
	goto loop;

    case R_CURL:
	if (--depth > 0)
	    goto loop;
	goto out;

    case '\'':
    case '"':
	{
	    char *s = copy_string(c);
	    msprintf(code_mstr, "%s", s);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment();
	    msprintf(code_mstr, "%s", s);
	    free(s);
	}
	goto loop;

    default:
	goto loop;
    }
  out:
    return msdone(code_mstr);
}

static void
copy_initial_action(void)
{
    struct ainfo a;

    initial_action = get_code(&a, "yyloc");
    free(a.a_line);
}

static void
copy_destructor(void)
{
    char *code_text;
    struct ainfo a;
    bucket *bp;

    code_text = get_code(&a, "(*loc)");

    for (;;)
    {
	int c = nextc();
	if (c == EOF)
	    unexpected_EOF();
	if (c == '<')
	{
	    if (cptr[1] == '>')
	    {			/* "no semantic type" default destructor */
		cptr += 2;
		if ((bp = default_destructor[UNTYPED_DEFAULT]) == NULL)
		{
		    static char untyped_default[] = "<>";
		    bp = make_bucket("untyped default");
		    bp->tag = untyped_default;
		    default_destructor[UNTYPED_DEFAULT] = bp;
		}
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(&a);
		else
		    /* replace "$$" with "(*val)" in destructor code */
		    bp->destructor = process_destructor_XX(code_text, NULL);
	    }
	    else if (cptr[1] == '*' && cptr[2] == '>')
	    {			/* "no per-symbol or per-type" default destructor */
		cptr += 3;
		if ((bp = default_destructor[TYPED_DEFAULT]) == NULL)
		{
		    static char typed_default[] = "<*>";
		    bp = make_bucket("typed default");
		    bp->tag = typed_default;
		    default_destructor[TYPED_DEFAULT] = bp;
		}
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(&a);
		else
		{
		    /* postpone re-processing destructor $$s until end of grammar spec */
		    bp->destructor = TMALLOC(char, strlen(code_text) + 1);
		    NO_SPACE(bp->destructor);
		    strcpy(bp->destructor, code_text);
		}
	    }
	    else
	    {			/* "semantic type" default destructor */
		char *tag = get_tag();
		bp = lookup_type_destructor(tag);
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(&a);
		else
		    /* replace "$$" with "(*val).tag" in destructor code */
		    bp->destructor = process_destructor_XX(code_text, tag);
	    }
	}
	else if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	{			/* "symbol" destructor */
	    bp = get_name();
	    if (bp->destructor != NULL)
		destructor_redeclared_warning(&a);
	    else
	    {
		/* postpone re-processing destructor $$s until end of grammar spec */
		bp->destructor = TMALLOC(char, strlen(code_text) + 1);
		NO_SPACE(bp->destructor);
		strcpy(bp->destructor, code_text);
	    }
	}
	else
	    break;
    }
    free(a.a_line);
    free(code_text);
}

static char *
process_destructor_XX(char *code, char *tag)
{
    int c;
    int quote;
    int depth;
    struct mstring *new_code = msnew();
    char *codeptr = code;

    depth = 0;
  loop:			/* step thru code */
    c = *codeptr;
    if (c == '$' && codeptr[1] == '$')
    {
	codeptr += 2;
	if (tag == NULL)
	    msprintf(new_code, "(*val)");
	else
	    msprintf(new_code, "(*val).%s", tag);
	goto loop;
    }
    if (IS_NAME1(c))
    {
	do
	{
	    mputc(new_code, c);
	    c = *++codeptr;
	}
	while (IS_NAME2(c));
	goto loop;
    }
    ++codeptr;
    mputc(new_code, c);
    switch (c)
    {
    case L_CURL:
	++depth;
	goto loop;

    case R_CURL:
	if (--depth > 0)
	    goto loop;
	return msdone(new_code);

    case '\'':
    case '"':
	quote = c;
	for (;;)
	{
	    c = *codeptr++;
	    mputc(new_code, c);
	    if (c == quote)
		goto loop;
	    if (c == '\\')
	    {
		c = *codeptr++;
		mputc(new_code, c);
	    }
	}

    case '/':
	c = *codeptr;
	if (c == '*')
	{
	    mputc(new_code, c);
	    ++codeptr;
	    for (;;)
	    {
		c = *codeptr++;
		mputc(new_code, c);
		if (c == '*' && *codeptr == '/')
		{
		    mputc(new_code, '/');
		    ++codeptr;
		    goto loop;
		}
	    }
	}
	goto loop;

    default:
	goto loop;
    }
}
#endif /* defined(YYBTYACC) */

static int
mark_symbol(void)
{
    int c;
    bucket *bp = NULL;

    c = cptr[1];
    if (c == '%' || c == '\\')
    {
	cptr += 2;
	return (1);
    }

    if (c == '=')
	cptr += 2;
    else if ((c == 'p' || c == 'P') &&
	     ((c = cptr[2]) == 'r' || c == 'R') &&
	     ((c = cptr[3]) == 'e' || c == 'E') &&
	     ((c = cptr[4]) == 'c' || c == 'C') &&
	     ((c = cptr[5], !IS_IDENT(c))))
	cptr += 5;
    else if ((c == 'e' || c == 'E') &&
	     ((c = cptr[2]) == 'm' || c == 'M') &&
	     ((c = cptr[3]) == 'p' || c == 'P') &&
	     ((c = cptr[4]) == 't' || c == 'T') &&
	     ((c = cptr[5]) == 'y' || c == 'Y') &&
	     ((c = cptr[6], !IS_IDENT(c))))
    {
	cptr += 6;
	return (1);
    }
    else
	syntax_error(lineno, line, cptr);

    c = nextc();
    if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	bp = get_name();
    else if (c == '\'' || c == '"')
	bp = get_literal();
    else
    {
	syntax_error(lineno, line, cptr);
	/*NOTREACHED */
    }

    if (rprec[nrules] != UNDEFINED && bp->prec != rprec[nrules])
	prec_redeclared();

    if(!ignore_prec_flag) {
        rprec[nrules] = bp->prec;
        rassoc[nrules] = bp->assoc;
        rprec_bucket[nrules] = bp;
    }
    return (0);
}

static void
read_grammar(void)
{
    initialize_grammar();
    advance_to_start();

    for (;;)
    {
	int c = nextc();

	if (c == EOF)
	    break;
	if (isalpha(UCH(c))
	    || c == '_'
	    || c == '.'
	    || c == '$'
	    || c == '\''
	    || c == '"')
	{
	    add_symbol();
	}
	else if (c == L_CURL || c == '='
#if defined(YYBTYACC)
		 || (backtrack && c == L_BRAC)
#endif
	    )
	{
	    copy_action();
	}
	else if (c == '|')
	{
	    end_rule();
	    start_rule(plhs[nrules - 1], 0);
	    ++cptr;
	}
	else if (c == '%')
	{
	    if (mark_symbol())
		break;
	}
	else
	    syntax_error(lineno, line, cptr);
    }
    end_rule();
#if defined(YYBTYACC)
    if (goal->args > 0)
	start_requires_args(goal->name);
#endif
}

static void
free_tags(void)
{
    int i;

    if (tag_table == 0)
	return;

    for (i = 0; i < ntags; ++i)
    {
	assert(tag_table[i]);
	FREE(tag_table[i]);
    }
    FREE(tag_table);
}

static void
pack_names(void)
{
    bucket *bp;
    char *p;
    char *t;

    name_pool_size = 13;	/* 13 == sizeof("$end") + sizeof("$accept") */
    for (bp = first_symbol; bp; bp = bp->next)
	name_pool_size += strlen(bp->name) + 1;

    name_pool = TMALLOC(char, name_pool_size);
    NO_SPACE(name_pool);

    strcpy(name_pool, "$accept");
    strcpy(name_pool + 8, "$end");
    t = name_pool + 13;
    for (bp = first_symbol; bp; bp = bp->next)
    {
	char *s = bp->name;

	p = t;
	while ((*t++ = *s++) != 0)
	    continue;
	FREE(bp->name);
	bp->name = p;
    }
}

static void
check_symbols(void)
{
    bucket *bp;

    if (goal->class == UNKNOWN)
	undefined_goal(goal->name);

    for (bp = first_symbol; bp; bp = bp->next)
    {
	if (bp->class == UNKNOWN)
	{
	    undefined_symbol_warning(bp->name);
	    bp->class = TERM;
	}
    }
}

static void
protect_string(char *src, char **des)
{
    *des = src;
    if (src)
    {
	char *s;
	char *d;

	unsigned len = 1;

	s = src;
	while (*s)
	{
	    if ('\\' == *s || '"' == *s)
		len++;
	    s++;
	    len++;
	}

	*des = d = TMALLOC(char, len);
	NO_SPACE(d);

	s = src;
	while (*s)
	{
	    if ('\\' == *s || '"' == *s)
		*d++ = '\\';
	    *d++ = *s++;
	}
	*d = '\0';
    }
}

static void
pack_symbols(void)
{
    bucket *bp;
    bucket **v;
    Value_t i, j, k, n;
#if defined(YYBTYACC)
    Value_t max_tok_pval;
#endif

    nsyms = 2;
    ntokens = 1;
    for (bp = first_symbol; bp; bp = bp->next)
    {
	++nsyms;
	if (bp->class == TERM)
	    ++ntokens;
    }
    start_symbol = (Value_t)ntokens;
    nvars = (Value_t)(nsyms - ntokens);

    symbol_name = TMALLOC(char *, nsyms);
    NO_SPACE(symbol_name);

    symbol_value = TMALLOC(Value_t, nsyms);
    NO_SPACE(symbol_value);

    symbol_prec = TMALLOC(Value_t, nsyms);
    NO_SPACE(symbol_prec);

    symbol_assoc = TMALLOC(char, nsyms);
    NO_SPACE(symbol_assoc);

#if defined(YYBTYACC)
    symbol_pval = TMALLOC(Value_t, nsyms);
    NO_SPACE(symbol_pval);

    if (destructor)
    {
	symbol_destructor = CALLOC(sizeof(char *), nsyms);
	NO_SPACE(symbol_destructor);

	symbol_type_tag = CALLOC(sizeof(char *), nsyms);
	NO_SPACE(symbol_type_tag);
    }
#endif

    v = TMALLOC(bucket *, nsyms);
    NO_SPACE(v);

    v[0] = 0;
    v[start_symbol] = 0;

    i = 1;
    j = (Value_t)(start_symbol + 1);
    for (bp = first_symbol; bp; bp = bp->next)
    {
	if (bp->class == TERM)
	    v[i++] = bp;
	else
	    v[j++] = bp;
    }
    assert(i == ntokens && j == nsyms);

    for (i = 1; i < ntokens; ++i)
	v[i]->index = i;

    goal->index = (Index_t)(start_symbol + 1);
    k = (Value_t)(start_symbol + 2);
    while (++i < nsyms)
	if (v[i] != goal)
	{
	    v[i]->index = k;
	    ++k;
	}

    goal->value = 0;
    k = 1;
    for (i = (Value_t)(start_symbol + 1); i < nsyms; ++i)
    {
	if (v[i] != goal)
	{
	    v[i]->value = k;
	    ++k;
	}
    }

    k = 0;
    for (i = 1; i < ntokens; ++i)
    {
	n = v[i]->value;
	if (n > 256)
	{
	    for (j = k++; j > 0 && symbol_value[j - 1] > n; --j)
		symbol_value[j] = symbol_value[j - 1];
	    symbol_value[j] = n;
	}
    }

    assert(v[1] != 0);

    if (v[1]->value == UNDEFINED)
	v[1]->value = 256;

    j = 0;
    n = 257;
    for (i = 2; i < ntokens; ++i)
    {
	if (v[i]->value == UNDEFINED)
	{
	    while (j < k && n == symbol_value[j])
	    {
		while (++j < k && n == symbol_value[j])
		    continue;
		++n;
	    }
	    v[i]->value = n;
	    ++n;
	}
    }

    symbol_name[0] = name_pool + 8;
    symbol_value[0] = 0;
    symbol_prec[0] = 0;
    symbol_assoc[0] = TOKEN;
#if defined(YYBTYACC)
    symbol_pval[0] = 0;
    max_tok_pval = 0;
#endif
    for (i = 1; i < ntokens; ++i)
    {
	symbol_name[i] = v[i]->name;
	symbol_value[i] = v[i]->value;
	symbol_prec[i] = v[i]->prec;
	symbol_assoc[i] = v[i]->assoc;
#if defined(YYBTYACC)
	symbol_pval[i] = v[i]->value;
	if (symbol_pval[i] > max_tok_pval)
	    max_tok_pval = symbol_pval[i];
	if (destructor)
	{
	    symbol_destructor[i] = v[i]->destructor;
	    symbol_type_tag[i] = v[i]->tag;
	}
#endif
    }
    symbol_name[start_symbol] = name_pool;
    symbol_value[start_symbol] = -1;
    symbol_prec[start_symbol] = 0;
    symbol_assoc[start_symbol] = TOKEN;
#if defined(YYBTYACC)
    symbol_pval[start_symbol] = (Value_t)(max_tok_pval + 1);
#endif
    for (++i; i < nsyms; ++i)
    {
	k = v[i]->index;
	symbol_name[k] = v[i]->name;
	symbol_value[k] = v[i]->value;
	symbol_prec[k] = v[i]->prec;
	symbol_assoc[k] = v[i]->assoc;
#if defined(YYBTYACC)
	symbol_pval[k] = (Value_t)((max_tok_pval + 1) + v[i]->value + 1);
	if (destructor)
	{
	    symbol_destructor[k] = v[i]->destructor;
	    symbol_type_tag[k] = v[i]->tag;
	}
#endif
    }

    if (gflag)
    {
	symbol_pname = TMALLOC(char *, nsyms);
	NO_SPACE(symbol_pname);

	for (i = 0; i < nsyms; ++i)
	    protect_string(symbol_name[i], &(symbol_pname[i]));
    }

    FREE(v);
}

static void
pack_grammar(void)
{
    int i;
    Value_t j;

    ritem = TMALLOC(Value_t, nitems);
    NO_SPACE(ritem);

    rlhs = TMALLOC(Value_t, nrules);
    NO_SPACE(rlhs);

    rrhs = TMALLOC(Value_t, nrules + 1);
    NO_SPACE(rrhs);

    rprec = TREALLOC(Value_t, rprec, nrules);
    NO_SPACE(rprec);

    rassoc = TREALLOC(Assoc_t, rassoc, nrules);
    NO_SPACE(rassoc);

    rprec_bucket = TREALLOC(bucket*, rprec_bucket, nrules);
    NO_SPACE(rprec_bucket);

    ritem[0] = -1;
    ritem[1] = goal->index;
    ritem[2] = 0;
    ritem[3] = -2;
    rlhs[0] = 0;
    rlhs[1] = 0;
    rlhs[2] = start_symbol;
    rrhs[0] = 0;
    rrhs[1] = 0;
    rrhs[2] = 1;

    j = 4;
    for (i = 3; i < nrules; ++i)
    {
	Assoc_t assoc;
	Value_t prec2;

#if defined(YYBTYACC)
	if (plhs[i]->args > 0)
	{
	    if (plhs[i]->argnames)
	    {
		FREE(plhs[i]->argnames);
		plhs[i]->argnames = NULL;
	    }
	    if (plhs[i]->argtags)
	    {
		FREE(plhs[i]->argtags);
		plhs[i]->argtags = NULL;
	    }
	}
#endif /* defined(YYBTYACC) */
	rlhs[i] = plhs[i]->index;
	rrhs[i] = j;
	assoc = TOKEN;
	prec2 = 0;
	while (pitem[j])
	{
	    ritem[j] = pitem[j]->index;
	    if (pitem[j]->class == TERM)
	    {
                if(prec2 == 0 || !lemon_prec_flag) {
                    prec2 = pitem[j]->prec;
                    assoc = pitem[j]->assoc;
                }
	    }
	    ++j;
	}
	ritem[j] = (Value_t)-i;
	++j;
	if (rprec[i] == UNDEFINED)
	{
	    rprec[i] = prec2;
	    rassoc[i] = assoc;
	}
    }
    rrhs[i] = j;

    FREE(plhs);
    FREE(pitem);
#if defined(YYBTYACC)
    clean_arg_cache();
#endif
}

#define START_RULE_IDX 2

static void
print_grammar(void)
{
    int i, k;
    size_t j, spacing = 0;
    FILE *f = verbose_file;

    if (!vflag)
	return;

    k = 1;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
	if (rlhs[i] != rlhs[i - 1])
	{
	    if (i != 2)
		fprintf(f, "\n");
	    fprintf(f, "%4d  %s :", i - RULE_NUM_OFFSET, symbol_name[rlhs[i]]);
	    spacing = strlen(symbol_name[rlhs[i]]) + 1;
	}
	else
	{
	    fprintf(f, "%4d  ", i - RULE_NUM_OFFSET);
	    j = spacing;
	    while (j-- != 0)
		putc(' ', f);
	    putc('|', f);
	}

	while (ritem[k] >= 0)
	{
	    fprintf(f, " %s", symbol_name[ritem[k]]);
	    ++k;
	}
	++k;
        bucket *prec_bucket = rprec_bucket[i];
        if(prec_bucket) {
            fprintf(f, " %%prec %s", prec_bucket->name);
        }
	putc('\n', f);
    }
}

static void
print_grammar_ebnf(void)
{
    int i, k;
    FILE *f = ebnf_file;

    if (!ebnf_flag)
	return;

    k = 1;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (rlhs[i] != rlhs[i - 1])
	{
	    if (i != START_RULE_IDX && !skip_rule)
		fprintf(f, "\n");
	    if(!skip_rule)
                fprintf(f, "%s ::=\n\t", (i == START_RULE_IDX) ? "_start_rule_" : sym_name);
	}
	else
	{
	    fprintf(f, "\t|");
	}

        if(!(ritem[k] >= 0)) {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
        else
        {
            while (ritem[k] >= 0)
            {
                sym_name = symbol_name[ritem[k]];
                if(!skip_rule && sym_name[0] != '$')
                    fprintf(f, " %s", sym_name);
                ++k;
            }
        }
	++k;
        if(!skip_rule)
            putc('\n', f);
    }
}

static const char * ascii_names_st[] = {
/*0*/ "0NUL" /*Null char*/,
/*1*/ "SOH" /*Start of Heading*/,
/*2*/ "STX" /*Start of Text*/,
/*3*/ "ETX" /*End of Text*/,
/*4*/ "EOT" /*End of Transmission*/,
/*5*/ "ENQ" /*Enquiry*/,
/*6*/ "ACK" /*Acknowledgment*/,
/*7*/ "BEL" /*Bell*/,
/*8*/ "BS" /*Back Space*/,
/*9*/ "HT" /*Horizontal Tab*/,
/*10*/ "LF" /*Line Feed*/,
/*11*/ "VT" /*Vertical Tab*/,
/*12*/ "FF" /*Form Feed*/,
/*13*/ "CR" /*Carriage Return*/,
/*14*/ "SO" /*Shift Out / X-On*/,
/*15*/ "SI" /*Shift In / X-Off*/,
/*16*/ "DLE" /*Data Line Escape*/,
/*17*/ "DC1" /*Device Control 1 (oft. XON)*/,
/*18*/ "DC2" /*Device Control 2*/,
/*19*/ "DC3" /*Device Control 3 (oft. XOFF)*/,
/*20*/ "DC4" /*Device Control 4*/,
/*21*/ "NAK" /*Negative Acknowledgement*/,
/*22*/ "SYN" /*Synchronous Idle*/,
/*23*/ "ETB" /*End of Transmit Block*/,
/*24*/ "CAN" /*Cancel*/,
/*25*/ "EM" /*End of Medium*/,
/*26*/ "SUB" /*Substitute*/,
/*27*/ "ESC" /*Escape*/,
/*28*/ "FS" /*File Separator*/,
/*29*/ "GS" /*Group Separator*/,
/*30*/ "RS" /*Record Separator*/,
/*31*/ "US" /*Unit Separator*/,
/*32*/ "32SPACE" /*Space*/,
/*33*/ "EXCLA" /*! Exclamation mark*/,
/*34*/ "DQUOTE" /*Double quotes (or speech marks)*/,
/*35*/ "POUND" /*# Number*/,
/*36*/ "DOLLAR" /*$ Dollar*/,
/*37*/ "PERC" /*% Per cent sign*/,
/*38*/ "AMPER" /*&	Ampersand*/,
/*39*/ "SQUOTE" /* ' Single quote*/,
/*40*/ "LP" /*( Open parenthesis (or open bracket)*/,
/*41*/ "RP" /*) Close parenthesis (or close bracket)*/,
/*42*/ "ASTERISK" /** Asterisk*/,
/*43*/ "PLUS" /*+ Plus*/,
/*44*/ "COMMA" /*, Comma*/,
/*45*/ "HYPHEN" /*- Hyphen*/,
/*46*/ "PERIOD" /*. Period, dot or full stop*/,
/*47*/ "SLASH" /*/ Slash or divide*/,
/*48*/ "0" /*Zero*/,
/*49*/ "1" /*One*/,
/*50*/ "2" /*Two*/,
/*51*/ "3" /*Three*/,
/*52*/ "4" /*Four*/,
/*53*/ "5" /*Five*/,
/*54*/ "6" /*Six*/,
/*55*/ "7" /*Seven*/,
/*56*/ "8" /*Eight*/,
/*57*/ "9" /*Nine*/,
/*58*/ "COLON" /*: Colon*/,
/*59*/ "SEMICOL" /*; Semicolon*/,
/*60*/ "LT" /*< Less than (or open angled bracket)*/,
/*61*/ "EQ" /*= Equals*/,
/*62*/ "GT" /*>Greater than (or close angled bracket)*/,
/*63*/ "QMARK" /*? Question mark*/,
/*64*/ "AT" /*@ At symbol*/,
/*65*/ "A" /*Uppercase A*/,
/*66*/ "B" /*Uppercase B*/,
/*67*/ "C" /*Uppercase C*/,
/*68*/ "D" /*Uppercase D*/,
/*69*/ "E" /*Uppercase E*/,
/*70*/ "F" /*Uppercase F*/,
/*71*/ "G" /*Uppercase G*/,
/*72*/ "H" /*Uppercase H*/,
/*73*/ "I" /*Uppercase I*/,
/*74*/ "J" /*Uppercase J*/,
/*75*/ "K" /*Uppercase K*/,
/*76*/ "L" /*Uppercase L*/,
/*77*/ "M" /*Uppercase M*/,
/*78*/ "N" /*Uppercase N*/,
/*79*/ "O" /*Uppercase O*/,
/*80*/ "P" /*Uppercase P*/,
/*81*/ "Q" /*Uppercase Q*/,
/*82*/ "R" /*Uppercase R*/,
/*83*/ "S" /*Uppercase S*/,
/*84*/ "T" /*Uppercase T*/,
/*85*/ "U" /*Uppercase U*/,
/*86*/ "V" /*Uppercase V*/,
/*87*/ "W" /*Uppercase W*/,
/*88*/ "X" /*Uppercase X*/,
/*89*/ "Y" /*Uppercase Y*/,
/*90*/ "Z" /*Uppercase Z*/,
/*91*/ "LBRACKET" /*[ Opening bracket*/,
/*92*/ "BACKSLASH" /*/ Backslash*/,
/*93*/ "RBRACKET" /*] Closing bracket*/,
/*94*/ "CARET" /*^Caret - circumflex*/,
/*95*/ "UNDERSCORE" /*_ Underscore*/,
/*96*/ "GRAVE" /*` Grave accent*/,
/*97*/ "a" /*Lowercase a*/,
/*98*/ "b" /*Lowercase b*/,
/*99*/ "c" /*Lowercase c*/,
/*100*/ "d" /*Lowercase d*/,
/*101*/ "e" /*Lowercase e*/,
/*102*/ "f" /*Lowercase f*/,
/*103*/ "g" /*Lowercase g*/,
/*104*/ "h" /*Lowercase h*/,
/*105*/ "i" /*Lowercase i*/,
/*106*/ "j" /*Lowercase j*/,
/*107*/ "k" /*Lowercase k*/,
/*108*/ "l" /*Lowercase l*/,
/*109*/ "m" /*Lowercase m*/,
/*110*/ "n" /*Lowercase n*/,
/*111*/ "o" /*Lowercase o*/,
/*112*/ "p" /*Lowercase p*/,
/*113*/ "q" /*Lowercase q*/,
/*114*/ "r" /*Lowercase r*/,
/*115*/ "s" /*Lowercase s*/,
/*116*/ "t" /*Lowercase t*/,
/*117*/ "u" /*Lowercase u*/,
/*118*/ "v" /*Lowercase v*/,
/*119*/ "w" /*Lowercase w*/,
/*120*/ "x" /*Lowercase x*/,
/*121*/ "y" /*Lowercase y*/,
/*122*/ "z" /*Lowercase z*/,
/*123*/ "LBRACE" /*{ Opening brace*/,
/*124*/ "VERTBAR" /*| Vertical bar*/,
/*125*/ "RBRACE" /*} Closing brace*/,
/*126*/ "TILDE" /*~ Equivalency sign - tilde*/,
/*127*/ "127" /*Delete*/
};

typedef char char64_t[64];

static const char *get_lemon_token_name(char64_t *buf, const char *tkname)
{
    int ch;
    if(tkname[0] == '\'') {
        if(tkname[2] == '\'') {
            ch = tkname[1];
            if(ch < 127) snprintf(*buf, sizeof(char64_t), "Tk_%s", ascii_names_st[ch]);
            else snprintf(*buf, sizeof(char64_t), "Tk_%d", ch);
        }            
        else if(tkname[3] == '\'') {
            ch = tkname[2];
            switch(ch) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'f': ch = '\f'; break;
                case 'r': ch = '\r'; break;
                case 'v': ch = '\v'; break;
                case '\\': ch = '\\'; break;
                default:
                    ch = -1;
            }
            if(ch < 127) snprintf(*buf, sizeof(char64_t), "Tk_%s", ascii_names_st[ch]);
            else snprintf(*buf, sizeof(char64_t), "Tk_%d", ch);
        } else
            snprintf(*buf, sizeof(char64_t), "Tk_%*s", (int)strlen(tkname)-2, tkname+1);            
    }
    else if(tkname[0] == '"') {
            snprintf(*buf, sizeof(char64_t), "Tk_%s", tkname+1);
            (*buf)[strlen(*buf)-1] = '\0';
    }
    else if(tkname[0] == '.' || tkname[0] == '_') {
            snprintf(*buf, sizeof(char64_t), "Tk_%s", tkname);
            (*buf)[3] = '_';
    }
    else if(islower(tkname[0])) {
            snprintf(*buf, sizeof(char64_t), "Tk_%s", tkname);
    }
    else
        snprintf(*buf, sizeof(char64_t), "%s", tkname);
    for(int i=0; (*buf)[i]; ++i) {
        if((*buf)[i] == '.')
            (*buf)[i] = '_';
    }
    return *buf;
}

static const char *get_lemon_rule_name(char64_t *buf, const char *tkname)
{
    if(tkname[0] == '.') {
            snprintf(*buf, sizeof(char64_t), "x%s", tkname);
            (*buf)[1] = '_';
    }
    else if(tkname[0] == '_') {
            snprintf(*buf, sizeof(char64_t), "z%s", tkname);
    }
    else if(isupper(tkname[0])) {
            snprintf(*buf, sizeof(char64_t), "l_%s", tkname);
    }
    else
        snprintf(*buf, sizeof(char64_t), "%s", tkname);
    for(int i=0; (*buf)[i]; ++i) {
        if((*buf)[i] == '.')
            (*buf)[i] = '_';
    }
    return *buf;
}

static void
print_symbol_prec_assoc_commented(FILE *out, int prec, Value_t assoc)
{
    char assoc_chr;
    switch(assoc) {
        case LEFT: assoc_chr = 'L'; break;
        case RIGHT: assoc_chr = 'R'; break;
        case NONASSOC: assoc_chr = 'N'; break;
        case PRECEDENCE: assoc_chr = 'P'; break;
        default:
            assoc_chr = 'U';
    }
    fprintf(out," /*%d%c*/", prec, assoc_chr);
}

static void
print_symbol_prec_commented(FILE *out, Value_t sym)
{
    if(symbol_prec[sym]) {
        print_symbol_prec_assoc_commented(out, symbol_prec[sym], symbol_assoc[sym]);
    }
}

static const char*
get_prec_name(int sym_id) {
    Value_t assoc = symbol_assoc[sym_id];
    const char *assoc_name;
    switch(assoc) {
        case LEFT:
            assoc_name = "left";
            break;

        case RIGHT:
            assoc_name = "right";
            break;

        case NONASSOC:
            assoc_name = "nonassoc";
            break;

        case PRECEDENCE:
            assoc_name = "precedence";
            break;

        default:
            assoc_name = NULL;
    }
    return assoc_name;
}

static void
print_grammar_lemon(void)
{
    int i, k;
    FILE *f = lemon_file;
    char64_t buf;

    if (!lemon_flag)
	return;

    for (i = 0; i < ntokens; ++i)
    {
        const char *sym_name = symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            if(sym_name[0] != '"')
                fprintf(f, "%%token %s .\n", get_lemon_token_name(&buf, sym_name));
    }
    fprintf(f, "\n");

    for (k = 0; k <= prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < ntokens; ++i)
        {
            Value_t prec = symbol_prec[i];
            if(prec == k) {
                const char *sym_name = symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(i);
                        if(!assoc_name) continue;
                        fprintf(f, "%%%s /*%d*/", assoc_name, prec);
                        sassoc = 1;
                    }
                    fprintf(f, " %s", get_lemon_token_name(&buf, sym_name));
                }
            }
        }
        if(sassoc) {
            fprintf(f, " .\n");
        }
    }
    fprintf(f, "\n%%start_symbol %s\n", get_lemon_rule_name(&buf, goal->name));

    k = 1;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	{
            if (rlhs[i] != rlhs[i - 1]) fprintf(f, "\n");
	    if(!skip_rule)
                fprintf(f, "%s ::=", (i == START_RULE_IDX) ? "x_start_rule_" : get_lemon_rule_name(&buf, sym_name));
	}

        if(!(ritem[k] >= 0)) {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
        else
        {
            while (ritem[k] >= 0)
            {
                Value_t symbol = ritem[k];
                sym_name = symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    bucket *bp = lookup(sym_name);
                    if(bp->class == TERM) {
                        fprintf(f, " %s", get_lemon_token_name(&buf, sym_name));
                        print_symbol_prec_commented(f, symbol);
                    }
                    else
                        fprintf(f, " %s", get_lemon_rule_name(&buf, sym_name));
                }
                ++k;
            }
        }
	++k;
        if(!skip_rule) {
            bucket *prec_bucket = rprec_bucket[i];
            if(prec_bucket) {
                fprintf(f, " . [%s]", get_lemon_token_name(&buf, prec_bucket->name));
                print_symbol_prec_assoc_commented(f, prec_bucket->prec, prec_bucket->assoc);
                fprintf(f, "\n");
            }
            else fprintf(f, " .\n");
        }
    }
}

static void
print_grammar_naked(void)
{
    int i, k;
    FILE *f = naked_file;

    if (!naked_flag)
	return;

    for (i = 0; i < ntokens; ++i)
    {
        const char *sym_name = symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            fprintf(f, "%%token %s\n", sym_name);
    }
    fprintf(f, "\n");

    for (k = 0; k <= prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < ntokens; ++i)
        {
            Value_t prec = symbol_prec[i];
            if(prec == k) {
                const char *sym_name = symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(i);
                        if(!assoc_name) continue;
                        fprintf(f, "%%%s /*%d*/", assoc_name, prec);
                        sassoc = 1;
                    }
                    fprintf(f, " %s", sym_name);
                }
            }
        }
        if(sassoc) {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "\n%%start %s\n", goal->name);
    fprintf(f, "\n%%%%\n\n");

    k = 1;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (rlhs[i] != rlhs[i - 1])
	{
	    if (i > START_RULE_IDX+1 && !skip_rule) /*+1 to not print ';' at the beginning*/
		fprintf(f, "\t;\n");
	    if(!skip_rule)
        	fprintf(f, "%s :\n\t", (i == START_RULE_IDX) ? "x_start_rule_" : sym_name);
	}
	else
	{
	    fprintf(f, "\t|");
	}

        if(ritem[k] >= 0) {
            while (ritem[k] >= 0)
            {
                Value_t symbol = ritem[k];
                sym_name = symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    fprintf(f, " %s", sym_name);
                    print_symbol_prec_commented(f, symbol);
                }
                ++k;
            }
        }
        else
        {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
	++k;
        if(!skip_rule) {
            bucket *prec_bucket = rprec_bucket[i];
            if(prec_bucket) {
                fprintf(f, " %%prec %s", prec_bucket->name);
                print_symbol_prec_assoc_commented(f, prec_bucket->prec, prec_bucket->assoc);
                fprintf(f, "\n");
            }
            else fprintf(f, "\n");
        }
    }
    fprintf(f, "\t;\n"); /*last rule*/
}

static const char *get_normalized_rule_name(char64_t *buf, const char *tkname)
{
    snprintf(*buf, sizeof(char64_t), "%s", tkname);
    for(int i=0; (*buf)[i]; ++i) {
        if((*buf)[i] == '.')
            (*buf)[i] = '_';
    }
    return *buf;
}


static void
print_grammar_unicc(void)
{
    int b, i, k;
    FILE *f = unicc_file;
    char64_t buf;

    if (!unicc_flag)
	return;

    fprintf(f, "#!mode insensitive ;\n\n");
/*
    for (i = 0; i < ntokens; ++i)
    {
        const char *sym_name = symbol_name[i];
        if(!(sym_name[0] == '$' || sym_name[0] == '\'' || strcmp(sym_name, "error") == 0))
            fprintf(f, "@%s '%s';\n", sym_name, sym_name);
    }
    fprintf(f, "\n");
*/
    for (k = 0; k <= prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < ntokens; ++i)
        {
            Value_t prec = symbol_prec[i];
            if(prec == k) {
                const char *sym_name = symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(i);
                        if(!assoc_name) continue;
                        fprintf(f, "#%s /*%d*/", assoc_name, prec);
                        sassoc = 1;
                    }
                    if(sym_name[0] == '\'')
                        fprintf(f, " %s", sym_name);
                    else
                        fprintf(f, " \"%s\"", sym_name);
                }
            }
        }
        if(sassoc) {
            fprintf(f, " ;\n");
        }
    }
    fprintf(f, "\n");
    //fprintf(f, "\n%%start %s\n", goal->name);
    //fprintf(f, "\n%%%%\n\n");

    k = 1;
    b = 0;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (rlhs[i] != rlhs[i - 1])
	{
	    if (i > START_RULE_IDX+1 && !skip_rule) /*+1 to not print ';' at the beginning*/
		fprintf(f, "\t;\n");
	    if(!skip_rule) {
                if(b == 0 && strcmp(goal->name, sym_name) == 0) {
                    ++b;
                    fprintf(f, "%s$ :\n\t", get_normalized_rule_name(&buf, sym_name));
                }
        	else fprintf(f, "%s :\n\t", (i == START_RULE_IDX) ? "x_start_rule_" : 
                    get_normalized_rule_name(&buf, sym_name));
            }
	}
	else
	{
	    fprintf(f, "\t|");
	}

        if(ritem[k] >= 0) {
            while (ritem[k] >= 0)
            {
                Value_t symbol = ritem[k];
                sym_name = symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    bucket *bp = lookup(sym_name);
                    if(bp->class == TERM) {
                        if(sym_name[0] == '\'')
                            fprintf(f, " %s", sym_name);
                        else
                            fprintf(f, " \"%s\"", sym_name);
                        print_symbol_prec_commented(f, symbol);
                    }
                    else
                        fprintf(f, " %s", get_normalized_rule_name(&buf, sym_name));
                    print_symbol_prec_commented(f, symbol);
                }
                ++k;
            }
        }
        else
        {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
	++k;
        if(!skip_rule) {
            bucket *prec_bucket = rprec_bucket[i];
            if(prec_bucket) {
                if(prec_bucket->name[0] == '\'')
                    fprintf(f, " #precedence %s", prec_bucket->name);
                else
                    fprintf(f, " #precedence @%s", prec_bucket->name);
                print_symbol_prec_assoc_commented(f, prec_bucket->prec, prec_bucket->assoc);
                fprintf(f, "\n");
            }
            else fprintf(f, "\n");
        }
    }
    fprintf(f, "\t;\n"); /*last rule*/
}

static void
print_grammar_carburetta(void)
{
    int b, i, k;
    FILE *f = carburetta_file;
    char64_t buf;

    if (!carburetta_flag)
	return;

    for (i = 0; i < ntokens; ++i)
    {
        const char *sym_name = symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            fprintf(f, "%%token %s\n", get_lemon_token_name(&buf, sym_name));
    }
    fprintf(f, "\n");
    
    //fprintf(f, "\n%%start %s\n", goal->name);
    fprintf(f, "\n%%grammar%%\n\n");
    for (b = 0, i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (rlhs[i] != rlhs[i - 1])
	{
	    if(!skip_rule) {
                bucket *bp = lookup(sym_name);
                if(bp->class == TERM) continue;
                if(!b) {
                    fprintf(f, "%%nt");
                }
        	fprintf(f, " %s", get_normalized_rule_name(&buf, sym_name));
                ++b;
                if((b % 5) == 0)
                    fprintf(f, "\n%%nt");
            }
	}
    }
    if(b)
        fprintf(f, "\n");
    fprintf(f, "\n");

    k = 1;
    for (i = START_RULE_IDX; i < nrules; ++i)
    {
        const char *sym_name = get_normalized_rule_name(&buf, symbol_name[rlhs[i]]);
        int skip_rule = sym_name[0] == '$';
	if (rlhs[i] != rlhs[i - 1])
	{
	    if (i > START_RULE_IDX+1 && !skip_rule) /*+1 to not print ';' at the beginning*/
		fprintf(f, "\t;\n");
	    if(!skip_rule)
        	fprintf(f, "%s :\n\t", (i == START_RULE_IDX) ? "x_start_rule_" : sym_name);
	}
	else
	{
	    fprintf(f, "\t|");
	}

        if(ritem[k] >= 0) {
            while (ritem[k] >= 0)
            {
                Value_t symbol = ritem[k];
                sym_name = get_normalized_rule_name(&buf, symbol_name[symbol]);
                if(!skip_rule && sym_name[0] != '$') {
                    fprintf(f, " %s", sym_name);
                    print_symbol_prec_commented(f, symbol);
                }
                ++k;
            }
        }
        else
        {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
	++k;
        if(!skip_rule) {
            bucket *prec_bucket = rprec_bucket[i];
            if(prec_bucket) {
                fprintf(f, " //%%prec %s", prec_bucket->name);
                print_symbol_prec_assoc_commented(f, prec_bucket->prec, prec_bucket->assoc);
                fprintf(f, "\n");
            }
            else fprintf(f, "\n");
        }
    }
    fprintf(f, "\t;\n"); /*last rule*/
}

static const char *escape_sql(char64_t *buf, const char *tkname)
{
    int i, i2;
    for(i=0, i2 = 0; i<(sizeof(char64_t)-1) && tkname[i2]; ++i, ++i2) {
        switch(tkname[i2]) {
            case '\'':
                (*buf)[i++] = tkname[i2];
                (*buf)[i] = tkname[i2];
                break;
            default:
            (*buf)[i] = tkname[i2];
        }
    }
    (*buf)[i] = '\0';
    return *buf;
}

static void
print_grammar_sql(void)
{
    int b, i, j, k;
    FILE *f = sql_file;
    bucket *bp;
    char64_t buf;

    if (!sql_flag)
	return;

    fprintf(f,
        "BEGIN;\n"
        "CREATE TABLE symbol(\n"
        "  id INTEGER PRIMARY KEY,\n"
        "  name TEXT NOT NULL,\n"
        "  isTerminal BOOLEAN NOT NULL,\n"
        "  fallback INTEGER REFERENCES symbol"
                " DEFERRABLE INITIALLY DEFERRED\n"
        ");\n"
    );

    for (bp = first_symbol, b = 0; bp; bp = bp->next)
    {
        switch(bp->class) {
            case TERM:
            case NONTERM:
                if(b==0) {
                  fprintf(f,
                     "INSERT INTO symbol(id,name,isTerminal)"
                     " VALUES\n");
                }
                fprintf(f,
                   " %s(%d,'%s',%s)\n",
                   (b>0) ? "," : " ", bp->index, escape_sql(&buf, bp->name),
                   (bp->class == TERM) ? "TRUE" : "FALSE"
                );
                ++b;
                break;
        }
    }
    if(b>0) fprintf(f, " ;\n");
    fprintf(f, "\n");

    fprintf(f,
        "CREATE TABLE precedence(\n"
        "  id INTEGER PRIMARY KEY REFERENCES symbol"
                " DEFERRABLE INITIALLY DEFERRED,\n"
        "  prec INTEGER NOT NULL,\n"
        "  assoc CHAR NOT NULL CHECK(assoc IN('L','R','N','P'))\n"
        ");\n"
    );

    for (k = 0, b = 0; k <= prec; ++k) {
        for (i = 0; i < ntokens; ++i)
        {
            Value_t prec = symbol_prec[i];
            if(prec == k && prec > 0) {
                const char *sym_name = symbol_name[i];
                if(sym_name[0] != '$') {
                    if(b==0) {
                        fprintf(f,
                           "INSERT INTO precedence(id,prec,assoc)"
                           " VALUES\n");
                    }
                    const char *assoc_name = get_prec_name(i);
                    fprintf(f,
                       " %s(%d,%d,'%c')",
                       (b > 0) ? "," : " ", i, prec, toupper(assoc_name[0])
                    );
                    ++b;
                    if((b%4) == 0) fprintf(f, "\n");
                }
            }
        }
    }
    if(b>0) fprintf(f, " ;\n");
    fprintf(f, "\n");

    fprintf(f,
        "CREATE TABLE rule(\n"
        "  ruleid INTEGER PRIMARY KEY,\n"
        "  lhs INTEGER REFERENCES symbol(id),\n"
        "  prec_id INTEGER REFERENCES symbol(id)\n"
        ");\n"
    );

    for (i = START_RULE_IDX, b = 0; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        if(sym_name[0] != '$') {
            if(b==0) {
              fprintf(f,
                "INSERT INTO rule(ruleid,lhs,prec_id) VALUES\n");
            }
            fprintf(f,
              " %s(%d,%d,",
              (b > 0) ? "," : " ", i, rlhs[i]
            );
            bucket *prec_bucket = rprec_bucket[i];
            if(prec_bucket && prec_bucket->prec_on_decl) {
                fprintf(f,"%d)", prec_bucket->index);
            }
            else fprintf(f,"NULL)");
            ++b;
            if((b%4) == 0) fprintf(f, "\n");
        }
    }
    if(b>0) fprintf(f, " ;\n");
    fprintf(f, "\n");

    fprintf(f,
        "CREATE TABLE directives(\n"
        "  id INTEGER PRIMARY KEY CHECK(id = 1),\n"
        "  stack_size TEXT,\n"
        "  start_symbol INTEGER REFERENCES symbol,\n"
        "  wildcard INTEGER REFERENCES symbol\n"
        ");\n"
        "INSERT INTO directives(id) values(1);\n"
    );

    fprintf(f,
        "UPDATE directives SET start_symbol=%d;\n",
            goal->index
    );
    
    fprintf(f,
        "CREATE TABLE rulerhs(\n"
        "  id INTEGER PRIMARY KEY,"
        "  ruleid INTEGER REFERENCES rule(ruleid),\n"
        "  pos INTEGER,\n"
        "  sym INTEGER REFERENCES symbol(id)\n"
        ");\n"
    );
    
    k = 1;
    for (i = START_RULE_IDX, b = 0; i < nrules; ++i)
    {
        const char *sym_name = symbol_name[rlhs[i]];
        int skip_rule = sym_name[0] == '$';
        j = 0;
        while (ritem[k] >= 0)
        {
            Value_t symbol = ritem[k];
            sym_name = symbol_name[symbol];
            if(!skip_rule && sym_name[0] != '$') {
                if(b==0) {
                  fprintf(f,
                    "INSERT INTO rulerhs(ruleid,pos,sym) VALUES\n");
                }
                fprintf(f,
                  " %s(%d,%d,%d)",
                  (b > 0) ? "," : " ", i, j++, symbol
                );
                ++b;
                if((b%5) == 0) fprintf(f, "\n");
            }
            ++k;
        }
        ++k;
    }
    if(b>0) fprintf(f, " ;\n");
    fprintf(f, "\n");

    fprintf(f,
        "CREATE VIEW rule_view AS\n"
        "SELECT srule || (case when lhs <> lhs2 then '\n"
        "' else '' end) FROM (\n"
        "  SELECT s.name || ' : ' || ifnull((\n"
        "    SELECT group_concat(rs.name, ' ')\n"
        "    FROM rulerhs AS rr LEFT JOIN symbol AS rs ON rr.sym=rs.id\n"
        "    WHERE rr.ruleid=r.ruleid\n"
        "    ORDER BY rr.pos), '/*empty*/')\n"
        "      || ifnull((' %%prec ' || sp.name), '') || ' ;' as srule,\n"
        "    r.lhs, lead(r.lhs) OVER(ORDER BY r.ruleid) AS lhs2\n"
        "  FROM rule AS r\n"
        "    LEFT JOIN symbol AS s ON r.lhs=s.id\n"
        "    LEFT JOIN symbol AS sp ON r.prec_id=sp.id\n"
        "  ORDER BY r.ruleid\n"
        ") t;\n"
        "CREATE VIEW token_view AS\n"
        "SELECT ('%%token ' || '\n"
        "  ' || group_concat(name || (CASE WHEN (cnt %% 6) = 0 THEN '\n"
        "  ' ELSE ' ' END), '') || '\n"
        "  ') AS val FROM (\n"
        "  SELECT row_number() OVER (ORDER BY s.id) AS cnt, s.name\n"
        "  FROM symbol AS s WHERE isTerminal=TRUE\n"
        "  ORDER BY id\n"
        ") t;\n"
        "CREATE VIEW prec_view AS\n"
        "WITH prec(id, name) AS (VALUES('L','%%left'),('R','%%right'),('N','%%nonassoc'),('P','%%precedence'))\n"
        "SELECT prec.name || ' ' || group_concat(s.name, ' ')\n"
        "FROM precedence AS p\n"
        "	LEFT JOIN prec ON p.assoc=prec.id\n"
        "	LEFT JOIN symbol AS s on p.id=s.id\n"
        "GROUP BY p.prec\n"
        "ORDER BY p.prec;\n"
        "CREATE VIEW grammar_view AS\n"
        "SELECT * FROM token_view\n"
        "UNION ALL\n"
        "SELECT * FROM prec_view\n"
        "UNION ALL\n"
        "SELECT '\n"
        "%%start ' || s.name || '\n\n%%%%\n'\n"
        "FROM directives AS d join symbol AS s ON d.start_symbol=s.id\n"
        "UNION ALL\n"
        "SELECT * FROM rule_view;\n"
        "COMMIT;\n"
        "--select * from grammar_view;\n"
    );
    
}

#if defined(YYBTYACC)
static void
finalize_destructors(void)
{
    int i;
    bucket *bp;

    for (i = 2; i < nsyms; ++i)
    {
	char *tag = symbol_type_tag[i];

	if (symbol_destructor[i] == NULL)
	{
	    if (tag == NULL)
	    {			/* use <> destructor, if there is one */
		if ((bp = default_destructor[UNTYPED_DEFAULT]) != NULL)
		{
		    symbol_destructor[i] = TMALLOC(char,
						   strlen(bp->destructor) + 1);
		    NO_SPACE(symbol_destructor[i]);
		    strcpy(symbol_destructor[i], bp->destructor);
		}
	    }
	    else
	    {			/* use type destructor for this tag, if there is one */
		bp = lookup_type_destructor(tag);
		if (bp->destructor != NULL)
		{
		    symbol_destructor[i] = TMALLOC(char,
						   strlen(bp->destructor) + 1);
		    NO_SPACE(symbol_destructor[i]);
		    strcpy(symbol_destructor[i], bp->destructor);
		}
		else
		{		/* use <*> destructor, if there is one */
		    if ((bp = default_destructor[TYPED_DEFAULT]) != NULL)
			/* replace "$$" with "(*val).tag" in destructor code */
			symbol_destructor[i]
			    = process_destructor_XX(bp->destructor, tag);
		}
	    }
	}
	else
	{			/* replace "$$" with "(*val)[.tag]" in destructor code */
	    char *destructor_source = symbol_destructor[i];
	    symbol_destructor[i]
		= process_destructor_XX(destructor_source, tag);
	    FREE(destructor_source);
	}
    }
    /* 'symbol_type_tag[]' elements are freed by 'free_tags()' */
    DO_FREE(symbol_type_tag);	/* no longer needed */
    if ((bp = default_destructor[UNTYPED_DEFAULT]) != NULL)
    {
	FREE(bp->name);
	/* 'bp->tag' is a static value, don't free */
	FREE(bp->destructor);
	FREE(bp);
    }
    if ((bp = default_destructor[TYPED_DEFAULT]) != NULL)
    {
	FREE(bp->name);
	/* 'bp->tag' is a static value, don't free */
	FREE(bp->destructor);
	FREE(bp);
    }
    if ((bp = default_destructor[TYPE_SPECIFIED]) != NULL)
    {
	bucket *p;
	for (; bp; bp = p)
	{
	    p = bp->link;
	    FREE(bp->name);
	    /* 'bp->tag' freed by 'free_tags()' */
	    FREE(bp->destructor);
	    FREE(bp);
	}
    }
}
#endif /* defined(YYBTYACC) */

void
reader(void)
{
    write_section(code_file, banner);
    create_symbol_table();
    read_declarations();
    read_grammar();
    pack_names();
    check_symbols();
    pack_symbols();
    pack_grammar();
    print_grammar_lemon(); // need be before free_symbols
    print_grammar_naked(); // need be before free_symbols
    print_grammar_unicc(); // need be before free_symbols
    print_grammar_carburetta(); // need be before free_symbols
    print_grammar_sql(); // need be before free_symbols
    free_symbol_table();
    free_symbols();
    print_grammar();
    print_grammar_ebnf();
#if defined(YYBTYACC)
    if (destructor)
	finalize_destructors();
#endif
    free_tags();
}

#ifdef NO_LEAKS
static param *
free_declarations(param *list)
{
    while (list != 0)
    {
	param *next = list->next;
	free(list->type);
	free(list->name);
	free(list->type2);
	free(list);
	list = next;
    }
    return list;
}

void
reader_leaks(void)
{
    lex_param = free_declarations(lex_param);
    parse_param = free_declarations(parse_param);

    DO_FREE(line);
    DO_FREE(rrhs);
    DO_FREE(rlhs);
    DO_FREE(rprec);
    DO_FREE(ritem);
    DO_FREE(rassoc);
    DO_FREE(cache);
    DO_FREE(name_pool);
    DO_FREE(symbol_name);
    DO_FREE(symbol_prec);
    DO_FREE(symbol_assoc);
    DO_FREE(symbol_value);
#if defined(YYBTYACC)
    DO_FREE(symbol_pval);
    DO_FREE(symbol_destructor);
    DO_FREE(symbol_type_tag);
#endif
}
#endif
