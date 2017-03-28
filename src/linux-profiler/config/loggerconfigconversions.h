#ifndef _LOGGER_CONFIG_CONVERSIONS_H_
#define _LOGGER_CONFIG_CONVERSIONS_H_

#include "loggerconfig.h"
#include "commonconfig.h"

template<>
LogLevel convert(const char *str);

template<>
LoggerOutputStream convert(const char *str);

#endif // _LOGGER_CONFIG_CONVERSIONS_H_
