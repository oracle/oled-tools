# Copyright (c) 2024, Oracle and/or its affiliates.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, see <https://www.gnu.org/licenses/>.
#
# Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
# or visit www.oracle.com if you need additional information or have any
# questions.

default: all

SUBDIRS := oomwatch-selinux

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	@echo "clean:$(CURDIR)"
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d clean; \
	done


oomwatch.8: oomwatch.8.scd
	scdoc < $< > $@

man: oomwatch.8

all: $(SUBDIRS)
	@echo "all:$(CURDIR)"
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d all; \
	done

install:
	@echo "install:$(CURDIR)"
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d install; \
	done
	install --mode=755 $(CURDIR)/oomwatch.py $(OLEDBINDIR)/oomwatch
	gzip -c $(CURDIR)/oomwatch.8 > $(MANDIR)/oled-oomwatch.8.gz
	chmod 644 $(MANDIR)/oled-oomwatch.8.gz
	install -D $(CURDIR)/config/oomwatch.template --mode=644 $(DESTDIR)/var/lib/pcp/config/pmieconf/oled/oomwatch
	install -D $(CURDIR)/config/oomwatch.json --mode=640 $(DESTDIR)/etc/oled/oomwatch.json
	install -D $(CURDIR)/config/99-pcp-oled-oomwatch --mode=440 $(DESTDIR)/etc/sudoers.d/99-pcp-oled-oomwatch
	#restorecon -vF /var/lib/pcp/config/pmieconf/oled/
	#restorecon -vF /var/lib/pcp/config/pmieconf/oled/oomwatch

uninstall:
	for d in $(SUBDIRS); do \
		$(MAKE) -C $$d uninstall; \
	done

	@echo "uninstall:$(CURDIR)"
	$(OLEDBINDIR)/oomwatch -d
	rm -f $(MANDIR)/oled-oomwatch.8.gz
	rm -f $(OLEDBINDIR)/oomwatch
	rm -f $(DESTDIR)/var/lib/pcp/config/pmieconf/oled
	rm -f $(DESTDIR)/etc/oled/oomwatch.json
	rm -f $(DESTDIR)/etc/sudoers.d/99-pcp-oled-oomwatch
