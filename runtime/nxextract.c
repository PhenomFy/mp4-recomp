/* Game files from the player's own .xci / .nsp: the program's ExeFS and RomFS, decrypted with the player's own keys
   (prod.keys: header_key, key_area_key_application_XX, titlekek_XX; a common ticket in the .nsp, or title.keys).
   beyond_setup.py runs it as `<runtime> --nx-extract --keys prod.keys --out DIR FILE` and gets DIR/exefs and
   DIR/romfs, which it checks against the manifest as it does an extracted folder. No keys or game data are included.

   Formats (the public Switch documentation, as hactool and LibHac read them): an .nsp is a PFS0 of NCAs and tickets,
   an .xci a game card whose root HFS0 holds the "secure" HFS0 of NCAs. An NCA's 0xC00-byte header is AES-128-XTS
   (header_key, 0x200-byte sectors, big-endian sector number as tweak); its sections are AES-128-CTR with the key from
   the key area (key_area_key) or the title key (titlekek), the counter = the section's 8-byte upper counter + the NCA
   offset / 16. The program NCA (content type 0) has the ExeFS (PFS0 section) and the RomFS (IVFC section, level 6).

   Progress on stdout as setup reads it: '@@P <percent> <stage>|<detail>', '@@E <message>' on failure. */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#define nx_mkdir(p) _mkdir(p)
#else
#define fseek64 fseeko
#define ftell64 ftello
#define nx_mkdir(p) mkdir(p,0755)
#endif
typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;

static void nx_fail(const char* fmt, ...) __attribute__((format(printf,1,2),noreturn));
#include <stdarg.h>
static void nx_fail(const char* fmt, ...){ char m[1024]; va_list a; va_start(a,fmt); vsnprintf(m,sizeof m,fmt,a); va_end(a);
  printf("@@E %s\n",m); fflush(stdout); exit(1); }
static u32 rd32(const u8* p){ return p[0]|p[1]<<8|p[2]<<16|(u32)p[3]<<24; }
static u64 rd64(const u8* p){ return rd32(p)|(u64)rd32(p+4)<<32; }

/* ---------------------------------------------------------------- AES-128 (tables for small jobs, AES-NI for CTR) */
static u8 sbox[256], isbox[256];
static void aes_tables(void){ if(sbox[0]) return; u8 p=1,q=1;
  do{ p=p^(u8)(p<<1)^(p&0x80? 0x1B : 0); q^=q<<1; q^=q<<2; q^=q<<4; if(q&0x80) q^=0x09;
    u8 x=q^(u8)(q<<1|q>>7)^(u8)(q<<2|q>>6)^(u8)(q<<3|q>>5)^(u8)(q<<4|q>>4); sbox[p]=x^0x63; }while(p!=1);
  sbox[0]=0x63; for(int i=0;i<256;i++) isbox[sbox[i]]=(u8)i; }
typedef struct { u8 rk[176]; } Aes;
static void aes_key(Aes* a, const u8 k[16]){ aes_tables(); memcpy(a->rk,k,16); u8 rc=1;
  for(int i=16;i<176;i+=4){ u8 t[4]; memcpy(t,a->rk+i-4,4);
    if(i%16==0){ u8 x=t[0]; t[0]=sbox[t[1]]^rc; t[1]=sbox[t[2]]; t[2]=sbox[t[3]]; t[3]=sbox[x]; rc=(u8)(rc<<1)^(rc&0x80? 0x1B : 0); }
    for(int j=0;j<4;j++) a->rk[i+j]=a->rk[i-16+j]^t[j]; } }
static u8 xt(u8 x){ return (u8)(x<<1)^(x&0x80? 0x1B : 0); }
static u8 gm(u8 x, u8 y){ u8 r=0; while(y){ if(y&1) r^=x; x=xt(x); y>>=1; } return r; }
static void aes_enc(const Aes* a, const u8 in[16], u8 out[16]){ u8 s[16]; for(int i=0;i<16;i++) s[i]=in[i]^a->rk[i];
  for(int r=1;r<=10;r++){ u8 t[16]; for(int c=0;c<4;c++) for(int i=0;i<4;i++) t[c*4+i]=sbox[s[((c+i)%4)*4+i]];
    if(r<10) for(int c=0;c<4;c++){ u8* x=t+c*4; u8 a0=x[0],a1=x[1],a2=x[2],a3=x[3];
      x[0]=xt(a0)^xt(a1)^a1^a2^a3; x[1]=a0^xt(a1)^xt(a2)^a2^a3; x[2]=a0^a1^xt(a2)^xt(a3)^a3; x[3]=xt(a0)^a0^a1^a2^xt(a3); }
    for(int i=0;i<16;i++) s[i]=t[i]^a->rk[r*16+i]; }
  memcpy(out,s,16); }
static void aes_dec(const Aes* a, const u8 in[16], u8 out[16]){ u8 s[16]; for(int i=0;i<16;i++) s[i]=in[i]^a->rk[160+i];
  for(int r=9;r>=0;r--){ u8 t[16]; for(int c=0;c<4;c++) for(int i=0;i<4;i++) t[((c+i)%4)*4+i]=isbox[s[c*4+i]];
    for(int i=0;i<16;i++) t[i]^=a->rk[r*16+i];
    if(r>0) for(int c=0;c<4;c++){ u8* x=t+c*4; u8 a0=x[0],a1=x[1],a2=x[2],a3=x[3];
      x[0]=gm(a0,14)^gm(a1,11)^gm(a2,13)^gm(a3,9); x[1]=gm(a0,9)^gm(a1,14)^gm(a2,11)^gm(a3,13);
      x[2]=gm(a0,13)^gm(a1,9)^gm(a2,14)^gm(a3,11); x[3]=gm(a0,11)^gm(a1,13)^gm(a2,9)^gm(a3,14); }
    memcpy(s,t,16); }
  memcpy(out,s,16); }
/* the XTS header: sector n's tweak is AES(key2, n as a big-endian 128-bit number), then doubled per block */
static void xts_dec(const u8 key[32], u8* buf, size_t n, u64 sector){ Aes k1,k2; aes_key(&k1,key); aes_key(&k2,key+16);
  for(size_t o=0;o<n;o+=0x200,sector++){ u8 tw[16]={0},t[16]; for(int i=0;i<8;i++) tw[15-i]=(u8)(sector>>(8*i)); aes_enc(&k2,tw,t);
    for(size_t b=0;b<0x200 && o+b<n;b+=16){ u8 x[16]; for(int i=0;i<16;i++) x[i]=buf[o+b+i]^t[i]; aes_dec(&k1,x,x);
      for(int i=0;i<16;i++) buf[o+b+i]=x[i]^t[i];
      int c=t[15]>>7; for(int i=15;i>0;i--) t[i]=(u8)(t[i]<<1|t[i-1]>>7); t[0]=(u8)(t[0]<<1)^(c? 0x87 : 0); } } }
#if defined(__x86_64__) || defined(__i386__)
#include <wmmintrin.h>
#include <emmintrin.h>
__attribute__((target("aes,sse2"))) static void ctr_ni(const Aes* a, u64 hi, u64 blk, u8* buf, size_t nblk){
  __m128i k[11]; for(int i=0;i<11;i++) k[i]=_mm_loadu_si128((const __m128i*)(a->rk+i*16));
  for(size_t b=0;b<nblk;b++,blk++){ u8 c[16]; for(int i=0;i<8;i++){ c[i]=(u8)(hi>>(56-8*i)); c[8+i]=(u8)(blk>>(56-8*i)); }
    __m128i x=_mm_xor_si128(_mm_loadu_si128((const __m128i*)c),k[0]); for(int r=1;r<10;r++) x=_mm_aesenc_si128(x,k[r]); x=_mm_aesenclast_si128(x,k[10]);
    _mm_storeu_si128((__m128i*)(buf+b*16),_mm_xor_si128(_mm_loadu_si128((const __m128i*)(buf+b*16)),x)); } }
#endif
/* CTR: the counter is hi (the section's upper 8 bytes) followed by the block number, both big-endian */
static void ctr_xor(const Aes* a, u64 hi, u64 blk, u8* buf, size_t nblk){
#if defined(__x86_64__) || defined(__i386__)
  static int ni=-1; if(ni<0) ni=__builtin_cpu_supports("aes"); if(ni){ ctr_ni(a,hi,blk,buf,nblk); return; }
#endif
  for(size_t b=0;b<nblk;b++,blk++){ u8 c[16],ks[16]; for(int i=0;i<8;i++){ c[i]=(u8)(hi>>(56-8*i)); c[8+i]=(u8)(blk>>(56-8*i)); }
    aes_enc(a,c,ks); for(int i=0;i<16;i++) buf[b*16+i]^=ks[i]; } }

/* ---------------------------------------------------------------- keys */
typedef struct { char name[48]; u8 v[32]; int n; } Key;
static Key g_keys[512]; static int g_nkeys;
static void keys_load(const char* path, int must){ FILE* f=fopen(path,"r"); if(!f){ if(must) nx_fail("Cannot open the keys file %s.",path); return; }
  char l[512]; while(fgets(l,sizeof l,f) && g_nkeys<512){ char* e=strchr(l,'='); if(!e) continue; *e=0; char* k=l; while(isspace((u8)*k)) k++;
    char* ke=e; while(ke>k && isspace((u8)ke[-1])) ke--; *ke=0; char* v=e+1; while(isspace((u8)*v)) v++;
    Key* K=&g_keys[g_nkeys]; snprintf(K->name,sizeof K->name,"%s",k); for(char* c=K->name;*c;c++) *c=(char)tolower((u8)*c); K->n=0;
    while(K->n<32 && isxdigit((u8)v[0]) && isxdigit((u8)v[1])){ unsigned x; sscanf(v,"%2x",&x); K->v[K->n++]=(u8)x; v+=2; }
    if(K->n==16 || K->n==32) g_nkeys++; }
  fclose(f); }
static const u8* key_get(const char* name, int len){ for(int i=0;i<g_nkeys;i++) if(g_keys[i].n==len && !strcmp(g_keys[i].name,name)) return g_keys[i].v; return NULL; }
static const u8* key_need(const char* name, int len){ const u8* k=key_get(name,len);
  if(!k) nx_fail("The keys file has no %s. Dump the keys from your own Switch again (Lockpick_RCM): a newer game needs newer keys.",name); return k; }

/* ---------------------------------------------------------------- file regions, PFS0 / HFS0 */
typedef struct { FILE* f; u64 off, size; } Reg;
static void reg_read(const Reg* r, u64 off, void* dst, size_t n){
  if(off+n>r->size) nx_fail("The file is cut short (needed %llu bytes at %llu of %llu): the dump may be incomplete.",(unsigned long long)n,(unsigned long long)off,(unsigned long long)r->size);
  if(fseek64(r->f,(long long)(r->off+off),SEEK_SET) || fread(dst,1,n,r->f)!=n) nx_fail("Reading the game file failed (%s).",strerror(errno)); }
typedef struct { char name[256]; Reg r; } Entry;
/* PFS0 (an .nsp, an ExeFS) and HFS0 (the game card's partitions): a header, entries, names, then the data */
static int fs0_list(const Reg* r, Entry* out, int max){ u8 h[16]; reg_read(r,0,h,16); int hfs= !memcmp(h,"HFS0",4);
  if(!hfs && memcmp(h,"PFS0",4)) return -1;
  u32 n=rd32(h+4), strsz=rd32(h+8), esz= hfs? 0x40 : 0x18; if(n>4096) return -1;
  u8* e=malloc((size_t)n*esz+strsz+1); reg_read(r,16,e,(size_t)n*esz+strsz); e[(size_t)n*esz+strsz]=0;
  u64 data=16+(u64)n*esz+strsz; int k=0;
  for(u32 i=0;i<n && k<max;i++){ const u8* x=e+i*esz; u32 so=rd32(x+16); if(so>=strsz) continue;
    snprintf(out[k].name,sizeof out[k].name,"%s",(char*)e+(size_t)n*esz+so); out[k].r=(Reg){r->f,r->off+data+rd64(x),rd64(x+8)}; k++; }
  free(e); return k; }

/* ---------------------------------------------------------------- NCA */
typedef struct { Reg r; u8 hdr[0xC00]; int type; u64 program; u8 key[16]; int haskey, gen; const char* keysrc; char name[64]; } Nca;
struct Bktr;
typedef struct { const Nca* n; u64 start, size; int enc; u64 hi; u8 fsh[0x200]; struct Bktr* bk; } Sec;
static u8 g_tiks[64][32]; static int g_ntik;   /* rights ID + encrypted title key, from the .nsp's tickets */
static void tik_add(const Reg* r){ u8 sig[4]; reg_read(r,0,sig,4); u32 t=rd32(sig); u64 skip;
  switch(t){ case 0x10000: case 0x10003: skip=0x240; break; case 0x10001: case 0x10004: skip=0x140; break; case 0x10002: case 0x10005: skip=0x80; break; default: return; }
  u8 d[0x180]; if(skip+sizeof d>r->size) return; reg_read(r,skip,d,sizeof d);
  if(d[0x141]!=0) nx_fail("The .nsp has a personalized ticket (tied to the console it was bought on), which needs that console's keys. Dump it again with a common ticket (DBI: the default).");
  if(g_ntik<64){ memcpy(g_tiks[g_ntik],d+0x160,16); memcpy(g_tiks[g_ntik]+16,d+0x40,16); g_ntik++; } }
static int nca_open(Nca* c, const Reg* r){ memset(c,0,sizeof *c); c->r=*r; if(r->size<0xC00) return 0;
  reg_read(r,0,c->hdr,0xC00); xts_dec(key_need("header_key",32),c->hdr,0xC00,0);
  if(memcmp(c->hdr+0x200,"NCA3",4)) return 0;
  c->type=c->hdr[0x205]; c->program=rd64(c->hdr+0x210);
  int gen= c->hdr[0x220]>c->hdr[0x206]? c->hdr[0x220] : c->hdr[0x206]; int idx= gen? gen-1 : 0; char nm[48]; c->gen=gen;
  static const u8 zero[16]; const u8* rights=c->hdr+0x230;
  if(memcmp(rights,zero,16)){ const u8* enc=NULL; for(int i=0;i<g_ntik;i++) if(!memcmp(g_tiks[i],rights,16)) enc=g_tiks[i]+16;
    char rid[40]; for(int i=0;i<16;i++) sprintf(rid+2*i,"%02x",rights[i]);
    if(!enc){ const u8* tk=key_get(rid,16); if(!tk) nx_fail("No title key for this content (rights ID %s): the .nsp has no ticket and title.keys does not list it.",rid);
      memcpy(c->key,tk,16); c->haskey=1; c->keysrc="title.keys"; return 1; }   /* title.keys (Lockpick_RCM) holds them decrypted */
    snprintf(nm,sizeof nm,"titlekek_%02x",idx); Aes k; aes_key(&k,key_need(nm,16)); aes_dec(&k,enc,c->key); c->haskey=1; c->keysrc="its ticket"; }
  else { snprintf(nm,sizeof nm,"key_area_key_application_%02x",idx); Aes k; aes_key(&k,key_need(nm,16)); aes_dec(&k,c->hdr+0x300+0x20,c->key); c->haskey=1; c->keysrc="the key area"; }
  return 1; }
static int nca_section(const Nca* c, int i, Sec* s){ const u8* e=c->hdr+0x240+i*16; u32 a=rd32(e), b=rd32(e+4); if(!b || b<=a) return 0;
  memset(s,0,sizeof *s); s->n=c; s->start=(u64)a*0x200; s->size=(u64)(b-a)*0x200; memcpy(s->fsh,c->hdr+0x400+i*0x200,0x200);
  s->enc=s->fsh[4]; for(int k=0;k<8;k++) s->hi=s->hi<<8|s->fsh[0x147-k]; return 1; }
/* section data, decrypted with upper counter hi: aligned to 16 for the counter */
static void sec_read_hi(const Sec* s, u64 off, u8* dst, size_t n, u64 hi){
  if(s->enc==1){ reg_read(&s->n->r,s->start+off,dst,n); return; }
  if(s->enc!=3 && s->enc!=4) nx_fail("This NCA section uses encryption type %d, which is not supported.",s->enc);
  Aes k; aes_key(&k,s->n->key); static u8* tmp; static size_t tcap;
  u64 a=off&~15ull, e=(off+n+15)&~15ull; size_t len=(size_t)(e-a); if(len>tcap){ free(tmp); tcap=len; tmp=malloc(tcap); }
  u64 avail= s->start+e<=s->n->r.size? len : (size_t)(s->n->r.size-s->start-a); memset(tmp,0,len); reg_read(&s->n->r,s->start+a,tmp,(size_t)avail);
  ctr_xor(&k,hi,(s->start+a)>>4,tmp,len/16); memcpy(dst,tmp+(off-a),n); }
static void bk_read(struct Bktr* b, u64 off, u8* dst, size_t n);
static void sec_read(const Sec* s, u64 off, u8* dst, size_t n){ if(s->bk) bk_read(s->bk,off,dst,n); else sec_read_hi(s,off,dst,n,s->hi); }
static int nca_romfs(const Nca* c, Sec* s){ for(int j=0;j<4;j++) if(nca_section(c,j,s) && s->fsh[3]==3) return 1; return 0; }
/* layers this reader doesn't apply: a sparse section (fs header 0x148, its generation at 0x170: an eShop download made while
   an update was out holds only what the update doesn't replace) and a compressed one (0x178). Read as plain data they
   come out wrong without any error, so they stop setup with the reason */
static void sec_check_layers(const Sec* s, const char* what){
  if(s->fsh[0x170] | s->fsh[0x171]) nx_fail("%s is stored sparse (an eShop download made while an update was already out), which setup cannot read yet. Dump the game from a game card, or report it with setup.log.",what);
  if(rd64(s->fsh+0x180)) nx_fail("%s is stored compressed, which setup cannot read yet. Report it with setup.log.",what); }
/* why a section may not decrypt: for a ticket's title key, the keys file's titlekek_XX can be checked against its own
   master_key_XX (titlekek = AES-128-ECB-decrypt(master_key, titlekek_source)); a missing master key means a keys file
   from older firmware than the game */
static const char* key_hint(const Nca* c){ static char m[400]; int idx= c->gen? c->gen-1 : 0; char a[48], b[48];
  if(!c->keysrc || strcmp(c->keysrc,"its ticket")) return "the keys file's key_area_key for it may be wrong, or the file is damaged";
  snprintf(a,sizeof a,"titlekek_%02x",idx); snprintf(b,sizeof b,"master_key_%02x",idx);
  const u8 *tk=key_get(a,16), *mk=key_get(b,16), *src=key_get("titlekek_source",16);
  if(!mk) { snprintf(m,sizeof m,"the keys file has no %s: it was dumped on older firmware than this game needs. Dump the keys again from your Switch (Lockpick_RCM) on its current firmware",b); return m; }
  if(src && tk){ Aes k; u8 d[16]; aes_key(&k,mk); aes_dec(&k,src,d); if(memcmp(d,tk,16)){ snprintf(m,sizeof m,"the keys file's %s does not match its %s: the file is mixed up or damaged. Dump the keys again from your Switch (Lockpick_RCM)",a,b); return m; } }
  return "the keys file's titlekek for it may be wrong, or the file is damaged"; }
/* the RomFS header (IVFC level 6) must read back: its size 0x50 and every table inside the level. A wrong key or the
   wrong NCA fails here instead of producing 26 GB of wrong files */
static int romfs_header_ok(const Sec* s){ const u8* iv=s->fsh+8; if(memcmp(iv,"IVFC",4)) return 0;
  const u8* l6=iv+0x10+5*0x18; u64 base=rd64(l6), size=rd64(l6+8); u8 h[0x50]; sec_read(s,base,h,0x50);
  if(rd64(h)!=0x50) return 0; for(int i=1;i<10;i++) if(rd64(h+8*i)>size) return 0; return 1; }
/* the program NCA to use: an update's (its RomFS a patch) or the game's (never a patch), the exact program first.
   An .nsp lists its NCAs by content ID, so the first one with the game's ID can be another program or an update
   bundled into the file */
static const Nca* pick_program(const Nca* n, int cnt, u64 want, int patch_wanted, const char* file){ const Nca* best=NULL; int bs=-1;
  for(int i=0;i<cnt;i++){ if(n[i].type!=0 || (want && (n[i].program&~0xFFFull)!=(want&~0xFFFull))) continue;
    Sec s; int has=nca_romfs(&n[i],&s), patch= has && s.enc==4;
    printf("  %s: program %016llX, key generation %d, key from %s, %s\n",n[i].name,(unsigned long long)n[i].program,n[i].gen,n[i].keysrc? n[i].keysrc : "?",
           has? (patch? "an update's RomFS" : "RomFS") : "no RomFS");
    if(!patch_wanted && patch) continue;
    int sc= (patch==patch_wanted)*4 + (want && n[i].program==want)*2 + has; if(sc>bs){ bs=sc; best=&n[i]; } }
  (void)file; return best; }

/* ---------------------------------------------------------------- an update's RomFS (BKTR)
   The update's RomFS section is a patch over the base game's: an indirect table maps every range of the patched
   section to the base game's RomFS section (storage 0) or to the update's own data (storage 1), and an AES-CTR-Ex
   table gives each range of the update's data the 4-byte generation that replaces the low half of its upper counter.
   Both tables are bucket trees (0x4000-byte nodes: a header, then entries) at the end of the update's section,
   encrypted with its plain counter. Indirect entries are 0x14 bytes (patched offset, source offset, storage), CTR-Ex
   ones 0x10 (offset, reserved, generation) */
typedef struct { u64 v, p; u32 patch; } IEnt;
typedef struct { u64 off; u32 gen; } XEnt;
struct Bktr { const Sec* base; Sec patch; IEnt* ie; u32 ni; u64 vend; XEnt* xe; u32 nx; u64 xend; };
static u8* bk_table(const Sec* p, int at, u32* count, u64* end){ const u8* f=p->fsh+at; u64 off=rd64(f), size=rd64(f+8);
  if(memcmp(f+16,"BKTR",4) || size<0x4000) nx_fail("The update's RomFS patch tables are not readable (an unsupported update format?).");
  u8* t=malloc((size_t)size); sec_read_hi(p,off,t,(size_t)size,p->hi); *count=rd32(t+4); *end=rd64(t+8); return t; }
static void bk_open(struct Bktr* b, const Sec* base, const Sec* patch){ memset(b,0,sizeof *b); b->base=base; b->patch=*patch; b->patch.bk=NULL;
  u32 nb; u64 tsz; u8* t=bk_table(patch,0x100,&nb,&b->vend); (void)tsz;
  b->ie=malloc(sizeof(IEnt)*0x400*(size_t)(nb+1));
  for(u32 i=0;i<nb;i++){ const u8* k=t+0x4000*(size_t)(i+1); u32 n=rd32(k+4); for(u32 j=0;j<n;j++){ const u8* e=k+0x10+j*0x14; b->ie[b->ni++]=(IEnt){rd64(e),rd64(e+8),rd32(e+16)}; } }
  free(t); t=bk_table(patch,0x120,&nb,&b->xend); b->xe=malloc(sizeof(XEnt)*0x400*(size_t)(nb+1));
  for(u32 i=0;i<nb;i++){ const u8* k=t+0x4000*(size_t)(i+1); u32 n=rd32(k+4); for(u32 j=0;j<n;j++){ const u8* e=k+0x10+j*0x10; b->xe[b->nx++]=(XEnt){rd64(e),rd32(e+12)}; } }
  free(t);
  printf("update RomFS: %u ranges (%.1f GB), %u counter ranges\n",b->ni,b->vend/1e9,b->nx); }
static u32 bk_find_i(const struct Bktr* b, u64 v){ u32 lo=0, hi=b->ni; while(hi-lo>1){ u32 m=(lo+hi)/2; if(b->ie[m].v<=v) lo=m; else hi=m; } return lo; }
static u32 bk_find_x(const struct Bktr* b, u64 p){ u32 lo=0, hi=b->nx; while(hi-lo>1){ u32 m=(lo+hi)/2; if(b->xe[m].off<=p) lo=m; else hi=m; } return lo; }
static void bk_read(struct Bktr* b, u64 off, u8* dst, size_t n){
  while(n){ if(!b->ni || off>=b->vend) nx_fail("A read past the end of the update's RomFS (%llu).",(unsigned long long)off);
    u32 i=bk_find_i(b,off); u64 end= i+1<b->ni? b->ie[i+1].v : b->vend; size_t m= end-off<n? (size_t)(end-off) : n; u64 p=b->ie[i].p+(off-b->ie[i].v);
    if(!b->ie[i].patch) sec_read(b->base,p,dst,m);
    else for(size_t d=0;d<m;){ u32 x=bk_find_x(b,p+d); u64 xe= x+1<b->nx? b->xe[x+1].off : b->xend; size_t k= xe-(p+d)<m-d? (size_t)(xe-(p+d)) : m-d;
      sec_read_hi(&b->patch,p+d,dst+d,k,(b->patch.hi&0xFFFFFFFF00000000ull)|b->xe[x].gen); d+=k; }
    off+=m; dst+=m; n-=m; } }

/* ---------------------------------------------------------------- output */
static void mkdirs(const char* path){ char p[4096]; snprintf(p,sizeof p,"%s",path);
  for(char* c=p+1;*c;c++) if(*c=='/' || *c=='\\'){ char s=*c; *c=0; nx_mkdir(p); *c=s; } nx_mkdir(p); }
static u64 g_done, g_total; static int g_lastpct=-1; static const char* g_stage="Reading";
static void progress(u64 add){ g_done+=add; int p= g_total? (int)(g_done*100/g_total) : 0; if(p!=g_lastpct){ g_lastpct=p; printf("@@P %d %s|%.1f of %.1f GB\n",p,g_stage,g_done/1e9,g_total/1e9); fflush(stdout); } }
static void write_out(const char* path, const Sec* s, u64 off, u64 size){ FILE* o=fopen(path,"wb"); if(!o) nx_fail("Cannot write %s (%s).",path,strerror(errno));
  static u8* buf; if(!buf) buf=malloc(8u<<20);
  for(u64 p=0;p<size;){ size_t n= size-p>(8u<<20)? (8u<<20) : (size_t)(size-p); sec_read(s,off+p,buf,n);
    if(fwrite(buf,1,n,o)!=n) nx_fail("Writing %s failed (%s): is the drive full?",path,strerror(errno)); p+=n; progress(n); }
  fclose(o); }

/* ExeFS: a PFS0 behind a SHA-256 hash table; the hash info's region 0 is the table, region 1 the PFS0 */
static int exefs_extract(const Sec* s, const char* dir){ const u8* hi=s->fsh+8; u64 po=rd64(hi+0x38), ps=rd64(hi+0x40);
  u8* img=malloc((size_t)ps); sec_read(s,po,img,(size_t)ps); if(memcmp(img,"PFS0",4)){ free(img); return 0; }
  u32 n=rd32(img+4), strsz=rd32(img+8); u64 data=16+(u64)n*0x18+strsz; mkdirs(dir);
  for(u32 i=0;i<n;i++){ const u8* e=img+16+i*0x18; const char* nm=(const char*)img+16+n*0x18+rd32(e+16); char p[4096];
    snprintf(p,sizeof p,"%s/%s",dir,nm); FILE* o=fopen(p,"wb"); if(!o) nx_fail("Cannot write %s.",p);
    fwrite(img+data+rd64(e),1,(size_t)rd64(e+8),o); fclose(o); }
  free(img); return (int)n; }
/* RomFS: the IVFC tree's level 6 holds it: a header, directory and file tables, then the file data */
typedef struct { const Sec* s; u64 base; u8* dirs; u8* files; u64 dlen, flen, data; u32 nfiles; } Rom;
static void rom_walk(Rom* r, u32 d, const char* path, int count){
  for(u32 f=rd32(r->dirs+d+12); f!=0xFFFFFFFF && f+0x20<=r->flen; f=rd32(r->files+f+4)){ const u8* e=r->files+f; u32 nl=rd32(e+0x1C);
    if(count){ g_total+=rd64(e+0x10); r->nfiles++; continue; }
    char p[4096]; snprintf(p,sizeof p,"%s/%.*s",path,(int)nl,(const char*)e+0x20); write_out(p,r->s,r->base+r->data+rd64(e+8),rd64(e+0x10)); }
  for(u32 c=rd32(r->dirs+d+8); c!=0xFFFFFFFF && c+0x18<=r->dlen; c=rd32(r->dirs+c+4)){ const u8* e=r->dirs+c; u32 nl=rd32(e+0x14);
    char p[4096]; snprintf(p,sizeof p,"%s/%.*s",path,(int)nl,(const char*)e+0x18); if(!count) mkdirs(p); rom_walk(r,c,p,count); } }
static int romfs_extract(const Sec* s, const char* dir){ const u8* iv=s->fsh+8; if(memcmp(iv,"IVFC",4)) return 0;
  const u8* l6=iv+0x10+5*0x18; Rom r={s,rd64(l6)}; u8 h[0x50]; sec_read(s,r.base,h,0x50);
  r.dlen=rd64(h+0x20); r.flen=rd64(h+0x40); r.data=rd64(h+0x48); r.dirs=malloc((size_t)r.dlen); r.files=malloc((size_t)r.flen);
  sec_read(s,r.base+rd64(h+0x18),r.dirs,(size_t)r.dlen); sec_read(s,r.base+rd64(h+0x38),r.files,(size_t)r.flen);
  mkdirs(dir); rom_walk(&r,0,dir,1); printf("RomFS: %u files, %.1f GB\n",r.nfiles,g_total/1e9); g_stage="Extracting the RomFS"; rom_walk(&r,0,dir,0);
  free(r.dirs); free(r.files); return 1; }

/* ---------------------------------------------------------------- the program NCA of an .xci / .nsp */
static int find_ncas(const Reg* r, Nca* out, int max){ Entry e[256]; int n=fs0_list(r,e,256), k=0;
  if(n<0){ u8 h[4]; if(r->size>0x204) reg_read(r,0x100,h,4); if(r->size<=0x204 || memcmp(h,"HEAD",4)) nx_fail("This is neither an .xci nor an .nsp.");
    u8 x[16]; reg_read(r,0x130,x,16); Reg root={r->f,r->off+rd64(x),r->size-rd64(x)}; Entry p[8]; int np=fs0_list(&root,p,8);
    for(int i=0;i<np;i++) if(!strcmp(p[i].name,"secure")) return find_ncas(&p[i].r,out,max);
    nx_fail("The .xci has no secure partition."); }
  for(int i=0;i<n;i++){ size_t l=strlen(e[i].name); if(l>4 && !strcmp(e[i].name+l-4,".tik")) tik_add(&e[i].r); }
  for(int i=0;i<n && k<max;i++){ size_t l=strlen(e[i].name); if(l>4 && !strcmp(e[i].name+l-4,".nca") && nca_open(&out[k],&e[i].r)){ snprintf(out[k].name,sizeof out[k].name,"%s",e[i].name); k++; } }
  return k; }

int nx_extract_main(int argc, char** argv){
  const char *keys=NULL, *tkeys=NULL, *out=NULL, *file=NULL, *basef=NULL; u64 want=0;
  for(int i=1;i<argc;i++){ if(!strcmp(argv[i],"--keys") && i+1<argc) keys=argv[++i]; else if(!strcmp(argv[i],"--title-keys") && i+1<argc) tkeys=argv[++i];
    else if(!strcmp(argv[i],"--out") && i+1<argc) out=argv[++i]; else if(!strcmp(argv[i],"--base") && i+1<argc) basef=argv[++i]; else if(!strcmp(argv[i],"--program") && i+1<argc) want=strtoull(argv[++i],NULL,16);
    else if(argv[i][0]!='-') file=argv[i]; }
  if(!keys || !out || !file){ printf("usage: --nx-extract --keys prod.keys [--title-keys title.keys] [--program ID] [--base GAME.xci|GAME.nsp] --out DIR GAME.xci|GAME.nsp|UPDATE.nsp\n"); return 2; }
  keys_load(keys,1); if(tkeys) keys_load(tkeys,0);
  FILE* f=fopen(file,"rb"); if(!f) nx_fail("Cannot open %s (%s).",file,strerror(errno));
  fseek64(f,0,SEEK_END); Reg whole={f,0,(u64)ftell64(f)};
  static Nca ncas[64]; int n=find_ncas(&whole,ncas,64);
  printf("%s:\n",file); const Nca* prog=pick_program(ncas,n,want,basef!=NULL,file);   /* an update's program ID can differ in its low bits */
  if(!prog) nx_fail("%s has no program NCA%s (found %d NCAs). Is it the game or its update?",file,want? " for this game" : "",n);
  printf("program %016llX, key generation %d\n",(unsigned long long)prog->program,prog->hdr[0x220]>prog->hdr[0x206]? prog->hdr[0x220] : prog->hdr[0x206]);
  char ex[4096], ro[4096]; snprintf(ex,sizeof ex,"%s/exefs",out); snprintf(ro,sizeof ro,"%s/romfs",out);
  int got_ex=0, got_ro=0;
  for(int i=0;i<4;i++){ Sec s; if(!nca_section(prog,i,&s)) continue;
    if(s.fsh[3]==2 && !got_ex) got_ex=exefs_extract(&s,ex)>0;
    else if(s.fsh[3]==3 && !got_ro){
      if(s.enc==4){ /* an update: its RomFS is a patch over the base game's, which --base gives */
        if(!basef) nx_fail("This is an update: its RomFS is a patch over the game's, so the game file is needed too (choose both).");
        FILE* bf=fopen(basef,"rb"); if(!bf) nx_fail("Cannot open %s (%s).",basef,strerror(errno)); fseek64(bf,0,SEEK_END); Reg bw={bf,0,(u64)ftell64(bf)};
        static Nca bn[64]; int nb=find_ncas(&bw,bn,64); printf("%s:\n",basef); const Nca* bp=pick_program(bn,nb,prog->program,0,basef);
        if(!bp) nx_fail("%s is not the game this update is for (no program %016llX in it, or only an update).",basef,(unsigned long long)(prog->program&~0xFFFull));
        Sec bs; if(!nca_romfs(bp,&bs)) nx_fail("The game file has no RomFS.");
        sec_check_layers(&bs,"The game file's RomFS");
        if(!romfs_header_ok(&bs)) nx_fail("The game file's RomFS does not decrypt (%s, key from %s, key generation %d): %s.",bp->name,bp->keysrc? bp->keysrc : "?",bp->gen,key_hint(bp));
        printf("game: %s (program %016llX, key from %s)\n",bp->name,(unsigned long long)bp->program,bp->keysrc? bp->keysrc : "?");
        sec_check_layers(&s,"The update's RomFS");
        static struct Bktr bk; bk_open(&bk,&bs,&s); s.bk=&bk;
        if(!romfs_header_ok(&s)) nx_fail("The game with the update applied does not read as a RomFS: the game file may not be the version this update is for.");
        got_ro=romfs_extract(&s,ro); fclose(bf); }
      else { sec_check_layers(&s,"The RomFS");
        if(!romfs_header_ok(&s)) nx_fail("The RomFS does not decrypt (%s, key from %s, key generation %d): %s.",prog->name,prog->keysrc? prog->keysrc : "?",prog->gen,key_hint(prog));
        got_ro=romfs_extract(&s,ro); } } }
  fclose(f);
  if(!got_ex) nx_fail("The program NCA has no ExeFS.");
  printf("@@P 100 Extracted|%s\n",got_ro? "ExeFS and RomFS" : "ExeFS only"); fflush(stdout);
  return 0; }
