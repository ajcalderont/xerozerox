#ifndef XZX_ANALYZER_H
#define XZX_ANALYZER_H

#ifdef __cplusplus
extern "C" {
#endif

#define malloc(size) xzx_alz_malloc(size, __FILE__, __LINE__)
#define calloc(num, size) xzx_alz_calloc(num, size, __FILE__, __LINE__)
#define free(ptr) xzx_alz_free(ptr, __FILE__, __LINE__)

void *xzx_alz_malloc(size_t size, const char *file, unsigned int line);
void *xzx_alz_calloc(size_t num, size_t size, const char *file, unsigned int line);
void xzx_alz_free(void *ptr, const char *file, unsigned int line);

#ifdef __cplusplus
}
#endif

#ifdef __CUDACC__
#define cudaMalloc(devPtr, size) xzx_alz_cudaMalloc(devPtr, size, __FILE__, __LINE__)
#define cudaFree(devPtr) xzx_alz_cudaFree(devPtr, __FILE__, __LINE__)
#define cudaMallocHost(ptr, size) xzx_alz_cudaMallocHost(ptr, size, __FILE__, __LINE__)
#define cudaFreeHost(ptr) xzx_alz_cudaFreeHost(ptr, __FILE__, __LINE__)
#define cudaMemcpy(dst, src, count, kind) xzx_alz_cudaMemcpy(dst, src, count, kind, __FILE__, __LINE__)

cudaError_t xzx_alz_cudaMalloc(void **devPtr, size_t size, const char *file, unsigned int line);
cudaError_t xzx_alz_cudaFree(void *devPtr, const char *file, unsigned int line);
cudaError_t xzx_alz_cudaMallocHost(void **ptr, size_t size, const char *file, unsigned int line);
cudaError_t xzx_alz_cudaFreeHost(void *ptr, const char *file, unsigned int line);
cudaError_t xzx_alz_cudaMemcpy(void *dst, const void *src, size_t count, cudaMemcpyKind kind, const char *file, unsigned int line);

#endif

#endif
