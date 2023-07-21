# makedumpfile

VERSION=1.5.7
DATE=18 Sep 2014

# Honour the environment variable CC
ifeq ($(strip $CC),)
CC	= gcc
endif

CFLAGS = -g -O2 -Wall -D_FILE_OFFSET_BITS=64 \
	  -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
	  -DVERSION='"$(VERSION)"' -DRELEASE_DATE='"$(DATE)"'
CFLAGS_ARCH	= -g -O2 -Wall -D_FILE_OFFSET_BITS=64 \
		    -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
# LDFLAGS = -L/usr/local/lib -I/usr/local/include

HOST_ARCH := $(shell uname -m)
# Use TARGET as the target architecture if specified.
# Defaults to uname -m
ifeq ($(strip($TARGET)),)
TARGET := $(HOST_ARCH)
endif

ARCH := $(shell echo ${TARGET}  | sed -e s/i.86/x86/ -e s/sun4u/sparc64/ \
			       -e s/arm.*/arm/ -e s/sa110/arm/ \
			       -e s/s390x/s390/ -e s/parisc64/parisc/ \
			       -e s/ppc64/powerpc64/ -e s/ppc/powerpc32/)

CROSS :=
ifneq ($(TARGET), $(HOST_ARCH))
CROSS := -U__$(HOST_ARCH)__
endif

CFLAGS += -D__$(ARCH)__ $(CROSS)
CFLAGS_ARCH += -D__$(ARCH)__ $(CROSS)

ifeq ($(ARCH), powerpc64)
CFLAGS += -m64
CFLAGS_ARCH += -m64
endif

ifeq ($(ARCH), powerpc32)
CFLAGS += -m32
CFLAGS_ARCH += -m32
endif

SRC_BASE = makedumpfile.h diskdump_mod.h sadump_mod.h sadump_info.h
SRC_PART = print_info.c dwarf_info.c elf_info.c erase_info.c sadump_info.c cache.c makedumpfile.c
OBJ_PART=$(patsubst %.c,%.o,$(SRC_PART))
SRC_ARCH = arch/arm.c arch/x86.c arch/x86_64.c arch/ia64.c arch/ppc64.c arch/s390x.c arch/ppc.c
OBJ_ARCH=$(patsubst %.c,%.o,$(SRC_ARCH))

LIBS = -ldw -lbz2 -lebl -ldl -lelf -lz
ifneq ($(LINKTYPE), dynamic)
LIBS := -static $(LIBS)
endif

ifeq ($(USELZO), on)
LIBS := -llzo2 $(LIBS)
CFLAGS += -DUSELZO
endif

ifeq ($(USESNAPPY), on)
LIBS := -lsnappy $(LIBS)
CFLAGS += -DUSESNAPPY
endif

all: libcore.a

$(OBJ_PART): $(SRC_PART)
	$(CC) $(CFLAGS) -c -o ./$@ $(VPATH)$(@:.o=.c)

$(OBJ_ARCH): $(SRC_ARCH)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_ARCH) -c -o ./$@ $(VPATH)$(@:.o=.c)

libcore.a: $(SRC_BASE) $(OBJ_PART) $(OBJ_ARCH)
	$(AR) r $@ $(OBJ_PART) $(OBJ_ARCH)

eppic_makedumpfile.so: extension_eppic.c
	$(CC) $(CFLAGS) $(LDFLAGS) -shared -rdynamic -o $@ extension_eppic.c -fPIC -leppic -ltinfo

clean:
	rm -f $(OBJ) $(OBJ_PART) $(OBJ_ARCH) libcore.a
