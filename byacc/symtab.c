/* $Id: symtab.c,v 1.11 2014/03/26 00:17:09 Tom.Shields Exp $ */

#include "defs.h"

/* TABLE_SIZE is the number of entries in the symbol table. */
/* TABLE_SIZE must be a power of two.			    */

#define	TABLE_SIZE 1024

static int
hash(const char *name)
{
    const char *s;
    int c, k;

    assert(name && *name);
    s = name;
    k = *s;
    while ((c = *++s) != 0)
	k = (31 * k + c) & (TABLE_SIZE - 1);

    return (k);
}

bucket *
make_bucket(byacc_t* S, const char *name)
{
    bucket *bp;

    assert(name != 0);

    bp = TMALLOC(bucket, 1);
    NO_SPACE(bp);

    bp->link = 0;
    bp->next = 0;

    bp->name = TMALLOC(char, strlen(name) + 1);
    NO_SPACE(bp->name);

    bp->tag = 0;
    bp->value = UNDEFINED;
    bp->index = 0;
    bp->prec = 0;
    bp->class = UNKNOWN;
    bp->assoc = TOKEN;
#if defined(YYBTYACC)
    bp->args = -1;
    bp->argnames = 0;
    bp->argtags = 0;
    bp->destructor = 0;
#endif
    bp->prec_on_decl = 0;
    strcpy(bp->name, name);

    return (bp);
}

bucket *
lookup(byacc_t* S, const char *name)
{
    bucket *bp, **bpp;

    bpp = S->fs7_symbol_table + hash(name);
    bp = *bpp;

    while (bp)
    {
	if (strcmp(name, bp->name) == 0)
	    return (bp);
	bpp = &bp->link;
	bp = *bpp;
    }

    *bpp = bp = make_bucket(S, name);
    S->last_symbol->next = bp;
    S->last_symbol = bp;

    return (bp);
}

void
create_symbol_table(byacc_t* S)
{
    int i;
    bucket *bp;

    S->fs7_symbol_table = TMALLOC(bucket *, TABLE_SIZE);
    NO_SPACE(S->fs7_symbol_table);

    for (i = 0; i < TABLE_SIZE; i++)
	S->fs7_symbol_table[i] = 0;

    bp = make_bucket(S, "error");
    bp->index = 1;
    bp->class = TERM;

    S->first_symbol = bp;
    S->last_symbol = bp;
    S->fs7_symbol_table[hash("error")] = bp;
}

void
free_symbol_table(byacc_t* S)
{
    FREE(S->fs7_symbol_table);
    S->fs7_symbol_table = 0;
}

void
free_symbols(byacc_t* S)
{
    bucket *p, *q;

    for (p = S->first_symbol; p; p = q)
    {
	q = p->next;
	FREE(p);
    }
}
