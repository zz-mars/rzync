#!/bin/bash

if [ $# -ne 4 ] ; then
	echo "Usage mktestcase <src_dir> <dst_dir> <add_times> <modi_times>"
	exit 1
fi

SRC_DIR=$1
DST_DIR=$2
ADDT=$3
MODIT=$4

echo "mktest case from $SRC_DIR to $DST_DIR ........... "

set -- $(ls "$SRC_DIR")
if [ $? -ne 0 ] ; then
	echo "exit"
	exit 1
fi

echo $#
for file in $@
do
	if [ -d "$SRC_DIR/$file" ] ;then
		echo "skip directory.."
		continue
	fi
	./mktestcase "$file" "$SRC_DIR" "$DST_DIR" "$ADDT" "$MODIT"
	if [ $? -ne 0 ] ; then
		echo "exit"
		exit 1
	fi
done

exit 0
