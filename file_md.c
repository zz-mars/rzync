#include "rzync.h"
#include "md5.h"

int main(int argc, char *argv[])
{
	if(argc != 2) {
		fprintf(stderr,"invalid argument\n");
		return 1;
	}
	char *filename = argv[1];
	char md5[RZYNC_MD5_CHECK_SUM_BITS];
	if(md5s_of_file(filename,md5) != 0) {
		fprintf(stderr,"file md5 error\n");
		return 1;
	}
	printf("%s\n",md5);
	return 0;
}
