CC = gcc
CFLAGS = -Wall -O2 -Isrc -Isrc/vendor $(shell pkg-config --cflags libpng)
LDFLAGS = -lpthread $(shell pkg-config --libs libpng)

SRC = src/main.c src/mapping.c src/pngseq.c src/control.c src/httpd.c \
      src/vendor/ws2811.c src/vendor/rpihw.c src/vendor/mailbox.c \
      src/vendor/pcm.c src/vendor/pwm.c src/vendor/dma.c src/vendor/rgb_hsv.c
OBJ = $(SRC:.c=.o)

BIN = flattube

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
