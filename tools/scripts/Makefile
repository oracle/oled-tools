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

all:
	@echo "all:$(CURDIR)"

clean:
	@echo "clean:$(CURDIR)"

scripts.8: scripts.8.scd
	scdoc < $< > $@

# Whenever scripts.8.scd is modified, `make man` must be run and the resulting
# changes to scripts.8 must be committed together with scripts.8.scd
man: scripts.8

install:
	@echo "install:$(CURDIR)"
	install --mode=755 $(CURDIR)/scripts.py $(OLEDBINDIR)/scripts
	gzip -c $(CURDIR)/scripts.8 > $(MANDIR)/oled-scripts.8.gz
	chmod 644 $(MANDIR)/oled-scripts.8.gz
	install -D $(CURDIR)/oled-tools-scripts.service \
		$(DESTDIR)/usr/lib/systemd/system/oled-tools-scripts.service
	install -D $(CURDIR)/startup-scripts.conf \
		$(DESTDIR)/usr/libexec/oled-tools/scripts.d/startup-scripts.conf
	@echo "SCRIPTS successfully installed!"

uninstall:
	@echo "uninstall:$(CURDIR)"
	rm -f $(MANDIR)/oled-scripts.8.gz
	rm -f $(OLEDBINDIR)/scripts
	rm -f $(OLEDBIN)/scripts.d/startup-scripts.conf
	@echo "SCRIPTS successfully uninstalled!"

