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
#include <math.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <asm-generic/termbits.h>
	
#include "usdg_udp.h"

#define PAGE_SIZE 4096
#define STKSIZE (10 * PAGE_SIZE)
/*
 * Golable variance
 * assume only `set_param`  can modify it
*/
char *port;	
char *path;
int bRate = DFT_BAUDRATE;
unsigned long int echar;
int pack_size;
int itv;
/*
 * The Average algorithm : 
 * avg(n) = avg(n-1)+[X-avg(n-1)/n] 
 */
static inline float 
_avg_count(float now_itv, int round)
{
	static float avg_itv = 0.0;
	float diff;

	diff = (now_itv-avg_itv) / (float)(round-2);
	avg_itv += diff;

	return avg_itv;
}
	
int itv_check(struct timeval *wtime, 
				struct timeval *rtime, 
				struct udp_pack_res *res)
{
	struct timeval tres;
	float tmp;
	float tmp_itv;
	float set_itv;
	float now_itv;

	set_itv =  itv/1000.0;
	timersub(rtime, wtime, &tres);
	tmp = (tres.tv_sec*1000000 + tres.tv_usec)/1000000.0;

	if(tmp < 0.00001) return -1;
	tmp_itv = fabs(set_itv-tmp);

	if(tmp_itv > 3.0){
		printf("\n** Critical time interval(wrtime-rtime) %.3f sec **\n", tmp);	
		return -1;
	}
	
	now_itv = tmp_itv;
	if(now_itv > res->max_itv)
		res->max_itv = now_itv;

	res->avg_itv = _avg_count(now_itv, res->round);

	return 0;
}

static void data_check
(unsigned char *rx, int len, struct udp_pack_res *res)
{
	for(int i = 0; i < (len==1?DATALEN-1:len-1); ++i){
		if(abs(*(rx+i+1) - *(rx+i)) != 1){
			// in echar, 0<->0xff is legal
			if(!echar && 
			*(rx+i+1) != 0)
			{
				res->err++;
				printf("< %x , %x>(%d) ",
						*(rx+i), *(rx+i+1), i);
			}
		}
	}
	if(echar){
		if(*(rx+len-1) != echar){
			printf("\n(%x != %lx)\n", *(rx+DATALEN), echar);
			res->ec_err++;
		}
	}	
}

static void
pack_by_size_data_compare
(struct udp_pack_res *res, unsigned char *buf, int len)
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
			res->err++;
			printf("\nError  <%x | %x>\n", 
					*(buf+i), *(buf+i+1));
		}
	}
}

static inline unsigned char 
_cir_bit_shift(unsigned char *a, int offset)
{
	return (*a<<offset | *a>>((int)sizeof(unsigned char)-offset));
}

static inline void _init_tx_data(unsigned char *tx, int len)
{
	int j = 0, k = 1;
	for(int i = 0; i < len; ++i, j+=k){
		if(i%255 == 0 && i != 0)
			k *= -1;
		*(tx+i) = j;
	}
}

static inline void usage(void)
{
	printf("Usage : ./udp_pack [-p/-b/-i/-c/-e/-s/-h] [argunment]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -c      Com port path\n");
	printf("    -b      Set baud rate(default = " XSTR(DFT_BAUDRATE) ")\n");	
	printf("    -p      Peer recving data port\n");
	printf("    -i      Interval time (usec)\n");
	printf("    -e      End-character (hex) ** CANNOT CHOICE 0 **\n");
	printf("    -s      pack by size (byte)\n");
	printf("    -h      For help\n"); 
}

static inline void disp_result(struct udp_pack_res *res)
{
	printf("< %s> "
			XSTR(send)" %d byte, "
			XSTR(recv)" %d byte, "
			XSTR(round)" %d, "
			XSTR(err)" %d",
			res->title,
			res->wlen,
			res->rlen,
			res->round,
			res->err
			);
	if(itv){
		printf(", "XSTR(Max itv_dva)" %.3lf sec, "
					XSTR(Avg itv_dva)" %.5lf sec",
					res->max_itv,
					res->avg_itv
				);
	}
	if(echar){
		printf(", end-char err %d",
					res->ec_err);
	}		
	printf(".     \r");
	fflush(stdout); 
}

static inline void 
disp_pack_size_result(struct udp_pack_res *res, int wlen)
{
	printf("< Pack By Size > "
			XSTR(round)" %d, "
			XSTR(write)" %d byte, "
			XSTR(size_err)" %d, "
			XSTR(err)" %d,"
			XSTR(      .)"\r",
			res->round, 
			wlen,
			res->size_err,
			res->err
			);
	fflush(stdout);
}

void set_param(int argc, char **argv)
{
	int c;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"comport", required_argument, NULL, 'c'},
		{"port", required_argument, NULL, 'p'},
		{"iterval", required_argument, NULL, 'i'},
		{"endchar", required_argument, NULL, 'e'},
		{"size", required_argument, NULL, 's'},
		{"baudrate", required_argument, NULL, 'b'},
	};
	
	while((c = getopt_long(argc, argv, "hp:i:c:s:e:b:", 
								long_opt, NULL)) != -1)
	{
		switch(c){
			case 'h':
				usage();
				exit(0);
			case 'p':
				port = optarg;
				break;
			case 'i':
				itv = atoi(optarg);
				break;
			case 'e':
				sscanf(optarg, "%lx", &echar);
				if(echar == 0){
					printf("End-character CANNOT choice 0 !!\n");
					exit(0);
				}
				break;
			case 's':
				pack_size = atoi(optarg);
				break;
			case 'c':
				path = optarg;
				break;
			case 'b':
				bRate = atoi(optarg);
				break;
			default:
				usage();
				exit(0);
		}
	}
	
	if(port == NULL ||
		path == NULL)
	{
		usage();
		exit(0);
	}
	printf("<Setting>\n");
	printf("ComPort = %s, peer recving data port = %s\n", path, port);
	if(itv)
		printf("time interval %.3f sec\n", (float)(itv/1000.0));
	if(echar)
		printf("end-character %lx (hex)\n", echar);
	if(pack_size)
		printf("data pack size %d byte\n", pack_size);
}

int open_serial(void)
{
	int fd;
	int ret;
	struct termios2 newtio;

	// open com port
	fd = open(path, O_RDWR);
	if(fd == -1){
		perror("Open serial: ");
		return -1;
	}

	ret = ioctl(fd, TCGETS2, &newtio);
	if(ret < 0){
		perror("Get serial ioctl: ");
		return -1;
	}
	newtio.c_iflag &= ~(ISTRIP|IUCLC|IGNCR|ICRNL|INLCR|ICANON|IXON|IXOFF|IXANY|PARMRK);
	newtio.c_iflag |= (IGNBRK|IGNPAR);
	newtio.c_lflag &= ~(ECHO|ICANON|ISIG);
	newtio.c_cflag &= ~CBAUD;
	newtio.c_cflag |= BOTHER;
	newtio.c_ospeed = bRate;
	newtio.c_ispeed = bRate;
	ret = ioctl(fd, TCSETS2, &newtio);
	if(ret < 0){
		perror("Set serial ioctl: ");
		return -1;
	}
	printf("Open %s SUCCESS, baud-rate output %d, input %d\n",
					path, newtio.c_ospeed, newtio.c_ispeed);
	return fd;
}

int create_udp_rx_sock(void)
{
	int fd;
	int ret;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *p;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	
	ret = getaddrinfo(NULL, port, &hints, &result);
	if(ret != 0){
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}
	
	int yes = 1;
	for(p = result; p != NULL; p = p->ai_next){
		fd = socket(p->ai_family, p->ai_socktype,
								 p->ai_protocol);
		if(fd == -1){
			perror("udp_rx_socket: ");
			continue;
		}
		
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
							&yes, sizeof(yes)) == -1)
		{
			perror("udp_rx setsockopt: ");
			close(fd);
			return -1;
		}
	
		if(bind(fd, p->ai_addr, p->ai_addrlen) == -1){
			close(fd);
			perror("udp_rx bind: ");
			continue;
		}
		break;
	}
	
	if(p == NULL){
		printf("%s failed\n", __func__);
		close(fd);
		return -1;
	}
	freeaddrinfo(result);
	printf("%s success, skfd = %d\n", __func__, fd);	

	return fd;
}

int data_flush(int fd, 
				struct sockaddr_storage *addr,
				socklen_t *addrlen)
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
			printf("%s: select failed!(%s)\n",
						__func__, strerror(errno));
			break;
		}
	
		if(ret == 0) break;
		
		tmp = recvfrom(fd, rxtmp, sizeof(rxtmp), 0,
							(struct sockaddr *)addr,
							addrlen);
		rlen += tmp;
	}while(1);
	
	return rlen;
}

int _udp_recv_all(int fd, unsigned char *rx, int len,
				struct sockaddr_storage *their_addr,
				socklen_t *addrlen,
				struct udp_pack_res *res)
{
	int ret, tmp = 0;

	if(len < 1) return -1;	

	alarm(RECVDATATIMEOUT);
	signal(SIGALRM, alarm_handle);

	do{
		ret = recvfrom(fd, rx+tmp, len-tmp, 0,
					(struct sockaddr *)their_addr,
					addrlen);
		if(ret < 0){
			printf("%s: recv failed(%s)\n",
					__func__, strerror(errno));
			return -1;
		}
		if(timeout){
			printf("%s timeout!\n", __func__);
			timeout = 0;
			return -1;
		}
		tmp += ret;
	}while(tmp < len);
	res->rlen += tmp;	

	return 1;
}

int _serial_write_all(int fd, 
					unsigned char *tx, 
					int len, 
					struct udp_pack_res *res)
{
	int ret, tmp = 0;
	
	do{
		ret = write(fd, tx+tmp, len-tmp);
		if(ret < 0){
			printf("%s: sendto failed(%s)\n", 
					__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
	}while(tmp < len);
	res->wlen += tmp;	

	return 1;
}

int packing_size_test(struct udp_pack *usdg)
{
	int ret, tmp;
	int wlen = 0;
	int rlen = 0;
	unsigned char txbuf = 0x01;
	unsigned char rxbuf[ONE_K_BYTE];
	struct udp_pack_res *res = &usdg->res;
	struct sockaddr_storage their_addr;	//udp rx
	socklen_t addrlen = sizeof(their_addr); //udp rx
	int ser_fd = usdg->serial_fd;
	int skfd = usdg->skfd;
	int maxfd = MAX(ser_fd, skfd);
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	
	printf("%s: flushing %d byte data!\n",
						__func__,
					data_flush(skfd, &their_addr, &addrlen));
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(ser_fd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed!(%s)\n", 
						__func__, strerror(errno));
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(ser_fd, &wfds) && rlen == 0){
			txbuf = _cir_bit_shift(&txbuf, 1);
			tmp = write(ser_fd, &txbuf, sizeof(txbuf));
			if(tmp < 0){
				printf("%s: send failed!(%s)\n",
						 __func__, strerror(errno));
				break;
			}
			wlen += tmp;
			disp_pack_size_result(res, wlen);
		}
		
		if(FD_ISSET(skfd, &rfds)){
			rlen = recvfrom(skfd, rxbuf, sizeof(rxbuf), 0,
						(struct sockaddr *)&their_addr, &addrlen); 
			if(rlen < 0){
				printf("%s: recv failed!(%s)\n", 
								__func__, strerror(errno));
				break;
			}
			res->round++;
			pack_by_size_data_compare(res, rxbuf, rlen);
			disp_pack_size_result(res, wlen);
			rlen = 0;
			wlen = 0;
		}
		usleep(100*1000);
	}while(1);
	
	return 0;
}

int packing_time_test(struct udp_pack *usdg)
{
	int wRes = 0;
	int rRes = 0;
	unsigned char rxbuf[ONE_K_BYTE];
	unsigned char txbuf[ONE_K_BYTE];
	struct udp_pack_res *res = &usdg->res;
	struct sockaddr_storage their_addr;	//udp rx
	socklen_t addrlen = sizeof(their_addr); //udp rx

	_init_tx_data(txbuf, sizeof(txbuf));

	int ret;	
	int ser_fd = usdg->serial_fd;
	int skfd = usdg->skfd;
	int maxfd = MAX(ser_fd, skfd);
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;	
	struct timeval wtime;
	struct timeval rtime;

	printf("%s: flushing %d byte data!\n",
						__func__,
					data_flush(skfd, &their_addr, &addrlen));

	timerclear(&wtime);
	timerclear(&rtime);
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(ser_fd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed (%s)\n", 
						__func__, strerror(errno));
			break;
		}

		if(ret == 0) continue;

		if(FD_ISSET(ser_fd, &wfds)){
			wRes = usdg->ser_tx(ser_fd, txbuf, sizeof(txbuf), res);
			if(wRes == -1) break;
		
			if(!timerisset(&wtime) && res->round > 2)
				gettimeofday(&wtime, NULL);
		}

		if(FD_ISSET(skfd, &rfds)){
			if(!timerisset(&rtime) && res->round > 2)
				gettimeofday(&rtime, NULL);	

			rRes = usdg->udp_rx(skfd, rxbuf, sizeof(rxbuf),
								&their_addr, &addrlen, res);
			if(rRes == -1){
				printf("%s: recv failed!\n", __func__);
				break;
			}
		}
	
		if(wRes && rRes){ 
			data_check(rxbuf, rRes, res);
			if(timerisset(&wtime) && 
				timerisset(&rtime) &&
				res->round > 2)
			{
				itv_check(&wtime, &rtime, res);
				timerclear(&wtime);
				timerclear(&rtime);
			}
			wRes = 0;
			rRes = 0;
			res->round++;
			disp_result(res);
		}
	}while(1);
	
	return 0;
}

int packing_char_test(struct udp_pack *usdg)
{
	int wRes = 0;
	int rRes = 0;
	unsigned char rxbuf[DATALEN];
	unsigned char txbuf[DATALEN];
	struct udp_pack_res *res = &usdg->res;
	struct sockaddr_storage their_addr;	//udp rx
	socklen_t addrlen = sizeof(their_addr); //udp rx

	_init_tx_data(txbuf, sizeof(txbuf));

	int ret;	
	int ser_fd = usdg->serial_fd;
	int skfd = usdg->skfd;
	int maxfd = MAX(ser_fd, skfd);
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;	
	
	printf("%s: flushing %d byte data!\n",
						__func__,
					data_flush(skfd, &their_addr, &addrlen));
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(ser_fd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed (%s)\n", 
						__func__, strerror(errno));
			break;
		}

		if(ret == 0) continue;

		if(FD_ISSET(ser_fd, &wfds)){
			wRes = usdg->ser_tx(ser_fd, txbuf, sizeof(txbuf), res);
			if(wRes == -1) break;
		}

		if(FD_ISSET(skfd, &rfds)){
			rRes = recvfrom(skfd, rxbuf, sizeof(rxbuf), 0,
					(struct sockaddr *)&their_addr, &addrlen);
			
			if(rRes == -1){
				printf("%s: recv failed!\n", __func__);
				break;
			}
			res->rlen += rRes;
		}
	
		if(wRes && rRes){ 
			data_check(rxbuf, rRes, res);
			wRes = 0;
			rRes = 0;
			res->round++;
			disp_result(res);
		}
	}while(1);
	
	return 0;
}

static int usdg_udp_pack_init(struct udp_pack **self)
{
	*self = (struct udp_pack *)malloc(sizeof(struct udp_pack));
	if(*self == NULL) return -1;

	(*self)->serial_fd = 0;
	(*self)->skfd = 0;
	(*self)->ser_tx = _serial_write_all;
	(*self)->udp_rx = _udp_recv_all;
	
	if(echar || pack_size || itv){
		if(echar)
			strcat((*self)->res.title, "End-char ");
		if(itv)
			strcat((*self)->res.title, "Time ");
	}else{
		sprintf((*self)->res.title, "Normal ");
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	struct udp_pack *usdg;

	if(argc < 5){
		usage();
		return -1;
	}

	set_param(argc, argv);
	
	if(usdg_udp_pack_init(&usdg) == -1){
		printf("Usdg packing test initial failed!\n");
		return -1;
	}

	do{
		usdg->serial_fd = open_serial();
		if(usdg->serial_fd == -1) break;
		
		usdg->skfd = create_udp_rx_sock();
		if(usdg->skfd == -1) break;
	
		if(pack_size)
			packing_size_test(usdg);
		else if(itv)
			packing_time_test(usdg);
		else
			packing_char_test(usdg);

	}while(0);

	close(usdg->skfd);
	close(usdg->serial_fd);
	free(usdg);
	
	return 0;
}






