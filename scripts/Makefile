# Copyright (c) 2020, Oracle and/or its affiliates.
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

SCRIPTSDIR := $(BINDIR)/scripts
DOCSDIR := $(BINDIR)/scripts/docs

all:
	@echo "all:$(CURDIR)"

clean:
	@echo "clean:$(CURDIR)"

install:
	@echo "install:$(CURDIR)"
	mkdir -p $(SCRIPTSDIR)
	mkdir -p $(DOCSDIR)
#net
	install -m 755 $(CURDIR)/net/arp_origin.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/arp_origin_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_bcopy_metric.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_bcopy_metric_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_check_tx_stall.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_check_tx_stall_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_conn2irq.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_conn2irq_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_egress_TP.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_egress_TP_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_rdma_lat.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_rdma_lat_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_rdma_xfer_rate.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_rdma_xfer_rate_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/rds_tx_funccount.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/rds_tx_funccount_example.txt $(DOCSDIR)
	install -m 755 $(CURDIR)/net/ping_lat.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/net/ping_lat_example.txt $(DOCSDIR)
#core
	install -m 755 $(CURDIR)/core/spinlock_time.d $(SCRIPTSDIR)
	install -m 755 $(CURDIR)/core/spinlock_time_example.txt $(DOCSDIR)
	@echo "scripts successfully installed"

uninstall:
	@echo "uninstall:$(CURDIR)"
	rm -rf $(SCRIPTSDIR)
	rm -rf $(DOCSDIR)
	@echo "scripts successfully uninstalled"
