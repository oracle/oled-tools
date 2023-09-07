# Copyright (c) 2023, Oracle and/or its affiliates.
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

PYTHON_SITEDIR=$(shell python3 -c "import site; print(site.getsitepackages()[-1])")
MEMSTATE_LIB=$(DESTDIR)$(PYTHON_SITEDIR)/memstate_lib
CURLIB=$(CURDIR)/memstate_lib

all:
	@echo "all:$(CURDIR)"

clean:
	@echo "clean:$(CURDIR)"

install:
	@echo "install:$(CURDIR)"
	@echo "$(MEMSTATE_LIB)"
	mkdir -p $(MEMSTATE_LIB)
	install -m 755 $(CURDIR)/memstate.py $(BINDIR)/memstate
	install -m 755 $(CURLIB)/__init__.py $(MEMSTATE_LIB)/__init__.py
	install -m 755 $(CURLIB)/base.py $(MEMSTATE_LIB)/base.py
	install -m 755 $(CURLIB)/buddyinfo.py $(MEMSTATE_LIB)/buddyinfo.py
	install -m 755 $(CURLIB)/constants.py $(MEMSTATE_LIB)/constants.py
	install -m 755 $(CURLIB)/hugepages.py $(MEMSTATE_LIB)/hugepages.py
	install -m 755 $(CURLIB)/logfile.py $(MEMSTATE_LIB)/logfile.py
	install -m 755 $(CURLIB)/meminfo.py $(MEMSTATE_LIB)/meminfo.py
	install -m 755 $(CURLIB)/numa.py $(MEMSTATE_LIB)/numa.py
	install -m 755 $(CURLIB)/pss.py $(MEMSTATE_LIB)/pss.py
	install -m 755 $(CURLIB)/rss.py $(MEMSTATE_LIB)/rss.py
	install -m 755 $(CURLIB)/slabinfo.py $(MEMSTATE_LIB)/slabinfo.py
	install -m 755 $(CURLIB)/swap.py $(MEMSTATE_LIB)/swap.py
	gzip -c $(CURDIR)/memstate.man > $(MANDIR)/oled-memstate.8.gz
	chmod 644 $(MANDIR)/oled-memstate.8.gz
	@echo "memstate successfully installed!"

uninstall:
	@echo "uninstall:$(CURDIR)"
	rm -f $(BINDIR)/memstate
	rm -f $(MEMSTATE_LIB)/base.py
	rm -f $(MEMSTATE_LIB)/buddyinfo.py
	rm -f $(MEMSTATE_LIB)/constants.py
	rm -f $(MEMSTATE_LIB)/hugepages.py
	rm -f $(MEMSTATE_LIB)/logfile.py
	rm -f $(MEMSTATE_LIB)/meminfo.py
	rm -f $(MEMSTATE_LIB)/numa.py
	rm -f $(MEMSTATE_LIB)/pss.py
	rm -f $(MEMSTATE_LIB)/rss.py
	rm -f $(MEMSTATE_LIB)/slabinfo.py
	rm -f $(MEMSTATE_LIB)/swap.py
	rm -f $(MEMSTATE_LIB)/__init__.py
	rm -f $(MEMSTATE_LIB)/*.pyc
	rm -f $(MANDIR)/oled-memstate.8.gz
	rmdir $(MEMSTATE_LIB) || :
	@echo "memstate successfully uninstalled!"
