CFLAGS+=-g
LDFLAGS+=`pkg-config --cflags --libs jack` -lpcap
version=0.0.9

all: hearnet grain.raw
	-sudo make suid

%.raw: %.wav
	sox $< -r 44800 -s -w $@
clean: 
	rm -f hearnet *.o core*

suid: hearnet
	chown root $<
	chmod u+s $<

dist: all
	darcs dist -d hearnet-$(version)

.PHONY: suid dist all clean
