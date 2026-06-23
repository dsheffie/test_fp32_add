// Checker for fp_compare. Tests LT/LE/EQ vs SoftFloat, separating NaN-involved
// mismatches (the module has no NaN handling) from core ordering.  -DDO_DP for
// double precision (verilate with -GW=64).
#include <verilated.h>
#include <cstdint>
#include <cstdio>
#include "Vfp_compare.h"
extern "C" {
#include "softfloat.h"
}

enum { CMP_LT=1, CMP_LE=2, CMP_EQ=3 };
static const int D = 4;
static const char* pname[4] = {"?","LT","LE","EQ"};

#ifdef DO_DP
typedef uint64_t U;
static bool is_nan(U u){ return ((u>>52)&0x7ff)==0x7ff && (u&((1ULL<<52)-1)); }
static bool ref_cmp(int p, U a, U b){ float64_t x,y; x.v=a; y.v=b;
  return p==CMP_LT? f64_lt(x,y) : p==CMP_LE? f64_le(x,y) : f64_eq(x,y); }
static U xs(U &x){ U b=x; x^=x<<13; x^=x>>7; x^=x<<17; return b; }
static const char* TAG="DP";
#else
typedef uint32_t U;
static bool is_nan(U u){ return ((u>>23)&0xff)==0xff && (u&0x7fffff); }
static bool ref_cmp(int p, U a, U b){ float32_t x,y; x.v=a; y.v=b;
  return p==CMP_LT? f32_lt(x,y) : p==CMP_LE? f32_le(x,y) : f32_eq(x,y); }
static U xs(U &x){ U b=x; x^=x<<13; x^=x>>17; x^=x<<5; return b; }
static const char* TAG="SP";
#endif

int main(int argc, char**argv){
  const std::unique_ptr<VerilatedContext> ctx{new VerilatedContext};
  ctx->commandArgs(argc, argv);
  auto tb = new Vfp_compare;
  tb->start = 1;
  long N = (argc>1)? atol(argv[1]) : 5000000;

  for(int p=CMP_LT; p<=CMP_EQ; p++){
    tb->cmp_type = p;
    U xa=3, xb=4; long checked=0, core_mism=0, nan_mism=0, flag_mism=0, inv_seen=0; int shown=0;
    for(long i=0;i<N;i++){
      U ua=xs(xa), ub=xs(xb);
      tb->a=ua; tb->b=ub;
      for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
      bool dut = tb->y;
      uint8_t df = tb->fflags;
      softfloat_exceptionFlags = 0;
      bool ref = ref_cmp(p, ua, ub);
      uint8_t rf = softfloat_exceptionFlags & 0x1f;
      checked++;
      if(rf & 0x10) inv_seen++;
      if(dut != ref){
        bool nan = is_nan(ua) || is_nan(ub);
        if(nan) nan_mism++; else { core_mism++;
          if(shown<8){ shown++; printf("[%s %s CORE] a=%llx b=%llx dut=%d ref=%d\n",
                TAG, pname[p], (unsigned long long)ua,(unsigned long long)ub, dut, ref); } }
      }
      if(df != rf){ flag_mism++;
        if(shown<8){ shown++; printf("[%s %s FFLAG] a=%llx b=%llx df=%02x rf=%02x\n",
              TAG, pname[p], (unsigned long long)ua,(unsigned long long)ub, df, rf); } }
    }
    printf("%s %s: checked=%ld  core_mism=%ld  nan_mism=%ld  fflag_mism=%ld  (invalid raised=%ld)\n",
           TAG, pname[p], checked, core_mism, nan_mism, flag_mism, inv_seen);
  }

  // directed NaN sweep: random testing essentially never produces equal NaN
  // bit patterns, so exercise NaN-vs-self, NaN-vs-finite, finite-vs-NaN, and
  // qNaN/sNaN explicitly. Every ordered predicate must be false here.
#ifdef DO_DP
  const U EXP1 = 0x7ffULL<<52; const U FMASK = (1ULL<<52)-1; const U ONE = 0x3ff0000000000000ULL;
#else
  const U EXP1 = 0xffULL<<23;  const U FMASK = (1ULL<<23)-1;  const U ONE = 0x3f800000ULL;
#endif
  U xn=0x1234567; long nd_checked=0, nd_mism=0; int nd_shown=0;
  for(long t=0;t<300000;t++){
    U fr = xs(xn) & FMASK; if(fr==0) fr=1;
    U sgn = (xs(xn)&1) ? (TAG[0]?0:0) : 0;  // keep sign 0; sign-of-NaN irrelevant
    (void)sgn;
    U nan = EXP1 | fr;                       // a NaN (qNaN or sNaN per fr's top bit)
    U other = xs(xn);                        // arbitrary (maybe finite, maybe NaN)
    U pairs_a[3] = { nan, nan,  other };
    U pairs_b[3] = { nan, ONE,  nan   };
    for(int q=0;q<3;q++){
      for(int p=CMP_LT;p<=CMP_EQ;p++){
        tb->cmp_type=p; tb->a=pairs_a[q]; tb->b=pairs_b[q];
        for(int k=0;k<D;k++){ tb->clk=0; tb->eval(); tb->clk=1; tb->eval(); }
        uint8_t df = tb->fflags;
        softfloat_exceptionFlags = 0;
        bool ref = ref_cmp(p, pairs_a[q], pairs_b[q]);
        uint8_t rf = softfloat_exceptionFlags & 0x1f;
        nd_checked++;
        if((bool)tb->y != ref || df != rf){ nd_mism++;
          if(nd_shown<12){ nd_shown++; printf("[%s NaN-dir %s] a=%llx b=%llx dut=%d ref=%d df=%02x rf=%02x\n",
              TAG, pname[p], (unsigned long long)pairs_a[q],(unsigned long long)pairs_b[q],(int)tb->y,ref,df,rf); } }
      }
    }
  }
  printf("%s directed-NaN: checked=%ld  mism=%ld\n", TAG, nd_checked, nd_mism);
  delete tb;
  return 0;
}
