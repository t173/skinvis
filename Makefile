
SRCS = main.c profile.c skintalk.c cmdline.c
DEPS = profile.h skintalk.h util.h cmdline.h
TARG = skintalk

python = $(HOME)/python3.8/bin/python3.8
python-config = $(python)-config

OBJS = $(SRCS:.c=.o)

CC = gcc
CCFLAGS = -g -std=c11 -Wall -DDEBUG=2 $(shell python-config --cflags)

# Any linked libraries (-lm -lpthread, etc.)
LDLIBS = -lpthread $(shell python-config --libs)
LDFLAGS = $(shell python-config --ldflags)

.PHONY: all clean extension

all: $(TARG) extension

clean:
	@-$(RM) -r $(TARG) $(OBJS) a.out *~ build/

extension: setup.py skinmodule.c $(OBJS)
	$(python) setup.py build

$(TARG): $(OBJS) | $(DEPS) Makefile
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(DEPS) Makefile
	$(CC) $(CCFLAGS) -DEXECNAME=$(TARG) -c -o $@ $<

fake: fake.c util.h
	$(CC) $(CCFLAGS) -o $@ $< -lm -lrt -lpthread
