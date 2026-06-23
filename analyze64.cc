#include <verilated.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
extern "C" {
#include "softfloat.h"
}

#ifdef DO_MUL
#include "Vfp_mul.h"
typedef Vfp_mul Dut;
static const int LAT = 4;
static float64_t op(float64_t a, float64_t b){ return f64_mul(a,b); }
#else
#include "Vfp_add.h"
typedef Vfp_add Dut;
static const int LAT = 2;
static float64_t op(float64_t a, float64_t b){ return f64_add(a,b); }
#endif

static int    expo(uint64_t u){ return (u>>52)&0x7ff; }
static uint64_t fr(uint64_t u){ return u&((1ULL<<52)-1); }

// xorshift over raw bit patterns (covers NaN/inf too); returns current, advances
static uint64_t rng(uint64_t &x){
  uint64_t b = x;
  x ^= x<<13; x ^= x>>7; x ^= x<<17;
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
  auto tb = new Dut;
  tb->en = 1;
#ifndef DO_MUL
  tb->sub = 0;
#endif

  long N = (argc>1)? atol(argv[1]) : 5000000;
  long flag_fp=0, flag_fn=0, value_mism=0, fflag_mism=0, normal_ok=0;
  std::map<std::string,int> shown;

  for(int m=0;m<4;m++){
    tb->rm = m;
    softfloat_roundingMode = sf_rm(m);
    uint64_t xa=3, xb=4;
    long checked=0, vm=0, ff=0, fm=0, nok=0;
    for(long i=0;i<N;i++){
      uint64_t ua=rng(xa), ub=rng(xb);   // includes NaN/inf patterns
      tb->a=ua; tb->b=ub;
      for(int k=0;k<LAT;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      uint64_t dut=tb->y;
      bool dut_denorm = tb->denorm;
      uint8_t dutf = tb->fflags;

      float64_t fa,fb,fr2; fa.v=ua; fb.v=ub;
      softfloat_exceptionFlags = 0;
      fr2=op(fa,fb);
      uint64_t ref=fr2.v;
      uint8_t reff = softfloat_exceptionFlags & 0x1f;
      checked++;

      bool a_sub = (expo(ua)==0) && (fr(ua)!=0);
      bool b_sub = (expo(ub)==0) && (fr(ub)!=0);
      bool r_sub = (expo(ref)==0) && (fr(ref)!=0);
      bool a_zero = (ua & 0x7fffffffffffffffULL)==0;
      bool b_zero = (ub & 0x7fffffffffffffffULL)==0;
      bool ref_zero = (ref & 0x7fffffffffffffffULL)==0;
      bool underflow0 = ref_zero && !a_zero && !b_zero;
      bool special = (expo(ua)==0x7ff) || (expo(ub)==0x7ff);  // NaN/inf input -> HW handles
      bool denorm_event = !special && (a_sub || b_sub || r_sub || underflow0);

      if(dut_denorm != denorm_event){
        std::string c = std::string(rmname[m]) + (dut_denorm ? ":FALSE_POS" : ":FALSE_NEG");
        if(dut_denorm) flag_fp++; else flag_fn++;
        ff++;
        if(shown[c]<4){ shown[c]++;
          printf("[%-12s] a=%016lx b=%016lx dut=%016lx ref=%016lx  a_sub=%d b_sub=%d r_sub=%d uf0=%d flag=%d\n",
                 c.c_str(), ua, ub, dut, ref, a_sub, b_sub, r_sub, underflow0, dut_denorm);
        }
        continue;
      }
      if(dut_denorm) continue;
      nok++; normal_ok++;

      if(dutf != reff){
        fm++; fflag_mism++;
        std::string c = std::string(rmname[m]) + ":fflag";
        if(shown[c]<8){ shown[c]++;
          printf("[%-12s] a=%016lx b=%016lx dut=%016lx ref=%016lx  dutf=%02x reff=%02x\n",
                 c.c_str(), ua, ub, dut, ref, dutf, reff);
        }
      }
      if(dut==ref) continue;
      nok--; normal_ok--;
      vm++; value_mism++;
      long ulp = (long)((int64_t)dut - (int64_t)ref); if(ulp<0) ulp=-ulp;
      std::string c = std::string(rmname[m]) + (ulp==1?":rnd_1ulp":":value_BIG");
      if(shown[c]<6){ shown[c]++;
        printf("[%-12s ulp=%-8ld] a=%016lx b=%016lx dut=%016lx ref=%016lx\n", c.c_str(), ulp, ua, ub, dut, ref);
      }
    }
    printf("mode %s: checked=%ld  normal_ok=%ld  value_mism=%ld  denorm_err=%ld  fflag_mism=%ld\n",
           rmname[m], checked, nok, vm, ff, fm);
  }
  printf("\nTOTAL: normal_ok=%ld  value=%ld  denorm(fp=%ld,fn=%ld)  fflag=%ld\n",
         normal_ok, value_mism, flag_fp, flag_fn, fflag_mism);
  delete tb;
  return 0;
}
