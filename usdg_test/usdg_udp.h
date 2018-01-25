#ifndef USDG_UDP_H
#define USDG_UDP_H

#define FIRST 1
#define SECOND 2
#define UDPSVRTHREADMAX 16
#define SERIALTHREADNUM 2
#define DFT_UDP_RUN_THRD 4
#define DFT_BAUDRATE 9600
#define RECVDATATIMEOUT 30

#define FIVE_BYTE 5
#define ONE_K_BYTE 1024
#define TEN_K_BYTE ONE_K_BYTE*10

#define DATALEN 256
#define SERDATALEN TEN_K_BYTE
#define UDPDATALEN FIVE_BYTE

#define XSTR(s) STR(s)
#define STR(s) #s
#define MAX(a, b) (((a) >= (b))?(a):(b))
#define handle_error_en(en, msg) \
	do{ errno = en; perror(msg); \
		goto out; }while(0)

int timeout = 0;
static inline void alarm_handle(int sig)
{
	timeout = 1;
}
/* ----------------- usdg_udp_simp ------------------*/
struct udp_simp_res{
	int wlen;
	int rlen;
	int err;	//err byte
};

struct udp_simp{
	struct udp_simp_res res;
	pthread_cond_t cond;	// threads-sync
	pthread_mutex_t mutex;	// threads_sync
	void (*tx) (int, unsigned char *, 
				struct udp_simp_res *);
	void (*rx) (int, unsigned char *, 
				struct udp_simp_res *);
};
/*---------------------------------------------------*/
/*------------------ usdg_udp_multi -----------------*/
struct udp_multi_res{
	int ser_rbyte;
	long ser_wbyte;
	int ser_err;
	long udp_rbyte;
	int udp_wbyte;
	int udp_err;	
	int udp_send_order;
};

struct udp_multi{
	int order; // udp create order;
	int serial_fd;
	struct udp_multi_res res;
	pthread_cond_t cond;	// threads-sync
	pthread_mutex_t mutex;	// threads_sync
	int (*ser_tx) (int, unsigned char *, int, 
						struct udp_multi_res *);
	int (*udp_tx) (int, unsigned char *, int,
						struct addrinfo *p,
						struct udp_multi_res *);
	int (*udp_rx) (int, unsigned char *, int, 
						struct sockaddr_storage *,
						socklen_t *,
						struct udp_multi_res *);
};
/*---------------------------------------------------*/
/*------------------- usdg_udp_pack -----------------*/
struct udp_pack_res{
	int wlen;
	int rlen;
	int round;
	int err;
	float max_itv;
	float avg_itv;
	int ec_err;
	int size_err;
	char title[32];
};

struct udp_pack{
	struct udp_pack_res res;
	int serial_fd;
	int skfd;
	int (*ser_tx) (int, unsigned char *, int,
						struct udp_pack_res *);
	int (*udp_rx) (int, unsigned char *, int, 
						struct sockaddr_storage *,
						socklen_t *,
						struct udp_pack_res *);
};
/*---------------------------------------------------*/
/*------------------ usdg_multicast -----------------*/
struct multicast_res{
	int wlen;
	int rlen;
	int err;
};
	
struct multicast{
	struct multicast_res res;
	int (*ser_rx) (int, unsigned char *, int,
						struct multicast_res *);
	int (*multi_tx) (int, unsigned char *, int,
					struct addrinfo *,
					struct multicast_res *);
};
/*---------------------------------------------------*/
/*---------------- usdg_udp_open_close --------------*/
struct udp_oc_res{
	int wlen;
	int rlen;
	int err;
	int round;
};

struct udp_oc{
	struct udp_oc_res res;
};
#endif
