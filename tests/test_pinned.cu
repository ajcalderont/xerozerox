#include "test_common.h"

int main()
{
    printf("test_pinned\n");

    float *h_data_A = NULL;
    float *h_data_B = NULL;
    float *d_data_A = NULL;
    float *d_data_B = NULL;
    int stack_buffer[32] = {0};

    cudaCheck(cudaMalloc((void**)&d_data_A, 512));
    cudaCheck(cudaMalloc((void**)&d_data_B, 512));
    cudaCheck(cudaMallocHost((void**)&h_data_A, 512));
    cudaCheck(cudaMallocHost((void**)&h_data_B, 512));

    for(int i = 0; i < 128; i++)
    {
        ((unsigned char*)h_data_A)[i] = (unsigned char)(i & 0xFF);
        ((unsigned char*)h_data_B)[i] = (unsigned char)((255 - i) & 0xFF);
    }

    cudaCheck(cudaMemcpy(d_data_A, h_data_A, 256, cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(d_data_B, h_data_B, 256, cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(d_data_B, stack_buffer, sizeof(stack_buffer), cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(stack_buffer, d_data_B, sizeof(stack_buffer), cudaMemcpyDeviceToHost));

    printf("pinned transfers done: %d\n", stack_buffer[0]);

    cudaCheck(cudaFree(d_data_A));
    cudaCheck(cudaFree(d_data_B));
    cudaCheck(cudaFreeHost(h_data_A));
    cudaCheck(cudaFreeHost(h_data_B));
    cudaCheck(cudaDeviceReset());
    return 0;
}
