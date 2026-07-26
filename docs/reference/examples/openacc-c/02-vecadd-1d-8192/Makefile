# 02-vecadd-1d-8192: c[i] = a[i] + b[i] (1D, 4096 PE)
#
# make              -> S2S: input.c -> output.c + .cl + .stparam, MNCL: .cl -> ._vsm
# make build-e2e    -> + vsmlink + assemble3 + host C
# make test         -> 使い方を表示
# make test-emu-lib -> emu:lib backend で実行・検証
# make test-device  -> device (実機) backend で実行・検証
# make verify       -> emu:lib で plain C と出力を比較
#
# 前提: source activate 済み

SDK_ROOT ?= $(error SDK_ROOT が未設定。source activate を実行してください)
BUILDDIR = _build

CL_EXT = -cl-ext=-all,+cl_khr_byte_addressable_store,+cl_khr_global_int32_base_atomics,+cl_khr_global_int32_extended_atomics,+cl_khr_local_int32_base_atomics,+cl_khr_local_int32_extended_atomics,+cl_khr_fp64,+cl_khr_int64_base_atomics,+cl_khr_int64_extended_atomics,+cl_nv_device_attribute_query,+cl_khr_spir

.PHONY: all build-e2e test test-emu-lib test-device verify clean

# --- default: S2S + MNCL compile ---
all: $(BUILDDIR)/vecadd._vsm

$(BUILDDIR)/output.c $(BUILDDIR)/vecadd.cl $(BUILDDIR)/vecadd.stparam: input.c | $(BUILDDIR)
	openacc2mncore.sh $< vecadd

$(BUILDDIR)/vecadd._vsm: $(BUILDDIR)/vecadd.cl
	clang -cc1 -O1 \
		-triple=mncore2-unknown-unknown -target-cpu mncore2 \
		-cl-std=CL2.0 $(CL_EXT) \
		-fno-builtin \
		-include $(SDK_ROOT)/include/mncl/opencl-c.h \
		-S $< -o $@

# --- build-e2e: vsmlink + assemble3 + host C ---
build-e2e: $(BUILDDIR)/vecadd.asm $(BUILDDIR)/vecadd.idma.dat $(BUILDDIR)/output

$(BUILDDIR)/vecadd.vsm: $(BUILDDIR)/vecadd._vsm vecadd.param $(BUILDDIR)/vecadd.stparam
	vsmlink $(BUILDDIR)/vecadd._vsm vecadd.param $(BUILDDIR)/vecadd.stparam $@

$(BUILDDIR)/vecadd.asm: $(BUILDDIR)/vecadd.vsm
	assemble3 $< --output-file $@

$(BUILDDIR)/vecadd.idma.dat: $(BUILDDIR)/vecadd.vsm
	assemble3 $< --output-file $(BUILDDIR)/vecadd --loader

$(BUILDDIR)/output.o: $(BUILDDIR)/output.c
	gcc -Wall -Wextra -I$(SDK_ROOT)/include -I$(BUILDDIR) -c $< -o $@

$(BUILDDIR)/output: $(BUILDDIR)/output.o
	gcc $< -o $@ -L$(SDK_ROOT)/lib/emu-lib -L$(SDK_ROOT)/lib -lmnc2 -lgpfn3 -lstdc++ -lpthread -lm

# --- ref: plain C ---
$(BUILDDIR)/ref: input.c | $(BUILDDIR)
	gcc -Wall -Wextra -o $@ $< -lm

# --- test: 使い方 (mncl 流の 3 target ルール) ---
test:
	@echo "使い方: make test-emu-lib (host emu) / make test-device (実機)"

test-emu-lib: $(BUILDDIR)/output $(BUILDDIR)/vecadd.asm $(BUILDDIR)/vecadd.idma.dat
	LD_LIBRARY_PATH=$(SDK_ROOT)/lib/emu-lib ./$(BUILDDIR)/output

test-device: $(BUILDDIR)/output $(BUILDDIR)/vecadd.asm $(BUILDDIR)/vecadd.idma.dat
	LD_LIBRARY_PATH=$(SDK_ROOT)/lib ./$(BUILDDIR)/output

# --- verify: compare ref vs mncore2 ---
verify: $(BUILDDIR)/ref $(BUILDDIR)/output $(BUILDDIR)/vecadd.asm $(BUILDDIR)/vecadd.idma.dat
	./$(BUILDDIR)/ref > $(BUILDDIR)/ref.out 2>&1
	LD_LIBRARY_PATH=$(SDK_ROOT)/lib/emu-lib ./$(BUILDDIR)/output > $(BUILDDIR)/mncore.out 2>&1
	diff $(BUILDDIR)/ref.out $(BUILDDIR)/mncore.out
	@echo "VERIFY OK: ref and mncore2 outputs match"

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)
