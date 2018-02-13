LINK_C_PROG=$(CC) -c -std=c99 -Werror $^
BILD_C_PROG=$(CC) $(LDFLAGS) $^

head: head.o reader.o
	$(BUILD_C_PROG)

tail: tail.o reader.o
	$(BUILD_C_PROG)

cp: cp.o reader.o
	$(BUILD_C_PROG)

pwc: pwc.o
	$(BUILD_C_PROG)

head.o: head.c
	$(LINK_C_PROG)

tail.o: tail.c
	$(LINK_C_PROG)

cp.o: cp.c
	$(LINK_C_PROG)

reader.o: reader.c
	$(LINK_C_PROG)

pwc.o: pwc.c
	$(LINK_C_PROG)

clean:
	rm -f *.o
