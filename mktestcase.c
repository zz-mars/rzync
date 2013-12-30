#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define TEST_CASE_DIR	"Test_case.dir"
static unsigned char *magic_s = "/* for test zzzzzzzzzzzzz */";
static unsigned int magic_s_len;
#define BUFSZ	BUFSIZ
static unsigned char buf[BUFSZ];

/* 1) write MAGIC_BYTES to the start and end of given file
 * 2) modify file content at 2 random places 
 * 3) add content in one random place */

int mktestcase(char *filename)
{
	magic_s_len = strlen(magic_s);
	struct stat stt;
	if(stat(filename,&stt) != 0) {
		perror("stat");
		return 1;
	}
	unsigned long long size = stt.st_size;
	if(access(TEST_CASE_DIR,F_OK) != 0) {
		if(errno != ENOENT) {
			perror("access test case dir");
			return 1;
		}
		if(mkdir(TEST_CASE_DIR,0770) != 0) {
			perror("mkdir");
			return 1;
		}
	}
	unsigned char test_case_file[256];
	memset(test_case_file,0,256);
	snprintf(test_case_file,256,"%s/%s",TEST_CASE_DIR,filename);

	int fd = open(filename,O_RDONLY);
	if(fd < 0) {
		return 1;
	}

	int ret = 1;
	int tfd = open(test_case_file,O_CREAT | O_TRUNC | O_WRONLY,0660);
	if(tfd < 0) {
		goto close_fd;
	}

	/* add magic_s to add_pos */
	unsigned long long add_pos[4];
	add_pos[0] = 0;		// add to head
	add_pos[3] = size;	// add to tail
	int i;
	for(i=1;i<3;i++) {
		srand(i);
		add_pos[i] = size*rand()/RAND_MAX;
	}
	if(add_pos[1] > add_pos[2]) {
		unsigned long long tmp = add_pos[1];
		add_pos[1] = add_pos[2];
		add_pos[2] = tmp;
	//	add_pos[1] ^= add_pos[2];
	//	add_pos[2] ^= add_pos[1];
	//	add_pos[1] ^= add_pos[2];
	}
	for(i=0;i<4;i++) {
		printf("add_pos[%u] -- %llu\n",i,add_pos[i]);
	}

	unsigned long long bytes_cpd = 0;
	for(i=0;i<4;i++) {
		while(bytes_cpd != add_pos[i]) {
			unsigned long long bytes_left = add_pos[i] - bytes_cpd;
			int to_read = bytes_left<BUFSZ?bytes_left:BUFSZ;
			memset(buf,0,BUFSZ);
			if(read(fd,buf,to_read) != to_read) {
				perror("read");
				goto close_tfd;
			}
			if(write(tfd,buf,to_read) != to_read) {
				perror("write");
				goto close_tfd;
			}
			bytes_cpd += to_read;
		}
		printf("add magic_s to pos -- %llu\n",add_pos[i]);
		if(write(tfd,magic_s,magic_s_len) != magic_s_len) {
			perror("write");
			goto close_tfd;
		}
	}
	
	if(fstat(tfd,&stt) != 0) {
		perror("fstat");
		goto close_tfd;
	}

	size = stt.st_size;

	unsigned long modification_pos[3];
	for(i=0;i<3;i++) {
		srand(i+tfd);
		modification_pos[i] = size*rand()/RAND_MAX;
	}
	for(i=0;i<3;i++) {
		lseek(tfd,modification_pos[i],SEEK_SET);
		printf("modify @%llu................\n",modification_pos[i]);
		if(write(tfd,magic_s,magic_s_len) != magic_s_len) {
			perror("write");
			goto close_tfd;
		}
	}

	ret = 0;
close_tfd:
	close(tfd);
close_fd:
	close(fd);
	return ret;
}

int main(int argc, char *argv[])
{
	if(argc != 2) {
		fprintf(stderr,"invalid argument!\n");
		exit(1);
	}

	char *filename = argv[1];
	if(mktestcase(filename)) {
		fprintf(stderr,"mktestcase fail @%s\n",filename);
		exit(1);
	}

	exit(0);
}

