RISCV_TEST_SRC ?= ./riscv-tests/Makefile
RISCV_TEST_DIR ?= ./riscv-tests
export RISCV_PREFIX ?= riscv64-unknown-elf-
$(RISCV_TEST_SRC):
	git submodule update --init
	touch $(@)
	cd $(RISCV_TEST_DIR); \
	git submodule update --init --recursive; \
	autoupdate; \
	autoconf; \
	./configure --prefix=$(TARGETDIR)
