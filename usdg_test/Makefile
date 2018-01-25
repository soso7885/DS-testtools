SRC_TCP_CLT = usdg_tcp_clt.c
SRC_TCP_CLT_NEW = usdg_tcp_clt_new.c
SRC_TCP_SVR = usdg_tcp_svr.c
SRC_UDP_SIMP = usdg_udp_simp.c
SRC_UDP_multi = usdg_udp_multi.c
SRC_UDP_PACK = usdg_udp_pack.c
SRC_UDP_OPEN_CLOSE = usdg_udp_open_close.c
SRC_MULTI = usdg_multicast.c
OUTPUT = ./output

LINKS = -pthread
CFLAGS = -std=c99
CFLAGS += -D_GNU_SOURCE

TARGET = tcp_clt
TARGET += tcp_svr
TARGET += udp_simple
TARGET += udp_multi
TARGET += udp_pack
TARGET += multicast
TARGET += tcp_clt_new
TARGET += udp_oc

all: output $(TARGET)

output:
	install -d $(OUTPUT)

tcp_clt: ${SRC_TCP_CLT}
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_TCP_CLT) $(LINKS)

tcp_clt_new: $(SRC_TCP_CLT_NEW)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_TCP_CLT_NEW) $(LINKS)

tcp_svr: $(SRC_TCP_SVR)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_TCP_SVR) $(LINKS)

udp_simple: $(SRC_UDP_SIMP)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_UDP_SIMP) $(LINKS)

udp_multi: $(SRC_UDP_multi)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_UDP_multi) $(LINKS)

udp_pack: $(SRC_UDP_PACK)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_UDP_PACK)

udp_oc: $(SRC_UDP_OPEN_CLOSE)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_UDP_OPEN_CLOSE)

multicast: $(SRC_MULTI)
	gcc $(CFLAGS) -o $(OUTPUT)/$@ $(SRC_MULTI)

clean:
	rm -rf $(OUTPUT)
