#include <verilated.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include "Vfp_add.h"
#include "svdpi.h"

extern "C" {
#include "softfloat.h"
}


bool sign(float x) {
  uint32_t u = *reinterpret_cast<uint32_t*>(&x);
  return u>>31;
}

uint32_t exp(float x) {
  uint32_t u = *reinterpret_cast<uint32_t*>(&x);
  return (u>>23)&255;
}

uint32_t frac(float x) {
  uint32_t u = *reinterpret_cast<uint32_t*>(&x);  
  return u&((1U<<23)-1);
}    


float r(uint32_t &x) {
  float y = *reinterpret_cast<float*>(&x); 
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return y;
}


int main(int argc, char *argv[]) {
  const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
  contextp->commandArgs(argc, argv);
  auto tb = new Vfp_add;
  tb->sub = 0;
  tb->en = 1;

  // Rounding mode: cycle through all four (MIPS RM 0=RN 1=RZ 2=RP 3=RM) and
  // keep the SoftFloat reference in lock-step. See FPU_ROUNDING_EXCEPTIONS.md.
  static const int sf_modes[4] = {
    softfloat_round_near_even, softfloat_round_minMag,
    softfloat_round_max,       softfloat_round_min };
  int rm = 0;
  tb->rm = rm;
  softfloat_roundingMode = sf_modes[rm];

  uint32_t init_x_a = 1, init_x_b = 2;
  double max_err = 0.0, max_a = 0.0, max_b = 0.0;
  uint32_t x_a = 1, x_b = 2;
  double sum = 0.0;
  size_t t = 0;
  size_t mismatches = 0, checked = 0;
  x_a++; x_b++;
  init_x_a = x_a;
  init_x_b = x_b;    
  // fp_add now has an ADD_LAT-deep output pipeline; hold a/b and clock the
  // result through before reading it (and the aligned denorm flag).
  const int ADD_LAT = 2;
  while( true ) {
    float a = r(x_a);
    float b = r(x_b);   // NaN/inf patterns now exercised too

    // rotate the rounding mode each op (DUT + reference together)
    rm = (rm + 1) & 3;
    tb->rm = rm;
    softfloat_roundingMode = sf_modes[rm];

    tb->a = *reinterpret_cast<uint32_t*>(&a);
    tb->b = *reinterpret_cast<uint32_t*>(&b);

    for(int k = 0; k < ADD_LAT; k++) {
      contextp->timeInc(1);
      tb->clk = 0;
      tb->eval();
      tb->clk = 1;
      tb->eval();
    }

    float y = *reinterpret_cast<float*>(&(tb->y));

    // correctly-rounded reference from Berkeley SoftFloat
    float32_t fa, fb, fref;
    fa.v = *reinterpret_cast<uint32_t*>(&a);
    fb.v = *reinterpret_cast<uint32_t*>(&b);
    fref = tb->sub ? f32_sub(fa, fb) : f32_add(fa, fb);
    uint32_t ref_bits = fref.v;
    float yy = *reinterpret_cast<float*>(&ref_bits);

    // bit-exact check against the reference (the real accuracy test).
    // When the DUT flags a denormal it punts (like the R4000 E-trap), so the
    // result value is don't-care and we skip the value comparison.
    checked++;
    if(not tb->denorm and tb->y != ref_bits) {
      mismatches++;
      printf("MISMATCH: %g %c %g : dut=%08x ref=%08x (%g vs %g)\n",
	     a, tb->sub ? '-' : '+', b, tb->y, ref_bits, y, yy);
    }

    double d = (yy-y);
    double err = std::sqrt(d*d) / yy;

    bool f = std::isfinite(yy) and std::isfinite(y);
    
    if(f and (err > 1)) {
      printf("err %g : %g + %g = %g, yy = %g, d = %g\n",
	     err, a, b, y, yy, d);

      if(sign(yy) != sign(y)) {
	printf("sign is wrong\n");
	std::cout << sign(yy) << "\n";
	std::cout << sign(y) << "\n";
	continue;
      }
      
      if(exp(yy) != exp(y)) {
	printf("exp is wrong, yy is denorm = %d\n", not(std::isnormal(yy)));
	std::cout << exp(yy) << "\n";
	std::cout << exp(y) << "\n";
	continue;
      }

      if(frac(yy) != frac(y)) {
	int d = static_cast<int>(frac(yy)) - static_cast<int>(frac(y));
	printf("frac is wrong, diff= %d\n", d);
	std::cout << frac(yy) << "\n";
	std::cout << frac(y) << "\n";
	continue;
      }
    }
    
    if(f) {
      sum += yy;
      t++;
    }
      
    if(err > max_err) {
      printf("%g + %g = %g, yy = %g\n", a, b, y, yy);
      max_a = a;
      max_b = b;
      max_err = err;
    }
    
    if(x_a == init_x_a) {
      break;
    }
  }

  printf("max error = %g, a = %g, b = %g\n",
	 max_err, max_a, max_b);
  printf("avg answer = %g\n", sum / static_cast<double>(t));
  printf("softfloat bit-exact: %zu mismatches / %zu checked\n",
	 mismatches, checked);
  
  delete tb;
  return 0;
}
