#!/bin/sh
# $Id: run_lint.sh,v 1.4 2021/06/19 19:30:10 tom Exp $
# vi:ts=4 sw=4:

# run lint on each of the ".c" files in the test directory

if test $# = 1
then
	PROG_DIR=`pwd`
	TEST_DIR=$1
else
	PROG_DIR=..
	TEST_DIR=.
fi

ifBTYACC=`grep -F -l 'define YYBTYACC' config.h > /dev/null; test $? != 0; echo $?`

if test "$ifBTYACC" = 0; then
	REF_DIR=${TEST_DIR}/yacc
else
	REF_DIR=${TEST_DIR}/btyacc
fi

echo "** `date`"
for i in ${REF_DIR}/*.c
do
	make -f $PROG_DIR/makefile lint C_FILES="$i" srcdir="$PROG_DIR"
done
