build: clear 
	gcc -lc main.c -o main

debug: clear
	gcc -lc main.c -g -o main

clear:
	rm -f main

all: build

run: build
	./main main.c "cat main.c"

run-debug: debug
	gdb ./main main.c "cat main.c"
