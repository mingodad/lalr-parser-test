/* $Id: graph.c,v 1.9 2020/09/10 17:22:51 tom Exp $ */

#include "defs.h"

static void graph_state(byacc_t* S, int stateno);
static void graph_LA(byacc_t* S, int ruleno);

void
graph(byacc_t* S)
{
    int i;
    int j;
    shifts *sp;
    int sn;
    int as;

    if (!S->gflag)
	return;

    for (i = 0; i < S->nstates; ++i)
    {
	closure(S, S->state_table[i]->items, S->state_table[i]->nitems);
	graph_state(S, i);
    }

    fprintf(S->graph_file, "\n\n");
    for (i = 0; i < S->nstates; ++i)
    {

	sp = S->shift_table[i];
	if (sp)
	    for (j = 0; j < sp->nshifts; ++j)
	    {
		sn = sp->shift[j];
		as = S->accessing_symbol[sn];
		fprintf(S->graph_file,
			"\tq%d -> q%d [label=\"%s\"];\n",
			i, sn, S->symbol_pname[as]);
	    }
    }

    fprintf(S->graph_file, "}\n");

    for (i = 0; i < S->nsyms; ++i)
	FREE(S->symbol_pname[i]);
    FREE(S->symbol_pname);
}

static void
graph_state(byacc_t* S, int stateno)
{
    Value_t *isp;
    Value_t *sp;

    S->fs2_larno = (unsigned)S->lookaheads[stateno];
    fprintf(S->graph_file, "\n\tq%d [label=\"%d:\\l", stateno, stateno);

    for (isp = S->itemset; isp < S->itemsetend; isp++)
    {
	Value_t *sp1;
	int rule;

	sp1 = sp = S->ritem + *isp;

	while (*sp >= 0)
	    ++sp;
	rule = -(*sp);
	fprintf(S->graph_file, "  %s -> ", S->symbol_pname[S->rlhs[rule]]);

	for (sp = S->ritem + S->rrhs[rule]; sp < sp1; sp++)
	    fprintf(S->graph_file, "%s ", S->symbol_pname[*sp]);

	putc('.', S->graph_file);

	while (*sp >= 0)
	{
	    fprintf(S->graph_file, " %s", S->symbol_pname[*sp]);
	    sp++;
	}

	if (*sp1 < 0)
	    graph_LA(S, -*sp1);

	fprintf(S->graph_file, "\\l");
    }
    fprintf(S->graph_file, "\"];");
}

static void
graph_LA(byacc_t* S, int ruleno)
{
    unsigned tokensetsize;

    tokensetsize = (unsigned)WORDSIZE(S->ntokens);

    if (ruleno == S->LAruleno[S->fs2_larno])
    {
	int i;
	bitword_t *rowp = S->LA + S->fs2_larno * tokensetsize;

	fprintf(S->graph_file, " { ");
	for (i = S->ntokens - 1; i >= 0; i--)
	{
	    if (BIT(rowp, i))
		fprintf(S->graph_file, "%s ", S->symbol_pname[i]);
	}
	fprintf(S->graph_file, "}");
	++S->fs2_larno;
    }
}
