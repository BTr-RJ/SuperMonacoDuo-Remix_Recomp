"""Idempotent, project-local integration; never changes the Road Rash SDK."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def change(path,marker,old,new):
    text=path.read_text(encoding='utf-8')
    if marker in text:return
    if old not in text:raise ValueError(f'Runtime anchor changed: {path.name} {marker}')
    path.write_text(text.replace(old,new,1),encoding='utf-8')
main=ROOT/'.tools/engine/runner/main.c'
machine=ROOT/'.tools/engine/runner/video/genesis_machine.c'
machine_h=ROOT/'.tools/engine/runner/video/genesis_machine.h'
# Retire the preliminary live-register probe: queued IRQ delivery still leaves
# that register one row behind. The game's completed big-endian raster table
# is the authoritative current-row source, without changing CPU/IRQ timing.
text=machine_h.read_text(encoding='utf-8').replace('/* SMGP_VSCROLL_RASTER18: host renderer timing, not guest state. */\nvoid machine_set_live_raster_vscroll(int enabled);\n','')
machine_h.write_text(text,encoding='utf-8')
text=machine.read_text(encoding='utf-8').replace('/* SMGP_VSCROLL_RASTER18: GP II writes the current row from its HBlank capsule. */\nstatic int smgp_live_raster_vscroll;\nvoid machine_set_live_raster_vscroll(int enabled){smgp_live_raster_vscroll=enabled;}','')
text=text.replace('if(!smgp_live_raster_vscroll||line<65)memcpy(m->vdp.vsram,rr_latched_vsram,sizeof rr_latched_vsram); /* SMGP_VSCROLL_FETCH18 */','memcpy(m->vdp.vsram,rr_latched_vsram,sizeof rr_latched_vsram);')
machine.write_text(text,encoding='utf-8')
change(machine_h,'SMGP_VSCROLL_TABLE18','void machine_run_frame(GenesisScanlineSink sink, void *user);','/* SMGP_VSCROLL_TABLE18: host presentation source, excluded from guest state. */\nvoid machine_set_raster_vscroll_source(const uint8_t *big_endian_rows, int first, int last);\nvoid machine_run_frame(GenesisScanlineSink sink, void *user);')
change(machine,'SMGP_VSCROLL_TABLE18','static uint32_t rr_slice_cpu, rr_slice_phase;',
       'static uint32_t rr_slice_cpu, rr_slice_phase;\n/* SMGP_VSCROLL_TABLE18: completed guest raster, presentation-only. */\nstatic const uint8_t *smgp_raster_vscroll;\nstatic int smgp_raster_first,smgp_raster_last;\nvoid machine_set_raster_vscroll_source(const uint8_t *rows,int first,int last){smgp_raster_vscroll=rows;smgp_raster_first=first;smgp_raster_last=last;}')
change(machine,'SMGP_VSCROLL_FETCH_TABLE18','memcpy(m->vdp.vsram,rr_latched_vsram,sizeof rr_latched_vsram);',
       'memcpy(m->vdp.vsram,rr_latched_vsram,sizeof rr_latched_vsram);\n                /* SMGP_VSCROLL_FETCH_TABLE18: restore live guest registers below. */\n                if(smgp_raster_vscroll&&line>=smgp_raster_first&&line<smgp_raster_last)\n                    m->vdp.vsram[1]=(uint16_t)((smgp_raster_vscroll[line*2]<<8)|smgp_raster_vscroll[line*2+1]);')
change(main,'SMGP_OUTPUT_STRIDE','#define NATIVE_SCREEN_STRIDE 512',
       '#define NATIVE_SCREEN_STRIDE 1024 /* SMGP_OUTPUT_STRIDE: includes 32:9 */')
header=ROOT/'.tools/engine/runner/video/genesis_vdp.h'
change(header,'SMGP_OUTPUT_CAPACITY','#define GVDP_MAX_WIDTH      512',
       '#define GVDP_MAX_WIDTH      1024 /* SMGP_OUTPUT_CAPACITY */')
change(main,'SMGP_GPU_CAPABILITY','s_video_renderer = renderer;',
       's_video_renderer = renderer;\n    smgp_scene_renderer(renderer); /* SMGP_GPU_CAPABILITY */')
change(main,'SMGP_FRAME_COUNTER','#include "gpu_present.h"',
       'extern unsigned long g_snd_frame; /* SMGP_FRAME_COUNTER */\n#include "gpu_present.h"')
change(main,'SMGP_SCENE_INCLUDE','#include "gpu_present.h"',
       '#include "scene.h" /* SMGP_SCENE_INCLUDE */\n#include "gpu_present.h"')
change(main,'SMGP_GPU_COMPOSE','const uint32_t *present_src = rr_motion_present(s_framebuf,s_frame_stride,s_screen_width,s_screen_height,frame_num);',
       '/* SMGP_GPU_COMPOSE: hardware sprite layer before shared CRT output. */\n'
       '        const uint32_t *present_src = smgp_gpu_compose(renderer,s_framebuf,s_frame_stride,s_screen_width,s_screen_height,frame_num);')
change(main,'SMGP_CAPTURE_COMPOSE','int ok = png_write_argb(resolved, s_framebuf,',
       '/* SMGP_CAPTURE_COMPOSE: capture the actual hardware sprite composition. */\n'
       '    const uint32_t *picture=smgp_gpu_compose(s_video_renderer,s_framebuf,s_frame_stride,s_screen_width,s_screen_height,g_snd_frame);\n'
       '    int ok = png_write_argb(resolved, picture,')
change(main,'SMGP_GPU_SHUTDOWN','    rr_gpu_shutdown();',
       '    smgp_scene_shutdown(); /* SMGP_GPU_SHUTDOWN */\n    rr_gpu_shutdown();')
change(main,'SMGP_SCRIPT_INPUT_ONLY',
'''            case GB_START: if (mask & 0x80) return cc_true; break;
            default: break;
        }
    }''',
'''            case GB_START: if (mask & 0x80) return cc_true; break;
            default: break;
        }
        /* SMGP_SCRIPT_INPUT_ONLY: explicit automated QA must not consume
         * the owner's controller while another local game is running. */
        if(getenv("SMGP_QA_INPUT_ONLY"))return cc_false;
    }''')
vdp=ROOT/'.tools/engine/runner/video/genesis_vdp.c'
change(vdp,'SMGP_FINITE_BACKGROUND','        /* Sprite layer (indexed in output-column space). */',
'''        /* SMGP_FINITE_BACKGROUND: the racing B plane owns the panorama/road;
         * A owns the 256px cockpit/HUD and unused columns are pattern storage.
         * Fetch new B columns, preserving pixel size, never expose A storage. */
        if(rr_road_profile==10 && s_visual_pass && w==256 && (x<0 || x>=w)) {
            a_op=0; a_hi=0;
            int source_x=x-128+((128-hs_b)&511);
            int source_y=(line+vs_b)&(ht*8-1);
            /* Road strips are finite; the panoramic upper section repeats. */
            if(source_y>=112 && (source_x<0 || source_x>=wt*8)) {
                int edge=source_x<0?(x&7):wt*8-8+(x&7);
                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,edge,line+vs_b,im2,&b_idx,&b_op,&b_hi);
            }
        }
        /* Sprite layer (indexed in output-column space). */''')
print('Super Monaco runtime integration ready')
change(ROOT/'.tools/engine/runner/gamepad.c','SMGP_RESERVED_PAD_CHORDS',
       'uint32_t gamepad_overlay_mask(void)\n{',
       'uint32_t gamepad_overlay_mask(void)\n{\n    if(getenv("RR_CONTROLS_PROFILE"))return 0; /* SMGP_RESERVED_PAD_CHORDS: mapped gear buttons must not open SDK menus. */')
change(ROOT/'.tools/engine/runner/input_map.c','SMGP_MAPPED_CONTROLS',
       '    const PlayerInput *pi = &g_input_map.p[player];\n    uint16_t m = 0;\n    if (pi->device & INPUT_DEV_KEYBOARD)',
       '    /* SMGP_MAPPED_CONTROLS: resolve actions before the guest pad, never implicit vertical steering. */\n'
       '    extern unsigned short smgp_controls_mask(int player);\n'
       '    return smgp_controls_mask(player);\n'
       '    const PlayerInput *pi = &g_input_map.p[player];\n    uint16_t m = 0;\n    if (pi->device & INPUT_DEV_KEYBOARD)')
change(ROOT/'.tools/engine/runner/input_script.c','SMGP_LONG_REPLAY',
       '#define MAX_OPS 4096','#define MAX_OPS 32768 /* SMGP_LONG_REPLAY: input-only long-race QA */')

# SMGP's original Z80 driver polls YM timer A for its music sequencer.
# The inherited CPU-visible status model implemented only timer B, leaving
# all title/menu music stalled even though PSG and DAC effects made noise.
bus_header=ROOT/'.tools/engine/runner/video/genesis_bus.h'
bus=ROOT/'.tools/engine/runner/video/genesis_bus.c'
change(bus_header,'SMGP_YM_TIMER_A_STATE','    uint8_t  ym_timer_b;',
       '    uint16_t ym_timer_a; /* SMGP_YM_TIMER_A_STATE: registers 24/25 */\n'
       '    uint8_t ym_timer_a_running;\n    uint64_t ym_timer_a_deadline,ym_timer_a_period;\n'
       '    uint8_t  ym_timer_b;')
change(bus,'SMGP_YM_TIMER_A_UPDATE',
'''    if (!b->ym_timer_b_running || b->ym_timer_b_period == 0 ||
        now < b->ym_timer_b_deadline)
        return;''',
'''    /* SMGP_YM_TIMER_A_UPDATE: 144 YM clocks per timer-A count;
     * timer B divides that clock by 16. CPU status follows scheduler time,
     * independently of the asynchronous audio renderer. */
    if(b->ym_timer_a_running && b->ym_timer_a_period && now>=b->ym_timer_a_deadline){
        if(b->ym_mode&4u)b->ym_status|=1u;
        uint64_t elapsed=now-b->ym_timer_a_deadline;
        b->ym_timer_a_deadline+=(elapsed/b->ym_timer_a_period+1u)*b->ym_timer_a_period;
    }
    if (!b->ym_timer_b_running || b->ym_timer_b_period == 0 ||
        now < b->ym_timer_b_deadline)
        return;''')
change(bus,'SMGP_YM_TIMER_A_REGISTERS','    if (reg == 0x26u) {',
'''    if(reg==0x24u || reg==0x25u){ /* SMGP_YM_TIMER_A_REGISTERS */
        if(reg==0x24u)b->ym_timer_a=(uint16_t)((b->ym_timer_a&3u)|((uint16_t)val<<2));
        else b->ym_timer_a=(uint16_t)((b->ym_timer_a&0x3fcu)|(val&3u));
        b->ym_timer_a_period=(uint64_t)(1024u-b->ym_timer_a)*144ull*7ull;
    } else if (reg == 0x26u) {''')
change(bus,'SMGP_YM_TIMER_A_CONTROL','        b->ym_mode = val;',
'''        /* SMGP_YM_TIMER_A_CONTROL: load is edge-sensitive. Clearing a
         * status flag with the load bit still set must not restart timers. */
        if(val&1u){
            if(!b->ym_timer_a_running){
                if(!b->ym_timer_a_period)b->ym_timer_a_period=(uint64_t)(1024u-b->ym_timer_a)*144ull*7ull;
                b->ym_timer_a_deadline=now+b->ym_timer_a_period;
            }
            b->ym_timer_a_running=1;
        }else b->ym_timer_a_running=0;
        b->ym_mode = val;''')
change(bus,'SMGP_TIMER_B_LOAD_EDGE','            b->ym_timer_b_running = 1;\n            b->ym_timer_b_deadline = now + b->ym_timer_b_period;',
'''            if(!b->ym_timer_b_running)b->ym_timer_b_deadline=now+b->ym_timer_b_period; /* SMGP_TIMER_B_LOAD_EDGE */
            b->ym_timer_b_running = 1;''')

# Timer A fields are pointer-free machine state. The raw machine image already
# includes them; size-checked loads reject the old layout. Keep drift guards.
rb=ROOT/'.tools/engine/runner/rb_state.c'
change(rb,'SMGP_TIMER_BUS_LAYOUT','sizeof(GenesisBus) == 24664',
       'sizeof(GenesisBus) == 24688 /* SMGP_TIMER_BUS_LAYOUT */')
change(rb,'SMGP_TIMER_MACHINE_LAYOUT','sizeof(GenesisMachine) == 90576',
       'sizeof(GenesisMachine) == 90600 /* SMGP_TIMER_MACHINE_LAYOUT */')
cosim=ROOT/'.tools/engine/runner/cosim_state.c'
change(cosim,'SMGP_TIMER_STATE_HASH','    h = cosim_fnv_u8(h, b->version);',
'''    h = cosim_fnv_u8(h, b->version);
    /* SMGP_TIMER_STATE_HASH: compare timer progress as well as guest RAM. */
    h = cosim_fnv_u16(h, b->ym_timer_a);
    h = cosim_fnv_u8(h, b->ym_timer_a_running);
    h = cosim_fnv_bytes(h, &b->ym_timer_a_deadline, sizeof b->ym_timer_a_deadline);
    h = cosim_fnv_bytes(h, &b->ym_timer_a_period, sizeof b->ym_timer_a_period);
    h = cosim_fnv_u8(h, b->ym_timer_b);
    h = cosim_fnv_u8(h, b->ym_timer_b_running);
    h = cosim_fnv_bytes(h, &b->ym_timer_b_deadline, sizeof b->ym_timer_b_deadline);
    h = cosim_fnv_bytes(h, &b->ym_timer_b_period, sizeof b->ym_timer_b_period);
    h = cosim_fnv_u8(h, b->ym_mode);
    h = cosim_fnv_u8(h, b->ym_status);''')

# Export the actual opaque foreground pixels from the visual pass. A HUD
# rectangle copied from a finished framebuffer also contains the old road.
change(header,'SMGP_FOREGROUND_ALPHA',
       'const uint8_t *native_index,*native_owner,*index,*owner;',
       'const uint8_t *native_index,*native_owner,*index,*owner;\n    const uint8_t *foreground; /* SMGP_FOREGROUND_ALPHA: 255 means transparent */')
change(vdp,'SMGP_FOREGROUND_BUFFER','static GVDPVisualAudit s_visual_audit;',
       'static uint8_t s_foreground[GVDP_MAX_WIDTH]; /* SMGP_FOREGROUND_BUFFER */\nstatic GVDPVisualAudit s_visual_audit;')
change(vdp,'SMGP_FOREGROUND_PIXELS','        if(s_visual_audit)s_visible_owner[xo]=',
       '        if(s_visual_audit)s_foreground[xo]=a_op?a_idx:((b_op&&b_hi)?b_idx:255); /* SMGP_FOREGROUND_PIXELS */\n'
       '        if(s_visual_audit)s_visible_owner[xo]=')
change(vdp,'SMGP_FOREGROUND_AUDIT','native,owners,out,s_visible_owner};',
       'native,owners,out,s_visible_owner,s_foreground}; /* SMGP_FOREGROUND_AUDIT */')
change(vdp,'SMGP_FOREGROUND_CLEAR',
       '    if(s_visual_audit)memset(s_visible_owner,0,sizeof s_visible_owner);',
       '    if(s_visual_audit){memset(s_visible_owner,0,sizeof s_visible_owner);memset(s_foreground,255,sizeof s_foreground);} /* SMGP_FOREGROUND_CLEAR: display-off early return must not reuse the prior raster row */')

# Retire the old modulo fetch, including the mirror's storage rows. The racing
# panorama occupies rows 64..111; rows 0..63 contain mirror/sky construction.
change(vdp,'SMGP_FINITE_BACKGROUND_V2',
'''            /* Road strips are finite; the panoramic upper section repeats. */
            if(source_y>=112 && (source_x<0 || source_x>=wt*8)) {
                int edge=source_x<0?(x&7):wt*8-8+(x&7);
                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,edge,line+vs_b,im2,&b_idx,&b_op,&b_hi);
            }''',
'''            /* SMGP_FINITE_BACKGROUND_V2: mirror rows must never repeat in
             * the racing sky. A road strip has a single finite horizontal
             * extent; outside it continue its left ground material/phase. */
            if(line>=65 && source_y<64) {
                b_idx=backdrop;b_op=1;b_hi=0;
            } else if(source_y>=112 && (source_x<0 || source_x>=wt*8)) {
                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,source_x&7,line+vs_b,im2,&b_idx,&b_op,&b_hi);
            }''')
change(vdp,'SMGP_PANORAMA_SAFE_VIEW',
'''            } else if(source_y>=112 && (source_x<0 || source_x>=wt*8)) {''',
'''            } else if(source_y<112) {
                /* SMGP_PANORAMA_SAFE_VIEW: the unused panorama columns can
                 * reference tiles repurposed by the live game. Repeat only
                 * the authored 256-column panorama viewport, at 1:1 scale. */
                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,(x&255)-hs_b,line+vs_b,im2,&b_idx,&b_op,&b_hi);
            } else if(source_x<0 || source_x>=wt*8) {''')
change(vdp,'SMGP_WORLD_GROUND_MATERIAL',
'''                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,source_x&7,line+vs_b,im2,&b_idx,&b_op,&b_hi);''',
'''                fetch_plane_pixel(v,&cache_b,base_b,wt,ht,source_x&7,line+vs_b,im2,&b_idx,&b_op,&b_hi);
                /* SMGP_WORLD_GROUND_MATERIAL: edge cells can contain curb/
                 * asphalt. World uses material nibble B in the row palette;
                 * repeating an edge cell created horizontal white/blue bars. */
                if(base_b==0xC000){b_idx=(uint8_t)(((cache_b.attr>>9)&0x30)|11);b_op=1;b_hi=0;}''')

# The native atlas proves rows 40..63 belong to the mountain panorama, not
# the mirror. Cropping at 64 cut its tops into floating rectangles. All 512
# panorama columns are authored; repeating a 256px viewport creates a seam.
change(vdp,'SMGP_PANORAMA_ATLAS_V3',
'''            if(line>=65 && source_y<64) {''',
'''            if(line>=65 && source_y<0) { /* SMGP_PANORAMA_ATLAS_V3: no invented horizon crop */''')
change(vdp,'SMGP_PANORAMA_512_V3',
'''fetch_plane_pixel(v,&cache_b,base_b,wt,ht,(x&255)-hs_b,line+vs_b,im2,&b_idx,&b_op,&b_hi);''',
'''fetch_plane_pixel(v,&cache_b,base_b,wt,ht,x-hs_b,line+vs_b,im2,&b_idx,&b_op,&b_hi); /* SMGP_PANORAMA_512_V3 */''')

# Split audible channels while clocking and computing ALL native voices exactly
# once. Canonical output/LPF/save/hash are unchanged; only delivery is selected.
opnh=ROOT/'.tools/engine/runner/external/ymfm/src/ymfm_opn.h'
opnc=ROOT/'.tools/engine/runner/external/ymfm/src/ymfm_opn.cpp'
text=opnh.read_text(encoding='utf-8');start=text.index('class ym2612\n')
if 'SMGP_OUTPUT_SPLIT17' not in text[start:]:
    before=text[:start];tail=text[start:]
    tail=tail.replace('void generate(output_data *output, uint32_t numsamples = 1);','void generate(output_data *output, uint32_t numsamples = 1);\n\tvoid generate_split(output_data *output, output_data *audible, uint32_t mask); // SMGP_OUTPUT_SPLIT17',1)
    opnh.write_text(before+tail,encoding='utf-8')
change(opnc,'SMGP_OUTPUT_SPLIT17',
'''void ym2612::generate(output_data *output, uint32_t numsamples)
{
\tfor (uint32_t samp = 0; samp < numsamples; samp++, output++)
\t{''',
'''void ym2612::generate(output_data *output, uint32_t numsamples)
{
\tfor (uint32_t samp = 0; samp < numsamples; samp++, output++) {
\t\toutput_data audible; generate_split(output, &audible, 63);
\t}
}
// SMGP_OUTPUT_SPLIT17: presentation selection, never a synthesis clock mask.
void ym2612::generate_split(output_data *output, output_data *audible, uint32_t mask)
{
\t{''')
change(opnc,'SMGP_OUTPUT_CLEAR17','''\t\t// sum individual channels to apply DAC discontinuity on each
\t\toutput->clear();''','''\t\t// sum individual channels to apply DAC discontinuity on each
\t\taudible->clear(); // SMGP_OUTPUT_CLEAR17
\t\toutput->clear();''')
change(opnc,'SMGP_OUTPUT_FM17','''\t\t\toutput->data[1] += dac_discontinuity(temp.data[1]);''','''\t\t\toutput->data[1] += dac_discontinuity(temp.data[1]);
\t\t\tif (mask & (1u << chan)) { // SMGP_OUTPUT_FM17
\t\t\t\taudible->data[0] += dac_discontinuity(temp.data[0]);
\t\t\t\taudible->data[1] += dac_discontinuity(temp.data[1]);
\t\t\t}''')
change(opnc,'SMGP_OUTPUT_DAC17','''\t\t\toutput->data[1] += m_fm.regs().ch_output_1(0x102) ? dacval : dac_discontinuity(0);''','''\t\t\toutput->data[1] += m_fm.regs().ch_output_1(0x102) ? dacval : dac_discontinuity(0);
\t\t\tif (mask & 32) { // SMGP_OUTPUT_DAC17
\t\t\t\taudible->data[0] += m_fm.regs().ch_output_0(0x102) ? dacval : dac_discontinuity(0);
\t\t\t\taudible->data[1] += m_fm.regs().ch_output_1(0x102) ? dacval : dac_discontinuity(0);
\t\t\t}''')
change(opnc,'SMGP_OUTPUT_SCALE17','''\t\toutput->data[1] = (output->data[1] * 128) * 64 / (6 * 65);''','''\t\toutput->data[1] = (output->data[1] * 128) * 64 / (6 * 65);
\t\taudible->data[0] = (audible->data[0] * 128) * 64 / (6 * 65); // SMGP_OUTPUT_SCALE17
\t\taudible->data[1] = (audible->data[1] * 128) * 64 / (6 * 65);''')
fm=ROOT/'.tools/engine/runner/audio/ym2612_ymfm.cpp'
change(fm,'SMGP_AUDIBLE_STATE17','int32_t s_fm_lpf_prev[2] = {0, 0};','''unsigned s_output_mask = 63; // SMGP_AUDIBLE_STATE17: deliberately absent from canonical snapshots/hash
int32_t s_audible_prev[2]{},s_audible_out[2]{};
int32_t s_fm_lpf_prev[2] = {0, 0};''')
change(fm,'SMGP_AUDIBLE_GENERATE17','''    s_chip->generate(&out, 1);''','''    ymfm::ym2612::output_data audible; // SMGP_AUDIBLE_GENERATE17
    s_chip->generate_split(&out, &audible, s_output_mask);''')
change(fm,'SMGP_AUDIBLE_DELIVERY17','''    s_scratch_write++;
}''','''    // SMGP_AUDIBLE_DELIVERY17: keep canonical LPF above advancing unchanged.
    for (int ch=0;ch<2;ch++) {
        int32_t in=audible.data[ch]*YM_GAIN_NUM/YM_GAIN_DEN;
        int32_t value=clamp16(int32_t(((int64_t(in)+s_audible_prev[ch])*FM_LPF_SAMPLE_MAGIC+int64_t(s_audible_out[ch])*FM_LPF_OUTPUT_MAGIC)/65536));
        s_audible_prev[ch]=in;s_audible_out[ch]=value;
        if(s_output_mask!=63)s_scratch[s_scratch_write*2+ch]=int16_t(value);
    }
    s_scratch_write++;
}''')
change(fm,'SMGP_AUDIBLE_INIT17','''    s_inited = 1;''','''    s_audible_prev[0]=s_audible_prev[1]=s_audible_out[0]=s_audible_out[1]=0; // SMGP_AUDIBLE_INIT17
    s_output_mask=63;
    s_inited = 1;''')
change(fm,'SMGP_AUDIBLE_API17','''extern "C" void ym2612_advance(uint32_t cycles_master) {''','''extern "C" void ym2612_set_output_mask(unsigned mask) { s_output_mask=mask&63; } // SMGP_AUDIBLE_API17
extern "C" void ym2612_advance(uint32_t cycles_master) {''')
change(ROOT/'.tools/engine/runner/audio/ym2612.h','SMGP_AUDIBLE_API17','#endif','void ym2612_set_output_mask(unsigned mask); /* SMGP_AUDIBLE_API17: host output only */\n\n#endif')
