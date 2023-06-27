/* $Id: closure.c,v 1.13 2020/09/22 20:17:00 tom Exp $ */

#include "defs.h"

Value_t *itemset;
Value_t *itemsetend;
bitword_t *ruleset;

static bitword_t *first_base;
static bitword_t *first_derives;
static bitword_t *EFF;

#ifdef	DEBUG
static void print_closure(int);
static void print_EFF(void);
static void print_first_derives(void);
#endif

static void
set_EFF(void)
{
    bitword_t *row;
    int symbol;
    int rowsize;
    int i;
    int rule;

    rowsize = WORDSIZE(nvars);
    EFF = NEW2(nvars * rowsize, bitword_t);

    row = EFF;
    for (i = start_symbol; i < nsyms; i++)
    {
	Value_t *sp = derives[i];
	for (rule = *sp; rule > 0; rule = *++sp)
	{
	    symbol = ritem[rrhs[rule]];
	    if (ISVAR(symbol))
	    {
		symbol -= start_symbol;
		SETBIT(row, symbol);
	    }
	}
	row += rowsize;
    }

    reflexive_transitive_closure(EFF, nvars);

#ifdef	DEBUG
    print_EFF();
#endif
}

void
set_first_derives(void)
{
    bitword_t *rrow;
    int j;
    bitword_t cword = 0;
    Value_t *rp;

    int rule;
    int i;
    int rulesetsize;
    int varsetsize;

    rulesetsize = WORDSIZE(nrules);
    varsetsize = WORDSIZE(nvars);
    first_base = NEW2(nvars * rulesetsize, bitword_t);
    first_derives = first_base - ntokens * rulesetsize;

    set_EFF();

    rrow = first_derives + ntokens * rulesetsize;
    for (i = start_symbol; i < nsyms; i++)
    {
	bitword_t *vrow = EFF + ((i - ntokens) * varsetsize);
	unsigned k = BITS_PER_WORD;

	for (j = start_symbol; j < nsyms; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *vrow++;
		k = 0;
	    }

	    if (cword & (ONE_AS_BITWORD << k))
	    {
		rp = derives[j];
		while ((rule = *rp++) >= 0)
		{
		    SETBIT(rrow, rule);
		}
	    }
	}

	rrow += rulesetsize;
    }

#ifdef	DEBUG
    print_first_derives();
#endif

    FREE(EFF);
}

void
closure(Value_t *nucleus, int n)
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

    rulesetsize = WORDSIZE(nrules);
    rsend = ruleset + rulesetsize;
    for (rsp = ruleset; rsp < rsend; rsp++)
	*rsp = 0;

    csend = nucleus + n;
    for (csp = nucleus; csp < csend; ++csp)
    {
	int symbol = ritem[*csp];

	if (ISVAR(symbol))
	{
	    dsp = first_derives + symbol * rulesetsize;
	    rsp = ruleset;
	    while (rsp < rsend)
		*rsp++ |= *dsp++;
	}
    }

    ruleno = 0;
    itemsetend = itemset;
    csp = nucleus;
    for (rsp = ruleset; rsp < rsend; ++rsp)
    {
	bitword_t word = *rsp;

	if (word)
	{
	    for (i = 0; i < BITS_PER_WORD; ++i)
	    {
		if (word & (ONE_AS_BITWORD << i))
		{
		    itemno = rrhs[ruleno + i];
		    while (csp < csend && *csp < itemno)
			*itemsetend++ = *csp++;
		    *itemsetend++ = itemno;
		    while (csp < csend && *csp == itemno)
			++csp;
		}
	    }
	}
	ruleno += BITS_PER_WORD;
    }

    while (csp < csend)
	*itemsetend++ = *csp++;

#ifdef	DEBUG
    print_closure(n);
#endif
}

void
finalize_closure(void)
{
    FREE(itemset);
    FREE(ruleset);
    FREE(first_base);
}

#ifdef	DEBUG

static void
print_closure(int n)
{
    Value_t *isp;

    printf("\n\nn = %d\n\n", n);
    for (isp = itemset; isp < itemsetend; isp++)
	printf("   %d\n", *isp);
}

static void
print_EFF(void)
{
    int i, j;
    bitword_t *rowp;
    bitword_t word;
    unsigned k;

    printf("\n\nEpsilon Free Firsts\n");

    for (i = start_symbol; i < nsyms; i++)
    {
	printf("\n%s", symbol_name[i]);
	rowp = EFF + ((i - start_symbol) * WORDSIZE(nvars));
	word = *rowp++;

	k = BITS_PER_WORD;
	for (j = 0; j < nvars; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		word = *rowp++;
		k = 0;
	    }

	    if (word & (ONE_AS_BIWORD << k))
		printf("  %s", symbol_name[start_symbol + j]);
	}
    }
}

static void
print_first_derives(void)
{
    int i;
    int j;
    bitword_t *rp;
    bitword_t cword = 0;
    unsigned k;

    printf("\n\n\nFirst Derives\n");

    for (i = start_symbol; i < nsyms; i++)
    {
	printf("\n%s derives\n", symbol_name[i]);
	rp = first_derives + i * WORDSIZE(nrules);
	k = BITS_PER_WORD;
	for (j = 0; j <= nrules; k++, j++)
	{
	    if (k >= BITS_PER_WORD)
	    {
		cword = *rp++;
		k = 0;
	    }

	    if (cword & (ONE_AS_BIWORD << k))
		printf("   %d\n", j);
	}
    }

    fflush(stdout);
}

#endif
