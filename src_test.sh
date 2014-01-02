#!/bin/bash

if [ $# -ne 4 ] ;then
	echo "Usage : ./src_test.sh <ip> <data_dir> <log_dir> <blk_sz upper limit>"
	exit 1
fi

IP_ADDR=$1
DIR_TOS=$2
DIR_LOG=$3
BLK_SZ_LIMIT=$4
SUMMARY_FILE=summary.log

if [ ! -x "$DIR_LOG" ] ;then
	mkdir "$DIR_LOG"
fi

unlink "$DIR_LOG/$SUMMARY_FILE"	2> /dev/null
touch "$DIR_LOG/$SUMMARY_FILE"

i=0
for((i=1;i<="$BLK_SZ_LIMIT";i++))
{
#	echo "$i"
	./src.sh "$IP_ADDR" "$DIR_TOS" "$i" > "$DIR_LOG/log$i"
	if [ $? -ne 0 ] ;then
		exit 1
	fi
	echo "+++++++++++++ blk_sz -- $i +++++++++++++" >> "$DIR_LOG/$SUMMARY_FILE"
	awk -f test/log_analysis.awk "$DIR_LOG/log$i" >> "$DIR_LOG/$SUMMARY_FILE"
}

exit 0
