#include "safe_read.h"

#include <windows.h>

#include <cstring>

namespace decima {

bool safeCopy(void* out, uintptr_t address, size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

}  // namespace decima
