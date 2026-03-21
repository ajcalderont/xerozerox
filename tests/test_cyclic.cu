#include "test_common.h"

__global__ void cudaKernel(float *d_data_A, float *d_data_B, float *d_data_C, float *d_data_D)
{
    int id = blockDim.x * blockIdx.x + threadIdx.x;
    d_data_C[id] = d_data_A[id] + d_data_B[id];
    d_data_D[id] = d_data_A[id] - d_data_B[id] + 1;
}

#define BLOCKS 1
#define THREADS 32
#define N (BLOCKS * THREADS)

int main()
{
    printf("test_cyclic\n");

    for(int x = 0; x < 6; x++)
    {
        float *h_data_A = (float*)malloc((N + x) * sizeof(float));
        float *h_data_B = (float*)malloc((N + 2 * x) * sizeof(float));
        float *h_data_C = (float*)malloc((N + 3 * x) * sizeof(float));
        float *h_data_D = (float*)malloc((N + 4 * x) * sizeof(float));
        float *d_data_A = NULL;
        float *d_data_B = NULL;
        float *d_data_C = NULL;
        float *d_data_D = NULL;

        for(int i = 0; i < N; i++)
        {
            h_data_A[i] = (float)(2 * i);
            h_data_B[i] = (float)i;
        }

        cudaCheck(cudaMalloc((void**)&d_data_A, (N + x) * sizeof(float)));
        cudaCheck(cudaMalloc((void**)&d_data_B, (N + 2 * x) * sizeof(float)));
        cudaCheck(cudaMalloc((void**)&d_data_C, (N + 3 * x) * sizeof(float)));
        cudaCheck(cudaMalloc((void**)&d_data_D, (N + 4 * x) * sizeof(float)));

        cudaCheck(cudaMemcpy(d_data_A, h_data_A, (N + x) * sizeof(float), cudaMemcpyHostToDevice));
        cudaCheck(cudaMemcpy(d_data_B, h_data_B, (N + 2 * x) * sizeof(float), cudaMemcpyHostToDevice));
        cudaKernel<<<BLOCKS, THREADS>>>(d_data_A, d_data_B, d_data_C, d_data_D);
        cudaCheck(cudaDeviceSynchronize());
        cudaCheck(cudaMemcpy(h_data_C, d_data_C, (N + 3 * x) * sizeof(float), cudaMemcpyDeviceToHost));
        cudaCheck(cudaMemcpy(h_data_D, d_data_D, (N + 4 * x) * sizeof(float), cudaMemcpyDeviceToHost));

        printf("cycle %d -> %.2f %.2f\n", x, h_data_C[0], h_data_D[0]);

        free(h_data_A);
        free(h_data_B);
        free(h_data_C);
        free(h_data_D);
        cudaCheck(cudaFree(d_data_A));
        cudaCheck(cudaFree(d_data_B));
        cudaCheck(cudaFree(d_data_C));
        cudaCheck(cudaFree(d_data_D));
    }

    cudaCheck(cudaDeviceReset());
    return 0;
}
