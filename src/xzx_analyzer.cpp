#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <cuda_runtime.h>
#include <stdint.h>
#include <unordered_map>
#include <vector>
#include "allocator_status.h"

enum operation_type {pageable_allocation, pageable_free, device_allocation, device_free, pinned_allocation, pinned_free, unknown_allocation, unknown_free};

struct csv_entry
{
    operation_type type;
    const char *function;
    size_t size;
    const char *file;
    unsigned int line;
    void *pointer;
};

struct allocation_data
{
    const char *filename;
    unsigned long long file_hash;
    unsigned int line;
    size_t size;
};

struct allocation_pair
{
    void *pointer;
    bool cyclic;
    unsigned long long device_file_hash;
    unsigned int device_line;
    unsigned long long host_file_hash;
    unsigned int host_line;
    size_t max_size;
    unsigned int count;
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

static std::unordered_map<const void*,allocation_data> *host_allocations;
static std::unordered_map<const void*,allocation_data> *device_allocations;
static std::unordered_map<const void*,size_t> *nondynamic_allocations;
static std::vector<csv_entry> *csv_entries;
static std::vector<allocation_pair> *allocation_pairs;
static unsigned long h2d_count = 0;
static unsigned long d2h_count = 0;
static unsigned long current_requested_memory = 0;
static unsigned long max_requested_memory = 0;
static const uint32_t XZX_PROFILE_MAGIC = 0x585A5831U;
static const uint32_t XZX_PROFILE_VERSION = 1U;

static unsigned long long xzx_hash_string(const char *text);
static bool xzx_pointer_in_allocation(const void *pointer, const void *base, size_t size);

#ifdef XZX_DEBUG
#define XZX_DEBUG_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define XZX_DEBUG_LOG(...) ((void)0)
#endif

static bool xzx_same_site(unsigned long long lhs_hash, unsigned int lhs_line, unsigned long long rhs_hash, unsigned int rhs_line)
{
    return lhs_hash == rhs_hash && lhs_line == rhs_line;
}

static ptrdiff_t xzx_pointer_offset_in_allocation(const void *pointer, const void *base, size_t size)
{
    if(!xzx_pointer_in_allocation(pointer, base, size))
        return -1;

    const char *address = static_cast<const char*>(pointer);
    const char *start = static_cast<const char*>(base);
    return address - start;
}

static bool xzx_pointer_in_allocation(const void *pointer, const void *base, size_t size)
{
    const char *address = static_cast<const char*>(pointer);
    const char *start = static_cast<const char*>(base);
    return address >= start && address < (start + size);
}

static std::unordered_map<const void*,allocation_data>::iterator xzx_find_host_allocation(const void *pointer)
{
    auto exact = host_allocations->find(pointer);
    if(exact != host_allocations->end())
        return exact;

    for(auto it = host_allocations->begin(); it != host_allocations->end(); ++it)
    {
        if(xzx_pointer_in_allocation(pointer, it->first, it->second.size))
            return it;
    }

    return host_allocations->end();
}

static std::unordered_map<const void*,allocation_data>::iterator xzx_find_device_allocation(const void *pointer)
{
    auto exact = device_allocations->find(pointer);
    if(exact != device_allocations->end())
        return exact;

    for(auto it = device_allocations->begin(); it != device_allocations->end(); ++it)
    {
        if(xzx_pointer_in_allocation(pointer, it->first, it->second.size))
            return it;
    }

    return device_allocations->end();
}

static void xzx_mark_host_allocation_as_pageable(const void *host_pointer, const allocation_data &host_allocation)
{
    for(auto it = csv_entries->begin(); it != csv_entries->end(); ++it)
    {
        if(it->type == unknown_allocation
        && it->pointer == host_pointer
        && it->line == host_allocation.line
        && xzx_hash_string(it->file) == host_allocation.file_hash)
        {
            it->type = pageable_allocation;
            break;
        }
    }
}

static allocation_pair *xzx_select_allocation_pair_candidate(
    const void *device_pointer,
    const allocation_data &device_allocation,
    const allocation_data &host_allocation)
{
    allocation_pair *pointer_match = NULL;
    allocation_pair *same_host_match = NULL;
    allocation_pair *unassigned_match = NULL;

    for(auto it = allocation_pairs->begin(); it != allocation_pairs->end(); ++it)
    {
        if(!xzx_same_site(it->device_file_hash, it->device_line, device_allocation.file_hash, device_allocation.line))
            continue;

        if(it->pointer == device_pointer)
            pointer_match = &(*it);

        if(same_host_match == NULL
        && xzx_same_site(it->host_file_hash, it->host_line, host_allocation.file_hash, host_allocation.line))
            same_host_match = &(*it);

        if(unassigned_match == NULL && it->host_line == 0 && it->host_file_hash == 0)
            unassigned_match = &(*it);
    }

    if(pointer_match != NULL)
        return pointer_match;

    if(same_host_match != NULL)
        return same_host_match;

    return unassigned_match;
}

static void xzx_update_allocation_pair(
    const void *device_pointer,
    const allocation_data &device_allocation,
    const allocation_data &host_allocation,
    ptrdiff_t device_offset,
    ptrdiff_t host_offset,
    cudaMemcpyKind kind,
    const char *file,
    unsigned int line)
{
    allocation_pair *best_candidate = xzx_select_allocation_pair_candidate(device_pointer, device_allocation, host_allocation);

    if(best_candidate != NULL)
    {
        best_candidate->host_file_hash = host_allocation.file_hash;
        best_candidate->host_line = host_allocation.line;
        XZX_DEBUG_LOG(
            "XeroZerox debug: matched %s at %s(%u): device %s(%u) + %td -> host %s(%u) + %td, size=%zu\n",
            kind == cudaMemcpyHostToDevice ? "H2D memcpy" : "D2H memcpy",
            file,
            line,
            device_allocation.filename,
            device_allocation.line,
            device_offset,
            host_allocation.filename,
            host_allocation.line,
            host_offset,
            device_allocation.size);
        return;
    }

    XZX_DEBUG_LOG(
        "XeroZerox debug: unable to match %s at %s(%u) for device %s(%u) + %td and host %s(%u) + %td\n",
        kind == cudaMemcpyHostToDevice ? "H2D memcpy" : "D2H memcpy",
        file,
        line,
        device_allocation.filename,
        device_allocation.line,
        device_offset,
        host_allocation.filename,
        host_allocation.line,
        host_offset);
}

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

static void lib_init(void);
static void lib_fini(void);

struct xzx_analyzer_lifecycle
{
    xzx_analyzer_lifecycle() { lib_init(); }
    ~xzx_analyzer_lifecycle() { lib_fini(); }
};

static xzx_analyzer_lifecycle xzx_analyzer_runtime;

static FILE *xzx_open_or_abort(const char *path, const char *mode)
{
    FILE *file = fopen(path, mode);
    if(file == NULL)
    {
        fprintf(stderr, "XeroZerox analyzer error: unable to open %s\n", path);
        exit(1);
    }
    return file;
}

static void xzx_write_or_abort(const void *data, size_t size, size_t count, FILE *file, const char *path)
{
    if(fwrite(data, size, count, file) != count)
    {
        fprintf(stderr, "XeroZerox analyzer error: unable to write %s\n", path);
        fclose(file);
        exit(1);
    }
}

static uint64_t xzx_to_u64_or_abort(size_t value, const char *label)
{
    uint64_t converted = (uint64_t)value;
    if((size_t)converted != value)
    {
        fprintf(stderr, "XeroZerox analyzer error: %s does not fit into the profile format\n", label);
        exit(1);
    }
    return converted;
}

static void lib_init(void)
{
    host_allocations = new std::unordered_map<const void*,allocation_data>();
    device_allocations = new std::unordered_map<const void*,allocation_data>();
    nondynamic_allocations = new std::unordered_map<const void*,size_t>();
    csv_entries = new std::vector<csv_entry>();
    allocation_pairs = new std::vector<allocation_pair>();
}

static void lib_fini(void)
{
    FILE *csv_file = xzx_open_or_abort("xzx_data.csv","w");
    fprintf(csv_file, "\"id\",\"pageable\",\"device_user\",\"device_allocator\",\"pinned_user\",\"pinned_allocator\",\"total\",\"function\",\"file\",\"line\",\"pool_created\",\"pool_deleted\"\n");
    fprintf(csv_file, "0,0,0,0,0,0,0,(origin),(origin),0,0,0\n");
    AllocatorStatus deviceAllocatorStatus("info.cfg");
    AllocatorStatus pinnedAllocatorStatus("info.cfg");
    unsigned int id = 0;
    unsigned long current_pageable = 0;
    unsigned long max_total = 0;
    unsigned long current_unknown = 0;
    unsigned long max_unknown = 0;
    unsigned long device_pools_created = 0;
    unsigned long device_pools_deleted = 0;
    unsigned long pinned_pools_created = 0;
    unsigned long pinned_pools_deleted = 0;
    unsigned long gpu_related_operations = 0;
    for(csv_entry e: *(csv_entries))
    {
        bool pool_created = 0;
        bool pool_deleted = 0;
        switch(e.type)
        {
            case pageable_allocation:
                current_pageable += e.size;
                break;
            case pageable_free:
                current_pageable -= e.size;
                break;
            case device_allocation:
                pool_created = deviceAllocatorStatus.addAllocation(e.pointer, e.size);
                if(pool_created) device_pools_created++;
                break;
            case device_free:
                pool_deleted = deviceAllocatorStatus.removeAllocation(e.pointer);
                if(pool_deleted) device_pools_deleted++;
                break;
            case pinned_allocation:
                pool_created = pinnedAllocatorStatus.addAllocation(e.pointer, e.size);
                if(pool_created) pinned_pools_created++;
                break;
            case pinned_free:
                pool_deleted = pinnedAllocatorStatus.removeAllocation(e.pointer);
                if(pool_deleted) pinned_pools_deleted++;
                break;
            case unknown_allocation:
                current_unknown += e.size;
                break;
            case unknown_free:
                current_unknown -= e.size;
                break;
        }
        if(e.type != unknown_allocation && e.type != unknown_free)
        {
            gpu_related_operations++;
            unsigned long dev_u = deviceAllocatorStatus.getTotalUserMemory();
            unsigned long dev_a = deviceAllocatorStatus.getTotalMemoryReserved();
            unsigned long pin_u = pinnedAllocatorStatus.getTotalUserMemory();
            unsigned long pin_a = pinnedAllocatorStatus.getTotalMemoryReserved();
            unsigned long total = current_pageable + dev_a + pin_a;
            if(total > max_total) max_total = total;
            fprintf(csv_file, "%u,%lu,%lu,%lu,%lu,%lu,%lu,%s(%zu),%s,%u,%u,%u\n", ++id, current_pageable, dev_u, dev_a, pin_u, pin_a, total, e.function, e.size, e.file, e.line, pool_created, pool_deleted);
        }
        else
        {
            if(current_unknown > max_unknown) max_unknown = current_unknown;
        }
    }
    fclose(csv_file);
    
    FILE *report_file = xzx_open_or_abort("xzx_report.txt","w");
    fprintf(report_file, "Max total GPU-related dynamic memory: %lu bytes\n", max_total);
    fprintf(report_file, "Max pageable memory not related to GPU: %lu bytes\n", max_unknown);
    size_t total_nondynamic = 0;
    for(auto &it: *nondynamic_allocations)
    {
        total_nondynamic += it.second;
    }
    fprintf(report_file, "Total non-dynamic memory used in GPU transfers: %zu bytes\n", total_nondynamic);
    fprintf(report_file, "Device pools created: %lu\n", device_pools_created);
    fprintf(report_file, "Device pools deleted: %lu\n", device_pools_deleted);
    fprintf(report_file, "Pinned pools created: %lu\n", pinned_pools_created);
    fprintf(report_file, "Pinned pools deleted: %lu\n", pinned_pools_deleted);
    fprintf(report_file, "Total number of memory operations (allocations + deallocations): %zu\n", csv_entries->size());
    fprintf(report_file, "Total number of gpu-related memory operations (allocations + deallocations): %lu\n", gpu_related_operations);
    fprintf(report_file, "Total number of h2d transfers: %lu\n", h2d_count);
    fprintf(report_file, "Total number of d2h transfers: %lu\n", d2h_count);
    for(auto &it: *host_allocations)
    {
        fprintf(report_file, "Host memory leak detected in %s(%d): %zu bytes\n", it.second.filename, it.second.line, it.second.size);
    }
    for(auto &it: *device_allocations)
    {
        fprintf(report_file, "Device memory leak detected in %s(%d): %zu bytes\n", it.second.filename, it.second.line, it.second.size);
    }
    fclose(report_file);

    FILE *config = xzx_open_or_abort("optimization_profile.txt", "w");
    FILE *bin_config = xzx_open_or_abort("optimization_profile.cfg", "wb");
    fprintf(config, "%lu\n", max_requested_memory);
    unsigned long n_elements = allocation_pairs->size();
    fprintf(config, "%lu\n", n_elements);
    profile_header header = {
        XZX_PROFILE_MAGIC,
        XZX_PROFILE_VERSION,
        xzx_to_u64_or_abort(max_requested_memory, "max requested memory"),
        xzx_to_u64_or_abort(n_elements, "profile row count")
    };
    xzx_write_or_abort(&header, sizeof(profile_header), 1, bin_config, "optimization_profile.cfg");
    for(allocation_pair p: *(allocation_pairs))
    {
        fprintf(config, "%u %llu %u %llu %u %zu %u\n", p.cyclic, p.device_file_hash, p.device_line, p.host_file_hash, p.host_line, p.max_size, p.count);
        profile_row row = {
            p.cyclic ? 1U : 0U,
            p.device_file_hash,
            p.device_line,
            p.host_file_hash,
            p.host_line,
            xzx_to_u64_or_abort(p.max_size, "profile max size"),
            p.count
        };
        xzx_write_or_abort(&row, sizeof(profile_row), 1, bin_config, "optimization_profile.cfg");
    }
    fclose(config);
    fclose(bin_config);

    delete(csv_entries);
    delete(nondynamic_allocations);
    delete(host_allocations);
    delete(device_allocations);
    delete(allocation_pairs);
}

extern "C" {

void *xzx_alz_malloc(size_t size, const char *file, unsigned int line) 
{
    void *pointer = malloc(size);
    allocation_data ad = {file, xzx_hash_string(file), line, size};
    host_allocations->emplace(pointer, ad);
    csv_entry entry = {unknown_allocation, "malloc", size, file, line, pointer};
    csv_entries->push_back(entry);
    return pointer; 
}

void *xzx_alz_calloc(size_t num, size_t size, const char *file, unsigned int line) 
{
    void *pointer = calloc(num, size);
    allocation_data ad = {file, xzx_hash_string(file), line, num * size};
    host_allocations->emplace(pointer, ad);
    csv_entry entry = {unknown_allocation, "calloc", num * size, file, line, pointer};
    csv_entries->push_back(entry);
    return pointer;
}

void xzx_alz_free(void *ptr, const char *file, unsigned int line)
{
    if(ptr == NULL)
    {
        free(ptr);
        return;
    }

    auto allocation = host_allocations->find(ptr);
    if(allocation == host_allocations->end())
    {
        fprintf(stderr, "XeroZerox analyzer warning: unknown host pointer %p passed to free at %s(%u)\n", ptr, file, line);
        return;
    }

    unsigned int size = allocation->second.size;
    unsigned int h_line = allocation->second.line;
    csv_entry entry = {unknown_free, "free", size, file, line, NULL};
    std::vector<csv_entry>::iterator it_ce;
    for(it_ce = csv_entries->begin(); it_ce != csv_entries->end(); ++it_ce)
    {
        if(it_ce->pointer == ptr && it_ce->line == h_line)
        {
            if(it_ce->type == pageable_allocation)
                entry.type = pageable_free;
            break;
        }
    }
    csv_entries->push_back(entry);
    host_allocations->erase(allocation);
    free(ptr);
}

}

cudaError_t xzx_alz_cudaMalloc(void **devPtr, size_t size, const char *file, unsigned int line)
{
    cudaError_t error = cudaMalloc(devPtr, size);
    if(error != cudaSuccess)
        return error;

    unsigned long long file_hash = xzx_hash_string(file);
    bool alive = false;
    for (std::pair<const void*,allocation_data> element : (*device_allocations))
    {
        if(element.second.line == line && element.second.file_hash == file_hash)
        {
            allocation_pair ap = {*devPtr, 0, file_hash, line, 0, 0, size, 1};
            allocation_pairs->push_back(ap);
            alive = true;
            break;
        }
    }
    if(!alive)
    {
        bool repeated = false;
        std::vector<allocation_pair>::iterator it;
        for(it = allocation_pairs->begin(); it != allocation_pairs->end(); ++it)
        {
            if(it->device_line == line && it->device_file_hash == file_hash)
            {
                it->pointer = *devPtr;
                it->count++;
                it->cyclic = 1;
                if(it->max_size < size)
                    it->max_size = size;
                repeated = true;
                break;
            }
        }
        if(!repeated)
        {
            allocation_pair ap = {*devPtr, 0, file_hash, line, 0, 0, size, 1};
            allocation_pairs->push_back(ap);
        }
    }
    current_requested_memory += size;
    if(current_requested_memory > max_requested_memory)
        max_requested_memory = current_requested_memory;

    allocation_data ad = {file, file_hash, line, size};
    device_allocations->emplace(*devPtr, ad);
    csv_entry entry = {device_allocation, "cudaMalloc", size, file, line, *devPtr};
    csv_entries->push_back(entry);
    return error;
}

cudaError_t xzx_alz_cudaFree(void *devPtr, const char *file, unsigned int line)
{
    if(devPtr == NULL)
        return cudaSuccess;

    auto allocation = device_allocations->find(devPtr);
    if(allocation == device_allocations->end())
    {
        fprintf(stderr, "XeroZerox analyzer warning: unknown device pointer %p passed to cudaFree at %s(%u)\n", devPtr, file, line);
        return cudaErrorInvalidDevicePointer;
    }

    unsigned long size = allocation->second.size;
    device_allocations->erase(allocation);
    csv_entry entry = {device_free, "cudaFree", size, file, line, devPtr};
    csv_entries->push_back(entry);
    current_requested_memory -= size;
    return cudaFree(devPtr);
}

cudaError_t xzx_alz_cudaMallocHost(void **ptr, size_t size, const char *file, unsigned int line)
{
    cudaError_t error = cudaMallocHost(ptr, size);
    if(error != cudaSuccess)
        return error;

    allocation_data ad = {file, xzx_hash_string(file), line, size};
    host_allocations->emplace(*ptr, ad);
    csv_entry entry = {pinned_allocation, "cudaMallocHost", size, file, line, *ptr};
    csv_entries->push_back(entry);
    return error;
}

cudaError_t xzx_alz_cudaFreeHost(void *ptr, const char *file, unsigned int line)
{
    if(ptr == NULL)
        return cudaSuccess;

    auto allocation = host_allocations->find(ptr);
    if(allocation == host_allocations->end())
    {
        fprintf(stderr, "XeroZerox analyzer warning: unknown pinned host pointer %p passed to cudaFreeHost at %s(%u)\n", ptr, file, line);
        return cudaErrorInvalidValue;
    }

    unsigned long size = allocation->second.size;
    host_allocations->erase(allocation);
    csv_entry entry = {pinned_free, "cudaFreeHost", size, file, line, ptr};
    csv_entries->push_back(entry);
    return cudaFreeHost(ptr);
}

cudaError_t xzx_alz_cudaMemcpy(void *dst, const void *src, size_t count, cudaMemcpyKind kind, const char *file, unsigned int line)
{
    if(kind == cudaMemcpyHostToDevice || kind == cudaMemcpyDeviceToHost)
    {
        const void *host_pointer;
        const void *device_pointer;
        if(kind == cudaMemcpyHostToDevice)
        {
            host_pointer = src;
            device_pointer = dst;
            h2d_count++;
        }
        else
        {
            host_pointer = dst;
            device_pointer = src;
            d2h_count++;
        }

        auto h_element = xzx_find_host_allocation(host_pointer);
        if(h_element != host_allocations->end())
        {
            xzx_mark_host_allocation_as_pageable(h_element->first, h_element->second);

            auto d_element = xzx_find_device_allocation(device_pointer);
            if(d_element != device_allocations->end())
            {
                ptrdiff_t host_offset = xzx_pointer_offset_in_allocation(host_pointer, h_element->first, h_element->second.size);
                ptrdiff_t device_offset = xzx_pointer_offset_in_allocation(device_pointer, d_element->first, d_element->second.size);
                xzx_update_allocation_pair(device_pointer, d_element->second, h_element->second, device_offset, host_offset, kind, file, line);
            }
            else
            {
                XZX_DEBUG_LOG(
                    "XeroZerox debug: %s at %s(%u) used known host allocation %s(%u) but no tracked device allocation matched pointer %p\n",
                    kind == cudaMemcpyHostToDevice ? "H2D memcpy" : "D2H memcpy",
                    file,
                    line,
                    h_element->second.filename,
                    h_element->second.line,
                    device_pointer);
            }
        }
        else
        {
            bool ptr_arith = false;
            for(auto &it: *host_allocations)
            {
                if(xzx_pointer_in_allocation(host_pointer, it.first, it.second.size))
                {
                    ptr_arith = true;
                    break;
                }
            }
            if(!ptr_arith)
            {
                nondynamic_allocations->emplace(host_pointer, count);
                XZX_DEBUG_LOG(
                    "XeroZerox debug: classified non-dynamic host memory in %s(%u), pointer=%p, bytes=%zu\n",
                    file,
                    line,
                    host_pointer,
                    count);
            }
            else
            {
                XZX_DEBUG_LOG(
                    "XeroZerox debug: %s at %s(%u) touched host pointer %p inside a tracked allocation but no base allocation was selected\n",
                    kind == cudaMemcpyHostToDevice ? "H2D memcpy" : "D2H memcpy",
                    file,
                    line,
                    host_pointer);
            }
        }
    }
    return cudaMemcpy(dst, src, count, kind);
}
