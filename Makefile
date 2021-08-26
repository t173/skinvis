
SRCS = main.c profile.c skintalk.c ring.c cmdline.c
DEPS = profile.h skintalk.h util.h ring.h cmdline.h
TARG = skintalk

OBJS = $(SRCS:.c=.o)

CC = gcc
CCFLAGS = -g -std=c11 -Wall -DDEBUG=2

# Any linked libraries (-lm -lpthread, etc.)
LDLIBS = -lpthread
LDFLAGS =

.PHONY: all clean extension

all: $(TARG) extension

clean:
	@-$(RM) -r $(TARG) $(OBJS) a.out *~ build/

extension: setup.py skinmodule.c $(OBJS)
	python3 setup.py build

$(TARG): $(OBJS) | $(DEPS) Makefile
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(DEPS) Makefile
	$(CC) $(CCFLAGS) -DEXECNAME=$(TARG) -c -o $@ $<

fake: fake.c util.h
	$(CC) $(CCFLAGS) -o $@ $< -lm -lrt -lpthread
