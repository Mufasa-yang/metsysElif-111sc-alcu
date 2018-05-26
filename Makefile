# NAME: Xiao Yang,Zhengyuan Liu
# EMAIL: avadayang@icloud.com,zhengyuanliu@ucla.edu
# ID: 104946787,604945064


.SILENT:

default:
	gcc -g -Wall -Wextra  -o  lab3a lab3a.c -lm
	
build: default

	
clean:
	rm -f lab3a *.tar.gz

dist: 
	tar -czf lab3a-104946787.tar.gz Makefile README  *.c


