NAME = rpmreaper
VERSION = 0.1.4

CFLAGS = -O -g -Wall
LDFLAGS = -lrpmbuild -lncurses

prefix = /usr/local
bindir = $(prefix)/bin
mandir = $(prefix)/man
man1dir = $(mandir)/man1

objs = $(patsubst %.c,%.o,$(wildcard *.c))

all: $(NAME)

clean:
	-rm -rf $(NAME) *.o .deps

$(NAME): $(objs)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

rpm.o dep.o: CFLAGS += -Wno-return-type
rpm.o dep.o: CPPFLAGS += -I/usr/include/rpm

install: $(NAME)
	mkdir -p $(bindir) $(man1dir)
	install $(NAME) $(bindir)
	install -p -m 644 $(NAME).1 $(man1dir)

archive:
	@git archive -v --prefix=$(NAME)-$(VERSION)/ v$(VERSION) | \
		tar --delete $(NAME)-$(VERSION)/.gitignore | \
		gzip -9 > $(NAME)-$(VERSION).tar.gz

.deps:
	@mkdir .deps

.deps/%.d: %.c .deps
	@$(CC) -MM $(CPPFLAGS) $< | \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' > $@

-include $(objs:%.o=.deps/%.d)
