#ifndef _PROFILER_CONFIG_CONVERSIONS_H_
#define _PROFILER_CONFIG_CONVERSIONS_H_

#include "profilerconfig.h"
#include "commonconfig.h"

template<>
CollectionMethod convert(const char *str);

template<>
const char *convert(CollectionMethod method);

#endif // _PROFILER_CONFIG_CONVERSIONS_H_
