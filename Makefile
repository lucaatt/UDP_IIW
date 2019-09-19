makeall:
	gcc -c Client.c
	gcc -c Server.c
	gcc -c shared_functions.c
	gcc -o client Client.o shared_functions.o -pthread -lrt
	gcc -o server Server.o shared_functions.o -pthread
cleanall:
	rm Client.o
	rm Server.o
	rm shared_functions.o
	rm client
	rm server
