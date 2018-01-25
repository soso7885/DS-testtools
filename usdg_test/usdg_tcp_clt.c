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
int bRate = 0;

static inline void usage(void)
{
	printf("Usage : ./tcp_clt [-c/-p/-b/-h] [argument]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -p      Peer recving data port\n");
	printf("    -c      Com port path\n");
	printf("    -b      Set baud rate\n");
	printf("    -h      For help\n");				
}

static inline void disp_result(struct tcp_clt *usdg)
{
	printf("<Serial> rFrom %d, rByte %d, err %d |", 
									usdg->ctrl+1, 
									usdg->ser_rbyte,
									usdg->ser_err);
	printf("<TCP> sFrom %d, rFail %d, err %d, round %d", 
						usdg->ctrl+1, usdg->tcp_rfail, 
						usdg->tcp_err, usdg->round/16);
	printf(" .      \r");
	fflush(stdout); 
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
		printf("<Server> getaddrinfo : %s\n", gai_strerror(ret));	
		return -1;
	}

	for(p = result; p != NULL; p = p->ai_next){
		skfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(skfd == -1){
			perror("<Server> socket");
			continue;
		}
		printf("<Server> Create socket, socket fd = %d\n", skfd);
	
		if(setsockopt(skfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
			perror("<Server> setsockopt");
			close(skfd);
			return -1;
		}
	
		if(bind(skfd, p->ai_addr, p->ai_addrlen) == -1){
			perror("<Server> bind");
			continue;
		}
		printf("<Server> Bind success\n");
	
		break;
	}
	
	if(p == NULL){
		printf("<Server> Fail to create\n");
		close(skfd);
		return -1;
	}
	
	if(listen(skfd, BACKLOG) == -1){
		close(skfd);
		printf("<Server> listen");
		return -1;
	}

	freeaddrinfo(result);
	printf("<Server> Create Server success\n");
	
	return skfd;
}

int accept_svr(int skfd, int conn_num)
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
//		printf("[%d] TCP connect with IP %s, fd = %d\n", conn_num+1, addr, acp_fd);
	}else if(acp_addr.ss_family == AF_INET6){
		s = (struct sockaddr_in6 *)&acp_addr;
		inet_ntop(AF_INET6, &s->sin6_addr, addr, sizeof(addr));
//		printf("[%d] TCP connect with IP %s, fd = %d\n", conn_num+1, addr, acp_fd);
	}else{
		printf("fuckin wried ! What is the recv addr family?");
		return -1;
	}

	return acp_fd;
}

int set_termios(int fd)
{
	int ret;
	struct termios2 newtio;	

	ret = ioctl(fd, TCGETS2, &newtio);	
	if(ret < 0){
		perror("<Server> ioctl");
		return -1;
	}
	printf("<Server> Before setting : output speed %d, input speed %d\n",
			 newtio.c_ospeed, newtio.c_ispeed);
	newtio.c_iflag &= ~(ISTRIP|IUCLC|IGNCR|ICRNL|INLCR|ICANON|IXON|IXOFF|IXANY|PARMRK);
	newtio.c_iflag |= (IGNBRK|IGNPAR);
	newtio.c_lflag &= ~(ECHO|ICANON|ISIG);
	newtio.c_cflag &= ~CBAUD;
	newtio.c_cflag |= BOTHER;
	newtio.c_oflag &= ~(OPOST);	// should I close the flag?
	newtio.c_ospeed = bRate;
	newtio.c_ispeed = bRate;
	ret = ioctl(fd, TCSETS2, &newtio);
	if(ret < 0){
		perror("<Server> ioctl");
		return -1;
	}
	printf("<Server> After setting : output speed %d, input speed %d\n",
			newtio.c_ospeed, newtio.c_ispeed);
	
	return 0;
}

int readdata(struct tcp_clt *usdg, unsigned char *rxbuf, int recvlen)
{
	int tmp;
	int ret;
	fd_set rfds;
	struct timeval tv;
	int rtime = TCPDATALEN*10/bRate;
	int fd = usdg->ser_fd;

	alarm(rtime + RECVDATATIMEOUT);
	signal(SIGALRM, alarm_handle);		// set recv timeout 

	while(usdg->ser_rbyte != recvlen){
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 200*1000;
		ret = select(fd+1, &rfds, 0, 0, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			return -1;
		}

		if(ret == 0) continue;
		
		if(FD_ISSET(fd, &rfds)){
			tmp = read(fd, rxbuf+usdg->ser_rbyte, recvlen-usdg->ser_rbyte);
			usdg->ser_rbyte += tmp;
			disp_result(usdg);
		}

		if(timeout){
			usdg->ser_rbyte = 0;
			timeout = 0;
			return -1;
		}
	}
	usdg->ser_rbyte = 0;	
		
	return recvlen;
}

void svr_close_connect(struct tcp_clt *usdg, int fd)
{
	close(fd);
	usdg->thrd_num--;
	usdg->round++;
}

struct tcp_clt_opt opt = {
	.create = create_svr,
	.accept = accept_svr,
	.termios = set_termios,
	.read_all = readdata,
	.close = svr_close_connect,
};

	
void *tcp_work(void *data)
{
	int ret;	
	int conn_num;
	int acp_fd;
	int rlen = 0;
	int wlen = 0;
	unsigned char rxbuf[SERDATALEN];
	struct tcp_clt *usdg = (struct tcp_clt *)data;
	unsigned char *txbuf = usdg->txbuf2;

	/* Get the acp_fd & conn_num FIRST, then conn_num++ in main() */
	pthread_mutex_lock(&usdg->cnum_mutex);
	conn_num = usdg->conn_num;
	acp_fd = usdg->acp_fd[conn_num];
	pthread_cond_signal(&usdg->cnum_cond);
	pthread_mutex_unlock(&usdg->cnum_mutex);

//	printf("[%d] Create TCP thread SUCCESS, ID %ld\n", conn_num+1, pthread_self());	
//	printf("[%d] TCP thread READY, waiting for all connection connect!\n", conn_num+1);
	do{
		if(usdg->thrd_num == TCPCONN){
			break;
		}
	}while(1);
//	printf("[%d] All connection connect ! Begin thread work\n", conn_num+1);
	
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(acp_fd, &rfds);
		pthread_mutex_lock(&usdg->ctrl_mutex);
		if(usdg->ctrl == conn_num)
			FD_SET(acp_fd, &wfds);
		pthread_mutex_unlock(&usdg->ctrl_mutex);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(acp_fd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("[%d] TCP connection thread CLOSE...\n", conn_num+1);
			break;
		}

		if(ret == 0) continue;
		
		if(FD_ISSET(acp_fd, &rfds)){
			rlen = recv(acp_fd, rxbuf, SERDATALEN, 0);
			if(rlen < 0){
				usdg->tcp_rfail++;
				break;	
			}
			usdg->tcp_err = compare_data(rxbuf, rlen, usdg->tcp_err);
		}

		pthread_mutex_lock(&usdg->ctrl_mutex);
		if(usdg->ctrl == conn_num){
			if(FD_ISSET(acp_fd, &wfds) && !wlen && rlen){
				wlen = send(acp_fd, txbuf, TCPDATALEN, 0);
				if(wlen != TCPDATALEN){
					printf("[%d] TCP connection Send INCOMPLETE %d byte\n"
							, conn_num+1, wlen);
					pthread_mutex_unlock(&usdg->ctrl_mutex);
					break;
				}
				disp_result(usdg);
			}			
			/* After send, close the thread */
			if(wlen == TCPDATALEN){
				pthread_cond_wait(&usdg->ctrl_cond, &usdg->ctrl_mutex);
				pthread_mutex_unlock(&usdg->ctrl_mutex);
				break;
			}
		}
		pthread_mutex_unlock(&usdg->ctrl_mutex);
	}while(1);

	opt.close(usdg, acp_fd);
//	printf("[%d] CLOSE TCP connect\n", conn_num+1);
	pthread_mutex_lock(&usdg->cnum_mutex);
	/* 
	 *The next create thread will be the right number,
	 * If we dont do that, the next create thread number
	 * will always be 16
	 */
	usdg->conn_num = conn_num;				
	pthread_mutex_unlock(&usdg->cnum_mutex);
	
	pthread_exit(NULL);
}
		
void *serial_write(void *data)
{
	int ret, wlen;
	int j = 0;
	int lock = 0;
	struct tcp_clt *usdg = (struct tcp_clt *)data;
	int fd = usdg->ser_fd;

	printf("Create Serial write thread SUCCESS, ID %lu\n", pthread_self());
	fd_set wfds;
	struct timeval tv;
	do{
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		/* Init 5 byte data txbuf (0, 1, 2,.., 5 | 6, 7,..., 10) */
		if(!lock){
			for(int i = 0; i < SERDATALEN; i++, j++){
				if(j == 255)
					j = 0;
				usdg->txbuf1[i] = j;
			}
			lock = 1;
		}		

		ret = select(fd+1, 0, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}

		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &wfds) && lock){
			wlen = write(fd, usdg->txbuf1, SERDATALEN);
			if(wlen != SERDATALEN){
				printf("\nSerial write incomplete...\n");
				continue;
			}
			lock = 0;
		}
		sleep(3);
	}while(1);

	pthread_exit(NULL);
}

void *serial_read(void *data)
{
	int ret;
	int rlen = 0;
	unsigned char rxbuf[TCPDATALEN];
	struct tcp_clt *usdg = (struct tcp_clt *)data;
	int fd = usdg->ser_fd;
	
	printf("Create Serial read thread SUCCESS, ID %lu\n", pthread_self());
	
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
	
		if(FD_ISSET(fd, &rfds) && rlen < 1){
			rlen = opt.read_all(usdg, rxbuf, TCPDATALEN);
			if(rlen == -1){
				usdg->ser_rto++;	
				disp_result(usdg);
				break;
			}
			usdg->ser_err = compare_data(rxbuf, TCPDATALEN, usdg->ser_err);	
			disp_result(usdg);
			rlen = 0;
			/* To control which TCP connection thread send 10K data */
			pthread_mutex_lock(&usdg->ctrl_mutex);
			if(usdg->ctrl == TCPCONN -1)
				usdg->ctrl = 0;
			else
				usdg->ctrl++;
			pthread_cond_signal(&usdg->ctrl_cond);
			pthread_mutex_unlock(&usdg->ctrl_mutex);
		}
	}while(1);
	
	pthread_exit(NULL);
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
		port == NULL || 
		bRate == 0)
	{
		usage();
		return -1;
	}
	printf("Connect PORT %s, COM PORT PATH %s, Baud rate %d\n", port, path, bRate);
	
	return 0;
}

void usdg_tcp_clt_remove(struct tcp_clt *usdg)
{
	pthread_mutex_destroy(&usdg->cnum_mutex);
	pthread_cond_destroy(&usdg->cnum_cond);
	pthread_mutex_destroy(&usdg->ctrl_mutex);
	pthread_cond_destroy(&usdg->ctrl_cond);
	free(usdg);
}

int usdg_tcp_clt_init(struct tcp_clt **self)
{
	*self = (struct tcp_clt *)malloc(sizeof(struct tcp_clt));
	if(*self == NULL) return -1;
	
	(*self)->ser_fd = 0;
	(*self)->svr_fd = 0;
	(*self)->conn_num = 0;
	(*self)->ctrl = 0;
	(*self)->ser_rbyte = 0;
	(*self)->ser_rto = 0;
	(*self)->tcp_err = 0;
	(*self)->tcp_rfail = 0;
	(*self)->round = 0;
	(*self)->thrd_num = 0;
	memset(&(*self)->acp_fd, 0, TCPCONN);
	pthread_mutex_init(&(*self)->cnum_mutex, NULL);
	pthread_cond_init(&(*self)->cnum_cond, NULL);
	pthread_mutex_init(&(*self)->ctrl_mutex, NULL);
	pthread_cond_init(&(*self)->ctrl_cond, NULL);

	return 0;
}
			
int main(int argc, char **argv)
{
	int ret;
	int acp_fd;	
	struct tcp_clt *usdg;
	pthread_t tid[SVRTHREADNUM];
	pthread_attr_t attr;

	if(argc < 3){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;

	if(usdg_tcp_clt_init(&usdg) == -1){
		printf("Usdg server test initial failed!\n ");
		exit(0);
	}

	/* Init 10K txbuf data (0, 1, 2... 255, 254, 253...0) */
	int j, k = 1;
	for(int i=0, j=0; i < TCPDATALEN; i++, j+=k){
		if((i%255) == 0 && i)
			k *= -1;
		usdg->txbuf2[i] = j;
	}
	/* Open Serial */
	usdg->ser_fd = open(path, O_RDWR);
	if(usdg->ser_fd == -1){
		printf("<Server> Open serial port fail: %s\n", strerror(errno));
		goto out;
	}
	printf("<Server> Open Serial Port, fd = %d\n", usdg->ser_fd);
	/* Set termios */
	if(opt.termios(usdg->ser_fd) == -1){
		printf("<Server> Set termios failed: %s\n", strerror(errno));	
		goto out;
	}
	/* Create socket server */
	usdg->svr_fd = opt.create();
	if(usdg->svr_fd == -1) goto out;

	pthread_attr_init(&attr);
	
	/* Create serial write 5 byte data thread  */
	ret = pthread_create(&tid[0], &attr, serial_write, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "Serial write thread");
	/* Create serial read 10k data thread */
	ret = pthread_create(&tid[1], &attr, serial_read, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "Serial read thread");
	/* TCP connection accpet */
	do{	
		acp_fd = opt.accept(usdg->svr_fd, usdg->conn_num);
		if(acp_fd == -1){
			printf("<Server> TCP connection %d accept fail...\n", usdg->conn_num+1);
			sleep(1);
			continue;
		}
		pthread_mutex_lock(&usdg->cnum_mutex);
		usdg->acp_fd[usdg->conn_num] = acp_fd;
		pthread_mutex_unlock(&usdg->cnum_mutex);
		/* accept SUCCESS, create TCP work thread */
		ret = pthread_create(&tid[usdg->conn_num+2], &attr, tcp_work, (void *)usdg);
		if(ret != 0){
			printf("<Server> Create  TCP connection %d thread Fail : %s\n", 
					usdg->conn_num, strerror(ret));
			goto out;
		}
		usdg->thrd_num++;	
		/* conn++, to aviod conn over TCPCONN */
		pthread_mutex_lock(&usdg->cnum_mutex);
		if(usdg->conn_num < TCPCONN-1){
			pthread_cond_wait(&usdg->cnum_cond, &usdg->cnum_mutex);	
			usdg->conn_num++;
		}
		pthread_mutex_unlock(&usdg->cnum_mutex);
	}while(1);

	for(int i = 0; i < SVRTHREADNUM; i++)
		pthread_join(tid[i], NULL);

out:
	usdg_tcp_clt_remove(usdg);
	
	return 0;
}


