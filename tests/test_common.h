#ifndef XZX_TEST_COMMON_H
#define XZX_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xerozerox.h"

#define cudaCheck(ans) { gpuAssert((ans), __FILE__, __LINE__); }

inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort = true)
{
    if(code != cudaSuccess)
    {
        fprintf(stderr, "CUDA error: %s - %s(%d)\n", cudaGetErrorString(code), file, line);
        if(abort)
            exit(code);
    }
}

#endif
