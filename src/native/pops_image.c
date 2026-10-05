#include "runtime.h"
#include <png.h>
#include <string.h>

/* +0x39034 read callback, adapted to libpng's native destination pointer.
 * Preserve the observed strict '< size' test; reject wraparound explicitly
 * rather than letting malformed input escape the guest-memory bounds.
 */
static void read_png(png_structp png, png_bytep destination, png_size_t count)
{
    rp_context *c=png_get_io_ptr(png);
    uint32_t cursor=rp_u32(c,0x49C440),size=rp_u32(c,0x49CBC4);
    if (count>UINT32_MAX || cursor>=size || count>=size-cursor) {
        rp_w32(c,0x49C434,UINT32_MAX);
        png_error(png,"reconstructed POPS PNG read bounds");
    }
    uint32_t base=rp_u32(c,0x49C430);
    memcpy(destination,rp_memory(c,base+cursor,count),count);
    rp_w32(c,0x49C440,cursor+(uint32_t)count);
}

/* +0x39314 setup followed by the param_1==0 metadata path of +0x390A8.
 * The original imports libpng 1.2.37 functions from scePaf. The native adapter
 * uses the installed libpng API. Pixel decode and PSP libpng ABI are not modeled.
 */
uint32_t rp_pops_icon_info(rp_context *c, uint32_t data, uint32_t size)
{
    rp_function(c,0x39314,"pops.png_source_setup");
    rp_w32(c,0x49C430,data); rp_w32(c,0x49C438,size);
    rp_w32(c,0x49C43C,data); rp_w32(c,0x49CBC4,size);
    rp_function(c,0x390A8,"pops.png_info_metadata_path");
    rp_w32(c,0x49C434,0); rp_w32(c,0x49C440,0);
    png_structp png=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,NULL,NULL);
    if (!png) return UINT32_MAX;
    png_infop info=png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png,NULL,NULL); return UINT32_MAX; }
    if (setjmp(png_jmpbuf(png))) {
        rp_w32(c,0x49C434,UINT32_MAX);
        png_destroy_read_struct(&png,&info,NULL);
        rp_event(c,"host_adapter","libpng_metadata_error",0x390A8,size);
        return UINT32_MAX;
    }
    png_set_read_fn(png,c,read_png);
    png_read_info(png,info);
    uint32_t width=png_get_image_width(png,info);
    uint32_t height=png_get_image_height(png,info);
    rp_event(c,"milestone","png_icon_dimensions",width,height);
    /* Free host resources even though the original early-return path appears
     * to retain its libpng allocations. This is a documented host adaptation.
     */
    png_destroy_read_struct(&png,&info,NULL);
    return width;
}

/* +0x28790. The caller passes a sign-extended low byte, not the full width. */
void rp_pops_icon_tag(rp_context *c, uint32_t width)
{
    rp_function(c,0x28790,"pops.store_icon_width_tag");
    uint32_t value=(uint32_t)(int32_t)(int8_t)(uint8_t)width;
    rp_w32(c,0x26AC30,(value | value<<8 | value<<16 | value<<24)^UINT32_C(0xFA499C89));
}
