#!/bin/bash

set -ue

if [ -e .git/MERGE_HEAD ]
then
    echo Merge commit
    exit 0
fi

def_ignore="SPLIT_STRING,SSCANF_TO_KSTRTO,PREFER_KERNEL_TYPES"
def_ignore+=",USE_NEGATIVE_ERRNO,CAMELCASE,STATIC_CONST_CHAR_ARRAY"
def_ignore+=",COMPARISON_TO_NULL,COMPLEX_MACRO,BIT_MACRO"
def_ignore+=",CONCATENATED_STRING,PARENTHESIS_ALIGNMENT"

# def_ignore+=",SPACING"

CP=../code_review/checkpatch.pl
if [ ! -x $CP ]
then
	CP=./code_review/checkpatch.pl
fi

if [ -x $CP ]
then
    for FILE in $(git diff-index --cached --name-only HEAD | grep "^src" | grep -v -e src/control/vendor -e .go\$ -e pb-c -e debug_setup.h)
    do
	ignore="$def_ignore"
	if grep -lq CRT_RPC_DECLARE "$FILE"; then
	    ignore+=",SPACING_CAST,SPACING,TRAILING_SEMICOLON,MULTISTATEMENT_MACRO_USE_DO_WHILE,INDENTED_LABEL"
	fi
#	git reset $file
        $CP --quiet --no-tree --file --show-types --no-summary --max-line-length=100 --ignore "$ignore" --strict "$FILE" --fix-inplace || true
	./ci/check_d_macro_calls2.py "$FILE" | patch -p1
	./ci/check_d_macro_calls2.py "$FILE" | patch -p1
	./ci/check_d_macro_calls2.py "$FILE" | patch -p1
	echo Adding file $FILE
	git add $FILE
    done
else
        echo "code_review not checked out, skipping checkpatch"
        echo "git -C ../ clone https://github.com/daos-stack/code_review.git"
fi

exit 0
