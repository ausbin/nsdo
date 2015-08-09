CFLAGS ?= -Wall -Werror -O3
PREFIX ?= /usr/local

PROG = nsdo
MANSECTION = 1
MANPAGE = $(PROG).$(MANSECTION)
MANPAGEGZ = $(MANPAGE).gz
README = README.md

.PHONY: all install clean

all: $(PROG) $(MANPAGEGZ) $(README)

install: $(PROG) $(MANPAGEGZ)
	install -Dm6755 $< $(PREFIX)/bin/$<
	install -Dm644 $(word 2,$^) $(PREFIX)/share/man/man$(MANSECTION)/$(word 2,$^)

clean:
	rm -fv $(PROG) $(MANPAGEGZ) $(README)

$(MANPAGEGZ): $(MANPAGE)
	gzip --best -k $<

$(README): readme.head $(MANPAGE)
	{ \
	  cat $<; \
	  echo; \
	  MANWIDTH=68 man -l $(word 2,$^) | sed 's/^/    /'; \
	} >$@
