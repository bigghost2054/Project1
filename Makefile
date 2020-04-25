all: project1_main.c
	gcc project1_main.c -o project1.out
clean:
	rm -f project1.out
