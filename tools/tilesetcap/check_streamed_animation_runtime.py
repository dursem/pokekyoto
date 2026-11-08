#!/usr/bin/env python3
"""Compile the real animation/cache C code against a simulated GBA VRAM buffer.

Requires a host GCC with AddressSanitizer/UBSan and tools/gbagfx/gbagfx.
Hardware DMA, registers and frame asset includes are the only substitutions.
This is a host regression test, not an emulator or gameplay recording.
"""
from pathlib import Path
import re, subprocess, struct, tempfile, os
root=Path(__file__).resolve().parents[2]
temporary = tempfile.TemporaryDirectory(prefix='lavender-runtime-')
out=Path(temporary.name)
def strip(s): return re.sub(r'^#include[^\n]*\n','',s,flags=re.M)
anims=strip((root/'src/tileset_anims.c').read_text())
def data(m):
 p=root/m[1]; dst=out/(p.stem+'.4bpp')
 subprocess.run([str(root/'tools/gbagfx/gbagfx'),str(p),str(dst)],check=True)
 b=dst.read_bytes(); assert len(b) in (128,256)
 return '{'+','.join(hex(v) for v in struct.unpack('<%dI' % (len(b)//4),b))+'}'
anims=re.sub(r'INCGFX_U32\("([^"]+)", "\.4bpp"\)',data,anims)
stub=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
typedef u8 bool8; typedef u32 bool32;
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define UNUSED __attribute__((unused))
#define ARRAY_COUNT(x) (sizeof(x)/sizeof((x)[0]))
#define TILE_SIZE_4BPP 32
#define TILE_OFFSET_4BPP(n) ((n)*32)
#define NUM_TILES_IN_PRIMARY 512
#define NUM_TILES_TOTAL 1024
#define NUM_TILES_PER_METATILE 12
#define BG_SCREEN_SIZE 2048
static u8 vram[32768];
#define BG_VRAM ((uintptr_t)vram)
#define CpuFill32(v,d,n) memset(d,v,n)
#define CpuFill16(v,d,n) memset(d,v,n)
#define CpuFastFill(v,d,n) memset(d,v,n)
#define CpuFastCopy(s,d,n) memcpy(d,s,n)
#define DmaCopy16(c,s,d,n) memcpy(d,s,n)
#define DebugPrintf(...) ((void)0)
#define assertf(c,...) if (!(c))
struct Tileset { void (*callback)(void); };
struct MapLayout { const struct Tileset *primaryTileset, *secondaryTileset; };
static struct { const struct MapLayout *mapLayout; } gMapHeader;
static struct { u32 vblankCounter1; } gMain;
static u16 map1[1024],map2[1024],map3[1024];
static u16 *gOverworldTilemapBuffer_Bg1=map1,*gOverworldTilemapBuffer_Bg2=map2,*gOverworldTilemapBuffer_Bg3=map3;
static u32 GetDma3RequestsQueued(void){return 0;}
static u32 GetDma3RequestsDone(void){return 0;}
static u32 MapPalettesTilemapsPrepared(void){return 0;}
static u32 MapPalettesTilemapsCommitted(void){return 0;}
'''
headers=strip((root/'include/tile_cache.h').read_text())+strip((root/'include/tileset_anims.h').read_text())
metadata=r'''
const struct Tileset gTileset_General = {InitTilesetAnim_General};
const struct Tileset gTileset_Petalburg = {InitTilesetAnim_Petalburg};
const struct Tileset gTileset_Other = {InitTilesetAnim_Rustboro};
static const u32 rawTiles[2048*8] = {0};
const struct TilesetCapacityInfo gTilesetCapacityInfo[] = {
 {&gTileset_General, rawTiles, NULL, NULL, 2048, 1, TRUE},
 {&gTileset_Petalburg, rawTiles, NULL, NULL, 2048, 1, TRUE},
 {&gTileset_Other, rawTiles, NULL, NULL, 2048, 1, TRUE},
};
const u32 gTilesetCapacityInfoCount=ARRAY_COUNT(gTilesetCapacityInfo);
'''
main=r'''
static void write_flower(u16 *cells){
 const u16 virtuals[]={2304,2305,2320,2321};
 for(u32 q=0;q<4;q++){
  // Palette 12 plus both hardware flip bits must survive the cache lookup.
  TileCache_WriteCell(cells+q,0xCC00|(virtuals[q]&1023),(virtuals[q]>>10)<<5);
  assert((cells[q]&0xFC00)==0xCC00);
  AcquireTile(600+q); // Put unrelated graphics between the animated quadrants.
 }
}
static void check_flower(const u16 *cells,u32 frame){
 for(u32 q=0;q<4;q++)
  assert(memcmp(vram+(cells[q]&1023)*32,sPetalburgLavenderFrames[frame]+q*8,32)==0);
}

static void write_large(u16 *cells){
 for(u32 q=0;q<8;q++){
  u32 v=1024+(124+q/2)*16+12+q%2;
  TileCache_WriteCell(cells+q,0xC000|(v&1023),(v>>10)<<5);
  assert((cells[q]&0xF000)==0xC000);
 }
}
static void check_large(const u16 *cells,u32 frame){
 for(u32 q=0;q<8;q++)
  assert(!memcmp(vram+(cells[q]&1023)*32,sPetalburgLavenderLargeFrames[frame]+q*8,32));
}
int main(void){
 const struct MapLayout lavender={&gTileset_General,&gTileset_Petalburg};
 const struct MapLayout other={&gTileset_General,&gTileset_Other};
 gMapHeader.mapLayout=&lavender;
 TileCache_LoadLayout(&lavender);InitTilesetAnimations();
 write_flower(map1);check_flower(map1,0);write_large(map1+16);check_large(map1+16,0);
 u8 before[32768];memcpy(before,vram,sizeof(vram));
 for(u32 tick=1;tick<=192;tick++){
  UpdateTilesetAnimations();TransferTilesetAnimsBuffer();
  check_flower(map1,(tick%48)/12);check_large(map1+16,(tick%96)/24);
 }
 // Every byte outside the twelve resident flower tiles is untouched.
 for(u32 slot=0;slot<1024;slot++){
  int flower=0;for(u32 q=0;q<4;q++)flower|=slot==(map1[q]&1023);
  for(u32 q=16;q<24;q++)flower|=slot==(map1[q]&1023);
  if(!flower)assert(!memcmp(before+slot*32,vram+slot*32,32));
 }
 for(u32 i=0;i<SLOT_BITMAP_WORDS;i++)assert(sTileCache.pinned[i]==0);
 for(u32 tick=0;tick<25;tick++)UpdateTilesetAnimations();
 TransferTilesetAnimsBuffer();check_flower(map1,2);
 // Release and reuse a flower slot for unrelated content, then stream it back in.
 u32 oldSlot=map1[0]&1023;
 TileCache_WriteCell(map1,0,0);
 gMain.vblankCounter1++;TickIfNewFrame();
 gMain.vblankCounter1++;TickIfNewFrame();
 sTileCache.cursor=oldSlot-TILE_CACHE_FIRST_SLOT;
 assert(AcquireTile(1536)==oldSlot);
 memcpy(before,vram,sizeof(vram));
 TransferTilesetAnimsBuffer();
 assert(!memcmp(before+oldSlot*32,vram+oldSlot*32,32));
 TileCache_WriteCell(map1,0xC100,0x40);
 assert((map1[0]&1023)!=oldSlot);check_flower(map1,2);
 // Leaving Petalburg, or leaving the field renderer, must never animate its slots.
 TileCache_SwitchLayout(&other);gMapHeader.mapLayout=&other;InitSecondaryTilesetAnimation();
 memcpy(before,vram,sizeof(vram));TransferTilesetAnimsBuffer();
 assert(!memcmp(before,vram,sizeof(vram)));
 TileCache_Disable();TransferTilesetAnimsBuffer();
 assert(!memcmp(before,vram,sizeof(vram)));
 // Menu/warp reload: frame 0 is correct immediately, then the full cycle resumes.
 gMapHeader.mapLayout=&lavender;TileCache_LoadLayout(&lavender);InitTilesetAnimations();
 write_flower(map1);check_flower(map1,0);write_large(map1+16);check_large(map1+16,0);
 for(u32 tick=1;tick<=48;tick++){
  UpdateTilesetAnimations();TransferTilesetAnimsBuffer();check_flower(map1,(tick%48)/12);check_large(map1+16,(tick%96)/24);
 }
 assert(TileCache_GetOverflowCount()==0);
 puts("PASS: legacy and all 8 tall tiles, four frames, palette/flips, unrelated tiles, eviction/reload, map transition, disable/re-enable, no pinned slots or overflow");
 return 0;
}
'''
source=stub+headers+metadata+anims+strip((root/'src/tile_cache.c').read_text())+main
(out/'test.c').write_text(source)
subprocess.run(['gcc','-std=gnu17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(out/'test.c'),'-o',str(out/'test')],check=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
subprocess.run([str(out/'test')], check=True, env=env)
temporary.cleanup()
