#ifndef USDG_INFO_H
#define USDG_INFO_H

#define RECVDATATIMEOUT	15

#define FIRST			1
#define SECOND			2

#define DFT_BAUDRATE 9600

#define DATALEN 		256
#define ONE_K_BYTE		1024
#define CONNECTNUM		4
#define CLTTHREADNUM   	2

#define BACKLOG			10
#define SERDATALEN		5
#define TCPDATALEN		10*ONE_K_BYTE
#define TCPCONN			16
#define SVRTHREADNUM	TCPCONN+2

#define handle_error_en(en, msg) \
			do{ errno = en; perror(msg); \
				goto out; }while(0)
#define TAKE_TURN(a) \
	do{ a == 15?a=0:a++; }while(0)
#define XSTR(s) STR(s)
#define STR(s) #s

int timeout = 0;		// the recv data clock alarm

struct tcp_svr_res{
	unsigned int round1;
	int conn1;
	int err1;
	unsigned int round2;
	int conn2;
	int err2;
	float max_itv;		// display max interval deviation
	float avg_itv;		// display averge interval deviation
	int ec_err;			// display end-char err
	int size_err; 
};
	
struct tcp_svr{
	struct tcp_svr_res res;
	unsigned char txbuf[DATALEN];
	int (*open) (void);
	void (*close) (struct tcp_svr_res *, int, int);
	int (*tx) (int, unsigned char *, int);
	int (*rx) (int, unsigned char *, int);
};

struct tcp_clt_new_res{
	int host;
	int ser_rbyte;
	int ser_wbyte;
	int ser_err;
	int tcp_rbyte;
	int tcp_wbyte;
	int tcp_err;
};
	
struct tcp_clt_new{
	struct tcp_clt_new_res res;
	int ser_fd;
	int svr_fd;
	int ctrl;
	int conn;
	unsigned char tcptxbuff[TCPDATALEN];
	int (*tcp_tx) (int, unsigned char *, 
					int, struct tcp_clt_new_res *);
	pthread_mutex_t mutex;
	pthread_cond_t cond;
};

struct tcp_clt{
	unsigned char txbuf1[SERDATALEN];
	unsigned char txbuf2[TCPDATALEN];
	int ser_err;
	int ser_rto;
	int ser_rbyte;
	int tcp_err;
	int tcp_rfail;
	unsigned int round;
	int ser_fd;
	int svr_fd;
	int acp_fd[TCPCONN];
	int conn_num;
	int thrd_num;	
	int ctrl;				// To control which TCP connection thread to send data
	pthread_mutex_t ctrl_mutex;	// ctrl mutex lock
	pthread_mutex_t cnum_mutex;		// conn_num mutex lock
	pthread_cond_t cnum_cond;		// conn_num lock condiction
	pthread_cond_t ctrl_cond;		// the control send data TCP close
};

struct tcp_clt_opt{
	int (*create)(void);
	int (*accept)(int, int);
	int (*termios)(int);
	int (*read_all)(struct tcp_clt*, unsigned char*, int);
	void (*close)(struct tcp_clt*, int);
};	

static inline int compare_data
(unsigned char *rx, int len, int err)
{
	do{
		if(len < 2) break;
		
		for(int i = 0; i < len-1; i++){
			if(abs(*(rx+i+1) - *(rx+i)) != 1 && 
				*(rx+i) != len-1 &&
				*(rx+i+1) != 0)
			{
				err++;
				printf("\n <%x,%x> \n", 
							*(rx+i), *(rx+i+1));
			}
		}
	}while(0);
	
	return err;
}

static inline void alarm_handle(int sig)
{
	timeout = 1;	
}

#endif
