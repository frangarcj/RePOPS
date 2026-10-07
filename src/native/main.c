#include "runtime.h"
#include <stdlib.h>
#include <string.h>
#include "pops_ge_backend.h"

static void load_data(rp_context *c, const char *image, const char *imports)
{
    FILE *f=fopen(image,"rb");
    if (!f) rp_block(c,"native_image_open_failed",0);
    size_t n=fread(c->regions[0].bytes,1,0x4AE730,f);
    int extra=fgetc(f); fclose(f);
    if (n!=0x4AE730 || extra!=EOF) rp_block(c,"native_image_size_mismatch",0);
    f=fopen(imports,"r");
    if (!f) rp_block(c,"import_manifest_open_failed",0);
    char line[256];
    while (fgets(line,sizeof(line),f)) {
        unsigned address,nid;
        char library[96];
        if (sscanf(line,"%x %95s %x",&address,library,&nid)!=3) {
            fclose(f); rp_block(c,"invalid_import_manifest",0);
        }
        if (address<0x3CF9C || address>0x3D48C || address%4 || c->imports>=159) {
            fclose(f); rp_block(c,"invalid_import_slot",address);
        }
        ++c->imports;
        /* Binding metadata for the reconstructed self-patcher. These guest
         * instruction words are not executed by this native-C executable.
         */
        rp_w32(c,address,0x03E00008);
        rp_w32(c,address+4,(c->imports<<6)|0xC);
    }
    fclose(f);
    if (c->imports!=159) rp_block(c,"incomplete_import_manifest",c->imports);
    rp_event(c,"image_loaded","pops_relocated_data",0,(uint32_t)n);
    rp_event(c,"metadata","import_slots_prepared_not_all_services_implemented",0,c->imports);
}

int main(int argc, char **argv)
{
    int skip_ui = 0;
    if (argc > 1 && strcmp(argv[argc - 1], "--diagnostic-skip-ui") == 0) {
        skip_ui = 1;
        --argc;
    }
    if (argc<4 || argc>5) {
        fprintf(stderr,"usage: %s <checked-image.bin> <imports.tsv> <new-trace.jsonl> [game.pbp] [--diagnostic-skip-ui]\n",argv[0]);
        return 64;
    }
    rp_context *c=calloc(1,sizeof(*c));
    if (!c) return 70;
    c->trace=fopen(argv[3],"wx");
    if (!c->trace) { perror("Refusing trace path"); free(c); return 73; }
    c->regions[0]=(rp_region){0,0x800000,NULL};
    c->regions[1]=(rp_region){0x1000000,0x800000,NULL};
    c->regions[2]=(rp_region){0x08000000,0x2000000,NULL};
    /* Shadow EDRAM for reconstructed state writes; this is not a renderer. */
    c->regions[3]=(rp_region){0x04000000,0x400000,NULL};
    for (unsigned i=0;i<RP_REGION_COUNT;++i) {
        c->regions[i].bytes=calloc(1,c->regions[i].size);
        if (!c->regions[i].bytes) { fputs("Guest memory allocation failed\n",stderr); return 70; }
    }
    c->gp=0x10000; c->heap_next=0x1000000; c->next_id=1;
    c->diagnostic_skip_ui=skip_ui;
    {
        const char *compact = getenv("REPOPS_TRACE_COMPACT");
        c->trace_compact = compact && compact[0] == '1';
    }
    c->disc_path=argc==5?argv[4]:NULL;
    rp_event(c,"metadata","compact_trace_enabled",0,(uint32_t)c->trace_compact);
    if (rp_ge_live_open(c) < 0) return 70;
    if (setjmp(c->stop)==0) {
        load_data(c,argv[1],argv[2]);
        rp_pops_module_start(c);
        rp_event(c,"milestone","module_start_returned_native_C",0x16000,0);
        rp_pops_main_thread(c);
        rp_block(c,"unexpected_main_thread_return",0x16080);
    }
    const char *edram_dump = getenv("REPOPS_GE_DUMP_EDRAM");
    if (c->ge_backend_active && edram_dump && *edram_dump)
        (void)rp_ge_live_dump_edram(c, edram_dump);
    fprintf(c->trace,"{\"kind\":\"result\",\"status\":\"%s\",\"address\":%u,"
            "\"native_function_entries\":%u,\"host_service_calls\":%u,"
            "\"execution\":\"native_C_POPS_with_Unicorn_generated_cache\",\"game_executed\":false,"
            "\"generated_executor\":\"%s\",\"generated_instruction_hook_events\":%llu,\"compiled_block_transfers\":%u,"
            "\"psx_pc\":%u,\"generated_pc\":%u,"
            "\"diagnostic_ui_bypassed\":%s,\"ge_backend\":\"%s\",\"ge_submissions\":%u,\"ge_completed_syncs\":%u}\n",
            c->stop_kind,c->stop_address,c->functions,c->services,
            c->generated_executor ? c->generated_executor : "not_started",
            (unsigned long long)c->generated_instructions,c->compiled_transfers,
            rp_u32(c,c->gp+0x1A0),c->run_pc,
            c->diagnostic_skip_ui ? "true" : "false",
            c->ge_backend_active ? "PPSSPP_software" : "capture_only",
            c->ge_backend_submissions,c->ge_backend_completed);
    printf("Native C stopped: %s at 0x%08X; %u function entries, %u host calls\n",
            c->stop_kind,c->stop_address,c->functions,c->services);
    if (c->disc) fclose(c->disc);
    rp_unicorn_close(c);
    fclose(c->trace);
    for (unsigned i=0;i<RP_REGION_COUNT;++i)
        if (i < 2 || !c->ge_backend_active) free(c->regions[i].bytes);
    rp_ge_live_close(c);
    free(c);
    return 78; /* Explicitly incomplete execution; never report game success. */
}
