# Copyright (c) 2022, Oracle and/or its affiliates.
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

syswatch.8: syswatch.8.scd
	scdoc < $< > $@

# Whenever syswatch.8.scd is modified, `make man` must be run and the resulting
# changes to syswatch.8 must be committed together with syswatch.8.scd
man: syswatch.8

install:
	@echo "install:$(CURDIR)"
	install --mode=755 $(CURDIR)/syswatch $(BINDIR)/syswatch
	gzip -c $(CURDIR)/syswatch.8 > $(MANDIR)/oled-syswatch.8.gz
	chmod 644 $(MANDIR)/oled-syswatch.8.gz
	@echo "SYSWATCH successfully installed!"

uninstall:
	@echo "uninstall:$(CURDIR)"
	rm -f $(MANDIR)/oled-syswatch.8.gz
	rm -f $(BINDIR)/syswatch
	@echo "SYSWATCH successfully uninstalled!"
