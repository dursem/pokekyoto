#!/usr/bin/env python3
"""Compile/run isolated V2 source logic; NOT a complete PokéKyoto build."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

PKG = Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('kyoto_install',PKG/'install.py')
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
PREAMBLE=r'''
#ifdef HOST_TEST
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t AttrValue;
#else
typedef unsigned long AttrValue;
#endif
typedef unsigned char u8;
typedef unsigned int u32;
typedef signed short s16;
enum { BATTLE_COORDS_SINGLES=0, BATTLE_COORDS_DOUBLES=1 };
enum { B_POSITION_PLAYER_LEFT=0, B_POSITION_OPPONENT_LEFT=1 };
enum { BATTLE_ENVIRONMENT_GRASS=0, BATTLE_ENVIRONMENT_PLAIN=9 };
enum { B_WIN_ACTION_NAME=27, B_WIN_TYPE_NORMAL=0, WINDOW_TILE_DATA=7, WINDOW_WIDTH=3 };
#define PIXEL_FILL(n) ((n) | ((n) << 4))
static u32 layout, contest, position, environment;
u32 GetBattlerCoordsIndex(u32 battler) { (void)battler; return layout; }
u32 IsContest(void) { return contest; }
u32 GetBattlerPosition(u32 battler) { (void)battler; return position; }
static u32 GetBattleEnvironmentOverride(void) { return environment; }
static struct { u32 windowsType; } gBattleScripting;
static u8 pixelsTest[704];
AttrValue GetWindowAttribute(u32 window, u32 attr) {
    (void)window;
    return attr == WINDOW_TILE_DATA ? (AttrValue)pixelsTest : 11;
}
void FillWindowPixelBuffer(u32 window, u8 value) {
    (void)window;
    for (u32 i=0;i<704;i++) pixelsTest[i]=value;
}
'''
BORDER='void TestNameWindow(u8 windowId)\n{\n'+m.BORDER_NEW.rsplit('\n        else',1)[0]+'\n}\n'
MAIN=r'''
#ifdef HOST_TEST
int main(void) {
    const s16 defaults[]={-40,0,32,60,64,72,80,174,176,178,208,240};
    u32 n=0;
    for(layout=0;layout<2;layout++)
    for(contest=0;contest<2;contest++)
    for(position=0;position<4;position++)
    for(environment=0;environment<26;environment++)
    for(u32 i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++) {
        s16 expect=defaults[i];
        if (!layout && !contest && position<2) {
            if(environment==0) expect=position==0?60:178;
            if(environment==9) expect=position==0?64:174;
        }
        if(KyotoBattlePlatformX(position,defaults[i])!=expect) return 2;
        n++;
    }
    layout=contest=0; position=1;
    for(environment=0;environment<26;environment++) {
        s16 slide=KyotoBattlePlatformX(1,208)-32;
        s16 expect=environment==0?178:environment==9?174:208;
        if(slide+32 != expect) return 3;
    }
    gBattleScripting.windowsType=B_WIN_TYPE_NORMAL;
    memset(pixelsTest,0xaa,sizeof(pixelsTest));
    TestNameWindow(B_WIN_ACTION_NAME);
    for(u32 y=0;y<16;y++) for(u32 x=0;x<88;x++) {
        u32 off=(y/8)*352+(x/8)*32+(y%8)*4+(x%8)/2;
        u8 pixel=(pixelsTest[off]>>((x&1)*4))&15;
        if(pixel!=(x==0?4:1)) return 4;
    }
    u8 saved[704]; memcpy(saved,pixelsTest,sizeof(saved));
    gBattleScripting.windowsType=1;
    TestNameWindow(B_WIN_ACTION_NAME);
    if(memcmp(saved,pixelsTest,sizeof(saved))) return 5;
    gBattleScripting.windowsType=0;
    memset(pixelsTest,0xaa,sizeof(pixelsTest));
    TestNameWindow(1);
    for(u32 i=0;i<704;i++) if(pixelsTest[i]!=0xaa) return 6;
    printf("PASS: %u horizontal-anchor cases; 26 trainer-slide compensations; 1408 name-window pixels; non-Kyoto guards.\n",n);
    return 0;
}
#endif
'''

def main():
    c=PREAMBLE+'\n'+m.CENTER_HELPER+'\n'+BORDER+'\n'+MAIN
    with tempfile.TemporaryDirectory() as tmp:
        p=Path(tmp); (p/'check.c').write_text(c)
        cc=shutil.which('cc') or shutil.which('gcc')
        if not cc: raise SystemExit('A native C compiler is required for this developer test.')
        subprocess.run([cc,'-std=gnu17','-Wall','-Wextra','-Werror','-DHOST_TEST',str(p/'check.c'),'-o',str(p/'check')],check=True)
        subprocess.run([str(p/'check')],check=True)
        clang=shutil.which('clang')
        if clang:
            subprocess.run([clang,'--target=arm-none-eabi','-mcpu=arm7tdmi','-mthumb','-std=gnu17','-Wall','-Wextra','-Werror','-O2','-c',str(p/'check.c'),'-o',str(p/'check.o')],check=True)
            print('PASS: isolated helper and name-window repair compile for ARM7TDMI Thumb.')
        else: print('SKIP: ARM/Thumb compilation; clang is not installed.')
    print('Scope: isolated logic with stubbed game queries, not the full ROM or a gameplay test.')
if __name__=='__main__':main()
