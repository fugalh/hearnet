CFLAGS+=-g
LDFLAGS+=`pkg-config --cflags --libs jack` -lpcap
version=0.0.4

all: hearnet grain.raw

%.raw: %.wav
	sox $< -r 44800 -s -w $@
clean: 
	rm -f hearnet *.o core*

dist: all
	darcs dist -d hearnet-$(version)
