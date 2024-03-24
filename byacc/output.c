/* $Id: output.c,v 1.101 2023/05/16 21:19:48 tom Exp $ */

#include "defs.h"

#define StaticOrR	(S->rflag ? "" : "static ")
#define CountLine(fp)   (!S->rflag || ((fp) == S->code_file))

#if defined(YYBTYACC)
#define PER_STATE 3
#else
#define PER_STATE 2
#endif

static void
putc_code(byacc_t* S, FILE * fp, int c)
{
    if ((c == '\n') && (fp == S->code_file))
	++S->outline;
    putc(c, fp);
}

static void
putl_code(byacc_t* S, FILE * fp, const char *s)
{
    if (fp == S->code_file)
	++S->outline;
    fputs(s, fp);
}

static void
puts_code(FILE * fp, const char *s)
{
    fputs(s, fp);
}

static void
puts_param_types(FILE * fp, param *list, int more)
{
    param *p;

    if (list != 0)
    {
	for (p = list; p; p = p->next)
	{
	    size_t len_type = strlen(p->type);
	    fprintf(fp, "%s%s%s%s%s", p->type,
		    (((len_type != 0) && (p->type[len_type - 1] == '*'))
		     ? ""
		     : " "),
		    p->name, p->type2,
		    ((more || p->next) ? ", " : ""));
	}
    }
    else
    {
	if (!more)
	    fprintf(fp, "void");
    }
}

static void
puts_param_names(FILE * fp, param *list, int more)
{
    param *p;

    for (p = list; p; p = p->next)
    {
	fprintf(fp, "%s%s", p->name,
		((more || p->next) ? ", " : ""));
    }
}

static void
write_code_lineno(byacc_t* S, FILE * fp)
{
    if (!S->lflag && (fp == S->code_file))
    {
	++S->outline;
	fprintf_lineno(fp, S->outline + 1, S->code_file_name);
    }
}

static void
write_input_lineno(byacc_t* S)
{
    if (!S->lflag)
    {
	++S->outline;
	fprintf_lineno(S->code_file, S->lineno, S->input_file_name);
    }
}

static void
define_prefixed(byacc_t* S, FILE * fp, const char *name)
{
    int bump_line = CountLine(fp);
    if (bump_line)
	++S->outline;
    fprintf(fp, "\n");

    if (bump_line)
	++S->outline;
    fprintf(fp, "#ifndef %s\n", name);

    if (bump_line)
	++S->outline;
    fprintf(fp, "#define %-10s %s%s\n", name, S->symbol_prefix, name + 2);

    if (bump_line)
	++S->outline;
    fprintf(fp, "#endif /* %s */\n", name);
}

static void
output_prefix(byacc_t* S, FILE * fp)
{
    if (S->symbol_prefix == NULL)
    {
	S->symbol_prefix = "yy";
    }
    else
    {
	define_prefixed(S, fp, "yyparse");
	define_prefixed(S, fp, "yylex");
	define_prefixed(S, fp, "yyerror");
	define_prefixed(S, fp, "yychar");
	define_prefixed(S, fp, "yyval");
	define_prefixed(S, fp, "yylval");
	define_prefixed(S, fp, "yydebug");
	define_prefixed(S, fp, "yynerrs");
	define_prefixed(S, fp, "yyerrflag");
	define_prefixed(S, fp, "yylhs");
	define_prefixed(S, fp, "yylen");
	define_prefixed(S, fp, "yydefred");
#if defined(YYBTYACC)
	define_prefixed(S, fp, "yystos");
#endif
	define_prefixed(S, fp, "yydgoto");
	define_prefixed(S, fp, "yysindex");
	define_prefixed(S, fp, "yyrindex");
	define_prefixed(S, fp, "yygindex");
	define_prefixed(S, fp, "yytable");
	define_prefixed(S, fp, "yycheck");
	define_prefixed(S, fp, "yyname");
	define_prefixed(S, fp, "yyrule");
#if defined(YYBTYACC)
	if (S->locations)
	{
	    define_prefixed(S, fp, "yyloc");
	    define_prefixed(S, fp, "yylloc");
	}
	putc_code(S, fp, '\n');
	putl_code(S, fp, "#if YYBTYACC\n");

	define_prefixed(S, fp, "yycindex");
	define_prefixed(S, fp, "yyctable");

	putc_code(S, fp, '\n');
	putl_code(S, fp, "#endif /* YYBTYACC */\n");
	putc_code(S, fp, '\n');
#endif
    }
    if (CountLine(fp))
	++S->outline;
    fprintf(fp, "#define YYPREFIX \"%s\"\n", S->symbol_prefix);
}

static void
output_code_lines(byacc_t* S, FILE * fp, int cl)
{
    if (S->code_lines[cl].lines != NULL)
    {
	if (fp == S->code_file)
	{
	    S->outline += (int)S->code_lines[cl].num;
	    S->outline += 3;
	    fprintf(fp, "\n");
	}
	fprintf(fp, "/* %%code \"%s\" block start */\n", S->code_lines[cl].name);
	fputs(S->code_lines[cl].lines, fp);
	fprintf(fp, "/* %%code \"%s\" block end */\n", S->code_lines[cl].name);
	if (fp == S->code_file)
	{
	    write_code_lineno(S, fp);
	}
    }
}

static void
output_newline(byacc_t* S)
{
    if (!S->rflag)
	++S->outline;
    putc('\n', S->output_file);
}

static void
output_line(byacc_t* S, const char *value)
{
    fputs(value, S->output_file);
    output_newline(S);
}

static void
output_int(byacc_t* S, int value)
{
    fprintf(S->output_file, "%5d,", value);
}

static void
start_int_table(byacc_t* S, const char *name, int value)
{
    int need = 34 - (int)(strlen(S->symbol_prefix) + strlen(name));

    if (need < 6)
	need = 6;
    fprintf(S->output_file,
	    "%sconst YYINT %s%s[] = {%*d,",
	    StaticOrR, S->symbol_prefix, name, need, value);
}

static void
start_str_table(byacc_t* S, const char *name)
{
    fprintf(S->output_file,
	    "%sconst char *const %s%s[] = {",
	    StaticOrR, S->symbol_prefix, name);
    output_newline(S);
}

static void
end_table(byacc_t* S)
{
    output_newline(S);
    output_line(S, "};");
}

static void
output_stype(byacc_t* S, FILE * fp)
{
    if (!S->unionized && S->ntags == 0)
    {
	putc_code(S, fp, '\n');
	putl_code(S, fp, "#if "
		  "! defined(YYSTYPE) && "
		  "! defined(YYSTYPE_IS_DECLARED)\n");
	putl_code(S, fp, "/* Default: YYSTYPE is the semantic value type. */\n");
	putl_code(S, fp, "typedef int YYSTYPE;\n");
	putl_code(S, fp, "# define YYSTYPE_IS_DECLARED 1\n");
	putl_code(S, fp, "#endif\n");
    }
}

#if defined(YYBTYACC)
static void
output_ltype(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');
    putl_code(S, fp, "#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED\n");
    putl_code(S, fp, "/* Default: YYLTYPE is the text position type. */\n");
    putl_code(S, fp, "typedef struct YYLTYPE\n");
    putl_code(S, fp, "{\n");
    putl_code(S, fp, "    int first_line;\n");
    putl_code(S, fp, "    int first_column;\n");
    putl_code(S, fp, "    int last_line;\n");
    putl_code(S, fp, "    int last_column;\n");
    putl_code(S, fp, "    unsigned source;\n");
    putl_code(S, fp, "} YYLTYPE;\n");
    putl_code(S, fp, "#define YYLTYPE_IS_DECLARED 1\n");
    putl_code(S, fp, "#endif\n");
    putl_code(S, fp, "#define YYRHSLOC(rhs, k) ((rhs)[k])\n");
}
#endif

static void
output_YYINT_typedef(byacc_t* S, FILE * fp)
{
    /* generate the type used to index the various parser tables */
    if (CountLine(fp))
	++S->outline;
    fprintf(fp, "typedef %s YYINT;\n", CONCAT1("", YYINT));
}

static void
output_rule_data(byacc_t* S)
{
    int i;
    int j;

    output_YYINT_typedef(S, S->output_file);

    start_int_table(S, "lhs", S->symbol_value[S->start_symbol]);

    j = 10;
    for (i = 3; i < S->nrules; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->symbol_value[S->rlhs[i]]);
    }
    end_table(S);

    start_int_table(S, "len", 2);

    j = 10;
    for (i = 3; i < S->nrules; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    j++;

	output_int(S, S->rrhs[i + 1] - S->rrhs[i] - 1);
    }
    end_table(S);
}

static void
output_yydefred(byacc_t* S)
{
    int i, j;

    start_int_table(S, "defred", (S->defred[0] ? S->defred[0] - RULE_NUM_OFFSET : 0));

    j = 10;
    for (i = 1; i < S->nstates; i++)
    {
	if (j < 10)
	    ++j;
	else
	{
	    output_newline(S);
	    j = 1;
	}

	output_int(S, (S->defred[i] ? S->defred[i] - RULE_NUM_OFFSET : 0));
    }

    end_table(S);
}

#if defined(YYBTYACC)
static void
output_accessing_symbols(byacc_t* S)
{
    if (S->nstates != 0)
    {
	int i, j;
	int *translate;

	translate = TCMALLOC(int, S->nstates);
	NO_SPACE(translate);

	for (i = 0; i < S->nstates; ++i)
	{
	    int gsymb = S->accessing_symbol[i];

	    translate[i] = S->symbol_pval[gsymb];
	}

	putl_code(S, S->output_file,
		  "#if defined(YYDESTRUCT_CALL) || defined(YYSTYPE_TOSTRING)\n");
	/* yystos[] may be unused, depending on compile-time defines */
	start_int_table(S, "stos", translate[0]);

	j = 10;
	for (i = 1; i < S->nstates; ++i)
	{
	    if (j < 10)
		++j;
	    else
	    {
		output_newline(S);
		j = 1;
	    }

	    output_int(S, translate[i]);
	}

	end_table(S);
	FREE(translate);
	putl_code(S, S->output_file,
		  "#endif /* YYDESTRUCT_CALL || YYSTYPE_TOSTRING */\n");
    }
}

static Value_t
find_conflict_base(byacc_t* S, int cbase)
{
    int i, j;

    for (i = 0; i < cbase; i++)
    {
	for (j = 0; j + cbase < S->nconflicts; j++)
	{
	    if (S->conflicts[i + j] != S->conflicts[cbase + j])
		break;
	}
	if (j + cbase >= S->nconflicts)
	    break;
    }
    return (Value_t)i;
}
#endif

static void
token_actions(byacc_t* S)
{
    int i, j;
    Value_t shiftcount, reducecount;
#if defined(YYBTYACC)
    Value_t conflictcount = 0;
    Value_t csym = -1;
    Value_t cbase = 0;
#endif
    Value_t max, min;
    Value_t *actionrow, *r, *s;
    action *p;

    actionrow = NEW2(PER_STATE * S->ntokens, Value_t);
    for (i = 0; i < S->nstates; ++i)
    {
	if (S->parser[i])
	{
	    for (j = 0; j < PER_STATE * S->ntokens; ++j)
		actionrow[j] = 0;

	    shiftcount = 0;
	    reducecount = 0;
#if defined(YYBTYACC)
	    if (S->backtrack)
	    {
		conflictcount = 0;
		csym = -1;
		cbase = S->nconflicts;
	    }
#endif
	    for (p = S->parser[i]; p; p = p->next)
	    {
#if defined(YYBTYACC)
		if (S->backtrack)
		{
		    if (csym != -1 && csym != p->symbol)
		    {
			conflictcount++;
			S->conflicts[S->nconflicts++] = -1;
			j = find_conflict_base(S, cbase);
			actionrow[csym + 2 * S->ntokens] = (Value_t)(j + 1);
			if (j == cbase)
			{
			    cbase = S->nconflicts;
			}
			else
			{
			    if (S->conflicts[cbase] == -1)
				cbase++;
			    S->nconflicts = cbase;
			}
			csym = -1;
		    }
		}
#endif
		if (p->suppressed == 0)
		{
		    if (p->action_code == SHIFT)
		    {
			++shiftcount;
			actionrow[p->symbol] = p->number;
		    }
		    else if (p->action_code == REDUCE && p->number != S->defred[i])
		    {
			++reducecount;
			actionrow[p->symbol + S->ntokens] = p->number;
		    }
		}
#if defined(YYBTYACC)
		else if (S->backtrack && p->suppressed == 1)
		{
		    csym = p->symbol;
		    if (p->action_code == SHIFT)
		    {
			S->conflicts[S->nconflicts++] = p->number;
		    }
		    else if (p->action_code == REDUCE && p->number != S->defred[i])
		    {
			if (cbase == S->nconflicts)
			{
			    if (cbase)
				cbase--;
			    else
				S->conflicts[S->nconflicts++] = -1;
			}
			S->conflicts[S->nconflicts++] = (Value_t)(p->number - RULE_NUM_OFFSET);
		    }
		}
#endif
	    }
#if defined(YYBTYACC)
	    if (S->backtrack && csym != -1)
	    {
		conflictcount++;
		S->conflicts[S->nconflicts++] = -1;
		j = find_conflict_base(S, cbase);
		actionrow[csym + 2 * S->ntokens] = (Value_t)(j + 1);
		if (j == cbase)
		{
		    cbase = S->nconflicts;
		}
		else
		{
		    if (S->conflicts[cbase] == -1)
			cbase++;
		    S->nconflicts = cbase;
		}
	    }
#endif

	    S->tally[i] = shiftcount;
	    S->tally[S->nstates + i] = reducecount;
#if defined(YYBTYACC)
	    if (S->backtrack)
		S->tally[2 * S->nstates + i] = conflictcount;
#endif
	    S->width[i] = 0;
	    S->width[S->nstates + i] = 0;
#if defined(YYBTYACC)
	    if (S->backtrack)
		S->width[2 * S->nstates + i] = 0;
#endif
	    if (shiftcount > 0)
	    {
		S->froms[i] = r = NEW2(shiftcount, Value_t);
		S->tos[i] = s = NEW2(shiftcount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < S->ntokens; ++j)
		{
		    if (actionrow[j])
		    {
			if (min > S->symbol_value[j])
			    min = S->symbol_value[j];
			if (max < S->symbol_value[j])
			    max = S->symbol_value[j];
			*r++ = S->symbol_value[j];
			*s++ = actionrow[j];
		    }
		}
		S->width[i] = (Value_t)(max - min + 1);
	    }
	    if (reducecount > 0)
	    {
		S->froms[S->nstates + i] = r = NEW2(reducecount, Value_t);
		S->tos[S->nstates + i] = s = NEW2(reducecount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < S->ntokens; ++j)
		{
		    if (actionrow[S->ntokens + j])
		    {
			if (min > S->symbol_value[j])
			    min = S->symbol_value[j];
			if (max < S->symbol_value[j])
			    max = S->symbol_value[j];
			*r++ = S->symbol_value[j];
			*s++ = (Value_t)(actionrow[S->ntokens + j] - RULE_NUM_OFFSET);
		    }
		}
		S->width[S->nstates + i] = (Value_t)(max - min + 1);
	    }
#if defined(YYBTYACC)
	    if (S->backtrack && conflictcount > 0)
	    {
		S->froms[2 * S->nstates + i] = r = NEW2(conflictcount, Value_t);
		S->tos[2 * S->nstates + i] = s = NEW2(conflictcount, Value_t);
		min = MAXYYINT;
		max = 0;
		for (j = 0; j < S->ntokens; ++j)
		{
		    if (actionrow[2 * S->ntokens + j])
		    {
			if (min > S->symbol_value[j])
			    min = S->symbol_value[j];
			if (max < S->symbol_value[j])
			    max = S->symbol_value[j];
			*r++ = S->symbol_value[j];
			*s++ = (Value_t)(actionrow[2 * S->ntokens + j] - 1);
		    }
		}
		S->width[2 * S->nstates + i] = (Value_t)(max - min + 1);
	    }
#endif
	}
    }
    FREE(actionrow);
}

static int
default_goto(byacc_t* S, int symbol)
{
    int i;
    int m;
    int n;
    int default_state;
    int max;

    m = S->goto_map[symbol];
    n = S->goto_map[symbol + 1];

    if (m == n)
	return (0);

    for (i = 0; i < S->nstates; i++)
	S->state_count[i] = 0;

    for (i = m; i < n; i++)
	S->state_count[S->to_state[i]]++;

    max = 0;
    default_state = 0;
    for (i = 0; i < S->nstates; i++)
    {
	if (S->state_count[i] > max)
	{
	    max = S->state_count[i];
	    default_state = i;
	}
    }

    return (default_state);
}

static void
save_column(byacc_t* S, int symbol, int default_state)
{
    int i;
    int m;
    int n;
    Value_t *sp;
    Value_t *sp1;
    Value_t *sp2;
    Value_t count;
    int symno;

    m = S->goto_map[symbol];
    n = S->goto_map[symbol + 1];

    count = 0;
    for (i = m; i < n; i++)
    {
	if (S->to_state[i] != default_state)
	    ++count;
    }
    if (count == 0)
	return;

    symno = S->symbol_value[symbol] + PER_STATE * S->nstates;

    S->froms[symno] = sp1 = sp = NEW2(count, Value_t);
    S->tos[symno] = sp2 = NEW2(count, Value_t);

    for (i = m; i < n; i++)
    {
	if (S->to_state[i] != default_state)
	{
	    *sp1++ = S->from_state[i];
	    *sp2++ = S->to_state[i];
	}
    }

    S->tally[symno] = count;
    S->width[symno] = (Value_t)(sp1[-1] - sp[0] + 1);
}

static void
goto_actions(byacc_t* S)
{
    int i, j, k;

    S->state_count = NEW2(S->nstates, Value_t);

    k = default_goto(S, S->start_symbol + 1);
    start_int_table(S, "dgoto", k);
    save_column(S, S->start_symbol + 1, k);

    j = 10;
    for (i = S->start_symbol + 2; i < S->nsyms; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	k = default_goto(S, i);
	output_int(S, k);
	save_column(S, i, k);
    }

    end_table(S);
    FREE(S->state_count);
}

static void
sort_actions(byacc_t* S)
{
    Value_t i;
    int j;
    int k;
    int t;
    int w;

    S->order = NEW2(S->nvectors, Value_t);
    S->nentries = 0;

    for (i = 0; i < S->nvectors; i++)
    {
	if (S->tally[i] > 0)
	{
	    t = S->tally[i];
	    w = S->width[i];
	    j = S->nentries - 1;

	    while (j >= 0 && (S->width[S->order[j]] < w))
		j--;

	    while (j >= 0 && (S->width[S->order[j]] == w) && (S->tally[S->order[j]] < t))
		j--;

	    for (k = S->nentries - 1; k > j; k--)
		S->order[k + 1] = S->order[k];

	    S->order[j + 1] = i;
	    S->nentries++;
	}
    }
}

/*  The function matching_vector determines if the vector specified by	*/
/*  the input parameter matches a previously considered	vector.  The	*/
/*  test at the start of the function checks if the vector represents	*/
/*  a row of shifts over terminal symbols or a row of reductions, or a	*/
/*  column of shifts over a nonterminal symbol.  Berkeley Yacc does not	*/
/*  check if a column of shifts over a nonterminal symbols matches a	*/
/*  previously considered vector.  Because of the nature of LR parsing	*/
/*  tables, no two columns can match.  Therefore, the only possible	*/
/*  match would be between a row and a column.  Such matches are	*/
/*  unlikely.  Therefore, to save time, no attempt is made to see if a	*/
/*  column matches a previously considered vector.			*/
/*									*/
/*  Matching_vector is poorly designed.  The test could easily be made	*/
/*  faster.  Also, it depends on the vectors being in a specific	*/
/*  order.								*/
#if defined(YYBTYACC)
/*									*/
/*  Not really any point in checking for matching conflicts -- it is    */
/*  extremely unlikely to occur, and conflicts are (hopefully) rare.    */
#endif

static int
matching_vector(byacc_t* S, int vector)
{
    int i;
    int k;
    int t;
    int w;
    int prev;

    i = S->order[vector];
    if (i >= 2 * S->nstates)
	return (-1);

    t = S->tally[i];
    w = S->width[i];

    for (prev = vector - 1; prev >= 0; prev--)
    {
	int j = S->order[prev];

	if (S->width[j] != w || S->tally[j] != t)
	{
	    return (-1);
	}
	else
	{
	    int match = 1;

	    for (k = 0; match && k < t; k++)
	    {
		if (S->tos[j][k] != S->tos[i][k] || S->froms[j][k] != S->froms[i][k])
		    match = 0;
	    }

	    if (match)
		return (j);
	}
    }

    return (-1);
}

static int
pack_vector(byacc_t* S, int vector)
{
    int i, j, k, l;
    int t;
    Value_t loc;
    int ok;
    Value_t *from;
    Value_t *to;
    int newmax;

    i = S->order[vector];
    t = S->tally[i];
    assert(t);

    from = S->froms[i];
    to = S->tos[i];

    j = S->lowzero - from[0];
    for (k = 1; k < t; ++k)
	if (S->lowzero - from[k] > j)
	    j = S->lowzero - from[k];
    for (;; ++j)
    {
	if (j == 0)
	    continue;
	ok = 1;
	for (k = 0; ok && k < t; k++)
	{
	    loc = (Value_t)(j + from[k]);
	    if (loc >= S->maxtable - 1)
	    {
		if (loc >= MAXTABLE - 1)
		    fatal(S, "maximum table size exceeded");

		newmax = S->maxtable;
		do
		{
		    newmax += 200;
		}
		while (newmax <= loc);

		S->table = TREALLOC(Value_t, S->table, newmax);
		NO_SPACE(S->table);

		S->check = TREALLOC(Value_t, S->check, newmax);
		NO_SPACE(S->check);

		for (l = S->maxtable; l < newmax; ++l)
		{
		    S->table[l] = 0;
		    S->check[l] = -1;
		}
		S->maxtable = newmax;
	    }

	    if (S->check[loc] != -1)
		ok = 0;
	}
	for (k = 0; ok && k < vector; k++)
	{
	    if (S->pos[k] == j)
		ok = 0;
	}
	if (ok)
	{
	    for (k = 0; k < t; k++)
	    {
		loc = (Value_t)(j + from[k]);
		S->table[loc] = to[k];
		S->check[loc] = from[k];
		if (loc > S->high)
		    S->high = loc;
	    }

	    while (S->check[S->lowzero] != -1)
		++S->lowzero;

	    return (j);
	}
    }
}

static void
pack_table(byacc_t* S)
{
    int i;
    Value_t place;

    S->base = NEW2(S->nvectors, Value_t);
    S->pos = NEW2(S->nentries, Value_t);

    S->maxtable = 1000;
    S->table = NEW2(S->maxtable, Value_t);
    S->check = NEW2(S->maxtable, Value_t);

    S->lowzero = 0;
    S->high = 0;

    for (i = 0; i < S->maxtable; i++)
	S->check[i] = -1;

    for (i = 0; i < S->nentries; i++)
    {
	int state = matching_vector(S, i);

	if (state < 0)
	    place = (Value_t)pack_vector(S, i);
	else
	    place = S->base[state];

	S->pos[i] = place;
	S->base[S->order[i]] = place;
    }

    for (i = 0; i < S->nvectors; i++)
    {
	if (S->froms[i])
	    FREE(S->froms[i]);
	if (S->tos[i])
	    FREE(S->tos[i]);
    }

    DO_FREE(S->froms);
    DO_FREE(S->tos);
    DO_FREE(S->tally);
    DO_FREE(S->width);
    DO_FREE(S->pos);
}

static void
output_base(byacc_t* S)
{
    int i, j;

    start_int_table(S, "sindex", S->base[0]);

    j = 10;
    for (i = 1; i < S->nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->base[i]);
    }

    end_table(S);

    start_int_table(S, "rindex", S->base[S->nstates]);

    j = 10;
    for (i = S->nstates + 1; i < 2 * S->nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->base[i]);
    }

    end_table(S);

#if defined(YYBTYACC)
    output_line(S, "#if YYBTYACC");
    start_int_table(S, "cindex", S->base[2 * S->nstates]);

    j = 10;
    for (i = 2 * S->nstates + 1; i < 3 * S->nstates; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->base[i]);
    }

    end_table(S);
    output_line(S, "#endif");
#endif

    start_int_table(S, "gindex", S->base[PER_STATE * S->nstates]);

    j = 10;
    for (i = PER_STATE * S->nstates + 1; i < S->nvectors - 1; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->base[i]);
    }

    end_table(S);
    FREE(S->base);
}

static void
output_table(byacc_t* S)
{
    int i;
    int j;

    if (S->high >= MAXYYINT)
    {
	fprintf(stderr, "YYTABLESIZE: %ld\n", S->high);
	fprintf(stderr, "Table is longer than %ld elements.\n", (long)MAXYYINT);
	done(S, 1);
    }

    ++S->outline;
    fprintf(S->code_file, "#define YYTABLESIZE %ld\n", S->high);
    start_int_table(S, "table", S->table[0]);

    j = 10;
    for (i = 1; i <= S->high; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->table[i]);
    }

    end_table(S);
    FREE(S->table);
}

static void
output_check(byacc_t* S)
{
    int i;
    int j;

    start_int_table(S, "check", S->check[0]);

    j = 10;
    for (i = 1; i <= S->high; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, S->check[i]);
    }

    end_table(S);
    FREE(S->check);
}

#if defined(YYBTYACC)
static void
output_ctable(byacc_t* S)
{
    int i;
    int j;
    int limit = (S->conflicts != 0) ? S->nconflicts : 0;

    if (limit < S->high)
	limit = (int)S->high;

    output_line(S, "#if YYBTYACC");
    start_int_table(S, "ctable", S->conflicts ? S->conflicts[0] : -1);

    j = 10;
    for (i = 1; i < limit; i++)
    {
	if (j >= 10)
	{
	    output_newline(S);
	    j = 1;
	}
	else
	    ++j;

	output_int(S, (S->conflicts != 0 && i < S->nconflicts) ? S->conflicts[i] : -1);
    }

    if (S->conflicts)
	FREE(S->conflicts);

    end_table(S);
    output_line(S, "#endif");
}
#endif

static void
output_actions(byacc_t* S)
{
    S->nvectors = PER_STATE * S->nstates + S->nvars;

    S->froms = NEW2(S->nvectors, Value_t *);
    S->tos = NEW2(S->nvectors, Value_t *);
    S->tally = NEW2(S->nvectors, Value_t);
    S->width = NEW2(S->nvectors, Value_t);

#if defined(YYBTYACC)
    if (S->backtrack && (S->SRtotal + S->RRtotal) != 0)
	S->conflicts = NEW2(4 * (S->SRtotal + S->RRtotal), Value_t);
#endif

    token_actions(S);
    FREE(S->lookaheads);
    FREE(S->LA);
    FREE(S->LAruleno);
    FREE(S->accessing_symbol);

    goto_actions(S);
    FREE(S->goto_base);
    FREE(S->from_state);
    FREE(S->to_state);

    sort_actions(S);
    pack_table(S);
    output_base(S);
    output_table(S);
    output_check(S);
#if defined(YYBTYACC)
    output_ctable(S);
#endif
}

static int
is_C_identifier(char *name)
{
    char *s;
    int c;

    s = name;
    c = *s;
    if (c == '"')
    {
	c = *++s;
	if (!IS_NAME1(c))
	    return (0);
	while ((c = *++s) != '"')
	{
	    if (!IS_NAME2(c))
		return (0);
	}
	return (1);
    }

    if (!IS_NAME1(c))
	return (0);
    while ((c = *++s) != 0)
    {
	if (!IS_NAME2(c))
	    return (0);
    }
    return (1);
}

#if USE_HEADER_GUARDS
static void
start_defines_file(byacc_t* S)
{
    fprintf(S->defines_file, "#ifndef _%s_defines_h_\n", S->symbol_prefix);
    fprintf(S->defines_file, "#define _%s_defines_h_\n\n", S->symbol_prefix);
}

static void
end_defines_file(byacc_t* S)
{
    fprintf(S->defines_file, "\n#endif /* _%s_defines_h_ */\n", S->symbol_prefix);
}
#else
#define start_defines_file(S)	/* nothing */
#define end_defines_file(S)	/* nothing */
#endif

static void
output_defines(byacc_t* S, FILE * fp)
{
    int c, i;

    if (fp == S->defines_file)
    {
	output_code_lines(S, fp, CODE_REQUIRES);
    }

    for (i = 2; i < S->ntokens; ++i)
    {
	char *s = S->symbol_name[i];

	if (is_C_identifier(s) && (!S->sflag || *s != '"'))
	{
	    fprintf(fp, "#define ");
	    c = *s;
	    if (c == '"')
	    {
		while ((c = *++s) != '"')
		{
		    putc(c, fp);
		}
	    }
	    else
	    {
		do
		{
		    putc(c, fp);
		}
		while ((c = *++s) != 0);
	    }
	    if (fp == S->code_file)
		++S->outline;
	    fprintf(fp, " %ld\n", (long)S->symbol_value[i]);
	}
    }

    if (fp == S->code_file)
	++S->outline;
    if (fp != S->defines_file || S->iflag)
	fprintf(fp, "#define YYERRCODE %ld\n", (long)S->symbol_value[1]);

    if (fp == S->defines_file)
    {
	output_code_lines(S, fp, CODE_PROVIDES);
    }

    if (S->token_table && S->rflag && fp != S->externs_file)
    {
	if (fp == S->code_file)
	    ++S->outline;
	fputs("#undef yytname\n", fp);
	if (fp == S->code_file)
	    ++S->outline;
	fputs("#define yytname yyname\n", fp);
    }

    if (fp == S->defines_file || (S->iflag && !S->dflag))
    {
	if (S->unionized)
	{
	    if (S->union_file != 0)
	    {
		rewind(S->union_file);
		while ((c = getc(S->union_file)) != EOF)
		    putc_code(S, fp, c);
	    }
	    if (!S->pure_parser)
		fprintf(fp, "extern YYSTYPE %slval;\n", S->symbol_prefix);
	}
#if defined(YYBTYACC)
	if (S->locations)
	{
	    output_ltype(S, fp);
	    fprintf(fp, "extern YYLTYPE %slloc;\n", S->symbol_prefix);
	}
#endif
    }
}

static void
output_stored_text(byacc_t* S, FILE * fp)
{
    int c;
    FILE *in;

    if (S->text_file == NULL)
	open_error(S, "text_file");
    rewind(S->text_file);
    in = S->text_file;
    if ((c = getc(in)) == EOF)
	return;
    putc_code(S, fp, c);
    while ((c = getc(in)) != EOF)
    {
	putc_code(S, fp, c);
    }
    write_code_lineno(S, fp);
}

static int
output_yydebug(byacc_t* S, FILE * fp)
{
    fprintf(fp, "#ifndef YYDEBUG\n");
    fprintf(fp, "#define YYDEBUG %d\n", S->tflag);
    fprintf(fp, "#endif\n");
    return 3;
}

static void
output_debug(byacc_t* S)
{
    int i, j, k, max, maxtok;
    const char **symnam;
    const char *s;

    ++S->outline;
    fprintf(S->code_file, "#define YYFINAL %ld\n", (long)S->final_state);

    S->outline += output_yydebug(S, S->code_file);

    if (S->rflag)
    {
	output_yydebug(S, S->output_file);
    }

    maxtok = 0;
    for (i = 0; i < S->ntokens; ++i)
	if (S->symbol_value[i] > maxtok)
	    maxtok = S->symbol_value[i];

    /* symbol_value[$accept] = -1         */
    /* symbol_value[<goal>]  = 0          */
    /* remaining non-terminals start at 1 */
    max = maxtok;
    for (i = S->ntokens; i < S->nsyms; ++i)
	if (((maxtok + 1) + (S->symbol_value[i] + 1)) > max)
	    max = (maxtok + 1) + (S->symbol_value[i] + 1);

    ++S->outline;
    fprintf(S->code_file, "#define YYMAXTOKEN %d\n", maxtok);

    ++S->outline;
    fprintf(S->code_file, "#define YYUNDFTOKEN %d\n", max + 1);

    ++S->outline;
    fprintf(S->code_file, "#define YYTRANSLATE(a) ((a) > YYMAXTOKEN ? "
	    "YYUNDFTOKEN : (a))\n");

    symnam = TCMALLOC(const char *, max + 2);
    NO_SPACE(symnam);

    /* Note that it is not necessary to initialize the element          */
    /* symnam[max].                                                     */
#if defined(YYBTYACC)
    for (i = 0; i < max; ++i)
	symnam[i] = 0;
    for (i = S->nsyms - 1; i >= 0; --i)
	symnam[S->symbol_pval[i]] = S->symbol_name[i];
    symnam[max + 1] = "illegal-symbol";
#else
    for (i = 0; i <= max; ++i)
	symnam[i] = 0;
    for (i = S->ntokens - 1; i >= 2; --i)
	symnam[S->symbol_value[i]] = S->symbol_name[i];
    symnam[0] = "end-of-file";
    symnam[max + 1] = "illegal-symbol";
#endif

    /*
     * bison's yytname[] array is roughly the same as byacc's yyname[] array.
     * The difference is that byacc does not predefine "$undefined".
     *
     * If the grammar declares "%token-table", define symbol "yytname" so
     * an application such as ntpd can build.
     */
    if (S->token_table)
    {
	if (!S->rflag)
	{
	    output_line(S, "#undef yytname");
	    output_line(S, "#define yytname yyname");
	}
    }
    else
    {
	output_line(S, "#if YYDEBUG");
    }

    start_str_table(S, "name");
    j = 80;
    for (i = 0; i <= max + 1; ++i)
    {
	if ((s = symnam[i]) != 0)
	{
	    if (s[0] == '"')
	    {
		k = 7;
		while (*++s != '"')
		{
		    ++k;
		    if (*s == '\\')
		    {
			k += 2;
			if (*++s == '\\')
			    ++k;
		    }
		}
		j += k;
		if (j > 80)
		{
		    output_newline(S);
		    j = k;
		}
		fprintf(S->output_file, "\"\\\"");
		s = symnam[i];
		while (*++s != '"')
		{
		    if (*s == '\\')
		    {
			fprintf(S->output_file, "\\\\");
			if (*++s == '\\')
			    fprintf(S->output_file, "\\\\");
			else
			    putc(*s, S->output_file);
		    }
		    else
			putc(*s, S->output_file);
		}
		fprintf(S->output_file, "\\\"\",");
	    }
	    else if (s[0] == '\'')
	    {
		if (s[1] == '"')
		{
		    j += 7;
		    if (j > 80)
		    {
			output_newline(S);
			j = 7;
		    }
		    fprintf(S->output_file, "\"'\\\"'\",");
		}
		else
		{
		    k = 5;
		    while (*++s != '\'')
		    {
			++k;
			if (*s == '\\')
			{
			    k += 2;
			    if (*++s == '\\')
				++k;
			}
		    }
		    j += k;
		    if (j > 80)
		    {
			output_newline(S);
			j = k;
		    }
		    fprintf(S->output_file, "\"'");
		    s = symnam[i];
		    while (*++s != '\'')
		    {
			if (*s == '\\')
			{
			    fprintf(S->output_file, "\\\\");
			    if (*++s == '\\')
				fprintf(S->output_file, "\\\\");
			    else
				putc(*s, S->output_file);
			}
			else
			    putc(*s, S->output_file);
		    }
		    fprintf(S->output_file, "'\",");
		}
	    }
	    else
	    {
		k = (int)strlen(s) + 3;
		j += k;
		if (j > 80)
		{
		    output_newline(S);
		    j = k;
		}
		putc('"', S->output_file);
		do
		{
		    putc(*s, S->output_file);
		}
		while (*++s);
		fprintf(S->output_file, "\",");
	    }
	}
	else
	{
	    j += 2;
	    if (j > 80)
	    {
		output_newline(S);
		j = 2;
	    }
	    fprintf(S->output_file, "0,");
	}
    }
    end_table(S);
    FREE(symnam);

    if (S->token_table)
	output_line(S, "#if YYDEBUG");
    start_str_table(S, "rule");
    for (i = 2; i < S->nrules; ++i)
    {
	fprintf(S->output_file, "\"%s :", S->symbol_name[S->rlhs[i]]);
	for (j = S->rrhs[i]; S->ritem[j] > 0; ++j)
	{
	    s = S->symbol_name[S->ritem[j]];
	    if (s[0] == '"')
	    {
		fprintf(S->output_file, " \\\"");
		while (*++s != '"')
		{
		    if (*s == '\\')
		    {
			if (s[1] == '\\')
			    fprintf(S->output_file, "\\\\\\\\");
			else
			    fprintf(S->output_file, "\\\\%c", s[1]);
			++s;
		    }
		    else
			putc(*s, S->output_file);
		}
		fprintf(S->output_file, "\\\"");
	    }
	    else if (s[0] == '\'')
	    {
		if (s[1] == '"')
		    fprintf(S->output_file, " '\\\"'");
		else if (s[1] == '\\')
		{
		    if (s[2] == '\\')
			fprintf(S->output_file, " '\\\\\\\\");
		    else
			fprintf(S->output_file, " '\\\\%c", s[2]);
		    s += 2;
		    while (*++s != '\'')
			putc(*s, S->output_file);
		    putc('\'', S->output_file);
		}
		else
		    fprintf(S->output_file, " '%c'", s[1]);
	    }
	    else
		fprintf(S->output_file, " %s", s);
	}
	fprintf(S->output_file, "\",");
	output_newline(S);
    }

    end_table(S);
    output_line(S, "#endif");
}

#if defined(YYBTYACC)
static void
output_backtracking_parser(byacc_t* S, FILE * fp)
{
    putl_code(S, fp, "#undef YYBTYACC\n");
#if defined(YYBTYACC)
    if (S->backtrack)
    {
	putl_code(S, fp, "#define YYBTYACC 1\n");
	putl_code(S, fp,
		  "#define YYDEBUGSTR (yytrial ? YYPREFIX \"debug(trial)\" : YYPREFIX \"debug\")\n");
    }
    else
#endif
    {
	putl_code(S, fp, "#define YYBTYACC 0\n");
	putl_code(S, fp, "#define YYDEBUGSTR YYPREFIX \"debug\"\n");
    }
}
#endif

static void
output_pure_parser(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');

    if (fp == S->code_file)
	++S->outline;
    fprintf(fp, "#define YYPURE %d\n", S->pure_parser);
#if defined(YY_NO_LEAKS)
    if (fp == S->code_file)
	++S->outline;
    fputs("#define YY_NO_LEAKS 1\n", fp);
#else
    putc_code(S, fp, '\n');
#endif
}

static void
output_trailing_text(byacc_t* S)
{
    int c, last;
    FILE *in;

    if (S->line == 0)
	return;

    in = S->input_file;
    c = *S->cptr;
    if (c == '\n')
    {
	++S->lineno;
	if ((c = getc(in)) == EOF)
	    return;
	write_input_lineno(S);
	putc_code(S, S->code_file, c);
	last = c;
    }
    else
    {
	write_input_lineno(S);
	do
	{
	    putc_code(S, S->code_file, c);
	}
	while ((c = *++S->cptr) != '\n');
	putc_code(S, S->code_file, c);
	last = '\n';
    }

    while ((c = getc(in)) != EOF)
    {
	putc_code(S, S->code_file, c);
	last = c;
    }

    if (last != '\n')
    {
	putc_code(S, S->code_file, '\n');
    }
    write_code_lineno(S, S->code_file);
}

static void
output_semantic_actions(byacc_t* S)
{
    int c, last;
    int state;
    char line_state[20];

    rewind(S->action_file);
    if ((c = getc(S->action_file)) == EOF)
	return;

    if (!S->lflag)
    {
	state = -1;
	sprintf(line_state, S->line_format, 1, "");
    }

    last = c;
    putc_code(S, S->code_file, c);
    while ((c = getc(S->action_file)) != EOF)
    {
	/*
	 * When writing the action file, we did not know the line-numbers in
	 * the code-file, but wrote empty #line directives.  Detect those and
	 * replace with proper #line directives.
	 */
	if (!S->lflag && (last == '\n' || state >= 0))
	{
	    if (c == line_state[state + 1])
	    {
		++state;
		if (line_state[state + 1] == '\0')
		{
		    write_code_lineno(S, S->code_file);
		    state = -1;
		}
		last = c;
		continue;
	    }
	    else
	    {
		int n;
		for (n = 0; n <= state; ++n)
		    putc_code(S, S->code_file, line_state[n]);
		state = -1;
	    }
	}
	putc_code(S, S->code_file, c);
	last = c;
    }

    if (last != '\n')
    {
	putc_code(S, S->code_file, '\n');
    }

    write_code_lineno(S, S->code_file);
}

static void
output_parse_decl(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');
    putl_code(S, fp, "/* compatibility with bison */\n");
    putl_code(S, fp, "#ifdef YYPARSE_PARAM\n");
    putl_code(S, fp, "/* compatibility with FreeBSD */\n");
    putl_code(S, fp, "# ifdef YYPARSE_PARAM_TYPE\n");
    putl_code(S, fp,
	      "#  define YYPARSE_DECL() yyparse(YYPARSE_PARAM_TYPE YYPARSE_PARAM)\n");
    putl_code(S, fp, "# else\n");
    putl_code(S, fp, "#  define YYPARSE_DECL() yyparse(void *YYPARSE_PARAM)\n");
    putl_code(S, fp, "# endif\n");
    putl_code(S, fp, "#else\n");

    puts_code(fp, "# define YYPARSE_DECL() yyparse(");
    puts_param_types(fp, S->parse_param, 0);
    putl_code(S, fp, ")\n");

    putl_code(S, fp, "#endif\n");
}

static void
output_lex_decl(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');
    putl_code(S, fp, "/* Parameters sent to lex. */\n");
    putl_code(S, fp, "#ifdef YYLEX_PARAM\n");
    if (S->pure_parser)
    {
	putl_code(S, fp, "# ifdef YYLEX_PARAM_TYPE\n");
#if defined(YYBTYACC)
	if (S->locations)
	{
	    putl_code(S, fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLTYPE *yylloc,"
		      " YYLEX_PARAM_TYPE YYLEX_PARAM)\n");
	}
	else
#endif
	{
	    putl_code(S, fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLEX_PARAM_TYPE YYLEX_PARAM)\n");
	}
	putl_code(S, fp, "# else\n");
#if defined(YYBTYACC)
	if (S->locations)
	{
	    putl_code(S, fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " YYLTYPE *yylloc,"
		      " void * YYLEX_PARAM)\n");
	}
	else
#endif
	{
	    putl_code(S, fp, "#  define YYLEX_DECL() yylex(YYSTYPE *yylval,"
		      " void * YYLEX_PARAM)\n");
	}
	putl_code(S, fp, "# endif\n");
#if defined(YYBTYACC)
	if (S->locations)
	    putl_code(S, fp,
		      "# define YYLEX yylex(&yylval, &yylloc, YYLEX_PARAM)\n");
	else
#endif
	    putl_code(S, fp, "# define YYLEX yylex(&yylval, YYLEX_PARAM)\n");
    }
    else
    {
	putl_code(S, fp, "# define YYLEX_DECL() yylex(void *YYLEX_PARAM)\n");
	putl_code(S, fp, "# define YYLEX yylex(YYLEX_PARAM)\n");
    }
    putl_code(S, fp, "#else\n");
    if (S->pure_parser && S->lex_param)
    {
#if defined(YYBTYACC)
	if (S->locations)
	    puts_code(fp,
		      "# define YYLEX_DECL() yylex(YYSTYPE *yylval, YYLTYPE *yylloc, ");
	else
#endif
	    puts_code(fp, "# define YYLEX_DECL() yylex(YYSTYPE *yylval, ");
	puts_param_types(fp, S->lex_param, 0);
	putl_code(S, fp, ")\n");

#if defined(YYBTYACC)
	if (S->locations)
	    puts_code(fp, "# define YYLEX yylex(&yylval, &yylloc, ");
	else
#endif
	    puts_code(fp, "# define YYLEX yylex(&yylval, ");
	puts_param_names(fp, S->lex_param, 0);
	putl_code(S, fp, ")\n");
    }
    else if (S->pure_parser)
    {
#if defined(YYBTYACC)
	if (S->locations)
	{
	    putl_code(S, fp,
		      "# define YYLEX_DECL() yylex(YYSTYPE *yylval, YYLTYPE *yylloc)\n");
	    putl_code(S, fp, "# define YYLEX yylex(&yylval, &yylloc)\n");
	}
	else
#endif
	{
	    putl_code(S, fp, "# define YYLEX_DECL() yylex(YYSTYPE *yylval)\n");
	    putl_code(S, fp, "# define YYLEX yylex(&yylval)\n");
	}
    }
    else if (S->lex_param)
    {
	puts_code(fp, "# define YYLEX_DECL() yylex(");
	puts_param_types(fp, S->lex_param, 0);
	putl_code(S, fp, ")\n");

	puts_code(fp, "# define YYLEX yylex(");
	puts_param_names(fp, S->lex_param, 0);
	putl_code(S, fp, ")\n");
    }
    else
    {
	putl_code(S, fp, "# define YYLEX_DECL() yylex(void)\n");
	putl_code(S, fp, "# define YYLEX yylex()\n");
    }
    putl_code(S, fp, "#endif\n");

    /*
     * Provide a prototype for yylex for the simplest case.  This is done for
     * better compatibility with older yacc's, but can be a problem if someone
     * uses "static int yylex(void);"
     */
    if (!S->pure_parser
#if defined(YYBTYACC)
	&& !S->backtrack
#endif
	&& !strcmp(S->symbol_prefix, "yy"))
    {
	putl_code(S, fp, "\n");
	putl_code(S, fp, "#if !(defined(yylex) || defined(YYSTATE))\n");
	putl_code(S, fp, "int YYLEX_DECL();\n");
	putl_code(S, fp, "#endif\n");
    }
}

static void
output_error_decl(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');
    putl_code(S, fp, "/* Parameters sent to yyerror. */\n");
    putl_code(S, fp, "#ifndef YYERROR_DECL\n");
    puts_code(fp, "#define YYERROR_DECL() yyerror(");
#if defined(YYBTYACC)
    if (S->locations)
	puts_code(fp, "YYLTYPE *loc, ");
#endif
    puts_param_types(fp, S->parse_param, 1);
    putl_code(S, fp, "const char *s)\n");
    putl_code(S, fp, "#endif\n");

    putl_code(S, fp, "#ifndef YYERROR_CALL\n");

    puts_code(fp, "#define YYERROR_CALL(msg) yyerror(");
#if defined(YYBTYACC)
    if (S->locations)
	puts_code(fp, "&yylloc, ");
#endif
    puts_param_names(fp, S->parse_param, 1);
    putl_code(S, fp, "msg)\n");

    putl_code(S, fp, "#endif\n");
}

#if defined(YYBTYACC)
static void
output_yydestruct_decl(byacc_t* S, FILE * fp)
{
    putc_code(S, fp, '\n');
    putl_code(S, fp, "#ifndef YYDESTRUCT_DECL\n");

    puts_code(fp,
	      "#define YYDESTRUCT_DECL() "
	      "yydestruct(const char *msg, int psymb, YYSTYPE *val");
#if defined(YYBTYACC)
    if (S->locations)
	puts_code(fp, ", YYLTYPE *loc");
#endif
    if (S->parse_param)
    {
	puts_code(fp, ", ");
	puts_param_types(fp, S->parse_param, 0);
    }
    putl_code(S, fp, ")\n");

    putl_code(S, fp, "#endif\n");

    putl_code(S, fp, "#ifndef YYDESTRUCT_CALL\n");

    puts_code(fp, "#define YYDESTRUCT_CALL(msg, psymb, val");
#if defined(YYBTYACC)
    if (S->locations)
	puts_code(fp, ", loc");
#endif
    puts_code(fp, ") yydestruct(msg, psymb, val");
#if defined(YYBTYACC)
    if (S->locations)
	puts_code(fp, ", loc");
#endif
    if (S->parse_param)
    {
	puts_code(fp, ", ");
	puts_param_names(fp, S->parse_param, 0);
    }
    putl_code(S, fp, ")\n");

    putl_code(S, fp, "#endif\n");
}

static void
output_initial_action(byacc_t* S)
{
    if (S->initial_action)
	fprintf(S->code_file, "%s\n", S->initial_action);
}

static void
output_yydestruct_impl(byacc_t* S)
{
    int i;
    char *s, *destructor_code;

    putc_code(S, S->code_file, '\n');
    putl_code(S, S->code_file, "/* Release memory associated with symbol. */\n");
    putl_code(S, S->code_file, "#if ! defined YYDESTRUCT_IS_DECLARED\n");
    putl_code(S, S->code_file, "static void\n");
    putl_code(S, S->code_file, "YYDESTRUCT_DECL()\n");
    putl_code(S, S->code_file, "{\n");
    putl_code(S, S->code_file, "    switch (psymb)\n");
    putl_code(S, S->code_file, "    {\n");
    for (i = 2; i < S->nsyms; ++i)
    {
	if ((destructor_code = S->symbol_destructor[i]) != NULL)
	{
	    ++S->outline;
	    fprintf(S->code_file, "\tcase %d:\n", S->symbol_pval[i]);
	    /* comprehend the number of lines in the destructor code */
	    for (s = destructor_code; (s = strchr(s, '\n')) != NULL; s++)
		++S->outline;
	    puts_code(S->code_file, destructor_code);
	    putc_code(S, S->code_file, '\n');
	    write_code_lineno(S, S->code_file);
	    putl_code(S, S->code_file, "\tbreak;\n");
	    FREE(destructor_code);
	}
    }
    putl_code(S, S->code_file, "    }\n");
    putl_code(S, S->code_file, "}\n");
    putl_code(S, S->code_file, "#define YYDESTRUCT_IS_DECLARED 1\n");
    putl_code(S, S->code_file, "#endif\n");

    DO_FREE(S->symbol_destructor);
}
#endif

static void
free_itemsets(byacc_t* S)
{
    core *cp, *next;

    FREE(S->state_table);
    for (cp = S->first_state; cp; cp = next)
    {
	next = cp->next;
	FREE(cp);
    }
}

static void
free_shifts(byacc_t* S)
{
    shifts *sp, *next;

    FREE(S->shift_table);
    for (sp = S->first_shift; sp; sp = next)
    {
	next = sp->next;
	FREE(sp);
    }
}

static void
free_reductions(byacc_t* S)
{
    reductions *rp, *next;

    FREE(S->reduction_table);
    for (rp = S->first_reduction; rp; rp = next)
    {
	next = rp->next;
	FREE(rp);
    }
}

static void
output_externs(byacc_t* S, FILE * fp, const char *const section[])
{
    int i;
    const char *s;

    for (i = 0; (s = section[i]) != 0; ++i)
    {
	/* prefix non-blank lines that don't start with
	   C pre-processor directives with 'extern ' */
	if (*s && (*s != '#'))
	    fputs("extern\t", fp);
	if (fp == S->code_file)
	    ++S->outline;
	fprintf(fp, "%s\n", s);
    }
}

void
output(byacc_t* S)
{
    FILE *fp;

    free_itemsets(S);
    free_shifts(S);
    free_reductions(S);

    output_code_lines(S, S->code_file, CODE_TOP);
#if defined(YYBTYACC)
    output_backtracking_parser(S, S->output_file);
    if (S->rflag)
	output_backtracking_parser(S, S->code_file);
#endif

    if (S->iflag)
    {
	write_code_lineno(S, S->code_file);
	++S->outline;
	fprintf(S->code_file, "#include \"%s\"\n", S->externs_file_name);
	fp = S->externs_file;
    }
    else
	fp = S->code_file;

    output_prefix(S, fp);
    output_pure_parser(S, fp);
    output_stored_text(S, fp);
    output_stype(S, fp);
#if defined(YYBTYACC)
    if (S->locations)
	output_ltype(S, fp);
#endif
    output_parse_decl(S, fp);
    output_lex_decl(S, fp);
    output_error_decl(S, fp);
#if defined(YYBTYACC)
    if (S->destructor)
	output_yydestruct_decl(S, fp);
#endif
    if (S->iflag || !S->rflag)
    {
	write_section(S, fp, ygv_xdecls);
    }

    if (S->iflag)
    {
	fprintf(S->externs_file, "\n");
	output_yydebug(S, S->externs_file);
	output_externs(S, S->externs_file, ygv_global_vars);
	if (!S->pure_parser)
	    output_externs(S, S->externs_file, ygv_impure_vars);
	if (S->dflag)
	{
	    ++S->outline;
	    fprintf(S->code_file, "#include \"%s\"\n", S->defines_file_name);
	}
	else
	    output_defines(S, S->externs_file);
    }
    else
    {
	putc_code(S, S->code_file, '\n');
	output_defines(S, S->code_file);
    }

    if (S->dflag)
    {
	start_defines_file(S);
	output_defines(S, S->defines_file);
	end_defines_file(S);
    }

    output_rule_data(S);
    output_yydefred(S);
#if defined(YYBTYACC)
    output_accessing_symbols(S);
#endif
    output_actions(S);
    free_parser(S);
    output_debug(S);

    if (S->rflag)
    {
	write_section(S, S->code_file, ygv_xdecls);
	output_YYINT_typedef(S, S->code_file);
	write_section(S, S->code_file, ygv_tables);
    }

    write_section(S, S->code_file, ygv_global_vars);
    if (!S->pure_parser)
    {
	write_section(S, S->code_file, ygv_impure_vars);
    }
    output_code_lines(S, S->code_file, CODE_REQUIRES);

    write_section(S, S->code_file, ygv_hdr_defs);
    if (!S->pure_parser)
    {
	write_section(S, S->code_file, ygv_hdr_vars);
    }

    output_code_lines(S, S->code_file, CODE_PROVIDES);
    output_code_lines(S, S->code_file, CODE_HEADER);

    output_trailing_text(S);
#if defined(YYBTYACC)
    if (S->destructor)
	output_yydestruct_impl(S);
#endif
    write_section(S, S->code_file, ygv_body_1);
    if (S->pure_parser)
    {
	write_section(S, S->code_file, ygv_body_vars);
    }
    write_section(S, S->code_file, ygv_body_2);
    if (S->pure_parser)
    {
	write_section(S, S->code_file, ygv_init_vars);
    }
#if defined(YYBTYACC)
    if (S->initial_action)
	output_initial_action(S);
#endif
    write_section(S, S->code_file, ygv_body_3);
    output_semantic_actions(S);
    write_section(S, S->code_file, ygv_trailer);
}

#ifdef NO_LEAKS
void
output_leaks(byacc_t* S)
{
    DO_FREE(S->tally);
    DO_FREE(S->width);
    DO_FREE(S->order);
}
#endif
