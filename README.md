# lalr-parser-test
## Testing how different LALR(1) parsers detect grammar conflicts

The `lemon` parser here have several modifications like:
- Command line option to convert from lemon grammar to yacc grammar `-y           Print yacc grammar without actions.`
- Command line option to convert from lemon grammar to yacc grammar with full precedences `-Y           Print yacc grammar without actions with full precedences.`
- Command line option to use yacc rule precedences `-z           Use yacc rule precedence`
- Command line option to ignore all precedences `-u           Ignore all precedences`
- Added a new directive "%yacc_prec" to use yacc rule precedences (the same as command line `-u`).
- Enhanced command line option to output a naked lemon grammar `-g           Print grammar without actions.`

The byacc parser (from https://invisible-island.net/byacc/byacc.html) here have several modifications like:
- Command line option to convert from yacc grammar to lemon grammar `-E                    write lemon grammar`
- Command line option to convert from yacc grammar to EBNF grammar (understood by https://www.bottlecaps.de/rr/ui) `-e                    write ebnf grammar`
- Command line option to ignore all precedences `-u                    ignore precedences`
- Command line option to use lemon rule precedences `-z                    use leftmost token for rule precedence`
- Add code to accept/skip bison styles alias for non-terminals `rule[alias]` 


All of the above was made to make easier to compare how lemon/byacc/bison parse LARL(1) grammars.

Obs: On the examples showun bellow `lemon-nb` and `byacc-nb` stands for the lemon/byacc parser from this repository,
`mybison` stands for bison-3.8.2 modified with an extra command line `-z, --noprec                     ignore all precedences`.

## Testing sqlite3 grammar:
```
$>lemon-nb -Y sqlite3.yl > sqlite3.y #convert lemon grammar to yacc grammar

$>byacc-nb -E sqlite3.y # convert yacc grammar to lemon grammar
byacc-nb: 53 reduce/reduce conflicts.
53 conflicts
185 terminal symbols
134 non-terminal symbols
319 total symbols
445 rules
887 states

$>mv y.yl sqlite3-y.yl # rename converted lemon grammar

$>lemon-nb -s sqlite3-y.yl
Parser statistics:
  terminal symbols...................   184
  non-terminal symbols...............   133
  total symbols......................   317
  rules..............................   442
  states.............................   602
  conflicts..........................     0
  action table entries...............  2078
  lookahead table entries............  2262
  total table size (bytes)........... 11956

$>lemon-nb -s -u sqlite3-y.yl # parse again but ignoring all precedences
827 parsing conflicts.
Parser statistics:
  terminal symbols...................   184
  non-terminal symbols...............   133
  total symbols......................   317
  rules..............................   442
  states.............................   602
  conflicts..........................   827
  conflicts S/R......................   788
  conflicts R/R......................    39
  action table entries...............  2000
  lookahead table entries............  2184
  total table size (bytes)........... 11644
 
$>byacc-nb  sqlite3.y
byacc-nb: 53 reduce/reduce conflicts.
53 conflicts
185 terminal symbols
134 non-terminal symbols
319 total symbols
445 rules
887 states

$>byacc-nb -u sqlite3.y # parse again but ignoring all precedences
byacc-nb: 788 shift/reduce conflicts, 39 reduce/reduce conflicts.
827 conflicts
185 terminal symbols
134 non-terminal symbols
319 total symbols
445 rules
887 states

$>mybison  sqlite3.y
sqlite3.y: warning: 53 reduce/reduce conflicts [-Wconflicts-rr]
sqlite3.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples

$>mybison -z sqlite3.y # parse again but ignoring all precedences
sqlite3.y: warning: 734 shift/reduce conflicts [-Wconflicts-sr]
sqlite3.y: warning: 93 reduce/reduce conflicts [-Wconflicts-rr]
sqlite3.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples
```

## Testing cfront3 grammar (https://github.com/mingodad/cfront-3/blob/master/src/gram.y):
```
$>byacc-nb -E -n cfront3.y # convert yacc grammar to lemon grammar, also generate naked yacc grammar
byacc-nb: 21 shift/reduce conflicts, 3 reduce/reduce conflicts.
24 conflicts
95 terminal symbols
115 non-terminal symbols
210 total symbols
417 rules
709 states

$>mv y.yl cfront3.yl # rename converted lemon grammar

$>mv y.yn cfront3-naked.y # rename naked yacc grammar

$>lemon-nb -s cfront3.yl
19 parsing conflicts.
Parser statistics:
  terminal symbols...................    94
  non-terminal symbols...............    92
  total symbols......................   186
  rules..............................   391
  states.............................   421
  conflicts..........................    19
  conflicts S/R......................    19
  conflicts R/R......................     0
  action table entries...............  5360
  lookahead table entries............  5360
  total table size (bytes)........... 18272

$>lemon-nb -s -u cfront3.yl # parse again but ignoring all precedences
cfront3.yl:230: This rule can not be reduced.

1077 parsing conflicts.
Parser statistics:
  terminal symbols...................    94
  non-terminal symbols...............    92
  total symbols......................   186
  rules..............................   391
  states.............................   423
  conflicts..........................  1077
  conflicts S/R......................  1074
  conflicts R/R......................     3
  action table entries...............  5167
  lookahead table entries............  5202
  total table size (bytes)........... 17736

$>byacc-nb  cfront3-naked.y
byacc-nb: 21 shift/reduce conflicts, 3 reduce/reduce conflicts.
24 conflicts
95 terminal symbols
92 non-terminal symbols
187 total symbols
394 rules
686 states

$>byacc-nb -u cfront3-naked.y # parse again but ignoring all precedences
byacc-nb: 1 rule never reduced
byacc-nb: 1074 shift/reduce conflicts, 3 reduce/reduce conflicts.
1077 conflicts
95 terminal symbols
92 non-terminal symbols
187 total symbols
394 rules
686 states

$>mybison  cfront3-naked.y
cfront3-naked.y: warning: 20 shift/reduce conflicts [-Wconflicts-sr]
cfront3-naked.y: warning: 4 reduce/reduce conflicts [-Wconflicts-rr]
cfront3-naked.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples

$>mybison -z cfront3-naked.y # parse again but ignoring all precedences
cfront3-naked.y: warning: 1073 shift/reduce conflicts [-Wconflicts-sr]
cfront3-naked.y: warning: 4 reduce/reduce conflicts [-Wconflicts-rr]
cfront3-naked.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples
cfront3-naked.y:251.11-29: warning: rule useless in parser due to conflicts [-Wother]
  251 | 	| e /*14:1*/ %prec GT
      |           ^~~~~~~~~~~~~~~~~~~
```

## Testing cql grammar (https://github.com/facebookincubator/CG-SQL/blob/main/sources/cql.y):
```
$>byacc-nb -E cql.y # convert yacc grammar to lemon grammar

$>mv y.yl cql.yl # rename converted lemon grammar

$>lemon-nb -s cql.yl
Parser statistics:
  terminal symbols...................   246
  non-terminal symbols...............   234
  total symbols......................   480
  rules..............................   695
  states.............................   766
  conflicts..........................     0
  action table entries...............  5146
  lookahead table entries............  5146
  total table size (bytes)........... 24484

$>lemon-nb -s -z cql.yl #parse using yacc rule precedences
Parser statistics:
  terminal symbols...................   246
  non-terminal symbols...............   234
  total symbols......................   480
  rules..............................   695
  states.............................   766
  conflicts..........................     0
  action table entries...............  5146
  lookahead table entries............  5146
  total table size (bytes)........... 24484

$>lemon-nb -s -u cql.yl # parse again but ignoring all precedences
1291 parsing conflicts.
Parser statistics:
  terminal symbols...................   246
  non-terminal symbols...............   234
  total symbols......................   480
  rules..............................   695
  states.............................   769
  conflicts..........................  1291
  conflicts S/R......................  1291
  conflicts R/R......................     0
  action table entries...............  5145
  lookahead table entries............  5145
  total table size (bytes)........... 24492

$>byacc-nb  cql.y

$>byacc-nb -u cql.y # parse again but ignoring all precedences
byacc-nb: 1291 shift/reduce conflicts.
1291 conflicts
263 terminal symbols
235 non-terminal symbols
498 total symbols
698 rules
1309 states

$>mybison  cql.y

$>mybison -z cql.y # parse again but ignoring all precedences
cql.y: warning: 1291 shift/reduce conflicts [-Wconflicts-sr]
cql.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples
```

## Testing zephir-lang grammar (https://github.com/zephir-lang/php-zephir-parser/blob/development/parser/zephir.lemon):
```
$>lemon-nb  -Y zephir.yl > zephir.y #convert lemon grammar to yacc grammar

$>lemon-nb -s zephir.yl
Parser statistics:
  terminal symbols...................   135
  non-terminal symbols...............   102
  total symbols......................   237
  rules..............................   491
  states.............................   645
  conflicts..........................     0
  action table entries............... 15308
  lookahead table entries............ 15369
  total table size (bytes)........... 49231

$>lemon-nb -s -u zephir.yl # parse again but ignoring all precedences
1215 parsing conflicts.
Parser statistics:
  terminal symbols...................   135
  non-terminal symbols...............   102
  total symbols......................   237
  rules..............................   491
  states.............................   645
  conflicts..........................  1215
  conflicts S/R......................  1215
  conflicts R/R......................     0
  action table entries............... 15964
  lookahead table entries............ 15977
  total table size (bytes)........... 51151

$>byacc-nb  zephir.y

$>byacc-nb -u zephir.y # parse again but ignoring all precedences
byacc-nb: 1215 shift/reduce conflicts.
1215 conflicts
136 terminal symbols
103 non-terminal symbols
239 total symbols
494 rules
1031 states

$>mybison  zephir.y

$>mybison -z zephir.y # parse again but ignoring all precedences
zephir.y: warning: 1215 shift/reduce conflicts [-Wconflicts-sr]
zephir.y: note: rerun with option '-Wcounterexamples' to generate conflict counterexamples
```
