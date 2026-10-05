#include "runtime.h"
#include "../bootstrap.h"
#include <string.h>

static int32_t sdk(void *ctx, uint32_t v) { rp_context *c=ctx; ++c->services; rp_event(c,"host_adapter","set_sdk",0,v); return 0; }
static int32_t compiler(void *ctx, uint32_t v) { rp_context *c=ctx; ++c->services; rp_event(c,"host_adapter","set_compiler",0,v); return 0; }
static int32_t hold(void *ctx, int32_t v) { rp_context *c=ctx; ++c->services; rp_event(c,"headless_adapter","display_hold",0,(uint32_t)v); return 0; }
static int32_t create_thread(void *ctx, const char *name, uint32_t entry, uint32_t priority,
                             uint32_t stack, uint32_t attributes, uint32_t option)
{
    rp_context *c=ctx; (void)name; (void)priority; (void)stack; (void)attributes; (void)option;
    ++c->services; c->thread_entry=entry;
    rp_event(c,"host_adapter","queue_native_thread",entry,1); return 1;
}
static int32_t start_thread(void *ctx, int32_t id, uint32_t n, uint32_t p)
{
    rp_context *c=ctx; (void)n; (void)p; ++c->services;
    if (id != 1 || c->thread_entry != 0x16080) rp_block(c,"unknown_native_thread",c->thread_entry);
    rp_event(c,"host_adapter","schedule_native_thread",c->thread_entry,1); return 0;
}
static void exit_vsh(void *ctx, int32_t error)
{
    rp_context *c=ctx; rp_event(c,"firmware_exit","exit_vsh",c->last_function,(uint32_t)error);
    rp_block(c,"firmware_requested_exit",c->last_function);
}

int32_t rp_pops_module_start(rp_context *c)
{
    rp_function(c,0x16000,"pops.module_start");
    const repops_host host={c,sdk,compiler,hold,create_thread,start_thread,exit_vsh};
    return repops_module_start_model(&host,0);
}

/* Reconstructed from +0x24A08. The native runtime initially binds imports to
 * synthetic syscall numbers. This keeps the observed stub-copy/patch memory
 * effects while C calls are dispatched natively, never interpreted as MIPS.
 */
uint32_t rp_pops_patch_syscalls(rp_context *c)
{
    rp_function(c,0x24A08,"pops.patch_syscall_stubs");
    uint32_t end=0x3D494, first=0;
    for (uint32_t tries=0;;++tries) {
        if (end<8 || tries>0x10000) rp_block(c,"syscall_tail_not_found",end);
        uint32_t a=rp_module_u32(c,end-8),b=rp_module_u32(c,end-4);
        if ((a==0x03E00008 && (b&0xFC00003F)==0xC) || ((a&0xFC000000)==0x08000000 && b==0)) break;
        end-=4;
    }
    while ((rp_module_u32(c,first+4)&0xFC00003F)!=0xC) {
        first+=4;
        if (first>=end) rp_block(c,"syscall_head_not_found",first);
    }
    const uint32_t size=end-first;
    if (!size) return 0;
    const uint32_t copy=rp_alloc(c,size);
    memcpy(rp_memory(c,copy,size),rp_module_memory(c,first,size),size);
    rp_w32(c,0x14D088,copy-first); rp_w32(c,0x14D084,first);
    uint32_t patched=0, immediate=0x34020000;
    for (uint32_t p=first;p<end;p+=8,immediate+=8) {
        if ((rp_u32(c,p+4)&0xFC00003F)==0xC) {
            rp_w32(c,p,0x08009266); rp_w32(c,p+4,immediate); ++patched;
        }
    }
    rp_event(c,"milestone","native_syscall_bridge_prepared",first,patched);
    return 0;
}

/* +0x1B004 through header loading and disc-ID validation. Do not follow Ghidra's spurious
 * fallthrough after ExitVSH into the next routine. This first-disc path has
 * no outstanding asynchronous read; a later busy case remains a real blocker.
 */
static uint32_t select_disc(rp_context *c)
{
    rp_function(c,0x1B004,"pops.select_disc_prefix");
    rp_function(c,0x287C4,"pops.get_selected_disc");
    uint32_t selected=rp_u32(c,0x163240);
    uint8_t active=*(uint8_t *)rp_memory(c,c->gp+0x3E45,1);
    if (active==selected) return 0;
    uint8_t count=*(uint8_t *)rp_memory(c,c->gp+0x3E44,1);
    if (selected>=count) exit_vsh(c,(int32_t)UINT32_C(0x80000004));
    rp_function(c,0xDEFC,"pops.wait_pending_cd_io");
    if (rp_u32(c,c->gp+0x3E14)!=UINT32_MAX)
        rp_block(c,"cd_io_wait_not_implemented",0xDEFC);
    for (uint32_t i=0;i<17;++i) rp_w32(c,c->gp+0x3D08+i*16,0x80000000);
    rp_w32(c,c->gp+0x3E1C,UINT32_MAX);
    uint32_t offset=rp_u32(c,c->gp+0x3E38)+rp_u32(c,c->gp+0x3E24+selected*4);
    if (rp_provider_read_at(c,0x09E80000,offset,0x400)!=0x400) return UINT32_MAX;
    if (memcmp(rp_memory(c,0x09E80000,12),"PSISOIMG0000",12)!=0) return UINT32_MAX;
    rp_event(c,"milestone","selected_disc_header_loaded",0x09E80000,selected);
    uint32_t result = rp_provider_plain_disc_header(c, 0x09E80000, selected);
    result = rp_pops_remember_provider_result(c, result);
    if (result & UINT32_C(0x80000000)) return UINT32_MAX;
    const uint32_t id = rp_pops_normalize_disc_id(c, 0x09E80400, 0x20);
    if (rp_pops_check_disc_id(c, id) != 0) return UINT32_MAX;
    rp_event(c, "milestone", "disc_identifier_validated", id, rp_u32(c, id));
    result = rp_pops_apply_game_config(c, id, rp_u32(c, 0x09E80420),
                                      rp_u32(c, 0x09E80424), 0x09E80428);
    if (result & UINT32_C(0x80000000)) return UINT32_MAX;
    return rp_pops_finalize_disc_selection(c, selected, offset);
}

/* Reviewed prefix of +0x1B2F0 and +0x1B56C. The linked-list initialization is
 * native C; the filesystem provider is an explicit host adapter for now.
 * Unreconstructed callees terminate instead of silently returning success.
 */
uint32_t rp_pops_disc_init(rp_context *c)
{
    rp_function(c,0x1B2F0,"pops.disc_init_prefix");
    const uint32_t head=c->gp+0x3D00;
    rp_w32(c,head,head); rp_w32(c,head+4,head); rp_w32(c,c->gp+0x3E10,head);
    rp_w32(c,0x49CBD0,0); rp_w8(c,c->gp+0x3E44,1);
    rp_w8(c,c->gp+0x3E45,255); rp_w32(c,c->gp+0x3E14,0xFFFFFFFF);
    for (uint32_t i=0;i<17;++i) {
        uint32_t node=head+i*16, tail=rp_u32(c,head+4);
        rp_w32(c,node+12,0x09492600+i*0x9300);
        if (i) {
            uint32_t next=rp_u32(c,tail);
            rp_w32(c,node+4,tail); rp_w32(c,node,next);
            rp_w32(c,next+4,node); rp_w32(c,tail,node);
        }
    }
    rp_function(c,0x1B56C,"pops.open_disc_handle");
    if (c->disc) { fclose(c->disc); c->disc=NULL; }
    int32_t fd=rp_provider_open_image(c,c->gp+0x3E38);
    rp_w32(c,c->gp+0x3E40,(uint32_t)fd);
    if (fd<0) return (uint32_t)fd;
    rp_function(c,0x36CF4,"pops.pbp_metadata");
    rp_w32(c,0x4514F4,0);
    uint32_t header=rp_alloc(c,40);
    if (rp_provider_read_at(c,header,0,40)!=40 || rp_u32(c,header)!=0x50425000) return 0xFFFFFFFF;
    uint32_t begin=rp_u32(c,header+12),end=rp_u32(c,header+16);
    if (end<begin) return 0xFFFFFFFF;
    uint32_t length=end-begin,buffer=rp_alloc(c,length);
    rp_w32(c,0x4514F4,length); rp_w32(c,0x450EE8,buffer);
    if (rp_provider_read_at(c,buffer,begin,length)!=(int32_t)length) return 0xFFFFFFFF;
    rp_event(c,"milestone","pbp_metadata_read",buffer,length);
    uint32_t width=rp_pops_icon_info(c,buffer,length);
    rp_pops_icon_tag(c,width);
    uint32_t disc_header=rp_alloc(c,0x400);
    if (rp_provider_read_at(c,disc_header,rp_u32(c,c->gp+0x3E38),0x400)!=0x400)
        return UINT32_C(0x80000004);
    const void *magic=rp_memory(c,disc_header,16);
    if (memcmp(magic,"PSTITLEIMG000000",16)==0)
        rp_block(c,"multidisc_path_not_reconstructed",0x1B2F0);
    if (memcmp(magic,"PSISOIMG0000",12)!=0) return UINT32_MAX;
    rp_event(c,"milestone","single_disc_PSISOIMG_header",disc_header,0x400);
    uint32_t selected_result=select_disc(c);
    if (selected_result&UINT32_C(0x80000000)) return selected_result;
    rp_pops_finish_disc_boot(c);
    return 0;
}

void rp_pops_main_thread(rp_context *c)
{
    rp_function(c,0x16080,"pops.popsmain_prefix");
    char *build=rp_memory(c,c->gp+0x3FC0,64);
    snprintf(build,64,"branches/pops-660/pops/build(r%d)",0x321F);
    rp_w8(c,c->gp+0x3F00,255);
    /* Headless single-threaded environment: store callback handles; there are
     * no synthetic power/hotplug events. Device behavior is not reproduced.
     */
    ++c->services; rp_event(c,"headless_adapter","register_power_callback",0x34388,++c->next_id);
    ++c->services; rp_w32(c,0x09BF0000,++c->next_id);
    rp_event(c,"headless_adapter","register_storage_callback",0x34468,c->next_id);
    rp_pops_patch_syscalls(c);
    uint32_t result=rp_pops_disc_init(c);
    if (result&0x80000000) exit_vsh(c,(int32_t)result);
    if (!c->diagnostic_skip_ui) rp_block(c,"function_not_reconstructed",0x28DF8);
    /* Explicit investigative entry, not a claimed implementation of the UI.
     * No savedata/config produced by that UI is fabricated here.
     */
    rp_event(c,"diagnostic_bypass","startup_UI_not_reconstructed",0x28DF8,1);
    result=select_disc(c);
    if (result != 0) exit_vsh(c,(int32_t)UINT32_C(0x80000004));
    result=rp_pops_mc_init(c);
    if (result&0x80000000) exit_vsh(c,(int32_t)UINT32_C(0x80000004));
    result=rp_pops_controller_init(c);
    if (result&0x80000000) exit_vsh(c,(int32_t)UINT32_C(0x80000004));
    rp_pops_initialize_core(c);
    rp_block(c,"function_not_reconstructed",0x1C964);
}
