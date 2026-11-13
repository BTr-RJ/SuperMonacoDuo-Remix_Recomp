"""Export arcade (X-board) roadside props used as hybrid replacements: chevron boards and tyre stacks.

QA/asset tool only. Decodes the owner's sprite ROMs (smgp.zip, SHA-256 checked) with the
native sprite descriptors and palette RAM captured by tools/arcade_drive20.lua in MAME
(build/analysis/arcade21-persp). Nothing here is linked into the product; the product reads
the exported SGPA files and their measured world size from assets/arcade-smgp/props21.

World size (units per source pixel, 1 unit = one rival-car pixel at the same depth) was
measured from the same capture: guard rails are ground objects (foot = 170 + 42.38*depth);
chevron and tyre factors come from the approach rate of 1/scale against guard rails in the
same frames (measured with the MAME QA oracle, which is not part of the product).
"""
from pathlib import Path
import argparse,hashlib,json,math,struct,zipfile
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--captures',type=Path,default=ROOT/'build/analysis/arcade21-persp');a=p.parse_args()
archive=ROOT/'smgp.zip';identity=hashlib.sha256(archive.read_bytes()).hexdigest()
assert identity=='984858770e4511cc90e6e50a127e88d9d0b5d9e4a1e325c848a5971daa60c1b8','Unqualified arcade archive'
groups=[['mpr-12425.90','mpr-12426.94','mpr-12427.98','mpr-12428.102'],
 ['mpr-12421.91','mpr-12422.95','mpr-12423.99','mpr-12424.103'],
 ['mpr-12417.92','mpr-12418.96','mpr-12419.100','mpr-12420.104'],
 ['epr-12609.93','epr-12610.97','epr-12611.101','epr-12612.105']]
rom=bytearray(0x200000)
with zipfile.ZipFile(archive)as z:
    for group,names in enumerate(groups):
        for lane,name in enumerate(names):
            data=z.read(name);assert len(data)==0x20000
            rom[group*0x80000+lane:(group+1)*0x80000:4]=data
weights=[1/3900,1/2000,1/1000,1/500,1/250];total=sum(weights)
def channel(value):return round(sum(w for i,w in enumerate(weights)if value&(1<<i))*255/total)
def palette(raw,number):
    out=[]
    for i in range(16):
        v=int.from_bytes(raw[(number*16+i)*2:(number*16+i)*2+2],'big')
        out.append((channel(((v>>12)&1)|((v<<1)&30)),channel(((v>>13)&1)|((v>>3)&30)),channel(((v>>14)&1)|((v>>7)&30)),255 if i not in (0,15)else 0))
    return out
def decode(bank,addr,pitch,flip,rows,colours):
    lines=[];width=0
    for y in range(rows):
        row=[];start=(addr+pitch*y)&65535
        for x in range(64):
            word=(start+(-x if flip else x))&65535;off=(bank%8)*0x40000+word*4
            value=int.from_bytes(rom[off:off+4],'little')
            pixels=[(value>>shift)&15 for shift in (range(0,32,4)if flip else range(28,-1,-4))]
            row.extend(pixels)
            if pixels[6]==15:break
        lines.append(row);width=max(width,len(row))
    im=Image.new('RGBA',(width,rows));pix=im.load()
    for y,row in enumerate(lines):
        for x,n in enumerate(row):pix[x,y]=colours[n]
    return im
# name, bank, address, palette, units per source pixel (measured), elevation units (measured, informative)
WANT=[('chevron-blue',2,0xbc29,70,1.22,35),('chevron-red',2,0xbc29,112,1.22,35),
      ('tyres-red-full',1,0x718a,147,1.25,0),('tyres-green-full',1,0x718a,148,1.25,0)]
found={}
for capture in sorted(a.captures.glob('sat*.bin')):
    frame=int(capture.stem[3:]);sat=capture.read_bytes();pal=(a.captures/f'pal{frame:05}.bin').read_bytes()
    for slot in range(len(sat)//16):
        d=struct.unpack_from('>8H',sat,slot*16)
        if d[0]&0x8000:break
        if d[0]&0x5000:continue
        bank=(d[0]>>9)&7;addr=d[1];number=d[6]&255
        for name,b,ad,pn,unit,elev in WANT:
            if name in found or (bank,addr,number)!=(b,ad,pn):continue
            pitch=((d[2]>>1)|((d[4]&0x1000)<<3))>>8;pitch=pitch-256 if pitch>=128 else pitch
            flip=(~d[4]>>14)&1;zoom=max(64,d[3]&2047);height=(d[5]&4095)+1;rows=math.ceil(height*zoom/512)
            im=decode(bank,addr,pitch,flip,rows,palette(pal,number))
            if not d[4]&0x8000:im=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            if not d[4]&0x2000:im=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            box=im.getbbox();im=im.crop(box)
            found[name]=dict(image=im,frame=frame,slot=slot,zoom=zoom,unit=unit,elevation=elev,bank=bank,address=addr,palette=number)
missing=[w[0] for w in WANT if w[0] not in found];assert not missing,missing
out=ROOT/'assets/arcade-smgp/props21';out.mkdir(parents=True,exist_ok=True);entries=[]
for name,_,_,_,_,_ in WANT:
    f=found[name];im=f['image'];raw=b'SGPA'+struct.pack('<II',im.width,im.height)+im.tobytes()
    (out/(name+'.rgba')).write_bytes(raw);im.save(out/(name+'.png'))
    entries.append(dict(file=name+'.rgba',sha256=hashlib.sha256(raw).hexdigest(),bank=f['bank'],address=f['address'],palette=f['palette'],
        width=im.width,height=im.height,units_per_pixel=f['unit'],elevation_units=f['elevation'],capture_frame=f['frame'],capture_slot=f['slot'],capture_zoom=f['zoom']))
(out/'units.txt').write_text(''.join(f"{e['file'][:-5]} {e['units_per_pixel']} {e['elevation_units']}\n" for e in entries))
(out/'PROVENANCE.json').write_text(json.dumps(dict(archive_sha256=identity,private_local=True,format='SGPA + little-endian u32 width,height + row-major RGBA8',
    captures=str(a.captures.relative_to(ROOT)),unit='one rival-car source pixel at the same depth',assets=entries),indent=2))
print(json.dumps([{k:v for k,v in e.items() if k!='sha256'} for e in entries],indent=1))
