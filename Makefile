build: clear 
	gcc -std=gnu90 -lc main.c -o rac

debug: clear
	gcc -std=gnu90 -lc main.c -g -o rac

clear:
	rm -f rac

all: build

