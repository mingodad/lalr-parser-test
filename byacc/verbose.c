/* $Id: verbose.c,v 1.14 2021/05/20 23:57:23 tom Exp $ */

#include "defs.h"

static void log_conflicts(byacc_t* S);
static void log_unused(byacc_t* S);
static void print_actions(byacc_t* S, int stateno);
static void print_conflicts(byacc_t* S, int state);
static void print_core(byacc_t* S, int state);
static void print_gotos(byacc_t* S, int stateno);
static void print_nulls(byacc_t* S, int state);
static void print_shifts(byacc_t* S, action *p);
static void print_state(byacc_t* S, int state);
static void print_reductions(byacc_t* S, action *p, int defred2);

void
verbose(byacc_t* S)
{
    int i;

    if (!S->vflag)
	return;

    S->fs6_null_rules = TMALLOC(Value_t, S->nrules);
    NO_SPACE(S->fs6_null_rules);

    fprintf(S->verbose_file, "\f\n");
    for (i = 0; i < S->nstates; i++)
	print_state(S, i);
    FREE(S->fs6_null_rules);

    if (S->nunused)
	log_unused(S);
    if (S->SRtotal || S->RRtotal)
	log_conflicts(S);

    fprintf(S->verbose_file, "\n\n%ld terminals, %ld nonterminals\n",
	    (long)S->ntokens, (long)S->nvars);
    fprintf(S->verbose_file, "%ld grammar rules, %ld states\n",
	    (long)(S->nrules - RULE_NUM_OFFSET), (long)S->nstates);
#if defined(YYBTYACC)
    {				/* print out the grammar symbol # and parser internal symbol # for each
				   symbol as an aide to writing the implementation for YYDESTRUCT_CALL()
				   and YYSTYPE_TOSTRING() */
	int maxtok = 0;

	fputs("\ngrammar parser grammar\n", S->verbose_file);
	fputs("symbol# value# symbol\n", S->verbose_file);
	for (i = 0; i < S->ntokens; ++i)
	{
	    fprintf(S->verbose_file, " %5d  %5d  %s\n",
		    i, S->symbol_value[i], S->symbol_name[i]);
	    if (S->symbol_value[i] > maxtok)
		maxtok = S->symbol_value[i];
	}
	for (i = S->ntokens; i < S->nsyms; ++i)
	    fprintf(S->verbose_file, " %5d  %5d  %s\n",
		    i, (maxtok + 1) + S->symbol_value[i] + 1, S->symbol_name[i]);
    }
#endif
}

static void
log_unused(byacc_t* S)
{
    int i;
    Value_t *p;

    fprintf(S->verbose_file, "\n\nRules never reduced:\n");
    for (i = 3; i < S->nrules; ++i)
    {
	if (!S->rules_used[i])
	{
	    fprintf(S->verbose_file, "\t%s :", S->symbol_name[S->rlhs[i]]);
	    for (p = S->ritem + S->rrhs[i]; *p >= 0; ++p)
		fprintf(S->verbose_file, " %s", S->symbol_name[*p]);
	    fprintf(S->verbose_file, "  (%d)\n", i - RULE_NUM_OFFSET);
	}
    }
}

static void
log_conflicts(byacc_t* S)
{
    int i;

    fprintf(S->verbose_file, "\n\n");
    for (i = 0; i < S->nstates; i++)
    {
	if (S->SRconflicts[i] || S->RRconflicts[i])
	{
	    fprintf(S->verbose_file, "State %d contains ", i);
	    if (S->SRconflicts[i] > 0)
		fprintf(S->verbose_file, "%ld shift/reduce conflict%s",
			(long)S->SRconflicts[i],
			PLURAL(S->SRconflicts[i]));
	    if (S->SRconflicts[i] && S->RRconflicts[i])
		fprintf(S->verbose_file, ", ");
	    if (S->RRconflicts[i] > 0)
		fprintf(S->verbose_file, "%ld reduce/reduce conflict%s",
			(long)S->RRconflicts[i],
			PLURAL(S->RRconflicts[i]));
	    fprintf(S->verbose_file, ".\n");
	}
    }
}

static void
print_state(byacc_t* S, int state)
{
    if (state)
	fprintf(S->verbose_file, "\n\n");
    if (S->SRconflicts[state] || S->RRconflicts[state])
	print_conflicts(S, state);
    fprintf(S->verbose_file, "state %d\n", state);
    print_core(S, state);
    print_nulls(S, state);
    print_actions(S, state);
}

static void
print_conflicts(byacc_t* S, int state)
{
    int symbol, act, number;
    Value_t r1, r2;
    action *p;

    act = 0;			/* not shift/reduce... */
    number = -1;
    symbol = -1;
    for (p = S->parser[state]; p; p = p->next)
    {
	if (p->suppressed == 2)
	    continue;

	if (p->symbol != symbol)
	{
	    symbol = p->symbol;
	    number = p->number;
	    if (p->action_code == SHIFT)
		act = SHIFT;
	    else
		act = REDUCE;
	}
	else if (p->suppressed == 1)
	{
            r2 = p->number - RULE_NUM_OFFSET;
	    if (state == S->final_state && symbol == 0)
	    {
		fprintf(S->verbose_file, "%d: shift/reduce conflict \
(accept, reduce %ld [%s]) on $end\n", state, (long)(r2), S->symbol_name[S->rlhs[p->number]]);
	    }
	    else
	    {
		if (act == SHIFT)
		{
		    fprintf(S->verbose_file, "%d: shift/reduce conflict \
(shift %ld, reduce %ld [%s]) on %s\n", state, (long)number, (long)(r2),
			    S->symbol_name[S->rlhs[p->number]], S->symbol_name[symbol]);
		}
		else
		{
                    r1 = number - RULE_NUM_OFFSET;
		    fprintf(S->verbose_file, "%d: reduce/reduce conflict \
(reduce %ld [%s], reduce %ld [%s]) on %s\n", state,
                            (long)(r1), S->symbol_name[S->rlhs[r1]],
                            (long)(r2), S->symbol_name[S->rlhs[p->number]],
			    S->symbol_name[symbol]);
		}
	    }
	}
    }
}

static void
print_core(byacc_t* S, int state)
{
    int i;
    core *statep = S->state_table[state];
    int k = statep->nitems;

    for (i = 0; i < k; i++)
    {
	int rule;
	Value_t *sp = S->ritem + statep->items[i];
	Value_t *sp1 = sp;

	while (*sp >= 0)
	    ++sp;
	rule = -(*sp);
	fprintf(S->verbose_file, "\t%s : ", S->symbol_name[S->rlhs[rule]]);

	for (sp = S->ritem + S->rrhs[rule]; sp < sp1; sp++)
	    fprintf(S->verbose_file, "%s ", S->symbol_name[*sp]);

	putc(VERBOSE_RULE_POINT_CHAR, S->verbose_file);

	while (*sp >= 0)
	{
	    fprintf(S->verbose_file, " %s", S->symbol_name[*sp]);
	    sp++;
	}
	fprintf(S->verbose_file, "  (%ld)\n", (long)(-2 - *sp));
    }
}

static void
print_nulls(byacc_t* S, int state)
{
    action *p;
    Value_t i, j, k, nnulls;

    nnulls = 0;
    for (p = S->parser[state]; p; p = p->next)
    {
	if (p->action_code == REDUCE &&
	    (p->suppressed == 0 || p->suppressed == 1))
	{
	    i = p->number;
	    if (S->rrhs[i] + 1 == S->rrhs[i + 1])
	    {
		for (j = 0; j < nnulls && i > S->fs6_null_rules[j]; ++j)
		    continue;

		if (j == nnulls)
		{
		    ++nnulls;
		    S->fs6_null_rules[j] = i;
		}
		else if (i != S->fs6_null_rules[j])
		{
		    ++nnulls;
		    for (k = (Value_t)(nnulls - 1); k > j; --k)
			S->fs6_null_rules[k] = S->fs6_null_rules[k - 1];
		    S->fs6_null_rules[j] = i;
		}
	    }
	}
    }

    for (i = 0; i < nnulls; ++i)
    {
	j = S->fs6_null_rules[i];
	fprintf(S->verbose_file, "\t%s : .  (%ld)\n", S->symbol_name[S->rlhs[j]],
		(long)(j - RULE_NUM_OFFSET));
    }
    fprintf(S->verbose_file, "\n");
}

static void
print_actions(byacc_t* S, int stateno)
{
    action *p;
    shifts *sp;

    if (stateno == S->final_state)
	fprintf(S->verbose_file, "\t$end  accept\n");

    p = S->parser[stateno];
    if (p)
    {
	print_shifts(S, p);
	print_reductions(S, p, S->defred[stateno]);
    }

    sp = S->shift_table[stateno];
    if (sp && sp->nshifts > 0)
    {
	int as = S->accessing_symbol[sp->shift[sp->nshifts - 1]];

	if (ISVAR(as))
	    print_gotos(S, stateno);
    }
}

static void
print_shifts(byacc_t* S, action *p)
{
    int count;
    action *q;

    count = 0;
    for (q = p; q; q = q->next)
    {
	if (q->suppressed < 2 && q->action_code == SHIFT)
	    ++count;
    }

    if (count > 0)
    {
	for (; p; p = p->next)
	{
	    if (p->action_code == SHIFT && p->suppressed == 0)
		fprintf(S->verbose_file, "\t%s  shift %ld\n",
			S->symbol_name[p->symbol], (long)p->number);
#if defined(YYBTYACC)
	    if (S->backtrack && p->action_code == SHIFT && p->suppressed == 1)
		fprintf(S->verbose_file, "\t%s  [trial] shift %d\n",
			S->symbol_name[p->symbol], p->number);
#endif
	}
    }
}

static void
print_reductions(byacc_t* S, action *p, int defred2)
{
    int anyreds;
    action *q;

    anyreds = 0;
    for (q = p; q; q = q->next)
    {
	if (q->action_code == REDUCE && q->suppressed < 2)
	{
	    anyreds = 1;
	    break;
	}
    }

    if (anyreds == 0)
	fprintf(S->verbose_file, "\t.  error\n");
    else
    {
	for (; p; p = p->next)
	{
	    if (p->action_code == REDUCE && p->number != defred2)
	    {
		int k = p->number - RULE_NUM_OFFSET;

		if (p->suppressed == 0)
		    fprintf(S->verbose_file, "\t%s  reduce %d [%s]\n",
			    S->symbol_name[p->symbol], k, S->symbol_name[S->rlhs[p->number]]);
#if defined(YYBTYACC)
		if (S->backtrack && p->suppressed == 1)
		    fprintf(S->verbose_file, "\t%s  [trial] reduce %d\n",
			    S->symbol_name[p->symbol], k);
#endif
	    }
	}

	if (defred2 > 0)
	    fprintf(S->verbose_file, "\t.  reduce %d\n", defred2 - RULE_NUM_OFFSET);
    }
}

static void
print_gotos(byacc_t* S, int stateno)
{
    int i;
    Value_t *to_state2;
    shifts *sp;

    putc('\n', S->verbose_file);
    sp = S->shift_table[stateno];
    to_state2 = sp->shift;
    for (i = 0; i < sp->nshifts; ++i)
    {
	int k = to_state2[i];
	int as = S->accessing_symbol[k];

	if (ISVAR(as))
	    fprintf(S->verbose_file, "\t%s  goto %d\n", S->symbol_name[as], k);
    }
}
