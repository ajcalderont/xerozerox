#include "test_common.h"

int main()
{
    printf("test_offsets\n");

    unsigned char *h_data_A = (unsigned char*)malloc(512);
    unsigned char *h_data_B = (unsigned char*)malloc(512);
    unsigned char *h_data_C = NULL;
    unsigned char *d_data_A = NULL;
    unsigned char *d_data_B = NULL;
    unsigned char *d_data_C = NULL;
    unsigned char stack_buffer[256] = {0};

    cudaCheck(cudaMalloc((void**)&d_data_A, 512));
    cudaCheck(cudaMalloc((void**)&d_data_B, 512));
    cudaCheck(cudaMalloc((void**)&d_data_C, 512));
    cudaCheck(cudaMallocHost((void**)&h_data_C, 512));

    for(int i = 0; i < 512; i++)
    {
        h_data_A[i] = (unsigned char)(i & 0xFF);
        h_data_B[i] = (unsigned char)((255 - i) & 0xFF);
        h_data_C[i] = (unsigned char)((i * 3) & 0xFF);
    }

    cudaCheck(cudaMemcpy(d_data_A + 64, h_data_A + 32, 192, cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(d_data_B + 80, h_data_B + 40, 176, cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(d_data_C + 48, h_data_C + 24, 160, cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(d_data_C + 208, stack_buffer + 32, 96, cudaMemcpyHostToDevice));

    memset(h_data_A, 0, 512);
    memset(h_data_B, 0, 512);
    memset(stack_buffer, 0, sizeof(stack_buffer));

    cudaCheck(cudaMemcpy(h_data_A + 16, d_data_A + 64, 192, cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(h_data_B + 24, d_data_B + 80, 176, cudaMemcpyDeviceToHost));
    cudaCheck(cudaMemcpy(stack_buffer + 64, d_data_C + 48, 160, cudaMemcpyDeviceToHost));

    printf("offset checks: %u %u %u %u %u %u\n",
        h_data_A[16], h_data_A[16 + 191],
        h_data_B[24], h_data_B[24 + 175],
        stack_buffer[64], stack_buffer[64 + 159]);

    free(h_data_A);
    free(h_data_B);
    cudaCheck(cudaFreeHost(h_data_C));
    cudaCheck(cudaFree(d_data_A));
    cudaCheck(cudaFree(d_data_B));
    cudaCheck(cudaFree(d_data_C));
    cudaCheck(cudaDeviceReset());
    return 0;
}
