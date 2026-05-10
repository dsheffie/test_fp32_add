#include <verilated.h>
#include <algorithm>
#include <cmath>
#include "Vfp_add.h"
#include "svdpi.h"

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
  
  uint32_t init_x_a = 1, init_x_b = 2;
  double max_err = 0.0, max_a = 0.0, max_b = 0.0;
  uint32_t x_a = 1, x_b = 2;
  double sum = 0.0;
  size_t t = 0;
  x_a++; x_b++;
  init_x_a = x_a;
  init_x_b = x_b;    
  while( true ) {
    contextp->timeInc(1);  // 1 timeprecision periodd passes...    
    tb->clk = 0;
    tb->eval();
      
    float a = r(x_a);
    float b = r(x_b);
      
    if(not(std::isfinite(a) and std::isfinite(b))) {
      continue;
    }
    
    tb->a = *reinterpret_cast<uint32_t*>(&a);
    tb->b = *reinterpret_cast<uint32_t*>(&b);    
      
    tb->clk = 1;
    tb->eval();
      
    float y = *reinterpret_cast<float*>(&(tb->y));
    float yy = a + b;
    double err = (yy-y);
    err *= err;

    if(std::isfinite(yy)) {
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
  
  delete tb;
  return 0;
}
