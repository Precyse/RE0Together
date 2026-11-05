#include "safe_read.h"

#include <windows.h>

#include <cstring>

namespace safe_read {

bool bytes(uintptr_t address, void* out, size_t size) {
    if (!address) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace safe_read
