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
	}else{
		t=$1;
	}
}
END{
	total_interact_data=ts+tr;
	compression_ratio=100.0*(tfsz-total_interact_data)/tfsz;
	printf("%u\n",t);
	printf("%u\n",tfsz);
	printf("%u\n",total_interact_data);
	printf("%u\n",tcks);
	printf("%u\n",tduph);
	printf("%u\n",tnduph);
	printf("%u\n",tdd);
	printf("%f%\n",compression_ratio);
}

