#include "tinykv/common/posix_error.h"

#include <cerrno>
#include <cstring>

namespace tinykv {

std::string ErrorMessage(const char* prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}

}
