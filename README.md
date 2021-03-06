# micro_httpd
2015 distributed system assignment#2

             micro_httpd - really small HTTP server

micro_httpd is a very small HTTP server. It runs from inetd, which
means its performance is poor. But for low-traffic sites, it's
quite adequate. It implements all the basic features of an HTTP
server, including:

  *  Security against ".." filename snooping.
  *  The common MIME types.
  *  Trailing-slash redirection.
  *  index.html
  *  Directory listings.

All in about 200 lines of code.

See the manual entry for more details.

Files in this distribution:

    README              this
    Makefile            guess
    micro_httpd.c       source file
    micro_httpd.8       manual entry

To build: If you're on a SysV-like machine (which includes old Linux systems
but not new Linux systems), edit the Makefile and uncomment the SYSVLIBS line.
Otherwise, just do a make.

Feedback is welcome - send bug reports, enhancements, checks, money
orders, etc. to the addresses below.

    Jef Poskanzer  jef@mail.acme.com  http://www.acme.com/jef/
