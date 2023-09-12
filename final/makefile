CC	= gcc
OBJS	= AEROPORTO.o 
PROG	= beta
OBJS2	= PIPE-comandos.o 
PROG2	= pipe

# GENERIC

all:		${PROG} ${PROG2}

clean:
		rm ${OBJS} ${PROG}
		rm ${OBJS2} ${PROG2}

${PROG}:	${OBJS}
		${CC} ${OBJS} -g -pthread -lm -o $@
		
${PROG2}:	${OBJS2}
		${CC} ${OBJS2} -g -lm -o $@

.c.o:
		${CC} $< -Wall -c -o $@

###########################

AEROPORTO.o: AEROPORTO.c

PIPE-comandos.o: PIPE-comandos.c

###########################
