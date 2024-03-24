/* $Id: lr0.c,v 1.21 2021/05/20 23:57:23 tom Exp $ */

#include "defs.h"

static core *new_state(byacc_t* S, int symbol);
static Value_t get_state(byacc_t* S, int symbol);
static void allocate_itemsets(byacc_t* S);
static void allocate_storage(byacc_t* S);
static void append_states(byacc_t* S);
static void free_storage(byacc_t* S);
static void generate_states(byacc_t* S);
static void initialize_states(byacc_t* S);
static void new_itemsets(byacc_t* S);
static void save_reductions(byacc_t* S);
static void save_shifts(byacc_t* S);
static void set_derives(byacc_t* S);
static void set_nullable(byacc_t* S);

static void
allocate_itemsets(byacc_t* S)
{
    Value_t *itemp;
    Value_t *item_end;
    int i;
    int count;
    int max;
    Value_t *symbol_count;

    count = 0;
    symbol_count = NEW2(S->nsyms, Value_t);

    item_end = S->ritem + S->nitems;
    for (itemp = S->ritem; itemp < item_end; itemp++)
    {
	int symbol = *itemp;

	if (symbol >= 0)
	{
	    count++;
	    symbol_count[symbol]++;
	}
    }

    S->fs4_kernel_base = NEW2(S->nsyms, Value_t *);
    S->fs4_kernel_items = NEW2(count, Value_t);

    count = 0;
    max = 0;
    for (i = 0; i < S->nsyms; i++)
    {
	S->fs4_kernel_base[i] = S->fs4_kernel_items + count;
	count += symbol_count[i];
	if (max < symbol_count[i])
	    max = symbol_count[i];
    }

    S->fs4_shift_symbol = symbol_count;
    S->fs4_kernel_end = NEW2(S->nsyms, Value_t *);
}

static void
allocate_storage(byacc_t* S)
{
    allocate_itemsets(S);
    S->fs4_shiftset = NEW2(S->nsyms, Value_t);
    S->fs4_redset = NEW2(S->nrules + 1, Value_t);
    S->fs4_state_set = NEW2(S->nitems, core *);
}

static void
append_states(byacc_t* S)
{
    int i;
    Value_t symbol;

#ifdef	TRACE
    fprintf(stderr, "Entering append_states()\n");
#endif
    for (i = 1; i < S->fs4_nshifts; i++)
    {
	int j = i;

	symbol = S->fs4_shift_symbol[i];
	while (j > 0 && S->fs4_shift_symbol[j - 1] > symbol)
	{
	    S->fs4_shift_symbol[j] = S->fs4_shift_symbol[j - 1];
	    j--;
	}
	S->fs4_shift_symbol[j] = symbol;
    }

    for (i = 0; i < S->fs4_nshifts; i++)
    {
	symbol = S->fs4_shift_symbol[i];
	S->fs4_shiftset[i] = get_state(S, symbol);
    }
}

static void
free_storage(byacc_t* S)
{
    FREE(S->fs4_shift_symbol);
    FREE(S->fs4_redset);
    FREE(S->fs4_shiftset);
    FREE(S->fs4_kernel_base);
    FREE(S->fs4_kernel_end);
    FREE(S->fs4_kernel_items);
    FREE(S->fs4_state_set);
}

static void
generate_states(byacc_t* S)
{
    allocate_storage(S);
    S->itemset = NEW2(S->nitems, Value_t);
    S->ruleset = NEW2(WORDSIZE(S->nrules), bitword_t);
    set_first_derives(S);
    initialize_states(S);

    while (S->fs4_this_state)
    {
	closure(S, S->fs4_this_state->items, S->fs4_this_state->nitems);
	save_reductions(S);
	new_itemsets(S);
	append_states(S);

	if (S->fs4_nshifts > 0)
	    save_shifts(S);

	S->fs4_this_state = S->fs4_this_state->next;
    }

    free_storage(S);
}

static Value_t
get_state(byacc_t* S, int symbol)
{
    int key;
    Value_t *isp1;
    Value_t *iend;
    core *sp;
    int n;

#ifdef	TRACE
    fprintf(stderr, "Entering get_state(%d)\n", symbol);
#endif

    isp1 = S->fs4_kernel_base[symbol];
    iend = S->fs4_kernel_end[symbol];
    n = (int)(iend - isp1);

    key = *isp1;
    assert(0 <= key && key < S->nitems);
    sp = S->fs4_state_set[key];
    if (sp)
    {
	int found = 0;

	while (!found)
	{
	    if (sp->nitems == n)
	    {
		Value_t *isp2;

		found = 1;
		isp1 = S->fs4_kernel_base[symbol];
		isp2 = sp->items;

		while (found && isp1 < iend)
		{
		    if (*isp1++ != *isp2++)
			found = 0;
		}
	    }

	    if (!found)
	    {
		if (sp->link)
		{
		    sp = sp->link;
		}
		else
		{
		    sp = sp->link = new_state(S, symbol);
		    found = 1;
		}
	    }
	}
    }
    else
    {
	S->fs4_state_set[key] = sp = new_state(S, symbol);
    }

    return (sp->number);
}

static void
initialize_states(byacc_t* S)
{
    unsigned i;
    Value_t *start_derives;
    core *p;

    start_derives = S->derives[S->start_symbol];
    for (i = 0; start_derives[i] >= 0; ++i)
	continue;

    p = (core *)MALLOC(sizeof(core) + i * sizeof(Value_t));
    NO_SPACE(p);

    p->next = 0;
    p->link = 0;
    p->number = 0;
    p->accessing_symbol = 0;
    p->nitems = (Value_t)i;

    for (i = 0; start_derives[i] >= 0; ++i)
	p->items[i] = S->rrhs[start_derives[i]];

    S->first_state = S->fs4_last_state = S->fs4_this_state = p;
    S->nstates = 1;
}

static void
new_itemsets(byacc_t* S)
{
    Value_t i;
    int shiftcount;
    Value_t *isp;
    Value_t *ksp;

    for (i = 0; i < S->nsyms; i++)
	S->fs4_kernel_end[i] = 0;

    shiftcount = 0;
    isp = S->itemset;
    while (isp < S->itemsetend)
    {
	int j = *isp++;
	Value_t symbol = S->ritem[j];

	if (symbol > 0)
	{
	    ksp = S->fs4_kernel_end[symbol];
	    if (!ksp)
	    {
		S->fs4_shift_symbol[shiftcount++] = symbol;
		ksp = S->fs4_kernel_base[symbol];
	    }

	    *ksp++ = (Value_t)(j + 1);
	    S->fs4_kernel_end[symbol] = ksp;
	}
    }

    S->fs4_nshifts = shiftcount;
}

static core *
new_state(byacc_t* S, int symbol)
{
    unsigned n;
    core *p;
    Value_t *isp1;
    Value_t *isp2;
    Value_t *iend;

#ifdef	TRACE
    fprintf(stderr, "Entering new_state(%d)\n", symbol);
#endif

    if (S->nstates >= MAXYYINT)
	fatal(S, "too many states");

    isp1 = S->fs4_kernel_base[symbol];
    iend = S->fs4_kernel_end[symbol];
    n = (unsigned)(iend - isp1);

    p = (core *)allocate(S, (sizeof(core) + (n - 1) * sizeof(Value_t)));
    p->accessing_symbol = (Value_t)symbol;
    p->number = (Value_t)S->nstates;
    p->nitems = (Value_t)n;

    isp2 = p->items;
    while (isp1 < iend)
	*isp2++ = *isp1++;

    S->fs4_last_state->next = p;
    S->fs4_last_state = p;

    S->nstates++;

    return (p);
}

/* show_cores is used for debugging */
#ifdef DEBUG
void
show_cores(byacc_t* S)
{
    core *p;
    int i, j, k, n;
    int itemno;

    k = 0;
    for (p = S->first_state; p; ++k, p = p->next)
    {
	if (k)
	    printf("\n");
	printf("state %d, number = %d, accessing symbol = %s\n",
	       k, p->number, S->symbol_name[p->accessing_symbol]);
	n = p->nitems;
	for (i = 0; i < n; ++i)
	{
	    itemno = p->items[i];
	    printf("%4d  ", itemno);
	    j = itemno;
	    while (S->ritem[j] >= 0)
		++j;
	    printf("%s :", S->symbol_name[S->rlhs[-S->ritem[j]]]);
	    j = S->rrhs[-S->ritem[j]];
	    while (j < itemno)
		printf(" %s", S->symbol_name[S->ritem[j++]]);
	    printf(" .");
	    while (S->ritem[j] >= 0)
		printf(" %s", S->symbol_name[S->ritem[j++]]);
	    printf("\n");
	    fflush(stdout);
	}
    }
}

/* show_ritems is used for debugging */

void
show_ritems(byacc_t* S)
{
    int i;

    for (i = 0; i < S->nitems; ++i)
	printf("ritem[%d] = %d\n", i, S->ritem[i]);
}

/* show_rrhs is used for debugging */
void
show_rrhs(byacc_t* S)
{
    int i;

    for (i = 0; i < S->nrules; ++i)
	printf("rrhs[%d] = %d\n", i, S->rrhs[i]);
}

/* show_shifts is used for debugging */

void
show_shifts(byacc_t* S)
{
    shifts *p;
    int i, j, k;

    k = 0;
    for (p = S->first_shift; p; ++k, p = p->next)
    {
	if (k)
	    printf("\n");
	printf("shift %d, number = %d, nshifts = %d\n", k, p->number,
	       p->nshifts);
	j = p->nshifts;
	for (i = 0; i < j; ++i)
	    printf("\t%d\n", p->shift[i]);
    }
}
#endif

static void
save_shifts(byacc_t* S)
{
    shifts *p;
    Value_t *sp1;
    Value_t *sp2;
    Value_t *send;

    p = (shifts *)allocate(S, (sizeof(shifts) +
			      (unsigned)(S->fs4_nshifts - 1) * sizeof(Value_t)));

    p->number = S->fs4_this_state->number;
    p->nshifts = (Value_t)S->fs4_nshifts;

    sp1 = S->fs4_shiftset;
    sp2 = p->shift;
    send = S->fs4_shiftset + S->fs4_nshifts;

    while (sp1 < send)
	*sp2++ = *sp1++;

    if (S->fs4_last_shift)
    {
	S->fs4_last_shift->next = p;
	S->fs4_last_shift = p;
    }
    else
    {
	S->first_shift = p;
	S->fs4_last_shift = p;
    }
}

static void
save_reductions(byacc_t* S)
{
    Value_t *isp;
    Value_t *rp1;
    Value_t count;
    reductions *p;

    count = 0;
    for (isp = S->itemset; isp < S->itemsetend; isp++)
    {
	int item = S->ritem[*isp];

	if (item < 0)
	{
	    S->fs4_redset[count++] = (Value_t)-item;
	}
    }

    if (count)
    {
	Value_t *rp2;
	Value_t *rend;

	p = (reductions *)allocate(S, (sizeof(reductions) +
				      (unsigned)(count - 1) *
				    sizeof(Value_t)));

	p->number = S->fs4_this_state->number;
	p->nreds = count;

	rp1 = S->fs4_redset;
	rp2 = p->rules;
	rend = rp1 + count;

	while (rp1 < rend)
	    *rp2++ = *rp1++;

	if (S->fs4_last_reduction)
	{
	    S->fs4_last_reduction->next = p;
	    S->fs4_last_reduction = p;
	}
	else
	{
	    S->first_reduction = p;
	    S->fs4_last_reduction = p;
	}
    }
}

static void
set_derives(byacc_t* S)
{
    Value_t i, k;
    int lhs;

    S->derives = NEW2(S->nsyms, Value_t *);
    S->fs4_rules = NEW2(S->nvars + S->nrules, Value_t);

    k = 0;
    for (lhs = S->start_symbol; lhs < S->nsyms; lhs++)
    {
	S->derives[lhs] = S->fs4_rules + k;
	for (i = 0; i < S->nrules; i++)
	{
	    if (S->rlhs[i] == lhs)
	    {
		S->fs4_rules[k] = i;
		k++;
	    }
	}
	S->fs4_rules[k] = -1;
	k++;
    }

#ifdef	DEBUG
    print_derives(S);
#endif
}

#ifdef	DEBUG
void
print_derives(byacc_t* S)
{
    int i;
    Value_t *sp;

    printf("\nDERIVES\n\n");

    for (i = S->start_symbol; i < S->nsyms; i++)
    {
	printf("%s derives ", S->symbol_name[i]);
	for (sp = S->derives[i]; *sp >= 0; sp++)
	{
	    printf("  %d", *sp);
	}
	putchar('\n');
    }

    putchar('\n');
}
#endif

static void
set_nullable(byacc_t* S)
{
    int i, j;
    int empty;
    int done_flag;

    S->nullable = TMALLOC(char, S->nsyms);
    NO_SPACE(S->nullable);

    for (i = 0; i < S->nsyms; ++i)
	S->nullable[i] = 0;

    done_flag = 0;
    while (!done_flag)
    {
	done_flag = 1;
	for (i = 1; i < S->nitems; i++)
	{
	    empty = 1;
	    while ((j = S->ritem[i]) >= 0)
	    {
		if (!S->nullable[j])
		    empty = 0;
		++i;
	    }
	    if (empty)
	    {
		j = S->rlhs[-j];
		if (!S->nullable[j])
		{
		    S->nullable[j] = 1;
		    done_flag = 0;
		}
	    }
	}
    }

#ifdef DEBUG
    for (i = 0; i < S->nsyms; i++)
    {
	if (S->nullable[i])
	    printf("%s is nullable\n", S->symbol_name[i]);
	else
	    printf("%s is not nullable\n", S->symbol_name[i]);
    }
#endif
}

void
lr0(byacc_t* S)
{
    set_derives(S);
    set_nullable(S);
    generate_states(S);
}

#ifdef NO_LEAKS
void
lr0_leaks(byacc_t* S)
{
    if (S->derives)
    {
	if (S->derives[S->start_symbol] != S->fs4_rules)
	{
	    DO_FREE(S->derives[S->start_symbol]);
	}
	DO_FREE(S->derives);
	DO_FREE(S->fs4_rules);
    }
    DO_FREE(S->nullable);
}
#endif
