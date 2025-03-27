#include <nmmintrin.h>

int main(void)
{
    unsigned int result = ~(unsigned int)0;

    result = _mm_crc32_u8(result, 0x0);

    return result;
}

