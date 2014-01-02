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
	tdupblk=0;
	tfsz=0;
}
{
	if (NF==10) {
		ts=ts+$1;
		treq=treq+$2;
		tduph=tduph+$3;
		tnduph=tnduph+$4;
		tdd=tdd+$5;
		tr=tr+$6;
		tckh=tckh+$7;
		tcks=tcks+$8;
		tdupblk=tdupblk+$9;
		tfsz=tfsz+$10;
	} else if (NF==1) {
		t = $1;
	}
}
END{
	total_interact_data=ts+tr;
	compression_ratio=100.0*(tfsz-total_interact_data)/tfsz;
	printf("time elapsed -------- : %u\n",t);
	printf("total_sent ---------- : %u\n",ts);
	printf("total_req ----------- : %u\n",treq);
	printf("total_dup_h --------- : %u\n",tduph);
	printf("total_ndup_h -------- : %u\n",tnduph);
	printf("total_delta_data ---- : %u\n",tdd);
	printf("total_recved -------- : %u\n",tr);
	printf("total_checksum_h ---- : %u\n",tckh);
	printf("total_checksums ----- : %u\n",tcks);
	printf("total_interact_data - : %u\n",total_interact_data);
	printf("total_dup_blks ------ : %u\n",tdupblk);
	printf("total_filesz -------- : %u\n",tfsz);
	printf("compression_ratio --- : %f%\n",compression_ratio);
}

