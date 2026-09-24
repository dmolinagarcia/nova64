#===============================================================================
# novafs — host build of the filesystem libraries, their tool and their tests
#
#   make            library, fat32tool and unit tests
#   make test       unit tests, then the image-based tests (python3, mtools,
#                   dosfstools)
#   make san        the same tests under AddressSanitizer and UBSan
#   make check16    type-check the portable sources for a 16-bit-int target
#   make clean
#===============================================================================

CC      ?= cc
AR      ?= ar
OPT     ?= -O2 -g
WARN    := -std=c99 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
           -Wstrict-prototypes -Wmissing-prototypes
CFLAGS  += $(OPT) $(WARN) -Icommon -Ifat32
BUILD   ?= build

# Portable sources: everything except the host backend.
PORT_SRCS := common/bdev.c common/fs_err.c common/mbr.c \
             fat32/fat32_ondisk.c fat32/fat32_fat.c fat32/fat32_lfn.c \
             fat32/fat32_dir.c fat32/fat32_file.c fat32/fat32_fs.c
LIB_SRCS  := $(PORT_SRCS) common/bdev_file.c
LIB_OBJS  := $(LIB_SRCS:%.c=$(BUILD)/%.o)
LIB       := $(BUILD)/libnovafs_fat32.a

TOOL      := $(BUILD)/fat32tool
UNIT      := $(BUILD)/test_unit

all: $(LIB) $(TOOL) $(UNIT)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(TOOL): $(BUILD)/tools/fat32tool.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(UNIT): $(BUILD)/tests/unit/test_fat32.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: all
	$(UNIT)
	python3 tests/run_tests.py --tool $(TOOL)

SAN := -fsanitize=address,undefined -fno-sanitize-recover=all

san:
	$(MAKE) BUILD=build-san OPT="-O1 -g -fno-omit-frame-pointer $(SAN)" \
	        LDFLAGS="$(SAN)" test

# clang with a 16-bit int and 16-bit size_t catches every implicit
# promotion that would be wrong on the machine (DN-FS-FUSE-001 §4.7).
check16:
	clang --target=msp430-none-elf -ffreestanding -fsyntax-only \
	      -DNOVAFS_FREESTANDING -DFAT_CFG_MAX_SECTOR=512u \
	      -DFAT_CFG_FAT_CACHE=2u \
	      $(WARN) -Werror -Itests/port16 -Icommon -Ifat32 $(PORT_SRCS)

clean:
	rm -rf build build-san

.PHONY: all test san check16 clean

-include $(LIB_OBJS:.o=.d) $(BUILD)/tools/fat32tool.d $(BUILD)/tests/unit/test_fat32.d
