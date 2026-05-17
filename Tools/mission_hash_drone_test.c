/*
 * Standalone test of the drone-side mission hash computation.
 * Reads canonical mission text from stdin, outputs SHA-256 hex to stdout.
 * Compile: gcc -o /tmp/drone_hash Tools/mission_hash_drone_test.c
 * Usage:   python3 Tools/mission_hash.py <plan> /dev/stdout | ./drone_hash
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* --- Exact same SHA-256 as missionHashCheck.cpp --- */

static const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
    0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
    0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
    0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
    0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
    0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
    0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
    0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
    0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z)  (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR(x,2)^ROTR(x,13)^ROTR(x,22))
#define EP1(x) (ROTR(x,6)^ROTR(x,11)^ROTR(x,25))
#define SIG0(x)(ROTR(x,7)^ROTR(x,18)^((x)>>3))
#define SIG1(x)(ROTR(x,17)^ROTR(x,19)^((x)>>10))

typedef struct { uint32_t h[8]; uint8_t block[64]; uint32_t blen; uint64_t tlen; } Ctx;

static void transform(Ctx *c, const uint8_t *d) {
    uint32_t a,b,cc,e,f,g,h,t1,t2,w[64]; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)d[i*4]<<24)|((uint32_t)d[i*4+1]<<16)|((uint32_t)d[i*4+2]<<8)|(uint32_t)d[i*4+3];
    for(i=16;i<64;i++) w[i]=SIG1(w[i-2])+w[i-7]+SIG0(w[i-15])+w[i-16];
    a=c->h[0];b=c->h[1];cc=c->h[2];uint32_t dd=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];
    for(i=0;i<64;i++){t1=h+EP1(e)+CH(e,f,g)+K[i]+w[i];t2=EP0(a)+MAJ(a,b,cc);h=g;g=f;f=e;e=dd+t1;dd=cc;cc=b;b=a;a=t1+t2;}
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=dd;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;
}

static void sha_init(Ctx *c) {
    c->h[0]=0x6a09e667;c->h[1]=0xbb67ae85;c->h[2]=0x3c6ef372;c->h[3]=0xa54ff53a;
    c->h[4]=0x510e527f;c->h[5]=0x9b05688c;c->h[6]=0x1f83d9ab;c->h[7]=0x5be0cd19;
    c->blen=0;c->tlen=0;
}

static void sha_update(Ctx *c, const void *data, size_t len) {
    const uint8_t *p=(const uint8_t*)data; c->tlen+=len;
    while(len>0){uint32_t sp=64-c->blen,n=(len<sp)?(uint32_t)len:sp;
        memcpy(c->block+c->blen,p,n);c->blen+=n;p+=n;len-=n;
        if(c->blen==64){transform(c,c->block);c->blen=0;}}
}

static void sha_final(Ctx *c, uint8_t out[32]) {
    c->block[c->blen++]=0x80;
    if(c->blen>56){memset(c->block+c->blen,0,64-c->blen);transform(c,c->block);c->blen=0;}
    memset(c->block+c->blen,0,56-c->blen);
    uint64_t bl=c->tlen*8;
    c->block[56]=(uint8_t)(bl>>56);c->block[57]=(uint8_t)(bl>>48);c->block[58]=(uint8_t)(bl>>40);c->block[59]=(uint8_t)(bl>>32);
    c->block[60]=(uint8_t)(bl>>24);c->block[61]=(uint8_t)(bl>>16);c->block[62]=(uint8_t)(bl>>8);c->block[63]=(uint8_t)bl;
    transform(c,c->block);
    int i; for(i=0;i<8;i++){out[i*4]=(uint8_t)(c->h[i]>>24);out[i*4+1]=(uint8_t)(c->h[i]>>16);out[i*4+2]=(uint8_t)(c->h[i]>>8);out[i*4+3]=(uint8_t)c->h[i];}
}

/* --- main: read stdin, hash it, print hex --- */

int main(void) {
    Ctx ctx; sha_init(&ctx);
    uint8_t buf[4096]; size_t n;
    while((n=fread(buf,1,sizeof(buf),stdin))>0)
        sha_update(&ctx,buf,n);
    uint8_t digest[32]; sha_final(&ctx,digest);
    int i; for(i=0;i<32;i++) printf("%02x",digest[i]);
    printf("\n");
    return 0;
}
