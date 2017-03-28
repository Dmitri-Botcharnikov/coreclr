#include <system_error>

#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "localtime.h"

VOID
PALAPI
GetLocalTime(OUT LPSYSTEMTIME lpLocalTime)
{
    time_t tt;
    struct tm ut;
    struct tm *utPtr;
    struct timeval timeval;
    int timeofday_retval;

    tt = time(NULL);

    /* We can't get millisecond resolution from time(), so we get it from
       gettimeofday() */
    timeofday_retval = gettimeofday(&timeval, NULL);

    utPtr = &ut;
    if (localtime_r(&tt, utPtr) == NULL)
    {
        throw std::system_error(errno, std::system_category(),
            "localtime_r() failed");
    }

    lpLocalTime->wYear = 1900 + utPtr->tm_year;
    lpLocalTime->wMonth = utPtr->tm_mon + 1;
    lpLocalTime->wDayOfWeek = utPtr->tm_wday;
    lpLocalTime->wDay = utPtr->tm_mday;
    lpLocalTime->wHour = utPtr->tm_hour;
    lpLocalTime->wMinute = utPtr->tm_min;
    lpLocalTime->wSecond = utPtr->tm_sec;

    if(-1 == timeofday_retval)
    {
        lpLocalTime->wMilliseconds = 0;
        throw std::system_error(errno, std::system_category(),
            "gettimeofday() failed");
    }
    else
    {
        int old_seconds;
        int new_seconds;

        lpLocalTime->wMilliseconds = timeval.tv_usec / 1000;

        old_seconds = utPtr->tm_sec;
        new_seconds = timeval.tv_sec%60;

        /* just in case we reached the next second in the interval between
           time() and gettimeofday() */
        if(old_seconds != new_seconds)
        {
            lpLocalTime->wMilliseconds = 999;
        }
    }
}
