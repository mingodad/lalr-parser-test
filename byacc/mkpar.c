/* $Id: mkpar.c,v 1.18 2021/05/20 23:57:23 tom Exp $ */

#include "defs.h"

#define NotSuppressed(p)	((p)->suppressed == 0)

#if defined(YYBTYACC)
#define MaySuppress(p)		((S->backtrack ? ((p)->suppressed <= 1) : (p)->suppressed == 0))
    /* suppress the preferred action => enable backtracking */
#define StartBacktrack(p)	if (S->backtrack && (p) != NULL && NotSuppressed(p)) (p)->suppressed = 1
#else
#define MaySuppress(p)		((p)->suppressed == 0)
#define StartBacktrack(p)	/*nothing */
#endif

static action *add_reduce(byacc_t* S, action *actions, int ruleno, int symbol);
static action *add_reductions(byacc_t* S, int stateno, action *actions);
static action *get_shifts(byacc_t* S, int stateno);
static action *parse_actions(byacc_t* S, int stateno);
static int sole_reduction(byacc_t* S, int stateno);
static void defreds(byacc_t* S);
static void find_final_state(byacc_t* S);
static void free_action_row(action *p);
static void remove_conflicts(byacc_t* S);
static void total_conflicts(byacc_t* S);
static void unused_rules(byacc_t* S);

void
make_parser(byacc_t* S)
{
    int i;

    S->parser = NEW2(S->nstates, action *);
    for (i = 0; i < S->nstates; i++)
	S->parser[i] = parse_actions(S, i);

    find_final_state(S);
    remove_conflicts(S);
    unused_rules(S);
    if (S->SRtotal + S->RRtotal > 0)
	total_conflicts(S);
    defreds(S);
}

static action *
parse_actions(byacc_t* S, int stateno)
{
    action *actions;

    actions = get_shifts(S, stateno);
    actions = add_reductions(S, stateno, actions);
    return (actions);
}

static action *
get_shifts(byacc_t* S, int stateno)
{
    action *actions, *temp;
    shifts *sp;
    Value_t *to_state2;

    actions = 0;
    sp = S->shift_table[stateno];
    if (sp)
    {
	Value_t i;

	to_state2 = sp->shift;
	for (i = (Value_t)(sp->nshifts - 1); i >= 0; i--)
	{
	    Value_t k = to_state2[i];
	    Value_t symbol = S->accessing_symbol[k];

	    if (ISTOKEN(symbol))
	    {
		temp = NEW(action);
		temp->next = actions;
		temp->symbol = symbol;
		temp->number = k;
		temp->prec = S->symbol_prec[symbol];
		temp->action_code = SHIFT;
		temp->assoc = S->symbol_assoc[symbol];
		actions = temp;
	    }
	}
    }
    return (actions);
}

static action *
add_reductions(byacc_t* S, int stateno, action *actions)
{
    int i, j, m, n;
    int tokensetsize;

    tokensetsize = WORDSIZE(S->ntokens);
    m = S->lookaheads[stateno];
    n = S->lookaheads[stateno + 1];
    for (i = m; i < n; i++)
    {
	int ruleno = S->LAruleno[i];
	bitword_t *rowp = S->LA + i * tokensetsize;

	for (j = S->ntokens - 1; j >= 0; j--)
	{
	    if (BIT(rowp, j))
		actions = add_reduce(S, actions, ruleno, j);
	}
    }
    return (actions);
}

static action *
add_reduce(byacc_t* S, action *actions,
	   int ruleno,
	   int symbol)
{
    action *temp, *prev, *next;

    prev = 0;
    for (next = actions; next && next->symbol < symbol; next = next->next)
	prev = next;

    while (next && next->symbol == symbol && next->action_code == SHIFT)
    {
	prev = next;
	next = next->next;
    }

    while (next && next->symbol == symbol &&
	   next->action_code == REDUCE && next->number < ruleno)
    {
	prev = next;
	next = next->next;
    }

    temp = NEW(action);
    temp->next = next;
    temp->symbol = (Value_t)symbol;
    temp->number = (Value_t)ruleno;
    temp->prec = S->rprec[ruleno];
    temp->action_code = REDUCE;
    temp->assoc = S->rassoc[ruleno];

    if (prev)
	prev->next = temp;
    else
	actions = temp;

    return (actions);
}

static void
find_final_state(byacc_t* S)
{
    Value_t *to_state2;
    shifts *p;

    if ((p = S->shift_table[0]) != 0)
    {
	int i;
	int goal = S->ritem[1];

	to_state2 = p->shift;
	for (i = p->nshifts - 1; i >= 0; --i)
	{
	    S->final_state = to_state2[i];
	    if (S->accessing_symbol[S->final_state] == goal)
		break;
	}
    }
}

static void
unused_rules(byacc_t* S)
{
    int i;
    action *p;

    S->rules_used = TMALLOC(Value_t, S->nrules);
    NO_SPACE(S->rules_used);

    for (i = 0; i < S->nrules; ++i)
	S->rules_used[i] = 0;

    for (i = 0; i < S->nstates; ++i)
    {
	for (p = S->parser[i]; p; p = p->next)
	{
	    if ((p->action_code == REDUCE) && MaySuppress(p))
		S->rules_used[p->number] = 1;
	}
    }

    S->nunused = 0;
    for (i = 3; i < S->nrules; ++i)
	if (!S->rules_used[i])
	    ++S->nunused;

    if (S->nunused)
    {
	if (S->nunused == 1)
	    fprintf(stderr, "%s: 1 rule never reduced\n", S->myname);
	else
	    fprintf(stderr, "%s: %ld rules never reduced\n", S->myname, (long)S->nunused);
    }
}

static void
remove_conflicts(byacc_t* S)
{
    int i, b;
    action *p, *pref = 0;

    S->SRtotal = 0;
    S->RRtotal = 0;
    S->SRconflicts = NEW2(S->nstates, Value_t);
    S->RRconflicts = NEW2(S->nstates, Value_t);
    for (i = 0; i < S->nstates; i++)
    {
	int symbol = -1;

	S->fs8_SRcount = 0;
	S->fs5_RRcount = 0;
#if defined(YYBTYACC)
	pref = NULL;
#endif
	for (p = S->parser[i]; p; p = p->next)
	{
	    if (p->symbol != symbol)
	    {
		/* the first parse action for each symbol is the preferred action */
		pref = p;
		symbol = p->symbol;
	    }
	    /* following conditions handle multiple, i.e., conflicting, parse actions */
	    else if (i == S->final_state && symbol == 0)
	    {
		S->fs8_SRcount++;
		p->suppressed = 1;
		StartBacktrack(pref);
	    }
	    else if (pref != 0 && pref->action_code == SHIFT)
	    {
		if (pref->prec > 0 && p->prec > 0)
		{
		    if (pref->prec < p->prec)
		    {
			pref->suppressed = 2;
			pref = p;
		    }
		    else if (pref->prec > p->prec)
		    {
			p->suppressed = 2;
		    }
		    else if (pref->assoc == LEFT)
		    {
			pref->suppressed = 2;
			pref = p;
		    }
		    else if (pref->assoc == RIGHT)
		    {
			p->suppressed = 2;
		    }
		    else
		    {
			pref->suppressed = 2;
			p->suppressed = 2;
		    }
		}
		else
		{
		    S->fs8_SRcount++;
		    p->suppressed = 1;
		    StartBacktrack(pref);
		}
	    }
	    else
	    {
                b = 0;
                if(S->lemon_prec_flag) {
                    if (pref->prec > 0 && p->prec > 0)
                    {
                        if (pref->prec < p->prec)
                        {
                            pref->suppressed = 2;
                            pref = p;
                            b = 1;
                        }
                        else if (pref->prec > p->prec)
                        {
                            p->suppressed = 2;
                            b = 1;
                        }
                    }
                }
		if(b==0)
		{
		    S->fs5_RRcount++;
		    p->suppressed = 1;
		    StartBacktrack(pref);
		}
	    }
	}
	S->SRtotal += S->fs8_SRcount;
	S->RRtotal += S->fs5_RRcount;
	S->SRconflicts[i] = S->fs8_SRcount;
	S->RRconflicts[i] = S->fs5_RRcount;
    }
}

static void
total_conflicts(byacc_t* S)
{
    fprintf(stderr, "%s: ", S->myname);
    if (S->SRtotal == 1)
	fprintf(stderr, "1 shift/reduce conflict");
    else if (S->SRtotal > 1)
	fprintf(stderr, "%d shift/reduce conflicts", S->SRtotal);

    if (S->SRtotal && S->RRtotal)
	fprintf(stderr, ", ");

    if (S->RRtotal == 1)
	fprintf(stderr, "1 reduce/reduce conflict");
    else if (S->RRtotal > 1)
	fprintf(stderr, "%d reduce/reduce conflicts", S->RRtotal);

    fprintf(stderr, ".\n");

    if (S->SRexpect >= 0 && S->SRtotal != S->SRexpect)
    {
	fprintf(stderr, "%s: ", S->myname);
	fprintf(stderr, "expected %d shift/reduce conflict%s.\n",
		S->SRexpect, PLURAL(S->SRexpect));
	S->exit_code = EXIT_FAILURE;
    }
    if (S->RRexpect >= 0 && S->RRtotal != S->RRexpect)
    {
	fprintf(stderr, "%s: ", S->myname);
	fprintf(stderr, "expected %d reduce/reduce conflict%s.\n",
		S->RRexpect, PLURAL(S->RRexpect));
	S->exit_code = EXIT_FAILURE;
    }
    fprintf(stderr, "%d conflicts\n", S->SRtotal+S->RRtotal);
    fprintf(stderr, "%d terminal symbols\n", S->ntokens);
    fprintf(stderr, "%d non-terminal symbols\n", S->nvars);
    fprintf(stderr, "%d total symbols\n", S->nsyms);
    fprintf(stderr, "%d rules\n", S->nrules);
    fprintf(stderr, "%d states\n", S->nstates);
}

static int
sole_reduction(byacc_t* S, int stateno)
{
    int count, ruleno;
    action *p;

    count = 0;
    ruleno = 0;
    for (p = S->parser[stateno]; p; p = p->next)
    {
	if (p->action_code == SHIFT && MaySuppress(p))
	    return (0);
	else if ((p->action_code == REDUCE) && MaySuppress(p))
	{
	    if (ruleno > 0 && p->number != ruleno)
		return (0);
	    if (p->symbol != 1)
		++count;
	    ruleno = p->number;
	}
    }

    if (count == 0)
	return (0);
    return (ruleno);
}

static void
defreds(byacc_t* S)
{
    int i;

    S->defred = NEW2(S->nstates, Value_t);
    for (i = 0; i < S->nstates; i++)
	S->defred[i] = (Value_t)sole_reduction(S, i);
}

static void
free_action_row(action *p)
{
    action *q;

    while (p)
    {
	q = p->next;
	FREE(p);
	p = q;
    }
}

void
free_parser(byacc_t* S)
{
    int i;

    for (i = 0; i < S->nstates; i++)
	free_action_row(S->parser[i]);

    FREE(S->parser);
}

#ifdef NO_LEAKS
void
mkpar_leaks(byacc_t* S)
{
    DO_FREE(S->defred);
    DO_FREE(S->rules_used);
    DO_FREE(S->SRconflicts);
    DO_FREE(S->RRconflicts);
}
#endif
