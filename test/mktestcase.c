#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#define MAGIC_S_LEN	64
static unsigned char magic_s[MAGIC_S_LEN] = "/* for test zzzzzzzzzzzzz */";

#define BUFSZ	BUFSIZ
static unsigned char buf[BUFSZ];

#define FILE_NAME_BUFSZ	1024
static unsigned char file_name_buf[FILE_NAME_BUFSZ];

void bubble_sort(unsigned long long *a,unsigned int n)
{
	unsigned int i,j;
	for(i=0;i<n;i++) {
		for(j=n-1;j>i;j--) {
			if(a[j]<a[j-1]) {
				unsigned long long tmp = a[j];
				a[j] = a[j-1];
				a[j-1] = tmp;
			}
		}
	}
}

/* Write magic_s to random positions for 'add_times'
 * Modify at 'modify_times' random positions */
int mktestcase(char *filename,char *src_dir,char *dst_dir,unsigned int add_times,unsigned int modify_times)
{
	assert(add_times > 0 && modify_times > 0);
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
	unsigned long long *add_pos = malloc(sizeof(unsigned long long)*(1+add_times));
	if(!add_pos) {
		perror("malloc for add_pos");
		goto close_tfd;
	}


	int i;
	for(i=0;i<add_times;i++) {
		srand(i+time(NULL));
		add_pos[i] = size*rand()/RAND_MAX;
	}
	bubble_sort(add_pos,add_times);

//	printf("add_poss : ");
//	for(i=0;i<add_times;i++) {
//		printf("%llu - ",add_pos[i]);
//	}
//	putchar('\n');

	add_pos[add_times++] = size;

	unsigned long long bytes_cpd = 0;
	for(i=0;i<add_times;i++) {
		while(bytes_cpd != add_pos[i]) {
			unsigned long long bytes_left = add_pos[i] - bytes_cpd;
			int to_read = bytes_left<BUFSZ?bytes_left:BUFSZ;
			memset(buf,0,BUFSZ);
			if(read(fd,buf,to_read) != to_read) {
				perror("read");
				goto free_add_pos;
			}
			if(write(tfd,buf,to_read) != to_read) {
				perror("write");
				goto free_add_pos;
			}
			bytes_cpd += to_read;
		}
		if(i < (add_times-1)) {
			if(write(tfd,magic_s,MAGIC_S_LEN) != MAGIC_S_LEN) {
				perror("write");
				goto free_add_pos;
			}
		}
	}
	
	if(fstat(tfd,&stt) != 0) {
		perror("fstat");
		goto free_add_pos;
	}

	size = stt.st_size;

	unsigned long long *modification_pos = malloc(sizeof(unsigned long long)*modify_times);
	if(!modification_pos) {
		goto free_add_pos;
	}

	size -= (MAGIC_S_LEN+1);
	for(i=0;i<modify_times;i++) {
		srand(i+10+time(NULL));
		modification_pos[i] = size*rand()/RAND_MAX;
	}

	for(i=0;i<modify_times;i++) {
		lseek(tfd,modification_pos[i],SEEK_SET);
//		printf("modify @%llu................\n",modification_pos[i]);
		if(write(tfd,magic_s,MAGIC_S_LEN) != MAGIC_S_LEN) {
			perror("write");
			goto free_modi_pos;
		}
	}

	ret = 0;
free_modi_pos:
	free(modification_pos);
free_add_pos:
	free(add_pos);
close_tfd:
	close(tfd);
close_fd:
	close(fd);
	return ret;
}

unsigned int zti(unsigned char* s)
{
	unsigned char* p = s;
	unsigned int r = 0;
	while(*p != '\0') {
		unsigned char ch = *p++;
		if(ch < '0' && ch > '9') {
			return 0;
		}
		r *= 10;
		r += (ch-'0');
	}
	return r;
}

int main(int argc, char *argv[])
{
	if(argc != 6) {
		fprintf(stderr,"Usage : ./mktestcase <filename> <src_dir> <dst_dir> <add_times> <modify_times>\n");
		exit(1);
	}

	char *filename = argv[1];
	char *src_dir = argv[2];
	char *dst_dir = argv[3];
	unsigned int add_times = zti(argv[4]);
	if(add_times == 0) {
		fprintf(stderr,"Invalid add_times!\n");
		return 1;
	}
	unsigned int modify_times = zti(argv[5]);
	if(modify_times == 0) {
		fprintf(stderr,"Invalid modify_times!\n");
		return 1;
	}

	if(mktestcase(filename,src_dir,dst_dir,add_times,modify_times)) {
		fprintf(stderr,"mktestcase fail @%s\n",filename);
		exit(1);
	}

	exit(0);
}

