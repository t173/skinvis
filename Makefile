
SRCS = layout.c profile.c skintalk.c
DEPS = layout.h profile.h skintalk.h util.h
#TARG =

python = /usr/bin/python3
python_config = $(python)-config

OBJS = $(SRCS:.c=.o)

CC = gcc
CCFLAGS = -g -std=c11 -Wall -DDEBUG=2 $(shell $(python_config) --cflags)

# Any linked libraries (-lm -lpthread, etc.)
LDLIBS = -lpthread $(shell $(python_config) --libs)
LDFLAGS = $(shell $(python_config) --ldflags)

.PHONY: all clean extension

all: $(OBJS) extension
#all: $(TARG) extension

clean:
	@-$(RM) -r $(TARG) $(OBJS) a.out *~ build/

extension: setup.py skinmodule.c $(OBJS)
	$(python) setup.py build

# $(TARG): $(OBJS) | $(DEPS) Makefile
# 	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(DEPS) Makefile
	$(CC) $(CCFLAGS) -DEXECNAME=$(TARG) -c -o $@ $<
