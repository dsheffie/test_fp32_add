// Unified single/double-precision FP multiplier.
//
// One datapath, sized for double precision (a 53x53 significand multiply);
// single precision runs through it left-justified and is rounded once at single
// precision. `fmt`: 0 = single (operand in low 32 bits), 1 = double.

module fpu_mul(/*AUTOARG*/
   // Outputs
   y, denorm, fflags,
   // Inputs
   clk, a, b, en, rm, fmt
   );
   parameter MUL_LAT = 4;

   input logic 	      clk;
   input logic [63:0] a;
   input logic [63:0] b;
   input logic 	      en;
   input logic [1:0]  rm;     // 0=RN 1=RZ 2=RP(+inf) 3=RM(-inf)
   input logic 	      fmt;    // 0=single, 1=double
   output logic [63:0] y;
   output logic        denorm;
   output logic [4:0]  fflags; // {V,Z,O,U,I}

   localparam EW = 11;
   localparam FW = 52;

   // ---------------- field extraction (fmt-dependent) ----------------
   wire 	sgn_a = fmt ? a[63] : a[31];
   wire 	sgn_b = fmt ? b[63] : b[31];
   wire 	w_sign = sgn_a ^ sgn_b;
   wire [EW-1:0] exp_a = fmt ? a[62:52] : {3'b0, a[30:23]};
   wire [EW-1:0] exp_b = fmt ? b[62:52] : {3'b0, b[30:23]};
   wire [FW-1:0] frac_a = fmt ? a[51:0] : {a[22:0], 29'b0};
   wire [FW-1:0] frac_b = fmt ? b[51:0] : {b[22:0], 29'b0};

   localparam [EW-1:0] INF_EXP_D = 11'd2047;
   localparam [EW-1:0] INF_EXP_S = 11'd255;
   wire [EW-1:0] INF_EXP = fmt ? INF_EXP_D : INF_EXP_S;
   wire [EW-1:0] BIAS    = fmt ? 11'd1023 : 11'd127;

   wire 	a_is_zero = (exp_a == 'd0) & (frac_a == 'd0);
   wire 	b_is_zero = (exp_b == 'd0) & (frac_b == 'd0);

   // ---------------- special-value detection ----------------
   wire 	exp_all1_a = (exp_a == INF_EXP);
   wire 	exp_all1_b = (exp_b == INF_EXP);
   wire 	a_is_nan = exp_all1_a & (frac_a != 'd0);
   wire 	b_is_nan = exp_all1_b & (frac_b != 'd0);
   wire 	a_is_inf = exp_all1_a & (frac_a == 'd0);
   wire 	b_is_inf = exp_all1_b & (frac_b == 'd0);
   wire 	a_qbit = fmt ? a[51] : a[22];
   wire 	b_qbit = fmt ? b[51] : b[22];
   wire 	a_is_snan = a_is_nan & ~a_qbit;
   wire 	b_is_snan = b_is_nan & ~b_qbit;
   wire 	any_nan = a_is_nan | b_is_nan;
   wire 	special = any_nan | a_is_inf | b_is_inf;
   wire 	inf_x_zero = (a_is_inf & b_is_zero) | (b_is_inf & a_is_zero);
   wire 	w_invalid = a_is_snan | b_is_snan | inf_x_zero;

   // ---------------- significand multiply (53 x 53) ----------------
   wire [FW:0] 	 sig_a = {1'b1, frac_a};   // 53-bit
   wire [FW:0] 	 sig_b = {1'b1, frac_b};
   wire [2*FW+1:0] w_prod = sig_a * sig_b;   // 106-bit, value in [1,4)

   wire 	w_prod_top = w_prod[2*FW+1];           // bit 105: product in [2,4)?
   // normalized 53-bit significand (leading 1 at bit 52)
   wire [FW:0] 	w_sig = w_prod_top ? w_prod[2*FW+1:FW+1] : w_prod[2*FW:FW];
   // double-precision guard/round/sticky (just below the 53-bit significand)
   wire 	g_d = w_prod_top ? w_prod[FW]   : w_prod[FW-1];
   wire 	r_d = w_prod_top ? w_prod[FW-1] : w_prod[FW-2];
   wire 	s_d = w_prod_top ? (|w_prod[FW-2:0]) : (|w_prod[FW-3:0]);

   // ---------------- rounding (fmt-dependent round point) ----------------
   // double keeps 52 frac (lsb=w_sig[0]); single keeps 23 (lsb=w_sig[29])
   wire 	g_s = w_sig[28];
   wire 	r_s = w_sig[27];
   wire 	s_s = (|w_sig[26:0]) | g_d | r_d | s_d;
   wire 	lsb_d = w_sig[0];
   wire 	lsb_s = w_sig[29];

   wire 	w_g = fmt ? g_d : g_s;
   wire 	w_r = fmt ? r_d : r_s;
   wire 	w_s = fmt ? s_d : s_s;
   wire 	w_lsb = fmt ? lsb_d : lsb_s;
   wire 	w_inexact = w_g | w_r | w_s;
   wire 	w_round_up =
		(rm == 2'd0) ? (w_g & (w_r | w_s | w_lsb)) :
		(rm == 2'd1) ? 1'b0 :
		(rm == 2'd2) ? (~w_sign & w_inexact) :
		               ( w_sign & w_inexact);

   wire [FW:0] 	w_inc = fmt ? {{(FW){1'b0}}, 1'b1} : ({{(FW){1'b0}}, 1'b1} << 29);
   wire [FW+1:0] w_sum_r = {1'b0, w_sig} + (w_round_up ? {1'b0, w_inc} : {(FW+2){1'b0}});
   wire 	w_round_carry = w_sum_r[FW+1];
   wire [FW:0] 	w_final_sig = w_round_carry ? w_sum_r[FW+1:1] : w_sum_r[FW:0];

   // ---------------- exponent (signed, unbiased to the format) ----------------
   wire [EW+1:0] w_exp_sum = {2'b0, exp_a} + {2'b0, exp_b}
			   + {{(EW+1){1'b0}}, w_prod_top} + {{(EW+1){1'b0}}, w_round_carry};
   wire signed [EW+2:0] w_exp_real = $signed({1'b0, w_exp_sum}) - $signed({2'b0, BIAS});

   wire 	w_overflow  = (w_exp_real >= $signed({3'b0, INF_EXP}));
   wire 	w_underflow = (w_exp_real[EW+2] | ~(|w_exp_real)) & ~a_is_zero & ~b_is_zero;
   wire 	w_ovf_inf =
		(rm == 2'd0) ? 1'b1 :
		(rm == 2'd1) ? 1'b0 :
		(rm == 2'd2) ? ~w_sign :
		                w_sign;

   wire 	w_a_denorm = (exp_a == 'd0) & (frac_a != 'd0);
   wire 	w_b_denorm = (exp_b == 'd0) & (frac_b != 'd0);

   // ---------------- pack result (fmt-dependent) ----------------
   wire [EW-1:0] w_pack_exp = w_exp_real[EW-1:0];

   wire [63:0] 	DEF_NAN = fmt ? {1'b1, 11'h7ff, 1'b1, 51'd0}
		              : {32'd0, 1'b1, 8'hff, 1'b1, 22'd0};
   wire [63:0] 	nan_src = (a_is_nan ? a : b);
   wire [63:0] 	qnan = fmt ? {nan_src[63:52], 1'b1, nan_src[50:0]}
		           : {32'd0, nan_src[31:23], 1'b1, nan_src[21:0]};
   wire [63:0] 	inf_y = fmt ? {w_sign, 11'h7ff, 52'd0}
		            : {32'd0, w_sign, 8'hff, 23'd0};
   wire [63:0] 	special_y = any_nan ? qnan : inf_x_zero ? DEF_NAN : inf_y;

   wire [63:0] 	ovf_inf = fmt ? {w_sign, 11'h7ff, 52'd0}
		             : {32'd0, w_sign, 8'hff, 23'd0};
   wire [63:0] 	ovf_max = fmt ? {w_sign, 11'h7fe, 52'hfffffffffffff}
		             : {32'd0, w_sign, 8'hfe, 23'h7fffff};
   wire [63:0] 	ovf_y = w_ovf_inf ? ovf_inf : ovf_max;

   wire [63:0] 	zero_y = fmt ? {w_sign, 63'd0} : {32'd0, w_sign, 31'd0};
   wire [63:0] 	norm_y = fmt ? {w_sign, w_pack_exp[10:0], w_final_sig[51:0]}
		            : {32'd0, w_sign, w_pack_exp[7:0], w_final_sig[51:29]};

   wire [63:0] 	w_y = special ? special_y :
		(a_is_zero | b_is_zero) ? zero_y :
		w_overflow ? ovf_y :
		norm_y;

   wire 	w_denorm = ~special & (w_a_denorm | w_b_denorm | w_underflow);

   // ---------------- IEEE flags ----------------
   wire 	w_exact = special | a_is_zero | b_is_zero;
   wire 	w_f_inexact   = ~w_exact & (w_inexact | w_overflow);
   wire 	w_f_overflow  = ~w_exact & w_overflow;
   wire 	w_f_underflow = ~w_exact & w_underflow;
   wire [4:0] 	w_fflags = {w_invalid, 1'b0, w_f_overflow, w_f_underflow, w_f_inexact};

   // ---------------- output pipeline ----------------
   logic [63+6:0] r_pipe [MUL_LAT-1:0];
   integer 	  i;
   always_ff @(posedge clk)
     begin
	r_pipe[0] <= {w_fflags, w_denorm, w_y};
	for(i = 1; i < MUL_LAT; i = i + 1)
	  r_pipe[i] <= r_pipe[i-1];
     end
   assign y      = r_pipe[MUL_LAT-1][63:0];
   assign denorm = r_pipe[MUL_LAT-1][64];
   assign fflags = r_pipe[MUL_LAT-1][69:65];

endmodule // fpu_mul
