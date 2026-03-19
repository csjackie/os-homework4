CC = g++
CFLAGS = -g3

TARGET1 = oss
TARGET2 = worker

all: $(TARGET1) $(TARGET2)

oss: oss.o
	$(CC) $(CFLAGS) oss.o -o oss

worker: worker.o
	$(CC) $(CFLAGS) worker.o -o worker

oss.o: oss.cpp
	$(CC) $(CFLAGS) -c oss.cpp

worker.o: worker.cpp
	$(CC) $(CFLAGS) -c worker.cpp

clean:
	rm -f *.o oss worker

