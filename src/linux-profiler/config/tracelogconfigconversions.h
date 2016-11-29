#ifndef _TRACE_LOG_CONFIG_CONVERSIONS_H_
#define _TRACE_LOG_CONFIG_CONVERSIONS_H_

#include "tracelogconfig.h"
#include "commonconfig.h"

template<>
TraceLogOutputStream convert(const char *str);

#endif // _TRACE_LOG_CONFIG_CONVERSIONS_H_
