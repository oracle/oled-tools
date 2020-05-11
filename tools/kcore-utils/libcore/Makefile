subdirs := makedumpfile_1.5.7 makedumpfile_1.6.7
all:
	$(foreach dir,$(subdirs), make BINDIR=$(OLEDBIN) -C $(dir) all || exit 1;)
clean:
	$(foreach dir,$(subdirs), rm -f $(dir)/*.o; rm -f $(dir)/*.a;)
