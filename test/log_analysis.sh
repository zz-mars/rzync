#!/bin/bash

if [ $# -ne 1 ] ;then
	echo "Usange : ./log_analysis.sh <log_file>"
	exit 1
fi

LOG_FILE=$1

cat "$LOG_FILE" | \
		awk 'BEGIN{ts=0;tfsz=0;}	\
		{tid=tid+$1+$2;tfsz=tfsz+$3}		\
		END{print "total_interacter_bytes : ",tid,"\ntotal_filesz : ",tfsz,"\ncompression_ratio : ",100.0*(tfsz-tid)/tfsz,"%"}'

