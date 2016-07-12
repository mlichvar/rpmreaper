NAME = rpmreaper
VERSION = 0.2.0

EXTRA_CFLAGS = -O -g -Wall
CFLAGS = $(shell pkg-config --cflags rpm ncurses) $(EXTRA_CFLAGS)
LDFLAGS = $(shell pkg-config --libs rpm ncurses) $(EXTRA_LDFLAGS)

prefix = /usr/local
bindir = $(prefix)/bin
mandir = $(prefix)/share/man
man1dir = $(mandir)/man1

objs = $(patsubst %.c,%.o,$(wildcard *.c))

all: $(NAME)

clean:
	-rm -rf $(NAME) *.o .deps

$(NAME): $(objs)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

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
