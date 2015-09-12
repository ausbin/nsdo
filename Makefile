CFLAGS ?= -Wall -Werror -O3
# if you're building a system package, you should probably set these to
# /usr, /usr/share/bash-completion/completions, and your package dir
PREFIX ?= /usr/local
BASH_COMPLETION_DIR ?= /etc/bash_completion.d
DESTDIR ?=

PROG = nsdo
MANSECTION = 1
MANPAGE = $(PROG).$(MANSECTION)
MANPAGEGZ = $(MANPAGE).gz
README = README.md

.PHONY: all install clean

all: $(PROG) $(MANPAGEGZ) $(README)

install: $(PROG) $(MANPAGEGZ)
	install -Dm4755 $< $(DESTDIR)$(PREFIX)/bin/$<
	install -Dm644 $(word 2,$^) $(DESTDIR)$(PREFIX)/share/man/man$(MANSECTION)/$(word 2,$^)
	install -Dm644 bash_completion/$(PROG) $(DESTDIR)$(BASH_COMPLETION_DIR)/$(PROG)

clean:
	rm -fv $(PROG) $(MANPAGEGZ) $(README)

$(MANPAGEGZ): $(MANPAGE)
	gzip --best -f -k $<

$(README): readme.head $(MANPAGE)
	{ \
	  cat $<; \
	  echo; \
	  MANWIDTH=68 man -l $(word 2,$^) | sed 's/^/    /'; \
	} >$@
