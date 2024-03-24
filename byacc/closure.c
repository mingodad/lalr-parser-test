/* $Id: closure.c,v 1.13 2020/09/22 20:17:00 tom Exp $ */

#include "defs.h"

static void
set_EFF(byacc_t* S)
{
    bitword_t *row;
    int symbol;
    int rowsize;
    int i;
    int rule;

    rowsize = WORDSIZE(S->nvars);
    S->fs1_EFF = NEW2(S->nvars * rowsize, bitword_t);

    row = S->fs1_EFF;
    for (i = S->start_symbol; i < S->nsyms; i++)
    {
	Value_t *sp = S->derives[i];
	for (rule = *sp; rule > 0; rule = *++sp)
	{
	    symbol = S->ritem[S->rrhs[rule]];
	    if (ISVAR(symbol))
	    {
		symbol -= S->start_symbol;
		SETBIT(row, symbol);
	    }
	}
	row += rowsize;
    }

    reflexive_transitive_closure(S->fs1_EFF, S->nvars);

#ifdef	DEBUG
    print_EFF(S);
#endif
}

void
set_first_derives(byacc_t* S)
{
    bitword_t *rrow;
    int j;
    bitword_t cword = 0;
    Value_t *rp;

    int rule;
    int i;
    int rulesetsize;
    int varsetsize;

    rulesetsize = WORDSIZE(S->nrules);
    varsetsize = WORDSIZE(S->nvars);
    S->fs1_first_derives = NEW2(S->nvars * rulesetsize, bitword_t);

    set_EFF(S);

    rrow = S->fs1_first_derives;
    for (i = S->start_symbol; i < S->nsyms; i++)
    {
	bitword_t *vrow = S->fs1_EFF + ((i - S->ntokens) * varsetsize);
	unsigned k = BITS_PER_WORD;

	for (j = S->start_symbol; j < S->nsyms; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *vrow++;
		k = 0;
	    }

	    if (cword & (ONE_AS_BITWORD << k))
	    {
		rp = S->derives[j];
		while ((rule = *rp++) >= 0)
		{
		    SETBIT(rrow, rule);
		}
	    }
	}

	rrow += rulesetsize;
    }

#ifdef	DEBUG
    print_first_derives(S);
#endif

    FREE(S->fs1_EFF);
}

void
closure(byacc_t* S, Value_t *nucleus, int n)
{
    unsigned ruleno;
    unsigned i;
    Value_t *csp;
    bitword_t *dsp;
    bitword_t *rsp;
    int rulesetsize;

    Value_t *csend;
    bitword_t *rsend;
    Value_t itemno;

    rulesetsize = WORDSIZE(S->nrules);
    rsend = S->ruleset + rulesetsize;
    for (rsp = S->ruleset; rsp < rsend; rsp++)
	*rsp = 0;

    csend = nucleus + n;
    for (csp = nucleus; csp < csend; ++csp)
    {
	int symbol = S->ritem[*csp];

	if (ISVAR(symbol))
	{
	    dsp = S->fs1_first_derives + (symbol - S->ntokens) * rulesetsize;
	    rsp = S->ruleset;
	    while (rsp < rsend)
		*rsp++ |= *dsp++;
	}
    }

    ruleno = 0;
    S->itemsetend = S->itemset;
    csp = nucleus;
    for (rsp = S->ruleset; rsp < rsend; ++rsp)
    {
	bitword_t word = *rsp;

	if (word)
	{
	    for (i = 0; i < BITS_PER_WORD; ++i)
	    {
		if (word & (ONE_AS_BITWORD << i))
		{
		    itemno = S->rrhs[ruleno + i];
		    while (csp < csend && *csp < itemno)
			*S->itemsetend++ = *csp++;
		    *S->itemsetend++ = itemno;
		    while (csp < csend && *csp == itemno)
			++csp;
		}
	    }
	}
	ruleno += BITS_PER_WORD;
    }

    while (csp < csend)
	*S->itemsetend++ = *csp++;

#ifdef	DEBUG
    print_closure(S, n);
#endif
}

void
finalize_closure(byacc_t* S)
{
    FREE(S->itemset);
    FREE(S->ruleset);
    FREE(S->fs1_first_derives);
}

#ifdef	DEBUG

void
print_closure(byacc_t* S, int n)
{
    Value_t *isp;

    printf("\n\nn = %d\n\n", n);
    for (isp = S->itemset; isp < S->itemsetend; isp++)
	printf("   %d\n", *isp);
}

void
print_EFF(byacc_t* S)
{
    int i, j;
    bitword_t *rowp;
    bitword_t word;
    unsigned k;

    printf("\n\nEpsilon Free Firsts\n");

    for (i = S->start_symbol; i < S->nsyms; i++)
    {
	printf("\n%s", S->symbol_name[i]);
	rowp = S->fs1_EFF + ((i - S->start_symbol) * WORDSIZE(S->nvars));
	word = *rowp++;

	k = BITS_PER_WORD;
	for (j = 0; j < S->nvars; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		word = *rowp++;
		k = 0;
	    }

	    if (word & (ONE_AS_BITWORD << k))
		printf("  %s", S->symbol_name[S->start_symbol + j]);
	}
    }
}

void
print_first_derives(byacc_t* S)
{
    int i;
    int j;
    bitword_t *rp;
    bitword_t cword = 0;
    unsigned k;

    printf("\n\n\nFirst Derives\n");

    for (i = S->start_symbol; i < S->nsyms; i++)
    {
	printf("\n%s derives\n", S->symbol_name[i]);
	rp = S->fs1_first_derives + (i -S->ntokens) * WORDSIZE(S->nrules);
	k = BITS_PER_WORD;
	for (j = 0; j <= S->nrules; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *rp++;
		k = 0;
	    }

	    if (cword & (ONE_AS_BITWORD << k))
		printf("   %d\n", j);
	}
    }

    fflush(stdout);
}

#endif
