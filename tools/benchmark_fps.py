"""Measure ROM frame intervals in headless mGBA, in a reproducible seeded scene.
Not a complete gameplay benchmark; results are emulated GBA cycles, not host speed.
"""
from pathlib import Path
import subprocess,os,re,tempfile,shutil,sys,json
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('rom',nargs='?',default='gba_3d.gba')
parser.add_argument('--scenario',choices=['idle','drive','turn','boost'],default='idle')
parser.add_argument('--mode',choices=['soccer','hockey'],default='soccer')
args=parser.parse_args()
rom=Path(args.rom).resolve()
elf=rom.with_suffix('.elf')
nm=subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm',str(elf)],text=True)
symbols={v[2]:int(v[0],16) for l in nm.splitlines() if len(v:=l.split())==3}
disasm=subprocess.check_output(['/opt/devkitpro/devkitARM/bin/arm-none-eabi-objdump','-d',str(elf)],text=True)
# Press A in Play through the actual menu handler after key_poll. This calls
# reset_match and initializes opponents/pads, unlike forcing a training state.
key_return=int(re.search(r'\n\s*([0-9a-f]+):[^\n]*\bbl\s+[^\n]*<key_poll>',disasm).group(1),16)+4
commands=[f'b/t 0x{symbols.get("present_frame",symbols["swap_buffers"]):x}','c',
    f'w/4 0x{symbols["game_state"]:x} 2',f'b/t 0x{key_return:x}','c',
    f'w/2 0x{symbols["__key_curr"]:x} {129 if args.mode=="hockey" else 1}',f'w/2 0x{symbols["__key_prev"]:x} 0','d 2','c']
keys={'idle':0,'drive':64,'turn':64|32,'boost':64|2}[args.scenario]
commands += [f'b/t 0x{key_return:x}']
for frame in range(150):
    commands += ['c',f'w/2 0x{symbols["__key_curr"]:x} {keys}',
                 f'w/2 0x{symbols["__key_prev"]:x} {keys}','c']
commands += [f'r/4 0x{symbols["is_hockey_match"]:x}',f'r/4 0x{symbols["game_state"]:x}','q']
with tempfile.TemporaryDirectory() as tmp:
    target=Path(tmp)/'bench.gba';shutil.copyfile(rom,target)
    result=subprocess.run(['stdbuf','-oL','/usr/games/mgba','-d',str(target)],input='\n'.join(commands)+'\n',text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env={**os.environ,'SDL_VIDEODRIVER':'dummy','SDL_AUDIODRIVER':'dummy'},timeout=20)
states=re.findall(r'^\s*0x([0-9A-Fa-f]{8})\s*$',result.stdout,re.M)
assert states and int(states[-1],16)==8, "Benchmark did not remain in active gameplay"
assert int(states[-2],16)==int(args.mode=="hockey"), "Wrong game mode"
# Only presentation breakpoints count; input-injection stops are excluded.
cycles=[int(x) for x in re.findall(r'Hit breakpoint 1 at[^\n]*\n(?:(?!Hit breakpoint).)*?Cycle: (\d+)',result.stdout,re.S)]
assert len(cycles)>=152,result.stdout[-2000:]
cycles=cycles[-121:]
intervals=[b-a for a,b in zip(cycles,cycles[1:])]
print(json.dumps(dict(rom=str(rom),scenario=args.scenario,mode=args.mode,frames=len(intervals),mean_cycles=round(sum(intervals)/len(intervals)),fps=round(16777216*len(intervals)/sum(intervals),2),worst_cycles=max(intervals),target_cycles=561792)))
