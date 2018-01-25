SRC = test.c
CFLAGS = -Wall
CFLAGS += -std=c99
CFLAGS += -D_GNU_SOURCE

TARGET = vcom_pack_testtool

all: $(TARGET)

vcom_pack_testtool: $(SRC_TEST)
	gcc $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)
