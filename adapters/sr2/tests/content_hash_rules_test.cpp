// The content hash stream (no game): dinput8.dll first, then the scripts in ascending byte order of their lower case names,
// each as name, 0x00, size, bytes; an unreadable file counts as size 0xFFFFFFFF.
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../src/content_hash_rules.h"
#include "../src/script_override_rules.h"

namespace {

using content_hash_rules::File;
using content_hash_rules::Result;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

using Bytes = std::vector<uint8_t>;

void append(Bytes& stream, const std::string& text) { stream.insert(stream.end(), text.begin(), text.end()); }

void appendSize(Bytes& stream, uint32_t size) {
    for (int i = 0; i < 4; ++i) stream.push_back(static_cast<uint8_t>(size >> (8 * i)));
}

uint64_t hashOf(const Bytes& stream) { return fnv1a::hash64(stream.data(), stream.size()); }

// The scripts by lower case name; a name the map lacks cannot be read.
content_hash_rules::ReadScript readerOf(const std::map<std::string, Bytes>& files) {
    return [&files](const std::string& name) -> File {
        const auto found = files.find(name);
        if (found == files.end()) return std::nullopt;
        return found->second;
    };
}

}  // namespace

int main() {
    const Bytes dll = {0x4D, 0x5A, 0x90};
    const std::map<std::string, Bytes> files = {{"b.lua", {2, 2}}, {"a.lua", {1}}, {"ug_lib.lua", {}}};
    const std::set<std::string> names = {"b.lua", "a.lua", "ug_lib.lua"};

    Bytes expected;
    appendSize(expected, 3);
    expected.insert(expected.end(), dll.begin(), dll.end());
    for (const char* name : {"a.lua", "b.lua", "ug_lib.lua"}) {
        append(expected, name);
        expected.push_back(0);
        appendSize(expected, static_cast<uint32_t>(files.at(name).size()));
        expected.insert(expected.end(), files.at(name).begin(), files.at(name).end());
    }
    const Result result = content_hash_rules::compute(dll, names, readerOf(files));
    check(result.hash == hashOf(expected), "the stream is dinput8.dll, then each script as name, 0x00, size, bytes, in name order");
    check(result.dllBytes == 3 && result.scripts == 3 && result.unreadable.empty(), "the counts are the dll bytes and the scripts");

    const std::set<std::string> shuffled = {"ug_lib.lua", "a.lua", "b.lua"};
    check(content_hash_rules::compute(dll, shuffled, readerOf(files)).hash == result.hash,
          "the order is by name, not the order the names were found in");
    Bytes swapped;
    appendSize(swapped, 3);
    swapped.insert(swapped.end(), dll.begin(), dll.end());
    for (const char* name : {"b.lua", "a.lua", "ug_lib.lua"}) {
        append(swapped, name);
        swapped.push_back(0);
        appendSize(swapped, static_cast<uint32_t>(files.at(name).size()));
        swapped.insert(swapped.end(), files.at(name).begin(), files.at(name).end());
    }
    check(hashOf(swapped) != result.hash, "another order is another hash");

    Bytes scriptFirst;
    append(scriptFirst, "a.lua");
    scriptFirst.push_back(0);
    appendSize(scriptFirst, 1);
    scriptFirst.push_back(1);
    appendSize(scriptFirst, 3);
    scriptFirst.insert(scriptFirst.end(), dll.begin(), dll.end());
    check(content_hash_rules::compute(dll, {"a.lua"}, readerOf(files)).hash != hashOf(scriptFirst),
          "dinput8.dll comes before the scripts");

    check(content_hash_rules::compute(dll, {script_override_rules::lowered("A.LUA"), script_override_rules::lowered("B.Lua"),
                                            script_override_rules::lowered("UG_LIB.lua")},
                                      readerOf(files)).hash == result.hash,
          "names are the lower case ones script_override keeps, so a file's case does not change the hash");

    Bytes another = files.at("a.lua");
    another.push_back(9);
    std::map<std::string, Bytes> changed = files;
    changed["a.lua"] = another;
    check(content_hash_rules::compute(dll, names, readerOf(changed)).hash != result.hash, "a changed script changes the hash");
    check(content_hash_rules::compute(Bytes{0x4D, 0x5A, 0x91}, names, readerOf(files)).hash != result.hash,
          "a changed dinput8.dll changes the hash");
    std::map<std::string, Bytes> withExtra = files;
    withExtra["c.lua"] = {};
    check(content_hash_rules::compute(dll, {"a.lua", "b.lua", "c.lua", "ug_lib.lua"}, readerOf(withExtra)).hash != result.hash,
          "an extra script changes the hash");

    Bytes missing;
    appendSize(missing, 3);
    missing.insert(missing.end(), dll.begin(), dll.end());
    append(missing, "gone.lua");
    missing.push_back(0);
    appendSize(missing, content_hash_rules::kUnreadableSize);
    const Result unreadable = content_hash_rules::compute(dll, {"gone.lua"}, readerOf({}));
    check(unreadable.hash == hashOf(missing), "an unreadable script is its name and the size 0xFFFFFFFF, no bytes");
    check(unreadable.unreadable == std::vector<std::string>{"gone.lua"} && unreadable.scripts == 1, "and it is named");
    const Result noDll = content_hash_rules::compute(std::nullopt, names, readerOf(files));
    check(noDll.unreadable == std::vector<std::string>{"dinput8.dll"} && noDll.dllBytes == 0 && noDll.hash != result.hash,
          "an unreadable dinput8.dll is named and counts as 0xFFFFFFFF");

    Bytes empty;
    appendSize(empty, 3);
    empty.insert(empty.end(), dll.begin(), dll.end());
    const Result noScripts = content_hash_rules::compute(dll, {}, readerOf({}));
    check(noScripts.hash == hashOf(empty) && noScripts.scripts == 0, "an empty or missing folder is dinput8.dll alone");
    return g_failures == 0 ? 0 : 1;
}
