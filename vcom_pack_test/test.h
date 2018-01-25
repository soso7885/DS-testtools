#ifndef TEST_H
#define TEST_H

#define RECVDATATIMEOUT	15

#define DATALEN 		256
#define CONNECTNUM		15
#define CLTTHREADNUM   	2

#define handle_error_en(en, msg) \
		do{ errno = en; perror(msg);\
		exit(EXIT_FAILURE); }while(0)

#ifdef SHOWDATA
#define print_data(buf, len, io)\
		do{show_data(buf, len, io);}while(0)
#define show_result(wlen, rlen, vcom)\
		do{}while(0)
#else
#define print_data(buf, len, io)\
		do{}while(0)
#define show_result(wlen, rlen, vcom)\
		do{disp_result(wlen, rlen, vcom);}while(0)
#endif

struct vcom_pack_test {
	int round;
	int err;
	int ecerr;
	int pack_size_err;
	double max_itv;
	double avg_itv;
	int wlen;
	int rlen;
};

struct vcom_md {
	int (*termios)(void);
	int (*test)(struct vcom_pack_test*);
};
	
static inline void 
show_data(unsigned char *buf, int len, char *io)
{
	printf("%s :", io);
	for(int i = 0; i < len; i++)
		printf(" %x |", *(buf+i));
	printf(" ** %d byte **\n", len);
	if(strcmp(io, "Read") == 0)
		printf("\n\n");
}
#endif
