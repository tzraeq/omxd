omxd: omxd.c playlist.c omxd.h client.c Makefile
	gcc -g -o omxd omxd.c playlist.c client.c
omxd.1: README
	curl -F page=@README http://mantastic.herokuapp.com > omxd.1
install:
	-cp omxd /usr/bin
	perl -pe '$$o=1 if /omxd/; print "omxd\n" if !$$o && /exit 0/' -i /etc/rc.local
	cp omxd.1 /usr/share/man/man1/
uninstall:
	rm /usr/bin/omxd
	perl -ne 'print unless /omxd/' -i /etc/rc.local
	rm /usr/share/man/man1/omxd.1 
