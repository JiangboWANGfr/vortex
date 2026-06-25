`include "VX_define.vh"

/*
AES utilities adapted from the legacy crypto branch.
This module implements the AES32 scalar round primitives used by the
RISC-V crypto extension style instructions.
*/

module riscv_crypto_sbox_inv_mid(
    input  wire [20:0] x,
    output wire [17:0] y
);
    // Shared nonlinear core used by both the forward and inverse AES S-boxes
    // after their respective basis transforms.

    wire t0  = x[ 3] ^ x[12];
    wire t1  = x[ 9] & x[ 5];
    wire t2  = x[17] & x[ 6];
    wire t3  = x[10] ^ t1;
    wire t4  = x[14] & x[ 0];
    wire t5  = t4 ^ t1;
    wire t6  = x[ 3] & x[12];
    wire t7  = x[16] & x[ 7];
    wire t8  = t0 ^ t6;
    wire t9  = x[15] & x[13];
    wire t10 = t9 ^ t6;
    wire t11 = x[ 1] & x[11];
    wire t12 = x[ 4] & x[20];
    wire t13 = t12 ^ t11;
    wire t14 = x[ 2] & x[ 8];
    wire t15 = t14 ^ t11;
    wire t16 = t3 ^ t2;
    wire t17 = t5 ^ x[18];
    wire t18 = t8 ^ t7;
    wire t19 = t10 ^ t15;
    wire t20 = t16 ^ t13;
    wire t21 = t17 ^ t15;
    wire t22 = t18 ^ t13;
    wire t23 = t19 ^ x[19];
    wire t24 = t22 ^ t23;
    wire t25 = t22 & t20;
    wire t26 = t21 ^ t25;
    wire t27 = t20 ^ t21;
    wire t28 = t23 ^ t25;
    wire t29 = t28 & t27;
    wire t30 = t26 & t24;
    wire t31 = t20 & t23;
    wire t32 = t27 & t31;
    wire t33 = t27 ^ t25;
    wire t34 = t21 & t22;
    wire t35 = t24 & t34;
    wire t36 = t24 ^ t25;
    wire t37 = t21 ^ t29;
    wire t38 = t32 ^ t33;
    wire t39 = t23 ^ t30;
    wire t40 = t35 ^ t36;
    wire t41 = t38 ^ t40;
    wire t42 = t37 ^ t39;
    wire t43 = t37 ^ t38;
    wire t44 = t39 ^ t40;
    wire t45 = t42 ^ t41;

    assign y[ 0] = t38 & x[ 7];
    assign y[ 1] = t37 & x[13];
    assign y[ 2] = t42 & x[11];
    assign y[ 3] = t45 & x[20];
    assign y[ 4] = t41 & x[ 8];
    assign y[ 5] = t44 & x[ 9];
    assign y[ 6] = t40 & x[17];
    assign y[ 7] = t39 & x[14];
    assign y[ 8] = t43 & x[ 3];
    assign y[ 9] = t38 & x[16];
    assign y[10] = t37 & x[15];
    assign y[11] = t42 & x[ 1];
    assign y[12] = t45 & x[ 4];
    assign y[13] = t41 & x[ 2];
    assign y[14] = t44 & x[ 5];
    assign y[15] = t40 & x[ 6];
    assign y[16] = t39 & x[ 0];
    assign y[17] = t43 & x[12];

endmodule

module riscv_crypto_sbox_aes_top(
    input  wire [ 7:0] x,
    output wire [20:0] y
);
    // Forward AES S-box input transform into the composite-field basis used by
    // the shared inversion core.

    wire y0  = x[0];
    wire y1  = x[7] ^ x[4];
    wire y2  = x[7] ^ x[2];
    wire y3  = x[7] ^ x[1];
    wire y4  = x[4] ^ x[2];
    wire t0  = x[3] ^ x[1];
    wire y5  = y1 ^ t0;
    wire t1  = x[6] ^ x[5];
    wire y6  = x[0] ^ y5;
    wire y7  = x[0] ^ t1;
    wire y8  = y5 ^ t1;
    wire t2  = x[6] ^ x[2];
    wire t3  = x[5] ^ x[2];
    wire y9  = y3 ^ y4;
    wire y10 = y5 ^ t2;
    wire y11 = t0 ^ t2;
    wire y12 = t0 ^ t3;
    wire y13 = y7 ^ y12;
    wire t4  = x[4] ^ x[0];
    wire y14 = t1 ^ t4;
    wire y15 = y1 ^ y14;
    wire t5  = x[1] ^ x[0];
    wire y16 = t1 ^ t5;
    wire y17 = y2 ^ y16;
    wire y18 = y2 ^ y8;
    wire y19 = y15 ^ y13;
    wire y20 = y1 ^ t3;

    assign y[0]  = y0;
    assign y[1]  = y1;
    assign y[2]  = y2;
    assign y[3]  = y3;
    assign y[4]  = y4;
    assign y[5]  = y5;
    assign y[6]  = y6;
    assign y[7]  = y7;
    assign y[8]  = y8;
    assign y[9]  = y9;
    assign y[10] = y10;
    assign y[11] = y11;
    assign y[12] = y12;
    assign y[13] = y13;
    assign y[14] = y14;
    assign y[15] = y15;
    assign y[16] = y16;
    assign y[17] = y17;
    assign y[18] = y18;
    assign y[19] = y19;
    assign y[20] = y20;

endmodule

module riscv_crypto_sbox_aes_out(
    input  wire [17:0] x,
    output wire [ 7:0] y
);
    // Forward AES S-box output transform back from the shared core domain.

    wire t0  = x[11] ^ x[12];
    wire t1  = x[0] ^ x[6];
    wire t2  = x[14] ^ x[16];
    wire t3  = x[15] ^ x[5];
    wire t4  = x[4] ^ x[8];
    wire t5  = x[17] ^ x[11];
    wire t6  = x[12] ^ t5;
    wire t7  = x[14] ^ t3;
    wire t8  = x[1] ^ x[9];
    wire t9  = x[2] ^ x[3];
    wire t10 = x[3] ^ t4;
    wire t11 = x[10] ^ t2;
    wire t12 = x[16] ^ x[1];
    wire t13 = x[0] ^ t0;
    wire t14 = x[2] ^ x[11];
    wire t15 = x[5] ^ t1;
    wire t16 = x[6] ^ t0;
    wire t17 = x[7] ^ t1;
    wire t18 = x[8] ^ t8;
    wire t19 = x[13] ^ t4;
    wire t20 = t0 ^ t1;
    wire t21 = t1 ^ t7;
    wire t22 = t3 ^ t12;
    wire t23 = t18 ^ t2;
    wire t24 = t15 ^ t9;
    wire t25 = t6 ^ t10;
    wire t26 = t7 ^ t9;
    wire t27 = t8 ^ t10;
    wire t28 = t11 ^ t14;
    wire t29 = t11 ^ t17;

    assign y[0] = t6 ^~ t23;
    assign y[1] = t13 ^~ t27;
    assign y[2] = t25 ^ t29;
    assign y[3] = t20 ^ t22;
    assign y[4] = t6 ^ t21;
    assign y[5] = t19 ^~ t28;
    assign y[6] = t16 ^~ t26;
    assign y[7] = t6 ^ t24;

endmodule

module riscv_crypto_sbox_aesi_top(
    output wire [20:0] y,
    input  wire [ 7:0] x
);
    // Inverse AES S-box input transform into the same shared core domain.

    wire y17 = x[7] ^ x[4];
    wire y16 = x[6] ^~ x[4];
    wire y2  = x[7] ^~ x[6];
    wire y1  = x[4] ^ x[3];
    wire y18 = x[3] ^~ x[0];
    wire t0  = x[1] ^ x[0];
    wire y6  = x[6] ^~ y17;
    wire y14 = y16 ^ t0;
    wire y7  = x[0] ^~ y1;
    wire y8  = y2 ^ y18;
    wire y9  = y2 ^ t0;
    wire y3  = y1 ^ t0;
    wire y19 = x[5] ^~ y1;
    wire t1  = x[6] ^ x[1];
    wire y13 = x[5] ^~ y14;
    wire y15 = y18 ^ t1;
    wire y4  = x[3] ^ y6;
    wire t2  = x[5] ^~ x[2];
    wire t3  = x[2] ^~ x[1];
    wire t4  = x[5] ^~ x[3];
    wire y5  = y16 ^ t2;
    wire y12 = t1 ^ t4;
    wire y20 = y1 ^ t3;
    wire y11 = y8 ^ y20;
    wire y10 = y8 ^ t3;
    wire y0  = x[7] ^ t2;

    assign y[0]  = y0;
    assign y[1]  = y1;
    assign y[2]  = y2;
    assign y[3]  = y3;
    assign y[4]  = y4;
    assign y[5]  = y5;
    assign y[6]  = y6;
    assign y[7]  = y7;
    assign y[8]  = y8;
    assign y[9]  = y9;
    assign y[10] = y10;
    assign y[11] = y11;
    assign y[12] = y12;
    assign y[13] = y13;
    assign y[14] = y14;
    assign y[15] = y15;
    assign y[16] = y16;
    assign y[17] = y17;
    assign y[18] = y18;
    assign y[19] = y19;
    assign y[20] = y20;

endmodule

module riscv_crypto_sbox_aesi_out(
    output wire [ 7:0] y,
    input  wire [17:0] x
);
    // Inverse AES S-box output transform back from the shared core domain.

    wire t0  = x[2] ^ x[11];
    wire t1  = x[8] ^ x[9];
    wire t2  = x[4] ^ x[12];
    wire t3  = x[15] ^ x[0];
    wire t4  = x[16] ^ x[6];
    wire t5  = x[14] ^ x[1];
    wire t6  = x[17] ^ x[10];
    wire t7  = t0 ^ t1;
    wire t8  = x[0] ^ x[3];
    wire t9  = x[5] ^ x[13];
    wire t10 = x[7] ^ t4;
    wire t11 = t0 ^ t3;
    wire t12 = x[14] ^ x[16];
    wire t13 = x[17] ^ x[1];
    wire t14 = x[17] ^ x[12];
    wire t15 = x[4] ^ x[9];
    wire t16 = x[7] ^ x[11];
    wire t17 = x[8] ^ t2;
    wire t18 = x[13] ^ t5;
    wire t19 = t2 ^ t3;
    wire t20 = t4 ^ t6;
    wire t22 = t2 ^ t7;
    wire t23 = t7 ^ t8;
    wire t24 = t5 ^ t7;
    wire t25 = t6 ^ t10;
    wire t26 = t9 ^ t11;
    wire t27 = t10 ^ t18;
    wire t28 = t11 ^ t25;
    wire t29 = t15 ^ t20;

    assign y[0] = t9 ^ t16;
    assign y[1] = t14 ^ t23;
    assign y[2] = t19 ^ t24;
    assign y[3] = t23 ^ t27;
    assign y[4] = t12 ^ t22;
    assign y[5] = t17 ^ t28;
    assign y[6] = t26 ^ t29;
    assign y[7] = t13 ^ t22;

endmodule
module GF_2_4_Mul2 (
    output wire [3:0] q,
    input  wire [3:0] a,
    input  wire [3:0] b
);
    assign q[0] = (a[3] & b[3]) ^ (a[3] & b[2]) ^ (a[2] & b[3]) ^ (a[0] & b[0]) ^
                  (a[3] & b[1]) ^ (a[2] & b[2]) ^ (a[1] & b[3]);
    assign q[1] = (a[3] & b[3]) ^ (a[3] & b[2]) ^ (a[2] & b[3]) ^
                  (a[1] & b[0]) ^ (a[0] & b[1]);
    assign q[2] = (a[3] & b[3]) ^ (a[2] & b[0]) ^ (a[1] & b[1]) ^ (a[0] & b[2]);
    assign q[3] = (a[3] & b[3]) ^ (a[3] & b[2]) ^ (a[2] & b[3]) ^ (a[3] & b[1]) ^
                  (a[2] & b[2]) ^ (a[1] & b[3]) ^ (a[3] & b[0]) ^ (a[2] & b[1]) ^
                  (a[1] & b[2]) ^ (a[0] & b[3]);
endmodule

module GF_2_4_Sqr2 (
    output wire [3:0] q,
    input  wire [3:0] a
);
    assign q[0] = a[0] ^ a[2] ^ a[3];
    assign q[1] = a[3];
    assign q[2] = a[1] ^ a[3];
    assign q[3] = a[2] ^ a[3];
endmodule

module GF_2_4_Inv2_LUT (
    output reg  [3:0] out,
    input  wire [3:0] in
);
    always @(*) begin
        case (in)
            4'h0: out = 4'b0000;
            4'h1: out = 4'b0001;
            4'h2: out = 4'b1100;
            4'h3: out = 4'b1000;
            4'h4: out = 4'b0110;
            4'h5: out = 4'b1111;
            4'h6: out = 4'b0100;
            4'h7: out = 4'b1110;
            4'h8: out = 4'b0011;
            4'h9: out = 4'b1101;
            4'hA: out = 4'b1011;
            4'hB: out = 4'b1010;
            4'hC: out = 4'b0010;
            4'hD: out = 4'b1001;
            4'hE: out = 4'b0111;
            4'hF: out = 4'b0101;
        endcase
    end
endmodule

module riscv_crypto_sbox_aes_2_2_8 (
    input  wire [7:0] in,
    output wire [7:0] out
);
    // Explorer candidate GF((2^4)^2) S-box #2_2_8.
    // This is used as an experimental forward AES S-box replacement so we can
    // compare it against the current shared-core implementation in Vortex.

    wire [3:0] inH, inL, inH2, inH2E, inL2_add_inHL, inH_add_inL;
    wire [3:0] outH, outL, d, d_inv;

    assign inH[3] = in[7] ^ in[6] ^ in[4] ^ in[1];
    assign inH[2] = in[6] ^ in[5] ^ in[4] ^ in[3] ^ in[2] ^ in[1];
    assign inH[1] = in[3] ^ in[2];
    assign inH[0] = in[7] ^ in[6] ^ in[4];
    assign inL[3] = in[2];
    assign inL[2] = in[7] ^ in[6] ^ in[5] ^ in[4] ^ in[3];
    assign inL[1] = in[7] ^ in[6] ^ in[5] ^ in[2];
    assign inL[0] = in[7] ^ in[5] ^ in[4] ^ in[3] ^ in[0];

    GF_2_4_Sqr2 u_sqr_H (.q(inH2), .a(inH));
    GF_2_4_Mul2 u_mul_constE (.q(inH2E), .a(inH2), .b(4'h3));
    assign inH_add_inL = inH ^ inL;
    GF_2_4_Mul2 u_mul_H_L (.q(inL2_add_inHL), .a(inH_add_inL), .b(inL));
    assign d = inH2E ^ inL2_add_inHL;
    GF_2_4_Inv2_LUT u_inv (.out(d_inv), .in(d));
    GF_2_4_Mul2 u_mul_H_di (.q(outH), .a(inH), .b(d_inv));
    GF_2_4_Mul2 u_mul_H_L_di (.q(outL), .a(inH_add_inL), .b(d_inv));

    assign out[7] = outL[2];
    assign out[6] = ~(outH[3] ^ outH[2] ^ outH[0]);
    assign out[5] = ~(outH[3] ^ outH[1] ^ outL[3] ^ outL[1]);
    assign out[4] = outH[2] ^ outH[1] ^ outH[0] ^ outL[3] ^ outL[0];
    assign out[3] = outH[3] ^ outL[1] ^ outL[0];
    assign out[2] = outH[2] ^ outL[0];
    assign out[1] = ~(outH[2] ^ outH[1] ^ outH[0] ^ outL[2] ^ outL[0]);
    assign out[0] = ~(outH[3] ^ outH[2] ^ outL[1] ^ outL[0]);
endmodule

module riscv_crypto_sbox_aesi_2_2_8 (
    input  wire [7:0] in,
    output wire [7:0] out
);
    // Direct inverse for explorer candidate GF((2^4)^2) S-box #2_2_8.
    // This undoes the combined output transform, applies the same composite
    // field inversion core, then applies the inverse of the input mapping.

    wire [7:0] z;
    wire [3:0] inH, inL, inH2, inH2E, inL2_add_inHL, inH_add_inL;
    wire [3:0] outH, outL, d, d_inv;
    wire [7:0] mapped_in;

    assign z = in ^ 8'h63;

    assign inH[0] = z[0] ^ z[4] ^ z[5];
    assign inH[1] = z[0] ^ z[1] ^ z[2] ^ z[4] ^ z[5] ^ z[7];
    assign inH[2] = z[0] ^ z[3];
    assign inH[3] = z[3] ^ z[4] ^ z[5] ^ z[6];
    assign inL[0] = z[0] ^ z[2] ^ z[3];
    assign inL[1] = z[0] ^ z[2] ^ z[3] ^ z[4] ^ z[5] ^ z[6];
    assign inL[2] = z[7];
    assign inL[3] = z[1] ^ z[4] ^ z[7];

    GF_2_4_Sqr2 u_sqr_H (.q(inH2), .a(inH));
    GF_2_4_Mul2 u_mul_constE (.q(inH2E), .a(inH2), .b(4'h3));
    assign inH_add_inL = inH ^ inL;
    GF_2_4_Mul2 u_mul_H_L (.q(inL2_add_inHL), .a(inH_add_inL), .b(inL));
    assign d = inH2E ^ inL2_add_inHL;
    GF_2_4_Inv2_LUT u_inv (.out(d_inv), .in(d));
    GF_2_4_Mul2 u_mul_H_di (.q(outH), .a(inH), .b(d_inv));
    GF_2_4_Mul2 u_mul_H_L_di (.q(outL), .a(inH_add_inL), .b(d_inv));

    assign mapped_in[0] = outH[1] ^ outH[2] ^ outH[3] ^ outL[0] ^ outL[1] ^ outL[2] ^ outL[3];
    assign mapped_in[1] = outH[0] ^ outH[3];
    assign mapped_in[2] = outL[3];
    assign mapped_in[3] = outH[1] ^ outL[3];
    assign mapped_in[4] = outH[1] ^ outL[1] ^ outL[2];
    assign mapped_in[5] = outH[0] ^ outH[1] ^ outL[2] ^ outL[3];
    assign mapped_in[6] = outH[1] ^ outH[2] ^ outH[3] ^ outL[1] ^ outL[3];
    assign mapped_in[7] = outH[0] ^ outH[2] ^ outH[3] ^ outL[2] ^ outL[3];

    assign out = mapped_in;
endmodule

module riscv_crypto_sbox_aes_lut (
    input  wire [7:0] in,
    output reg  [7:0] out
);
    // Direct AES S-box lookup table adapted from aes_sbox_explorer/aes_sbox_lut.v.
    always @(*) begin
        case (in)
            8'h00: out = 8'h63; 8'h01: out = 8'h7c; 8'h02: out = 8'h77; 8'h03: out = 8'h7b;
            8'h04: out = 8'hf2; 8'h05: out = 8'h6b; 8'h06: out = 8'h6f; 8'h07: out = 8'hc5;
            8'h08: out = 8'h30; 8'h09: out = 8'h01; 8'h0a: out = 8'h67; 8'h0b: out = 8'h2b;
            8'h0c: out = 8'hfe; 8'h0d: out = 8'hd7; 8'h0e: out = 8'hab; 8'h0f: out = 8'h76;
            8'h10: out = 8'hca; 8'h11: out = 8'h82; 8'h12: out = 8'hc9; 8'h13: out = 8'h7d;
            8'h14: out = 8'hfa; 8'h15: out = 8'h59; 8'h16: out = 8'h47; 8'h17: out = 8'hf0;
            8'h18: out = 8'had; 8'h19: out = 8'hd4; 8'h1a: out = 8'ha2; 8'h1b: out = 8'haf;
            8'h1c: out = 8'h9c; 8'h1d: out = 8'ha4; 8'h1e: out = 8'h72; 8'h1f: out = 8'hc0;
            8'h20: out = 8'hb7; 8'h21: out = 8'hfd; 8'h22: out = 8'h93; 8'h23: out = 8'h26;
            8'h24: out = 8'h36; 8'h25: out = 8'h3f; 8'h26: out = 8'hf7; 8'h27: out = 8'hcc;
            8'h28: out = 8'h34; 8'h29: out = 8'ha5; 8'h2a: out = 8'he5; 8'h2b: out = 8'hf1;
            8'h2c: out = 8'h71; 8'h2d: out = 8'hd8; 8'h2e: out = 8'h31; 8'h2f: out = 8'h15;
            8'h30: out = 8'h04; 8'h31: out = 8'hc7; 8'h32: out = 8'h23; 8'h33: out = 8'hc3;
            8'h34: out = 8'h18; 8'h35: out = 8'h96; 8'h36: out = 8'h05; 8'h37: out = 8'h9a;
            8'h38: out = 8'h07; 8'h39: out = 8'h12; 8'h3a: out = 8'h80; 8'h3b: out = 8'he2;
            8'h3c: out = 8'heb; 8'h3d: out = 8'h27; 8'h3e: out = 8'hb2; 8'h3f: out = 8'h75;
            8'h40: out = 8'h09; 8'h41: out = 8'h83; 8'h42: out = 8'h2c; 8'h43: out = 8'h1a;
            8'h44: out = 8'h1b; 8'h45: out = 8'h6e; 8'h46: out = 8'h5a; 8'h47: out = 8'ha0;
            8'h48: out = 8'h52; 8'h49: out = 8'h3b; 8'h4a: out = 8'hd6; 8'h4b: out = 8'hb3;
            8'h4c: out = 8'h29; 8'h4d: out = 8'he3; 8'h4e: out = 8'h2f; 8'h4f: out = 8'h84;
            8'h50: out = 8'h53; 8'h51: out = 8'hd1; 8'h52: out = 8'h00; 8'h53: out = 8'hed;
            8'h54: out = 8'h20; 8'h55: out = 8'hfc; 8'h56: out = 8'hb1; 8'h57: out = 8'h5b;
            8'h58: out = 8'h6a; 8'h59: out = 8'hcb; 8'h5a: out = 8'hbe; 8'h5b: out = 8'h39;
            8'h5c: out = 8'h4a; 8'h5d: out = 8'h4c; 8'h5e: out = 8'h58; 8'h5f: out = 8'hcf;
            8'h60: out = 8'hd0; 8'h61: out = 8'hef; 8'h62: out = 8'haa; 8'h63: out = 8'hfb;
            8'h64: out = 8'h43; 8'h65: out = 8'h4d; 8'h66: out = 8'h33; 8'h67: out = 8'h85;
            8'h68: out = 8'h45; 8'h69: out = 8'hf9; 8'h6a: out = 8'h02; 8'h6b: out = 8'h7f;
            8'h6c: out = 8'h50; 8'h6d: out = 8'h3c; 8'h6e: out = 8'h9f; 8'h6f: out = 8'ha8;
            8'h70: out = 8'h51; 8'h71: out = 8'ha3; 8'h72: out = 8'h40; 8'h73: out = 8'h8f;
            8'h74: out = 8'h92; 8'h75: out = 8'h9d; 8'h76: out = 8'h38; 8'h77: out = 8'hf5;
            8'h78: out = 8'hbc; 8'h79: out = 8'hb6; 8'h7a: out = 8'hda; 8'h7b: out = 8'h21;
            8'h7c: out = 8'h10; 8'h7d: out = 8'hff; 8'h7e: out = 8'hf3; 8'h7f: out = 8'hd2;
            8'h80: out = 8'hcd; 8'h81: out = 8'h0c; 8'h82: out = 8'h13; 8'h83: out = 8'hec;
            8'h84: out = 8'h5f; 8'h85: out = 8'h97; 8'h86: out = 8'h44; 8'h87: out = 8'h17;
            8'h88: out = 8'hc4; 8'h89: out = 8'ha7; 8'h8a: out = 8'h7e; 8'h8b: out = 8'h3d;
            8'h8c: out = 8'h64; 8'h8d: out = 8'h5d; 8'h8e: out = 8'h19; 8'h8f: out = 8'h73;
            8'h90: out = 8'h60; 8'h91: out = 8'h81; 8'h92: out = 8'h4f; 8'h93: out = 8'hdc;
            8'h94: out = 8'h22; 8'h95: out = 8'h2a; 8'h96: out = 8'h90; 8'h97: out = 8'h88;
            8'h98: out = 8'h46; 8'h99: out = 8'hee; 8'h9a: out = 8'hb8; 8'h9b: out = 8'h14;
            8'h9c: out = 8'hde; 8'h9d: out = 8'h5e; 8'h9e: out = 8'h0b; 8'h9f: out = 8'hdb;
            8'ha0: out = 8'he0; 8'ha1: out = 8'h32; 8'ha2: out = 8'h3a; 8'ha3: out = 8'h0a;
            8'ha4: out = 8'h49; 8'ha5: out = 8'h06; 8'ha6: out = 8'h24; 8'ha7: out = 8'h5c;
            8'ha8: out = 8'hc2; 8'ha9: out = 8'hd3; 8'haa: out = 8'hac; 8'hab: out = 8'h62;
            8'hac: out = 8'h91; 8'had: out = 8'h95; 8'hae: out = 8'he4; 8'haf: out = 8'h79;
            8'hb0: out = 8'he7; 8'hb1: out = 8'hc8; 8'hb2: out = 8'h37; 8'hb3: out = 8'h6d;
            8'hb4: out = 8'h8d; 8'hb5: out = 8'hd5; 8'hb6: out = 8'h4e; 8'hb7: out = 8'ha9;
            8'hb8: out = 8'h6c; 8'hb9: out = 8'h56; 8'hba: out = 8'hf4; 8'hbb: out = 8'hea;
            8'hbc: out = 8'h65; 8'hbd: out = 8'h7a; 8'hbe: out = 8'hae; 8'hbf: out = 8'h08;
            8'hc0: out = 8'hba; 8'hc1: out = 8'h78; 8'hc2: out = 8'h25; 8'hc3: out = 8'h2e;
            8'hc4: out = 8'h1c; 8'hc5: out = 8'ha6; 8'hc6: out = 8'hb4; 8'hc7: out = 8'hc6;
            8'hc8: out = 8'he8; 8'hc9: out = 8'hdd; 8'hca: out = 8'h74; 8'hcb: out = 8'h1f;
            8'hcc: out = 8'h4b; 8'hcd: out = 8'hbd; 8'hce: out = 8'h8b; 8'hcf: out = 8'h8a;
            8'hd0: out = 8'h70; 8'hd1: out = 8'h3e; 8'hd2: out = 8'hb5; 8'hd3: out = 8'h66;
            8'hd4: out = 8'h48; 8'hd5: out = 8'h03; 8'hd6: out = 8'hf6; 8'hd7: out = 8'h0e;
            8'hd8: out = 8'h61; 8'hd9: out = 8'h35; 8'hda: out = 8'h57; 8'hdb: out = 8'hb9;
            8'hdc: out = 8'h86; 8'hdd: out = 8'hc1; 8'hde: out = 8'h1d; 8'hdf: out = 8'h9e;
            8'he0: out = 8'he1; 8'he1: out = 8'hf8; 8'he2: out = 8'h98; 8'he3: out = 8'h11;
            8'he4: out = 8'h69; 8'he5: out = 8'hd9; 8'he6: out = 8'h8e; 8'he7: out = 8'h94;
            8'he8: out = 8'h9b; 8'he9: out = 8'h1e; 8'hea: out = 8'h87; 8'heb: out = 8'he9;
            8'hec: out = 8'hce; 8'hed: out = 8'h55; 8'hee: out = 8'h28; 8'hef: out = 8'hdf;
            8'hf0: out = 8'h8c; 8'hf1: out = 8'ha1; 8'hf2: out = 8'h89; 8'hf3: out = 8'h0d;
            8'hf4: out = 8'hbf; 8'hf5: out = 8'he6; 8'hf6: out = 8'h42; 8'hf7: out = 8'h68;
            8'hf8: out = 8'h41; 8'hf9: out = 8'h99; 8'hfa: out = 8'h2d; 8'hfb: out = 8'h0f;
            8'hfc: out = 8'hb0; 8'hfd: out = 8'h54; 8'hfe: out = 8'hbb; 8'hff: out = 8'h16;
        endcase
    end
endmodule

module riscv_crypto_sbox_aesi_lut (
    input  wire [7:0] in,
    output reg  [7:0] out
);
    // Direct inverse AES S-box lookup table.
    always @(*) begin
        case (in)
            8'h00: out = 8'h52; 8'h01: out = 8'h09; 8'h02: out = 8'h6a; 8'h03: out = 8'hd5;
            8'h04: out = 8'h30; 8'h05: out = 8'h36; 8'h06: out = 8'ha5; 8'h07: out = 8'h38;
            8'h08: out = 8'hbf; 8'h09: out = 8'h40; 8'h0a: out = 8'ha3; 8'h0b: out = 8'h9e;
            8'h0c: out = 8'h81; 8'h0d: out = 8'hf3; 8'h0e: out = 8'hd7; 8'h0f: out = 8'hfb;
            8'h10: out = 8'h7c; 8'h11: out = 8'he3; 8'h12: out = 8'h39; 8'h13: out = 8'h82;
            8'h14: out = 8'h9b; 8'h15: out = 8'h2f; 8'h16: out = 8'hff; 8'h17: out = 8'h87;
            8'h18: out = 8'h34; 8'h19: out = 8'h8e; 8'h1a: out = 8'h43; 8'h1b: out = 8'h44;
            8'h1c: out = 8'hc4; 8'h1d: out = 8'hde; 8'h1e: out = 8'he9; 8'h1f: out = 8'hcb;
            8'h20: out = 8'h54; 8'h21: out = 8'h7b; 8'h22: out = 8'h94; 8'h23: out = 8'h32;
            8'h24: out = 8'ha6; 8'h25: out = 8'hc2; 8'h26: out = 8'h23; 8'h27: out = 8'h3d;
            8'h28: out = 8'hee; 8'h29: out = 8'h4c; 8'h2a: out = 8'h95; 8'h2b: out = 8'h0b;
            8'h2c: out = 8'h42; 8'h2d: out = 8'hfa; 8'h2e: out = 8'hc3; 8'h2f: out = 8'h4e;
            8'h30: out = 8'h08; 8'h31: out = 8'h2e; 8'h32: out = 8'ha1; 8'h33: out = 8'h66;
            8'h34: out = 8'h28; 8'h35: out = 8'hd9; 8'h36: out = 8'h24; 8'h37: out = 8'hb2;
            8'h38: out = 8'h76; 8'h39: out = 8'h5b; 8'h3a: out = 8'ha2; 8'h3b: out = 8'h49;
            8'h3c: out = 8'h6d; 8'h3d: out = 8'h8b; 8'h3e: out = 8'hd1; 8'h3f: out = 8'h25;
            8'h40: out = 8'h72; 8'h41: out = 8'hf8; 8'h42: out = 8'hf6; 8'h43: out = 8'h64;
            8'h44: out = 8'h86; 8'h45: out = 8'h68; 8'h46: out = 8'h98; 8'h47: out = 8'h16;
            8'h48: out = 8'hd4; 8'h49: out = 8'ha4; 8'h4a: out = 8'h5c; 8'h4b: out = 8'hcc;
            8'h4c: out = 8'h5d; 8'h4d: out = 8'h65; 8'h4e: out = 8'hb6; 8'h4f: out = 8'h92;
            8'h50: out = 8'h6c; 8'h51: out = 8'h70; 8'h52: out = 8'h48; 8'h53: out = 8'h50;
            8'h54: out = 8'hfd; 8'h55: out = 8'hed; 8'h56: out = 8'hb9; 8'h57: out = 8'hda;
            8'h58: out = 8'h5e; 8'h59: out = 8'h15; 8'h5a: out = 8'h46; 8'h5b: out = 8'h57;
            8'h5c: out = 8'ha7; 8'h5d: out = 8'h8d; 8'h5e: out = 8'h9d; 8'h5f: out = 8'h84;
            8'h60: out = 8'h90; 8'h61: out = 8'hd8; 8'h62: out = 8'hab; 8'h63: out = 8'h00;
            8'h64: out = 8'h8c; 8'h65: out = 8'hbc; 8'h66: out = 8'hd3; 8'h67: out = 8'h0a;
            8'h68: out = 8'hf7; 8'h69: out = 8'he4; 8'h6a: out = 8'h58; 8'h6b: out = 8'h05;
            8'h6c: out = 8'hb8; 8'h6d: out = 8'hb3; 8'h6e: out = 8'h45; 8'h6f: out = 8'h06;
            8'h70: out = 8'hd0; 8'h71: out = 8'h2c; 8'h72: out = 8'h1e; 8'h73: out = 8'h8f;
            8'h74: out = 8'hca; 8'h75: out = 8'h3f; 8'h76: out = 8'h0f; 8'h77: out = 8'h02;
            8'h78: out = 8'hc1; 8'h79: out = 8'haf; 8'h7a: out = 8'hbd; 8'h7b: out = 8'h03;
            8'h7c: out = 8'h01; 8'h7d: out = 8'h13; 8'h7e: out = 8'h8a; 8'h7f: out = 8'h6b;
            8'h80: out = 8'h3a; 8'h81: out = 8'h91; 8'h82: out = 8'h11; 8'h83: out = 8'h41;
            8'h84: out = 8'h4f; 8'h85: out = 8'h67; 8'h86: out = 8'hdc; 8'h87: out = 8'hea;
            8'h88: out = 8'h97; 8'h89: out = 8'hf2; 8'h8a: out = 8'hcf; 8'h8b: out = 8'hce;
            8'h8c: out = 8'hf0; 8'h8d: out = 8'hb4; 8'h8e: out = 8'he6; 8'h8f: out = 8'h73;
            8'h90: out = 8'h96; 8'h91: out = 8'hac; 8'h92: out = 8'h74; 8'h93: out = 8'h22;
            8'h94: out = 8'he7; 8'h95: out = 8'had; 8'h96: out = 8'h35; 8'h97: out = 8'h85;
            8'h98: out = 8'he2; 8'h99: out = 8'hf9; 8'h9a: out = 8'h37; 8'h9b: out = 8'he8;
            8'h9c: out = 8'h1c; 8'h9d: out = 8'h75; 8'h9e: out = 8'hdf; 8'h9f: out = 8'h6e;
            8'ha0: out = 8'h47; 8'ha1: out = 8'hf1; 8'ha2: out = 8'h1a; 8'ha3: out = 8'h71;
            8'ha4: out = 8'h1d; 8'ha5: out = 8'h29; 8'ha6: out = 8'hc5; 8'ha7: out = 8'h89;
            8'ha8: out = 8'h6f; 8'ha9: out = 8'hb7; 8'haa: out = 8'h62; 8'hab: out = 8'h0e;
            8'hac: out = 8'haa; 8'had: out = 8'h18; 8'hae: out = 8'hbe; 8'haf: out = 8'h1b;
            8'hb0: out = 8'hfc; 8'hb1: out = 8'h56; 8'hb2: out = 8'h3e; 8'hb3: out = 8'h4b;
            8'hb4: out = 8'hc6; 8'hb5: out = 8'hd2; 8'hb6: out = 8'h79; 8'hb7: out = 8'h20;
            8'hb8: out = 8'h9a; 8'hb9: out = 8'hdb; 8'hba: out = 8'hc0; 8'hbb: out = 8'hfe;
            8'hbc: out = 8'h78; 8'hbd: out = 8'hcd; 8'hbe: out = 8'h5a; 8'hbf: out = 8'hf4;
            8'hc0: out = 8'h1f; 8'hc1: out = 8'hdd; 8'hc2: out = 8'ha8; 8'hc3: out = 8'h33;
            8'hc4: out = 8'h88; 8'hc5: out = 8'h07; 8'hc6: out = 8'hc7; 8'hc7: out = 8'h31;
            8'hc8: out = 8'hb1; 8'hc9: out = 8'h12; 8'hca: out = 8'h10; 8'hcb: out = 8'h59;
            8'hcc: out = 8'h27; 8'hcd: out = 8'h80; 8'hce: out = 8'hec; 8'hcf: out = 8'h5f;
            8'hd0: out = 8'h60; 8'hd1: out = 8'h51; 8'hd2: out = 8'h7f; 8'hd3: out = 8'ha9;
            8'hd4: out = 8'h19; 8'hd5: out = 8'hb5; 8'hd6: out = 8'h4a; 8'hd7: out = 8'h0d;
            8'hd8: out = 8'h2d; 8'hd9: out = 8'he5; 8'hda: out = 8'h7a; 8'hdb: out = 8'h9f;
            8'hdc: out = 8'h93; 8'hdd: out = 8'hc9; 8'hde: out = 8'h9c; 8'hdf: out = 8'hef;
            8'he0: out = 8'ha0; 8'he1: out = 8'he0; 8'he2: out = 8'h3b; 8'he3: out = 8'h4d;
            8'he4: out = 8'hae; 8'he5: out = 8'h2a; 8'he6: out = 8'hf5; 8'he7: out = 8'hb0;
            8'he8: out = 8'hc8; 8'he9: out = 8'heb; 8'hea: out = 8'hbb; 8'heb: out = 8'h3c;
            8'hec: out = 8'h83; 8'hed: out = 8'h53; 8'hee: out = 8'h99; 8'hef: out = 8'h61;
            8'hf0: out = 8'h17; 8'hf1: out = 8'h2b; 8'hf2: out = 8'h04; 8'hf3: out = 8'h7e;
            8'hf4: out = 8'hba; 8'hf5: out = 8'h77; 8'hf6: out = 8'hd6; 8'hf7: out = 8'h26;
            8'hf8: out = 8'he1; 8'hf9: out = 8'h69; 8'hfa: out = 8'h14; 8'hfb: out = 8'h63;
            8'hfc: out = 8'h55; 8'hfd: out = 8'h21; 8'hfe: out = 8'h0c; 8'hff: out = 8'h7d;
        endcase
    end
endmodule

module VX_aes #(
    parameter LANES = 1
) (
    input  wire                     clk,
    input  wire                     reset,
    input  wire [LANES-1:0][31:0]   rs1_data,
    input  wire [LANES-1:0][31:0]   rs2_data,
    input  wire [1:0]               bs,
    input  wire                     op_saes32_encs,
    input  wire                     op_saes32_encsm,
    input  wire                     op_saes32_decs,
    input  wire                     op_saes32_decsm,
    output wire [LANES-1:0][31:0]   result,
    input  wire                     valid_in,
    output wire                     ready_in,
    output wire                     valid_out,
    input  wire                     ready_out
);
    `UNUSED_VAR(op_saes32_encs)

    wire stall_out = ~ready_out && valid_out;

    reg [LANES-1:0][7:0] sel_byte;
    for (genvar i = 0; i < LANES; ++i) begin : g_sel_byte
        always @(*) begin
            // AES32 instructions operate on one selected byte from rs2.
            case (bs)
                2'b00: sel_byte[i] = rs2_data[i][7:0];
                2'b01: sel_byte[i] = rs2_data[i][15:8];
                2'b10: sel_byte[i] = rs2_data[i][23:16];
                2'b11: sel_byte[i] = rs2_data[i][31:24];
            endcase
        end
    end

    wire dec = op_saes32_decs || op_saes32_decsm;
    wire mix = op_saes32_encsm || op_saes32_decsm;

`ifdef VX_AES_USE_LUT_SBOX
    wire [LANES-1:0][7:0] stage1_fwd_alt_in;
    wire [LANES-1:0][7:0] stage1_inv_alt_in;
`elsif VX_AES_USE_EXPLORER_SBOX_228
    wire [LANES-1:0][7:0] stage1_fwd_alt_in;
    wire [LANES-1:0][7:0] stage1_inv_alt_in;
`else
    wire [LANES-1:0][17:0] stage1_in;
`endif
    for (genvar i = 0; i < LANES; ++i) begin : g_stage1_in
`ifdef VX_AES_USE_LUT_SBOX
        riscv_crypto_sbox_aes_lut alt_fwd (.out(stage1_fwd_alt_in[i]), .in(sel_byte[i]));
        riscv_crypto_sbox_aesi_lut alt_inv (.out(stage1_inv_alt_in[i]), .in(sel_byte[i]));
`elsif VX_AES_USE_EXPLORER_SBOX_228
        riscv_crypto_sbox_aes_2_2_8 alt_fwd (.out(stage1_fwd_alt_in[i]), .in(sel_byte[i]));
        riscv_crypto_sbox_aesi_2_2_8 alt_inv (.out(stage1_inv_alt_in[i]), .in(sel_byte[i]));
`else
        wire [20:0] top_fwd;
        wire [20:0] top_inv;
        // Both S-box directions share the same middle inversion logic. The
        // dec bit selects which basis transform pair to use around it.
        riscv_crypto_sbox_aes_top top (.y(top_fwd), .x(sel_byte[i]));
        riscv_crypto_sbox_aesi_top inv_top (.y(top_inv), .x(sel_byte[i]));
        riscv_crypto_sbox_inv_mid mid (.y(stage1_in[i]), .x(dec ? top_inv : top_fwd));
`endif
    end

    wire                   stage1_valid;
    wire                   stage1_dec;
    wire                   stage1_mix;
    wire [1:0]             stage1_bs;
    wire [LANES-1:0][31:0] stage1_rs1_data;
`ifdef VX_AES_USE_LUT_SBOX
    wire [LANES-1:0][7:0]  stage1_fwd_alt_out;
    wire [LANES-1:0][7:0]  stage1_inv_alt_out;
`elsif VX_AES_USE_EXPLORER_SBOX_228
    wire [LANES-1:0][7:0]  stage1_fwd_alt_out;
    wire [LANES-1:0][7:0]  stage1_inv_alt_out;
`else
    wire [LANES-1:0][17:0] stage1_out;
`endif

    assign ready_in = ~stall_out || ~stage1_valid;
    assign valid_out = stage1_valid;

    VX_pipe_register #(
`ifdef VX_AES_USE_LUT_SBOX
        .DATAW  (1 + (LANES * 8) + (LANES * 8) + 1 + 1 + 2 + (LANES * 32)),
`elsif VX_AES_USE_EXPLORER_SBOX_228
        .DATAW  (1 + (LANES * 8) + (LANES * 8) + 1 + 1 + 2 + (LANES * 32)),
`else
        .DATAW  (1 + (LANES * 18) + 1 + 1 + 2 + (LANES * 32)),
`endif
        .RESETW (1)
    ) stage1_reg (
        .clk      (clk),
        .reset    (reset),
        .enable   (ready_in),
        // Keep the S-box middle stage result together with the operation mode,
        // byte index, and rs1 so the second stage can finish the AES32 op.
`ifdef VX_AES_USE_LUT_SBOX
        .data_in  ({valid_in, stage1_fwd_alt_in, stage1_inv_alt_in, dec, mix, bs, rs1_data}),
        .data_out ({stage1_valid, stage1_fwd_alt_out, stage1_inv_alt_out, stage1_dec, stage1_mix, stage1_bs, stage1_rs1_data})
`elsif VX_AES_USE_EXPLORER_SBOX_228
        .data_in  ({valid_in, stage1_fwd_alt_in, stage1_inv_alt_in, dec, mix, bs, rs1_data}),
        .data_out ({stage1_valid, stage1_fwd_alt_out, stage1_inv_alt_out, stage1_dec, stage1_mix, stage1_bs, stage1_rs1_data})
`else
        .data_in  ({valid_in, stage1_in, dec, mix, bs, rs1_data}),
        .data_out ({stage1_valid, stage1_out, stage1_dec, stage1_mix, stage1_bs, stage1_rs1_data})
`endif
    );

    wire [LANES-1:0][7:0] sbox_out;
    for (genvar i = 0; i < LANES; ++i) begin : g_sbox_out
`ifdef VX_AES_USE_LUT_SBOX
        assign sbox_out[i] = stage1_dec ? stage1_inv_alt_out[i] : stage1_fwd_alt_out[i];
`elsif VX_AES_USE_EXPLORER_SBOX_228
        assign sbox_out[i] = stage1_dec ? stage1_inv_alt_out[i] : stage1_fwd_alt_out[i];
`else
        wire [7:0] outer_inv;
        wire [7:0] outer_fwd;
        riscv_crypto_sbox_aesi_out inv_out (.y(outer_inv), .x(stage1_out[i]));
        riscv_crypto_sbox_aes_out out (.y(outer_fwd), .x(stage1_out[i]));
        assign sbox_out[i] = stage1_dec ? outer_inv : outer_fwd;
`endif
    end

    function automatic [7:0] xtime2(input [7:0] a);
        // GF(2^8) multiply by x modulo x^8 + x^4 + x^3 + x + 1 (0x11b).
        xtime2 = {a[6:0], 1'b0} ^ (a[7] ? 8'h1b : 8'b0);
    endfunction

    function automatic [7:0] xtimeN(
        input [7:0] a,
        input [3:0] b
    );
        // Multiply by a small AES constant using the xtime decomposition.
        xtimeN =
            (b[0] ? a : 0) ^
            (b[1] ? xtime2(a) : 0) ^
            (b[2] ? xtime2(xtime2(a)) : 0) ^
            (b[3] ? xtime2(xtime2(xtime2(a))) : 0);
    endfunction

    for (genvar i = 0; i < LANES; ++i) begin : g_result
        // Coefficients for MixColumns / InvMixColumns contribution of one byte.
        wire [7:0] mix_b3 = xtimeN(sbox_out[i], (stage1_dec ? 4'd11 : 4'd3));
        wire [7:0] mix_b2 = stage1_dec ? xtimeN(sbox_out[i], 4'd13) : sbox_out[i];
        wire [7:0] mix_b1 = stage1_dec ? xtimeN(sbox_out[i], 4'd9)  : sbox_out[i];
        wire [7:0] mix_b0 = xtimeN(sbox_out[i], (stage1_dec ? 4'd14 : 4'd2));

        wire [31:0] mixed = {mix_b3, mix_b2, mix_b1, mix_b0};
        // Non-mix variants contribute only the substituted byte in the low byte.
        wire [31:0] zext  = stage1_mix ? mixed : {24'b0, sbox_out[i]};
        // Rotate into the byte lane selected by bs, matching AES32 instruction
        // semantics before XORing with rs1.
        wire [31:0] rotated =
            ({32{stage1_bs == 2'b00}} & {zext}) |
            ({32{stage1_bs == 2'b01}} & {zext[23:0], zext[31:24]}) |
            ({32{stage1_bs == 2'b10}} & {zext[15:0], zext[31:16]}) |
            ({32{stage1_bs == 2'b11}} & {zext[7:0],  zext[31:8]});

        // rs1 carries the running 32-bit word that this byte contribution is
        // merged into.
        assign result[i] = rotated ^ stage1_rs1_data[i];
    end

endmodule

module VX_aes64 #(
    parameter LANES = 1
) (
    input  wire                     clk,
    input  wire                     reset,
    input  wire [LANES-1:0][63:0]   rs1_data,
    input  wire [LANES-1:0][63:0]   rs2_data,
    input  wire [3:0]               round_imm,
    input  wire                     op_aes64es,
    input  wire                     op_aes64esm,
    input  wire                     op_aes64ds,
    input  wire                     op_aes64dsm,
    input  wire                     op_aes64im,
    input  wire                     op_aes64ks1i,
    input  wire                     op_aes64ks2,
    output wire [LANES-1:0][63:0]   result,
    input  wire                     valid_in,
    output wire                     ready_in,
    output wire                     valid_out,
    input  wire                     ready_out
);
    function automatic [7:0] xtime2(input [7:0] a);
        xtime2 = {a[6:0], 1'b0} ^ (a[7] ? 8'h1b : 8'b0);
    endfunction

    function automatic [7:0] xtimeN(
        input [7:0] a,
        input [3:0] b
    );
        xtimeN =
            (b[0] ? a : 0) ^
            (b[1] ? xtime2(a) : 0) ^
            (b[2] ? xtime2(xtime2(a)) : 0) ^
            (b[3] ? xtime2(xtime2(xtime2(a))) : 0);
    endfunction

    function automatic [31:0] pack_bytes(
        input [7:0] b0,
        input [7:0] b1,
        input [7:0] b2,
        input [7:0] b3
    );
        pack_bytes = {b3, b2, b1, b0};
    endfunction

    function automatic [31:0] mixcolumn_fwd(input [31:0] word);
        reg [7:0] b0, b1, b2, b3;
        begin
            b0 = word[7:0];
            b1 = word[15:8];
            b2 = word[23:16];
            b3 = word[31:24];
            mixcolumn_fwd = pack_bytes(
                xtime2(b0) ^ b1 ^ xtime2(b1) ^ b2 ^ b3,
                b0 ^ xtime2(b1) ^ b2 ^ xtime2(b2) ^ b3,
                b0 ^ b1 ^ xtime2(b2) ^ b3 ^ xtime2(b3),
                b0 ^ xtime2(b0) ^ b1 ^ b2 ^ xtime2(b3)
            );
        end
    endfunction

`ifndef CRYPTO_AES_ENC_ONLY
    // InvMixColumns: only the inverse cipher (decryption) needs it. Set
    // CRYPTO_AES_ENC_ONLY=1 for an AEAD/CTR-only build (AES-GCM never runs the
    // inverse cipher) to drop the whole decrypt datapath -- the 8 inverse
    // S-boxes per lane and this InvMixColumns -- keeping only the forward cipher
    // + key schedule (aes64es/esm/ks1i/ks2).
    function automatic [31:0] mixcolumn_inv(input [31:0] word);
        reg [7:0] b0, b1, b2, b3;
        reg [7:0] x0, x1, x2, x3;
        reg [7:0] x20, x21, x22, x23;
        reg [7:0] x30, x31, x32, x33;
        begin
            b0 = word[7:0];
            b1 = word[15:8];
            b2 = word[23:16];
            b3 = word[31:24];
            x0 = xtime2(b0);
            x1 = xtime2(b1);
            x2 = xtime2(b2);
            x3 = xtime2(b3);
            x20 = xtime2(x0);
            x21 = xtime2(x1);
            x22 = xtime2(x2);
            x23 = xtime2(x3);
            x30 = xtime2(x20);
            x31 = xtime2(x21);
            x32 = xtime2(x22);
            x33 = xtime2(x23);
            mixcolumn_inv = pack_bytes(
                x0 ^ x20 ^ x30 ^ b1 ^ x1 ^ x31 ^ b2 ^ x22 ^ x32 ^ b3 ^ x33,
                b0 ^ x30 ^ x1 ^ x21 ^ x31 ^ b2 ^ x2 ^ x32 ^ b3 ^ x23 ^ x33,
                b0 ^ x20 ^ x30 ^ b1 ^ x31 ^ x2 ^ x22 ^ x32 ^ b3 ^ x3 ^ x33,
                b0 ^ x0 ^ x30 ^ b1 ^ x21 ^ x31 ^ b2 ^ x32 ^ x3 ^ x23 ^ x33
            );
        end
    endfunction
`endif

    function automatic [31:0] aes_rcon(input [3:0] round);
        case (round)
            4'd0: aes_rcon = 32'h00000000;
            4'd1: aes_rcon = 32'h00000001;
            4'd2: aes_rcon = 32'h00000002;
            4'd3: aes_rcon = 32'h00000004;
            4'd4: aes_rcon = 32'h00000008;
            4'd5: aes_rcon = 32'h00000010;
            4'd6: aes_rcon = 32'h00000020;
            4'd7: aes_rcon = 32'h00000040;
            4'd8: aes_rcon = 32'h00000080;
            4'd9: aes_rcon = 32'h0000001b;
            4'd10: aes_rcon = 32'h00000036;
            default: aes_rcon = 32'h00000000;
        endcase
    endfunction

    function automatic [7:0] get_byte32(
        input [31:0] word,
        input integer index
    );
        case (index)
            0: get_byte32 = word[7:0];
            1: get_byte32 = word[15:8];
            2: get_byte32 = word[23:16];
            default: get_byte32 = word[31:24];
        endcase
    endfunction

    wire [LANES-1:0][7:0][7:0] fwd_sbox_in;
    wire [LANES-1:0][7:0][7:0] fwd_sbox_out;
`ifndef CRYPTO_AES_ENC_ONLY
    wire [LANES-1:0][7:0][7:0] inv_sbox_in;
    wire [LANES-1:0][7:0][7:0] inv_sbox_out;
`endif

    wire [LANES-1:0][63:0] result_next;
    reg  [LANES-1:0][63:0] result_r;
    reg                    valid_r;

    assign ready_in = ~valid_r || ready_out;
    assign valid_out = valid_r;
    assign result = result_r;

`ifdef CRYPTO_AES_ENC_ONLY
    `UNUSED_VAR ({op_aes64ds, op_aes64dsm, op_aes64im})
`endif

    for (genvar i = 0; i < LANES; ++i) begin : g_lane
        wire [31:0] rs1_lo = rs1_data[i][31:0];
        wire [31:0] rs1_hi = rs1_data[i][63:32];
        wire [31:0] rs2_lo = rs2_data[i][31:0];
        wire [31:0] rs2_hi = rs2_data[i][63:32];

        wire [31:0] shift_fwd_lo = pack_bytes(rs1_lo[7:0], rs1_hi[15:8], rs2_lo[23:16], rs2_hi[31:24]);
        wire [31:0] shift_fwd_hi = pack_bytes(rs1_hi[7:0], rs2_lo[15:8], rs2_hi[23:16], rs1_lo[31:24]);
`ifndef CRYPTO_AES_ENC_ONLY
        wire [31:0] shift_inv_lo = pack_bytes(rs1_lo[7:0], rs2_hi[15:8], rs2_lo[23:16], rs1_hi[31:24]);
        wire [31:0] shift_inv_hi = pack_bytes(rs1_hi[7:0], rs1_lo[15:8], rs2_hi[23:16], rs2_lo[31:24]);
`else
        // forward ShiftRows uses only bytes 0 and 3 of rs1_lo here
        `UNUSED_VAR (rs1_lo)
`endif
        wire [31:0] ks1_word = (round_imm == 4'ha) ? rs1_hi : {rs1_hi[7:0], rs1_hi[31:8]};

        for (genvar j = 0; j < 4; ++j) begin : g_sbox_lo
            wire [7:0] shift_fwd_byte = get_byte32(shift_fwd_lo, j);
            assign fwd_sbox_in[i][j] = op_aes64ks1i ? get_byte32(ks1_word, j) : shift_fwd_byte;
            riscv_crypto_sbox_aes_lut  fwd_sbox (.out(fwd_sbox_out[i][j]), .in(fwd_sbox_in[i][j]));
`ifndef CRYPTO_AES_ENC_ONLY
            wire [7:0] shift_inv_byte = get_byte32(shift_inv_lo, j);
            assign inv_sbox_in[i][j] = shift_inv_byte;
            riscv_crypto_sbox_aesi_lut inv_sbox (.out(inv_sbox_out[i][j]), .in(inv_sbox_in[i][j]));
`endif
        end

        for (genvar j = 4; j < 8; ++j) begin : g_sbox_hi
            wire [7:0] shift_fwd_byte = get_byte32(shift_fwd_hi, j - 4);
            assign fwd_sbox_in[i][j] = op_aes64ks1i ? 8'h00 : shift_fwd_byte;
            riscv_crypto_sbox_aes_lut  fwd_sbox (.out(fwd_sbox_out[i][j]), .in(fwd_sbox_in[i][j]));
`ifndef CRYPTO_AES_ENC_ONLY
            wire [7:0] shift_inv_byte = get_byte32(shift_inv_hi, j - 4);
            assign inv_sbox_in[i][j] = shift_inv_byte;
            riscv_crypto_sbox_aesi_lut inv_sbox (.out(inv_sbox_out[i][j]), .in(inv_sbox_in[i][j]));
`endif
        end

        wire [31:0] sub_fwd_lo = pack_bytes(fwd_sbox_out[i][0], fwd_sbox_out[i][1], fwd_sbox_out[i][2], fwd_sbox_out[i][3]);
        wire [31:0] sub_fwd_hi = pack_bytes(fwd_sbox_out[i][4], fwd_sbox_out[i][5], fwd_sbox_out[i][6], fwd_sbox_out[i][7]);
`ifndef CRYPTO_AES_ENC_ONLY
        wire [31:0] sub_inv_lo = pack_bytes(inv_sbox_out[i][0], inv_sbox_out[i][1], inv_sbox_out[i][2], inv_sbox_out[i][3]);
        wire [31:0] sub_inv_hi = pack_bytes(inv_sbox_out[i][4], inv_sbox_out[i][5], inv_sbox_out[i][6], inv_sbox_out[i][7]);
`endif

        reg [63:0] lane_result;
        always @(*) begin
            lane_result = '0;
            if (op_aes64es) begin
                lane_result = {sub_fwd_hi, sub_fwd_lo};
            end else if (op_aes64esm) begin
                lane_result = {mixcolumn_fwd(sub_fwd_hi), mixcolumn_fwd(sub_fwd_lo)};
`ifndef CRYPTO_AES_ENC_ONLY
            end else if (op_aes64ds) begin
                lane_result = {sub_inv_hi, sub_inv_lo};
            end else if (op_aes64dsm) begin
                lane_result = {mixcolumn_inv(sub_inv_hi), mixcolumn_inv(sub_inv_lo)};
            end else if (op_aes64im) begin
                lane_result = {mixcolumn_inv(rs1_hi), mixcolumn_inv(rs1_lo)};
`endif
            end else if (op_aes64ks1i) begin
                lane_result = {sub_fwd_lo ^ aes_rcon(round_imm), sub_fwd_lo ^ aes_rcon(round_imm)};
            end else if (op_aes64ks2) begin
                lane_result[31:0] = rs1_hi ^ rs2_lo;
                lane_result[63:32] = (rs1_hi ^ rs2_lo) ^ rs2_hi;
            end
        end

        assign result_next[i] = lane_result;
    end

    always @(posedge clk) begin
        if (reset) begin
            valid_r <= 1'b0;
        end else if (ready_in) begin
            valid_r <= valid_in;
            result_r <= result_next;
        end
    end

endmodule
