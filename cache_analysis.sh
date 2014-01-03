#!/bin/bash

if [ $# -ne 1 ] ;then
	echo "usage : xx <log_file>"
	exit 1
fi

file=$1

echo "------------- $file -------------"
awk -f test/cache_hit_analysis.awk $file

exit 0

