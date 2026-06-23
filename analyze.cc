#include <verilated.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include "Vfp_add.h"
extern "C" {
#include "softfloat.h"
}

static int expo(uint32_t u){ return (u>>23)&0xff; }
static uint32_t fr(uint32_t u){ return u&0x7fffff; }

// xorshift over raw bit patterns (covers NaN/inf too); returns current, advances
static uint32_t rng(uint32_t &x){
  uint32_t b = x;
  x ^= x<<13; x ^= x>>17; x ^= x<<5;
  return b;
}

static int sf_rm(int m){
  switch(m){
    case 0: return softfloat_round_near_even;
    case 1: return softfloat_round_minMag;   // toward zero
    case 2: return softfloat_round_max;      // toward +inf
    default:return softfloat_round_min;      // toward -inf
  }
}
static const char* rmname[4] = {"RN","RZ","RP","RM"};

int main(int argc, char**argv){
  const std::unique_ptr<VerilatedContext> ctx{new VerilatedContext};
  ctx->commandArgs(argc, argv);
  auto tb = new Vfp_add;
  tb->sub = 0; tb->en = 1;

  long N = (argc>1)? atol(argv[1]) : 5000000;
  long flag_fp=0, flag_fn=0, value_mism=0, fflag_mism=0;
  long flags_seen=0, ovf_seen=0, inv_seen=0;   // coverage: ops where ref raised flags
  std::map<std::string,int> shown;

  for(int m=0;m<4;m++){
    tb->rm = m;
    softfloat_roundingMode = sf_rm(m);
    uint32_t xa=3, xb=4;
    long checked=0, vm=0, ff=0, fm=0;
    for(long i=0;i<N;i++){
      uint32_t ua=rng(xa), ub=rng(xb);   // includes NaN/inf patterns
      tb->a=ua; tb->b=ub;
      for(int k=0;k<2;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); } // ADD_LAT=2
      uint32_t dut=tb->y;
      bool dut_denorm = tb->denorm;
      uint8_t dutf = tb->fflags;

      float32_t fa,fb,fr2; fa.v=ua; fb.v=ub;
      softfloat_exceptionFlags = 0;
      fr2=f32_add(fa,fb);
      uint32_t ref=fr2.v;
      uint8_t reff = softfloat_exceptionFlags & 0x1f;   // {V,Z,O,U,I}
      checked++;

      bool a_sub = (expo(ua)==0) && (fr(ua)!=0);
      bool b_sub = (expo(ub)==0) && (fr(ub)!=0);
      bool r_sub = (expo(ref)==0) && (fr(ref)!=0);
      bool special = (expo(ua)==0xff) || (expo(ub)==0xff);   // NaN/inf input -> HW handles
      bool denorm_event = !special && (a_sub || b_sub || r_sub);

      if(dut_denorm != denorm_event){
        std::string c = std::string(rmname[m]) + (dut_denorm ? ":FALSE_POS" : ":FALSE_NEG");
        if(dut_denorm) flag_fp++; else flag_fn++;
        ff++;
        if(shown[c]<4){ shown[c]++;
          printf("[%-12s] a=%08x b=%08x dut=%08x ref=%08x  a_sub=%d b_sub=%d r_sub=%d flag=%d\n",
                 c.c_str(), ua, ub, dut, ref, a_sub, b_sub, r_sub, dut_denorm);
        }
        continue;
      }
      if(dut_denorm) continue;          // value + flags don't-care when punted

      if(reff){ flags_seen++; if(reff & 4) ovf_seen++; if(reff & 16) inv_seen++; }   // coverage
      if(dutf != reff){
        fm++; fflag_mism++;
        std::string c = std::string(rmname[m]) + ":fflag";
        if(shown[c]<8){ shown[c]++;
          printf("[%-12s] a=%08x b=%08x dut=%08x ref=%08x  dutf=%02x reff=%02x\n",
                 c.c_str(), ua, ub, dut, ref, dutf, reff);
        }
      }
      if(dut==ref) continue;
      vm++; value_mism++;
      std::string c = std::string(rmname[m]) + ":value_bug";
      if(shown[c]<6){ shown[c]++;
        printf("[%-12s] a=%08x b=%08x dut=%08x ref=%08x\n", c.c_str(), ua, ub, dut, ref);
      }
    }
    printf("mode %s: checked=%ld  value_mism=%ld  denorm_err=%ld  fflag_mism=%ld\n",
           rmname[m], checked, vm, ff, fm);
  }
  printf("\nTOTAL: value=%ld  denorm(fp=%ld,fn=%ld)  fflag=%ld\n",
         value_mism, flag_fp, flag_fn, fflag_mism);
  printf("coverage: ops with ref flags set=%ld (overflow=%ld, invalid/V=%ld)\n",
         flags_seen, ovf_seen, inv_seen);
  delete tb;
  return 0;
}
