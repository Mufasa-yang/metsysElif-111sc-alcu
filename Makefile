#NAME:Xiao Yang
#EMAIL:avadayang@icloud.com
#ID:104946787


.SILENT:

default:
	gcc -g -Wall -Wextra  -o  lab3a lab3a.c -lm
	
build: default

	
clean:
	rm -f lab3a *.tar.gz

dist: 
	tar -czf lab3a-104946787.tar.gz Makefile README  *.c


