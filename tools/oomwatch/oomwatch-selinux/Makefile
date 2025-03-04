IAM = pcp-oomwatch

default: all

all: $(IAM).pp

$(IAM).pp: $(IAM).te $(IAM).fc
	$(MAKE) -f /usr/share/selinux/devel/Makefile $(IAM).pp

clean:
	rm -f $(IAM).pp $(IAM).if
	rm -rf tmp

install:
	#@echo "running semodule -i"
	#semodule -X300 -i pcp-oomwatch.pp
	bzip2 -9 $(IAM).pp
	install --mode=644 -D $(CURDIR)/$(IAM).pp.bz2 $(DESTDIR)/usr/share/selinux/packages/targeted/$(IAM).pp.bz2

uninstall:
	@echo "running semodule -r"
	semodule -X300 -r pcp-oomwatch

