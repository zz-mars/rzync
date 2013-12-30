#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define MAGIC_S_LEN	64
static unsigned char magic_s[MAGIC_S_LEN] = "/* for test zzzzzzzzzzzzz */";

#define BUFSZ	BUFSIZ
static unsigned char buf[BUFSZ];

#define FILE_NAME_BUFSZ	1024
static unsigned char file_name_buf[FILE_NAME_BUFSZ];

/* 1) write MAGIC_BYTES to the start and end of given file
 * 2) modify file content at 2 random places 
 * 3) add content in one random place */
/* modify the files in src_dir,
 * put the modified files into dst_dir */
int mktestcase(char *filename,char *src_dir,char *dst_dir)
{
	int ret = 1;
	if(access(dst_dir,F_OK) != 0) {
		if(errno != ENOENT) {
			perror("access test case dir");
			return 1;
		}
		if(mkdir(dst_dir,0770) != 0) {
			perror("mkdir");
			return 1;
		}
	}

	memset(file_name_buf,0,FILE_NAME_BUFSZ);
	snprintf(file_name_buf,FILE_NAME_BUFSZ,"%s/%s",src_dir,filename);
	int fd = open(file_name_buf,O_RDONLY);
	if(fd < 0) {
		perror("open fd");
		return 1;
	}

	struct stat stt;
	if(fstat(fd,&stt) != 0) {
		perror("stat");
		return 1;
	}
	unsigned long long size = stt.st_size;

	memset(file_name_buf,0,FILE_NAME_BUFSZ);
	snprintf(file_name_buf,FILE_NAME_BUFSZ,"%s/%s",dst_dir,filename);
	int tfd = open(file_name_buf,O_CREAT | O_TRUNC | O_WRONLY,0660);
	if(tfd < 0) {
		perror("open tfd");
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
		if(write(tfd,magic_s,MAGIC_S_LEN) != MAGIC_S_LEN) {
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
		if(write(tfd,magic_s,MAGIC_S_LEN) != MAGIC_S_LEN) {
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
	if(argc != 4) {
		fprintf(stderr,"invalid argument!\n");
		exit(1);
	}

	char *filename = argv[1];
	char *src_dir = argv[2];
	char *dst_dir = argv[3];
	if(mktestcase(filename,src_dir,dst_dir)) {
		fprintf(stderr,"mktestcase fail @%s\n",filename);
		exit(1);
	}

	exit(0);
}

