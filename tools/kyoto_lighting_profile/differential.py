from pathlib import Path
import subprocess
s=subprocess.check_output(['git', 'show', '6a9ac4a323f3009bbc8fec0f1f8e7318932f8c89:src/kyoto_lamp_mask.c'], text=True)
a=s.index('static bool32 BehindMap');b=s.index('\nvoid KyotoLamp_Prepare',a)
ref=s[a:b].replace('BehindMap','ReferenceBehindMap')
a=s.index('void KyotoLamp_Prepare');b=s.index('    if (cut)',a)
f=s[a:b].replace('void KyotoLamp_Prepare(const struct Sprite *sprite, u32 index)','static bool32 ReferenceMask(const struct Sprite *sprite, u8 *pixels)').replace('    u8 *pixels = (u8 *)sLampPixels[index];\n','').replace('        ReleaseMask(index);\n        return;', '        return FALSE;').replace('BehindMap(', 'ReferenceBehindMap(')
f+='    return cut;\n}\n'
f=f.replace('static bool32 ReferenceMask(', 'static __attribute__((noinline)) bool32 ReferenceMask(')
p=Path('src/kyoto_lamp_mask.c');s=p.read_text();s=s.replace('void KyotoLamp_Prepare(', 'EWRAM_DATA u32 gKyotoDiff[4] = {0};\n'+ref+'\n'+f+'\nvoid KyotoLamp_Prepare(',1)
s=s.replace('    u32 work[MASK_BYTES / 4];','    u32 work[MASK_BYTES / 4], expected[MASK_BYTES / 4];\n    bool32 expectedCut = ReferenceMask(sprite, (u8 *)expected);')
s=s.replace('    if (cut)\n    {\n        bool32 allocated', '''    gKyotoDiff[0]++;
    if (cut != expectedCut || memcmp(pixels, expected, MASK_BYTES))
        gKyotoDiff[1]++;
    if (cut)
    {
        bool32 allocated''')
p.write_text(s)
