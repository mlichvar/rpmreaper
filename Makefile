NAME = rpmreaper
VERSION = 0.1.5

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

rpm.o dep.o: override CPPFLAGS += -I/usr/include/rpm $(shell pkg-config \
	--atleast-version 4.5.90 rpm 2> /dev/null || echo -D_RPM_4_4_COMPAT)

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
	@$(CC) -MM $(CPPFLAGS) -MT '$(<:%.c=%.o) $@' $< -o $@

-include $(objs:%.o=.deps/%.d)
