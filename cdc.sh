#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage : ./cdc.sh <src_dir> <dst_dir>"
	exit 1
fi

SRC_DIR=$1
DST_DIR=$2

if [ ! -x "$DST_DIR" ] ;then
	mkdir "$DST_DIR"
fi

set -- $(ls "$SRC_DIR")

if [ $? -ne 0 ] ;then
	exit 1
fi

for file in $@
do
	if [ -d "$SRC_DIR/$file" ] ;then
		continue
	fi

	./cdc "$SRC_DIR/$file" > "$DST_DIR/$file"
	if [ $? -ne 0 ] ;then
		exit 1
	fi
done

exit 0

