void cal_md5(u8 * ptr,u32 len,u8 md5[])
{
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx,ptr,len);
	bzero(md5,MD5_SZ);
	MD5_Final(md5,&ctx);
	return;
}
u32 cal_rollin_cksm(u8 * const ptr,u32 * const aklz,u32 * const bklz,u32 blk_len)
{
	u64 r,akl = 0,bkl = 0;
	u32 p,i,skl;
	for(i = 0;i<blk_len;i++){
		p = 0;
		p = (u32)(*(ptr + i));
		akl += p;
		bkl += (blk_len - i)*p;
	}
	akl %= ROLLING_CHKSUM_M;
	bkl %= ROLLING_CHKSUM_M;
	if(aklz != NULL){
		*aklz = akl;
	}
	if(bklz != NULL){
		*bklz = bkl;
	}
	skl = akl + bkl*ROLLING_CHKSUM_M;
	return skl;
}

u32 cal_rollin_cksm_plus_1(u8 o,u8 n,u32 * const akl,u32 * const bkl,u32 blk_len)
{
	u32 skl;
	u32 new_akl = *akl;
	u32 new_bkl = *bkl;
	new_akl = (new_akl - o + n)%ROLLING_CHKSUM_M;
	new_bkl = (new_bkl - blk_len*o + new_akl)%ROLLING_CHKSUM_M;
	*akl = new_akl;
	*bkl = new_bkl;
	skl = new_akl + new_bkl*ROLLING_CHKSUM_M;
	return skl;
}

ssize_t Read(int fd,void * buf,size_t count)
{
	int n = 0;
	int i;
	u8 * p = buf;
	while(n != count){
		i = read(fd,buf+n,count-n);
		if(i < 0 && errno == EINTR)
			continue;
		n += i;
	}
	return n;
}

ssize_t Write(int fd,void * buf,size_t count)
{
	int n = 0;
	int i;
	u8 * p = buf;
	while(n != count){
		i = write(fd,buf+n,count-n);
		if(i < 0 && errno == EINTR)
			continue;
		n += i;
	}
	return n;
}
