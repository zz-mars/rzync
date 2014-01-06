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
	}
}
END{
	printf("%u %u %u %u %u %u %u %u %u %u\n",ts,treq,tduph,tnduph,tdd,tr,tckh,tcks,tdupblk,tfsz);
}

