#include "xml_format.h"
#include "xlog_format.h"
#include <sstream>
#include <iomanip>
#include <ctime>
using namespace std;

static std::string GetNow(
    const char* fmt = "%Y-%m-%d %H:%M:%S",
    int time_zone = 8)
{
    std::time_t unix_sec = std::time(nullptr);
    std::tm tm;
    static const int kHoursInDay = 24;
    static const int kMinutesInHour = 60;
    static const int kDaysFromUnixTime = 2472632;
    static const int kDaysFromYear = 153;
    static const int kMagicUnkonwnFirst = 146097;
    static const int kMagicUnkonwnSec = 1461;
    tm.tm_sec = unix_sec % kMinutesInHour;
    int i = (unix_sec / kMinutesInHour);
    tm.tm_min = i % kMinutesInHour; //nn
    i /= kMinutesInHour;
    tm.tm_hour = (i + time_zone) % kHoursInDay; // hh
    tm.tm_mday = (i + time_zone) / kHoursInDay;
    int a = tm.tm_mday + kDaysFromUnixTime;
    int b = (a * 4 + 3) / kMagicUnkonwnFirst;
    int c = (-b * kMagicUnkonwnFirst) / 4 + a;
    int d = ((c * 4 + 3) / kMagicUnkonwnSec);
    int e = -d * kMagicUnkonwnSec;
    e = e / 4 + c;
    int m = (5 * e + 2) / kDaysFromYear;
    tm.tm_mday = -(kDaysFromYear * m + 2) / 5 + e + 1;
    tm.tm_mon = (-m / 10) * 12 + m + 2;
    tm.tm_year = b * 100 + d - 6700 + (m / 10);
    stringstream ss;
    ss << std::put_time(&tm, fmt); //#include <iomanip>
    return ss.str();
}
std::string XmlFormat::Format(
    const std::string& level,
    const std::string& log,
    const std::string& file,
    int line
)
{
    stringstream ss;
    ss << "<log>\n"
        << "\t<time>"<<GetNow()<<"</time>"
        << "\t<level>"<<level<<"</level>"
        << "\t<message>"<<log<<"</message>"
        << "\t<file>"<<file<<"</file>"
        << "\t<line>"<<line<<"< / line>"
        << "\n</log>";
    return ss.str();
}