#include <cstdlib>
#include <ctime>

#include "chronosstream/utils/time_utils.hpp"

int main() {
    std::time_t now = std::time(nullptr);
    // 基础冒烟：确保函数可调用且语义正确。
    const bool ended = chronosstream::time_utils::isProgrammeEnded(now - 10, now);
    return ended ? EXIT_SUCCESS : EXIT_FAILURE;
}
