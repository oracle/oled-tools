# Copyright (c) 2021, Oracle and/or its affiliates.
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

LKCEDIR := $(OLEDDIR)/lkce
LKCE_KDUMPDIR := $(OLEDDIR)/lkce/lkce_kdump.d

all:
	@echo "all:$(CURDIR)"

clean:
	@echo "clean:$(CURDIR)"

install:
	@echo "install:$(CURDIR)"
	mkdir -p $(LKCEDIR)
	mkdir -p $(LKCE_KDUMPDIR)
	mkdir -p $(MANDIR)
	install -m 755 $(CURDIR)/scripts/lkce.py $(BINDIR)/lkce
	install -m 755 $(CURDIR)/scripts/kdump_report.py $(LKCE_KDUMPDIR)/kdump_report
	gzip -c $(CURDIR)/lkce.man > $(MANDIR)/oled-lkce.8.gz
	chmod 644 $(MANDIR)/oled-lkce.8.gz
	@echo "lkce successfully installed"

uninstall:
	@echo "uninstall:$(CURDIR)"
	$(BINDIR)/lkce disable || :
	rm -rf $(LKCE_KDUMPDIR)
	rm -rf $(LKCEDIR)
	rm -f $(BINDIR)/lkce
	rm -f $(MANDIR)/oled-lkce.8.gz
	@echo "lkce successfully uninstalled"
