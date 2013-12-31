#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage : ./pick_original_data_set.sh <src_dir> <dst_dir>"
	exit 1
fi

SRC_DIR=$1
DST_DIR=$2

set -- $(ls "$SRC_DIR")
if [ $? -ne 0 ] ;then
	exit 1
fi

# if dst_dir does not exist
if [ ! -x "$DST_DIR" ] ;then
	mkdir "$DST_DIR"
fi

for file in $@
do
	full_name="$SRC_DIR/$file"
	if [ -d "$full_name" ] ;then
		continue
	fi
	sz=$(stat "$full_name" | awk 'NR==2{print $2}')
	if [ $sz -gt 8192 ] ;then
		cp "$full_name" "$DST_DIR/$file"
	fi
done

exit 0

