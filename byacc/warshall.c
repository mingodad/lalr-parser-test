/* $Id: warshall.c,v 1.9 2020/09/22 20:17:00 tom Exp $ */

#include "defs.h"

static void
transitive_closure(bitword_t *R, int n)
{
    int rowsize;
    unsigned i;
    bitword_t *rowj;
    bitword_t *rp;
    bitword_t *rend;
    bitword_t *relend;
    bitword_t *cword;
    bitword_t *rowi;

    rowsize = WORDSIZE(n);
    relend = R + n * rowsize;

    cword = R;
    i = 0;
    rowi = R;
    while (rowi < relend)
    {
	bitword_t *ccol = cword;

	rowj = R;

	while (rowj < relend)
	{
	    if (*ccol & (ONE_AS_BITWORD << i))
	    {
		rp = rowi;
		rend = rowj + rowsize;
		while (rowj < rend)
		    *rowj++ |= *rp++;
	    }
	    else
	    {
		rowj += rowsize;
	    }

	    ccol += rowsize;
	}

	if (++i >= BITS_PER_WORD)
	{
	    i = 0;
	    cword++;
	}

	rowi += rowsize;
    }
}

void
reflexive_transitive_closure(bitword_t *R, int n)
{
    int rowsize;
    unsigned i;
    bitword_t *rp;
    bitword_t *relend;

    transitive_closure(R, n);

    rowsize = WORDSIZE(n);
    relend = R + n * rowsize;

    i = 0;
    rp = R;
    while (rp < relend)
    {
	*rp |= (ONE_AS_BITWORD << i);
	if (++i >= BITS_PER_WORD)
	{
	    i = 0;
	    rp++;
	}

	rp += rowsize;
    }
}
