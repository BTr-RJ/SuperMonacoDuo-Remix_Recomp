"""Decode the owner's X-board sprite ROMs using native descriptor/palette captures.

MAME is an optional QA oracle only. No ROM download, guest code, or emulator is
linked into the product. Raw capture descriptors preserve source/colour provenance.
"""
from pathlib import Path
import argparse,hashlib,json,math,struct,zipfile
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--captures',type=Path,default=ROOT/'build/analysis/arcade05');p.add_argument('--reuse',action='store_true');a=p.parse_args()
archive=ROOT/'smgp.zip';identity=hashlib.sha256(archive.read_bytes()).hexdigest()
assert identity=='984858770e4511cc90e6e50a127e88d9d0b5d9e4a1e325c848a5971daa60c1b8','Unqualified arcade archive'
groups=[['mpr-12425.90','mpr-12426.94','mpr-12427.98','mpr-12428.102'],
 ['mpr-12421.91','mpr-12422.95','mpr-12423.99','mpr-12424.103'],
 ['mpr-12417.92','mpr-12418.96','mpr-12419.100','mpr-12420.104'],
 ['epr-12609.93','epr-12610.97','epr-12611.101','epr-12612.105']]
rom=bytearray(0x200000);inputs=[]
with zipfile.ZipFile(archive)as z:
    assert z.testzip()is None
    for group,names in enumerate(groups):
        for lane,name in enumerate(names):
            data=z.read(name);assert len(data)==0x20000
            rom[group*0x80000+lane:(group+1)*0x80000:4]=data
            inputs.append(dict(name=name,sha256=hashlib.sha256(data).hexdigest(),crc32=f'{z.getinfo(name).CRC:08x}'))
out=ROOT/'.local/arcade-assets';out.mkdir(parents=True,exist_ok=True)
weights=[1/3900,1/2000,1/1000,1/500,1/250];total=sum(weights)
def channel(value):return round(sum(w for i,w in enumerate(weights)if value&(1<<i))*255/total)
def palette(raw,number):
    colours=[]
    for i in range(16):
        v=int.from_bytes(raw[(number*16+i)*2:(number*16+i)*2+2],'big')
        colours.append((channel(((v>>12)&1)|((v<<1)&30)),channel(((v>>13)&1)|((v>>3)&30)),channel(((v>>14)&1)|((v>>7)&30)),255 if i not in (0,15)else 0))
    return colours
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
    if not width:return None
    im=Image.new('RGBA',(width,rows));pix=im.load()
    for y,row in enumerate(lines):
        for x,n in enumerate(row):pix[x,y]=colours[n]
    box=im.getbbox()
    return (im.crop(box),box)if box else None
assets={};observations=0
for capture in ([] if a.reuse else sorted(a.captures.glob('sat*.bin'))):
    frame=int(capture.stem[3:]);sat=capture.read_bytes();pal=(a.captures/f'pal{frame:05}.bin').read_bytes()
    for slot in range(len(sat)//16):
        d=struct.unpack_from('>8H',sat,slot*16)
        if d[0]&0x8000:break
        if d[0]&0x5000:continue
        bank=(d[0]>>9)&7;addr=d[1];pitch=((d[2]>>1)|((d[4]&0x1000)<<3))>>8;pitch=pitch-256 if pitch>=128 else pitch
        flip=(~d[4]>>14)&1;zoom=max(64,d[3]&2047);height=(d[5]&4095)+1
        rows=math.ceil(height*zoom/512);x=d[2]&511;top=(d[0]&511)-256
        if not 1<=rows<=512 or not pitch:continue
        number=d[6]&255;colours=palette(pal,number)
        result=decode(bank,addr,pitch,flip,rows,colours)
        if not result:continue
        im,box=result
        if not d[4]&0x8000:im=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        digest=hashlib.sha256(struct.pack('<II',im.width,im.height)+im.tobytes()).hexdigest();observations+=1
        observation=dict(frame=frame,slot=slot,bank=bank,address=addr,pitch=pitch,flip=flip,source_rows=rows,palette=number,screen_x=x-190,screen_y=top,native_height=height,vertical_zoom=zoom,horizontal_zoom=d[4]&2047,source_crop=list(box))
        if digest not in assets:
            index=len(assets);name=f'sprite-{index:04}';im.save(out/(name+'.png'))
            # A tiny, documented RGBA source format for our own presentation.
            (out/(name+'.rgba')).write_bytes(b'SGPA'+struct.pack('<II',im.width,im.height)+im.tobytes())
            assets[digest]=dict(id=index,file=name+'.png',rgba=name+'.rgba',width=im.width,height=im.height,sha256=digest,observations=[])
        assets[digest]['observations'].append(observation)
if a.reuse:
    previous=json.loads((out/'index.json').read_text());assert previous['archive_sha256']==identity
    assets={item['sha256']:item for item in previous['assets']};observations=previous['observations']
    for item in assets.values():assert hashlib.sha256((out/item['rgba']).read_bytes()[4:]).hexdigest()==item['sha256']
items=list(assets.values());report=dict(archive_sha256=identity,rom_inputs=inputs,observations=observations,unique_assets=len(items),assets=items,format='SGPA + little-endian u32 width,height + row-major RGBA8',source='Owner-provided ROM; native sprite/palette descriptors captured with official MAME, QA only')
(out/'index.json').write_text(json.dumps(report,indent=2))
delivery=ROOT/'assets/arcade-smgp';delivery.mkdir(parents=True,exist_ok=True);exported=[]
def display_image(item,bank,address,number):
    image=Image.open(out/item['file'])
    observation=next(o for o in item['observations']if o['bank']==bank and o['address']==address and o['palette']==number)
    data=(a.captures/f"sat{observation['frame']:05}.bin").read_bytes();control=struct.unpack_from('>8H',data,observation['slot']*16)[4]
    # The ROM word order and the X-board display direction are separate.
    # Normalise to the native captured screen direction before exporting.
    if not control&0x2000:image=image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    return image
for name,address in [('rear-straight',0x2e00),('rear-slight',0x319c),('rear-turn',0x3538),('rear-sharp',0x3975)]:
    candidates=[item for item in items if any(o['bank']==3 and o['address']==address and o['flip']==0 and o['palette']==0 for o in item['observations'])]
    assert candidates,name
    item=max(candidates,key=lambda x:x['width']*x['height']);im=display_image(item,3,address,0)
    for mirror in [False,True]:
        image=im.transpose(Image.Transpose.FLIP_LEFT_RIGHT)if mirror else im
        filename=name+('-mirror'if mirror else '')+'.rgba'
        raw=b'SGPA'+struct.pack('<II',image.width,image.height)+image.tobytes();(delivery/filename).write_bytes(raw)
        image.save(delivery/filename.replace('.rgba','.png'))
        exported.append(dict(file=filename,sha256=hashlib.sha256(raw).hexdigest(),source_asset=item['id'],bank=3,address=address,palette=0,mirror=mirror,width=image.width,height=image.height))
(delivery/'PROVENANCE.json').write_text(json.dumps(dict(archive_sha256=identity,private_local=True,format=report['format'],assets=exported),indent=2))
for name,bank,address,number in [('tyres-red',1,0x718a,147),('tyres-green',1,0x7845,148),('billboard-longines-campari',1,0xcf3a,167)]:
    candidates=[item for item in items if any(o['bank']==bank and o['address']==address and o['palette']==number for o in item['observations'])]
    assert candidates,name
    item=max(candidates,key=lambda x:x['width']*x['height']);im=display_image(item,bank,address,number);filename=name+'.rgba'
    raw=b'SGPA'+struct.pack('<II',im.width,im.height)+im.tobytes();(delivery/filename).write_bytes(raw);im.save(delivery/(name+'.png'))
    exported.append(dict(file=filename,sha256=hashlib.sha256(raw).hexdigest(),source_asset=item['id'],bank=bank,address=address,palette=number,width=im.width,height=im.height))
(delivery/'PROVENANCE.json').write_text(json.dumps(dict(archive_sha256=identity,private_local=True,format=report['format'],assets=exported),indent=2))
review=ROOT/'build/analysis/arcade05';review.mkdir(parents=True,exist_ok=True)
for start in range(0,len(items),100):
    sheet=Image.new('RGB',(1600,1600),'#333');d=ImageDraw.Draw(sheet)
    for j,item in enumerate(items[start:start+100]):
        x=j%10*160;y=j//10*160;im=Image.open(out/item['file']);im.thumbnail((156,138));sheet.paste(im,(x,y+20),im)
        d.text((x,y),f"{item['id']} {item['width']}x{item['height']}",fill='white')
    sheet.save(review/f'assets-{start:04}.png')
assert hashlib.sha256(archive.read_bytes()).hexdigest()==identity
print(json.dumps(dict(archive_sha256=identity,unique_assets=len(items),observations=observations,output=str(out)),indent=2))
