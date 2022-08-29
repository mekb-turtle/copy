CC=cc
CFLAGS=-Wall -O2
LFLAGS=-lm -s

OBJS=copy.o

TARGET=copy

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -fv -- $(OBJS) $(TARGET)
