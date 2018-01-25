#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <getopt.h>	//getopt_long
#include <netdb.h>
#include <sys/time.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm-generic/termbits.h>

#include "usdg_udp.h"
#define SERIAL 1
#define UDP 2
//#define DBG_ERR
/*
 * gloable variable
 * assume only `set_param` can modify it!
*/
char port[16][8];
char *path;
char *eki_data_listen_port;
char *eki_ip;
int bRate = DFT_BAUDRATE;
int thrd_num = DFT_UDP_RUN_THRD;

static inline void disp_res(struct udp_multi_res *res)
{
	printf("<Serial> "
			XSTR(rbyte)"(%d), " 
			XSTR(wbyte)"(%ld), "
			XSTR(err)"(%d)"
			" | <UDP> "
			XSTR(thread)"(%d), "
			XSTR(rbyte)"(%ld), "
			XSTR(wbyte)"(%d), "
			XSTR(err)"(%d)"
			"        ...\r",
			res->ser_rbyte, 
			res->ser_wbyte, 
			res->ser_err,
			res->udp_send_order, 
			res->udp_rbyte, 
			res->udp_wbyte, 
			res->udp_err
		);
	fflush(stdout);
}
	
static inline void _init_10K_txbuf(unsigned char *buf)
{
	*buf = 0x01;
	for(int i = 1; i <= SERDATALEN; ++i){
		// circular bit shift
		*(buf+i) = *(buf+i-1)<<1 | *(buf+i-1)>>(8-1);
	}
}

static inline int _ascII_data_check(unsigned char *buf, int len)
{
	int err = 0;
	
	for(int i = 0; i < len-1; ++i){
		if(*(buf+i+1)-*(buf+i) != 1){
			if(*(buf+i) != '}' && *(buf+i+1) != '!'){
#ifdef DBG_ERR
				printf("\nSerial recv Error: <%c | %c>\n", 
							*(buf+i), *(buf+i+1));
#endif
				err++;
			}
		}
	}
	
	return err;
}

static inline int _cir_bit_check(unsigned char *buf, int len)
{
	int err = 0;
	int ctz1, ctz2;
	
	for(int i = 0; i < len-1; ++i){
		/*
		 * Count Trailing Zero(CTZ)
		 * return first '1' position of byte
		 * built in gcc
		*/
		ctz1 = __builtin_ctz(*(buf+i));
		ctz2 = __builtin_ctz(*(buf+i+1));
		/*
		 * wrap!
		 * 10000000 next shift is 00000001
		 * so if ctz1 = 7, ctz2 must be 0
		*/
		if((ctz2 - ctz1 != 1) && ctz2 != 0){
#ifdef DEG_ERR
			printf("\nUDP recv Error: <%x | %x>\n",
						*(buf+i), *(buf+i+1));
#endif
			err++;
		}
	}
	
	return err;
}
	
static void data_check(unsigned char *buf, int len, int *err, int type)
{
	if(len < 2) return;

	if(type == UDP)
		*err += _cir_bit_check(buf, len);
	else if(type == SERIAL)
		*err += _ascII_data_check(buf, len);
	else{
		printf("\nFUCK! data check only support UDP or Serial\n");
		exit(0);
	}
}
	
static inline void usage(void)
{
	printf("Usage : ./udp_multi [-c/-p/-b/-n/-h/-l/-i] [argument]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -p      Peer recving data port (first port)\n");
	printf("    -c      Com port path\n");
	printf("    -b      Set baud rate(default = " XSTR(DFT_BAUDRATE) ")\n");
	printf("    -n      Number of UDP thread(default = " XSTR(DFT_UDP_RUN_THRD) ")\n");
	printf("    -l      EKI data listen port\n");
	printf("    -i      EKI IP address\n");
	printf("    -h      For help\n");				
}

static inline void _port_set(void)
{
	int tmp;
	
	for(int i = 1; i < thrd_num; ++i){
		tmp = atoi(port[i-1])+1;
		snprintf(port[i], sizeof(port[0]), "%d", tmp);
	}
}
		
int set_param(int argc, char **argv)
{
	int c;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"udp_port", required_argument, NULL, 'p'},
		{"com_port", required_argument, NULL, 'c'},
		{"eki_port", required_argument, NULL, 'l'},
		{"eki_ip", required_argument, NULL, 'i'},
		{"baud_rate", required_argument, NULL, 'b'},
		{"udp_num", required_argument, NULL, 'n'},
	};

	while((c = getopt_long(argc, argv, "hp:c:b:n:i:l:", 
								long_opt, NULL)) != -1)
	{
		switch(c){
			case 'h':
				usage();
				return -1;
			case 'p':
				snprintf(port[0], sizeof(port[0]), "%s", optarg);
				break;
			case 'c':
				path = optarg;
				break;
			case 'b':
				bRate = atoi(optarg);
				break;
			case 'l':
				eki_data_listen_port = optarg;
				break;
			case 'i':
				eki_ip = optarg;
				break;
			case 'n':
				if(atoi(optarg) > UDPSVRTHREADMAX){
					printf("The maxium running thread is 16!\n");
					thrd_num = UDPSVRTHREADMAX;
				}else{
					thrd_num = atoi(optarg);
				}
				break;
			default:
				usage();
				return -1;
		}
	}

	if(port[0] == NULL ||
		path == NULL ||
		eki_data_listen_port == NULL ||
		eki_ip == NULL)
	{	
		usage();
		return -1;
	}

	_port_set();

	printf("< Setting >\n\n");
	printf("Create udp port:\n");
	for(int i = 0; i < thrd_num; ++i)
		printf("%s ", port[i]);
	printf("\nCom Port: %s, baud rate %d\n", path, bRate);
	printf("EKI ip %s, data listen port %s\n",
				eki_ip, eki_data_listen_port);
	return 0;
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
	//newtio.c_oflag &= ~(OPOST); // should I close the flag?
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

int create_tx_socket(struct addrinfo *hints,
						struct addrinfo **servinfo,
						struct addrinfo **p)
{
	int skfd, ret;
	
	memset(hints, 0, sizeof(struct addrinfo));
	hints->ai_family = AF_UNSPEC;
	hints->ai_socktype = SOCK_DGRAM;

	ret = getaddrinfo(eki_ip, eki_data_listen_port, 
									hints, servinfo);
	if(ret != 0){
		printf("getaddrinof: %s\n", gai_strerror(ret));
		return -1;
	}
	
	for(*p = *servinfo; *p != NULL; *p = (*p)->ai_next){
		skfd = socket((*p)->ai_family,
						(*p)->ai_socktype,
						(*p)->ai_protocol);
		if(skfd == -1){
			perror("Creaet_tx_socket socket:");
			continue;
		}
		break;
	}

	if(*p == NULL){
		printf("%s: Cannot bind to socket!\n",
				__func__);
		return -1;
	}

	return skfd;
}

int create_rx_socket(int order)
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
	
	ret = getaddrinfo(NULL, port[order], &hints, &result);
	if(ret != 0){
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}
	
	int yes = 1;
	for(p = result; p != NULL; p = p->ai_next){
		fd = socket(p->ai_family, p->ai_socktype,
								 p->ai_protocol);
		if(fd == -1){
			perror("rx_socket: ");
			continue;
		}
		
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, 
							&yes, sizeof(yes)) == -1)
		{
			perror("rx setsockopt: ");
			close(fd);
			return -1;
		}
	
		if(bind(fd, p->ai_addr, p->ai_addrlen) == -1){
			close(fd);
			perror("rx bind: ");
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
	
	return fd;
}

void *serial_read(void *data)
{
	int ret;
	int rlen;
	struct udp_multi *usdg = (struct udp_multi *)data;
	struct udp_multi_res *res = &usdg->res;
	int fd = usdg->serial_fd;
	unsigned char rbuf[UDPDATALEN];// to read data from serial
	
	printf("Create `Serial Read Thread`: %lu\n", pthread_self());

	fd_set rfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(fd+1, &rfds, NULL, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &rfds)){
			rlen = read(fd, rbuf, sizeof(rbuf));
			if(rlen < 0){
				printf("\n%s: read failed(%s)\n",
						__func__, strerror(errno));
				break;
			}
			res->ser_rbyte += rlen;
			data_check(rbuf, rlen, &res->ser_err, SERIAL);
			disp_res(res);
		}
	}while(1);
	
	close(fd);
	pthread_exit(NULL);
}

int _serial_write_all
(int fd, unsigned char *tx, int len, struct udp_multi_res *res)
{
	int ret;
	int tmp = 0;
	
	if(len < 1) return -1;

	do{
		ret = write(fd, tx+tmp, len-tmp);
		if(ret < 0){
			printf("%s: write failed(%s)\n", 
						__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
		res->ser_wbyte += tmp;
		disp_res(res);
	}while(tmp < len);
	
	return 0;
}

void *serial_write(void *data)
{
	int ret, wRes;
	struct udp_multi *usdg = (struct udp_multi *)data;
	struct udp_multi_res *res = &usdg->res;
	int fd = usdg->serial_fd;
	//to send data to udp
	unsigned char wbuf[SERDATALEN];

	_init_10K_txbuf(wbuf);
	printf("Create `Serial Write Thread`: %lu\n", pthread_self());
	pthread_cond_wait(&usdg->cond, &usdg->mutex);
	/*
	 * after pthread_cond_wait recv signal, the mutex lock still locked
	 * so need to unlock it
	*/
	pthread_mutex_unlock(&usdg->mutex);
	fd_set wfds;
	struct timeval tv;
	do{
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		
		ret = select(fd+1, NULL, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}
		
		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &wfds)){
			wRes = usdg->ser_tx(fd, wbuf, sizeof(wbuf), res);
			if(wRes == -1) break;
		}
		sleep(3);
	}while(1);
	
	close(fd);	
	pthread_exit(NULL);
}

int _udp_send_all(int fd, unsigned char *tx, int len,
					struct addrinfo *p, 
					struct udp_multi_res *res)
{
	int ret;
	int tmp = 0;
	
	if(len < 1) return -1;

	do{
		ret = sendto(fd, tx+tmp, len-tmp, 0, 
				p->ai_addr, p->ai_addrlen);
		if(ret < 0){
			printf("%s: send failed(%s)\n", 
							__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
		res->udp_wbyte += tmp;
	}while(tmp < len);

	return 0;
}

int _udp_recv_all(int fd, unsigned char *rx, int len,
				struct sockaddr_storage *their_addr,
				socklen_t *addrlen,
				struct udp_multi_res *res)
{
	int ret;
	int tmp = 0;
	
	if(len < 1) return -1;
	
	do{
		ret = recvfrom(fd, rx+tmp, len-tmp, 0, 
				(struct sockaddr *)their_addr, addrlen);
		if(ret < 0){
			printf("%s: recv faied(%s)\n",
							__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
	}while(tmp < len);
	res->udp_rbyte += tmp;
	disp_res(res);

	return 0;
}

void *udp_work(void *data)
{
	int ret;
	int rxfd = 0, txfd, maxfd;
	int wRes, rRes;
	int lock = 0;
	// recv data from serial
	unsigned char rxbuf[SERDATALEN];
	// send data to serial
	unsigned char txbuf[UDPDATALEN] = 
							{'!', '"', '#', '$', '%'};
	struct udp_multi *usdg = (struct udp_multi *)data;
	struct udp_multi_res *res = &usdg->res;
	int order = usdg->order;
	
	pthread_mutex_lock(&usdg->mutex);
	usdg->order++;
	pthread_mutex_unlock(&usdg->mutex);
	printf("Create `UDP Work[%d]` Thread`: %lu\n", 
							order, pthread_self());

	struct addrinfo hints;			//tx socket
	struct addrinfo *servinfo, *p;	//tx socket
	txfd = create_tx_socket(&hints, &servinfo, &p);
	if(txfd == -1)
		pthread_exit(NULL);

	if(order == thrd_num-1)
		pthread_cond_broadcast(&usdg->cond); //wake the serial thread up!
	
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	struct sockaddr_storage their_addr;	//rx socket
	socklen_t addrlen = sizeof(their_addr); //rx socket
	do{
		if(!lock && !rxfd){
			rxfd = create_rx_socket(order);
			if(rxfd == -1) break;
			lock = 1;
		}
		maxfd = MAX(rxfd, txfd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_ZERO(&rfds);
		FD_SET(txfd, &wfds);
		FD_SET(rxfd, &rfds);
		
		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s[%d]: select failed!\n", __func__, order);
			break;
		}
		
		if(ret == 0) continue;
		
		if(FD_ISSET(txfd, &wfds) && lock &&
			pthread_mutex_trylock(&usdg->mutex) == 0)
		{
			res->udp_send_order = order;
			/* 
			 * we don't need to lock the `conter`,
			 * becuase only one udp thread can 
			 * send data for each times
			*/
			wRes = usdg->udp_tx(txfd, txbuf, UDPDATALEN, p, res);
			if(wRes == -1){
				pthread_mutex_unlock(&usdg->mutex);
				break;
			}
			disp_res(res);
			lock = 0;
			pthread_mutex_unlock(&usdg->mutex);
		}
	
		if(FD_ISSET(rxfd, &rfds)){
			rRes = usdg->udp_rx(rxfd, rxbuf, SERDATALEN, 
								&their_addr, &addrlen, res);
			if(rRes == -1) break;
			data_check(rxbuf, (int)sizeof(rxbuf), &res->udp_err, UDP);
			disp_res(res);
		}
		
		if(!lock){
			close(rxfd);
			rxfd = 0;
		}
	}while(1);

	printf("\n\n%s: %d thread exit!!!\n\n", __func__, order);	
	close(txfd);
	close(rxfd);
	pthread_exit(NULL);
}

static int usdg_udp_multi_init(struct udp_multi **self)
{
	*self = (struct udp_multi *)malloc(sizeof(struct udp_multi));
	if(*self == NULL) return -1;

	(*self)->ser_tx = _serial_write_all;
	(*self)->udp_tx = _udp_send_all;
	(*self)->udp_rx = _udp_recv_all;

	pthread_mutex_init(&(*self)->mutex, NULL);
	pthread_cond_init(&(*self)->cond, NULL);

	return 0;
}

static void usdg_udp_multi_remove(struct udp_multi *self)
{
	pthread_mutex_destroy(&self->mutex);
	pthread_cond_destroy(&self->cond);
	free(self);
}

int main(int argc, char **argv)
{
	int ret;
	struct udp_multi *usdg;
	pthread_t tid[UDPSVRTHREADMAX+SERIALTHREADNUM];
	pthread_attr_t attr;

	if(argc < 8){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;

	if(usdg_udp_multi_init(&usdg) == -1){
		printf("Usdg udp server test initial faied!\n");
		return -1;
	}
	pthread_attr_init(&attr);

	usdg->serial_fd = open_serial();
	if(usdg->serial_fd == -1) goto out;	

	// serial write 10K byte thread
	ret = pthread_create(&tid[0], &attr, serial_write, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "serial_write thread");
	
	// serial read 5 byte thread	
	ret = pthread_create(&tid[1], &attr, serial_read, (void *)usdg);
	if(ret != 0) 
		handle_error_en(ret, "serial_read thread");

	// multi-udp read/write thread
	for(int i = 0; i < thrd_num; ++i){
		ret = pthread_create(&tid[i+SERIALTHREADNUM], 
							&attr, udp_work, (void *)usdg);
		if(ret != 0)
			handle_error_en(ret, "udp_work");
	}
	
	for(int i = 0; i < thrd_num+SERIALTHREADNUM; ++i)
		pthread_join(tid[i], NULL);

out:
	usdg_udp_multi_remove(usdg);
	
	return 0;
}


