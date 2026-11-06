#include "refw.h"

#include <windows.h>

#include <cstring>

#include "fnv1a32.h"

namespace {

constexpr unsigned kMaxParams = 32;
constexpr unsigned kNameBuffer = 512;

const REFrameworkPluginInitializeParam* g_param = nullptr;

const REFrameworkSDKData& sdk() { return *g_param->sdk; }
REFrameworkTDBHandle tdb() { return sdk().functions->get_tdb(); }

std::string fullName(REFrameworkTypeDefinitionHandle type) {
    if (!type) return {};
    char buffer[kNameBuffer] = {};
    unsigned length = 0;
    if (sdk().type_definition->get_full_name(type, buffer, sizeof(buffer), &length) != REFRAMEWORK_ERROR_NONE) {
        return sdk().type_definition->get_name(type);
    }
    return std::string(buffer, strnlen(buffer, sizeof(buffer)));
}

std::string paramList(REFrameworkMethodHandle method) {
    REFrameworkMethodParameter params[kMaxParams] = {};
    unsigned count = 0;
    if (sdk().method->get_params(method, params, sizeof(params), &count) != REFRAMEWORK_ERROR_NONE) return "?";
    std::string out;
    for (unsigned i = 0; i < count; ++i) out += (i ? "," : "") + fullName(params[i].t);
    return out;
}

}  // namespace

namespace refw {

void init(const REFrameworkPluginInitializeParam* param) { g_param = param; }

const REFrameworkPluginInitializeParam* param() { return g_param; }

REFrameworkMethodHandle findMethod(const char* type, const char* method, const char* params) {
    const REFrameworkTypeDefinitionHandle definition = sdk().tdb->find_type(tdb(), type);
    if (!definition) return nullptr;
    const unsigned count = sdk().type_definition->get_num_methods(definition);
    std::vector<REFrameworkMethodHandle> methods(count);
    unsigned written = 0;
    if (sdk().type_definition->get_methods(definition, methods.data(), count * sizeof(REFrameworkMethodHandle),
                                           &written) != REFRAMEWORK_ERROR_NONE) {
        return nullptr;
    }
    for (unsigned i = 0; i < written; ++i) {
        if (std::strcmp(sdk().method->get_name(methods[i]), method) == 0 && paramList(methods[i]) == params) {
            return methods[i];
        }
    }
    return nullptr;
}

uintptr_t rebasedBody(REFrameworkMethodHandle method) {
    const auto body = reinterpret_cast<uintptr_t>(method ? sdk().method->get_function(method) : nullptr);
    if (!body) return 0;
    return body - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + kDumpImageBase;
}

std::vector<std::pair<std::string, int64_t>> enumLiterals(const char* type) {
    std::vector<std::pair<std::string, int64_t>> out;
    const REFrameworkTypeDefinitionHandle definition = sdk().tdb->find_type(tdb(), type);
    if (!definition) return out;
    // Sized from the type: app.CharacterID alone has over 1700 literals.
    std::vector<REFrameworkFieldHandle> fields(sdk().type_definition->get_num_fields(definition));
    unsigned count = 0;
    if (fields.empty() || sdk().type_definition->get_fields(definition, fields.data(), static_cast<unsigned>(fields.size() * sizeof(fields[0])),
                                                            &count) != REFRAMEWORK_ERROR_NONE) {
        return out;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!sdk().field->is_literal(fields[i]) || !sdk().field->is_static(fields[i])) continue;
        const auto* value = static_cast<const int32_t*>(sdk().field->get_init_data(fields[i]));
        if (value) out.emplace_back(sdk().field->get_name(fields[i]), *value);
    }
    return out;
}

std::optional<int64_t> enumLiteral(const char* type, const char* name) {
    for (const auto& [each, value] : enumLiterals(type)) {
        if (each == name) return value;
    }
    return std::nullopt;
}

unsigned addHook(REFrameworkMethodHandle method, REFPreHookFn pre, REFPostHookFn post) {
    const unsigned id = sdk().functions->add_hook(method, pre, post, false);
    return id == UINT32_MAX ? 0 : id + 1;
}

void removeHook(REFrameworkMethodHandle method, unsigned hook) { sdk().functions->remove_hook(method, hook - 1); }

const InvokeResult* invoke(REFrameworkMethodHandle method, void* self, std::initializer_list<void*> args) {
    thread_local InvokeResult result;
    result = {};
    void* in[kMaxParams] = {};
    unsigned count = 0;
    for (void* arg : args) {
        if (count == kMaxParams) return nullptr;
        in[count++] = arg;
    }
    const REFrameworkResult status = sdk().method->invoke(method, self, in, count * sizeof(void*), &result, sizeof(result));
    return status == REFRAMEWORK_ERROR_NONE && !result.threw ? &result : nullptr;
}

void* managedSingleton(const char* type) { return sdk().functions->get_managed_singleton(type); }

void* nativeSingleton(const char* type) { return sdk().functions->get_native_singleton(type); }

std::string typeNameOf(void* object) {
    if (!object) return {};
    return fullName(sdk().managed_object->get_type_definition(static_cast<REFrameworkManagedObjectHandle>(object)));
}

std::vector<std::string> paramNames(REFrameworkMethodHandle method) {
    REFrameworkMethodParameter params[kMaxParams] = {};
    unsigned count = 0;
    std::vector<std::string> out;
    if (sdk().method->get_params(method, params, sizeof(params), &count) != REFRAMEWORK_ERROR_NONE) return out;
    for (unsigned i = 0; i < count; ++i) out.emplace_back(params[i].name ? params[i].name : "");
    return out;
}

void* typeOf(const char* type) { return sdk().functions->typeof_(type); }

void* managedString(const std::wstring& text) { return sdk().functions->create_managed_string(text.c_str()); }

std::wstring stringValue(const void* string) {
    constexpr size_t kLength = 0x10, kChars = 0x14;  // System.String: i32 length, then UTF-16 characters
    if (!string) return {};
    const auto* bytes = static_cast<const uint8_t*>(string);
    int32_t length = 0;
    std::memcpy(&length, bytes + kLength, sizeof(length));
    if (length <= 0) return {};
    return std::wstring(reinterpret_cast<const wchar_t*>(bytes + kChars), static_cast<size_t>(length));
}

void* createInstance(const char* type, bool runConstructor) {
    const REFrameworkTypeDefinitionHandle definition = sdk().tdb->find_type(tdb(), type);
    if (!definition) return nullptr;
    void* object = sdk().type_definition->create_instance(
        definition, runConstructor ? REFRAMEWORK_CREATE_INSTANCE_FLAGS_NONE : REFRAMEWORK_CREATE_INSTANCE_FLAGS_SIMPLIFY);
    if (object) addRef(object);
    return object;
}

void addRef(void* object) {
    if (object) sdk().managed_object->add_ref(static_cast<REFrameworkManagedObjectHandle>(object));
}

void release(void* object) {
    if (object) sdk().managed_object->release(static_cast<REFrameworkManagedObjectHandle>(object));
}

void writeRef(void* object, uint32_t offset, void* value) {
    auto* slot = reinterpret_cast<void**>(static_cast<uint8_t*>(object) + offset);
    addRef(value);
    void* old = *slot;
    *slot = value;
    release(old);
}

bool staticFieldBytes(const char* type, const char* field, void* out, size_t size) {
    const REFrameworkFieldHandle handle = sdk().tdb->find_field(tdb(), type, field);
    if (!handle || !sdk().field->is_static(handle)) return false;
    const void* data = sdk().field->get_data_raw(handle, nullptr, false);
    if (!data) return false;
    std::memcpy(out, data, size);
    return true;
}

uint32_t tdbHash() {
    const uint32_t counts[] = {sdk().tdb->get_num_types(tdb()), sdk().tdb->get_num_methods(tdb()),
                               sdk().tdb->get_num_fields(tdb())};
    return fnv1a32::hash({reinterpret_cast<const uint8_t*>(counts), sizeof(counts)});
}

}  // namespace refw
