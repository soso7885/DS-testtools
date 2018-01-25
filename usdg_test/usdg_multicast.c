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
char *port; // data listen port
char *group; // multicast group
int bRate = DFT_BAUDRATE;
int ascII; // send ascII or heximal

static inline void disp_res(struct multicast_res *res)
{
	printf("<Multicast> "
			XSTR(UDP write)" %d byte, "
			XSTR(Serial read)" %d byte, "
			XSTR(Err)" %d byte        ...\r",
			res->wlen,
			res->rlen,
			res->err
			);
	fflush(stdout);
}

static inline void usage(void)
{
	printf("Usage : ./multicast [-c/-p/-b/-g/-h] [argument]\n");
	printf("The most commonly used commands are (-h for help) :\n");
	printf("    -p      Data listen port\n");
	printf("    -c      Com port path\n");
	printf("    -g      Multicast group IP\n");
	printf("    -b      Set baud rate(Default 9600)\n");
	printf("    -h      For help\n");
}

int set_param(int argc, char **argv)
{
	int c;
	char ch;
	struct option long_opt[] = {
		{"help", no_argument, NULL, 'h'},
		{"data_listen_port", required_argument, NULL, 'p'},
		{"com_port", required_argument, NULL, 'c'},
		{"group", required_argument, NULL, 'g'},
		{"baud_rate", required_argument, NULL, 'b'},
	};
	
	while((c = getopt_long(argc, argv, "hp:c:b:g:", long_opt, NULL)) != -1)
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
				break;
			case 'g':
				group = optarg;
				break;
			default:
				usage();
				return -1;
		}
	}

	if(port == NULL ||
		path == NULL ||
		group == NULL)
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
	printf("Data listen port: %s\n", port);
	printf("Multicats group IP: %s\n", group);
	printf("Com Port: %s with baud rate %d\n", path, bRate);
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

int create_tx_socket(struct addrinfo *hints,
						struct addrinfo **servinfo,
						struct addrinfo **p)
{
	int skfd, ret;
	
	memset(hints, 0, sizeof(struct addrinfo));
	hints->ai_family = AF_UNSPEC;
	hints->ai_socktype = SOCK_DGRAM;

	ret = getaddrinfo(group, port, hints, servinfo);
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

static void init_data(unsigned char *buf, int len)
{
	if(ascII){	// send ascII
		for(int i = 0; i < len; ++i){
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
		for(int i = 1; i <= len; ++i){
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
				*err += 1;
				printf("\nError <%x | %x>\n", 
							*(buf+i), *(buf+i+1));
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
				*err+=1;
				printf("\nError  <%x | %x>\n", 
							*(buf+i), *(buf+i+1));
			}
		}
	}
}

int _multicast_send_all(int fd, 
						unsigned char *tx, 
						int len, 
						struct addrinfo *p,
						struct multicast_res *res)
{
	int tmp = 0;
	int ret;
	
	do{
		ret = sendto(fd, tx+tmp, len-tmp, 0,
						p->ai_addr, p->ai_addrlen);
		if(ret < 0){
			printf("%s: sendto failed(%s)\n",
							__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
		res->wlen += tmp;
		disp_res(res);
	}while(tmp < len);
	
	return 1;
}

int _serial_read_all(int fd, 
					unsigned char *rx, 
					int len, 
					struct multicast_res *res)
{
	int tmp = 0;
	int ret;
	
	do{
		ret = read(fd, rx+tmp, len-tmp);
		if(ret < 0){
			printf("%s: read failed(%s)\n",
						__func__, strerror(errno));
			return -1;
		}
		tmp += ret;
		res->rlen += tmp;
		disp_res(res);
	}while(tmp < len);
	
	return 1;
}

int work(struct multicast *usdg)
{
	int ret;
	int skfd, serfd;
	int wRes, rRes;
	unsigned char txbuf[ONE_K_BYTE];
	unsigned char rxbuf[ONE_K_BYTE];
	struct multicast_res *res = &usdg->res;

	serfd = open_serial();
	if(serfd == -1) return -1;

	struct addrinfo hints;	//tx socket
	struct addrinfo *servinfo, *p; //tx socket
	skfd = create_tx_socket(&hints, &servinfo, &p);
	if(skfd == -1){
		close(serfd);
		return -1;
	}

	init_data(txbuf, sizeof(txbuf));
	
	int maxfd = MAX(serfd, skfd);
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&rfds);
		FD_SET(skfd, &wfds);
		FD_SET(serfd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(skfd, &wfds)){
			wRes = usdg->multi_tx(skfd, txbuf, sizeof(txbuf),
										p, res);
			if(wRes == -1) break;
			disp_res(res);
		}
	
		if(FD_ISSET(serfd, &rfds)){
			rRes = usdg->ser_rx(serfd, rxbuf, sizeof(rxbuf), res);
			if(rRes == -1) break;
			data_check(rxbuf, sizeof(rxbuf), &res->err);
		}
	}while(1);
	
	close(serfd);
	close(skfd);
	
	return 0;
}

static int usdg_multicast_init(struct multicast **self)
{
	*self = (struct multicast *)malloc(sizeof(struct multicast));
	if(*self == NULL) return -1;
	
	(*self)->ser_rx = _serial_read_all;
	(*self)->multi_tx = _multicast_send_all;

	return 0;
}

int main(int argc, char **argv)
{
	struct multicast *usdg = NULL; 

	if(argc < 6){
		usage();
		return -1;
	}
	
	if(set_param(argc, argv) == -1) return -1;

	if(usdg_multicast_init(&usdg) == -1){
		printf("multicast malloc failed...\n");
		return -1;
	}
	
	work(usdg);

	free(usdg);
	
	return 0;
}	

