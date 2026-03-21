#ifndef ALLOCATORSTATUS_H
#define ALLOCATORSTATUS_H

#include <list>
#include <map>

using namespace std;

class MemoryPool
{
    public:
        MemoryPool(char *baseAddress, int size);
        void addUsed(int allocationSize);
        void freeUsed(int allocationSize);
        int getMemoryUsed();
        bool includesAddress(char *address);

    private:
        char *baseAddress;
        int size;
        int memoryUsed;
};

class SizeClass
{
    public:
        SizeClass(int classId, int minBytes, int maxBytes, int poolSize, int granularity);
        bool addAllocation(char *address, int allocationSize);
        bool removeAllocation(char *address, int allocationSize);
        bool includesSize(int allocationSize);
        int getClassId();
        int getMemoryReserved();
        int getMemoryUsed();
        void setMinBytes(int minBytes);
        void setMaxBytes(int maxBytes);

    private:
        int classId;
        int minBytes;
        int maxBytes;
        int poolSize;
        int granularity;
        list<MemoryPool> memoryPools;
};

class SizeClassInfo
{
    public:
        SizeClassInfo(int classId, int minBlocks, int maxBlocks, int minBytes, int maxBytes);
        int classId;
        int minBlocks;
        int maxBlocks;
        int minBytes;
        int maxBytes;
};

class AllocatorInfo
{
    public:
        AllocatorInfo(const char* fileName);
        char deviceName[21];
        char computeCapability[11];
        char runtimeVersion[11];
        char driverVersion[11];
        char policy[21];
        char expansionPolicy[21];
        char poolUsage[21];
        char shrinking[31];
        int poolSize;
        int granularity;
        int largerMultiple;
        int coalescing;
        int splitting;
        list<SizeClassInfo> sizeClassesInfo;
};

class AllocatorStatus
{
    public:
        AllocatorStatus(const char* fileName);
        bool addAllocation(void *address, int size);
        bool removeAllocation(void *address);
        int getTotalMemoryReserved();
        int getTotalUserMemory();
        int getMaximumMemoryReserved();
        int getMaximumUserMemory();
        void printStatus();
        void printMaximum();

    private:
        int poolSize;
        int granularity;
        int largeGranularity;
        int totalUserMemory;
        int totalLargeMemory;
        int totalLargeMemoryUsed;
        int maximumMemoryReserved;
        int maximumUserMemory;
        list<SizeClass> sizeClasses;
        map<void*,int> allocations;
};

#endif
