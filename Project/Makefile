SRCS:=$(wildcard *.c)
OBJS:=$(patsubst %.c, %, $(SRCS))
CC:=gcc
LIBS:=-lmysqlclient
FLAGS:=-g

ALL:$(OBJS)

%:%.c
	$(CC)  $^ -o $@ $(FLAGS) $(LIBS)

clean:
	rm -rf $(OBJS)

