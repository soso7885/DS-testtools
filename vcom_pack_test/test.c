#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/select.h>
#include <math.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <asm-generic/termbits.h>
#include <sys/ioctl.h>
#include <sys/time.h>
//#define SHOWDATA
#include "test.h"

/* 
 * Golable variable 
 * assume no one can modify it
*/
int fd;
int baud;
char *path;
int itv;
int pack_size;
unsigned long int ec;

static inline void 
disp_result(struct vcom_pack_test *vcom)
{
	printf("Round %d, Wlen %d, Rlen %d", 
							vcom->round, 
							vcom->wlen, 
							vcom->rlen);
	if(!pack_size){
		printf(", Err %d", vcom->err);
		if(ec)
			printf(", End-Char err %d", vcom->ecerr); 
		if(itv){
			printf(", Avg-Itv dva %f, Max-Itv dva %f ",
						vcom->avg_itv, vcom->max_itv);
		}
	}else{
		printf(", Pack-Size err %d  ", 
					vcom->pack_size_err);
	}
	printf(" .     \r");                                            
	fflush(stdout);
}

static inline void disp_result_pack_size(int wlen,
										int rlen,
										struct vcom_pack_test *vcom)
{
	printf("Round %d, Wlen %d, Rlen %d", 
							vcom->round, 
							wlen, 
							rlen);
	printf(", err %d, Pack-Size err %d  ",
					vcom->err,
					vcom->pack_size_err);
	printf(" .     \r");                                            
	fflush(stdout);

}

static inline void 
compare_data(unsigned char *rx, 
			int len, 
			struct vcom_pack_test *vcom)
{
	for(int i = 0; i < len-1; ++i){
		if(abs(*(rx+i+1) - *(rx+i)) != 1 && 
				*(rx+i) != len-1) 
		{
			if(!pack_size){
				vcom->err++;
				printf("| <%x,%x> \n", 
					*(rx+i), *(rx+i+1));
			}else{
				if(*(rx+i) != 0 && 
					*(rx+i+1) != 0)
				{
					if(*(rx+i) != 1 && 
						*(rx+i+1) != 1)
					{
						vcom->err++;
						printf("\n\n| (%d)<%x,%x> \n\n", i, 
							*(rx+i), *(rx+i+1));
					}
				}
			}
		}
	}
	if(ec){
		if(*(rx+len-1) != ec)
			vcom->ecerr++;
	}
}
static void pack_by_size_data_check(unsigned char *rx, 
									int len, 
									struct vcom_pack_test *vcom)
{
	int ctz1, ctz2;
	
	if(len != pack_size){
		printf("\nRead %d byte != SetPackLen %d byte\n\n", 
										len , pack_size);
		vcom->pack_size_err++;
	}
	
	for(int i = 0; i < len-1; ++i){
		ctz2 = __builtin_ctz(*(rx+i+1));
		ctz1 = __builtin_ctz(*(rx+i));
		if((ctz2 - ctz1 != 1) && ctz2 != 0){
			vcom->err += 1;
			printf("\nData error <%x | %x>\n",
					*(rx+i), *(rx+i+1));
		}
	}
}
	
/*
 * The Average algorithm : 
 * avg(n) = avg(n-1)+(NowValue-avg(n-1))/n 
 */
float __avg_count(float now_itv, int round)
{
	static float avg_itv = 0.0;	
	float dif;

	dif = (now_itv-avg_itv) / (float)round;
	avg_itv += dif;

	return avg_itv;
}
	
int _pkdata_itv_chk(struct timeval *wtime, 
					struct timeval *rtime, 
					struct vcom_pack_test *vcom)
{
	struct timeval res;
	float tmp;
	float tmp_itv;
	float set_itv;
	float now_itv;
		
	set_itv =  itv/1000.0;
	timersub(rtime, wtime, &res);
	tmp = (res.tv_sec*1000000 + res.tv_usec) / 1000000.0;
	
	if(tmp < 0.001) return -1;
	tmp_itv = fabs(set_itv - tmp);

//	printf("\nSet %.2f, tmp %.2f, tmp_itv %.6f\n", set_itv, tmp, tmp_itv);
	if(tmp_itv > 3.0){
		printf("\n***** Critical time interval(wrtime-rtime) %.3f sec ****\n", tmp);	
		return -1;
	}
	
	now_itv = tmp_itv;
	
	if(now_itv > vcom->max_itv)
		vcom->max_itv = now_itv;
	
	vcom->avg_itv = __avg_count(now_itv, vcom->round);

	return 0;
}

int _tty_flush(void)
{
	int rlen;
	int tmp = 0;
	int ret;
	char rxtmp[1024];
	fd_set rfds;
	struct timeval tv;

	do{
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(fd+1, &rfds, 0, 0, &tv);
	
		if(ret < 0){
			printf("%s select failed !\n", __func__);
			break;
		}

		if(ret == 0) break;
	
		rlen = read(fd, rxtmp, DATALEN);
		tmp += rlen;
	}while(1);
	
	return tmp;
}
		
int set_termios(void)
{
	struct termios2 newtio;
	
	fd = open(path, O_RDWR);
	if(fd == -1){
		printf("Failed to open %s: %s\n",
					path, strerror(errno));
		exit(0);
	}
	printf("Open ComPort, fd %d\n", fd);
	
	if(ioctl(fd, TCGETS2, &newtio) < 0){
		printf("Failed to get termios setting: %s\n",
					strerror(errno));
		return -1;
	}
	printf("BEFORE setting : ospeed %d ispeed %d\n", 
				newtio.c_ospeed, newtio.c_ispeed);
	
	newtio.c_iflag &= ~(ISTRIP|IUCLC|IGNCR|ICRNL|INLCR|ICANON|IXON|PARMRK);
	newtio.c_iflag |= (IGNBRK|IGNPAR);
	newtio.c_lflag &= ~(ECHO|ICANON|ISIG);
	newtio.c_cflag &= ~CBAUD;
	newtio.c_cflag |= BOTHER;
	/* For 0a/0d issue */
	newtio.c_oflag &= ~(OPOST);
	newtio.c_ospeed = baud;
	newtio.c_ispeed = baud;

	if(ioctl(fd, TCSETS2, &newtio) < 0){
		printf("Failed to set termios: %s\n",
					strerror(errno));
		return -1;
	}
	printf("AFTER setting : ospeed %d ispeed %d\n", 
				newtio.c_ospeed, newtio.c_ispeed);
	
	return 0;
}

int 
pack_test_with_size_by_large_packet(struct vcom_pack_test *vcom)
{
	int i, j;
	int k = 1;
	int mode;
	int flush_res, ret;
	int base;
	int sendLen;

	printf("Support 4 mode:\n");
	printf("	1. per packet size < setting size\n");
	printf("	2. per packet size > setting size\n");
	printf("	3. unstable packet size\n");
	printf("	4. per packet with double size\nmode: ");
	scanf("%d", &mode);
	switch(mode){
		case 1:
			base = pack_size - 8;
			break;
		case 2:
			base = pack_size + 8;
			break;
		case 3:
			base = pack_size;
			break;
		case 4:
			base = pack_size*2 + 1;
			break;
		default:
			printf("mode %d is not support\n", mode);
			return -1;
	}
	if(base >= 2048){
		printf("Sorry, the packing size cannot grater than 1024 byte\n");
		return -1;
	}

	unsigned char txbuf[2048];
	unsigned char rxbuf[2048];

	printf("txbuf initial...\n");
	for(i = 0, j = 0; i < sizeof(txbuf); i++, j+=k){
		if(i%255 == 0 && i != 0)
			k *= -1;
		txbuf[i] = j;
	}

	flush_res = _tty_flush();
	printf("Flush %d byte\n", flush_res);

	int rlen = 0;
	int wlen = 0;
	fd_set wfds;
	fd_set rfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(fd+1, &rfds, &wfds, NULL, &tv);
	
		if(ret < 0){
			printf("%s select failed!\n", __func__);
			break;
		}
		
		if(ret == 0) continue;
		
		if(FD_ISSET(fd, &wfds)){
			if(mode == 3){
				if(vcom->round%2 == 0)
					sendLen = base + 8;
				else 
					sendLen = base - 8;
				wlen = write(fd, txbuf, sendLen);
			}else{
				sendLen = base;
				wlen = write(fd, txbuf, sendLen);
			}
			
			if(wlen != sendLen){
				printf("%s: write data failed!\n", __func__);
				break;
			}

		}
	
		if(FD_ISSET(fd, &rfds)){
			rlen = read(fd, rxbuf, sizeof(rxbuf));
			if(rlen < 0){
				printf("%s: read data failed(%s)\n", 
							__func__, strerror(errno));
				break;
			}
		}
		
		if(rlen > 0){
			compare_data(rxbuf, rlen, vcom);
			if(rlen % pack_size != 0){
				printf("\nrlen = %d\n", rlen);
				vcom->pack_size_err++;
			}
			vcom->round++;
			disp_result_pack_size(wlen, rlen, vcom);
			usleep(1*1000);
		}
	}while(1);

	return 0;
}

int pack_test_with_size(struct vcom_pack_test *vcom)
{
	int ret, tmp;
	int flush_res;
	int rlen = 0;
	int wlen = 0;
	unsigned char txbuf = 0x01;
	unsigned char rxbuf[1024] = {0};
	
	flush_res = _tty_flush();
	printf("Flush tty buffer %d byte\n", flush_res);

	fd_set wfds;
	fd_set rfds;
	struct timeval tv;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		
		ret = select(fd+1, &rfds, &wfds, 0, &tv);	
		if(ret < 0){
			printf("%s: select failed!\n", __func__);
			break;
		}
		
		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &rfds)){
			rlen = read(fd, rxbuf, DATALEN);
			if(rlen < 0){
				printf("Round %d, read length %d, FAILED !!\n", 
											vcom->round, rlen);
				break;
			}
			print_data(rxbuf, rlen, "Read");
		}

		if(FD_ISSET(fd, &wfds)){
			/* circular bit shift */
			txbuf = (txbuf<<1) | (txbuf>>(8-1));
			tmp = write(fd, &txbuf, 1);
			if(tmp <= 0){
				printf("Round %d, write FAILED !!\n", vcom->round);
				break;
			}
			wlen += tmp;
			usleep(100*1000);
			disp_result_pack_size(wlen, rlen, vcom);
		}
	
		if(rlen > 0){
			pack_by_size_data_check(rxbuf, rlen, vcom);
			disp_result_pack_size(wlen, rlen, vcom);
			rlen = wlen = 0;
			vcom->round++;
		}
	}while(1);

	return 0;
}

int pack_test_with_time(struct vcom_pack_test *vcom)
{
	int ret;
	int flush_res;
	int wlen = 0;
	int rlen = 0;
	unsigned char txbuf[DATALEN];
	unsigned char rxbuf[DATALEN];
	
	for(int i = 0; i < DATALEN; i++)
		txbuf[i] = i;
	printf("Init write data done !\n");
	
	flush_res = _tty_flush();
	printf("Flush tty buffer %d byte\n", flush_res);

	struct timeval tv;
	struct timeval wtime;
	struct timeval rtime;
	fd_set rfds;
	fd_set wfds;
	timerclear(&wtime);
	timerclear(&rtime);
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(fd+1, &rfds, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &wfds) && !wlen){
			wlen = write(fd, txbuf, DATALEN);
			if(wlen != DATALEN){
				printf("Round %d, write FAILED !!\n", 
											vcom->round);
				break;
			}
			gettimeofday(&wtime, NULL);
			print_data(txbuf, wlen, "Write");
			vcom->wlen += wlen;
			disp_result(vcom);
		}
	
		if(FD_ISSET(fd, &rfds) && !rlen && wlen){
			rlen = read(fd, rxbuf, DATALEN);
			if(rlen < 0){
				printf("%s: read failed!\n", __func__);
				break;
			}
			gettimeofday(&rtime, NULL);
			print_data(rxbuf, rlen, "Read");	
			vcom->rlen += rlen;
			disp_result(vcom);
		}
		
		if(wlen && rlen){			
			compare_data(rxbuf, rlen, vcom);
			_pkdata_itv_chk(&wtime, &rtime, vcom);
			vcom->round += 1;
			disp_result(vcom);
			wlen = rlen = 0;
			timerclear(&wtime);
			timerclear(&rtime);
		}
	}while(1);
	
	return 0;
}

int pack_test_with_ec(struct vcom_pack_test *vcom)
{
	int ret;
	int flush_res;
	int wlen = 0;
	int rlen = 0;
	unsigned char txbuf[DATALEN];
	unsigned char rxbuf[DATALEN];
	
	for(int i = 0; i < DATALEN; i++)
		txbuf[i] = i;
	printf("Init write data done !\n");
	
	flush_res = _tty_flush();
	printf("Flush tty buffer %d byte\n", flush_res);

	struct timeval tv;
	fd_set rfds;
	fd_set wfds;
	do{
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &rfds);
		FD_SET(fd, &wfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
	
		ret = select(fd+1, &rfds, &wfds, 0, &tv);
		if(ret < 0){
			printf("%s: select failed !\n", __func__);
			break;
		}
	
		if(ret == 0) continue;
	
		if(FD_ISSET(fd, &wfds)){
			wlen = write(fd, txbuf, DATALEN);
			if(wlen != DATALEN){
				printf("Round %d, write FAILED !!\n", 
										vcom->round);
				break;
			}
			print_data(txbuf, wlen, "Write");
			vcom->wlen += wlen;
			disp_result(vcom);
		}
	
		if(FD_ISSET(fd, &rfds)){
			rlen = read(fd, rxbuf, DATALEN);
			if(rlen < 0){
				printf("%s: read failed!\n", __func__);
				break;
			}
			print_data(rxbuf, rlen, "Read");
			compare_data(rxbuf, rlen, vcom);
			vcom->rlen += rlen;
			disp_result(vcom);
			vcom->round++;
		}
	}while(1);
	
	return 0;
}

int choice_mode(struct vcom_md *vcom_method)
{
	char ch;
	
	printf("Support 4 mode:\n");
	printf("	A. pack by character test\n");
	printf("	B. pack by time test\n");
	printf("	C. pack by size test with per byte\n");
	printf("	D. pack by size test with large packet\nmode: ");
	ch = getc(stdin);
	switch(ch){
		case 'A':
			printf("Enter end-character in Hex: ");
			scanf("%lx", &ec);
			itv = 0;
			pack_size = 0;
			vcom_method->test = pack_test_with_ec;
			break;
		case 'B':
			printf("Enter time interval: ");
			scanf("%d", &itv);
			ec = 0;
			pack_size = 0;
			vcom_method->test = pack_test_with_time;
			break;
		case 'C':
			printf("Enter packing size: ");
			scanf("%d", &pack_size);
			ec = 0;
			itv = 0;
			vcom_method->test = pack_test_with_size;
			break;
		case 'D':
			printf("Enter packing size: ");
			scanf("%d", &pack_size);
			ec = 0;
			itv = 0;
			vcom_method->test = pack_test_with_size_by_large_packet;
			break;
		default:
			printf("No support %c mode!\n", ch);
			return -1;
	}
	printf("Setting done !\n");
	printf("VCOM port path : [%s]\n", path);
	printf("baud rate : [%d]\n", baud);
	if(ec)
		printf("end-character : [%lx]\n", ec);
	if(itv)
		printf("interval : [%d ms]\n", itv);
	if(pack_size)
		printf("packing size: [%d byte]\n", pack_size);

	return 0;
}

static int init_vcom_pack_test(struct vcom_pack_test **self)
{
	*self = (struct vcom_pack_test *)malloc(sizeof(struct vcom_pack_test));
	if(*self == NULL) return -1;
	
	(*self)->err = 0;
	(*self)->ecerr = 0;
	(*self)->round = 1;
	(*self)->max_itv = 0;
	(*self)->avg_itv = 0;
	(*self)->pack_size_err = 0;

	return 0;
}

int main(int argc, char **argv)
{
	struct vcom_pack_test *vcom;
	struct vcom_md vcom_method = {
		.termios = set_termios,
	};

	if(argc < 3){
		printf("./vcom_pack_testtool [VCOM port path] [baud rate]\n");
		exit(0);
	}
	path = argv[1];
	baud = atoi(argv[2]);

	if(choice_mode(&vcom_method) == -1) exit(0);

	if(init_vcom_pack_test(&vcom) == -1){
		printf("Vcom packing test initial failed!\n");
		exit(0);
	}

	if(vcom_method.termios() == -1) goto out;
	/* ---------------------- TEST START --------------------------------*/
	vcom_method.test(vcom);	

	printf("Ready to close...\n");
out:
	close(fd);
	free(vcom);
	
	return 0;
}

