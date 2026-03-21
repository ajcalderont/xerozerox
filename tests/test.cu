#include "test_common.h"

#define BLOCKS 1
#define THREADS 32
#define N (BLOCKS * THREADS)

__global__ void cudaKernel(float* d_data_A, float* d_data_B, float* d_data_C, float* d_data_D)
{
	int id = blockDim.x * blockIdx.x + threadIdx.x;
	d_data_C[id] = d_data_A[id] + d_data_B[id];
	d_data_D[id] = d_data_A[id] - d_data_B[id] + 1;
}

int main()
{
	printf("Test program\n");

	float *h_data_A, *h_data_B, *h_data_C, *h_data_D, *h_data_E, *h_data_F;
	float *d_data_A, *d_data_B, *d_data_C, *d_data_D, *d_data_E, *d_data_F;
	unsigned char *h_data_G, *h_data_H, *h_data_I;
	unsigned char *d_data_G, *d_data_H, *d_data_I;
	float *extra_memory;
	
	for(int x = 0; x < 10; x++)
	{
		h_data_A = (float*) malloc((N + x*1)*sizeof(float));
		h_data_B = (float*) malloc((N + x*2)*sizeof(float));
		extra_memory = (float*) malloc(4096);
		h_data_C = (float*) malloc((N + x*3)*sizeof(float));
		h_data_D = (float*) malloc((N + x*4)*sizeof(float));

		printf("Host pointer A: %p \n", h_data_A);
		printf("Host pointer B: %p \n", h_data_B);
		printf("Host pointer extra: %p \n", extra_memory);
		printf("Host pointer C: %p \n", h_data_C);
		printf("Host pointer D: %p \n", h_data_D);

		cudaCheck(cudaMalloc((void**)&d_data_A, (N + x*1)*sizeof(float)));
		cudaCheck(cudaMalloc((void**)&d_data_B, (N + x*2)*sizeof(float)));
		cudaCheck(cudaMalloc((void**)&d_data_C, (N + x*3)*sizeof(float)));
		cudaCheck(cudaMalloc((void**)&d_data_D, (N + x*4)*sizeof(float)));

		printf("Device pointer A: %p \n", d_data_A);
		printf("Device pointer B: %p \n", d_data_B);
		printf("Device pointer C: %p \n", d_data_C);
		printf("Device pointer D: %p \n", d_data_D);
		
		for(int i = 0; i < N; i++)
		{
			h_data_A[i] = 2*i;
			h_data_B[i] = i;
		}
		
		cudaCheck(cudaMemcpy(d_data_A, h_data_A, (N + x*1)*sizeof(float), cudaMemcpyHostToDevice));
		cudaCheck(cudaMemcpy(d_data_B, h_data_B, (N + x*2)*sizeof(float), cudaMemcpyHostToDevice));
		cudaKernel<<<BLOCKS, THREADS>>>(d_data_A, d_data_B, d_data_C, d_data_D);
		cudaCheck(cudaDeviceSynchronize());
		
		cudaCheck(cudaMemcpy(h_data_C, d_data_C, (N + x*3)*sizeof(float), cudaMemcpyDeviceToHost));
		cudaCheck(cudaMemcpy(h_data_D, d_data_D, (N + x*4)*sizeof(float), cudaMemcpyDeviceToHost));
		
		for(int i = 0; i < N; i++)
			printf("%02d: [%05.2f][%05.2f][%05.2f][%05.2f]\n", i, h_data_A[i], h_data_B[i], h_data_C[i], h_data_D[i]);
		
		free(h_data_A);
		free(h_data_B);
		free(extra_memory);
		free(h_data_C);
		free(h_data_D);
		cudaCheck(cudaFree(d_data_A));
		cudaCheck(cudaFree(d_data_B));
		cudaCheck(cudaFree(d_data_C));
		cudaCheck(cudaFree(d_data_D));
	}
	
	cudaCheck(cudaMalloc((void**)&d_data_E, 500));
	cudaCheck(cudaMalloc((void**)&d_data_F, 500));
	cudaCheck(cudaMallocHost((void**)&h_data_E, 500));
	cudaCheck(cudaMallocHost((void**)&h_data_F, 500));
	printf("Device pointer E: %p \n", d_data_E);
	printf("Device pointer F: %p \n", d_data_F);
	printf("Host pointer E: %p \n", h_data_E);
	printf("Host pointer F: %p \n", h_data_F);
	cudaCheck(cudaMemcpy(d_data_E, (char*)h_data_E, 250, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_data_F, (char*)h_data_F, 250, cudaMemcpyHostToDevice));
	int non_dynamic[20];
	cudaCheck(cudaMemcpy(d_data_F, &non_dynamic, 20 * sizeof(int), cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(&non_dynamic, d_data_F, 20 * sizeof(int), cudaMemcpyDeviceToHost));
	cudaCheck(cudaFree(d_data_E));
	cudaCheck(cudaFree(d_data_F));
	cudaCheck(cudaFreeHost(h_data_E));
	cudaCheck(cudaFreeHost(h_data_F));

	h_data_G = (unsigned char*) malloc(512);
	h_data_H = (unsigned char*) malloc(512);
	cudaCheck(cudaMalloc((void**)&d_data_G, 512));
	cudaCheck(cudaMalloc((void**)&d_data_H, 512));
	for(int i = 0; i < 512; i++)
	{
		h_data_G[i] = (unsigned char)(i & 0xFF);
		h_data_H[i] = (unsigned char)((255 - i) & 0xFF);
	}
	cudaCheck(cudaMemcpy(d_data_G + 64, h_data_G + 32, 192, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(d_data_H + 80, h_data_H + 40, 176, cudaMemcpyHostToDevice));
	memset(h_data_G, 0, 512);
	memset(h_data_H, 0, 512);
	cudaCheck(cudaMemcpy(h_data_G + 16, d_data_G + 64, 192, cudaMemcpyDeviceToHost));
	cudaCheck(cudaMemcpy(h_data_H + 24, d_data_H + 80, 176, cudaMemcpyDeviceToHost));
	printf("Offset pageable copy check: [%u][%u][%u][%u]\n", h_data_G[16], h_data_G[16 + 191], h_data_H[24], h_data_H[24 + 175]);
	free(h_data_G);
	free(h_data_H);
	cudaCheck(cudaFree(d_data_G));
	cudaCheck(cudaFree(d_data_H));

	cudaCheck(cudaMallocHost((void**)&h_data_I, 512));
	cudaCheck(cudaMalloc((void**)&d_data_I, 512));
	for(int i = 0; i < 512; i++)
	{
		h_data_I[i] = (unsigned char)((255 - i) & 0xFF);
	}
	cudaCheck(cudaMemcpy(d_data_I + 48, h_data_I + 24, 160, cudaMemcpyHostToDevice));
	unsigned char non_dynamic_offset[256] = {0};
	cudaCheck(cudaMemcpy(d_data_I + 208, non_dynamic_offset + 32, 96, cudaMemcpyHostToDevice));
	cudaCheck(cudaMemcpy(non_dynamic_offset + 64, d_data_I + 48, 160, cudaMemcpyDeviceToHost));
	printf("Offset pinned/non-dynamic copy check: [%u][%u]\n", non_dynamic_offset[64], non_dynamic_offset[64 + 159]);
	cudaCheck(cudaFree(d_data_I));
	cudaCheck(cudaFreeHost(h_data_I));
	cudaDeviceReset();
	return 0;
}
