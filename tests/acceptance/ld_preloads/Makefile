
SRC := $(wildcard *.c)
OBJS := $(SRC:%.c=%.o)

all: $(OBJS)

clean:
	$(RM) $(OBJS)

%.o: %.c
	gcc -shared -fPIC -ldl -o $@ $<

.PHONY: all clean
