#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>
#include <signal.h>
	
#include "usdg_tcp.h"

#define PAGE_SIZE 4096
#define STKSIZE (10 * PAGE_SIZE)

/*
 * Golable variance
 * assume no one can modify it
*/
char *addr;
char *port;
int itv;
int pack_size;
long unsigned int echar;
int multi;

static inline void usage(void)
{
	printf("Usage : ./tcp_svr [-a/-i/-p/-c/-h/-s/-m] [argunment]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -a      IP address\n");
	printf("    -p      Data listening port\n");
	printf("    -i      Interval time (usec)\n");
	printf("    -c      End-character (hex) ** CANNOT CHOICE 0 **\n");
	printf("    -s      pack by size (byte)\n");
	printf("    -m      Enable multi-thread\n");
	printf("    -h      For help\n"); 
}

static inline void disp_result(struct tcp_svr_res *res)
{
	printf("<Thread 1> round %d, conn %d, err %d"
					, res->round1, res->conn1, res->err1);
	if(itv){
		printf(", Max itv_dva %.3lf sec, ", res->max_itv);
		printf("Avg itv_dva %.5lf sec", res->avg_itv);
	}
			
	if(echar)
		printf(", end-char err %d", res->ec_err);

	if(multi){
		printf(" | <Thread 2> round %d, conn %d, err %d"
					, res->round2, res->conn2, res->err2);
	}
	printf(".     \r");
	fflush(stdout); 
}

static inline void 
disp_pack_size_result(struct tcp_svr_res *res, int wlen)
{
	printf("<Pack By Size> round %d, wlen %d, size_err %d err %d",
			res->round1, wlen, res->size_err, res->err1);
	printf("          .\r");
	fflush(stdout);
}

static void data_check(struct tcp_svr_res *res,
						unsigned char *rx,
						int len, 
						int thread)
{
	if(echar && thread == 1){
		if(*(rx+(len-1)) != echar){	
			printf("\n# end-char error #(%x != %lx)\n", 
					*(rx-(len-1)), echar);
			res->ec_err++;
		}
	}

	if(thread == 1)
		res->err1 += compare_data(rx, len, res->err1);
	else
		res->err2 += compare_data(rx, len, res->err2);
}

int data_flush(int fd)
{
	int tmp = 0;
	int rlen = 0;
	int ret;
	unsigned char rxtmp[2048];
	fd_set rfds;
	struct timeval tv;
	
	do{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(fd, &rfds, NULL, NULL, &tv);
	
		if(ret < 0){
			printf("%s: select failed!\n", 
									__func__);
			break;
		}
		
		if(ret == 0) break;
	
		tmp = read(fd, rxtmp, sizeof(rxtmp));
		
		rlen += tmp;
	}while(1);
	
	return rlen;
}	

int _sk_create_connect(void)
{
	int skfd;
	int ret;
	struct addrinfo hints;
	struct addrinfo *res;
	struct addrinfo *p;
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	
	ret = getaddrinfo(addr, port, &hints, &res);
	if(ret != 0){
		printf("getaddrinfo fail : %s\n", 
							gai_strerror(ret));
		return -1;
	}	
	
	for(p = res; p != NULL; p = p->ai_next){
		skfd = socket(p->ai_family, 
						p->ai_socktype, 
						p->ai_protocol);
		if(skfd == -1) continue;
	
		ret = connect(skfd, p->ai_addr, p->ai_addrlen);
		if(ret == -1){
			close(skfd);
			continue;
		}
		break;
	}
	if(p == NULL){
		printf("Failed to connect !\n");
		return -1;
	}
	freeaddrinfo(res);
	
	return skfd;
}

void _clt_close_connect(struct tcp_svr_res *res, 
						int fd, 
						int thrd)
{
	close(fd);
	if(thrd == FIRST){
		res->conn1--;
		res->round1++;
	}else if(thrd == SECOND){
		res->conn2--;
		disp_result(res);
		usleep(150*1000);
	}
}
/*
 * The Average algorithm : 
 * avg(n) = avg(n-1)+[X-avg(n-1)/n] 
 */
float _avg_count(float now_itv, int round)
{
	static float avg_itv = 0.0;	
	float dif;

	dif = (now_itv-avg_itv) / (float)(round-1);
	avg_itv += dif;

	return avg_itv;
}
	
int pkdata_itv_chk(struct timeval *wtime, 
					struct timeval *rtime, 
					struct tcp_svr_res *res)
{
	struct timeval tres;
	float tmp;
	float tmp_itv;
	float set_itv;
	float now_itv;
		
	set_itv =  itv/1000.0;
	timersub(rtime, wtime, &tres);
	tmp = (tres.tv_sec*1000000 + tres.tv_usec) / 1000000.0;
	
	if(tmp < 0.00001)
        return -1;
	tmp_itv = fabs(set_itv-tmp);

	if(tmp_itv > 3.0){
		printf("\n** Critical time interval(wrtime-rtime) %.3f sec **\n", 
					tmp);	
		return -1;
	}
	now_itv = tmp_itv;
	if(now_itv > res->max_itv)
		res->max_itv = now_itv;
	
	res->avg_itv = _avg_count(now_itv, res->round1);

	return 0;
}

int _recv_all(int fd, unsigned char *rx, int len)
{
	int tmp;
	int rlen = 0;
	
	alarm(RECVDATATIMEOUT);
	signal(SIGALRM, alarm_handle);

	while(rlen < len){
		tmp = recv(fd, rx+rlen, len-rlen, 0);
		if(tmp < 0){
			printf("%s: recv failed!(%s)\n",
					__func__, strerror(errno));
			return -1;
		}
		rlen += tmp;

		if(timeout){
			timeout = 0;
			return -1;
		}
	}
	
	return 1;
}

int _send_all(int fd, unsigned char *tx, int len)
{
	int wlen = 0;
	int tmp;
	
	while(wlen < len){
		tmp = send(fd, tx+wlen, len-wlen, 0);
		if(tmp < 0){
			printf("%s: send failed (%s)\n",
					__func__, strerror(errno));
			return -1;
		}
		wlen += tmp;
	}
	
	return 1;
}

static void
pack_by_size_data_compare(struct tcp_svr_res *res, 
							unsigned char *buf, 
							int len)
{
	int ctz1, ctz2;
	
	if(pack_size != len){
		printf("\nreadlen(%d) != packlen(%d)\n", 
					len, pack_size);
		res->size_err++;
	}

	for(int i = 0; i < len-1; ++i){
		/* 
		 * Count Trailing Zero
		 * return the first '1' position in one byte,
		 * built in gcc
		*/
		ctz2 = __builtin_ctz(*(buf+i+1));
		ctz1 = __builtin_ctz(*(buf+i));
		/*
		 * circular bit shift check,
		 * 10000000 next shift is 00000001
		 * so if ctz1 == 7, ctz2 must be 0
		*/
		if((ctz2 - ctz1 != 1) && ctz2 != 0){
			res->err1++;
			printf("\nError  <%x | %x>\n", *(buf+i), *(buf+i+1));
		}
	}
}
/*
 *	four port connection with open-colse
 */
void *muti_connect_thread(void *data)
{
	int ret;
	int rRes = 0;
	int skfd[CONNECTNUM];
	int lock = 0;
	unsigned char rxbuf[DATALEN];
	struct tcp_svr *usdg = (struct tcp_svr *)data;
	struct tcp_svr_res *res = &usdg->res;

	printf("create Multi Connect Thread success, ID = %lu\n", 
								pthread_self());
	fd_set rfds;
	struct timeval tv;
	do{
		if(!lock){
			for(int i = 0; i < CONNECTNUM; i++){
				skfd[i] = usdg->open();
				res->conn2++;
				disp_result(res);
				usleep(150*1000);
			}
			lock = 1;
		}
	
		for(int i = 0; i < CONNECTNUM; i++){
			tv.tv_sec = 3;
			tv.tv_usec = 0;
			FD_ZERO(&rfds);
			FD_SET(skfd[i], &rfds);	
			ret = select(skfd[i]+1, &rfds, 0, 0, &tv);
			if(ret < 0){
				printf("%s(%d): select failed!\n", __func__, i);
				break;
			}

			if(ret == 0) continue;
			
			if(FD_ISSET(skfd[i], &rfds) && lock){
				rRes = usdg->rx(skfd[i], rxbuf, DATALEN);
				if(rRes == -1){
					printf("\n<Thread 2> RECV data TIMEOUT (%d)...\n", i);
					break;
				}
				if(res->round2)// dont compare data in first round	
					data_check(res, rxbuf, DATALEN, 2);
			}	
		}
		
		if(rRes && lock){
			for(int i = 0; i < CONNECTNUM; i++)
				usdg->close(res, skfd[i], SECOND);
			lock = 0;
			res->round2++;
		}
	}while(1);
	pthread_exit(NULL);
}

int one_thread_with_pack_size_test(struct tcp_svr *usdg)
{
	int ret, tmp;
	int wlen = 0;
	int rlen = 0;
	unsigned char txbuf = 0x01;
	unsigned char rxbuf[ONE_K_BYTE];
	struct tcp_svr_res *res = &usdg->res;	

	int skfd = usdg->open();
	if(skfd == -1) return -1;
	
	int flush = data_flush(skfd);
	printf("Flushing %d byte data\n", flush);

	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(skfd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(skfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed!(%s)\n", 
					__func__, strerror(errno));
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(skfd, &wfds) && rlen == 0){
			/* circular bit shift */
			txbuf = (txbuf<<1) | (txbuf>>(8-1));
			tmp = send(skfd, &txbuf, sizeof(txbuf), 0);
			if(tmp != sizeof(txbuf)){
				printf("%s: send failed!(%s)\n",
						 __func__, strerror(errno));
				break;
			}
			wlen += tmp;
			disp_pack_size_result(res, wlen);
		}
		
		if(FD_ISSET(skfd, &rfds)){
			rlen = recv(skfd, rxbuf, sizeof(rxbuf), 0);
			if(rlen <= 0){
				printf("%s: recv failed!\n", __func__);
				break;
			}
			res->round1++;
			pack_by_size_data_compare(res, rxbuf, rlen);
			disp_pack_size_result(res, wlen);
			rlen = 0;
			wlen = 0;
		}
		usleep(200*1000);
	}while(1);
	
	close(skfd);
	return 0;
}

int one_thread_with_pack_time_test(struct tcp_svr *usdg)
{
	int ret;
	int wRes = 0;
	int rRes = 0;
	unsigned char rxbuf[DATALEN];
	unsigned char txbuf[DATALEN];
	struct tcp_svr_res *res = &usdg->res;
	
	for(int i = 0; i < DATALEN; ++i)
		txbuf[i] = i;

	int skfd = usdg->open();
	if(skfd == -1) return -1;
	printf("%s: flush %d byte\n", 
			__func__, data_flush(skfd));
	res->conn1++;

	fd_set rfds;
	fd_set wfds;
	struct timeval tv;	
	struct timeval wtime;
	struct timeval rtime;
	timerclear(&wtime);
	timerclear(&rtime);
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(skfd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(skfd+1, &rfds, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}

		if(ret == 0) continue;	
	
		if(FD_ISSET(skfd, &wfds) && !wRes){	
			wRes = usdg->tx(skfd, txbuf, DATALEN);
			if(wRes == -1) break;
			
			if(res->round1 > 1)
				gettimeofday(&wtime, NULL);
		}

		if(FD_ISSET(skfd, &rfds) && !rRes){
			if(res->round1 > 1)
				gettimeofday(&rtime, NULL);	
				
			rRes = usdg->rx(skfd, rxbuf, DATALEN);
			if(rRes == -1){
				printf("%s: recv failed(%s)\n",
							__func__, strerror(errno));
				break;
			}
		}
	
		if(rRes && wRes){
			if(res->round1 > 1){		// don't compare in first round
				data_check(res, rxbuf, DATALEN, 1);
				pkdata_itv_chk(&wtime, &rtime, res);
			}
			rRes = 0;
			wRes = 0;
			disp_result(res);
			timerclear(&wtime);
			timerclear(&rtime);
			usleep(200*1000);
			res->round1++;
		}
	}while(1);
	
	close(skfd);

	return 0;
}

int one_thread_with_pack_char_test(struct tcp_svr *usdg)
{
	int ret;
	int wRes = 0;
	int rlen = 0;
	unsigned char rxbuf[DATALEN];
	unsigned char txbuf[DATALEN];
	struct tcp_svr_res *res = &usdg->res;
	
	for(int i = 0; i < DATALEN; ++i)
		txbuf[i] = i;

	int skfd = usdg->open();
	if(skfd == -1) return -1;
	printf("%s: flush %d byte\n",
			__func__, data_flush(skfd));
	res->conn1++;

	fd_set rfds;
	fd_set wfds;
	struct timeval tv;	
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(skfd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(skfd+1, &rfds, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}

		if(ret == 0) continue;	
	
		if(FD_ISSET(skfd, &wfds) && !wRes){	
			wRes = usdg->tx(skfd, txbuf, DATALEN);
			if(wRes == -1) break;
		}

		if(FD_ISSET(skfd, &rfds) && !rlen){
			rlen = recv(skfd, rxbuf, sizeof(rxbuf), 0);
			if(rlen < 0){
				printf("%s: recv failed(%s)\n",
							__func__, strerror(errno));
				break;
			}
		}

		if(rlen && wRes){
			data_check(res, rxbuf, rlen, 1);
			disp_result(res);
			res->round1++;
		}
	}while(1);
	
	res->conn1--;
	close(skfd);

	return 0;
}

int one_thread_with_open_close(struct tcp_svr *usdg)
{
	int skfd, ret;
	int wRes = 0;
	int rRes = 0;
	int lock = 0;
	unsigned char rxbuf[DATALEN];
	unsigned char txbuf[DATALEN];
	struct tcp_svr_res *res = &usdg->res;
	
	for(int i = 0; i < DATALEN; ++i)
		txbuf[i] = i;

	fd_set rfds;
	fd_set wfds;
	struct timeval tv;	
	do{
		if(!lock){
			skfd = usdg->open();	
			if(skfd == -1) break;
			res->conn1++;
			lock = 1;
			disp_result(res);
		}
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(skfd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(skfd+1, &rfds, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}

		if(ret == 0) continue;	
	
		if(FD_ISSET(skfd, &wfds) && !wRes){	
			wRes = usdg->tx(skfd, txbuf, DATALEN);
			if(wRes == -1) break;
		}

		if(FD_ISSET(skfd, &rfds) && !rRes){
			rRes = usdg->rx(skfd, rxbuf, DATALEN);
			if(rRes == -1){
				printf("%s: recv failed(%s)\n",
							__func__, strerror(errno));
				break;
			}
		}
	
		if(rRes && wRes && lock){
			usdg->close(res, skfd, FIRST);
			if(res->round1 > 1){		// don't compare in first round
				data_check(res, rxbuf, DATALEN, 1);
			}
			rRes = 0;
			wRes = 0;
			lock = 0;
			disp_result(res);
			usleep(200*1000);
		}
	}while(1);
	
	close(skfd);
	return 0;
}
/*
 *	one connection with open-close
 */
void *connet_thread(void *data)
{
	struct tcp_svr *usdg = (struct tcp_svr *)data;
	printf("create Single Connect Thread success, thread ID = %lu\n", pthread_self());
	
	if(pack_size)
		one_thread_with_pack_size_test(usdg);
	else if(echar)
		one_thread_with_pack_char_test(usdg);
	else if(itv)
		one_thread_with_pack_time_test(usdg);
	else
		one_thread_with_open_close(usdg);
	
	pthread_exit(NULL);
}

void set_param(int argc, char **argv)
{
	int c;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"address", required_argument, NULL, 'a'},
		{"port", required_argument, NULL, 'p'},
		{"iterval", required_argument, NULL, 'i'},
		{"char", required_argument, NULL, 'c'},
		{"size", required_argument, NULL, 's'},
		{"multi-pthread", no_argument, NULL, 'm'}
	};
	
	while((c = getopt_long(argc, argv, "ha:p:i:c:s:m", 
									long_opt, NULL)) != -1)
	{
		switch(c){
			case 'h':
				usage();
				exit(0);
			case 'a':
				addr = optarg;
				break;
			case 'p':
				port = optarg;
				break;
			case 'i':
				itv = atoi(optarg);
				break;
			case 'c':
				sscanf(optarg, "%lx", &echar);
				if(echar == 0){
					printf("End-character CANNOT choice 0 !!\n");
					exit(0);
				}
				break;
			case 's':
				pack_size = atoi(optarg);
				break;
			case 'm':
				multi = 1;
				break;
			default:
				usage();
				exit(0);
		}
	}
	
	if(addr == NULL || port == NULL){
		usage();
		exit(0);
	}
	
	printf("Connect to %s, PORT = %s\n", addr, port);
	if(itv)
		printf("Set time interval %.3f sec\n", (float)(itv/1000.0));
	if(echar)
		printf("Set end-character %lx (hex)\n", echar);
}

static int usdg_tcp_svr_init(struct tcp_svr **self)
{
	*self = (struct tcp_svr *)malloc(sizeof(struct tcp_svr));
	if(*self == NULL) return -1;

	(*self)->open = _sk_create_connect;
	(*self)->close = _clt_close_connect;
	(*self)->tx = _send_all;
	(*self)->rx = _recv_all;	
	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	struct tcp_svr *usdg;
	pthread_t tid[CLTTHREADNUM];
	pthread_attr_t attr;

	if(argc < 5){
		usage();
		exit(0);
	}
	
	set_param(argc, argv);
	
	if(usdg_tcp_svr_init(&usdg) == -1){
		printf("Usdg client test initial failed!\n");
		exit(0);
	}

	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, STKSIZE);
  	
	printf("Create thread 1 !\n");
	ret = pthread_create(&tid[0], &attr, connet_thread, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "pthread_create 1");
	if(multi && !pack_size){
		printf("Create thread 2 !\n");
		ret = pthread_create(&tid[1], &attr, muti_connect_thread, (void *)usdg);
		if(ret != 0)
			handle_error_en(ret, "pthread_create 2");
	}
	pthread_join(tid[0], NULL);	
	if(multi && !pack_size)
		pthread_join(tid[1], NULL);
out:
	free(usdg);

	return 0;
}






