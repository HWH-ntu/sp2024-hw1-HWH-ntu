all:
# Build: gcc commaned
# echo "all: Please modify Makefile!"
	gcc server.c -D WRITE_SERVER -o write_server
	gcc server.c -D READ_SERVER -o read_server
clean:
# RM remove(delete): stuffs that gonna deleted
# echo "clean: Please modify Makefile!"
	rm -v write_server read_server