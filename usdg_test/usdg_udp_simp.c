#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <asm-generic/termbits.h>
#include <pthread.h>

#include "usdg_udp.h"
/*
 * Gloable variance
 * assume only `set_param` can modify it
*/
char *path;	//the ComPort path
char *port_num;	// the udp port number;
int bRate = DFT_BAUDRATE;	// baud rate
int ascII; // send ascII or heximal

static inline void disp_res(struct udp_simp_res *res)
{
	printf("<Simple> "
			XSTR(Write)" %d byte, "
			XSTR(Read)" %d byte, "
			XSTR(Err)" %d byte        ...\r",
			res->wlen,
			res->rlen,
			res->err
			);
	fflush(stdout);
}

static inline void usage(void)
{
	printf("Usage : ./udp_simple [-c/-p/-b/-h] [argument]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -p      Peer recving data port\n");
	printf("    -c      Com port path\n");
	printf("    -b      Set baud rate(Default 9600)\n");
	printf("    -h      For help\n");
}

int set_param(int argc, char **argv)
{
	int c;
	char ch;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"udp_port", required_argument, NULL, 'p'},
		{"com_port", required_argument, NULL, 'c'},
		{"baud_rate", required_argument, NULL, 'b'},
	};
	
	while((c = getopt_long(argc, argv, "hp:c:b:", long_opt, NULL)) != -1)
	{
		switch(c){
			case 'h':
				usage();
				return -1;
			case 'p':
				port_num = optarg;
				break;
			case 'c':
				path = optarg;
				break;
			case 'b':
				bRate = atoi(optarg);
				break;
			default:
				usage();
				return -1;
		}
	}

	if(port_num == NULL ||
		path == NULL)
	{	
		usage();
		return -1;
	}

	printf("Send data in ascII? [Y/N]");
	ch = getc(stdin);
	if(ch == 'Y')
		ascII = 1;
	else
		ascII = 0;
	
	printf("< Setting >\n\n");
	printf("Create udp port: %s\n", port_num);
	printf("Com Port: %s, baud rate %d\n", path, bRate);
	printf("Send %s data\n\n", ascII==1?"ASCII":"heximal");

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
	printf("Before setting : output speed %d, input speed %d\n",
									newtio.c_ospeed, newtio.c_ispeed);
	newtio.c_iflag &= ~(ISTRIP|IUCLC|IGNCR|ICRNL|INLCR|ICANON|IXON|IXOFF|IXANY|PARMRK);
	newtio.c_iflag |= (IGNBRK|IGNPAR);
	newtio.c_lflag &= ~(ECHO|ICANON|ISIG);
	newtio.c_cflag &= ~CBAUD;
	newtio.c_cflag |= BOTHER;
	newtio.c_oflag &= ~(OPOST); // should I close the flag?
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

int create_svr(void)
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
	
	ret = getaddrinfo(NULL, port_num, &hints, &result);
	if(ret != 0){
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		return -1;
	}
	
	int yes = 1;
	for(p = result; p != NULL; p = p->ai_next){
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if(fd == -1){
			perror("socket: ");
			continue;
		}
		
		if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1){
			perror("setsockopt: ");
			close(fd);
			return -1;
		}
	
		if(bind(fd, p->ai_addr, p->ai_addrlen) == -1){
			close(fd);
			perror("bind: ");
			continue;
		}
		break;
	}
	
	if(p == NULL){
		printf("Create UDP server failed\n");
		close(fd);
		return -1;
	}
	printf("Create UDP server FD: %d\n", fd);
	
	freeaddrinfo(result);
	
	return fd;
}

static void _init_data(unsigned char *buf)
{
	if(ascII){	// send ascII
		for(int i = 0; i < DATALEN; ++i){
			if(i == 0){
				*(buf+i) = '!';
			}else{
				*(buf+i) = *(buf+i-1)+1;
				if(*(buf+i) > '}')
					*(buf+i) = '!';
			}
		}
	}else{
		*(buf) = 0x01;
		for(int i = 1; i <= DATALEN; ++i){
			/* circular bit shift */
			*(buf+i) = *(buf+i-1)<<1 | *(buf+i-1)>>(8-1);
		}
	}	
}

static inline void 
data_check(unsigned char *buf, int len, int *err)
{
	// ascII data check
	if(ascII){
		for(int i = 0; i < len-1; ++i){
			if(abs(*(buf+i) - *(buf+i+1)) != 1 &&
					*(buf+i+1) != '!')
			{
				if(*(buf+i) != 0 && *(buf+i+1) != 0){
					*err += 1;
					printf("\nError <%x | %x>\n", 
								*(buf+i), *(buf+i+1));
				}
			}
		}
	}else{ // circular bit shift data check
		int ctz1, ctz2;
		for(int i = 0; i < len-1; ++i){
			/*
		 	 * Count Trailing Zero
		 	 * return the first '1' position of byte,
			 * built in gcc
			*/
			ctz1 = __builtin_ctz(*(buf+i));
			ctz2 = __builtin_ctz(*(buf+i+1));
			/*
			 * circular bit shift check,
			 * 10000000 next shift is 00000001
			 * so if ctz1 == 7, ctz2 must be 0
			*/
			if((ctz2 - ctz1 != 1) && ctz2 != 0){
				if(*(buf+i) != 0 && *(buf+i+1) != 0){
					*err += 1;
					printf("\nError  <%x | %x>\n", 
								*(buf+i), *(buf+i+1));
				}
			}
		}
	}
}

void serial_write_data
(int fd, unsigned char *buf, struct udp_simp_res *res)
{
	int ret;
	fd_set wfds;
	struct timeval tv;
	int wlen;

	do{
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(fd+1, NULL, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}

		if(ret == 0) continue;
		
		if(FD_ISSET(fd, &wfds)){
			wlen = write(fd, buf, DATALEN);
			if(wlen < 0){
				printf("%s: write faied(%s)\n",
						__func__, strerror(errno));
				break;
			}
			res->wlen += wlen;
			disp_res(res);
		}
	}while(1);

	close(fd);
}

void *serial_write_thread(void *data)
{
	int fd;
	unsigned char wbuf[DATALEN];
	struct udp_simp *usdg = (struct udp_simp *)data;
	
	printf("Create %s thread, ID %ld\n", 
						__func__, pthread_self());

	int wlen;
	int rlen;	
	fd = open_serial();
	if(fd == -1)
		pthread_exit(NULL);

	_init_data(wbuf);
	pthread_cond_wait(&usdg->cond, &usdg->mutex);
	
	usdg->tx(fd, wbuf, &usdg->res);
	
	pthread_exit(NULL);
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

void udp_recv_data
(int skfd,  unsigned char *rbuf, struct udp_simp_res *res)
{
	int ret;
	int rlen;
	struct sockaddr_storage their_addr;
	socklen_t addrlen = sizeof(their_addr);
	fd_set rfds;
	struct timeval tv;

	printf("%s: flushing %d byte data\n",
								__func__, 
			data_flush(skfd, &their_addr, &addrlen));
	
	do{
		FD_ZERO(&rfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(skfd+1, &rfds, NULL, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed...\n", __func__);
			break;
		}

		if(ret == 0) continue;
		
		if(FD_ISSET(skfd, &rfds)){
			rlen = recvfrom(skfd, rbuf, DATALEN, 0,
							(struct sockaddr *)&their_addr, 
							&addrlen);
			if(rlen < 0){
				printf("%s: recv failed(%s)\n",
							__func__, strerror(errno));
				break;
			}
			data_check(rbuf, rlen, &res->err);
			res->rlen += rlen;
			disp_res(res);
		}
	}while(1);
	
	close(skfd);
}

void *udp_recv_thread(void *data)
{
	int skfd;
	unsigned char rbuf[DATALEN];

	struct udp_simp *usdg = (struct udp_simp *)data;

	printf("Create %s thread, ID %ld\n", __func__, pthread_self());

	skfd = create_svr();
	if(skfd == -1)
		pthread_exit(NULL);

	pthread_cond_signal(&usdg->cond);
	
	usdg->rx(skfd, rbuf, &usdg->res);
	
	pthread_exit(NULL);
}

static int usdg_udp_simp_init(struct udp_simp **self)
{
	*self = (struct udp_simp *)malloc(sizeof(struct udp_simp));
	if(*self == NULL) return -1;
	
	pthread_cond_init(&(*self)->cond, NULL);
	pthread_mutex_init(&(*self)->mutex, NULL);
	(*self)->tx = serial_write_data;
	(*self)->rx = udp_recv_data;

	return 0;
}

static void usdg_udp_simp_remove(struct udp_simp *self)
{
	pthread_cond_destroy(&self->cond);
	pthread_mutex_destroy(&self->mutex);
	free(self);
}

int main(int argc, char **argv)
{
	int ret;
	pthread_t tid[2];
	struct udp_simp *usdg = NULL; 

	if(argc < 5){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;

	if(usdg_udp_simp_init(&usdg) == -1){
		printf("Malloc failed...\n");
		return -1;
	}
	
	ret = pthread_create(&tid[0], NULL, serial_write_thread, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "serial_write_thread: ");
	ret = pthread_create(&tid[1], NULL, udp_recv_thread, (void *)usdg);
	if(ret != 0)
		handle_error_en(ret, "udp_recv_thread: ");

	pthread_join(tid[0], NULL);
	pthread_join(tid[1], NULL);
out:
	usdg_udp_simp_remove(usdg);

	return 0;
}	

