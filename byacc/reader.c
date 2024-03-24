/* $Id: reader.c,v 1.104 2023/05/18 21:18:17 tom Exp $ */

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

/* limit the size of optional names for %union */
#define NAME_LEN 32

#define IS_ALNUM(c) (isalnum(c) || (c) == '_')

#define begin_case(f,n) fprintf(f, "case %d:\n", (int)(n))

#define end_case(f) \
	    fprintf(f, "\n"); \
	    fprintf_lineno(f, 1, ""); \
	    fprintf(f, "break;\n")

#define begin_ainfo(data, offset) do { \
	    data.a_lineno = S->lineno; \
	    data.a_line = dup_line(S); \
	    data.a_cptr = data.a_line + (S->cptr - S->line - offset); \
	} while (0)

#define end_ainfo(data) do { \
	    FREE(data.a_line); \
	    memset(&data, 0, sizeof(data)); \
	} while (0)

static void start_rule(byacc_t* S, bucket *bp, int s_lineno);
#if defined(YYBTYACC)
static void copy_initial_action(byacc_t* S);
static void copy_destructor(byacc_t* S);
static char *process_destructor_XX(byacc_t* S, char *code, char *tag);
#endif

#define CACHE_SIZE 256

const char *ygv_line_format = "#line %d \"%s\"\n";


static const char *ygv_code_keys[] =
{
    "", "requires", "provides", "top", "imports",
};

#if defined(YYBTYACC)

#define UNTYPED_DEFAULT 0
#define TYPED_DEFAULT   1
#define TYPE_SPECIFIED  2

static bucket *
lookup_type_destructor(byacc_t* S, char *tag)
{
    const char fmt[] = "%.*s destructor";
    char name[1024] = "\0";
    bucket *bp, **bpp = &S->default_destructor[TYPE_SPECIFIED];

    while ((bp = *bpp) != NULL)
    {
	if (bp->tag == tag)
	    return (bp);
	bpp = &bp->link;
    }

    sprintf(name, fmt, (int)(sizeof(name) - sizeof(fmt)), tag);
    *bpp = bp = make_bucket(S, name);
    bp->tag = tag;

    return (bp);
}
#endif /* defined(YYBTYACC) */

static void
cachec(byacc_t* S, int c)
{
    assert(S->cinc >= 0);
    if (S->cinc >= S->cache_size)
    {
	S->cache_size += CACHE_SIZE;
	S->cache = TREALLOC(char, S->cache, S->cache_size);
	NO_SPACE(S->cache);
    }
    S->cache[S->cinc] = (char)c;
    ++S->cinc;
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
line_directive(byacc_t* S)
{
#define UNLESS(what) if (what) { ld = ldERR; break; }
    int n;
    int line_1st = -1;
    int name_1st = -1;
    int name_end = -1;
    LINE_DIR ld = ldSPC1;
    for (n = 0; (ld <= ldOK) && (S->line[n] != '\0'); ++n)
    {
	int ch = UCH(S->line[n]);
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
	    UNLESS(strncmp(S->line + n, "line", 4));
	    n += 4;
	    if (S->line[n] == '\0')
	    {
		ld = ldOK;
		break;
	    }
	    else
		UNLESS(!isspace(UCH(S->line[n])));
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
	    UNLESS(S->line[n + 1] == '"');
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
	if ((long)need > (long)S->input_file_name_len)
	{
	    S->input_file_name_len = ((need + 1) * 3) / 2;
	    S->input_file_name = TREALLOC(char, S->input_file_name, S->input_file_name_len);
	    NO_SPACE(S->input_file_name);
	}
	if ((long)need > 0)
	{
	    memcpy(S->input_file_name, S->line + name_1st + 1, need - 1);
	    S->input_file_name[need - 1] = '\0';
	}
	else
	{
	    S->input_file_name[0] = '\0';
	}
    }

    if (ld >= ldNUM && ld < ldERR)
    {
	if (line_1st >= 0)
	{
	    S->lineno = (int)strtol(S->line + line_1st, NULL, 10) - 1;
	}
	else
	{
	    S->lineno = 0;
	}
    }

    return (ld == ldOK);
#undef UNLESS
}

static void
save_line(byacc_t* S)
{
    /* remember to save the input-line if we call get_line() */
    if (!S->must_save)
    {
	S->must_save = 1;
	S->save_area.line_used = (size_t)(S->cptr - S->line);
    }
}

static void
restore_line(byacc_t* S)
{
    /* if we saved the line, restore it */
    if (S->must_save < 0)
    {
	free(S->line);
	S->line = S->save_area.line_data;
	S->cptr = S->save_area.line_used + S->line;
	S->linesize = S->save_area.line_size;
	if (fsetpos(S->input_file, &S->save_area.line_fpos) != 0)
	    on_error(S);
	memset(&S->save_area, 0, sizeof(S->save_area));
    }
    else if (S->must_save > 0)
    {
	S->cptr = S->line + S->save_area.line_used;
    }
    S->must_save = 0;
}

static void
get_line(byacc_t* S)
{
    FILE *f = S->input_file;

    if (S->must_save > 0)
    {
	S->save_area.line_data = TMALLOC(char, S->linesize);
	S->save_area.line_used = (size_t)(S->cptr - S->line);
	S->save_area.line_size = S->linesize;
	NO_SPACE(S->save_area.line_data);
	memcpy(S->save_area.line_data, S->line, S->linesize);
	if (fgetpos(f, &S->save_area.line_fpos) != 0)
	    on_error(S);
	S->must_save = -S->must_save;
    }

    do
    {
	int c;
	size_t i;

	if (S->saw_eof || (c = getc(f)) == EOF)
	{
	    if (S->line)
	    {
		FREE(S->line);
		S->line = 0;
	    }
	    S->cptr = 0;
	    S->saw_eof = 1;
	    return;
	}

	if (S->line == NULL || S->linesize != (LINESIZE + 1))
	{
	    if (S->line)
		FREE(S->line);
	    S->linesize = LINESIZE + 1;
	    S->line = TMALLOC(char, S->linesize);
	    NO_SPACE(S->line);
	}

	i = 0;
	++S->lineno;
	for (;;)
	{
	    S->line[i++] = (char)c;
	    if (c == '\n')
		break;
	    if ((i + 3) >= S->linesize)
	    {
		S->linesize += LINESIZE;
		S->line = TREALLOC(char, S->line, S->linesize);
		NO_SPACE(S->line);
	    }
	    c = getc(f);
	    if (c == EOF)
	    {
		S->line[i++] = '\n';
		S->saw_eof = 1;
		break;
	    }
	}
	S->line[i] = '\0';
    }
    while (line_directive(S));
    S->cptr = S->line;
    return;
}

static char *
dup_line(byacc_t* S)
{
    char *p, *s, *t;

    if (S->line == NULL)
	return (NULL);
    s = S->line;
    while (*s != '\n')
	++s;
    p = TMALLOC(char, s - S->line + 1);
    NO_SPACE(p);

    s = S->line;
    t = p;
    while ((*t++ = *s++) != '\n')
	continue;
    return (p);
}

static void
skip_comment(byacc_t* S)
{
    char *s;
    struct ainfo a;

    begin_ainfo(a, 0);

    s = S->cptr + 2;
    for (;;)
    {
	if (*s == '*' && s[1] == '/')
	{
	    S->cptr = s + 2;
	    end_ainfo(a);
	    return;
	}
	if (*s == '\n')
	{
	    get_line(S);
	    if (S->line == NULL)
		unterminated_comment(S, &a);
	    s = S->cptr;
	}
	else
	    ++s;
    }
}

static int
next_inline(byacc_t* S)
{
    char *s;

    if (S->line == NULL)
    {
	get_line(S);
	if (S->line == NULL)
	    return (EOF);
    }

    s = S->cptr;
    for (;;)
    {
	switch (*s)
	{
	case '/':
	    if (s[1] == '*')
	    {
		S->cptr = s;
		skip_comment(S);
		s = S->cptr;
		break;
	    }
	    else if (s[1] == '/')
	    {
		get_line(S);
		if (S->line == NULL)
		    return (EOF);
		s = S->cptr;
		break;
	    }
	    /* FALLTHRU */

	default:
	    S->cptr = s;
	    return (*s);
	}
    }
}

static int
nextc(byacc_t* S)
{
    int ch;
    int finish = 0;

    do
    {
	switch (ch = next_inline(S))
	{
	case '\n':
	    get_line(S);
	    break;
	case ' ':
	case '\t':
	case '\f':
	case '\r':
	case '\v':
	case ',':
	case ';':
	    ++S->cptr;
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
struct keyword
{
    char name[16];
    int token;
};
static struct keyword ygv_keywords[] = {
    { "binary",      NONASSOC },
    { "code",        XCODE },
    { "debug",       NONPOSIX_DEBUG },
    { "define",      HACK_DEFINE },
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
    { "nterm",       TYPE },
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
keyword(byacc_t* S)
{
    int c;
    char *t_cptr = S->cptr;

    c = *++S->cptr;
    if (isalpha(UCH(c)))
    {
	struct keyword *key;

	S->cinc = 0;
	for (;;)
	{
	    if (isalpha(UCH(c)))
	    {
		if (isupper(UCH(c)))
		    c = tolower(c);
		cachec(S, c);
	    }
	    else if (isdigit(UCH(c))
		     || c == '-'
		     || c == '.'
		     || c == '$')
	    {
		cachec(S, c);
	    }
	    else if (c == '_')
	    {
		/* treat keywords spelled with '_' as if it were '-' */
		cachec(S, '-');
	    }
	    else
	    {
		break;
	    }
	    c = *++S->cptr;
	}
	cachec(S, NUL);

	if ((key = bsearch(S->cache, ygv_keywords,
			   sizeof(ygv_keywords) / sizeof(*key),
			   sizeof(*key), compare_keys)))
	    return key->token;
    }
    else
    {
	++S->cptr;
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
    syntax_error(S, S->lineno, S->line, t_cptr);
    /*NOTREACHED */
}

static void
copy_ident(byacc_t* S)
{
    int c;
    FILE *f = S->output_file;

    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (c != '"')
	syntax_error(S, S->lineno, S->line, S->cptr);
    ++S->outline;
    fprintf(f, "#ident \"");
    for (;;)
    {
	c = *++S->cptr;
	if (c == '\n')
	{
	    fprintf(f, "\"\n");
	    return;
	}
	putc(c, f);
	if (c == '"')
	{
	    putc('\n', f);
	    ++S->cptr;
	    return;
	}
    }
}

static char *
copy_string(byacc_t* S, int quote)
{
    struct mstring *temp = msnew();
    struct ainfo a;

    begin_ainfo(a, 1);

    for (;;)
    {
	int c = *S->cptr++;

	mputc(temp, c);
	if (c == quote)
	{
	    end_ainfo(a);
	    return msdone(temp);
	}
	if (c == '\n')
	    unterminated_string(S, &a);
	if (c == '\\')
	{
	    c = *S->cptr++;
	    mputc(temp, c);
	    if (c == '\n')
	    {
		get_line(S);
		if (S->line == NULL)
		    unterminated_string(S, &a);
	    }
	}
    }
}

static char *
copy_comment(byacc_t* S)
{
    struct mstring *temp = msnew();
    int c;

    c = *S->cptr;
    if (c == '/')
    {
	mputc(temp, '*');
	while ((c = *++S->cptr) != '\n')
	{
	    mputc(temp, c);
	    if (c == '*' && S->cptr[1] == '/')
		mputc(temp, ' ');
	}
	mputc(temp, '*');
	mputc(temp, '/');
    }
    else if (c == '*')
    {
	struct ainfo a;

	begin_ainfo(a, 1);

	mputc(temp, c);
	++S->cptr;
	for (;;)
	{
	    c = *S->cptr++;
	    mputc(temp, c);
	    if (c == '*' && *S->cptr == '/')
	    {
		mputc(temp, '/');
		++S->cptr;
		end_ainfo(a);
		return msdone(temp);
	    }
	    if (c == '\n')
	    {
		get_line(S);
		if (S->line == NULL)
		    unterminated_comment(S, &a);
	    }
	}
    }
    return msdone(temp);
}

static int
check_key(byacc_t* S, int pos)
{
    const char *key = ygv_code_keys[pos];
    while (*S->cptr && *key)
	if (*key++ != *S->cptr++)
	    return 0;
    if (*key || (!isspace(UCH(*S->cptr)) && *S->cptr != L_CURL))
	return 0;
    S->cptr--;
    return 1;
}

static void
copy_code(byacc_t* S)
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
	c = *++S->cptr;
	if (c == EOF)
	    unexpected_EOF(S);
        if (c == '\0') {
	    get_line(S);
	    if (S->line == NULL)
	    {
		unexpected_EOF(S);
		/*NOTREACHED */
	    }
	    c = *S->cptr;
        }
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

	    if (pos == -1 || !check_key(S, pos))
	    {
		syntax_error(S, S->lineno, S->line, S->cptr);
		/*NOTREACHED */
	    }
	}
    }

    S->cptr++;			/* skip initial curl */
    while (*S->cptr && isspace(UCH(*S->cptr)))	/* skip space */
	S->cptr++;
    curl = 1;			/* nesting count */

    /* gather text */
    S->code_lines[pos].name = ygv_code_keys[pos];
    if ((cline = (int)S->code_lines[pos].num) != 0)
    {
	code_mstr = msrenew(S->code_lines[pos].lines);
    }
    else
    {
	code_mstr = msnew();
    }
    cline++;
    if (!S->lflag)
	msprintf(S, code_mstr, S->line_format, S->lineno, S->input_file_name);
    for (;;)
    {
	c = *S->cptr++;
	switch (c)
	{
	case '\0':
	    get_line(S);
	    if (S->line == NULL)
	    {
		unexpected_EOF(S);
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
		S->code_lines[pos].lines = msdone(code_mstr);
		S->code_lines[pos].num = (size_t)cline;
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
copy_text(byacc_t* S)
{
    int c;
    FILE *f = S->text_file;
    int need_newline = 0;
    struct ainfo a;

    begin_ainfo(a, 2);

    if (*S->cptr == '\n')
    {
	get_line(S);
	if (S->line == NULL)
	    unterminated_text(S, &a);
    }
    fprintf_lineno(f, S->lineno, S->input_file_name);

  loop:
    c = *S->cptr++;
    switch (c)
    {
    case '\n':
	putc('\n', f);
	need_newline = 0;
	get_line(S);
	if (S->line)
	    goto loop;
	unterminated_text(S, &a);

    case '\'':
    case '"':
	putc(c, f);
	{
	    char *s = copy_string(S, c);
	    fputs(s, f);
	    free(s);
	}
	need_newline = 1;
	goto loop;

    case '/':
	putc(c, f);
	{
	    char *s = copy_comment(S);
	    fputs(s, f);
	    free(s);
	}
	need_newline = 1;
	goto loop;

    case '%':
    case '\\':
	if (*S->cptr == R_CURL)
	{
	    if (need_newline)
		putc('\n', f);
	    ++S->cptr;
	    end_ainfo(a);
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
puts_both(byacc_t* S, const char *s)
{
    if (s && *s)
    {
        fputs(s, S->text_file);
        if (S->dflag)
	    fputs(s, S->union_file);
    }
}

static void
putc_both(byacc_t* S, int c)
{
    putc(c, S->text_file);
    if (S->dflag)
	putc(c, S->union_file);
}

static void
copy_union(byacc_t* S)
{
    int c;
    int depth;
    struct ainfo a;
    char prefix_buf[NAME_LEN + 1];
    size_t prefix_len = 0;
    char filler_buf[NAME_LEN + 1];
    size_t filler_len = 0;
    int in_prefix = 1;

    prefix_buf[0] = '\0';
    filler_buf[0] = '\0';

    begin_ainfo(a, 6);

    if (S->unionized)
	over_unionized(S, S->cptr - 6);
    S->unionized = 1;

    puts_both(S, "#ifdef YYSTYPE\n");
    puts_both(S, "#undef  YYSTYPE_IS_DECLARED\n");
    puts_both(S, "#define YYSTYPE_IS_DECLARED 1\n");
    puts_both(S, "#endif\n");
    puts_both(S, "#ifndef YYSTYPE_IS_DECLARED\n");
    puts_both(S, "#define YYSTYPE_IS_DECLARED 1\n");

    fprintf_lineno(S->text_file, S->lineno, S->input_file_name);

    depth = 0;
  loop:
    c = *S->cptr++;
    if (in_prefix)
    {
	if (c == L_CURL)
	{
	    in_prefix = 0;
	    if (prefix_len)
	    {
		puts_both(S, "union ");
		puts_both(S, prefix_buf);
		puts_both(S, filler_buf);
	    }
	    else
	    {
		puts_both(S, "typedef union YYSTYPE");
		puts_both(S, filler_buf);
	    }
	}
	else if (isspace(c))
	{
	    if (filler_len >= sizeof(filler_buf) - 1)
	    {
		puts_both(S, filler_buf);
		filler_len = 0;
	    }
	    filler_buf[filler_len++] = (char)c;
	    filler_buf[filler_len] = 0;
	    if (c != '\n')
		goto loop;
	}
	else if (IS_IDENT(c))
	{
	    if (prefix_len < NAME_LEN)
	    {
		prefix_buf[prefix_len++] = (char)c;
		prefix_buf[prefix_len] = 0;
	    }
	    goto loop;
	}
    }
    if (c != '\n' || !in_prefix)
	putc_both(S, c);
    switch (c)
    {
    case '\n':
	get_line(S);
	if (S->line == NULL)
	    unterminated_union(S, &a);
	goto loop;

    case L_CURL:
	++depth;
	goto loop;

    case R_CURL:
	if (--depth == 0)
	{
	    puts_both(S, prefix_len ? "; " : " YYSTYPE;\n");
	    if (prefix_len)
	    {
		puts_both(S, "typedef union ");
		puts_both(S, prefix_buf);
		puts_both(S, " YYSTYPE;\n");
	    }
	    puts_both(S, "#endif /* !YYSTYPE_IS_DECLARED */\n");
	    end_ainfo(a);
	    return;
	}
	goto loop;

    case '\'':
    case '"':
	{
	    char *s = copy_string(S, c);
	    puts_both(S, s);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment(S);
	    puts_both(S, s);
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
more_curly(byacc_t* S)
{
    int result = 0;
    int finish = 0;
    save_line(S);
    do
    {
	switch (next_inline(S))
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
	++S->cptr;
    }
    while (!finish);
    restore_line(S);
    return result;
}

static void
save_param(byacc_t* S, int k, char *buffer, int name, int type2)
{
    param *head, *p;

    p = TMALLOC(param, 1);
    NO_SPACE(p);

    p->type2 = strdup(buffer + type2);
    NO_SPACE(p->type2);
    buffer[type2] = '\0';
    (void)trim_blanks(p->type2);

    if (!IS_ALNUM(buffer[name]))
    {
	int n;
	for (n = name - 1; n >= 0; --n)
	{
	    if (!isspace(UCH(buffer[n])))
	    {
		break;
	    }
	}
	while (n > 0)
	{
	    if (!IS_ALNUM(UCH(buffer[n - 1])))
		break;
	    --n;
	}
	name = n;
    }
    p->name = strdup(buffer + name);
    NO_SPACE(p->name);
    buffer[name] = '\0';
    (void)trim_blanks(p->name);

    p->type = strdup(buffer);
    NO_SPACE(p->type);
    (void)trim_blanks(p->type);

    if (k == LEX_PARAM)
	head = S->lex_param;
    else
	head = S->parse_param;

    if (head != NULL)
    {
	while (head->next)
	    head = head->next;
	head->next = p;
    }
    else
    {
	if (k == LEX_PARAM)
	    S->lex_param = p;
	else
	    S->parse_param = p;
    }
    p->next = NULL;
}

/*
 * Keep a linked list of parameters.  This may be multi-line, if the trailing
 * right-curly bracket is absent.
 */
static void
copy_param(byacc_t* S, int k)
{
    int c;
    int name, type2;
    int curly = 0;
    char *buf = 0;
    int i = -1;
    size_t buf_size = 0;
    int st_lineno = S->lineno;
    char *comma;

    do
    {
	int state = curly;
	c = next_inline(S);
	switch (c)
	{
	case EOF:
	    unexpected_EOF(S);
	    break;
	case L_CURL:
	    if (curly == 1)
	    {
		goto oops;
	    }
	    curly = 1;
	    st_lineno = S->lineno;
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
	    if ((curly == 1) && (S->cptr == S->line))
	    {
		S->lineno = st_lineno;
		missing_brace(S);
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
	    buf_size = (size_t)S->linesize;
	    buf = TMALLOC(char, buf_size);
	    NO_SPACE(buf);
	}
	else if (c == '\n')
	{
	    char *tmp;

	    get_line(S);
	    if (S->line == NULL)
		unexpected_EOF(S);
	    --S->cptr;
	    buf_size += (size_t)S->linesize;
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
	S->cptr++;
    }
    while (curly < 2 || more_curly(S));

    if (i == 0)
    {
	if (curly == 1)
	{
	    S->lineno = st_lineno;
	    missing_brace(S);
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
		unexpected_EOF(S);
	    type2 = i--;
	}
	else
	{
	    type2 = i + 1;
	}

	while (i > 0 && IS_ALNUM(UCH(parms[i])))
	    i--;

	if (!isspace(UCH(parms[i])) && parms[i] != '*')
	    goto oops;

	name = i + 1;

	save_param(S, k, parms, name, type2);
    }
    while (comma != 0);
    FREE(buf);
    return;

  oops:
    FREE(buf);
    syntax_error(S, S->lineno, S->line, S->cptr);
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
get_literal(byacc_t* S)
{
    int c, quote;
    int i;
    int n;
    char *s;
    bucket *bp;
    struct ainfo a;

    begin_ainfo(a, 0);

    quote = *S->cptr++;
    S->cinc = 0;
    for (;;)
    {
	c = *S->cptr++;
	if (c == quote)
	    break;
	if (c == '\n')
	    unterminated_string(S, &a);
	if (c == '\\')
	{
	    char *c_cptr = S->cptr - 1;

	    c = *S->cptr++;
	    switch (c)
	    {
	    case '\n':
		get_line(S);
		if (S->line == NULL)
		    unterminated_string(S, &a);
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
		c = *S->cptr;
		if (IS_OCTAL(c))
		{
		    n = (n << 3) + (c - '0');
		    c = *++S->cptr;
		    if (IS_OCTAL(c))
		    {
			n = (n << 3) + (c - '0');
			++S->cptr;
		    }
		}
		if (n > MAXCHAR)
		    illegal_character(S, c_cptr);
		c = n;
		break;

	    case 'x':
		c = *S->cptr++;
		n = hexval(c);
		if (n < 0 || n >= 16)
		    illegal_character(S, c_cptr);
		for (;;)
		{
		    c = *S->cptr;
		    i = hexval(c);
		    if (i < 0 || i >= 16)
			break;
		    ++S->cptr;
		    n = (n << 4) + i;
		    if (n > MAXCHAR)
			illegal_character(S, c_cptr);
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
	cachec(S, c);
    }
    end_ainfo(a);

    n = S->cinc;
    s = TMALLOC(char, n);
    NO_SPACE(s);

    for (i = 0; i < n; ++i)
	s[i] = S->cache[i];

    S->cinc = 0;
    if (n == 1)
	cachec(S, '\'');
    else
	cachec(S, '"');

    for (i = 0; i < n; ++i)
    {
	c = UCH(s[i]);
	if (c == '\\' || c == S->cache[0])
	{
	    cachec(S, '\\');
	    cachec(S, c);
	}
	else if (isprint(UCH(c)))
	    cachec(S, c);
	else
	{
	    cachec(S, '\\');
	    switch (c)
	    {
	    case 7:
		cachec(S, 'a');
		break;
	    case '\b':
		cachec(S, 'b');
		break;
	    case '\f':
		cachec(S, 'f');
		break;
	    case '\n':
		cachec(S, 'n');
		break;
	    case '\r':
		cachec(S, 'r');
		break;
	    case '\t':
		cachec(S, 't');
		break;
	    case '\v':
		cachec(S, 'v');
		break;
	    default:
		cachec(S, ((c >> 6) & 7) + '0');
		cachec(S, ((c >> 3) & 7) + '0');
		cachec(S, (c & 7) + '0');
		break;
	    }
	}
    }

    if (n == 1)
	cachec(S, '\'');
    else
	cachec(S, '"');

    cachec(S, NUL);
    bp = lookup(S, S->cache);
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
get_name(byacc_t* S)
{
    int c;

    S->cinc = 0;
    for (c = *S->cptr; IS_IDENT(c); c = *++S->cptr)
	cachec(S, c);
    cachec(S, NUL);

    if (is_reserved(S->cache))
	used_reserved(S, S->cache);

    return (lookup(S, S->cache));
}

static Value_t
get_number(byacc_t* S)
{
    int c;
    long n;
    char *base = S->cptr;

    n = 0;
    for (c = *S->cptr; isdigit(UCH(c)); c = *++S->cptr)
    {
	n = (10 * n + (c - '0'));
	if (n > MAXYYINT)
	{
	    syntax_error(S, S->lineno, S->line, base);
	    /*NOTREACHED */
	}
    }

    return (Value_t)(n);
}

static char *
cache_tag(byacc_t* S, char *tag, size_t len)
{
    int i;
    char *s;

    for (i = 0; i < S->ntags; ++i)
    {
	if (strncmp(tag, S->tag_table[i], len) == 0 &&
	    S->tag_table[i][len] == NUL)
	    return (S->tag_table[i]);
    }

    if (S->ntags >= S->tagmax)
    {
	S->tagmax += 16;
	S->tag_table =
	    (S->tag_table
	     ? TREALLOC(char *, S->tag_table, S->tagmax)
	     : TMALLOC(char *, S->tagmax));
	NO_SPACE(S->tag_table);
    }

    s = TMALLOC(char, len + 1);
    NO_SPACE(s);

    strncpy(s, tag, len);
    s[len] = 0;
    S->tag_table[S->ntags++] = s;
    return s;
}

static char *
get_tag(byacc_t* S)
{
    int c;
    int t_lineno = S->lineno;
    char *t_line = dup_line(S);
    char *t_cptr = t_line + (S->cptr - S->line);

    ++S->cptr;
    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (!IS_NAME1(c))
	illegal_tag(S, t_lineno, t_line, t_cptr);

    S->cinc = 0;
    do
    {
	cachec(S, c);
	c = *++S->cptr;
    }
    while (IS_IDENT(c));
    cachec(S, NUL);

    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (c != '>')
	illegal_tag(S, t_lineno, t_line, t_cptr);
    ++S->cptr;

    FREE(t_line);
    S->havetags = 1;
    return cache_tag(S, S->cache, (size_t)S->cinc);
}

#if defined(YYBTYACC)
static char *
scan_id(byacc_t* S)
{
    char *b = S->cptr;

    while (IS_NAME2(UCH(*S->cptr)))
	S->cptr++;
    return cache_tag(S, b, (size_t)(S->cptr - b));
}
#endif

static void
declare_tokens(byacc_t* S, int assoc)
{
    int c;
    bucket *bp;
    Value_t value;
    char *tag = 0;

    if (assoc != TOKEN)
	++S->prec;

    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (c == '<')
    {
	tag = get_tag(S);
	c = nextc(S);
	if (c == EOF)
	    unexpected_EOF(S);
    }

    for (;;)
    {
	if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	    bp = get_name(S);
	else if (c == '\'' || c == '"')
	    bp = get_literal(S);
	else
	    return;

	if (bp == S->goal)
	    tokenized_start(S, bp->name);
	bp->class = TERM;

	if (tag)
	{
	    if (bp->tag && tag != bp->tag)
		retyped_warning(S, bp->name);
	    bp->tag = tag;
	}

	if (assoc != TOKEN && !S->ignore_prec_flag)
	{
	    if (bp->prec && S->prec != bp->prec)
		reprec_warning(S, bp->name);
	    bp->assoc = (Assoc_t)assoc;
	    bp->prec = S->prec;
	    bp->prec_on_decl = 1;
	}

	c = nextc(S);
	if (c == EOF)
	    unexpected_EOF(S);

	if (isdigit(UCH(c)))
	{
	    value = get_number(S);
	    if (bp->value != UNDEFINED && value != bp->value)
		revalued_warning(S, bp->name);
	    bp->value = value;
	    c = nextc(S);
	    if (c == EOF)
		unexpected_EOF(S);
	}
    }
}

/*
 * %expect requires special handling
 * as it really isn't part of the yacc
 * grammar only a flag for yacc proper.
 */
static void
declare_expect(byacc_t* S, int assoc)
{
    int c;

    if (assoc != EXPECT && assoc != EXPECT_RR)
	++S->prec;

    /*
     * Stay away from nextc - doesn't
     * detect EOL and will read to EOF.
     */
    c = *++S->cptr;
    if (c == EOF)
	unexpected_EOF(S);

    for (;;)
    {
	if (isdigit(UCH(c)))
	{
	    if (assoc == EXPECT)
		S->SRexpect = get_number(S);
	    else
		S->RRexpect = get_number(S);
	    break;
	}
	/*
	 * Looking for number before EOL.
	 * Spaces, tabs, and numbers are ok,
	 * words, punc., etc. are syntax errors.
	 */
	else if (c == '\n' || isalpha(UCH(c)) || !isspace(UCH(c)))
	{
	    syntax_error(S, S->lineno, S->line, S->cptr);
	}
	else
	{
	    c = *++S->cptr;
	    if (c == EOF)
		unexpected_EOF(S);
	}
    }
}

#if defined(YYBTYACC)
static void
declare_argtypes(byacc_t* S, bucket *bp)
{
    char *tags[MAXARGS];
    int args = 0;

    if (bp->args >= 0)
	retyped_warning(S, bp->name);
    S->cptr++;			/* skip open paren */
    for (;;)
    {
	int c = nextc(S);
	if (c == EOF)
	    unexpected_EOF(S);
	if (c != '<')
	    syntax_error(S, S->lineno, S->line, S->cptr);
	tags[args++] = get_tag(S);
	c = nextc(S);
	if (c == R_PAREN)
	    break;
	if (c == EOF)
	    unexpected_EOF(S);
    }
    S->cptr++;			/* skip close paren */
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

static int
scan_blanks(byacc_t* S)
{
    int c;

    do
    {
	c = next_inline(S);
	if (c == '\n')
	{
	    ++S->cptr;
	    return 0;
	}
	else if (c == ' ' || c == '\t')
	    ++S->cptr;
	else
	    break;
    }
    while (c == ' ' || c == '\t');

    return 1;
}

static int
scan_ident(byacc_t* S)
{
    int c;

    S->cinc = 0;
    for (c = *S->cptr; IS_IDENT(c); c = *++S->cptr)
	cachec(S, c);
    cachec(S, NUL);

    return S->cinc;
}

static void
hack_defines(byacc_t* S)
{
    struct ainfo a;

    if (!scan_blanks(S))
	return;

    begin_ainfo(a, 0);
    if (!scan_ident(S))
    {
	end_ainfo(a);
    }

    if (!strcmp(S->cache, "api.pure"))
    {
	end_ainfo(a);
	scan_blanks(S);
	begin_ainfo(a, 0);
	scan_ident(S);

	if (!strcmp(S->cache, "false"))
	    S->pure_parser = 0;
	else if (!strcmp(S->cache, "true")
		 || !strcmp(S->cache, "full")
		 || *S->cache == 0)
	    S->pure_parser = 1;
	else
	    unexpected_value(S, &a);
	end_ainfo(a);
    }
    else
    {
	unexpected_value(S, &a);
    }

    while (next_inline(S) != '\n')
	++S->cptr;
}

static void
declare_types(byacc_t* S)
{
    int c;
    bucket *bp = NULL;
    char *tag = NULL;

    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (c == '<')
	tag = get_tag(S);

    for (;;)
    {
	c = nextc(S);
	if (c == EOF)
	    unexpected_EOF(S);
	if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	{
	    bp = get_name(S);
#if defined(YYBTYACC)
	    if (nextc(S) == L_PAREN)
		declare_argtypes(S, bp);
	    else
		bp->args = 0;
#endif
	}
	else if (c == '\'' || c == '"')
	{
	    bp = get_literal(S);
#if defined(YYBTYACC)
	    bp->args = 0;
#endif
	}
	else
	    return;

	if (tag)
	{
	    if (bp->tag && tag != bp->tag)
		retyped_warning(S, bp->name);
	    bp->tag = tag;
	}
    }
}

static void
declare_start(byacc_t* S)
{
    int c;
    bucket *bp;

    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    if (!isalpha(UCH(c)) && c != '_' && c != '.' && c != '$')
	syntax_error(S, S->lineno, S->line, S->cptr);
    bp = get_name(S);
    if (bp->class == TERM)
	terminal_start(S, bp->name);
    if (S->goal && S->goal != bp)
	restarted_warning(S);
    S->goal = bp;
}

static void
read_declarations(byacc_t* S)
{
    S->cache_size = CACHE_SIZE;
    S->cache = TMALLOC(char, S->cache_size);
    NO_SPACE(S->cache);

    for (;;)
    {
	int k;
	int c = nextc(S);

	if (c == EOF)
	    unexpected_EOF(S);
	if (c != '%')
	    syntax_error(S, S->lineno, S->line, S->cptr);
	switch (k = keyword(S))
	{
	case MARK:
	    return;

	case IDENT:
	    copy_ident(S);
	    break;

	case XCODE:
	    copy_code(S);
	    break;

	case TEXT:
	    copy_text(S);
	    break;

	case UNION:
	    copy_union(S);
	    break;

	case TOKEN:
	case LEFT:
	case RIGHT:
	case NONASSOC:
	case PRECEDENCE:
	    declare_tokens(S, k);
	    break;

	case EXPECT:
	case EXPECT_RR:
	    declare_expect(S, k);
	    break;

	case TYPE:
	    declare_types(S);
	    break;

	case START:
	    declare_start(S);
	    break;

	case PURE_PARSER:
	    S->pure_parser = 1;
	    break;

	case PARSE_PARAM:
	case LEX_PARAM:
	    copy_param(S, k);
	    break;

	case TOKEN_TABLE:
	    S->token_table = 1;
	    break;

	case ERROR_VERBOSE:
	    S->error_verbose = 1;
	    break;

#if defined(YYBTYACC)
	case LOCATIONS:
	    S->locations = 1;
	    break;

	case DESTRUCTOR:
	    S->destructor = 1;
	    copy_destructor(S);
	    break;
	case INITIAL_ACTION:
	    copy_initial_action(S);
	    break;
#endif

	case NONPOSIX_DEBUG:
	    S->tflag = 1;
	    break;

	case HACK_DEFINE:
	    hack_defines(S);
	    break;

	case POSIX_YACC:
	    /* noop for bison compatibility. byacc is already designed to be posix
	     * yacc compatible. */
	    break;
	}
    }
}

static void
initialize_grammar(byacc_t* S)
{
    S->nitems = 4;
    S->maxitems = 300;

    S->pitem = TMALLOC(bucket *, S->maxitems);
    NO_SPACE(S->pitem);

    S->pitem[0] = 0;
    S->pitem[1] = 0;
    S->pitem[2] = 0;
    S->pitem[3] = 0;

    S->nrules = 3;
    S->maxrules = 100;

    S->plhs = TMALLOC(bucket *, S->maxrules);
    NO_SPACE(S->plhs);

    S->plhs[0] = 0;
    S->plhs[1] = 0;
    S->plhs[2] = 0;

    S->rprec = TMALLOC(Value_t, S->maxrules);
    NO_SPACE(S->rprec);

    S->rprec[0] = 0;
    S->rprec[1] = 0;
    S->rprec[2] = 0;

    S->rassoc = TMALLOC(Assoc_t, S->maxrules);
    NO_SPACE(S->rassoc);

    S->rassoc[0] = TOKEN;
    S->rassoc[1] = TOKEN;
    S->rassoc[2] = TOKEN;

    S->rprec_bucket = TMALLOC(bucket*, S->maxrules);
    NO_SPACE(S->rprec_bucket);

    S->rprec_bucket[0] = 0;
    S->rprec_bucket[1] = 0;
    S->rprec_bucket[2] = 0;
}

static void
expand_items(byacc_t* S)
{
    S->maxitems += 300;
    S->pitem = TREALLOC(bucket*, S->pitem, S->maxitems);
    NO_SPACE(S->pitem);
}

static void
expand_rules(byacc_t* S)
{
    S->maxrules += 100;

    S->plhs = TREALLOC(bucket *, S->plhs, S->maxrules);
    NO_SPACE(S->plhs);

    S->rprec = TREALLOC(Value_t, S->rprec, S->maxrules);
    NO_SPACE(S->rprec);

    S->rassoc = TREALLOC(Assoc_t, S->rassoc, S->maxrules);
    NO_SPACE(S->rassoc);

    S->rprec_bucket = TREALLOC(bucket*, S->rprec_bucket, S->maxrules);
    NO_SPACE(S->rprec_bucket);
}

/* set immediately prior to where copy_args() could be called, and incremented by
   the various routines that will rescan the argument list as appropriate */
#if defined(YYBTYACC)

static char *
copy_args(byacc_t* S, int *alen)
{
    struct mstring *s = msnew();
    int depth = 0, len = 1;
    char c, quote = 0;
    struct ainfo a;

    begin_ainfo(a, 1);

    while ((c = *S->cptr++) != R_PAREN || depth || quote)
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
	    get_line(S);
	    if (!S->line)
	    {
		if (quote)
		    unterminated_string(S, &a);
		else
		    unterminated_arglist(S, &a);
	    }
	}
	else if (quote)
	{
	    if (c == quote)
		quote = 0;
	    else if (c == '\\')
	    {
		if (*S->cptr != '\n')
		    mputc(s, *S->cptr++);
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
    end_ainfo(a);
    return msdone(s);
}

static char *
parse_id(byacc_t* S, char *p, char **save)
{
    char *b;

    while (isspace(UCH(*p)))
	if (*p++ == '\n')
	    S->rescan_lineno++;
    if (!isalpha(UCH(*p)) && *p != '_')
	return NULL;
    b = p;
    while (IS_NAME2(UCH(*p)))
	p++;
    if (save)
    {
	*save = cache_tag(S, b, (size_t)(p - b));
    }
    return p;
}

static char *
parse_int(byacc_t* S, char *p, int *save)
{
    int neg = 0, val = 0;

    while (isspace(UCH(*p)))
	if (*p++ == '\n')
	    S->rescan_lineno++;
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
parse_arginfo(byacc_t* S, bucket *a, char *args, int argslen)
{
    char *p = args, *tmp;
    int i, redec = 0;

    if (a->args >= 0)
    {
	if (a->args != argslen)
	    arg_number_disagree_warning(S, S->rescan_lineno, a->name);
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
		S->rescan_lineno++;
	if (*p++ != '$')
	    bad_formals(S);
	while (isspace(UCH(*p)))
	    if (*p++ == '\n')
		S->rescan_lineno++;
	if (*p == '<')
	{
	    S->havetags = 1;
	    if (!(p = parse_id(S, p + 1, &tmp)))
		bad_formals(S);
	    while (isspace(UCH(*p)))
		if (*p++ == '\n')
		    S->rescan_lineno++;
	    if (*p++ != '>')
		bad_formals(S);
	    if (redec)
	    {
		if (a->argtags[i] != tmp)
		    arg_type_disagree_warning(S, S->rescan_lineno, i + 1, a->name);
	    }
	    else
		a->argtags[i] = tmp;
	}
	else if (!redec)
	    a->argtags[i] = NULL;
	if (!(p = parse_id(S, p, &a->argnames[i])))
	    bad_formals(S);
	while (isspace(UCH(*p)))
	    if (*p++ == '\n')
		S->rescan_lineno++;
	if (*p++)
	    bad_formals(S);
    }
    free(args);
}

static char *
compile_arg(byacc_t* S, char **theptr, char *yyvaltag)
{
    char *p = *theptr;
    struct mstring *c = msnew();
    int i, n;
    Value_t *offsets = NULL, maxoffset;
    bucket **rhs;

    maxoffset = 0;
    n = 0;
    for (i = S->nitems - 1; S->pitem[i]; --i)
    {
	n++;
	if (S->pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	int j;

	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < S->nitems; i++)
	    if (S->pitem[i]->class != ARGUMENT)
		offsets[++j] = (Value_t)(i - S->nitems + 1);
    }
    rhs = S->pitem + S->nitems - 1;

    if (yyvaltag)
	msprintf(S, c, "yyval.%s = ", yyvaltag);
    else
	msprintf(S, c, "yyval = ");
    while (*p)
    {
	if (*p == '$')
	{
	    char *tag = NULL;
	    if (*++p == '<')
		if (!(p = parse_id(S, ++p, &tag)) || *p++ != '>')
		    illegal_tag(S, S->rescan_lineno, NULL, NULL);
	    if (isdigit(UCH(*p)) || *p == '-')
	    {
		int val;
		if (!(p = parse_int(S, p, &val)))
		    dollar_error(S, S->rescan_lineno, NULL, NULL);
		if (val <= 0)
		    i = val - n;
		else if (val > maxoffset)
		{
		    dollar_warning(S, S->rescan_lineno, val);
		    i = val - maxoffset;
		}
		else if (maxoffset > 0)
		{
		    i = offsets[val];
		    if (!tag && !(tag = rhs[i]->tag) && S->havetags)
			untyped_rhs(S, val, rhs[i]->name);
		}
		msprintf(S, c, "yystack.l_mark[%d]", i);
		if (tag)
		    msprintf(S, c, ".%s", tag);
		else if (S->havetags)
		    unknown_rhs(S, val);
	    }
	    else if (isalpha(UCH(*p)) || *p == '_')
	    {
		char *arg;
		if (!(p = parse_id(S, p, &arg)))
		    dollar_error(S, S->rescan_lineno, NULL, NULL);
		for (i = S->plhs[S->nrules]->args - 1; i >= 0; i--)
		    if (arg == S->plhs[S->nrules]->argnames[i])
			break;
		if (i < 0)
		    unknown_arg_warning(S, S->rescan_lineno, "$", arg, NULL, NULL);
		else if (!tag)
		    tag = S->plhs[S->nrules]->argtags[i];
		msprintf(S, c, "yystack.l_mark[%d]",
			 i - S->plhs[S->nrules]->args + 1 - n);
		if (tag)
		    msprintf(S, c, ".%s", tag);
		else if (S->havetags)
		    untyped_arg_warning(S, S->rescan_lineno, "$", arg);
	    }
	    else
		dollar_error(S, S->rescan_lineno, NULL, NULL);
	}
	else if (*p == '@')
	{
	    at_error(S, S->rescan_lineno, NULL, NULL);
	}
	else
	{
	    if (*p == '\n')
		S->rescan_lineno++;
	    mputc(c, *p++);
	}
    }
    *theptr = p;
    if (maxoffset > 0)
	FREE(offsets);
    return msdone(c);
}

static int
can_elide_arg(byacc_t* S, char **theptr, char *yyvaltag)
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
	if (!(p = parse_id(S, ++p, &tag)) || *p++ != '>')
	    return 0;
    }
    for (i = S->nitems - 1; S->pitem[i]; --i)
    {
	n++;
	if (S->pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	int j;

	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < S->nitems; i++)
	    if (S->pitem[i]->class != ARGUMENT)
		offsets[++j] = (Value_t)(i - S->nitems + 1);
    }
    rhs = S->pitem + S->nitems - 1;

    if (isdigit(UCH(*p)) || *p == '-')
    {
	int val;
	if (!(p = parse_int(S, p, &val)))
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
	if (!(p = parse_id(S, p, &arg)))
	{
	    FREE(offsets);
	    return 0;
	}
	for (i = S->plhs[S->nrules]->args - 1; i >= 0; i--)
	    if (arg == S->plhs[S->nrules]->argnames[i])
		break;
	if (i >= 0)
	{
	    if (!tag)
		tag = S->plhs[S->nrules]->argtags[i];
	    rv = S->plhs[S->nrules]->args + n - i;
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
insert_arg_cache(byacc_t* S, char *code, int rule)
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
advance_to_start(byacc_t* S)
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

	c = nextc(S);
	if (c != '%')
	    break;
	s_cptr = S->cptr;
	switch (keyword(S))
	{
	case XCODE:
	    copy_code(S);
	    break;

	case MARK:
	    no_grammar(S);

	case TEXT:
	    copy_text(S);
	    break;

	case START:
	    declare_start(S);
	    break;

	default:
	    syntax_error(S, S->lineno, S->line, s_cptr);
	}
    }

    c = nextc(S);
    if (!isalpha(UCH(c)) && c != '_' && c != '.' && c != '_')
	syntax_error(S, S->lineno, S->line, S->cptr);
    bp = get_name(S);
    if (S->goal == 0)
    {
	if (bp->class == TERM)
	    terminal_start(S, bp->name);
	S->goal = bp;
    }

    s_lineno = S->lineno;
    c = nextc(S);
    if (c == EOF)
	unexpected_EOF(S);
    S->rescan_lineno = S->lineno;	/* line# for possible inherited args rescan */
#if defined(YYBTYACC)
    if (c == L_PAREN)
    {
	++S->cptr;
	args = copy_args(S, &argslen);
	NO_SPACE(args);
	c = nextc(S);
    }
#endif
    if (c != ':')
	syntax_error(S, S->lineno, S->line, S->cptr);
    start_rule(S, bp, s_lineno);
#if defined(YYBTYACC)
    parse_arginfo(S, bp, args, argslen);
#endif
    ++S->cptr;
}

static void
start_rule(byacc_t* S, bucket *bp, int s_lineno)
{
    if (bp->class == TERM)
	terminal_lhs(S, s_lineno);
    bp->class = NONTERM;
    if (!bp->index)
	bp->index = S->nrules;
    if (S->nrules >= S->maxrules)
	expand_rules(S);
    S->plhs[S->nrules] = bp;
    S->rprec[S->nrules] = UNDEFINED;
    S->rassoc[S->nrules] = TOKEN;
    S->rprec_bucket[S->nrules] = 0;
}

static void
end_rule(byacc_t* S)
{
    if (!S->last_was_action && S->plhs[S->nrules]->tag)
    {
	if (S->pitem[S->nitems - 1])
	{
	    int i;

	    for (i = S->nitems - 1; (i > 0) && S->pitem[i]; --i)
		continue;
	    if (S->pitem[i + 1] == 0 || S->pitem[i + 1]->tag != S->plhs[S->nrules]->tag)
		default_action_warning(S, S->plhs[S->nrules]->name);
	}
	else
	    default_action_warning(S, S->plhs[S->nrules]->name);
    }

    S->last_was_action = 0;
    if (S->nitems >= S->maxitems)
	expand_items(S);
    S->pitem[S->nitems] = 0;
    ++S->nitems;
    ++S->nrules;
}

static void
insert_empty_rule(byacc_t* S)
{
    bucket *bp, **bpp;

    assert(S->cache);
    assert(S->cache_size >= CACHE_SIZE);
    sprintf(S->cache, "$$%d", ++S->gensym);
    bp = make_bucket(S, S->cache);
    S->last_symbol->next = bp;
    S->last_symbol = bp;
    bp->tag = S->plhs[S->nrules]->tag;
    bp->class = ACTION;
#if defined(YYBTYACC)
    bp->args = 0;
#endif

    S->nitems = (Value_t)(S->nitems + 2);
    if (S->nitems > S->maxitems)
	expand_items(S);
    bpp = S->pitem + S->nitems - 1;
    *bpp-- = bp;
    while ((bpp[0] = bpp[-1]) != 0)
	--bpp;

    if (++S->nrules >= S->maxrules)
	expand_rules(S);
    S->plhs[S->nrules] = S->plhs[S->nrules - 1];
    S->plhs[S->nrules - 1] = bp;
    S->rprec[S->nrules] = S->rprec[S->nrules - 1];
    S->rprec[S->nrules - 1] = 0;
    S->rassoc[S->nrules] = S->rassoc[S->nrules - 1];
    S->rassoc[S->nrules - 1] = TOKEN;
    S->rprec_bucket[S->nrules] = S->rprec_bucket[S->nrules - 1];
    S->rprec_bucket[S->nrules - 1] = 0;
}

#if defined(YYBTYACC)
static char *
insert_arg_rule(byacc_t* S, char *arg, char *tag)
{
    int line_number = S->rescan_lineno;
    char *code = compile_arg(S, &arg, tag);
    int rule = lookup_arg_cache(code);
    FILE *f = S->action_file;

    if (rule < 0)
    {
	rule = S->nrules;
	insert_arg_cache(S, code, rule);
	S->trialaction = 1;	/* arg rules always run in trial mode */
	begin_case(f, rule - RULE_NUM_OFFSET);
	fprintf_lineno(f, line_number, S->input_file_name);
	fprintf(f, "%s;", code);
	end_case(f);
	insert_empty_rule(S);
	S->plhs[rule]->tag = cache_tag(S, tag, strlen(tag));
	S->plhs[rule]->class = ARGUMENT;
    }
    else
    {
	if (++S->nitems > S->maxitems)
	    expand_items(S);
	S->pitem[S->nitems - 1] = S->plhs[rule];
	free(code);
    }
    return arg + 1;
}
#endif

static void
add_symbol(byacc_t* S)
{
    int c;
    bucket *bp;
    int s_lineno = S->lineno;
#if defined(YYBTYACC)
    char *args = NULL;
    int argslen = 0;
#endif

    c = *S->cptr;
    if (c == '\'' || c == '"')
	bp = get_literal(S);
    else
	bp = get_name(S);

    c = nextc(S);
    S->rescan_lineno = S->lineno;	/* line# for possible inherited args rescan */
#if defined(YYBTYACC)
    if (c == L_PAREN)
    {
	++S->cptr;
	args = copy_args(S, &argslen);
	NO_SPACE(args);
	c = nextc(S);
    }
#endif
#if 0
    /*This need more thought because btyacc uses it.*/
    if (c == '[') //eat name alias 
    {
        for (;;++S->cptr)
        {
            c = nextc(S);

            if (c == EOF)
                break;
            if (c == ']') {
                ++S->cptr;
                c = nextc(S);
                break;
            }
        }
    }
#endif
    if (c == ':')
    {
	end_rule(S);
	start_rule(S, bp, s_lineno);
#if defined(YYBTYACC)
	parse_arginfo(S, bp, args, argslen);
#endif
	++S->cptr;
	return;
    }

    if (S->last_was_action)
	insert_empty_rule(S);
    S->last_was_action = 0;

#if defined(YYBTYACC)
    if (bp->args < 0)
	bp->args = argslen;
    if (argslen == 0 && bp->args > 0 && S->pitem[S->nitems - 1] == NULL)
    {
	int i;
	if (S->plhs[S->nrules]->args != bp->args)
	    wrong_number_args_warning(S, "default ", bp->name);
	for (i = bp->args - 1; i >= 0; i--)
	    if (S->plhs[S->nrules]->argtags[i] != bp->argtags[i])
		wrong_type_for_arg_warning(S, i + 1, bp->name);
    }
    else if (bp->args != argslen)
	wrong_number_args_warning(S, "", bp->name);
    if (args != 0)
    {
	char *ap = args;
	int i = 0;
	int elide_cnt = can_elide_arg(S, &ap, bp->argtags[0]);

	if (elide_cnt > argslen)
	    elide_cnt = 0;
	if (elide_cnt)
	{
	    for (i = 1; i < elide_cnt; i++)
		if (can_elide_arg(S, &ap, bp->argtags[i]) != elide_cnt - i)
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
	    ap = insert_arg_rule(S, ap, bp->argtags[i]);
	free(args);
    }
#endif /* defined(YYBTYACC) */

    if (++S->nitems > S->maxitems)
	expand_items(S);
    S->pitem[S->nitems - 1] = bp;
}

static void
copy_action(byacc_t* S)
{
    int c;
    int i, j, n;
    int depth;
#if defined(YYBTYACC)
    int haveyyval = 0;
#endif
    char *tag;
    FILE *f = S->action_file;
    struct ainfo a;
    Value_t *offsets = NULL, maxoffset;
    bucket **rhs;

    begin_ainfo(a, 0);

    if (S->last_was_action)
	insert_empty_rule(S);
    S->last_was_action = 1;
#if defined(YYBTYACC)
    S->trialaction = (*S->cptr == L_BRAC);
#endif

    begin_case(f, S->nrules - RULE_NUM_OFFSET);
#if defined(YYBTYACC)
    if (S->backtrack)
    {
	if (!S->trialaction)
	    fprintf(f, "  if (!yytrial)\n");
    }
#endif
    fprintf_lineno(f, S->lineno, S->input_file_name);
    if (*S->cptr == '=')
	++S->cptr;

    /* avoid putting curly-braces in first column, to ease editing */
    if (*after_blanks(S->cptr) == L_CURL)
    {
	putc('\t', f);
	S->cptr = after_blanks(S->cptr);
    }

    maxoffset = 0;
    n = 0;
    for (i = S->nitems - 1; S->pitem[i]; --i)
    {
	++n;
	if (S->pitem[i]->class != ARGUMENT)
	    maxoffset++;
    }
    if (maxoffset > 0)
    {
	offsets = TMALLOC(Value_t, maxoffset + 1);
	NO_SPACE(offsets);

	for (j = 0, i++; i < S->nitems; i++)
	{
	    if (S->pitem[i]->class != ARGUMENT)
	    {
		offsets[++j] = (Value_t)(i - S->nitems + 1);
	    }
	}
    }
    rhs = S->pitem + S->nitems - 1;

    depth = 0;
  loop:
    c = *S->cptr;
    if (c == '$')
    {
	if (S->cptr[1] == '<')
	{
	    int d_lineno = S->lineno;
	    char *d_line = dup_line(S);
	    char *d_cptr = d_line + (S->cptr - S->line);

	    ++S->cptr;
	    tag = get_tag(S);
	    c = *S->cptr;
	    if (c == '$')
	    {
		fprintf(f, "yyval.%s", tag);
		++S->cptr;
		FREE(d_line);
		goto loop;
	    }
	    else if (isdigit(UCH(c)))
	    {
		i = get_number(S);
		if (i == 0)
		    fprintf(f, "yystack.l_mark[%d].%s", -n, tag);
		else if (i > maxoffset)
		{
		    dollar_warning(S, d_lineno, i);
		    fprintf(f, "yystack.l_mark[%ld].%s",
			    (long)(i - maxoffset), tag);
		}
		else if (offsets)
		    fprintf(f, "yystack.l_mark[%ld].%s",
			    (long)offsets[i], tag);
		FREE(d_line);
		goto loop;
	    }
	    else if (c == '-' && isdigit(UCH(S->cptr[1])))
	    {
		++S->cptr;
		i = -get_number(S) - n;
		fprintf(f, "yystack.l_mark[%d].%s", i, tag);
		FREE(d_line);
		goto loop;
	    }
#if defined(YYBTYACC)
	    else if (isalpha(UCH(c)) || c == '_')
	    {
		char *arg = scan_id(S);
		for (i = S->plhs[S->nrules]->args - 1; i >= 0; i--)
		    if (arg == S->plhs[S->nrules]->argnames[i])
			break;
		if (i < 0)
		    unknown_arg_warning(S, d_lineno, "$", arg, d_line, d_cptr);
		fprintf(f, "yystack.l_mark[%d].%s",
			i - S->plhs[S->nrules]->args + 1 - n, tag);
		FREE(d_line);
		goto loop;
	    }
#endif
	    else
		dollar_error(S, d_lineno, d_line, d_cptr);
	}
	else if (S->cptr[1] == '$')
	{
	    if (S->havetags)
	    {
		tag = S->plhs[S->nrules]->tag;
		if (tag == 0)
		    untyped_lhs(S);
		fprintf(f, "yyval.%s", tag);
	    }
	    else
		fprintf(f, "yyval");
	    S->cptr += 2;
#if defined(YYBTYACC)
	    haveyyval = 1;
#endif
	    goto loop;
	}
	else if (isdigit(UCH(S->cptr[1])))
	{
	    ++S->cptr;
	    i = get_number(S);
	    if (S->havetags && offsets)
	    {
		if (i <= 0 || i > maxoffset)
		    unknown_rhs(S, i);
		tag = rhs[offsets[i]]->tag;
		if (tag == 0)
		    untyped_rhs(S, i, rhs[offsets[i]]->name);
		fprintf(f, "yystack.l_mark[%ld].%s", (long)offsets[i], tag);
	    }
	    else
	    {
		if (i == 0)
		    fprintf(f, "yystack.l_mark[%d]", -n);
		else if (i > maxoffset)
		{
		    dollar_warning(S, S->lineno, i);
		    fprintf(f, "yystack.l_mark[%ld]", (long)(i - maxoffset));
		}
		else if (offsets)
		    fprintf(f, "yystack.l_mark[%ld]", (long)offsets[i]);
	    }
	    goto loop;
	}
	else if (S->cptr[1] == '-')
	{
	    S->cptr += 2;
	    i = get_number(S);
	    if (S->havetags)
		unknown_rhs(S, -i);
	    fprintf(f, "yystack.l_mark[%d]", -i - n);
	    goto loop;
	}
#if defined(YYBTYACC)
	else if (isalpha(UCH(S->cptr[1])) || S->cptr[1] == '_')
	{
	    char *arg;
	    ++S->cptr;
	    arg = scan_id(S);
	    for (i = S->plhs[S->nrules]->args - 1; i >= 0; i--)
		if (arg == S->plhs[S->nrules]->argnames[i])
		    break;
	    if (i < 0)
		unknown_arg_warning(S, S->lineno, "$", arg, S->line, S->cptr);
	    tag = (i < 0 ? NULL : S->plhs[S->nrules]->argtags[i]);
	    fprintf(f, "yystack.l_mark[%d]", i - S->plhs[S->nrules]->args + 1 - n);
	    if (tag)
		fprintf(f, ".%s", tag);
	    else if (S->havetags)
		untyped_arg_warning(S, S->lineno, "$", arg);
	    goto loop;
	}
#endif
    }
#if defined(YYBTYACC)
    if (c == '@')
    {
	if (!S->locations)
	{
	    dislocations_warning(S);
	    S->locations = 1;
	}
	if (S->cptr[1] == '$')
	{
	    fprintf(f, "yyloc");
	    S->cptr += 2;
	    goto loop;
	}
	else if (isdigit(UCH(S->cptr[1])))
	{
	    ++S->cptr;
	    i = get_number(S);
	    if (i == 0)
		fprintf(f, "yystack.p_mark[%d]", -n);
	    else if (i > maxoffset)
	    {
		at_warning(S, S->lineno, i);
		fprintf(f, "yystack.p_mark[%d]", i - maxoffset);
	    }
	    else if (offsets)
		fprintf(f, "yystack.p_mark[%d]", offsets[i]);
	    goto loop;
	}
	else if (S->cptr[1] == '-')
	{
	    S->cptr += 2;
	    i = get_number(S);
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
	    c = *++S->cptr;
	}
	while (IS_NAME2(c));
	goto loop;
    }
    ++S->cptr;
#if defined(YYBTYACC)
    if (S->backtrack)
    {
	if (S->trialaction && c == L_BRAC && depth == 0)
	{
	    ++depth;
	    putc(L_CURL, f);
	    goto loop;
	}
	if (S->trialaction && c == R_BRAC && depth == 1)
	{
	    --depth;
	    putc(R_CURL, f);
	    c = nextc(S);
	    if (c == L_BRAC && !haveyyval)
	    {
		goto loop;
	    }
	    if (c == L_CURL && !haveyyval)
	    {
		fprintf(f, "  if (!yytrial)\n");
		fprintf_lineno(f, S->lineno, S->input_file_name);
		S->trialaction = 0;
		goto loop;
	    }
	    end_case(f);
	    end_ainfo(a);
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
	get_line(S);
	if (S->line)
	    goto loop;
	unterminated_action(S, &a);

    case ';':
	if (depth > 0)
	    goto loop;
	end_case(f);
	end_ainfo(a);
	if (maxoffset > 0)
	    FREE(offsets);
	return;

#if defined(YYBTYACC)
    case L_BRAC:
	if (S->backtrack)
	    ++depth;
	goto loop;

    case R_BRAC:
	if (S->backtrack)
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
	if (S->backtrack)
	{
	    c = nextc(S);
	    if (c == L_BRAC && !haveyyval)
	    {
		S->trialaction = 1;
		goto loop;
	    }
	    if (c == L_CURL && !haveyyval)
	    {
		fprintf(f, "  if (!yytrial)\n");
		fprintf_lineno(f, S->lineno, S->input_file_name);
		goto loop;
	    }
	}
#endif
	end_case(f);
	end_ainfo(a);
	if (maxoffset > 0)
	    FREE(offsets);
	return;

    case '\'':
    case '"':
	{
	    char *s = copy_string(S, c);
	    fputs(s, f);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment(S);
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
get_code(byacc_t* S, struct ainfo *a, const char *loc)
{
    int c;
    int depth;
    char *tag;
    struct mstring *code_mstr = msnew();

    if (!S->lflag)
	msprintf(S, code_mstr, S->line_format, S->lineno, S->input_file_name);

    S->cptr = after_blanks(S->cptr);
    if (*S->cptr == L_CURL)
	/* avoid putting curly-braces in first column, to ease editing */
	mputc(code_mstr, '\t');
    else
	syntax_error(S, S->lineno, S->line, S->cptr);

    begin_ainfo((*a), 0);

    depth = 0;
  loop:
    c = *S->cptr;
    if (c == '$')
    {
	if (S->cptr[1] == '<')
	{
	    int d_lineno = S->lineno;
	    char *d_line = dup_line(S);
	    char *d_cptr = d_line + (S->cptr - S->line);

	    ++S->cptr;
	    tag = get_tag(S);
	    c = *S->cptr;
	    if (c == '$')
	    {
		msprintf(S, code_mstr, "(*val).%s", tag);
		++S->cptr;
		FREE(d_line);
		goto loop;
	    }
	    else
		dollar_error(S, d_lineno, d_line, d_cptr);
	}
	else if (S->cptr[1] == '$')
	{
	    /* process '$$' later; replacement is context dependent */
	    msprintf(S, code_mstr, "$$");
	    S->cptr += 2;
	    goto loop;
	}
    }
    if (c == '@' && S->cptr[1] == '$')
    {
	if (!S->locations)
	{
	    dislocations_warning(S);
	    S->locations = 1;
	}
	msprintf(S, code_mstr, "%s", loc);
	S->cptr += 2;
	goto loop;
    }
    if (IS_NAME1(c))
    {
	do
	{
	    mputc(code_mstr, c);
	    c = *++S->cptr;
	}
	while (IS_NAME2(c));
	goto loop;
    }
    ++S->cptr;
    mputc(code_mstr, c);
    switch (c)
    {
    case '\n':
	get_line(S);
	if (S->line)
	    goto loop;
	unterminated_action(S, a);

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
	    char *s = copy_string(S, c);
	    msprintf(S, code_mstr, "%s", s);
	    free(s);
	}
	goto loop;

    case '/':
	{
	    char *s = copy_comment(S);
	    msprintf(S, code_mstr, "%s", s);
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
copy_initial_action(byacc_t* S)
{
    struct ainfo a;

    S->initial_action = get_code(S, &a, "yyloc");
    end_ainfo(a);
}

static void
copy_destructor(byacc_t* S)
{
    char *code_text;
    struct ainfo a;
    bucket *bp;

    code_text = get_code(S, &a, "(*loc)");

    for (;;)
    {
	int c = nextc(S);
	if (c == EOF)
	    unexpected_EOF(S);
	if (c == '<')
	{
	    if (S->cptr[1] == '>')
	    {			/* "no semantic type" default destructor */
		S->cptr += 2;
		if ((bp = S->default_destructor[UNTYPED_DEFAULT]) == NULL)
		{
		    static char untyped_default[] = "<>";
		    bp = make_bucket(S, "untyped default");
		    bp->tag = untyped_default;
		    S->default_destructor[UNTYPED_DEFAULT] = bp;
		}
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(S, &a);
		else
		    /* replace "$$" with "(*val)" in destructor code */
		    bp->destructor = process_destructor_XX(S, code_text, NULL);
	    }
	    else if (S->cptr[1] == '*' && S->cptr[2] == '>')
	    {			/* "no per-symbol or per-type" default destructor */
		S->cptr += 3;
		if ((bp = S->default_destructor[TYPED_DEFAULT]) == NULL)
		{
		    static char typed_default[] = "<*>";
		    bp = make_bucket(S, "typed default");
		    bp->tag = typed_default;
		    S->default_destructor[TYPED_DEFAULT] = bp;
		}
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(S, &a);
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
		char *tag = get_tag(S);
		bp = lookup_type_destructor(S, tag);
		if (bp->destructor != NULL)
		    destructor_redeclared_warning(S, &a);
		else
		    /* replace "$$" with "(*val).tag" in destructor code */
		    bp->destructor = process_destructor_XX(S, code_text, tag);
	    }
	}
	else if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	{			/* "symbol" destructor */
	    bp = get_name(S);
	    if (bp->destructor != NULL)
		destructor_redeclared_warning(S, &a);
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
    end_ainfo(a);
    free(code_text);
}

static char *
process_destructor_XX(byacc_t* S, char *code, char *tag)
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
	    msprintf(S, new_code, "(*val)");
	else
	    msprintf(S, new_code, "(*val).%s", tag);
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
mark_symbol(byacc_t* S)
{
    int c;
    bucket *bp = NULL;

    c = S->cptr[1];
    if (c == '%' || c == '\\')
    {
	S->cptr += 2;
	return (1);
    }

    if (c == '=')
	S->cptr += 2;
    else if ((c == 'p' || c == 'P') &&
	     ((c = S->cptr[2]) == 'r' || c == 'R') &&
	     ((c = S->cptr[3]) == 'e' || c == 'E') &&
	     ((c = S->cptr[4]) == 'c' || c == 'C') &&
	     ((c = S->cptr[5], !IS_IDENT(c))))
	S->cptr += 5;
    else if ((c == 'e' || c == 'E') &&
	     ((c = S->cptr[2]) == 'm' || c == 'M') &&
	     ((c = S->cptr[3]) == 'p' || c == 'P') &&
	     ((c = S->cptr[4]) == 't' || c == 'T') &&
	     ((c = S->cptr[5]) == 'y' || c == 'Y') &&
	     ((c = S->cptr[6], !IS_IDENT(c))))
    {
	S->cptr += 6;
	return (1);
    }
    else
	syntax_error(S, S->lineno, S->line, S->cptr);

    c = nextc(S);
    if (isalpha(UCH(c)) || c == '_' || c == '.' || c == '$')
	bp = get_name(S);
    else if (c == '\'' || c == '"')
	bp = get_literal(S);
    else
    {
	syntax_error(S, S->lineno, S->line, S->cptr);
	/*NOTREACHED */
    }

    if (S->rprec[S->nrules] != UNDEFINED && bp->prec != S->rprec[S->nrules])
	prec_redeclared(S);

    if(!S->ignore_prec_flag) {
        S->rprec[S->nrules] = bp->prec;
        S->rassoc[S->nrules] = bp->assoc;
        S->rprec_bucket[S->nrules] = bp;
    }
    return (0);
}

static void
read_grammar(byacc_t* S)
{
    initialize_grammar(S);
    advance_to_start(S);

    for (;;)
    {
	int c = nextc(S);

	if (c == EOF)
	    break;
	if (isalpha(UCH(c))
	    || c == '_'
	    || c == '.'
	    || c == '$'
	    || c == '\''
	    || c == '"')
	{
	    add_symbol(S);
	}
	else if (c == L_CURL || c == '='
#if defined(YYBTYACC)
		 || (S->backtrack && c == L_BRAC)
#endif
	    )
	{
	    copy_action(S);
	}
	else if (c == '|')
	{
	    end_rule(S);
	    start_rule(S, S->plhs[S->nrules - 1], 0);
	    ++S->cptr;
	}
	else if (c == '%')
	{
	    if (mark_symbol(S))
		break;
	}
	else
	    syntax_error(S, S->lineno, S->line, S->cptr);
    }
    end_rule(S);
#if defined(YYBTYACC)
    if (S->goal->args > 0)
	start_requires_args(S, S->goal->name);
#endif
}

static void
free_tags(byacc_t* S)
{
    int i;

    if (S->tag_table == 0)
	return;

    for (i = 0; i < S->ntags; ++i)
    {
	assert(S->tag_table[i]);
	FREE(S->tag_table[i]);
    }
    FREE(S->tag_table);
}

static void
pack_names(byacc_t* S)
{
    bucket *bp;
    char *p;
    char *t;

    S->name_pool_size = 13;	/* 13 == sizeof("$end") + sizeof("$accept") */
    for (bp = S->first_symbol; bp; bp = bp->next)
	S->name_pool_size += strlen(bp->name) + 1;

    S->name_pool = TMALLOC(char, S->name_pool_size);
    NO_SPACE(S->name_pool);

    strcpy(S->name_pool, "$accept");
    strcpy(S->name_pool + 8, "$end");
    t = S->name_pool + 13;
    for (bp = S->first_symbol; bp; bp = bp->next)
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
check_symbols(byacc_t* S)
{
    bucket *bp;

    if (S->goal->class == UNKNOWN)
	undefined_goal(S, S->goal->name);

    for (bp = S->first_symbol; bp; bp = bp->next)
    {
	if (bp->class == UNKNOWN)
	{
	    undefined_symbol_warning(S, bp->name);
	    bp->class = TERM;
	}
    }
}

static void
protect_string(byacc_t* S, char *src, char **des)
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
pack_symbols(byacc_t* S)
{
    bucket *bp;
    bucket **v;
    Value_t i, j, k, n;
#if defined(YYBTYACC)
    Value_t max_tok_pval;
#endif

    S->nsyms = 2;
    S->ntokens = 1;
    for (bp = S->first_symbol; bp; bp = bp->next)
    {
	++S->nsyms;
	if (bp->class == TERM)
	    ++S->ntokens;
    }
    S->start_symbol = (Value_t)S->ntokens;
    S->nvars = (Value_t)(S->nsyms - S->ntokens);

    S->symbol_name = TMALLOC(char *, S->nsyms);
    NO_SPACE(S->symbol_name);

    S->symbol_value = TMALLOC(Value_t, S->nsyms);
    NO_SPACE(S->symbol_value);

    S->symbol_prec = TMALLOC(Value_t, S->nsyms);
    NO_SPACE(S->symbol_prec);

    S->symbol_assoc = TMALLOC(char, S->nsyms);
    NO_SPACE(S->symbol_assoc);

#if defined(YYBTYACC)
    S->symbol_pval = TMALLOC(Value_t, S->nsyms);
    NO_SPACE(S->symbol_pval);

    if (S->destructor)
    {
	S->symbol_destructor = CALLOC(sizeof(char *), S->nsyms);
	NO_SPACE(S->symbol_destructor);

	S->symbol_type_tag = CALLOC(sizeof(char *), S->nsyms);
	NO_SPACE(S->symbol_type_tag);
    }
#endif

    v = TMALLOC(bucket *, S->nsyms);
    NO_SPACE(v);

    v[0] = 0;
    v[S->start_symbol] = 0;

    i = 1;
    j = (Value_t)(S->start_symbol + 1);
    for (bp = S->first_symbol; bp; bp = bp->next)
    {
	if (bp->class == TERM)
	    v[i++] = bp;
	else
	    v[j++] = bp;
    }
    assert(i == S->ntokens && j == S->nsyms);

    for (i = 1; i < S->ntokens; ++i)
	v[i]->index = i;

    S->goal->index = (Index_t)(S->start_symbol + 1);
    k = (Value_t)(S->start_symbol + 2);
    while (++i < S->nsyms)
	if (v[i] != S->goal)
	{
	    v[i]->index = k;
	    ++k;
	}

    S->goal->value = 0;
    k = 1;
    for (i = (Value_t)(S->start_symbol + 1); i < S->nsyms; ++i)
    {
	if (v[i] != S->goal)
	{
	    v[i]->value = k;
	    ++k;
	}
    }

    k = 0;
    for (i = 1; i < S->ntokens; ++i)
    {
	n = v[i]->value;
	if (n > 256)
	{
	    for (j = k++; j > 0 && S->symbol_value[j - 1] > n; --j)
		S->symbol_value[j] = S->symbol_value[j - 1];
	    S->symbol_value[j] = n;
	}
    }

    assert(v[1] != 0);

    if (v[1]->value == UNDEFINED)
	v[1]->value = 256;

    j = 0;
    n = 257;
    for (i = 2; i < S->ntokens; ++i)
    {
	if (v[i]->value == UNDEFINED)
	{
	    while (j < k && n == S->symbol_value[j])
	    {
		while (++j < k && n == S->symbol_value[j])
		    continue;
		++n;
	    }
	    v[i]->value = n;
	    ++n;
	}
    }

    S->symbol_name[0] = S->name_pool + 8;
    S->symbol_value[0] = 0;
    S->symbol_prec[0] = 0;
    S->symbol_assoc[0] = TOKEN;
#if defined(YYBTYACC)
    S->symbol_pval[0] = 0;
    max_tok_pval = 0;
#endif
    for (i = 1; i < S->ntokens; ++i)
    {
	S->symbol_name[i] = v[i]->name;
	S->symbol_value[i] = v[i]->value;
	S->symbol_prec[i] = v[i]->prec;
	S->symbol_assoc[i] = v[i]->assoc;
#if defined(YYBTYACC)
	S->symbol_pval[i] = v[i]->value;
	if (S->symbol_pval[i] > max_tok_pval)
	    max_tok_pval = S->symbol_pval[i];
	if (S->destructor)
	{
	    S->symbol_destructor[i] = v[i]->destructor;
	    S->symbol_type_tag[i] = v[i]->tag;
	}
#endif
    }
    S->symbol_name[S->start_symbol] = S->name_pool;
    S->symbol_value[S->start_symbol] = -1;
    S->symbol_prec[S->start_symbol] = 0;
    S->symbol_assoc[S->start_symbol] = TOKEN;
#if defined(YYBTYACC)
    S->symbol_pval[S->start_symbol] = (Value_t)(max_tok_pval + 1);
#endif
    for (++i; i < S->nsyms; ++i)
    {
	k = v[i]->index;
	S->symbol_name[k] = v[i]->name;
	S->symbol_value[k] = v[i]->value;
	S->symbol_prec[k] = v[i]->prec;
	S->symbol_assoc[k] = v[i]->assoc;
#if defined(YYBTYACC)
	S->symbol_pval[k] = (Value_t)((max_tok_pval + 1) + v[i]->value + 1);
	if (S->destructor)
	{
	    S->symbol_destructor[k] = v[i]->destructor;
	    S->symbol_type_tag[k] = v[i]->tag;
	}
#endif
    }

    if (S->gflag)
    {
	S->symbol_pname = TMALLOC(char *, S->nsyms);
	NO_SPACE(S->symbol_pname);

	for (i = 0; i < S->nsyms; ++i)
	    protect_string(S, S->symbol_name[i], &(S->symbol_pname[i]));
    }

    FREE(v);
}

static void
pack_grammar(byacc_t* S)
{
    int i;
    Value_t j;

    S->ritem = TMALLOC(Value_t, S->nitems);
    NO_SPACE(S->ritem);

    S->rlhs = TMALLOC(Value_t, S->nrules);
    NO_SPACE(S->rlhs);

    S->rrhs = TMALLOC(Value_t, S->nrules + 1);
    NO_SPACE(S->rrhs);

    S->rprec = TREALLOC(Value_t, S->rprec, S->nrules);
    NO_SPACE(S->rprec);

    S->rassoc = TREALLOC(Assoc_t, S->rassoc, S->nrules);
    NO_SPACE(S->rassoc);

    S->rprec_bucket = TREALLOC(bucket*, S->rprec_bucket, S->nrules);
    NO_SPACE(S->rprec_bucket);

    S->ritem[0] = -1;
    S->ritem[1] = S->goal->index;
    S->ritem[2] = 0;
    S->ritem[3] = -2;
    S->rlhs[0] = 0;
    S->rlhs[1] = 0;
    S->rlhs[2] = S->start_symbol;
    S->rrhs[0] = 0;
    S->rrhs[1] = 0;
    S->rrhs[2] = 1;

    j = 4;
    for (i = 3; i < S->nrules; ++i)
    {
	Assoc_t assoc;
	Value_t prec2;

#if defined(YYBTYACC)
	if (S->plhs[i]->args > 0)
	{
	    if (S->plhs[i]->argnames)
	    {
		FREE(S->plhs[i]->argnames);
		S->plhs[i]->argnames = NULL;
	    }
	    if (S->plhs[i]->argtags)
	    {
		FREE(S->plhs[i]->argtags);
		S->plhs[i]->argtags = NULL;
	    }
	}
#endif /* defined(YYBTYACC) */
	S->rlhs[i] = S->plhs[i]->index;
	S->rrhs[i] = j;
	assoc = TOKEN;
	prec2 = 0;
	while (S->pitem[j])
	{
	    S->ritem[j] = S->pitem[j]->index;
	    if (S->pitem[j]->class == TERM)
	    {
                if(prec2 == 0 || !S->lemon_prec_flag) {
                    prec2 = S->pitem[j]->prec;
                    assoc = S->pitem[j]->assoc;
                }
	    }
	    ++j;
	}
	S->ritem[j] = (Value_t)-i;
	++j;
	if (S->rprec[i] == UNDEFINED)
	{
	    S->rprec[i] = prec2;
	    S->rassoc[i] = assoc;
	}
    }
    S->rrhs[i] = j;

    FREE(S->plhs);
    FREE(S->pitem);
#if defined(YYBTYACC)
    clean_arg_cache();
#endif
}

#define START_RULE_IDX 2

static void
print_grammar(byacc_t* S)
{
    int i, k;
    size_t j, spacing = 0;
    FILE *f = S->verbose_file;

    if (!S->vflag)
	return;

    k = 1;
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
	if (S->rlhs[i] != S->rlhs[i - 1])
	{
	    if (i != 2)
		fprintf(f, "\n");
	    fprintf(f, "%4d  %s :", i - RULE_NUM_OFFSET, S->symbol_name[S->rlhs[i]]);
	    spacing = strlen(S->symbol_name[S->rlhs[i]]) + 1;
	}
	else
	{
	    fprintf(f, "%4d  ", i - RULE_NUM_OFFSET);
	    j = spacing;
	    while (j-- != 0)
		putc(' ', f);
	    putc('|', f);
	}

	while (S->ritem[k] >= 0)
	{
	    fprintf(f, " %s", S->symbol_name[S->ritem[k]]);
	    ++k;
	}
	++k;
        bucket *prec_bucket = S->rprec_bucket[i];
        if(prec_bucket) {
            fprintf(f, " %%prec %s", prec_bucket->name);
        }
	putc('\n', f);
    }
}

static void
print_grammar_ebnf(byacc_t* S)
{
    int i, k;
    FILE *f = S->ebnf_file;

    if (!S->ebnf_flag)
	return;

    k = 1;
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
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

        if(!(S->ritem[k] >= 0)) {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
        else
        {
            while (S->ritem[k] >= 0)
            {
                sym_name = S->symbol_name[S->ritem[k]];
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

static const char * ygv_ascii_names_st[] = {
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
            if(ch < 127) snprintf(*buf, sizeof(char64_t), "Tk_%s", ygv_ascii_names_st[ch]);
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
            if(ch < 127) snprintf(*buf, sizeof(char64_t), "Tk_%s", ygv_ascii_names_st[ch]);
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
print_symbol_prec_commented(byacc_t* S, FILE *out, Value_t sym)
{
    if(S->symbol_prec[sym]) {
        print_symbol_prec_assoc_commented(out, S->symbol_prec[sym], S->symbol_assoc[sym]);
    }
}

static const char*
get_prec_name(byacc_t* S, int sym_id) {
    Value_t assoc = S->symbol_assoc[sym_id];
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
print_grammar_lemon(byacc_t* S)
{
    int i, k;
    FILE *f = S->lemon_file;
    char64_t buf;

    if (!S->lemon_flag)
	return;

    for (i = 0; i < S->ntokens; ++i)
    {
        const char *sym_name = S->symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            if(sym_name[0] != '"')
                fprintf(f, "%%token %s .\n", get_lemon_token_name(&buf, sym_name));
    }
    fprintf(f, "\n");

    for (k = 0; k <= S->prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < S->ntokens; ++i)
        {
            Value_t prec = S->symbol_prec[i];
            if(prec == k) {
                const char *sym_name = S->symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(S, i);
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
    fprintf(f, "\n%%start_symbol %s\n", get_lemon_rule_name(&buf, S->goal->name));

    k = 1;
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	{
            if (S->rlhs[i] != S->rlhs[i - 1]) fprintf(f, "\n");
	    if(!skip_rule)
                fprintf(f, "%s ::=", (i == START_RULE_IDX) ? "x_start_rule_" : get_lemon_rule_name(&buf, sym_name));
	}

        if(!(S->ritem[k] >= 0)) {
            if(!skip_rule)
                fprintf(f, " /*empty*/");
        }
        else
        {
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = S->symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    bucket *bp = lookup(S, sym_name);
                    if(bp->class == TERM) {
                        fprintf(f, " %s", get_lemon_token_name(&buf, sym_name));
                        print_symbol_prec_commented(S, f, symbol);
                    }
                    else
                        fprintf(f, " %s", get_lemon_rule_name(&buf, sym_name));
                }
                ++k;
            }
        }
	++k;
        if(!skip_rule) {
            bucket *prec_bucket = S->rprec_bucket[i];
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
print_grammar_naked(byacc_t* S)
{
    int i, k;
    FILE *f = S->naked_file;

    if (!S->naked_flag)
	return;

    for (i = 0; i < S->ntokens; ++i)
    {
        const char *sym_name = S->symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            fprintf(f, "%%token %s\n", sym_name);
    }
    fprintf(f, "\n");

    for (k = 0; k <= S->prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < S->ntokens; ++i)
        {
            Value_t prec = S->symbol_prec[i];
            if(prec == k) {
                const char *sym_name = S->symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(S, i);
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
    fprintf(f, "\n%%start %s\n", S->goal->name);
    fprintf(f, "\n%%%%\n\n");

    k = 1;
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
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

        if(S->ritem[k] >= 0) {
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = S->symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    fprintf(f, " %s", sym_name);
                    print_symbol_prec_commented(S, f, symbol);
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
            bucket *prec_bucket = S->rprec_bucket[i];
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

static void
print_grammar_nakedQ(byacc_t* S)
{
    int i, k;
    FILE *f = S->nakedq_file;

    if (!S->nakedq_flag)
	return;

    for (i = 0; i < S->ntokens; ++i)
    {
        const char *sym_name = S->symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            fprintf(f, "%%token <%s>\n", sym_name);
    }
    fprintf(f, "\n");

    for (k = 0; k <= S->prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < S->ntokens; ++i)
        {
            Value_t prec = S->symbol_prec[i];
            if(prec == k) {
                const char *sym_name = S->symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(S, i);
                        if(!assoc_name) continue;
                        fprintf(f, "%%%s /*%d*/", assoc_name, prec);
                        sassoc = 1;
                    }
                    fprintf(f, " <%s>", sym_name);
                }
            }
        }
        if(sassoc) {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "\n%%start %s\n", S->goal->name);
    fprintf(f, "\n%%%%\n\n");

    k = 1;
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
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

        if(S->ritem[k] >= 0) {
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = S->symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    if(symbol < S->ntokens && !(sym_name[0] == '"' || sym_name[0] == '\'')) fprintf(f, " <%s>", sym_name);
                    else fprintf(f, " %s", sym_name);
                    print_symbol_prec_commented(S, f, symbol);
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
            bucket *prec_bucket = S->rprec_bucket[i];
            if(prec_bucket) {
                fprintf(f, " %%prec <%s>", prec_bucket->name);
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
print_grammar_unicc(byacc_t* S)
{
    int b, i, k;
    FILE *f = S->unicc_file;
    char64_t buf;

    if (!S->unicc_flag)
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
    for (k = 0; k <= S->prec; ++k) {
        int sassoc = 0;
        for (i = 0; i < S->ntokens; ++i)
        {
            Value_t prec = S->symbol_prec[i];
            if(prec == k) {
                const char *sym_name = S->symbol_name[i];
                if(sym_name[0] != '$') {
                    if(!sassoc) {
                        const char *assoc_name = get_prec_name(S, i);
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
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
	{
	    if (i > START_RULE_IDX+1 && !skip_rule) /*+1 to not print ';' at the beginning*/
		fprintf(f, "\t;\n");
	    if(!skip_rule) {
                if(b == 0 && strcmp(S->goal->name, sym_name) == 0) {
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

        if(S->ritem[k] >= 0) {
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = S->symbol_name[symbol];
                if(!skip_rule && sym_name[0] != '$') {
                    bucket *bp = lookup(S, sym_name);
                    if(bp->class == TERM) {
                        if(sym_name[0] == '\'')
                            fprintf(f, " %s", sym_name);
                        else
                            fprintf(f, " \"%s\"", sym_name);
                        print_symbol_prec_commented(S, f, symbol);
                    }
                    else
                        fprintf(f, " %s", get_normalized_rule_name(&buf, sym_name));
                    print_symbol_prec_commented(S, f, symbol);
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
            bucket *prec_bucket = S->rprec_bucket[i];
            if(prec_bucket) {
                if(prec_bucket->name[0] == '\'')
                    fprintf(f, " #precedence %s", prec_bucket->name);
                else
                    fprintf(f, " #precedence \"%s\"", prec_bucket->name);
                print_symbol_prec_assoc_commented(f, prec_bucket->prec, prec_bucket->assoc);
                fprintf(f, "\n");
            }
            else fprintf(f, "\n");
        }
    }
    fprintf(f, "\t;\n"); /*last rule*/
}

static void
print_grammar_carburetta(byacc_t* S)
{
    int b, i, k;
    FILE *f = S->carburetta_file;
    char64_t buf;

    if (!S->carburetta_flag)
	return;

    for (i = 0; i < S->ntokens; ++i)
    {
        const char *sym_name = S->symbol_name[i];
        if(!(sym_name[0] == '$' || strcmp(sym_name, "error") == 0))
            fprintf(f, "%%token %s\n", get_lemon_token_name(&buf, sym_name));
    }
    fprintf(f, "\n");
    
    //fprintf(f, "\n%%start %s\n", goal->name);
    fprintf(f, "\n%%grammar%%\n\n");
    for (b = 0, i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
	{
	    if(!skip_rule) {
                bucket *bp = lookup(S, sym_name);
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
    for (i = START_RULE_IDX; i < S->nrules; ++i)
    {
        const char *sym_name = get_normalized_rule_name(&buf, S->symbol_name[S->rlhs[i]]);
        int skip_rule = sym_name[0] == '$';
	if (S->rlhs[i] != S->rlhs[i - 1])
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

        if(S->ritem[k] >= 0) {
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = get_normalized_rule_name(&buf, S->symbol_name[symbol]);
                if(!skip_rule && sym_name[0] != '$') {
                    fprintf(f, " %s", sym_name);
                    print_symbol_prec_commented(S, f, symbol);
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
            bucket *prec_bucket = S->rprec_bucket[i];
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
    for(i=0, i2 = 0; i<(int)(sizeof(char64_t)-1) && tkname[i2]; ++i, ++i2) {
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
print_grammar_sql(byacc_t* S)
{
    int b, i, j, k;
    FILE *f = S->sql_file;
    bucket *bp;
    char64_t buf;

    if (!S->sql_flag)
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

    for (bp = S->first_symbol, b = 0; bp; bp = bp->next)
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

    for (k = 0, b = 0; k <= S->prec; ++k) {
        for (i = 0; i < S->ntokens; ++i)
        {
            Value_t prec = S->symbol_prec[i];
            if(prec == k && prec > 0) {
                const char *sym_name = S->symbol_name[i];
                if(sym_name[0] != '$') {
                    if(b==0) {
                        fprintf(f,
                           "INSERT INTO precedence(id,prec,assoc)"
                           " VALUES\n");
                    }
                    const char *assoc_name = get_prec_name(S, i);
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

    for (i = START_RULE_IDX, b = 0; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        if(sym_name[0] != '$') {
            if(b==0) {
              fprintf(f,
                "INSERT INTO rule(ruleid,lhs,prec_id) VALUES\n");
            }
            fprintf(f,
              " %s(%d,%d,",
              (b > 0) ? "," : " ", i, S->rlhs[i]
            );
            bucket *prec_bucket = S->rprec_bucket[i];
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
            S->goal->index
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
    for (i = START_RULE_IDX, b = 0; i < S->nrules; ++i)
    {
        const char *sym_name = S->symbol_name[S->rlhs[i]];
        int skip_rule = sym_name[0] == '$';
        j = 0;
        if(S->ritem[k] >= 0) { /*not an empty rule*/
            while (S->ritem[k] >= 0)
            {
                Value_t symbol = S->ritem[k];
                sym_name = S->symbol_name[symbol];
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
        } else { /* empty rule */
            if(!skip_rule) fprintf(f,
              " %s(%d,%d,NULL)",
              (b > 0) ? "," : " ", i, j++
            );
            ++b;
            if((b%5) == 0) fprintf(f, "\n");
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
finalize_destructors(byacc_t* S)
{
    int i;
    bucket *bp;

    for (i = 2; i < S->nsyms; ++i)
    {
	char *tag = S->symbol_type_tag[i];

	if (S->symbol_destructor[i] == NULL)
	{
	    if (tag == NULL)
	    {			/* use <> destructor, if there is one */
		if ((bp = S->default_destructor[UNTYPED_DEFAULT]) != NULL)
		{
		    S->symbol_destructor[i] = TMALLOC(char,
						   strlen(bp->destructor) + 1);
		    NO_SPACE(S->symbol_destructor[i]);
		    strcpy(S->symbol_destructor[i], bp->destructor);
		}
	    }
	    else
	    {			/* use type destructor for this tag, if there is one */
		bp = lookup_type_destructor(S, tag);
		if (bp->destructor != NULL)
		{
		    S->symbol_destructor[i] = TMALLOC(char,
						   strlen(bp->destructor) + 1);
		    NO_SPACE(S->symbol_destructor[i]);
		    strcpy(S->symbol_destructor[i], bp->destructor);
		}
		else
		{		/* use <*> destructor, if there is one */
		    if ((bp = S->default_destructor[TYPED_DEFAULT]) != NULL)
			/* replace "$$" with "(*val).tag" in destructor code */
			S->symbol_destructor[i]
			    = process_destructor_XX(S, bp->destructor, tag);
		}
	    }
	}
	else
	{			/* replace "$$" with "(*val)[.tag]" in destructor code */
	    char *destructor_source = S->symbol_destructor[i];
	    S->symbol_destructor[i]
		= process_destructor_XX(S, destructor_source, tag);
	    FREE(destructor_source);
	}
    }
    /* 'symbol_type_tag[]' elements are freed by 'free_tags()' */
    DO_FREE(S->symbol_type_tag);	/* no longer needed */
    if ((bp = S->default_destructor[UNTYPED_DEFAULT]) != NULL)
    {
	FREE(bp->name);
	/* 'bp->tag' is a static value, don't free */
	FREE(bp->destructor);
	FREE(bp);
    }
    if ((bp = S->default_destructor[TYPED_DEFAULT]) != NULL)
    {
	FREE(bp->name);
	/* 'bp->tag' is a static value, don't free */
	FREE(bp->destructor);
	FREE(bp);
    }
    if ((bp = S->default_destructor[TYPE_SPECIFIED]) != NULL)
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
reader(byacc_t* S)
{
    write_section(S, S->code_file, ygv_banner);
    create_symbol_table(S);
    read_declarations(S);
    read_grammar(S);
    pack_names(S);
    check_symbols(S);
    pack_symbols(S);
    pack_grammar(S);
    print_grammar_lemon(S); // need be before free_symbols
    print_grammar_naked(S); // need be before free_symbols
    print_grammar_nakedQ(S); // need be before free_symbols
    print_grammar_unicc(S); // need be before free_symbols
    print_grammar_carburetta(S); // need be before free_symbols
    print_grammar_sql(S); // need be before free_symbols
    free_symbol_table(S);
    print_grammar(S);
    print_grammar_ebnf(S);
    free_symbols(S);
#if defined(YYBTYACC)
    if (S->destructor)
	finalize_destructors(S);
#endif
    free_tags(S);
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
reader_leaks(byacc_t* S)
{
    S->lex_param = free_declarations(S->lex_param);
    S->parse_param = free_declarations(S->parse_param);

    DO_FREE(S->line);
    DO_FREE(S->rrhs);
    DO_FREE(S->rlhs);
    DO_FREE(S->rprec);
    DO_FREE(S->ritem);
    DO_FREE(S->rassoc);
    DO_FREE(S->rprec_bucket);
    DO_FREE(S->cache);
    DO_FREE(S->name_pool);
    DO_FREE(S->symbol_name);
    DO_FREE(S->symbol_prec);
    DO_FREE(S->symbol_assoc);
    DO_FREE(S->symbol_value);
#if defined(YYBTYACC)
    DO_FREE(S->symbol_pval);
    DO_FREE(S->symbol_destructor);
    DO_FREE(S->symbol_type_tag);
#endif
}
#endif
