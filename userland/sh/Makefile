# SzpontOS Almquist Unix Shell Makefile (/bin/sh ported from FreeBSD)
ROOT_DIR ?= $(abspath ../..)
include $(ROOT_DIR)/config.mk

TARGET   := $(ROOTFS_DIR)/bin/sh
SH_DIR   := $(ROOT_DIR)/userland/sh
BUILD_SH := $(BUILD_DIR)/userland/sh

SRCS := \
    alias.c \
    arith_yacc.c \
    arith_yylex.c \
    builtins.c \
    cd.c \
    echo.c \
    error.c \
    eval.c \
    exec.c \
    expand.c \
    histedit.c \
    input.c \
    jobs.c \
    kill.c \
    mail.c \
    main.c \
    memalloc.c \
    miscbltin.c \
    mystring.c \
    nodes.c \
    options.c \
    output.c \
    parser.c \
    printf.c \
    redir.c \
    show.c \
    syntax.c \
    test.c \
    trap.c \
    var.c

OBJS := $(patsubst %.c, $(BUILD_SH)/%.o, $(SRCS))

SH_CFLAGS := $(USER_CFLAGS) -DSHELL -DNO_HISTORY -I$(SH_DIR) -isystem $(abspath $(SYSROOT_DIR))/usr/include -I$(abspath $(SYSROOT_DIR))/usr/include

.PHONY: all sh clean

all: sh

sh: $(TARGET)

$(BUILD_SH)/%.o: $(SH_DIR)/%.c | $(BUILD_SH)
	@echo "  [CC]  $<"
	@$(CC) $(SH_CFLAGS) -c $< -o $@

$(BUILD_SH):
	@mkdir -p $(BUILD_SH)

$(TARGET): $(OBJS) $(CRT0_O) $(LIBC_SO) | $(ROOTFS_DIR)
	@mkdir -p $(ROOTFS_DIR)/bin
	@echo "  [LD]  $@"
	@$(CC) -O2 -ffreestanding -fno-builtin -nostdlib -B$(abspath $(SYSROOT_DIR))/usr/lib $(CRT0_O) $(OBJS) -L$(ROOTFS_DIR)/lib -L$(abspath $(SYSROOT_DIR))/usr/lib -lc -o $@
	@chmod +x $@

clean:
	@rm -rf $(BUILD_SH)
