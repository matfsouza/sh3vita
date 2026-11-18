// FILETIME -> SYSTEMTIME with civil-calendar arithmetic (proleptic Gregorian), valid for every FILETIME.
#include "w32_time.h"

void filetime_to_systemtime(uint64_t ft, uint16_t st[8]) {
    uint64_t ms_total = ft / 10000u;
    uint64_t days = ms_total / 86400000u;
    uint32_t ms_day = (uint32_t)(ms_total % 86400000u);
    // Days since 0000-03-01 (era-based civil-from-days); 1601-01-01 is day 584694 of that count.
    uint64_t z = days + 584694u;
    uint64_t era = z / 146097u;
    uint32_t doe = (uint32_t)(z - era * 146097u);                                // [0, 146096]
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;    // [0, 399]
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);                   // [0, 365], from March 1st
    uint32_t mp = (5u * doy + 2u) / 153u;                                        // [0, 11], March = 0
    uint32_t day = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t month = mp < 10u ? mp + 3u : mp - 9u;
    uint64_t year = era * 400u + yoe + (month <= 2u);
    st[0] = (uint16_t)year;
    st[1] = (uint16_t)month;
    st[2] = (uint16_t)((days + 1u) % 7u);   // 1601-01-01 was a Monday
    st[3] = (uint16_t)day;
    st[4] = (uint16_t)(ms_day / 3600000u);
    st[5] = (uint16_t)(ms_day / 60000u % 60u);
    st[6] = (uint16_t)(ms_day / 1000u % 60u);
    st[7] = (uint16_t)(ms_day % 1000u);
}
