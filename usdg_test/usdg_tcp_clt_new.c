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
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm-generic/termbits.h>
#include <signal.h>

#include "usdg_tcp.h"

/*
 * gloable variable
 * assume no one can modify it!
*/
char *port;
char *path;
int bRate = DFT_BAUDRATE;
/* use tid to control sending thread */
pthread_t tcp_work_tid[TCPCONN];

static inline void usage(void)
{
	printf("Usage : ./tcp_clt_new [-c/-p/-b/-h] [argument]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -p      Peer recving data port\n");
	printf("    -c      Com port path\n");
	printf("    -b      Set baud rate(default = " XSTR(DFT_BAUDRATE) ")\n");
	printf("    -h      For help\n");				
}

static void disp_result(struct tcp_clt_new_res *res)
{
	printf("<Serial> "
			XSTR(write)" %d, "
			XSTR(read)" %d, "
			XSTR(err)" %d | "
			"<TCP> "
			XSTR(host)" %d, "
			XSTR(send)" %d, "
			XSTR(recv)" %d, "
			XSTR(err)" %d        ...\r",
			res->ser_wbyte,
			res->ser_rbyte,
			res->ser_err,
			res->host,
			res->tcp_wbyte,
			res->tcp_rbyte,
			res->tcp_err);
	fflush(stdout);
}

int set_param(int argc, char **argv)
{
	int c;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"port", required_argument, NULL, 'p'},
		{"comport_path", required_argument, NULL, 'c'},
		{"baud_rate", required_argument, NULL, 'b'}
	};

	while((c = getopt_long(argc, argv, "hp:c:b:", long_opt, NULL)) != -1)
	{
		switch(c){
			case 'h':
				usage();
				return -1;
			case 'p':
				port = optarg;
				break;
			case 'c':
				path = optarg;
				break;
			case 'b':
				bRate = atoi(optarg);
				if(bRate > 921600 || bRate < 50){
					printf("Baud rate should be 50 < baud rate < 921600\n");
					return -1;
				}
				break;
			default:
				usage();
				return -1;
		}
	}
	
	if(path == NULL || 
		port == NULL)
	{
		usage();
		return -1;
	}
	printf("<Setting>\n");
	printf("Peer recving port [%s]\n", port);
	printf("ComPort [%s]\n", path);
	printf("BaudRate [%d]\n", bRate);
	
	return 0;
}

static void init_10K_data(unsigned char *buf,
						int len)
{
	int j = 0;
	int k = 1;
	
	// init 10K buf(0, 1, 2,..., 255, 254, 253...)
	for(int i = 0; i < len; i++, j+=k){
		if((i%255) == 0 && i)
			k *= -1;
		*(buf+i) = j;
	}
}

static int find_order(pthread_t id)
{
	for(int i = 0; i < TCPCONN; ++i){
		if(id == tcp_work_tid[i])
			return i;
	}
	
	return -1;
}

int create_svr(void)
{
	int skfd;
	int ret;
	int yes = 1;
	struct addrinfo hints;
	struct addrinfo *result;
	struct addrinfo *p;
	
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	hints.ai_protocol = 0;
	
	ret = getaddrinfo(NULL, port, &hints, &result);
	if(ret != 0){
		printf("getaddrinfo : %s\n", gai_strerror(ret));	
		return -1;
	}

	for(p = result; p != NULL; p = p->ai_next){
		skfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(skfd == -1){
			perror("socket");
			continue;
		}
		printf("Create socket, socket fd = %d\n", skfd);
	
		if(setsockopt(skfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
			perror("setsockopt");
			close(skfd);
			return -1;
		}
	
		if(bind(skfd, p->ai_addr, p->ai_addrlen) == -1){
			perror("bind");
			continue;
		}
		printf("Bind success\n");
	
		break;
	}
	
	if(p == NULL){
		printf("Fail to create\n");
		close(skfd);
		return -1;
	}
	
	if(listen(skfd, BACKLOG) == -1){
		close(skfd);
		printf("listen");
		return -1;
	}

	freeaddrinfo(result);
	printf("Create Server success\n");
	
	return skfd;
}
int accept_svr(int skfd)
{
	int acp_fd;
	char addr[INET6_ADDRSTRLEN];
	struct sockaddr_storage acp_addr;
	struct sockaddr_in *p;
	struct sockaddr_in6 *s;
	socklen_t addrlen = sizeof(acp_addr);

	acp_fd = accept(skfd, (struct sockaddr*)&acp_addr, &addrlen);
	if(acp_fd == -1){
		close(acp_fd);
		return -1;
	}

	if(acp_addr.ss_family == AF_INET){
		p = (struct sockaddr_in *)&acp_addr;
		inet_ntop(AF_INET, &p->sin_addr, addr, sizeof(addr));
	}else if(acp_addr.ss_family == AF_INET6){
		s = (struct sockaddr_in6 *)&acp_addr;
		inet_ntop(AF_INET6, &s->sin6_addr, addr, sizeof(addr));
	}else{
		printf("fuckin wried ! What is the recv addr family?");
		return -1;
	}

	return acp_fd;
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
	printf("Before setting : output speed %d, input speed %d\n",
									newtio.c_ospeed, newtio.c_ispeed);
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
	printf("Setting SUCCESS : output speed %d, input speed %d\n",
									newtio.c_ospeed, newtio.c_ispeed);
	return fd;
}

void *serial_read(void *data)
{
	int ret;
	int rlen;
	unsigned char rxbuf[TCPDATALEN];
	struct tcp_clt_new *usdg = (struct tcp_clt_new *)data;
	struct tcp_clt_new_res *res = &usdg->res;
	int fd = usdg->ser_fd;
	
	printf("Create Serial read thread SUCCESS, ID %lu\n", 
											pthread_self());
	fd_set rfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(fd+1, &rfds, 0, 0, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}

		if(ret == 0) continue;
	
		rlen = read(fd, rxbuf, sizeof(rxbuf));
		if(rlen < 0){
			printf("%s: read failed!(%s)\n",
					__func__, strerror(errno));
			break;
		}
		res->ser_rbyte += rlen;
		res->ser_err = compare_data(rxbuf,
									rlen,
									res->ser_err);
		disp_result(res);
	}while(1);
	
	pthread_exit(NULL);
}

void *serial_write(void *data)
{
	int ret;
	int wlen = 0;
	struct tcp_clt_new *usdg = (struct tcp_clt_new *)data;
	struct tcp_clt_new_res *res = &usdg->res;
	int fd = usdg->ser_fd;
	unsigned char sertx[SERDATALEN] = {0, 1, 2, 3, 4};

	printf("Create Serial write thread SUCCESS, ID %lu\n", 
											pthread_self());
	fd_set wfds;
	struct timeval tv;
	do{
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);

		ret = select(fd+1, 0, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}

		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &wfds)){
			wlen = write(fd, sertx, sizeof(sertx));
			if(wlen != SERDATALEN){
				printf("\nSerial write incomplete...\n");
				break;
			}
			res->ser_wbyte += wlen;
			disp_result(res);
		}

		sleep(3);
	}while(1);

	pthread_exit(NULL);
}

int _tcp_send_all(int fd,
					unsigned char *tx,
					int len,
					struct tcp_clt_new_res *res)
{
	int ret;
	int tmp = 0;
	
	do{
		ret = send(fd, tx+tmp, len-tmp, 0);
		if(ret < 0){
			printf("\n%s: send failed(%s)\n",
						__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
		res->tcp_wbyte += tmp;
		disp_result(res);
	}while(tmp < len);	
	
	return 1;
}

void *tcp_work(void *data)
{
	int ret;	
	int rlen = 0;
	int wRes = 0;
	int exit = 0;
	int acp_fd;
	struct tcp_clt_new *usdg = (struct tcp_clt_new *)data;
	struct tcp_clt_new_res *res = &usdg->res;
	int fd = usdg->svr_fd;
	unsigned char *txbuf = usdg->tcptxbuff;
	unsigned char rxbuf[SERDATALEN];

	pthread_cond_wait(&usdg->cond, &usdg->mutex);
	pthread_mutex_unlock(&usdg->mutex);

	int order = find_order(pthread_self());
	if(order == -1){
		printf("%lu: Cannot find order!!!!\n", pthread_self());
		pthread_exit(NULL);
	}
	
	printf("\nCreate TCP thread (%d) SUCCESS, ID %ld\n", 
									order, pthread_self());

	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	do{
		acp_fd = accept_svr(fd);
		if(acp_fd == -1){
			printf("\n%s(%d): accep_svr failed!\n",
									__func__, order);
			break;
		}
	//	pthread_mutex_lock(&usdg->conn_mutex);
		usdg->conn++;
	//	pthread_mutex_unlock(&usdg->conn_mutex);

		do{
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(acp_fd, &rfds);
			FD_SET(acp_fd, &wfds);
			tv.tv_sec = 1;
			tv.tv_usec = 0;

			ret = select(acp_fd+1, &rfds, &wfds, NULL, &tv);
			if(ret < 0){
				printf("\n%s(%d): select failed(%s)\n",
							__func__, order, strerror(errno));
				exit = 1;
				break;
			}

			if(ret == 0) continue;
		
			if(FD_ISSET(acp_fd, &rfds)){
				rlen = recv(acp_fd, rxbuf, sizeof(rxbuf), 0);
				if(rlen < 0){
					printf("\n%s(%d): recv failed(%s)\n",
								__func__, order, strerror(errno));
					exit = 1;
					break;	
				}
				res->tcp_rbyte += rlen;
				res->tcp_err = compare_data(rxbuf, rlen, res->tcp_err);
				disp_result(res);
			}

			if(FD_ISSET(acp_fd, &wfds)){
				pthread_mutex_lock(&usdg->mutex);
				if(usdg->ctrl == order){
					res->host = order;
					wRes = usdg->tcp_tx(acp_fd, txbuf, TCPDATALEN, res);
					if(wRes == -1)
						exit = 1;
					res->tcp_wbyte += wRes;
					TAKE_TURN(usdg->ctrl);
					pthread_mutex_unlock(&usdg->mutex);
					break;
				}
				pthread_mutex_unlock(&usdg->mutex);	
			}
			sleep(1);
		}while(1);
		
	//	pthread_mutex_lock(&usdg->conn_mutex);
		close(acp_fd);
		usdg->conn--;
	//	pthread_mutex_unlock(&usdg->conn_mutex);
		sleep(1);
		if(exit) break;

	}while(1);
	printf("\n[%d] Thread exit\n\n", order);

	pthread_exit(NULL);
}

void usdg_tcp_clt_new_remove(struct tcp_clt_new **self)
{
	pthread_mutex_destroy(&(*self)->mutex);
	pthread_cond_destroy(&(*self)->cond);
	free(*self);
}

int usdg_tcp_clt_new_init(struct tcp_clt_new **self)
{
	*self = (struct tcp_clt_new *)malloc(sizeof(struct tcp_clt_new));
	if(*self == NULL) return -1;

	init_10K_data((*self)->tcptxbuff, TCPDATALEN);
	
	(*self)->tcp_tx = _tcp_send_all;
	pthread_mutex_init(&(*self)->mutex, NULL);
	pthread_cond_init(&(*self)->cond, NULL);
	
	return 0;
}

int main(int argc, char **argv)
{
	int ret;
	struct tcp_clt_new *usdg;
	pthread_t tid[2];
	
	if(argc < 3){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;
	
	if(usdg_tcp_clt_new_init(&usdg) == -1) return -1;

	// open serail
	usdg->ser_fd = open_serial();
	if(usdg->ser_fd == -1) goto out;

	// create server
	usdg->svr_fd = create_svr();
	if(usdg->svr_fd == -1) goto out;
	
	ret = pthread_create(&tid[0], NULL, serial_write, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "Serial write thread");
	
	ret = pthread_create(&tid[1], NULL, serial_read, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "Serial read thread");
	
	for(int i = 0; i < TCPCONN; ++i){
		ret = pthread_create(&tcp_work_tid[i], NULL, tcp_work, (void *)usdg);
		if(ret != 0)
			handle_error_en(ret, "tcp_work");
	}
	sleep(1);
	pthread_cond_broadcast(&usdg->cond);
	
	for(int i = 0; i < SVRTHREADNUM; ++i)
		pthread_join(tid[i], NULL);	
	
out:
	usdg_tcp_clt_new_remove(&usdg);
	
	return 0;
}

