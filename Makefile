CFLAGS = -g -DREAL_TIMER
kim1: fake6502.o kim1.o
	gcc ${CFLAGS} -o kim1 kim1.o fake6502.o

clean:
	rm kim1 *.o
