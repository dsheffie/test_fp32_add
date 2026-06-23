UNAME_S = $(shell uname -s)

OBJ = top.o verilated.o

SV_SRC = fp_add.sv count_leading_zeros.sv

CXX = clang++-14 -flto
MAKE = make
VERILATOR_SRC = /home/dsheffie/local/share/verilator/include/verilated.cpp
VERILATOR_FST = /home/dsheffie/local/share/verilator/include/verilated_fst_c.cpp
VERILATOR_INC = /home/dsheffie/local/share/verilator/include
VERILATOR_DPI_INC = /home/dsheffie/local/share/verilator/include/vltstd/
VERILATOR = /home/dsheffie/local/bin/verilator
SOFTFLOAT = /home/dsheffie/rv64-linux-apps/SoftFloat-3e
SOFTFLOAT_INC = $(SOFTFLOAT)/source/include
SOFTFLOAT_LIB = $(SOFTFLOAT)/build/Linux-x86_64-GCC/softfloat.a
EXTRA_LD = -lboost_program_options -lunwind

OPT = -O3 -g -std=c++14 -fomit-frame-pointer
CXXFLAGS = -std=c++11 -g  $(OPT) -I$(VERILATOR_INC) -I$(VERILATOR_DPI_INC) -I$(SOFTFLOAT_INC)
LIBS =  $(SOFTFLOAT_LIB) $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)

EXE = test_fp_add
MUL_EXE = test_fp_mul
ADD64_EXE = test_fp_add64
MUL64_EXE = test_fp_mul64
FPU_ADD_EXE = test_fpu_add
FPU_MUL_EXE = test_fpu_mul
CMP_EXE = test_fp_compare
CMP64_EXE = test_fp_compare64

.PHONY : all clean

all: $(EXE) $(MUL_EXE) $(ADD64_EXE) $(MUL64_EXE) $(FPU_ADD_EXE) $(FPU_MUL_EXE) $(CMP_EXE) $(CMP64_EXE)

$(EXE) : $(OBJ) obj_dir/Vfp_add__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group $(OBJ) obj_dir/*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(EXE)

# multiplier test: isolated --Mdir so its verilated objects don't collide with
# the adder's obj_dir/*.o glob above.
$(MUL_EXE) : analyze_mul.o verilated.o obj_mul/Vfp_mul__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group analyze_mul.o verilated.o obj_mul/Vfp_mul*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(MUL_EXE)

analyze_mul.o: analyze_mul.cc obj_mul/Vfp_mul__ALL.a
	$(CXX) -MMD $(CXXFLAGS) -DHAVE_DENORM -Iobj_mul -c $<

obj_mul/Vfp_mul__ALL.a : fp_mul.sv
	$(VERILATOR) --top-module fp_mul --Mdir obj_mul --x-assign unique -cc fp_mul.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_mul -f Vfp_mul.mk

# double-precision (W=64) tests: same analyzer (analyze64.cc), top param
# overridden with -GW=64, each in its own --Mdir.
$(ADD64_EXE) : a64_add.o verilated.o obj_add64/Vfp_add__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group a64_add.o verilated.o obj_add64/Vfp_add*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(ADD64_EXE)

a64_add.o: analyze64.cc obj_add64/Vfp_add__ALL.a
	$(CXX) $(CXXFLAGS) -Iobj_add64 -c $< -o $@

obj_add64/Vfp_add__ALL.a : fp_add.sv count_leading_zeros.sv
	$(VERILATOR) --top-module fp_add -GW=64 --Mdir obj_add64 --x-assign unique -cc fp_add.sv count_leading_zeros.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_add64 -f Vfp_add.mk

$(MUL64_EXE) : a64_mul.o verilated.o obj_mul64/Vfp_mul__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group a64_mul.o verilated.o obj_mul64/Vfp_mul*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(MUL64_EXE)

a64_mul.o: analyze64.cc obj_mul64/Vfp_mul__ALL.a
	$(CXX) $(CXXFLAGS) -DDO_MUL -Iobj_mul64 -c $< -o $@

obj_mul64/Vfp_mul__ALL.a : fp_mul.sv
	$(VERILATOR) --top-module fp_mul -GW=64 --Mdir obj_mul64 --x-assign unique -cc fp_mul.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_mul64 -f Vfp_mul.mk

# unified single/double-precision units (one fmt-selected datapath each)
$(FPU_ADD_EXE) : af_add.o verilated.o obj_fpu_add/Vfpu_add__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group af_add.o verilated.o obj_fpu_add/Vfpu_add*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(FPU_ADD_EXE)

af_add.o: analyze_fpu.cc obj_fpu_add/Vfpu_add__ALL.a
	$(CXX) $(CXXFLAGS) -Iobj_fpu_add -c $< -o $@

obj_fpu_add/Vfpu_add__ALL.a : fpu_add.sv count_leading_zeros.sv
	$(VERILATOR) --top-module fpu_add --Mdir obj_fpu_add --x-assign unique -cc fpu_add.sv count_leading_zeros.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_fpu_add -f Vfpu_add.mk

$(FPU_MUL_EXE) : af_mul.o verilated.o obj_fpu_mul/Vfpu_mul__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group af_mul.o verilated.o obj_fpu_mul/Vfpu_mul*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(FPU_MUL_EXE)

af_mul.o: analyze_fpu.cc obj_fpu_mul/Vfpu_mul__ALL.a
	$(CXX) $(CXXFLAGS) -DDO_MUL -Iobj_fpu_mul -c $< -o $@

obj_fpu_mul/Vfpu_mul__ALL.a : fpu_mul.sv
	$(VERILATOR) --top-module fpu_mul --Mdir obj_fpu_mul --x-assign unique -cc fpu_mul.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_fpu_mul -f Vfpu_mul.mk

# fp_compare (single + double via -GW=64); needs fp_special_cases.sv + the header
$(CMP_EXE) : acmp.o verilated.o obj_cmp/Vfp_compare__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group acmp.o verilated.o obj_cmp/Vfp_compare*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(CMP_EXE)

acmp.o: analyze_cmp.cc obj_cmp/Vfp_compare__ALL.a
	$(CXX) $(CXXFLAGS) -Iobj_cmp -c $< -o $@

obj_cmp/Vfp_compare__ALL.a : fp_compare.sv fp_compare.vh fp_special_cases.sv
	$(VERILATOR) --top-module fp_compare --Mdir obj_cmp --x-assign unique -cc fp_compare.sv fp_special_cases.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_cmp -f Vfp_compare.mk

$(CMP64_EXE) : acmp64.o verilated.o obj_cmp64/Vfp_compare__ALL.a
	$(CXX) $(CXXFLAGS) -Wl,--start-group acmp64.o verilated.o obj_cmp64/Vfp_compare*.o $(SOFTFLOAT_LIB) -Wl,--end-group $(EXTRA_LD) -lpthread -o $(CMP64_EXE)

acmp64.o: analyze_cmp.cc obj_cmp64/Vfp_compare__ALL.a
	$(CXX) $(CXXFLAGS) -DDO_DP -Iobj_cmp64 -c $< -o $@

obj_cmp64/Vfp_compare__ALL.a : fp_compare.sv fp_compare.vh fp_special_cases.sv
	$(VERILATOR) --top-module fp_compare -GW=64 --Mdir obj_cmp64 --x-assign unique -cc fp_compare.sv fp_special_cases.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_cmp64 -f Vfp_compare.mk

top.o: top.cc obj_dir/Vfp_add__ALL.a
	$(CXX) -MMD $(CXXFLAGS) -Iobj_dir -c $<

verilated.o: $(VERILATOR_SRC)
	$(CXX) -MMD $(CXXFLAGS) -c $< 

verilated_fst_c.o: $(VERILATOR_FST)
	$(CXX) -MMD $(CXXFLAGS) -c $< 

%.o: %.cc
	$(CXX) -MMD $(CXXFLAGS) -c $< 

obj_dir/Vfp_add__ALL.a : $(SV_SRC)
	$(VERILATOR) --x-assign unique -cc fp_add.sv
	$(MAKE) OPT_FAST="-O3 -flto" -C obj_dir -f Vfp_add.mk


-include $(DEP)

clean:
	rm -rf $(EXE) $(MUL_EXE) $(ADD64_EXE) $(MUL64_EXE) $(FPU_ADD_EXE) $(FPU_MUL_EXE) \
	  $(CMP_EXE) $(CMP64_EXE) $(OBJ) \
	  analyze_mul.o analyze_mul.d a64_add.o a64_mul.o af_add.o af_mul.o acmp.o acmp64.o $(DEP) \
	  obj_dir obj_mul obj_add64 obj_mul64 obj_fpu_add obj_fpu_mul obj_cmp obj_cmp64
