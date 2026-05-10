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
EXTRA_LD = -lboost_program_options -lunwind

OPT = -O3 -g -std=c++14 -fomit-frame-pointer
CXXFLAGS = -std=c++11 -g  $(OPT) -I$(VERILATOR_INC) -I$(VERILATOR_DPI_INC) 
LIBS =  $(EXTRA_LD) -lpthread

DEP = $(OBJ:.o=.d)

EXE = test_fp_add

.PHONY : all clean

all: $(EXE)

$(EXE) : $(OBJ) obj_dir/Vfp_add__ALL.a
	$(CXX) $(CXXFLAGS) $(OBJ) obj_dir/*.o $(LIBS) -o $(EXE)

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
	rm -rf $(EXE) $(OBJ) $(DEP) obj_dir
