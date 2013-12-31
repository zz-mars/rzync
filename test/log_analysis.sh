#!/bin/bash

if [ $# -ne 1 ] ;then
	echo "Usange : ./log_analysis.sh <log_file>"
	exit 1
fi

LOG_FILE=$1

cat "$LOG_FILE" | \
		awk 'BEGIN{t=0;ts=0;tfsz=0;}{
			if (NF==3) {
				tid=tid+$1+$2;
				tfsz=tfsz+$3;
			} else if (NF==1) {
				t = $1;
			}
		}END{
			print "time elapsed : ",t,"s\ntotal_interacter_bytes : ",tid,"bytes\ntotal_filesz : ",tfsz,"bytes\ncompression_ratio : ",100.0*(tfsz-tid)/tfsz,"%"
		}'

