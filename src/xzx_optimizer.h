#ifndef XZX_OPTIMIZER_H
#define XZX_OPTIMIZER_H

#ifdef __cplusplus
extern "C" {
#endif

#define malloc(size) xzx_opt_malloc(size, __FILE__, __LINE__)
#define calloc(num, size) xzx_opt_calloc(num, size, __FILE__, __LINE__)
#define free(ptr) xzx_opt_free(ptr)

void *xzx_opt_malloc(size_t size, const char *file, unsigned int line);
void *xzx_opt_calloc(size_t num, size_t size, const char *file, unsigned int line);
void xzx_opt_free(void *ptr);

#ifdef __cplusplus
}
#endif

#ifdef __CUDACC__
#define cudaMalloc(devPtr, size) xzx_opt_cudaMalloc(devPtr, size, __FILE__, __LINE__)
#define cudaFree(devPtr) xzx_opt_cudaFree(devPtr)
#define cudaMallocHost(ptr, size) xzx_opt_cudaMallocHost(ptr, size, __FILE__, __LINE__)
#define cudaFreeHost(ptr) xzx_opt_cudaFreeHost(ptr)
#define cudaMemcpy(dst, src, count, kind) xzx_opt_cudaMemcpy(dst, src, count, kind)
#define cudaMemset(devPtr, value, count) xzx_opt_cudaMemset(devPtr, value, count)
#define cudaDeviceReset() xzx_opt_cudaDeviceReset()

cudaError_t xzx_opt_cudaMalloc(void **devPtr, size_t size, const char *file, unsigned int line);
cudaError_t xzx_opt_cudaFree(void *devPtr);
cudaError_t xzx_opt_cudaMallocHost(void **ptr, size_t size, const char *file, unsigned int line);
cudaError_t xzx_opt_cudaFreeHost(void *ptr);
cudaError_t xzx_opt_cudaMemcpy(void *dst, const void *src, size_t count, cudaMemcpyKind kind);
cudaError_t xzx_opt_cudaMemset(void *devPtr, int value, size_t count);
cudaError_t xzx_opt_cudaDeviceReset(void);

#endif

#endif
