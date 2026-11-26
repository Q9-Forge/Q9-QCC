# Private-QCC x86 CLib build.
#
# Each source becomes one ROF PSECT.  qclib.r is a concatenation accepted by
# qlx86's private-profile linker; it is deliberately not named qclib.l because
# the Microware indexed-library format has not been implemented for x86 yet.

ROOT       := $(abspath ../..)
QCC_RUN    := $(ROOT)/Q9-QCC
QCC        := $(QCC_RUN)/build/qcc
QLX86      := $(ROOT)/Q9-BACKEND-x86/q9-qlx86/build/qlx86
BUILD      := build
QIRX86     := $(ROOT)/Q9-BACKEND-x86/q9-qirx86/build/qirx86
QRX86      := $(ROOT)/Q9-BACKEND-x86/q9-qrx86/build/qrx86
QCIR       := $(ROOT)/Q9-FRONTEND-C/q9-qcir/build/qcir
QCPP       := $(ROOT)/Q9-FRONTEND-C/q9-qcpp/build/qcpp
COMPILERS  := $(QCC) $(QIRX86) $(QRX86) $(QCIR) $(QCPP) $(QCC_RUN)/config/qcc.conf
HEADERS    := $(wildcard $(ROOT)/Q9-FRONTEND-C/q9-qcpp/include/*.h) \
              $(wildcard $(ROOT)/Q9DEFS/include/*.h) \
              $(wildcard $(ROOT)/Q9DEFS/include/*/*.h) \
              $(wildcard $(ROOT)/Q9DEFS/include/*/*/*.h) \
              $(wildcard $(ROOT)/Q9DEFS/include/*/*/*/*.h)
SOURCES    := ctype string stdlib memory file format search inet calendar xdr
ROFS       := $(addprefix $(BUILD)/,$(addsuffix .r,$(SOURCES))) $(BUILD)/format_adapters.r

.PHONY: all inspect clean

all: $(BUILD)/qclib.r

$(BUILD):
	mkdir -p $@

$(BUILD)/%.r: src/%.c $(COMPILERS) $(HEADERS) | $(BUILD)
	cd $(QCC_RUN) && ./build/qcc --target OS9-x86 -c \
		--tmpdir $(abspath $(BUILD))/tmp-$* -o $(abspath $@) $(abspath $<)

$(BUILD)/format_adapters.r: src/format_adapters.sx86 $(QRX86) | $(BUILD)
	$(ROOT)/Q9-BACKEND-x86/q9-qrx86/build/qrx86 $< $@

$(BUILD)/qclib.r: $(ROFS)
	cat $(ROFS) > $@

inspect: $(BUILD)/qclib.r
	$(QLX86) --inspect $(BUILD)/qclib.r

clean:
	rm -rf $(BUILD)

# Experimental edition-1 OS-9000 path ABI; kept distinct from portable qclib.r.
.PHONY: runtime-candidate
runtime-candidate: $(BUILD)/qclib-os9000-candidate.r
$(BUILD)/os9trap.r: src/os9trap.sx86 $(QRX86) | $(BUILD)
	$(ROOT)/Q9-BACKEND-x86/q9-qrx86/build/qrx86 $< $@
$(BUILD)/qclib-os9000-candidate.r: $(BUILD)/qclib.r $(BUILD)/path_os9000.r $(BUILD)/syscalls_os9000.r $(BUILD)/process_os9000.r $(BUILD)/time_os9000.r $(BUILD)/fd_os9000.r $(BUILD)/os9trap.r
	cat $^ > $@
