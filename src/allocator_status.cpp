#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "allocator_status.h"

/*------------------------------------------------------------------------------------------------*/
MemoryPool::MemoryPool(char *baseAddress, int size)
{
    this->baseAddress = baseAddress;
    this->size = size;
    this->memoryUsed = 0;
}

void MemoryPool::addUsed(int allocationSize)
{
    this->memoryUsed += allocationSize;
}

void MemoryPool::freeUsed(int allocationSize)
{
    this->memoryUsed -= allocationSize;
}

int MemoryPool::getMemoryUsed()
{
    return this->memoryUsed;
}

bool MemoryPool::includesAddress(char *address)
{
    return ((address >= this->baseAddress) && (address < (this->baseAddress + this->size)));
}
/*------------------------------------------------------------------------------------------------*/
SizeClass::SizeClass(int classId, int minBytes, int maxBytes, int poolSize, int granularity)
{
    this->classId = classId;
    this->minBytes = minBytes;
    this->maxBytes = maxBytes;
    this->poolSize = poolSize;
    this->granularity = granularity;
}

bool SizeClass::addAllocation(char *address, int allocationSize)
{
    int realSize = ((allocationSize + this->granularity - 1) / this->granularity) * this->granularity;
    bool newPool = true;
    for(list<MemoryPool>::iterator i = this->memoryPools.begin(); i != this->memoryPools.end(); ++i)
    {
        if(i->includesAddress(address))
        {
            i->addUsed(realSize);
            newPool = false;
            break;
        }
    }

    if(newPool)
    {
        MemoryPool pool(address, this->poolSize);
        pool.addUsed(realSize);
        this->memoryPools.push_front(pool);
    }
    return newPool;
}

bool SizeClass::removeAllocation(char *address, int allocationSize)
{
    int realSize = ((allocationSize + this->granularity - 1) / this->granularity) * this->granularity;
    bool poolDeleted = false;
    for(list<MemoryPool>::iterator i = this->memoryPools.begin(); i != this->memoryPools.end(); ++i)
    {
        if(i->includesAddress(address))
        {
            i->freeUsed(realSize);
            if(i->getMemoryUsed() == 0)
            {
                this->memoryPools.erase(i);
                poolDeleted = true;
            }
            break;
        }
    }
    return poolDeleted;
}

bool SizeClass::includesSize(int allocationSize)
{
    return ((allocationSize >= this->minBytes) && (allocationSize <= this->maxBytes));
}

int SizeClass::getClassId()
{
    return this->classId;
}

int SizeClass::getMemoryReserved()
{
    return (this->memoryPools.size() * this->poolSize);
}

int SizeClass::getMemoryUsed()
{
    int memoryUsed = 0;
    for(list<MemoryPool>::iterator i = this->memoryPools.begin(); i != this->memoryPools.end(); ++i)
    {
        memoryUsed += i->getMemoryUsed();
    }
    return memoryUsed;
}

void SizeClass::setMinBytes(int minBytes)
{
    this->minBytes = minBytes;
}

void SizeClass::setMaxBytes(int maxBytes)
{
    this->maxBytes = maxBytes;
}
/*------------------------------------------------------------------------------------------------*/
SizeClassInfo::SizeClassInfo(int classId, int minBlocks, int maxBlocks, int minBytes, int maxBytes)
{
	this->classId = classId;
    this->minBlocks = minBlocks;
    this->maxBlocks = maxBlocks;
    this->minBytes = minBytes;
    this->maxBytes = maxBytes;
}
/*------------------------------------------------------------------------------------------------*/
AllocatorInfo::AllocatorInfo(const char* fileName)
{
	FILE *configFile = fopen(fileName, "r");
    if(configFile == NULL)
    {
        fprintf(stderr, "XeroZerox error: unable to open allocator config %s\n", fileName);
        exit(1);
    }
	char line[50], param[5];
	
	while(fgets(line, sizeof line, configFile) != NULL)
	{
		sscanf(line, "%3s", param);
		if(strcmp(param, "DEV") == 0)
		{
			sscanf(line, "%3s %20[^\n]", param, this->deviceName);
		} 
		else if(strcmp(param, "CAP") == 0)
		{
			sscanf(line, "%3s %10s", param, this->computeCapability);
		}
		else if(strcmp(param, "RTV") == 0)
		{
			sscanf(line, "%3s %10s", param, this->runtimeVersion);
		}
		else if(strcmp(param, "DRV") == 0)
		{
			sscanf(line, "%3s %10s", param, this->driverVersion);
		}
		else if(strcmp(param, "PSZ") == 0)
		{
			sscanf(line, "%3s %d", param, &this->poolSize);
		}
		else if(strcmp(param, "GRN") == 0)
		{
			sscanf(line, "%3s %d", param, &this->granularity);
		}
		else if(strcmp(param, "SCL") == 0)
		{
			int classId, minBlocks, maxBlocks, minBytes, maxBytes;
			sscanf(line, "%3s %d %d %d %d %d", param, &classId, &minBlocks, &maxBlocks, &minBytes, &maxBytes);
			SizeClassInfo sizeClassInfo(classId, minBlocks, maxBlocks, minBytes, maxBytes);
			this->sizeClassesInfo.push_back(sizeClassInfo);
		}
		else if(strcmp(param, "LGM") == 0)
		{
			sscanf(line, "%3s %d", param, &this->largerMultiple);
		}
		else if(strcmp(param, "POL") == 0)
		{
			sscanf(line, "%3s %20[^\n]", param, this->policy);
		}
		else if(strcmp(param, "CLS") == 0)
		{
			sscanf(line, "%3s %d", param, &this->coalescing);
		}
		else if(strcmp(param, "SPL") == 0)
		{
			sscanf(line, "%3s %d", param, &this->splitting);
		}
		else if(strcmp(param, "EXP") == 0)
		{
			sscanf(line, "%3s %20[^\n]", param, this->expansionPolicy);
		}
		else if(strcmp(param, "USG") == 0)
		{
			sscanf(line, "%3s %20[^\n]", param, this->poolUsage);
		}
		else if(strcmp(param, "SHR") == 0)
		{
			sscanf(line, "%3s %30[^\n]", param, this->shrinking);
		}
	}

	fclose(configFile);
}
/*------------------------------------------------------------------------------------------------*/
AllocatorStatus::AllocatorStatus(const char* fileName)
{
    AllocatorInfo info(fileName);
    this->poolSize = info.poolSize;
    this->granularity = info.granularity;
    this->largeGranularity = info.largerMultiple;
    this->totalUserMemory = 0;
    this->totalLargeMemory = 0;
    this->totalLargeMemoryUsed = 0;
    this->maximumMemoryReserved = 0;
    for(list<SizeClassInfo>::iterator i = info.sizeClassesInfo.begin(); i != info.sizeClassesInfo.end(); ++i)
    {
        SizeClass sizeClass(i->classId, i->minBytes, i->maxBytes, this->poolSize, this->granularity);
        this->sizeClasses.push_back(sizeClass);
    }
    this->sizeClasses.back().setMaxBytes(this->poolSize);
}

bool AllocatorStatus::addAllocation(void *address, int size)
{
    this->allocations.insert(pair<void*,int>(address, size));
    this->totalUserMemory += size;
    bool poolCreated = false;
    if(size > this->poolSize)
    {
        int realSize = ((size + this->largeGranularity - 1) / this->largeGranularity) * this->largeGranularity;
        this->totalLargeMemory += realSize;
        this->totalLargeMemoryUsed += size;
        poolCreated = true;
    }
    else
    {
         for(list<SizeClass>::iterator i = this->sizeClasses.begin(); i != this->sizeClasses.end(); ++i)
         {
             if(i->includesSize(size))
             {
                 poolCreated = i->addAllocation(static_cast<char*>(address), size);
                 break;
             }
         }
    }
    int currentTotal = this->getTotalMemoryReserved();
    if(currentTotal >= this->maximumMemoryReserved)
    {
        this->maximumMemoryReserved = currentTotal;
        this->maximumUserMemory = this->totalUserMemory;
    }
    return poolCreated;
}

bool AllocatorStatus::removeAllocation(void *address)
{
    int size = this->allocations[address];
    this->totalUserMemory -= size;
    bool poolDeleted = false;
    if(size > this->poolSize)
    {
        int realSize = ((size + this->largeGranularity - 1) / this->largeGranularity) * this->largeGranularity;
        this->totalLargeMemory -= realSize;
        this->totalLargeMemoryUsed -= size;
        poolDeleted = true;
    }
    else
    {
        for(list<SizeClass>::iterator i = this->sizeClasses.begin(); i != this->sizeClasses.end(); ++i)
         {
             if(i->includesSize(size))
             {
                 poolDeleted = i->removeAllocation(static_cast<char*>(address), size);
                 break;
             }
         }
    }
    this->allocations.erase(address);
    return poolDeleted;
}

int AllocatorStatus::getTotalMemoryReserved()
{
    int totalMemoryReserved = this->totalLargeMemory;
    for(list<SizeClass>::iterator i = this->sizeClasses.begin(); i != this->sizeClasses.end(); ++i)
    {
        totalMemoryReserved += i->getMemoryReserved();
    }
    return totalMemoryReserved;
}

int AllocatorStatus::getTotalUserMemory()
{
    return this->totalUserMemory;
}

int AllocatorStatus::getMaximumMemoryReserved()
{
    return this->maximumMemoryReserved;
}

int AllocatorStatus::getMaximumUserMemory()
{
    return this->maximumUserMemory;
}

void AllocatorStatus::printStatus()
{
    fprintf(stderr, "Total memory reserved by user: %d bytes\n", this->totalUserMemory);
    fprintf(stderr, "Real memory reserved by allocator:\n");
    int totalMemoryUsed = this->totalLargeMemoryUsed;
    for(list<SizeClass>::iterator i = this->sizeClasses.begin(); i != this->sizeClasses.end(); ++i)
    {
        int memoryUsed = i->getMemoryUsed();
        fprintf(stderr, "Class %d: %d bytes (%d bytes used)\n", i->getClassId(), i->getMemoryReserved(), memoryUsed);
        totalMemoryUsed += memoryUsed;
    }
    int totalMemoryReserved = this->getTotalMemoryReserved();
    float percentageUsed = totalMemoryReserved > 0? (totalMemoryUsed/(float)totalMemoryReserved) * 100 : 0;
    float factor = this->totalUserMemory > 0? (totalMemoryReserved / (float)this->totalUserMemory) : 0;
    fprintf(stderr, "Larger allocations: %d bytes\n", this->totalLargeMemory);
    fprintf(stderr, "Total memory reserved by allocator: %d bytes (%.2fx the amount requested by user)\n", totalMemoryReserved, factor);
    fprintf(stderr, "Total memory used : %d bytes (%.2f%% of the memory reserved by allocator)\n\n", totalMemoryUsed, percentageUsed);
}

void AllocatorStatus::printMaximum()
{
    int maxUserMemory = this->getMaximumUserMemory();
    if(maxUserMemory > 0)
    {
        int maxMemoryReserved = this->getMaximumMemoryReserved();
        float factor = maxMemoryReserved / (float)maxUserMemory;
        fprintf(stderr, "Maximum memory requested by user: %d bytes\n", maxUserMemory);
        fprintf(stderr, "Maximum memory reserved by allocator: %d bytes (%.2fx the amount requested by user)\n\n", maxMemoryReserved, factor);
    }
}
