#!/bin/bash

if [ $# -ne 3 ] ;then
	echo "Usage : ./cdc_test.sh <origin_data_dir> <origin_cdc_dir> <to_search_cdc_dir>"
	exit 1
fi

DIR_ORID=$1
DIR_ORI=$2
DIR_TOS=$3

set -- $(ls "$DIR_TOS")

if [ $? -ne 0 ] ;then
	exit 1
fi

for file in $@
do
	orig_filesz=$(stat "$DIR_ORID/$file" | awk 'NR==2{print $2}')
	total_blks=$(wc -l "$DIR_ORI/$file" | awk '{print $1}')
	matched_blks=$(simple_search/sim_detect.py "$DIR_TOS/$file")
#	simple_search/sim_detect.py "$DIR_TOS/$file"
	if [ $? -ne 0 ] ;then
		exit 1
	fi
	let "hit_ratio=100*$matched_blks/$total_blks"
	echo "$orig_filesz $matched_blks $total_blks $hit_ratio%"
done
