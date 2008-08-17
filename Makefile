# Make file for varmon created 
# Thu Jul 15 15:39:06 PDT 1999
# Author Dragan Stancevic <visitor@valinux.com>
#        Julien Danjou <julien@danjou.info>

varmon: varmon.c
	gcc $(CFLAGS) -o varmon varmon.c -W -Wall -lncurses $(LDFLAGS)

clean:
	rm -f *.o core varmon

install:
	chmod 700 varmon;
	install varmon /usr/sbin/;

