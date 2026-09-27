# Host regression test for the HDLC transmitter in src/ax25.c.
#
#   make          build and run with the default seed
#   make SEED=n   run with another seed
#   make clean    remove the build output
#
# The modem sources are compiled unmodified, with FX.25 enabled as in the
# firmware build, under AddressSanitizer and UndefinedBehaviorSanitizer.

COMPONENT := ../..
CC        ?= cc
SEED      ?= 0x12345678

CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter \
           -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
           -DENABLE_FX25
CPPFLAGS := -Istubs -I$(COMPONENT) -I$(COMPONENT)/include -I$(COMPONENT)/lwfec

SRCS := test_ax25_tx_hdlc.c \
        $(COMPONENT)/src/ax25.c \
        $(COMPONENT)/src/crc_ccit.c \
        $(COMPONENT)/src/fx25.c \
        $(COMPONENT)/lwfec/rs.c \
        $(COMPONENT)/lwfec/gf.c

BIN := build/test_ax25_tx_hdlc

.PHONY: all run clean

all: run

$(BIN): $(SRCS) $(wildcard stubs/*.h stubs/*/*.h) $(wildcard $(COMPONENT)/include/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRCS)

run: $(BIN)
	./$(BIN) $(SEED)

clean:
	rm -rf build
