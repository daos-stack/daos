#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
	const size_t buffer_size = 1 << 14;
	uint64_t* buffer;

	printf("Allocating buffer...\n");
	buffer = (uint64_t*)malloc(buffer_size * sizeof(uint64_t));

	printf("Malloc header value: 0x%x\n", *(size_t*)(buffer-1));

	printf("Poisoning buffer...\n");
	for (uint64_t idx = 0; idx < buffer_size+2; ++idx) {
		uint64_t* poisoned_buffer = buffer - 2;
		poisoned_buffer[idx] = -1+idx;
	}

	printf("Malloc header value: 0x%x\n", *(size_t*)(buffer-1));

	printf("Deallocating buffer...\n");
	free(buffer);

	return 0;
}
