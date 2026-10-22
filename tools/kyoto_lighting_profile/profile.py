import ctypes as C, subprocess,json,sys
from pathlib import Path
from PIL import Image
rom, elf, save, out, runner = sys.argv[1:]
root=Path(out); root.mkdir(parents=True,exist_ok=True)
r=C.CDLL(runner); r.start.argtypes=[C.c_char_p,C.c_char_p]; r.video.restype=C.c_void_p
syms={p[2]:int(p[0],16) for l in subprocess.check_output(['arm-none-eabi-nm',elf],text=True).splitlines() if len(p:=l.split())==3}
r.start(rom.encode(),save.encode())
def snap(name):
    Image.frombytes('RGBA',(240,160),C.string_at(r.video(),240*160*4)).convert('RGB').save(root/(name+'.png'))
def status():
    a=syms['gMain']; print('callback',hex(r.read32(a+4)), 'overworld',hex(syms['CB2_Overworld']), 'pc',hex(r.pc()),'time',r.read8(syms['gTimeOfDay']), flush=True)

for i in range(600): r.frame(0)
r.write32(syms['gKyotoProfile']+60,1)
for i in range(200):
    r.write8(syms['gTimeOfDay'],3);r.frame(0)
snap('field');status()
print('profile',[r.read32(syms['gKyotoProfile']+4*i) for i in range(16)])
print('lamps',[(i,r.read16(syms['gSprites']+68*i+32),r.read16(syms['gSprites']+68*i+34)) for i in range(64) if r.read32(syms['gSprites']+68*i+28)==syms['UpdateLightSprite']+1])
for name,key,n in [('still',0,60),('up',64,64),('down',128,128),('left',32,64),('right',16,128)]:
    rows=[]
    for i in range(n):
        r.write8(syms['gTimeOfDay'],3); r.frame(key)
        rows.append([r.read32(syms['gKyotoProfile']+4*j) for j in range(16)])
    (root/(name+'.json')).write_text(json.dumps(rows))
    snap(name);status()

r.write32(syms['gKyotoProfile']+60,5)
for i in range(200):r.write8(syms['gTimeOfDay'],3);r.frame(0)
lamps=[syms['gSprites']+68*i for i in range(64) if r.read32(syms['gSprites']+68*i+28)==syms['UpdateLightSprite']+1]
player=syms['gSprites']+68*r.read8(syms['gPlayerAvatar']+4)
for j,a in enumerate(lamps[2:4]):
    r.write16(a+32,r.read16(player+32)+j*8)
    r.write16(a+34,r.read16(player+34)-8)
for command,name in [(0,'overlap-extra'),(2,'clouds-extra'),(3,'fireflies-extra')]:
    if command:r.write32(syms['gKyotoProfile']+60,command)
    rows=[]
    for i in range(360):
        r.write8(syms['gTimeOfDay'],3);r.frame(0)
        if i>=240:rows.append([r.read32(syms['gKyotoProfile']+4*j) for j in range(16)])
    (root/(name+'.json')).write_text(json.dumps(rows));snap(name)
    print(name, 'finished',flush=True)

# Exercise UI handoff and tile/palette cleanup; these are not timing samples.
for keys,n,name in [(8,2,None),(0,90,'start-menu'),(1,2,None),(0,90,'bag'),(2,2,None),(0,90,'bag-return'),(2,2,None),(0,90,'menu-return')]:
    for i in range(n):r.write8(syms['gTimeOfDay'],3);r.frame(keys)
    if name:snap(name)
for command,name in [(4,'indoors'),(5,'return-outdoors')]:
    r.write32(syms['gKyotoProfile']+60,command)
    for i in range(240):r.write8(syms['gTimeOfDay'],3);r.frame(0)
    snap(name);status()
for i in range(80):r.write8(syms['gTimeOfDay'],1);r.frame(0)
snap('daytime')
print('daytime masks',[r.read16(syms['sMasks']+8*i) for i in range(16)])
if 'gKyotoDiff' in syms:
    print('reference masks, failures',r.read32(syms['gKyotoDiff']),r.read32(syms['gKyotoDiff']+4))
