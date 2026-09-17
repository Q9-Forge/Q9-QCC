HOST_MAKE ?= /usr/bin/make

all:
	cd q9-qirv32 && $(HOST_MAKE)
	cd q9-qov32 && $(HOST_MAKE)
	cd q9-qlv32 && $(HOST_MAKE)

clean:
	cd q9-qirv32 && $(HOST_MAKE) clean
	cd q9-qov32 && $(HOST_MAKE) clean
	cd q9-qlv32 && $(HOST_MAKE) clean

test:
	@echo "Testing RISC-V backend pipeline..."
	@/Volumes/SSD1TB/projects/Q9-Forge/Q9-QCC/Q9-BACKEND-riscv/q9-qirv32/build/qirv32 /tmp/test_add.ir /tmp/test_add.srv32 2>&1
	@/Volumes/SSD1TB/projects/Q9-Forge/Q9-QCC/Q9-BACKEND-riscv/q9-qov32/build/qov32 /tmp/test_add.srv32 /tmp/test_add.r 2>&1 | grep "Generated"
	@/Volumes/SSD1TB/projects/Q9-Forge/Q9-QCC/Q9-BACKEND-riscv/q9-qlv32/build/qlv32 /tmp/test_add.r -O=/tmp/test_add.module 2>&1 | grep "OK"
	@echo "✓ Pipeline test passed"

.PHONY: all clean test
