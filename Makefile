CFLAGS+=-g
LDFLAGS+=`pkg-config --cflags --libs jack` -lpcap
version=0.0.3

all: hearnet grain.raw

%.raw: %.wav
	sox $< -r 44800 -s -w $@
clean: 
	rm -f hearnet *.o core*

dist: clean
	cd ..; tar czf hearnet-$(version).tar.gz hearnet
