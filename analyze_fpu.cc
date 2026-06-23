// Unified SP/DP checker for fpu_add / fpu_mul (one datapath, fmt-selected).
#include <verilated.h>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
extern "C" {
#include "softfloat.h"
}

#ifdef DO_MUL
#include "Vfpu_mul.h"
typedef Vfpu_mul Dut;
static const int LAT = 4;
static float32_t op32(float32_t a, float32_t b){ return f32_mul(a,b); }
static float64_t op64(float64_t a, float64_t b){ return f64_mul(a,b); }
#else
#include "Vfpu_add.h"
typedef Vfpu_add Dut;
static const int LAT = 2;
static float32_t op32(float32_t a, float32_t b){ return f32_add(a,b); }
static float64_t op64(float64_t a, float64_t b){ return f64_add(a,b); }
#endif
static int sf_rm(int m){
  switch(m){ case 0:return softfloat_round_near_even; case 1:return softfloat_round_minMag;
             case 2:return softfloat_round_max; default:return softfloat_round_min; }
}
static const char* rmname[4] = {"RN","RZ","RP","RM"};

template<class T> struct FC;
template<> struct FC<uint32_t>{ static int e(uint32_t u){return (u>>23)&0xff;} static uint32_t f(uint32_t u){return u&0x7fffff;} static int allone(){return 0xff;} };
template<> struct FC<uint64_t>{ static int e(uint64_t u){return (u>>52)&0x7ff;} static uint64_t f(uint64_t u){return u&((1ULL<<52)-1);} static int allone(){return 0x7ff;} };

int main(int argc, char**argv){
  const std::unique_ptr<VerilatedContext> ctx{new VerilatedContext};
  ctx->commandArgs(argc, argv);
  auto tb = new Dut;
  tb->en = 1;
#ifndef DO_MUL
  tb->sub = 0;
#endif
  long N = (argc>1)? atol(argv[1]) : 5000000;
  std::map<std::string,int> shown;
  long tot_val=0, tot_dn=0, tot_ff=0;

  // ---------- single precision ----------
  for(int m=0;m<4;m++){
    tb->fmt=0; tb->rm=m; softfloat_roundingMode=sf_rm(m);
    uint32_t xa=3, xb=4; long checked=0, vm=0, dn=0, fm=0;
    for(long i=0;i<N;i++){
      uint32_t ua=xa; xa^=xa<<13; xa^=xa>>17; xa^=xa<<5;
      uint32_t ub=xb; xb^=xb<<13; xb^=xb>>17; xb^=xb<<5;
      tb->a=ua; tb->b=ub;
      for(int k=0;k<LAT;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      uint32_t dut = (uint32_t)tb->y;
      bool dn_dut = tb->denorm; uint8_t df = tb->fflags;
      float32_t fa,fb,fr; fa.v=ua; fb.v=ub; softfloat_exceptionFlags=0; fr=op32(fa,fb);
      uint32_t ref=fr.v; uint8_t rf=softfloat_exceptionFlags&0x1f; checked++;
      bool a_sub=(FC<uint32_t>::e(ua)==0)&&FC<uint32_t>::f(ua), b_sub=(FC<uint32_t>::e(ub)==0)&&FC<uint32_t>::f(ub);
      bool r_sub=(FC<uint32_t>::e(ref)==0)&&FC<uint32_t>::f(ref);
      bool uf0=((ref&0x7fffffff)==0)&&((ua&0x7fffffff)!=0)&&((ub&0x7fffffff)!=0);
      bool spc=(FC<uint32_t>::e(ua)==0xff)||(FC<uint32_t>::e(ub)==0xff);
      bool dev=!spc&&(a_sub||b_sub||r_sub||uf0);
      if(dn_dut!=dev){ dn++; std::string c=std::string("S:")+rmname[m]+(dn_dut?":FP":":FN");
        if(shown[c]<4){shown[c]++; printf("[%-10s] a=%08x b=%08x dut=%08x ref=%08x\n",c.c_str(),ua,ub,dut,ref);} continue; }
      if(dn_dut) continue;
      if(df!=rf){ fm++; std::string c=std::string("S:")+rmname[m]+":fflag";
        if(shown[c]<6){shown[c]++; printf("[%-10s] a=%08x b=%08x dut=%08x ref=%08x df=%02x rf=%02x\n",c.c_str(),ua,ub,dut,ref,df,rf);} }
      if(dut!=ref){ vm++; std::string c=std::string("S:")+rmname[m]+":val";
        if(shown[c]<6){shown[c]++; printf("[%-10s] a=%08x b=%08x dut=%08x ref=%08x\n",c.c_str(),ua,ub,dut,ref);} }
    }
    printf("SP %s: checked=%ld val=%ld denorm=%ld fflag=%ld\n",rmname[m],checked,vm,dn,fm);
    tot_val+=vm; tot_dn+=dn; tot_ff+=fm;
  }

  // ---------- double precision ----------
  for(int m=0;m<4;m++){
    tb->fmt=1; tb->rm=m; softfloat_roundingMode=sf_rm(m);
    uint64_t xa=3, xb=4; long checked=0, vm=0, dn=0, fm=0;
    for(long i=0;i<N;i++){
      uint64_t ua=xa; xa^=xa<<13; xa^=xa>>7; xa^=xa<<17;
      uint64_t ub=xb; xb^=xb<<13; xb^=xb>>7; xb^=xb<<17;
      tb->a=ua; tb->b=ub;
      for(int k=0;k<LAT;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      uint64_t dut = tb->y;
      bool dn_dut = tb->denorm; uint8_t df = tb->fflags;
      float64_t fa,fb,fr; fa.v=ua; fb.v=ub; softfloat_exceptionFlags=0; fr=op64(fa,fb);
      uint64_t ref=fr.v; uint8_t rf=softfloat_exceptionFlags&0x1f; checked++;
      bool a_sub=(FC<uint64_t>::e(ua)==0)&&FC<uint64_t>::f(ua), b_sub=(FC<uint64_t>::e(ub)==0)&&FC<uint64_t>::f(ub);
      bool r_sub=(FC<uint64_t>::e(ref)==0)&&FC<uint64_t>::f(ref);
      bool uf0=((ref&0x7fffffffffffffffULL)==0)&&((ua&0x7fffffffffffffffULL)!=0)&&((ub&0x7fffffffffffffffULL)!=0);
      bool spc=(FC<uint64_t>::e(ua)==0x7ff)||(FC<uint64_t>::e(ub)==0x7ff);
      bool dev=!spc&&(a_sub||b_sub||r_sub||uf0);
      if(dn_dut!=dev){ dn++; std::string c=std::string("D:")+rmname[m]+(dn_dut?":FP":":FN");
        if(shown[c]<4){shown[c]++; printf("[%-10s] a=%016lx b=%016lx dut=%016lx ref=%016lx\n",c.c_str(),ua,ub,dut,ref);} continue; }
      if(dn_dut) continue;
      if(df!=rf){ fm++; std::string c=std::string("D:")+rmname[m]+":fflag";
        if(shown[c]<6){shown[c]++; printf("[%-10s] a=%016lx b=%016lx dut=%016lx ref=%016lx df=%02x rf=%02x\n",c.c_str(),ua,ub,dut,ref,df,rf);} }
      if(dut!=ref){ vm++; std::string c=std::string("D:")+rmname[m]+":val";
        if(shown[c]<6){shown[c]++; printf("[%-10s] a=%016lx b=%016lx dut=%016lx ref=%016lx\n",c.c_str(),ua,ub,dut,ref);} }
    }
    printf("DP %s: checked=%ld val=%ld denorm=%ld fflag=%ld\n",rmname[m],checked,vm,dn,fm);
    tot_val+=vm; tot_dn+=dn; tot_ff+=fm;
  }

  printf("\nTOTAL: value=%ld denorm=%ld fflag=%ld\n", tot_val, tot_dn, tot_ff);
  delete tb;
  return 0;
}
