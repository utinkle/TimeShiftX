#include "timeshiftx/time_utils.hpp"

#include <iomanip>
#include <sstream>

namespace timeshiftx {
namespace time_utils {

std::string formatUtc(std::time_t ts, const std::string& fmt) {
    std::tm utc_tm {};
#if defined(_WIN32)
    gmtime_s(&utc_tm, &ts);
#else
    gmtime_r(&ts, &utc_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc_tm, fmt.c_str());
    return oss.str();
}

bool isProgrammeEnded(std::time_t programme_end_time, std::time_t now_utc) {
    // 业务语义：结束时间小于当前时间，即认为节目已结束可尝试回看。
    return programme_end_time < now_utc;
}

} // namespace time_utils
} // namespace timeshiftx
