#include "../src/native/runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    rp_context *c=calloc(1,sizeof(*c));
    assert(c);
    c->regions[0]=(rp_region){0,0x20000,calloc(1,0x20000)};
    assert(c->regions[0].bytes);
    c->trace=tmpfile(); assert(c->trace);
    const uint8_t marker[]={0x40,0x19,0x0C,0};
    memcpy(rp_module_memory(c,0x13E24,4),marker,4);
    assert(rp_module_u32(c,0x13E24)==0xC1940);
    assert(rp_u32(c,0x13E24)==0);
    rp_w32(c,0x13E24,0x12345678);
    assert(rp_u32(c,0x13E24)==0x12345678);
    assert(rp_module_u32(c,0x13E24)==0xC1940);
    rp_w8(c,0x13FFF,0xA5);
    assert(c->scratchpad[0x3FFF]==0xA5);
    if (setjmp(c->stop)==0) {
        rp_memory(c,0x13FFF,2);
        assert(!"expected bounds stop");
    }
    assert(strcmp(c->stop_kind,"scratchpad_access_overrun")==0);
    c->regions[2]=(rp_region){0x09F40000,0x1000,calloc(1,0x1000)};
    assert(c->regions[2].bytes);
    rp_w32(c,0x49F40294,0x12345678);
    assert(rp_u32(c,0x09F40294)==0x12345678);
    assert(rp_memory(c,0x49F40294,4)==rp_memory(c,0x09F40294,4));
    free(c->regions[2].bytes);
    fclose(c->trace); free(c->regions[0].bytes); free(c);
    puts("Native memory: scratchpad/module separation and bounds passed.");
    return 0;
}
