#ifndef _COMMON_CONFIG_CONVERSIONS_H_
#define _COMMON_CONFIG_CONVERSIONS_H_

#include "commonconfig.h"

template<>
bool convert(const char *str);

template<>
unsigned long convert(const char *str);

template<>
std::string convert(const char *str);

template<>
const char *convert(bool value);

#endif // _COMMON_CONFIG_CONVERSIONS_H_
