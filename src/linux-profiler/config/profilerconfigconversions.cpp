#include <string.h>
#include <strings.h>
#include <assert.h>

#include "profilerconfigconversions.h"

template<>
CollectionMethod convert(const char *str)
{
    if (strcasecmp(str, "None") == 0 || strlen(str) == 0)
    {
        return CollectionMethod::None;
    }
    else if (strcasecmp(str, "Instrumentation") == 0)
    {
        return CollectionMethod::Instrumentation;
    }
    else if (strcasecmp(str, "Sampling") == 0)
    {
        return CollectionMethod::Sampling;
    }
    else
    {
        throw bad_conversion("incorrect value for type CollectionMethod");
    }
}

template<>
const char *convert(CollectionMethod method)
{
    switch (method)
    {
    case CollectionMethod::None:
        return "None";

    case CollectionMethod::Instrumentation:
        return "Instrumentation";

    case CollectionMethod::Sampling:
        return "Sampling";

    default:
        assert(!"Unreachable");
        return "UNKNOWN";
    }
}
