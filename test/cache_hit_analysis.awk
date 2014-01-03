BEGIN {
	hits=0;
	miss=0;
}
{
	if ( NF == 3) {
		hits_ratio=100.0*$2/$3;
		if(hits_ratio>50.0) {
			hits=hits+1;
		}else{
			miss=miss+1;
		}
	}
}
END {
	total=hits+miss;
	printf("%f%\n",100.0*hits/total);
}
