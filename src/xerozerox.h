#ifndef XEROZEROX_H
#define XEROZEROX_H

#ifdef XZX_ANALYZE
#include "xzx_analyzer.h"
#else
#ifdef XZX_OPTIMIZE
#include "xzx_optimizer.h"
/* Uncomment to use unified memory instead of zero-copy memory*/
//#define UNIFIED_MEMORY
#endif
#endif

#endif