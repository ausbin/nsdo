CFLAGS ?= -D_GNU_SOURCE -O2 -pedantic -std=c89 -pedantic -Wall -Werror \
          -Wextra -Wstrict-prototypes -Wold-style-definition
# if you're building a system package, you should probably set these to
# /usr, /usr/share/bash-completion/completions, and your package dir.
# and the systemd unit uses $(PREFIX), so you'll need to set these for
# `make' as well as `sudo make install'
PREFIX ?= /usr/local
BASH_COMPLETION_DIR ?= /etc/bash_completion.d
DESTDIR ?=

PROG = nsdo
MANSECTION = 1
MANPAGE = $(PROG).$(MANSECTION)
MANPAGEGZ = $(MANPAGE).gz
SYSTEMD_UNIT=netns@.service
OPENCONNECT_SYSTEMD_UNIT=anyconnect/openconnect@.service
README = README.md

.PHONY: all install install-anyconnect install-openvpn clean

all: $(PROG) $(MANPAGEGZ) $(SYSTEMD_UNIT) $(OPENCONNECT_SYSTEMD_UNIT) $(README)

install: $(PROG) $(MANPAGEGZ) $(SYSTEMD_UNIT) bash_completion/$(PROG)
	install -Dm4755 $< $(DESTDIR)$(PREFIX)/bin/$<
	install -Dm755 netns $(DESTDIR)$(PREFIX)/bin/netns
	install -Dm644 $(word 2,$^) $(DESTDIR)$(PREFIX)/share/man/man$(MANSECTION)/$(word 2,$^)
	install -Dm644 $(word 3,$^) $(DESTDIR)$(PREFIX)/lib/systemd/system/$(word 3,$^)
	install -Dm644 $(word 4,$^) $(DESTDIR)$(BASH_COMPLETION_DIR)/$(PROG)

install-anyconnect: $(OPENCONNECT_SYSTEMD_UNIT) anyconnect/openconnect-wrapper anyconnect/vpnc-script-netns
	install -Dm644 $< $(DESTDIR)$(PREFIX)/lib/systemd/system/$(notdir $<)
	install -Dm755 $(word 2,$^) $(DESTDIR)$(PREFIX)/share/openconnect/$(notdir $(word 2,$^))
	install -Dm755 $(word 3,$^) $(DESTDIR)$(PREFIX)/share/openconnect/$(notdir $(word 3,$^))

install-openvpn: openvpn/50-netns.conf openvpn/openvpn-ns
	install -Dm644 $< $(DESTDIR)$(PREFIX)/lib/systemd/system/openvpn-client@.service.d/$(notdir $<)
	install -Dm755 $(word 2,$^) $(DESTDIR)$(PREFIX)/bin/$(notdir $(word 2,$^))

clean:
	rm -fv $(PROG) $(MANPAGEGZ) $(SYSTEMD_UNIT) $(OPENCONNECT_SYSTEMD_UNIT) $(README)

$(MANPAGEGZ): $(MANPAGE)
	gzip --best -f -k $<

%.service: %.service.proto
	sed 's|PREFIX|$(PREFIX)|g' <$< >$@

$(README): readme.head $(MANPAGE)
	{ \
	  cat $<; \
	  echo; \
	  MANWIDTH=68 man -l $(word 2,$^) | sed 's/^/    /'; \
	} >$@
