// Unified SP/DP checker for fpu_compare (fmt-selected). Tests LT/LE/EQ vs
// SoftFloat for both formats, value + invalid flag, plus a directed NaN sweep.
#include <verilated.h>
#include <cstdint>
#include <cstdio>
#include "Vfpu_compare.h"
extern "C" {
#include "softfloat.h"
}

enum { CMP_LT=1, CMP_LE=2, CMP_EQ=3 };
static const int D = 4;
static const char* pname[4] = {"?","LT","LE","EQ"};

static bool nan32(uint32_t u){ return ((u>>23)&0xff)==0xff && (u&0x7fffff); }
static bool nan64(uint64_t u){ return ((u>>52)&0x7ff)==0x7ff && (u&((1ULL<<52)-1)); }

int main(int argc, char**argv){
  const std::unique_ptr<VerilatedContext> ctx{new VerilatedContext};
  ctx->commandArgs(argc, argv);
  auto tb = new Vfpu_compare;
  tb->start = 1;
  long N = (argc>1)? atol(argv[1]) : 5000000;
  long tot_val=0, tot_ff=0;

  // ---------- single precision ----------
  for(int p=CMP_LT;p<=CMP_EQ;p++){
    tb->fmt=0; tb->cmp_type=p;
    uint32_t xa=3, xb=4; long checked=0, vm=0, fm=0, inv=0; int shown=0;
    for(long i=0;i<N;i++){
      uint32_t ua=xa; xa^=xa<<13; xa^=xa>>17; xa^=xa<<5;
      uint32_t ub=xb; xb^=xb<<13; xb^=xb>>17; xb^=xb<<5;
      tb->a=ua; tb->b=ub;
      for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      bool dut=tb->y; uint8_t df=tb->fflags;
      float32_t x,y; x.v=ua; y.v=ub; softfloat_exceptionFlags=0;
      bool ref = p==CMP_LT? f32_lt(x,y) : p==CMP_LE? f32_le(x,y) : f32_eq(x,y);
      uint8_t rf=softfloat_exceptionFlags&0x1f; checked++; if(rf&0x10) inv++;
      if(dut!=ref){ vm++; if(shown<6){shown++; printf("[SP %s VAL] a=%08x b=%08x dut=%d ref=%d\n",pname[p],ua,ub,dut,ref);} }
      if(df!=rf){ fm++; if(shown<6){shown++; printf("[SP %s FFLAG] a=%08x b=%08x df=%02x rf=%02x\n",pname[p],ua,ub,df,rf);} }
    }
    printf("SP %s: checked=%ld val=%ld fflag=%ld (invalid raised=%ld)\n",pname[p],checked,vm,fm,inv);
    tot_val+=vm; tot_ff+=fm;
  }

  // ---------- double precision ----------
  for(int p=CMP_LT;p<=CMP_EQ;p++){
    tb->fmt=1; tb->cmp_type=p;
    uint64_t xa=3, xb=4; long checked=0, vm=0, fm=0, inv=0; int shown=0;
    for(long i=0;i<N;i++){
      uint64_t ua=xa; xa^=xa<<13; xa^=xa>>7; xa^=xa<<17;
      uint64_t ub=xb; xb^=xb<<13; xb^=xb>>7; xb^=xb<<17;
      tb->a=ua; tb->b=ub;
      for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      bool dut=tb->y; uint8_t df=tb->fflags;
      float64_t x,y; x.v=ua; y.v=ub; softfloat_exceptionFlags=0;
      bool ref = p==CMP_LT? f64_lt(x,y) : p==CMP_LE? f64_le(x,y) : f64_eq(x,y);
      uint8_t rf=softfloat_exceptionFlags&0x1f; checked++; if(rf&0x10) inv++;
      if(dut!=ref){ vm++; if(shown<6){shown++; printf("[DP %s VAL] a=%016lx b=%016lx dut=%d ref=%d\n",pname[p],ua,ub,dut,ref);} }
      if(df!=rf){ fm++; if(shown<6){shown++; printf("[DP %s FFLAG] a=%016lx b=%016lx df=%02x rf=%02x\n",pname[p],ua,ub,df,rf);} }
    }
    printf("DP %s: checked=%ld val=%ld fflag=%ld (invalid raised=%ld)\n",pname[p],checked,vm,fm,inv);
    tot_val+=vm; tot_ff+=fm;
  }

  // ---------- directed NaN sweep (both formats) ----------
  long nd_mism=0, nd_checked=0; int nd_shown=0;
  // single
  { uint32_t xn=0x1234567; const uint32_t E1=0xff<<23, FM=0x7fffff, ONE=0x3f800000;
    tb->fmt=0;
    for(long t=0;t<150000;t++){
      uint32_t fr=xn&FM; xn^=xn<<13; xn^=xn>>17; xn^=xn<<5; if(!fr) fr=1;
      uint32_t nan=E1|fr; uint32_t oth=xn;
      uint32_t pa[3]={nan,nan,oth}, pb[3]={nan,ONE,nan};
      for(int q=0;q<3;q++) for(int p=CMP_LT;p<=CMP_EQ;p++){
        tb->cmp_type=p; tb->a=pa[q]; tb->b=pb[q];
        for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
        float32_t x,y; x.v=pa[q]; y.v=pb[q]; softfloat_exceptionFlags=0;
        bool ref=p==CMP_LT?f32_lt(x,y):p==CMP_LE?f32_le(x,y):f32_eq(x,y);
        uint8_t rf=softfloat_exceptionFlags&0x1f; nd_checked++;
        if((bool)tb->y!=ref || tb->fflags!=rf){ nd_mism++;
          if(nd_shown<10){nd_shown++; printf("[SP NaN %s] a=%08x b=%08x dut=%d ref=%d df=%02x rf=%02x\n",pname[p],pa[q],pb[q],(int)tb->y,ref,(int)tb->fflags,rf);} }
      }
    }
  }
  // double
  { uint64_t xn=0x123456789ULL; const uint64_t E1=0x7ffULL<<52, FM=(1ULL<<52)-1, ONE=0x3ff0000000000000ULL;
    tb->fmt=1;
    for(long t=0;t<150000;t++){
      uint64_t fr=xn&FM; xn^=xn<<13; xn^=xn>>7; xn^=xn<<17; if(!fr) fr=1;
      uint64_t nan=E1|fr; uint64_t oth=xn;
      uint64_t pa[3]={nan,nan,oth}, pb[3]={nan,ONE,nan};
      for(int q=0;q<3;q++) for(int p=CMP_LT;p<=CMP_EQ;p++){
        tb->cmp_type=p; tb->a=pa[q]; tb->b=pb[q];
        for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
        float64_t x,y; x.v=pa[q]; y.v=pb[q]; softfloat_exceptionFlags=0;
        bool ref=p==CMP_LT?f64_lt(x,y):p==CMP_LE?f64_le(x,y):f64_eq(x,y);
        uint8_t rf=softfloat_exceptionFlags&0x1f; nd_checked++;
        if((bool)tb->y!=ref || tb->fflags!=rf){ nd_mism++;
          if(nd_shown<10){nd_shown++; printf("[DP NaN %s] a=%016lx b=%016lx dut=%d ref=%d df=%02x rf=%02x\n",pname[p],pa[q],pb[q],(int)tb->y,ref,(int)tb->fflags,rf);} }
      }
    }
  }
  printf("directed-NaN: checked=%ld mism=%ld\n", nd_checked, nd_mism);
  printf("\nTOTAL: value=%ld fflag=%ld nan_dir=%ld\n", tot_val, tot_ff, nd_mism);
  delete tb;
  return 0;
}
