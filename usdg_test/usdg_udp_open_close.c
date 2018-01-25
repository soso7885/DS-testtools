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

#include "usdg_udp.h"
/*
 * Gloable variance
 * assume only `set_param` can modify it
*/
char *path;	//the ComPort path
char *port_num;	// the udp port number;
int bRate = DFT_BAUDRATE;	// baud rate

static inline void disp_res(struct udp_oc_res *res)
{
	printf("<Open Close> "
			XSTR(Write)" %d byte, "
			XSTR(Read)" %d byte, "
			XSTR(Err)" %d byte, "
			XSTR(round)" %d       ...\r",
			res->wlen,
			res->rlen,
			res->err,
			res->round
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
	
	printf("< Setting >\n\n");
	printf("Create udp port: [%s]\n", port_num);
	printf("Com Port: [%s]\nbaud rate [%d]\n", path, bRate);

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
//	printf("Before setting : output speed %d, input speed %d\n",
//									newtio.c_ospeed, newtio.c_ispeed);
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
//	printf("Setting SUCCESS : output speed %d, input speed %d\n",
//									newtio.c_ospeed, newtio.c_ispeed);

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
	
	freeaddrinfo(result);
//	printf("Create UDP success, fd = %d\n", fd);
	
	return fd;
}

static void _init_data(unsigned char *buf)
{
	*(buf) = 0x01;
	for(int i = 1; i <= DATALEN; ++i){
		/* circular bit shift */
		*(buf+i) = *(buf+i-1)<<1 | *(buf+i-1)>>(8-1);
	}
}

static inline void data_check(unsigned char *buf,
								int len,
								int *err)
{
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

static int data_flush(int fd, 
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

static int _write_complete(int fd, 
							unsigned char *tx,
							int len)
{
	int tmp;
	int wlen = 0;
	
	do{
		tmp = write(fd, tx+wlen, len-wlen);
		if(tmp < 0){
			printf("%s(%d): write failed(%s)\n",
					__func__, __LINE__, strerror(errno));
			return -1;
		}
		
		wlen += tmp;
	}while(wlen < tmp);
	
	return len;
}

static int usdg_udp_open_close_init(struct udp_oc **self)
{
	*self = (struct udp_oc *)malloc(sizeof(struct udp_oc));
	if(*self == NULL) return -1;
	
	return 0;
}

int work(struct udp_oc *usdg)
{
	int ser_fd;
	int skfd;
	unsigned char txbuf[DATALEN];
	unsigned char rxbuf[DATALEN];
	struct udp_oc_res *res = &usdg->res;
	struct sockaddr_storage their_addr;
	socklen_t addrlen = sizeof(their_addr);

	_init_data(txbuf);	
	
	int ret;
	int maxfd;
	int lock = 0;
	int wlen;
	int rlen;
	fd_set wfds;
	fd_set rfds;
	struct timeval tv;
	
	do{
		if(!lock){
			ser_fd = open_serial();
			if(ser_fd == -1) break;
			skfd = create_svr();
			if(skfd == -1) break;
			maxfd = MAX(ser_fd, skfd);
			wlen = 0;
			rlen = 0;
			lock = 1;
		}
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(ser_fd, &wfds);
		FD_SET(skfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

	
		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s(%d): select failed(%s)\n",
					__func__, __LINE__, strerror(errno));
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(ser_fd, &wfds) && !wlen){
			wlen = _write_complete(ser_fd, txbuf, sizeof(txbuf));
			if(wlen < 0) break;
			res->wlen = wlen;
		}
	
		if(FD_ISSET(skfd, &rfds) && rlen != DATALEN){
			rlen += recvfrom(skfd, rxbuf+rlen, sizeof(rxbuf)-rlen, 0,
								(struct sockaddr *)&their_addr,
									&addrlen);
			if(rlen < 0){
				printf("%s(%d): recv failed (%s)\n",
						__func__, __LINE__, strerror(errno));
				break;
			}
			res->rlen = rlen;
		}
		
		if(wlen == DATALEN && rlen == DATALEN && lock){
			data_check(rxbuf, rlen, &res->err);
			disp_res(res);
			wlen = 0;
			rlen = 0;
			lock = 0;
			close(ser_fd);
			close(skfd);
			res->round++;
			lock = 0;
		}
	}while(1);
	
	close(skfd);
	close(ser_fd);

	return 0;
}

int main(int argc, char **argv)
{
	struct udp_oc *usdg = NULL;
	
	if(argc < 5){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;
	
	if(usdg_udp_open_close_init(&usdg) == -1){
		printf("Malloc failed...\n");
		return -1;
	}
	
	work(usdg);
	
	free(usdg);
	
	return 0;
}	
	
			

	

	
