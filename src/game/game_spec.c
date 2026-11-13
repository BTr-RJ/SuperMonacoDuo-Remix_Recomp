#include "game_spec.h"
#include "genesis_runtime.h"
#include "genesis_machine.h"
#include "m68k_interp.h"
#include "scene.h"
#include "controls.h"
#include "sound.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#if GAME_smgp1
#include "smgp1_identity.h"
#else
#include "smgp2_identity.h"
#endif

static uint64_t irq_epoch;
static int racing,margin;
int rr_game_key(int key) { (void)key; return 0; }
static void entry(void) { recomp_call_addr(GAME_ENTRY); }
/* These games install self-modifying HBlank handlers in RAM. Execute their
   live IRQ bodies until every copied-code path has a qualified translation. */
static void irq(unsigned pc) {
    uint32_t target=pc&0xffffffu,exit_pc=0;
    M68kiStatus status=m68k_interp_run_ram_handler(target,&exit_pc);
    if(status!=M68KI_OK) {
      fprintf(stderr,"IRQ failed %d target=%06X PC=%06X bad=%06X op=%04X A7=%08X\n",status,target,g_cpu.PC,g_m68ki_bad_pc,g_m68ki_bad_op,g_cpu.A[7]);
      FILE *f=fopen("irq-ram.bin","wb");if(f){fwrite(g_ram,1,65536,f);fclose(f);} exit(2);
    }
    ++irq_epoch;
}
static void vblank(void) { irq(GAME_VINT); smgp_scene_vblank(); }
static void hblank(void) { irq(GAME_HINT); }
void genesis_stop_until_interrupt(uint16_t sr) {
    uint64_t epoch=irq_epoch;
    g_cpu.SR=sr;
    do { glue_yield_for_interrupt_poll(); } while(epoch==irq_epoch);
}
static const char *no_states(void) { return "Super Monaco state restoration is not qualified in 0.1"; }
static void frame_pre(uint64_t frame) {
    (void)frame;
    int nw=gvdp_screen_width(&g_machine.vdp);
    racing=nw==256 && (g_machine.vdp.reg[1]&0x40)!=0;
#if GAME_smgp1
    /* Championship advice/showroom also uses H32, but its 128-column
       nametable is not a racing panorama. Never apply the road profile. */
    racing=racing && g_machine.vdp.reg[16]==0x11 &&
        (g_machine.vdp.reg[2]&0x38)==0x38 && (g_machine.vdp.reg[4]&7)==6;
#else
    racing=racing && g_machine.vdp.reg[16]==0x11 &&
        (g_machine.vdp.reg[2]&0x38)==0x30 && (g_machine.vdp.reg[4]&7)==7;
    /* Senna's three-course photo menu also uses this H32/map layout and
       D3A4, but its outside columns are storage, not a racing panorama.
       Qualified native dispatchers: setup, selection loop, next-page setup.
       Keep its entire original viewport, including animation and fades. */
    unsigned page=(unsigned)g_ram[0xff20]<<24|(unsigned)g_ram[0xff21]<<16|
        (unsigned)g_ram[0xff22]<<8|g_ram[0xff23];
    unsigned queued=(unsigned)g_ram[0xff24]<<24|(unsigned)g_ram[0xff25]<<16|
        (unsigned)g_ram[0xff26]<<8|g_ram[0xff27];
    /* FC04 marks the preliminary drive, not Championship's main race.
       Results clear both; preserve the qualified 11A50..11D84 result
       loaders/loops, including D34E's queued fade target from D31A/D33C.
       Layout alone also matches name, garage and course-photo pages. */
    unsigned result_page=page==0xd34e?queued:page;
    racing=racing&&(result_page==0x109d8||result_page==0x10a72||
        (g_ram[0xfc04]||g_ram[0xfc05])||
        (g_ram[0xfe1c]||g_ram[0xfe1d])||(result_page>=0x11a50&&result_page<0x11e00));
    /* 4E96/4FD2 are NAME ENTRY, including nationality: the same layout
       and sprite builder do not make this an engine/road scene. */
    if(page==0x4e96||page==0x4fd2||page==0x55c6||page==0x56cc||page==0x5944)racing=0;
#endif
    const char *aspect=getenv("SMGP_ASPECT");
    double ratio=16.0/9;
    if(aspect && !strcmp(aspect,"4:3"))ratio=4.0/3;
    if(aspect && !strcmp(aspect,"21:9"))ratio=21.0/9;
    if(aspect && !strcmp(aspect,"32:9"))ratio=32.0/9;
    margin=(int)(nw*(ratio/(4.0/3)-1)*.5+.5);
    gvdp_set_ws_canvas(nw+margin*2);
    gvdp_set_ws_extra(racing?margin:0);
    gvdp_set_ws_bar_black(1);
    extern void gvdp_set_road_profile(int);
    gvdp_set_road_profile(racing?10:0);
#if GAME_smgp1
    int game=0;
#else
    int game=1;
#endif
    /* GP II alternates two completed HBlank tables at CA00/CBC0. FC42 selects
       the table installed by D14E..D164 alongside the uploaded hscroll, not
       the inactive table currently being built by EDBE..EEB6. Read it for
       every B row, including the original mirror surround; restore guest
       registers after each fetch without changing CPU/IRQ timing. */
    int raster=game==1 && racing && g_ram[0xffd6]==0x33 && g_ram[0xffd7]==0xf8 &&
        g_ram[0xffda]==0 && g_ram[0xffdb]==0xc0 && g_ram[0xffdc]==0 && g_ram[0xffdd]==0;
    unsigned raster_bank=(unsigned)g_ram[0xfc42]<<8|g_ram[0xfc43];
    raster=raster&&(raster_bank==0||raster_bank==0x1c0);
    machine_set_raster_vscroll_source(raster?g_ram+0xca00+raster_bank:NULL,0,224);
    if(raster&&getenv("SMGP_RASTER_BANK_TRACE"))fprintf(stderr,"[SMGP RASTER20] frame=%llu active=%04X pending=%04X\n",(unsigned long long)frame,0xca00+raster_bank,((unsigned)g_ram[0xfc40]<<8)|g_ram[0xfc41]);
    smgp_controls_context(racing,game,g_ram);
    smgp_sound_frame(game,frame,racing);
    const char *original=getenv("SMGP_ORIGINAL");
    smgp_scene_begin(game,frame,racing,!(original&&original[0]=='1'));
}
static int video_config(const char *mode){return !strcmp(mode,"off");}
static int video_enabled(void){return 0;}
static int video_width(int a,int b,int c,int d){return 0;}
static void overlay(const struct GVDP *v,int line,uint32_t *out,int width){
    smgp_scene_formation_overlay(line,out,width);
    int native=gvdp_screen_width(v),offset=(width-native)/2;
    if(racing&&line<65){
#if GAME_smgp2
      /* Extend the actual original mirror frame, including palette fades.
         A fixed host grey made a second, visibly different surround. */
      uint32_t left=out[offset],right=out[offset+native-1];
      for(int x=0;x<offset;x++){out[x]=left;out[width-1-x]=right;}
#else
      for(int x=0;x<offset;x++)out[x]=out[width-1-x]=0xff303030u;
#endif
    }
}
static const GameVideo video={.configure=video_config,.enabled=video_enabled,.width=video_width,.overlay_scanline=overlay};
static void frame_post(uint64_t frame) {
    smgp_scene_diagnostics();
    if(getenv("SMGP_SOUND_TRACE")&&frame%60==0){
      fprintf(stderr,"[SMGP SOUND] frame=%llu zpc=%04X iff=%d halt=%d irq=%d request=%d reset_off=%d bank=%06X driver=",
       (unsigned long long)frame,g_machine.z80.pc,g_machine.z80.iff1,g_machine.z80.halted,g_machine.z80.int_pending,g_machine.bus.z80_busreq,g_machine.bus.z80_reset_off,g_machine.bus.z80_bank);
      for(unsigned a=0x5ac0;a<0x5af0;a++)fprintf(stderr,"%02X",g_ram[a]);
      fprintf(stderr," mailbox=");for(unsigned a=0x1fa0;a<0x2000;a++)fprintf(stderr,"%02X",g_machine.bus.z80_ram[a]);fprintf(stderr,"\n");
    }
    if(getenv("SMGP_TELEMETRY") && racing) {
      unsigned speed=(g_ram[0x9108]<<8)|g_ram[0x9109];
      fprintf(stderr,"[SMGP INPUT] frame=%llu pad=%02X speed=%u offroad=%u skid=%u brakebit=%u throttlebit=%u\n",
        (unsigned long long)frame,g_ram[0xff04],speed,
        (g_ram[0x9208]<<8)|g_ram[0x9209],(g_ram[0x910e]<<8)|g_ram[0x910f],g_ram[0xff23],g_ram[0xff22]);
    }
    if(frame%300==0 && getenv("SMGP_DIAGNOSTICS")) {
      unsigned nz=0;for(unsigned i=0;i<65536;i++)nz+=g_machine.vdp.vram[i]!=0;
      fprintf(stderr,"[SMGP VIDEO] VRAM-nz=%u CRAM:",nz);
      for(int i=0;i<64;i++)fprintf(stderr," %03X",g_machine.vdp.cram[i]);
      fprintf(stderr,"\n");
    }
    if(frame%300==0)fprintf(stderr,"[SMGP] %s frame=%llu PC=%06X SR=%04X VDP=%02X,%02X,%02X,%02X,%02X\n",
       GAME_SHORT,(unsigned long long)frame,g_cpu.PC,g_cpu.SR,g_machine.vdp.reg[1],
       g_machine.vdp.reg[2],g_machine.vdp.reg[4],g_machine.vdp.reg[5],g_machine.vdp.reg[16]);
}
static int observe(uint32_t pc) {
    smgp_scene_observe(pc);
    smgp_sound_observe(pc);
#if GAME_smgp1
    /* Qualified World steering output, before native clamping/CCR updates.
       Only the wheel angle changes when a real non-neutral stick supplies L/R. */
    if(pc==0x61ca && racing && (g_ram[0x9146]||g_ram[0x9147]) &&
       !(g_ram[0xfca8]||g_ram[0xfca9]||g_ram[0xae38]||g_ram[0xae39])) {
      int wheel;
      if(smgp_analog_wheel(g_ram[0x910b],g_cpu.D[7]&255,g_ram[0xff04],&wheel))
        g_cpu.D[7]=(g_cpu.D[7]&0xffffff00u)|(unsigned)wheel;
    }
#else
    /* GP II digital steering converges at 13AEC before its native clamp.
       FC2D is the unsigned wheel; 13B04..13B18 derives signed FC2C with
       its original dead band. Do not change forced/automatic steering. */
    if(pc==0x13aec&&getenv("SMGP_ANALOG_TRACE"))fprintf(stderr,"[SMGP GP2 HOOK20] race=%d enabled=%s axis=%s pause=%04X end=%04X forced=%04X auto=%04X pad=%02X prev=%02X next=%02X\n",racing,getenv("SMGP_ANALOG")?getenv("SMGP_ANALOG"):"default",getenv("SMGP_QA_ANALOG_AXIS")?getenv("SMGP_QA_ANALOG_AXIS"):"physical",(g_ram[0xfe42]<<8)|g_ram[0xfe43],(g_ram[0xfe1c]<<8)|g_ram[0xfe1d],(g_ram[0xfe16]<<8)|g_ram[0xfe17],(g_ram[0xd9d8]<<8)|g_ram[0xd9d9],g_ram[0xff08],g_ram[0xfc2d],g_cpu.D[7]&255);
    if(pc==0x13aec && racing && !g_ram[0xfe42]&&!g_ram[0xfe43] &&
       !g_ram[0xfe1c]&&!g_ram[0xfe1d]&&!g_ram[0xfe16]&&!g_ram[0xfe17] &&
       !g_ram[0xd9d8]&&!g_ram[0xd9d9]){
      int wheel;
      if(smgp_analog_wheel_gp2(g_ram[0xfc2d],g_cpu.D[7]&255,g_ram[0xff08],&wheel))
        g_cpu.D[7]=(g_cpu.D[7]&0xffffff00u)|(unsigned)wheel;
    }
#endif
    return 0;
}
const GameSpec g_game_spec={
 .video=&video,
 .display_name=GAME_NAME,.short_name=GAME_SHORT,.expected_rom_crc32=GAME_CRC,
 .expected_rom_size=GAME_SIZE,.tier3_floor_default=1,
 .call_entry_point=entry,.call_vblank=vblank,.call_hblank=hblank,
 .load_settings=smgp_controls_load,
 .on_frame_pre=frame_pre,.on_frame_post=frame_post,.state_unavailable_reason=no_states
 ,.instruction_hook=observe
};
