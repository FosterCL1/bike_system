ARCH := arm
CC := arm-linux-gnueabi-gcc

all : main.o
	$(CC) -o main main.o

main.o : main.c
	$(CC) -c main.c

clean :
	rm -f main main.o

install :
	scp main root@192.168.2.236:~/
