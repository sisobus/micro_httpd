BINDIR =	/usr/local/sbin
MANDIR =	/usr/local/man/man8
CC =		cc
CFLAGS =	-O -ansi -pedantic -U__STRICT_ANSI__ -Wall -Wpointer-arith -Wshadow -Wcast-qual -Wcast-align -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wredundant-decls -Wno-long-long -pthread
#SYSVLIBS =	-lnsl -lsocket
LDFLAGS =	-s $(SYSVLIBS)

all:		micro_httpd client

micro_httpd:	micro_httpd.o m_http.o helperlib.o queue.o
	$(CC) micro_httpd.o m_http.o helperlib.o queue.o $(LDFLAGS) -o micro_httpd

micro_httpd.o:	micro_httpd.c m_http.h helperlib.h queue.h
	$(CC) $(CFLAGS) -c micro_httpd.c

client:		client.o helperlib.o
	$(CC) client.o helperlib.o $(LDFLAGS) -o client

client.o:	client.c helperlib.h
	$(CC) $(CFLAGS) -c client.c

clean:
	rm -f micro_httpd *.o core core.* *.core client
