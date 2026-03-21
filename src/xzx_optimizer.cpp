#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <unordered_set>
#include <unordered_map>
#include <cuda_runtime.h>

struct node
{
    char *position;
    size_t size;
    node *next;
};

struct table_row
{
    bool cyclic;
    unsigned long long device_file_hash;
    unsigned int device_line;
    unsigned long long host_file_hash;
    unsigned int host_line;
    size_t max_size;
    unsigned int count;
    unsigned int device_lock;
    unsigned int host_lock;
    char *position;
};

struct config_row
{
    bool cyclic;
    unsigned long long device_file_hash;
    unsigned int device_line;
    unsigned long long host_file_hash;
    unsigned int host_line;
    size_t max_size;
    unsigned int count;
};

struct profile_header
{
    uint32_t magic;
    uint32_t version;
    uint64_t requested_size;
    uint64_t pairs_count;
};

struct profile_row
{
    uint32_t cyclic;
    uint64_t device_file_hash;
    uint32_t device_line;
    uint64_t host_file_hash;
    uint32_t host_line;
    uint64_t max_size;
    uint32_t count;
};

static node *list_head = NULL;
static char *base = NULL;
static std::unordered_set<void*> *fallback_host_allocations = NULL;
static std::unordered_set<void*> *fallback_pinned_allocations = NULL;
static std::unordered_map<void*,size_t> *fallback_device_allocations = NULL;
static bool runtime_reset = false;

static table_row *pairs_table = NULL;
static unsigned long requested_size;
static unsigned long pairs_count;
static const uint32_t XZX_PROFILE_MAGIC = 0x585A5831U;
static const uint32_t XZX_PROFILE_VERSION = 1U;
static unsigned long optimized_host_allocations = 0;
static unsigned long fallback_host_allocations_count = 0;
static unsigned long optimized_pinned_allocations = 0;
static unsigned long fallback_pinned_allocations_count = 0;
static unsigned long optimized_device_allocations = 0;
static unsigned long fallback_device_allocations_count = 0;
static unsigned long optimized_memcpy_count = 0;
static unsigned long fallback_memcpy_count = 0;
static unsigned long optimized_memset_count = 0;
static unsigned long fallback_memset_count = 0;
static size_t optimized_memcpy_bytes = 0;
static size_t fallback_memcpy_bytes = 0;
static size_t optimized_memset_bytes = 0;
static size_t fallback_memset_bytes = 0;

#ifdef XZX_DEBUG
#define XZX_DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define XZX_DEBUG_LOG(...) ((void)0)
#endif

static void lib_init(void);
static void lib_fini(void);
static void xzx_write_optimization_report(void);

struct xzx_optimizer_lifecycle
{
    xzx_optimizer_lifecycle() { lib_init(); }
    ~xzx_optimizer_lifecycle() { lib_fini(); }
};

static xzx_optimizer_lifecycle xzx_optimizer_runtime;

void *allocate_host(unsigned int host_line, unsigned long long host_file_hash, size_t size);
void *allocate_device(unsigned int device_line, unsigned long long device_file_hash, size_t size);
void deallocate_host(void *pointer);
cudaError_t deallocate_device(void *pointer);
void *allocate(size_t size);
bool deallocate(void *pointer);
static bool xzx_pointer_in_pool(const void *pointer);
static bool xzx_can_use_optimized_memcpy(const void *dst, const void *src, cudaMemcpyKind kind);

static unsigned long long xzx_hash_string(const char *text)
{
    const unsigned long long fnv_offset = 1469598103934665603ULL;
    const unsigned long long fnv_prime = 1099511628211ULL;
    unsigned long long hash = fnv_offset;
    const char *start = text;
    const char *last_separator = NULL;
    const char *second_last_separator = NULL;

    if(text == NULL)
        return 0;

    while(*text != '\0')
    {
        if(*text == '/' || *text == '\\')
        {
            second_last_separator = last_separator;
            last_separator = text;
        }
        ++text;
    }

    if(second_last_separator != NULL)
        start = second_last_separator + 1;

    while(*start != '\0')
    {
        unsigned char current = (unsigned char)(*start);
        if(current >= 'A' && current <= 'Z')
            current = (unsigned char)(current - 'A' + 'a');
        if(current == '\\')
            current = '/';

        hash ^= current;
        hash *= fnv_prime;
        ++start;
    }
    return hash;
}

static cudaError_t xzx_report_cuda_error(cudaError_t error, const char *message)
{
    if(error != cudaSuccess)
        fprintf(stderr, "XeroZerox optimizer error: %s: %s\n", message, cudaGetErrorString(error));
    return error;
}

static void xzx_read_or_abort(void *data, size_t size, size_t count, FILE *file, const char *path)
{
    if(fread(data, size, count, file) != count)
    {
        fprintf(stderr, "XeroZerox optimizer error: unable to read %s\n", path);
        fclose(file);
        exit(1);
    }
}

static unsigned long xzx_u64_to_ulong_or_abort(uint64_t value, const char *label)
{
    unsigned long converted = (unsigned long)value;
    if((uint64_t)converted != value)
    {
        fprintf(stderr, "XeroZerox optimizer error: %s from profile is too large for this build\n", label);
        exit(1);
    }
    return converted;
}

static unsigned int xzx_u32_to_uint_or_abort(uint32_t value, const char *label)
{
    unsigned int converted = (unsigned int)value;
    if((uint32_t)converted != value)
    {
        fprintf(stderr, "XeroZerox optimizer error: %s from profile is too large for this build\n", label);
        exit(1);
    }
    return converted;
}

static size_t xzx_u64_to_size_t_or_abort(uint64_t value, const char *label)
{
    size_t converted = (size_t)value;
    if((uint64_t)converted != value)
    {
        fprintf(stderr, "XeroZerox optimizer error: %s from profile is too large for this build\n", label);
        exit(1);
    }
    return converted;
}

static void lib_init(void)
{
    fallback_host_allocations = new std::unordered_set<void*>();
    fallback_pinned_allocations = new std::unordered_set<void*>();
    fallback_device_allocations = new std::unordered_map<void*,size_t>();
    FILE *config = fopen("optimization_profile.cfg", "rb");
    if(config == NULL)
    {
        printf("ERROR: Optimization profile not found\n");
        exit(1);
    }
    else
    {
        profile_header header;
        xzx_read_or_abort(&header, sizeof(profile_header), 1, config, "optimization_profile.cfg");
        if(header.magic != XZX_PROFILE_MAGIC)
        {
            fprintf(stderr, "ERROR: Invalid optimization profile magic\n");
            fclose(config);
            exit(1);
        }
        if(header.version != XZX_PROFILE_VERSION)
        {
            fprintf(stderr, "ERROR: Unsupported optimization profile version %u\n", header.version);
            fclose(config);
            exit(1);
        }
        requested_size = xzx_u64_to_ulong_or_abort(header.requested_size, "requested size");
        pairs_count = xzx_u64_to_ulong_or_abort(header.pairs_count, "pair count");
        pairs_table = new table_row[pairs_count];
        for(int i = 0; i < pairs_count; i++)
        {
            profile_row row;
            xzx_read_or_abort(&row, sizeof(profile_row), 1, config, "optimization_profile.cfg");
            pairs_table[i].cyclic = row.cyclic != 0;
            pairs_table[i].device_file_hash = row.device_file_hash;
            pairs_table[i].device_line = xzx_u32_to_uint_or_abort(row.device_line, "device line");
            pairs_table[i].host_file_hash = row.host_file_hash;
            pairs_table[i].host_line = xzx_u32_to_uint_or_abort(row.host_line, "host line");
            pairs_table[i].max_size = xzx_u64_to_size_t_or_abort(row.max_size, "max size");
            pairs_table[i].count = xzx_u32_to_uint_or_abort(row.count, "pair count");
            pairs_table[i].device_lock = 0;
            pairs_table[i].host_lock = 0;
            pairs_table[i].position = NULL;
        }
        fclose(config);
        #ifdef UNIFIED_MEMORY
        cudaError_t error = cudaMallocManaged((void**)&base, requested_size);
        #else
        cudaError_t error = cudaSetDeviceFlags(cudaDeviceMapHost);
        if(error == cudaSuccess)
            error = cudaHostAlloc((void**)&base, requested_size, cudaHostAllocMapped);
        #endif
        if(error != cudaSuccess)
        {
            xzx_report_cuda_error(error, "unable to allocate the optimization pool");
            exit(1);
        }
    }
}

static void lib_fini(void)
{
    xzx_write_optimization_report();

    if(pairs_table != NULL) delete[] pairs_table;    
    if(fallback_host_allocations != NULL) delete fallback_host_allocations;
    if(fallback_pinned_allocations != NULL) delete fallback_pinned_allocations;
    if(fallback_device_allocations != NULL)
    {
        for(auto &entry : *fallback_device_allocations)
            xzx_report_cuda_error(cudaFree(entry.first), "unable to release fallback device allocation");
        delete fallback_device_allocations;
    }
    if(base != NULL && !runtime_reset)
    {
        #ifdef UNIFIED_MEMORY
        xzx_report_cuda_error(cudaFree(base), "unable to release unified optimization pool");
        #else
        xzx_report_cuda_error(cudaFreeHost(base), "unable to release mapped host optimization pool");
        #endif
    }
}

extern "C" {

void *xzx_opt_malloc(size_t size, const char *file, unsigned int line) 
{
    void *pointer = malloc(size);
    if(pointer != NULL)
    {
        fallback_host_allocations->insert(pointer);
        fallback_host_allocations_count++;
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer host alloc forced to runtime fallback, file=%s, line=%u, size=%zu, pointer=%p\n",
            file,
            line,
            size,
            pointer);
    }
    return pointer; 
}

void *xzx_opt_calloc(size_t num, size_t size, const char *file, unsigned int line) 
{
    void *pointer = NULL;
    if(num != 0 && size > (((size_t)-1) / num))
        return NULL;

    size_t total_size = num * size;
    pointer = calloc(num, size);
    if(pointer != NULL)
    {
        fallback_host_allocations->insert(pointer);
        fallback_host_allocations_count++;
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer calloc forced to runtime fallback, file=%s, line=%u, size=%zu, pointer=%p\n",
            file,
            line,
            total_size,
            pointer);
    }
    return pointer; 
}

void xzx_opt_free(void *ptr)
{
    deallocate_host(ptr);
}

}

cudaError_t xzx_opt_cudaMalloc(void **devPtr, size_t size, const char *file, unsigned int line)
{
    *devPtr = allocate_device(line, xzx_hash_string(file), size);
    if(*devPtr == NULL)
    {
        cudaError_t error = cudaMalloc(devPtr, size);
        if(error == cudaSuccess)
        {
            fallback_device_allocations->emplace(*devPtr, size);
            fallback_device_allocations_count++;
            XZX_DEBUG_LOG(
                "XeroZerox debug: optimizer device alloc fell back to CUDA runtime, file=%s, line=%u, size=%zu, pointer=%p\n",
                file,
                line,
                size,
                *devPtr);
        }
        return error;
    }
    optimized_device_allocations++;
    return cudaSuccess;
}

cudaError_t xzx_opt_cudaFree(void *devPtr)
{
    if(devPtr == NULL)
        return cudaSuccess;
    return deallocate_device(devPtr);
}

cudaError_t xzx_opt_cudaMallocHost(void **ptr, size_t size, const char *file, unsigned int line)
{
    cudaError_t error = cudaMallocHost(ptr, size);
    if(error == cudaSuccess)
    {
        fallback_pinned_allocations->insert(*ptr);
        fallback_pinned_allocations_count++;
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer pinned alloc forced to runtime fallback, file=%s, line=%u, size=%zu, pointer=%p\n",
            file,
            line,
            size,
            *ptr);
    }
    return error;
}

cudaError_t xzx_opt_cudaFreeHost(void *ptr)
{    
    if(ptr == NULL)
        return cudaSuccess;

    auto fallback = fallback_pinned_allocations->find(ptr);
    if(fallback != fallback_pinned_allocations->end())
    {
        fallback_pinned_allocations->erase(fallback);
        return cudaFreeHost(ptr);
    }
    deallocate_host(ptr);
    return cudaSuccess;
}

cudaError_t xzx_opt_cudaMemcpy(void* dst, const void* src, size_t count, cudaMemcpyKind kind)
{
    if(dst == src)
    	return cudaSuccess;

    if(kind == cudaMemcpyHostToHost || xzx_can_use_optimized_memcpy(dst, src, kind))
    {
        optimized_memcpy_count++;
        optimized_memcpy_bytes += count;
        memmove(dst, src, count);
        return cudaSuccess;
    }

    fallback_memcpy_count++;
    fallback_memcpy_bytes += count;
    XZX_DEBUG_LOG(
        "XeroZerox debug: optimizer memcpy fell back to CUDA runtime, kind=%d, bytes=%zu, dst=%p, src=%p\n",
        (int)kind,
        count,
        dst,
        src);
    return cudaMemcpy(dst, src, count, kind);
}

cudaError_t xzx_opt_cudaMemset(void *devPtr, int value, size_t count)
{
    if(!xzx_pointer_in_pool(devPtr))
    {
        fallback_memset_count++;
        fallback_memset_bytes += count;
        return cudaMemset(devPtr, value, count);
    }

    optimized_memset_count++;
    optimized_memset_bytes += count;
    memset(devPtr, value, count);
    return cudaSuccess;
}

#undef cudaDeviceReset
cudaError_t xzx_opt_cudaDeviceReset(void)
{
    xzx_write_optimization_report();
    runtime_reset = true;
    base = NULL;
    list_head = NULL;
    return cudaDeviceReset();
}

void *allocate_host(unsigned int host_line, unsigned long long host_file_hash, size_t size)
{
    void *pointer = NULL;
    int i;
    for(i = 0; i < pairs_count; i++)
    {
        if(pairs_table[i].host_line == host_line && pairs_table[i].host_file_hash == host_file_hash)
        {
            if(pairs_table[i].cyclic)
                break;
            else
                if(pairs_table[i].max_size == size && !pairs_table[i].host_lock)
                    break;
        }
    }
    if(i < pairs_count)
    {
        pairs_table[i].host_lock = 1;
        if(pairs_table[i].position == NULL)
            pairs_table[i].position = (char*)allocate(pairs_table[i].max_size);
        pointer = pairs_table[i].position;
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer host alloc matched row %d, file_hash=%llu, line=%u, size=%zu, position=%p, cyclic=%u\n",
            i,
            host_file_hash,
            host_line,
            size,
            pointer,
            pairs_table[i].cyclic);
    }
    else
    {
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer host alloc fallback, file_hash=%llu, line=%u, size=%zu\n",
            host_file_hash,
            host_line,
            size);
    }
    return pointer;
}

void *allocate_device(unsigned int device_line, unsigned long long device_file_hash, size_t size)
{
    void *pointer = NULL;
    int i;
    for(i = 0; i < pairs_count; i++)
    {
        if(pairs_table[i].device_line == device_line && pairs_table[i].device_file_hash == device_file_hash)
        {
            if(pairs_table[i].cyclic)
                break;
            else
                if(pairs_table[i].max_size == size && !pairs_table[i].device_lock)
                    break;
        }
    }
    if(i < pairs_count)
    {
        pairs_table[i].device_lock = 1;
        if(pairs_table[i].position == NULL)
            pairs_table[i].position = (char*)allocate(pairs_table[i].max_size);
        pointer = pairs_table[i].position;
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer device alloc matched row %d, file_hash=%llu, line=%u, size=%zu, position=%p, cyclic=%u\n",
            i,
            device_file_hash,
            device_line,
            size,
            pointer,
            pairs_table[i].cyclic);
    }
    else
    {
        XZX_DEBUG_LOG(
            "XeroZerox debug: optimizer device alloc fallback, file_hash=%llu, line=%u, size=%zu\n",
            device_file_hash,
            device_line,
            size);
    }
    return pointer;
}

void deallocate_host(void *pointer)
{
    if(pointer == NULL)
        return;

    bool found = false;    
    for(int i = 0; i < pairs_count; i++)
    {
        if(pairs_table[i].position == pointer)
        {
            pairs_table[i].host_lock = 0;
            if(!pairs_table[i].device_lock)
            {
                pairs_table[i].count--;
                if(pairs_table[i].count == 0){
                    if(!deallocate(pointer))
                    {
                        fprintf(stderr, "XeroZerox optimizer warning: tracked host pointer not found during deallocation\n");
                    }
                    pairs_table[i].position = NULL;
                }
            }
            found = true;
            break;
        }
    }
    if(!found)
    {   
        auto fallback = fallback_host_allocations->find(pointer);
        if(fallback != fallback_host_allocations->end())
        {
            fallback_host_allocations->erase(fallback);
            free(pointer);
        }
        else
        {
            fprintf(stderr, "XeroZerox optimizer warning: unknown host pointer %p passed to free\n", pointer);
        }
    }
}

cudaError_t deallocate_device(void *pointer)
{
    if(pointer == NULL)
        return cudaSuccess;

    for(int i = 0; i < pairs_count; i++)
    {
        if(pairs_table[i].position == pointer)
        {
            pairs_table[i].device_lock = 0;
            if(!pairs_table[i].host_lock)
            {
                pairs_table[i].count--;
                if(pairs_table[i].count <= 0){
                    if(!deallocate(pointer))
                    {
                        fprintf(stderr, "XeroZerox optimizer warning: tracked device pointer not found during deallocation\n");
                    }
                    pairs_table[i].position = NULL;
                }
            }
            return cudaSuccess;
        }
    }

    auto fallback = fallback_device_allocations->find(pointer);
    if(fallback != fallback_device_allocations->end())
    {
        fallback_device_allocations->erase(fallback);
        return cudaFree(pointer);
    }

    fprintf(stderr, "XeroZerox optimizer error: unknown device pointer %p passed to cudaFree\n", pointer);
    return cudaErrorInvalidDevicePointer;
}

void *allocate(size_t size)
{
    if(base == NULL)
        return NULL;

    char *new_position = base;
    node *current = list_head;
    node *previous = list_head;
    while(current != NULL)
    {
        size_t space = current->position - new_position;
        if(size <= space)
            break;
        new_position = current->position + current->size;
        previous = current;
        current = current->next;
    }
    if(size > requested_size || (unsigned long)(new_position - base) > (requested_size - size))
        return NULL;

    node *new_node = new node{new_position, size, current};
    if(current == list_head)
        list_head = new_node;
    else
        previous->next = new_node;
    return (void*)new_position;
}

bool deallocate(void *pointer)
{
    node *current = list_head;
    node *previous = list_head;
    while(current != NULL)
    {
        if(current->position == pointer)
            break;
        previous = current;
        current = current->next;
    }
    if(current == NULL)
        return false;
    if(current == list_head)
        list_head = current->next;
    else
        previous->next = current->next;
    delete(current);
    return true;
}

static bool xzx_pointer_in_pool(const void *pointer)
{
    if(base == NULL || pointer == NULL)
        return false;

    const char *address = static_cast<const char*>(pointer);
    return address >= base && address < (base + requested_size);
}

static bool xzx_can_use_optimized_memcpy(const void *dst, const void *src, cudaMemcpyKind kind)
{
    switch(kind)
    {
        case cudaMemcpyHostToDevice:
            return xzx_pointer_in_pool(dst);
        case cudaMemcpyDeviceToHost:
            return xzx_pointer_in_pool(src);
        case cudaMemcpyDeviceToDevice:
            return xzx_pointer_in_pool(dst) && xzx_pointer_in_pool(src);
        default:
            return false;
    }
}

static void xzx_write_optimization_report(void)
{
    FILE *report = fopen("xzx_optimization_report.txt", "w");
    if(report == NULL)
        return;

    fprintf(report, "Optimization pool size: %lu bytes\n", requested_size);
    fprintf(report, "Optimized host allocations: %lu\n", optimized_host_allocations);
    fprintf(report, "Fallback host allocations: %lu\n", fallback_host_allocations_count);
    fprintf(report, "Optimized pinned allocations: %lu\n", optimized_pinned_allocations);
    fprintf(report, "Fallback pinned allocations: %lu\n", fallback_pinned_allocations_count);
    fprintf(report, "Optimized device allocations: %lu\n", optimized_device_allocations);
    fprintf(report, "Fallback device allocations: %lu\n", fallback_device_allocations_count);
    fprintf(report, "Optimized memcpy calls: %lu\n", optimized_memcpy_count);
    fprintf(report, "Fallback memcpy calls: %lu\n", fallback_memcpy_count);
    fprintf(report, "Optimized memcpy bytes: %zu\n", optimized_memcpy_bytes);
    fprintf(report, "Fallback memcpy bytes: %zu\n", fallback_memcpy_bytes);
    fprintf(report, "Optimized memset calls: %lu\n", optimized_memset_count);
    fprintf(report, "Fallback memset calls: %lu\n", fallback_memset_count);
    fprintf(report, "Optimized memset bytes: %zu\n", optimized_memset_bytes);
    fprintf(report, "Fallback memset bytes: %zu\n", fallback_memset_bytes);
    fclose(report);
}
