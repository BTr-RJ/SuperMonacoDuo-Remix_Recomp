"""Deterministic local boot/menu/race captures; never controls the desktop."""
from pathlib import Path
import os, subprocess, json, time, argparse, re
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--frames',type=int,default=3000)
p.add_argument('--tag',default='boot')
p.add_argument('--game',default='all')
p.add_argument('--script')
p.add_argument('--original',action='store_true')
p.add_argument('--analog-test',action='store_true',help='Explicit QA SDL virtual controller; ordinary replays keep analog disabled')
p.add_argument('--aspect',choices=['4:3','16:9','21:9','32:9'],default='16:9')
p.add_argument('--wav',action='store_true')
p.add_argument('--filter',default='raw')
p.add_argument('--build',default='build/native')
a=p.parse_args()
games=['smgp1','smgp2'] if a.game=='all' else [a.game]
for game in games:
    out=ROOT/'build/validation'/a.tag/game
    out.mkdir(parents=True,exist_ok=True)
    script=out/'replay.input'
    text='WAIT 600\nSCREENSHOT title.png\n'
    for i in range(6): text+=f'PRESS START 2\nWAIT 180\nSCREENSHOT menu{i}.png\n'
    text+='HOLD B\nWAIT 900\nSCREENSHOT race.png\nDUMP_RAM ram.bin\n'
    content=Path(a.script).read_text() if a.script else text
    binary_dir=ROOT/a.build/'bin'/game
    content=re.sub(r'(?m)^(SCREENSHOT|DUMP_RAM|DUMP_VRAM) (\S+)',
        lambda m:m[1]+' '+os.path.relpath(out/m[2],binary_dir).replace('\\','/'),content)
    script.write_text(content)
    env=os.environ.copy(); env.update(RR_TEST_HIDDEN='1',SMGP_QA_INPUT_ONLY='1',SMGP_ANALOG='0',RR_CONTROLS_PROFILE='1')
    if a.analog_test:
        assert 'SMGP_QA_ANALOG_AXIS'in env,'Analog QA requires an explicit virtual-controller axis'
        env['SMGP_ANALOG']='1'
    # Baseline replay comparisons isolate unchanged cartridge physics. Dedicated
    # tree-collision QA opts into the explicitly requested new gameplay feature.
    env.setdefault('SMGP_TREE_COLLISIONS','0')
    # Same isolation for the 0.21ab arcade prop collisions (owner-requested).
    env.setdefault('SMGP_PROP_COLLISIONS','0')
    env['SMGP_ORIGINAL']='1' if a.original else '0'
    env['SMGP_ASPECT']=a.aspect;env['GENESIS_SCREEN']=a.filter;env['RR_PRESENT_PROBE']='1'
    startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    start=time.monotonic()
    cmd=[str(binary_dir/f'{game}.exe'),str(ROOT/f'.local/build-inputs/{game}/game.bin'),
         '--benchmark',str(a.frames),'--pacing','accurate','--input-script',str(script),'--hash-frames','300']
    if a.wav:cmd+=['--wav',str(out/'audio.wav')]
    with (out/'run.log').open('w') as log:
        try: r=subprocess.run(cmd,cwd=out,stdout=log,stderr=log,env=env,timeout=max(100,a.frames/60+30),startupinfo=startup); code=r.returncode
        except subprocess.TimeoutExpired: code='timeout'
    report=dict(game=game,code=code,seconds=round(time.monotonic()-start,2),frames=a.frames,
                aspect=a.aspect,zoom=not a.original,captures=[x.name for x in out.glob('*.png')])
    (out/'result.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report),flush=True)
    print((out/'run.log').read_text(errors='replace')[-2400:],flush=True)
