VERSION = 0.1.3

CFLAGS = -O -g -Wall
LDFLAGS = -lrpmbuild -lncurses

prefix = /usr/local
bindir = $(prefix)/bin
mandir = $(prefix)/man
man1dir = $(mandir)/man1

objs = $(patsubst %.c,%.o,$(wildcard *.c))

all: rpmreaper

clean:
	-rm -rf rpmreaper *.o .deps

rpmreaper: $(objs)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

rpm.o dep.o: CFLAGS += -Wno-return-type
rpm.o dep.o: CPPFLAGS += -I/usr/include/rpm

install: rpmreaper
	mkdir -p $(bindir) $(man1dir)
	install rpmreaper $(bindir)
	install -p -m 644 rpmreaper.1 $(man1dir)

archive:
	@git archive -v --prefix=rpmreaper-$(VERSION)/ v$(VERSION) | \
		tar --delete rpmreaper-$(VERSION)/.gitignore | \
		gzip -9 > rpmreaper-$(VERSION).tar.gz

.deps:
	@mkdir .deps

.deps/%.d: %.c .deps
	@$(CC) -MM $(CPPFLAGS) $< | \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@

-include $(objs:%.o=.deps/%.d)
