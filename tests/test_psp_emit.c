#include <assert.h>
#include <stdint.h>
#include <stdio.h>

uint32_t *repops_emit_t9_decrement(int32_t amount, uint32_t *output);

int main(void)
{
    const int32_t amounts[] = {INT32_MIN, -65536, -1, 0, 1, 32767, 32768,
                              65535, 65536, 65537, INT32_MAX};
    for (unsigned i = 0; i < sizeof(amounts) / sizeof(amounts[0]); ++i) {
        uint32_t buffer[3] = {0xAABBCCDD, 0x11223344, 0x99887766};
        uint32_t *result = repops_emit_t9_decrement(amounts[i], &buffer[1]);
        assert(buffer[0] == UINT32_C(0xAABBCCDD));
        assert(buffer[2] == UINT32_C(0x99887766));
        if (amounts[i] > 0) {
            assert(result == &buffer[2]);
            const uint32_t immediate = (uint32_t)(-(int64_t)amounts[i]) & 0xFFFF;
            assert(buffer[1] == (UINT32_C(0x27390000) | immediate));
        } else {
            assert(result == &buffer[1]);
            assert(buffer[1] == UINT32_C(0x11223344));
        }
    }
    puts("PSP emitter: 11 host contract cases passed.");
    return 0;
}
