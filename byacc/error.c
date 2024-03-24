/* $Id: error.c,v 1.17 2023/05/18 21:38:35 tom Exp $ */

/* routines for printing error messages  */

#include "defs.h"

void
fatal(byacc_t* S, const char *msg)
{
    fprintf(stderr, "%s: f - %s\n", S->myname, msg);
    done(S, 2);
}

void
on_error(byacc_t* S)
{
    const char *msg;
    if (errno && (msg = strerror(errno)) != NULL)
	fatal(S, msg);
    else
	fatal(S, "unknown error");
}

void
open_error(byacc_t* S, const char *filename)
{
    fprintf(stderr, "%s: f - cannot open \"%s\"\n", S->myname, filename);
    done(S, 2);
}

void
missing_brace(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", missing '}'\n",
	    S->myname, S->lineno, S->input_file_name);
    done(S, 1);
}

void
unexpected_EOF(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unexpected end-of-file\n",
	    S->myname, S->lineno, S->input_file_name);
    done(S, 1);
}

static void
print_pos(const char *st_line, const char *st_cptr)
{
    const char *s;

    if (st_line == 0)
	return;
    for (s = st_line; *s != '\n'; ++s)
    {
	if (isprint(UCH(*s)) || *s == '\t')
	    putc(*s, stderr);
	else
	    putc('?', stderr);
    }
    putc('\n', stderr);
    for (s = st_line; s < st_cptr; ++s)
    {
	if (*s == '\t')
	    putc('\t', stderr);
	else
	    putc(' ', stderr);
    }
    putc('^', stderr);
    putc('\n', stderr);
}

void
syntax_error(byacc_t* S, int st_lineno, char *st_line, char *st_cptr)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", syntax error\n",
	    S->myname, st_lineno, S->input_file_name);
    print_pos(st_line, st_cptr);
    done(S, 1);
}

void
unexpected_value(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unexpected value\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
unterminated_comment(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unmatched /*\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
unterminated_string(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unterminated string\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
unterminated_text(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unmatched %%{\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
unterminated_union(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unterminated %%union \
declaration\n", S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
over_unionized(byacc_t* S, char *u_cptr)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", too many %%union \
declarations\n", S->myname, S->lineno, S->input_file_name);
    print_pos(S->line, u_cptr);
    done(S, 1);
}

void
illegal_tag(byacc_t* S, int t_lineno, char *t_line, char *t_cptr)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", illegal tag\n",
	    S->myname, t_lineno, S->input_file_name);
    print_pos(t_line, t_cptr);
    done(S, 1);
}

void
illegal_character(byacc_t* S, char *c_cptr)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", illegal character\n",
	    S->myname, S->lineno, S->input_file_name);
    print_pos(S->line, c_cptr);
    done(S, 1);
}

void
used_reserved(byacc_t* S, char *s)
{
    fprintf(stderr,
	    "%s: e - line %d of \"%s\", illegal use of reserved symbol \
%s\n", S->myname, S->lineno, S->input_file_name, s);
    done(S, 1);
}

void
tokenized_start(byacc_t* S, char *s)
{
    fprintf(stderr,
	    "%s: e - line %d of \"%s\", the start symbol %s cannot be \
declared to be a token\n", S->myname, S->lineno, S->input_file_name, s);
    done(S, 1);
}

void
retyped_warning(byacc_t* S, char *s)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", the type of %s has been \
redeclared\n", S->myname, S->lineno, S->input_file_name, s);
}

void
reprec_warning(byacc_t* S, char *s)
{
    fprintf(stderr,
	    "%s: w - line %d of \"%s\", the precedence of %s has been \
redeclared\n", S->myname, S->lineno, S->input_file_name, s);
}

void
revalued_warning(byacc_t* S, char *s)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", the value of %s has been \
redeclared\n", S->myname, S->lineno, S->input_file_name, s);
}

void
terminal_start(byacc_t* S, char *s)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", the start symbol %s is a \
token\n", S->myname, S->lineno, S->input_file_name, s);
    done(S, 1);
}

void
restarted_warning(byacc_t* S)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", the start symbol has been \
redeclared\n", S->myname, S->lineno, S->input_file_name);
}

void
no_grammar(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", no grammar has been \
specified\n", S->myname, S->lineno, S->input_file_name);
    done(S, 1);
}

void
terminal_lhs(byacc_t* S, int s_lineno)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", a token appears on the lhs \
of a production\n", S->myname, s_lineno, S->input_file_name);
    done(S, 1);
}

void
prec_redeclared(byacc_t* S)
{
    fprintf(stderr, "%s: w - line %d of  \"%s\", conflicting %%prec \
specifiers\n", S->myname, S->lineno, S->input_file_name);
}

void
unterminated_action(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", unterminated action\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
dollar_warning(byacc_t* S, int a_lineno, int i)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", $%d references beyond the \
end of the current rule\n", S->myname, a_lineno, S->input_file_name, i);
}

void
dollar_error(byacc_t* S, int a_lineno, char *a_line, char *a_cptr)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", illegal $-name\n",
	    S->myname, a_lineno, S->input_file_name);
    print_pos(a_line, a_cptr);
    done(S, 1);
}

void
dislocations_warning(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", expected %%locations\n",
	    S->myname, S->lineno, S->input_file_name);
}

void
untyped_lhs(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", $$ is untyped\n",
	    S->myname, S->lineno, S->input_file_name);
    done(S, 1);
}

void
untyped_rhs(byacc_t* S, int i, char *s)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", $%d (%s) is untyped\n",
	    S->myname, S->lineno, S->input_file_name, i, s);
    done(S, 1);
}

void
unknown_rhs(byacc_t* S, int i)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", $%d is untyped\n",
	    S->myname, S->lineno, S->input_file_name, i);
    done(S, 1);
}

void
default_action_warning(byacc_t* S, char *s)
{
    fprintf(stderr,
	    "%s: w - line %d of \"%s\", the default action for %s assigns an \
undefined value to $$\n",
	    S->myname, S->lineno, S->input_file_name, s);
}

void
undefined_goal(byacc_t* S, char *s)
{
    fprintf(stderr, "%s: e - the start symbol %s is undefined\n", S->myname, s);
    done(S, 1);
}

void
undefined_symbol_warning(byacc_t* S, char *s)
{
    fprintf(stderr, "%s: w - the symbol %s is undefined\n", S->myname, s);
}

#if ! defined(YYBTYACC)
void
unsupported_flag_warning(byacc_t* S, const char *flag, const char *details)
{
    fprintf(stderr, "%s: w - %s flag unsupported, %s\n",
	    S->myname, flag, details);
}
#endif

#if defined(YYBTYACC)
void
at_warning(byacc_t* S, int a_lineno, int i)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", @%d references beyond the \
end of the current rule\n", S->myname, a_lineno, S->input_file_name, i);
}

void
at_error(byacc_t* S, int a_lineno, char *a_line, char *a_cptr)
{
    fprintf(stderr,
	    "%s: e - line %d of \"%s\", illegal @$ or @N reference\n",
	    S->myname, a_lineno, S->input_file_name);
    print_pos(a_line, a_cptr);
    done(S, 1);
}

void
unterminated_arglist(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr,
	    "%s: e - line %d of \"%s\", unterminated argument list\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
    done(S, 1);
}

void
arg_number_disagree_warning(byacc_t* S, int a_lineno, char *a_name)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", number of arguments of %s "
	    "doesn't agree with previous declaration\n",
	    S->myname, a_lineno, S->input_file_name, a_name);
}

void
bad_formals(byacc_t* S)
{
    fprintf(stderr, "%s: e - line %d of \"%s\", bad formal argument list\n",
	    S->myname, S->lineno, S->input_file_name);
    print_pos(S->line, S->cptr);
    done(S, 1);
}

void
arg_type_disagree_warning(byacc_t* S, int a_lineno, int i, char *a_name)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", type of argument %d "
	    "to %s doesn't agree with previous declaration\n",
	    S->myname, a_lineno, S->input_file_name, i, a_name);
}

void
unknown_arg_warning(byacc_t* S, int d_lineno, const char *dlr_opt,
			const char *d_arg,
			const char *d_line,
			const char *d_cptr)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", unknown argument %s%s\n",
	    S->myname, d_lineno, S->input_file_name, dlr_opt, d_arg);
    print_pos(d_line, d_cptr);
}

void
untyped_arg_warning(byacc_t* S, int a_lineno, const char *dlr_opt, const char *a_name)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", untyped argument %s%s\n",
	    S->myname, a_lineno, S->input_file_name, dlr_opt, a_name);
}

void
wrong_number_args_warning(byacc_t* S, const char *which, const char *a_name)
{
    fprintf(stderr,
	    "%s: w - line %d of \"%s\", wrong number of %sarguments for %s\n",
	    S->myname, S->lineno, S->input_file_name, which, a_name);
    print_pos(S->line, S->cptr);
}

void
wrong_type_for_arg_warning(byacc_t* S, int i, char *a_name)
{
    fprintf(stderr,
	    "%s: w - line %d of \"%s\", wrong type for default argument %d to %s\n",
	    S->myname, S->lineno, S->input_file_name, i, a_name);
    print_pos(S->line, S->cptr);
}

void
start_requires_args(byacc_t* S, char *a_name)
{
    fprintf(stderr,
	    "%s: w - line %d of \"%s\", start symbol %s requires arguments\n",
	    S->myname, 0, S->input_file_name, a_name);

}

void
destructor_redeclared_warning(byacc_t* S, const struct ainfo *a)
{
    fprintf(stderr, "%s: w - line %d of \"%s\", destructor redeclared\n",
	    S->myname, a->a_lineno, S->input_file_name);
    print_pos(a->a_line, a->a_cptr);
}
#endif
