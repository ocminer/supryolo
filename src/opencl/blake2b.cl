// SPDX-License-Identifier: MIT
// Specialized BLAKE2b-256, 80-byte input. OpenCL C 1.2.
__constant ulong IV[8]={0x6a09e667f3bcc908UL,0xbb67ae8584caa73bUL,0x3c6ef372fe94f82bUL,0xa54ff53a5f1d36f1UL,0x510e527fade682d1UL,0x9b05688c2b3e6c1fUL,0x1f83d9abfb41bd6bUL,0x5be0cd19137e2179UL};
__constant int SIGMA[10][16]={{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                             {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                             {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
                             {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                             {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
                             {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                             {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
                             {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                             {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
                             {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}};
#if AMD_ROTATE
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
inline ulong amd_ror(ulong x,uint n){
 uint2 v=as_uint2(x);
 if(n==32)return as_ulong(v.s10);
 if(n<32)return as_ulong((uint2)(amd_bitalign(v.s1,v.s0,n),amd_bitalign(v.s0,v.s1,n)));
 return as_ulong((uint2)(amd_bitalign(v.s0,v.s1,n-32),amd_bitalign(v.s1,v.s0,n-32)));
}
#define ROR(x,n) amd_ror((x),(n))
#else
#define ROR(x,n) rotate((x),(ulong)(64-(n)))
#endif
#define G(a,b,c,d,x,y) {a+=b+x;d=ROR(d^a,32);c+=d;b=ROR(b^c,24);a+=b+y;d=ROR(d^a,16);c+=d;b=ROR(b^c,63);}
inline void hash80(__global const ulong *input,ulong nonce,ulong *out) {
 ulong m[16],v[16];
 #pragma unroll
 for(int i=0;i<16;++i)m[i]=i<10?input[i]:0;
 m[4]=nonce;
 #pragma unroll
 for(int i=0;i<8;++i)v[i]=v[i+8]=IV[i];
 v[0]^=0x01010020UL;v[12]^=80UL;v[14]=~v[14];
 #if PRECOMPUTE
 #pragma unroll
 for(int i=0;i<16;++i)v[i]=input[10+i];
 #endif
 #pragma unroll
 for(int r=0;r<12;++r){
  if(!PRECOMPUTE || r!=0) G(v[0],v[4],v[8],v[12],m[SIGMA[r%10][0]],m[SIGMA[r%10][1]]);
  if(!PRECOMPUTE || r!=0) G(v[1],v[5],v[9],v[13],m[SIGMA[r%10][2]],m[SIGMA[r%10][3]]);
  G(v[2],v[6],v[10],v[14],m[SIGMA[r%10][4]],m[SIGMA[r%10][5]]);
  if(!PRECOMPUTE || r!=0) G(v[3],v[7],v[11],v[15],m[SIGMA[r%10][6]],m[SIGMA[r%10][7]]);
  G(v[0],v[5],v[10],v[15],m[SIGMA[r%10][8]],m[SIGMA[r%10][9]]);
  G(v[1],v[6],v[11],v[12],m[SIGMA[r%10][10]],m[SIGMA[r%10][11]]);
  G(v[2],v[7],v[8],v[13],m[SIGMA[r%10][12]],m[SIGMA[r%10][13]]);
  G(v[3],v[4],v[9],v[14],m[SIGMA[r%10][14]],m[SIGMA[r%10][15]]);
 }
 #pragma unroll
 for(int i=0;i<4;++i)out[i]=IV[i]^(i==0?0x01010020UL:0UL)^v[i]^v[i+8];
}
inline ulong swap64(ulong x){return as_ulong(as_uchar8(x).s76543210);}
__kernel void scan(__global const ulong *input,ulong start,uint count,ulong target,__global uint *hits,__global ulong *nonces){
 size_t i=get_global_id(0);if(i>=count)return;
 ulong h[4];hash80(input,start+i,h);
 if(swap64(h[0])<=target){uint slot=atomic_inc(hits);if(slot<4096)nonces[slot]=start+i;}
}
__kernel void hashes(__global const ulong *input,ulong start,uint count,__global ulong *out){
 size_t i=get_global_id(0);if(i>=count)return;ulong h[4];hash80(input,start+i,h);
 for(int j=0;j<4;++j)out[i*4+j]=h[j];
}
