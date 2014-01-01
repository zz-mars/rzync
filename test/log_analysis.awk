#!/bin/awk
BEGIN{
	ts=0;
	treq=0;
	tduph=0;
	tnduph=0;
	tdd=0;
	tr=0;
	tckh=0;
	tcks=0;
	tfsz=0;
}
{
	if (NF==9) {
		ts=ts+$1;
		treq=treq+$2;
		tduph=tduph+$3;
		tnduph=tnduph+$4;
		tdd=tdd+$5;
		tr=tr+$6;
		tckh=tckh+$7;
		tcks=tcks+$8;
		tfsz=tfsz+$9;
	} else if (NF==1) {
		t = $1;
	}
}
END{
	printf("+++++++++++++++++++++++++++++");
	printf("time elapsed : %u\n",t);
	printf("total_sent : %u\n",ts);
	printf("total_req  : %u\n",treq);
	printf("total_dup_h: %u\n",tduph);
	printf("total_ndup_h: %u\n",tnduph);
	printf("total_delta_data : %u\n",tdd);
	printf("total_recved: %u\n",tr);
	printf("total_checksum_h : %u\n",tckh);
	printf("total_checksums : %u\n",tcks);
	printf("total_filesz : %u\n",tfsz);
}

