#!/bin/bash

if [ $# -ne 2 ] ;then
	echo "Usage : ./cdc_test.sh <origin_dir> <to_search_dir>"
	exit 1
fi

DIR_ORI=$1
DIR_TOS=$2

set -- $(ls "$DIR_TOS")

if [ $? -ne 0 ] ;then
	exit 1
fi

for file in $@
do
	total_lines=$(wc -l "$DIR_ORI/$file" | awk '{print $1}')
	out_put=$(simple_search/sim_detect.py "$DIR_TOS/$file")
#	simple_search/sim_detect.py "$DIR_TOS/$file"
	if [ $? -ne 0 ] ;then
		exit 1
	fi
	echo "$out_put $total_lines"
done
