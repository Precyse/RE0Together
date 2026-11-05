#pragma once
// The REFramework plugin interface as the rest of the plugin uses it: method lookup by type, name and parameter types,
// native bodies rebased to the dump's image base, enum literals by name, and hooks.
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "reframework/API.h"

namespace refw {

// The dump's addresses are rebased to this image base (DD2.exe loads with ASLR).
constexpr uintptr_t kDumpImageBase = 0x140000000;

// Called once from reframework_plugin_initialize.
void init(const REFrameworkPluginInitializeParam* param);
const REFrameworkPluginInitializeParam* param();

// The method of `type` named `method` whose parameter type names, joined by ',', equal `params`; null when none.
REFrameworkMethodHandle findMethod(const char* type, const char* method, const char* params);

// The method's native body rebased to kDumpImageBase, or 0.
uintptr_t rebasedBody(REFrameworkMethodHandle method);

// Every literal of an enum type as (name, value); empty when the type is unknown.
std::vector<std::pair<std::string, int64_t>> enumLiterals(const char* type);

// 1 + the hook id, or 0 when REFramework refused the hook.
unsigned addHook(REFrameworkMethodHandle method, REFPreHookFn pre, REFPostHookFn post);

// Takes back a hook addHook made (`hook` is its return value).
void removeHook(REFrameworkMethodHandle method, unsigned hook);

// Type, method and field counts of the loaded type database, folded into one number for GAME_HELLO.
uint32_t tdbHash();

// What REFramework's invoke writes back: the return value's bytes (a value type up to 128 bytes, or a pointer, an
// integer or a float at the start) and whether the method threw.
#pragma pack(push, 1)
struct InvokeResult {
    union {
        uint8_t bytes[128];
        uint8_t byte;
        uint32_t dword;
        float f;
        uint64_t qword;
        void* ptr;
    };
    bool threw;
};
#pragma pack(pop)

// Calls `method` on `self` (null for a static method) with arguments of 8 bytes each (integers, booleans and object
// pointers cast to void*). Null when REFramework refused the call or the method threw. Game thread only.
const InvokeResult* invoke(REFrameworkMethodHandle method, void* self, std::initializer_list<void*> args = {});

// The managed singleton of a type (app.CharacterManager ...), or null before the game created it.
void* managedSingleton(const char* type);

// The native singleton of a type (via.storage.saveService.SaveService ...), or null.
void* nativeSingleton(const char* type);

// The full type name of a managed object (its runtime type), empty for null.
std::string typeNameOf(void* object);

// The parameter names of a method, for checking what an Int32 argument means; empty on failure.
std::vector<std::string> paramNames(REFrameworkMethodHandle method);

// The System.Type object of a type name (REFramework typeof), or null.
void* typeOf(const char* type);

// A new managed object of `type`, holding one reference of ours (release it once the engine holds its own). With
// `runConstructor` false the object is only allocated, for a constructor with arguments invoked next. Null when the type
// is unknown.
void* createInstance(const char* type, bool runConstructor = true);
void addRef(void* object);
void release(void* object);

// A field of a managed object or value at a byte offset.
template <typename T>
T& field(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// Stores a managed reference into an object's field the way the engine does: a reference on the new value, the old
// value released.
void writeRef(void* object, uint32_t offset, void* value);

// A new System.String (REFramework create_managed_string), or null.
void* managedString(const std::wstring& text);

// The characters of a System.String, empty for null.
std::wstring stringValue(const void* string);

// A static field of `type` read as T; false when the field is unknown.
bool staticFieldBytes(const char* type, const char* field, void* out, size_t size);
template <typename T>
bool staticField(const char* type, const char* field, T& out) {
    return staticFieldBytes(type, field, &out, sizeof(T));
}

}  // namespace refw
