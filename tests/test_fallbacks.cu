#include "test_common.h"

int main()
{
    printf("test_fallbacks\n");

    float *h_data = (float*)malloc(128 * sizeof(float));
    float *d_data = NULL;
    const char *force_extra = getenv("XZX_FORCE_FALLBACKS");

    for(int i = 0; i < 128; i++)
        h_data[i] = (float)i;

    cudaCheck(cudaMalloc((void**)&d_data, 128 * sizeof(float)));
    cudaCheck(cudaMemcpy(d_data, h_data, 128 * sizeof(float), cudaMemcpyHostToDevice));
    cudaCheck(cudaMemcpy(h_data, d_data, 128 * sizeof(float), cudaMemcpyDeviceToHost));

    if(force_extra != NULL && force_extra[0] != '\0' && force_extra[0] != '0')
    {
        float extra_host[32] = {0};
        float *extra_device = NULL;

        for(int i = 0; i < 32; i++)
            extra_host[i] = (float)(100 + i);

        cudaCheck(cudaMalloc((void**)&extra_device, 32 * sizeof(float)));
        cudaCheck(cudaMemcpy(extra_device, extra_host, 32 * sizeof(float), cudaMemcpyHostToDevice));
        cudaCheck(cudaMemcpy(extra_host, extra_device, 32 * sizeof(float), cudaMemcpyDeviceToHost));
        cudaCheck(cudaFree(extra_device));

        printf("forced fallback path enabled: %.2f\n", extra_host[0]);
    }

    printf("fallback baseline value: %.2f\n", h_data[0]);

    free(h_data);
    cudaCheck(cudaFree(d_data));
    cudaCheck(cudaDeviceReset());
    return 0;
}
