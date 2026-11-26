/* CPU-independent Gregorian calendar arithmetic, signed 32-bit Unix epoch.
 * The current runtime uses UTC for local time until TZ/DST support exists. */
#include <time.h>
struct tm q9_calendar_result;
static int leap(int year)
{ return year%4==0 && (year%100!=0 || year%400==0); }
static int month_days(int year,int month)
{
    if (month==1) return leap(year)?29:28;
    if (month==3 || month==5 || month==8 || month==10) return 30;
    return 31;
}
struct tm *gmtime(const time_t *value)
{
    long days, seconds, original;
    int year=1970, month=0, length, yday;
    if (!value) return (struct tm *)0;
    days=*value/86400L;seconds=*value%86400L;
    if (seconds<0) { seconds+=86400L;--days; }
    original=days;
    if (days<0) {
        do { --year;days+=leap(year)?366:365; } while (days<0);
    } else {
        length=leap(year)?366:365;
        while (days>=length) { days-=length;++year;length=leap(year)?366:365; }
    }
    yday=days;
    length=month_days(year,month);
    while (days>=length) { days-=length;++month;length=month_days(year,month); }
    q9_calendar_result.tm_year=year-1900;q9_calendar_result.tm_mon=month;
    q9_calendar_result.tm_mday=days+1;q9_calendar_result.tm_hour=seconds/3600;
    q9_calendar_result.tm_min=(seconds%3600)/60;q9_calendar_result.tm_sec=seconds%60;
    q9_calendar_result.tm_yday=yday;
    q9_calendar_result.tm_wday=(original+4)%7;
    if (q9_calendar_result.tm_wday<0) q9_calendar_result.tm_wday+=7;
    q9_calendar_result.tm_isdst=0;
    return &q9_calendar_result;
}
struct tm *localtime(const time_t *value) { return gmtime(value); }
time_t mktime(struct tm *value)
{
    int year, month, i;
    long days=0, seconds;
    time_t result;
    struct tm *normalized;
    if (!value) return -1;
    /* Bound intermediate arithmetic; final output is the full signed
     * 32-bit range. Month/day/time overflow is normalized within this bound. */
    if (value->tm_year<0 || value->tm_year>200 || value->tm_mon < -120 || value->tm_mon>120 ||
        value->tm_mday < -100000 || value->tm_mday>100000 ||
        value->tm_hour < -10000 || value->tm_hour>10000 ||
        value->tm_min < -1000000 || value->tm_min>1000000 ||
        value->tm_sec < -1000000 || value->tm_sec>1000000) return -1;
    year=value->tm_year+1900;month=value->tm_mon;
    while (month<0) { month+=12;--year; }
    while (month>11) { month-=12;++year; }
    if (year>=1970) for (i=1970;i<year;++i) days+=leap(i)?366:365;
    else for (i=year;i<1970;++i) days-=leap(i)?366:365;
    for (i=0;i<month;++i) days+=month_days(year,i);
    days+=value->tm_mday-1;
    seconds=value->tm_hour*3600L+value->tm_min*60L+value->tm_sec;
    days+=seconds/86400L;seconds%=86400L;
    if (seconds<0) { seconds+=86400L;--days; }
    if (days < -24856L || days>24855L) return -1;
    if (days==24855L && seconds>11647L) return -1;
    if (days==-24856L) {
        if (seconds<74752L) return -1;
        result=(long)0x80000000U+(seconds-74752L);
    } else result=days*86400L+seconds;
    normalized=gmtime(&result);
    /* Copy fields explicitly: shared frontend has no struct assignment. */
    value->tm_year=normalized->tm_year;value->tm_mon=normalized->tm_mon;
    value->tm_mday=normalized->tm_mday;value->tm_hour=normalized->tm_hour;
    value->tm_min=normalized->tm_min;value->tm_sec=normalized->tm_sec;
    value->tm_wday=normalized->tm_wday;value->tm_yday=normalized->tm_yday;
    value->tm_isdst=0;
    return result;
}
