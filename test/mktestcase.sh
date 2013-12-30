#!/bin/bash

if [ $# -ne 2 ] ; then
	echo "Usage mktestcase <src_dir> <dst_dir>"
	exit 1
fi

SRC_DIR=$1
DST_DIR=$2

echo "mktest case from $SRC_DIR to $DST_DIR ........... "

set -- $(ls "$SRC_DIR")
if [ $? -ne 0 ] ; then
	echo "exit"
fi

echo $#
for file in $@
do
	./mktestcase "$file" $SRC_DIR $DST_DIR
done

exit 0
