/* $Id: lalr.c,v 1.14 2021/05/20 23:57:23 tom Exp $ */

#include "defs.h"

//typedef struct shorts
//{
//    struct shorts *next;
//    Value_t value;
//}
//shorts;

static Value_t map_goto(byacc_t* S, int state, int symbol);
static Value_t **transpose(byacc_t* S, Value_t **R, int n);
static void add_lookback_edge(byacc_t* S, int stateno, int ruleno, int gotono);
static void build_relations(byacc_t* S);
static void compute_FOLLOWS(byacc_t* S);
static void compute_lookaheads(byacc_t* S);
static void digraph(byacc_t* S, Value_t **relation);
static void initialize_F(byacc_t* S);
static void initialize_LA(byacc_t* S);
static void set_accessing_symbol(byacc_t* S);
static void set_goto_map(byacc_t* S);
static void set_maxrhs(byacc_t* S);
static void set_reduction_table(byacc_t* S);
static void set_shift_table(byacc_t* S);
static void set_state_table(byacc_t* S);
static void traverse(byacc_t* S, int i);

void
lalr(byacc_t* S)
{
    S->fs3_tokensetsize = WORDSIZE(S->ntokens);

    set_state_table(S);
    set_accessing_symbol(S);
    set_shift_table(S);
    set_reduction_table(S);
    set_maxrhs(S);
    initialize_LA(S);
    set_goto_map(S);
    initialize_F(S);
    build_relations(S);
    compute_FOLLOWS(S);
    compute_lookaheads(S);
}

static void
set_state_table(byacc_t* S)
{
    core *sp;

    S->state_table = NEW2(S->nstates, core *);
    for (sp = S->first_state; sp; sp = sp->next)
	S->state_table[sp->number] = sp;
}

static void
set_accessing_symbol(byacc_t* S)
{
    core *sp;

    S->accessing_symbol = NEW2(S->nstates, Value_t);
    for (sp = S->first_state; sp; sp = sp->next)
	S->accessing_symbol[sp->number] = sp->accessing_symbol;
}

static void
set_shift_table(byacc_t* S)
{
    shifts *sp;

    S->shift_table = NEW2(S->nstates, shifts *);
    for (sp = S->first_shift; sp; sp = sp->next)
	S->shift_table[sp->number] = sp;
}

static void
set_reduction_table(byacc_t* S)
{
    reductions *rp;

    S->reduction_table = NEW2(S->nstates, reductions *);
    for (rp = S->first_reduction; rp; rp = rp->next)
	S->reduction_table[rp->number] = rp;
}

static void
set_maxrhs(byacc_t* S)
{
    Value_t *itemp;
    Value_t *item_end;
    int length;
    int max;

    length = 0;
    max = 0;
    item_end = S->ritem + S->nitems;
    for (itemp = S->ritem; itemp < item_end; itemp++)
    {
	if (*itemp >= 0)
	{
	    length++;
	}
	else
	{
	    if (length > max)
		max = length;
	    length = 0;
	}
    }

    S->fs3_maxrhs = max;
}

static void
initialize_LA(byacc_t* S)
{
    int i, j, k;
    reductions *rp;

    S->lookaheads = NEW2(S->nstates + 1, Value_t);

    k = 0;
    for (i = 0; i < S->nstates; i++)
    {
	S->lookaheads[i] = (Value_t)k;
	rp = S->reduction_table[i];
	if (rp)
	    k += rp->nreds;
    }
    S->lookaheads[S->nstates] = (Value_t)k;

    S->LA = NEW2(k * S->fs3_tokensetsize, bitword_t);
    S->LAruleno = NEW2(k, Value_t);
    S->fs3_lookback = NEW2(k, shorts *);

    k = 0;
    for (i = 0; i < S->nstates; i++)
    {
	rp = S->reduction_table[i];
	if (rp)
	{
	    for (j = 0; j < rp->nreds; j++)
	    {
		S->LAruleno[k] = rp->rules[j];
		k++;
	    }
	}
    }
}

static void
set_goto_map(byacc_t* S)
{
    shifts *sp;
    int i;
    int symbol;
    int k;
    Value_t *temp_base;
    Value_t *temp_map;
    Value_t state2;

    S->goto_base = NEW2(S->nvars + 1, Value_t);
    temp_base = NEW2(S->nvars + 1, Value_t);

    S->goto_map = S->goto_base - S->ntokens;
    temp_map = temp_base - S->ntokens;

    S->fs3_ngotos = 0;
    for (sp = S->first_shift; sp; sp = sp->next)
    {
	for (i = sp->nshifts - 1; i >= 0; i--)
	{
	    symbol = S->accessing_symbol[sp->shift[i]];

	    if (ISTOKEN(symbol))
		break;

	    if (S->fs3_ngotos == MAXYYINT)
		fatal(S, "too many gotos");

	    S->fs3_ngotos++;
	    S->goto_map[symbol]++;
	}
    }

    k = 0;
    for (i = S->ntokens; i < S->nsyms; i++)
    {
	temp_map[i] = (Value_t)k;
	k += S->goto_map[i];
    }

    for (i = S->ntokens; i < S->nsyms; i++)
	S->goto_map[i] = temp_map[i];

    S->goto_map[S->nsyms] = (Value_t)S->fs3_ngotos;
    temp_map[S->nsyms] = (Value_t)S->fs3_ngotos;

    S->from_state = NEW2(S->fs3_ngotos, Value_t);
    S->to_state = NEW2(S->fs3_ngotos, Value_t);

    for (sp = S->first_shift; sp; sp = sp->next)
    {
	Value_t state1 = sp->number;

	for (i = sp->nshifts - 1; i >= 0; i--)
	{
	    state2 = sp->shift[i];
	    symbol = S->accessing_symbol[state2];

	    if (ISTOKEN(symbol))
		break;

	    k = temp_map[symbol]++;
	    S->from_state[k] = state1;
	    S->to_state[k] = state2;
	}
    }

    FREE(temp_base);
}

/*  Map_goto maps a state/symbol pair into its numeric representation.	*/

static Value_t
map_goto(byacc_t* S, int state, int symbol)
{
    int low = S->goto_map[symbol];
    int high = S->goto_map[symbol + 1];

    for (;;)
    {
	int middle;
	int s;

	assert(low <= high);
	middle = (low + high) >> 1;
	s = S->from_state[middle];
	if (s == state)
	    return (Value_t)(middle);
	else if (s < state)
	    low = middle + 1;
	else
	    high = middle - 1;
    }
}

static void
initialize_F(byacc_t* S)
{
    int i;
    int j;
    int k;
    shifts *sp;
    Value_t *edge;
    bitword_t *rowp;
    Value_t *rp;
    Value_t **reads;
    int nedges;
    int symbol;
    int nwords;

    nwords = S->fs3_ngotos * S->fs3_tokensetsize;
    S->fs3_F = NEW2(nwords, bitword_t);

    reads = NEW2(S->fs3_ngotos, Value_t *);
    edge = NEW2(S->fs3_ngotos + 1, Value_t);
    nedges = 0;

    rowp = S->fs3_F;
    for (i = 0; i < S->fs3_ngotos; i++)
    {
	int stateno = S->to_state[i];

	sp = S->shift_table[stateno];

	if (sp)
	{
	    k = sp->nshifts;

	    for (j = 0; j < k; j++)
	    {
		symbol = S->accessing_symbol[sp->shift[j]];
		if (ISVAR(symbol))
		    break;
		SETBIT(rowp, symbol);
	    }

	    for (; j < k; j++)
	    {
		symbol = S->accessing_symbol[sp->shift[j]];
		if (S->nullable[symbol])
		    edge[nedges++] = map_goto(S, stateno, symbol);
	    }

	    if (nedges)
	    {
		reads[i] = rp = NEW2(nedges + 1, Value_t);

		for (j = 0; j < nedges; j++)
		    rp[j] = edge[j];

		rp[nedges] = -1;
		nedges = 0;
	    }
	}

	rowp += S->fs3_tokensetsize;
    }

    SETBIT(S->fs3_F, 0);
    digraph(S, reads);

    for (i = 0; i < S->fs3_ngotos; i++)
    {
	if (reads[i])
	    FREE(reads[i]);
    }

    FREE(reads);
    FREE(edge);
}

static void
build_relations(byacc_t* S)
{
    int i;
    int j;
    int k;
    Value_t *rulep;
    Value_t *rp;
    shifts *sp;
    int length;
    int done_flag;
    Value_t stateno;
    int symbol2;
    Value_t *shortp;
    Value_t *edge;
    Value_t *states;
    Value_t **new_includes;

    S->fs3_includes = NEW2(S->fs3_ngotos, Value_t *);
    edge = NEW2(S->fs3_ngotos + 1, Value_t);
    states = NEW2(S->fs3_maxrhs + 1, Value_t);

    for (i = 0; i < S->fs3_ngotos; i++)
    {
	int nedges = 0;
	int symbol1 = S->accessing_symbol[S->to_state[i]];
	Value_t state1 = S->from_state[i];

	for (rulep = S->derives[symbol1]; *rulep >= 0; rulep++)
	{
	    length = 1;
	    states[0] = state1;
	    stateno = state1;

	    for (rp = S->ritem + S->rrhs[*rulep]; *rp >= 0; rp++)
	    {
		symbol2 = *rp;
		sp = S->shift_table[stateno];
		k = sp->nshifts;

		for (j = 0; j < k; j++)
		{
		    stateno = sp->shift[j];
		    if (S->accessing_symbol[stateno] == symbol2)
			break;
		}

		states[length++] = stateno;
	    }

	    add_lookback_edge(S, stateno, *rulep, i);

	    length--;
	    done_flag = 0;
	    while (!done_flag)
	    {
		done_flag = 1;
		rp--;
		if (ISVAR(*rp))
		{
		    stateno = states[--length];
		    edge[nedges++] = map_goto(S, stateno, *rp);
		    if (S->nullable[*rp] && length > 0)
			done_flag = 0;
		}
	    }
	}

	if (nedges)
	{
	    S->fs3_includes[i] = shortp = NEW2(nedges + 1, Value_t);
	    for (j = 0; j < nedges; j++)
		shortp[j] = edge[j];
	    shortp[nedges] = -1;
	}
    }

    new_includes = transpose(S, S->fs3_includes, S->fs3_ngotos);

    for (i = 0; i < S->fs3_ngotos; i++)
	if (S->fs3_includes[i])
	    FREE(S->fs3_includes[i]);

    FREE(S->fs3_includes);

    S->fs3_includes = new_includes;

    FREE(edge);
    FREE(states);
}

static void
add_lookback_edge(byacc_t* S, int stateno, int ruleno, int gotono)
{
    int i, k;
    int found;
    shorts *sp;

    i = S->lookaheads[stateno];
    k = S->lookaheads[stateno + 1];
    found = 0;
    while (!found && i < k)
    {
	if (S->LAruleno[i] == ruleno)
	    found = 1;
	else
	    ++i;
    }
    assert(found);

    sp = NEW(shorts);
    sp->next = S->fs3_lookback[i];
    sp->value = (Value_t)gotono;
    S->fs3_lookback[i] = sp;
}

static Value_t **
transpose(byacc_t* S, Value_t **R2, int n)
{
    Value_t **new_R;
    Value_t **temp_R;
    Value_t *nedges;
    Value_t *sp;
    int i;

    nedges = NEW2(n, Value_t);

    for (i = 0; i < n; i++)
    {
	sp = R2[i];
	if (sp)
	{
	    while (*sp >= 0)
		nedges[*sp++]++;
	}
    }

    new_R = NEW2(n, Value_t *);
    temp_R = NEW2(n, Value_t *);

    for (i = 0; i < n; i++)
    {
	int k = nedges[i];

	if (k > 0)
	{
	    sp = NEW2(k + 1, Value_t);
	    new_R[i] = sp;
	    temp_R[i] = sp;
	    sp[k] = -1;
	}
    }

    FREE(nedges);

    for (i = 0; i < n; i++)
    {
	sp = R2[i];
	if (sp)
	{
	    while (*sp >= 0)
		*temp_R[*sp++]++ = (Value_t)i;
	}
    }

    FREE(temp_R);

    return (new_R);
}

static void
compute_FOLLOWS(byacc_t* S)
{
    digraph(S, S->fs3_includes);
}

static void
compute_lookaheads(byacc_t* S)
{
    int i, n;
    bitword_t *fp1, *fp2, *fp3;
    shorts *sp, *next;
    bitword_t *rowp;

    rowp = S->LA;
    n = S->lookaheads[S->nstates];
    for (i = 0; i < n; i++)
    {
	fp3 = rowp + S->fs3_tokensetsize;
	for (sp = S->fs3_lookback[i]; sp; sp = sp->next)
	{
	    fp1 = rowp;
	    fp2 = S->fs3_F + S->fs3_tokensetsize * sp->value;
	    while (fp1 < fp3)
		*fp1++ |= *fp2++;
	}
	rowp = fp3;
    }

    for (i = 0; i < n; i++)
	for (sp = S->fs3_lookback[i]; sp; sp = next)
	{
	    next = sp->next;
	    FREE(sp);
	}

    FREE(S->fs3_lookback);
    FREE(S->fs3_F);
}

static void
digraph(byacc_t* S, Value_t **relation)
{
    int i;

    S->fs3_infinity = (Value_t)(S->fs3_ngotos + 2);
    S->fs3_INDEX = NEW2(S->fs3_ngotos + 1, Value_t);
    S->fs3_VERTICES = NEW2(S->fs3_ngotos + 1, Value_t);
    S->fs3_top = 0;

    S->fs3_R = relation;

    for (i = 0; i < S->fs3_ngotos; i++)
	S->fs3_INDEX[i] = 0;

    for (i = 0; i < S->fs3_ngotos; i++)
    {
	if (S->fs3_INDEX[i] == 0 && S->fs3_R[i])
	    traverse(S, i);
    }

    FREE(S->fs3_INDEX);
    FREE(S->fs3_VERTICES);
}

static void
traverse(byacc_t* S, int i)
{
    bitword_t *fp1;
    bitword_t *fp2;
    bitword_t *fp3;
    int j;
    Value_t *rp;

    Value_t height;
    bitword_t *base;

    S->fs3_VERTICES[++S->fs3_top] = (Value_t)i;
    S->fs3_INDEX[i] = height = S->fs3_top;

    base = S->fs3_F + i * S->fs3_tokensetsize;
    fp3 = base + S->fs3_tokensetsize;

    rp = S->fs3_R[i];
    if (rp)
    {
	while ((j = *rp++) >= 0)
	{
	    if (S->fs3_INDEX[j] == 0)
		traverse(S, j);

	    if (S->fs3_INDEX[i] > S->fs3_INDEX[j])
		S->fs3_INDEX[i] = S->fs3_INDEX[j];

	    fp1 = base;
	    fp2 = S->fs3_F + j * S->fs3_tokensetsize;

	    while (fp1 < fp3)
		*fp1++ |= *fp2++;
	}
    }

    if (S->fs3_INDEX[i] == height)
    {
	for (;;)
	{
	    j = S->fs3_VERTICES[S->fs3_top--];
	    S->fs3_INDEX[j] = S->fs3_infinity;

	    if (i == j)
		break;

	    fp1 = base;
	    fp2 = S->fs3_F + j * S->fs3_tokensetsize;

	    while (fp1 < fp3)
		*fp2++ = *fp1++;
	}
    }
}

#ifdef NO_LEAKS
void
lalr_leaks(byacc_t* S)
{
    if (S->fs3_includes != 0)
    {
	int i;

	for (i = 0; i < S->fs3_ngotos; i++)
	{
	    free(S->fs3_includes[i]);
	}
	DO_FREE(S->fs3_includes);
    }
}
#endif
