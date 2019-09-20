makeall:
	gcc -c Client.c
	gcc -c Server.c
	gcc -c shared_functions.c
	gcc -c thread_functions.c
	gcc -o client Client.o shared_functions.o thread_functions.o -pthread -lrt
	gcc -o server Server.o shared_functions.o thread_functions.o -pthread -lrt
cleanall:
	rm Client.o
	rm Server.o
	rm shared_functions.o
	rm thread_functions.o
	rm client
	rm server
