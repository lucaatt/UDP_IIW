makeall:
	gcc -c ClientAdam.c
	gcc -c ServerAdam.c
	gcc -c shared_functions.c
	gcc -o client ClientAdam.o shared_functions.o -pthread
	gcc -o server ServerAdam.o shared_functions.o
cleanall:
	rm ClientAdam.o
	rm ServerAdam.o
	rm shared_functions.o
	rm client
	rm server
