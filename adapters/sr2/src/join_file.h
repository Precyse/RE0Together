#pragma once
#include <optional>
#include <string>
#include <utility>

// <game dir>\coop\sr2_join.txt is how the launcher hands the adapter a session (docs/SR2_NOTES.md "Force join"): lines
// `role=host|guest`, `token=<digits>` (the Steam lobby id), `password=<digits>` and the optional `name=<UTF-8>` (the
// player's name in the game, shaped by the launcher: docs/CONTRACT.md "Join file"). No file, or a file without a valid
// role, token and password, means force join does nothing; a name longer than kMaxNameBytes is dropped, not the request.
namespace join_file {

enum class Role { Host, Guest };

struct JoinRequest {
    Role role = Role::Guest;
    std::string token;
    std::string password;
    std::string name;  // empty = the game keeps its own name

    bool operator==(const JoinRequest&) const = default;
};

inline constexpr const wchar_t* kJoinFileName = L"sr2_join.txt";
inline constexpr size_t kMaxDigits = 20;  // a 64 bit lobby id
inline constexpr size_t kMaxNameBytes = 64;

// The request in the file's text, or none when a key is missing or its value is not valid.
std::optional<JoinRequest> parse(const std::string& text);

// Follows the file by its last write time: poll() is true when the file appeared, disappeared or was rewritten (also
// with the same content: the launcher's Join pressed again), and request() then holds the new content.
class Watcher {
public:
    explicit Watcher(std::wstring path) : path_(std::move(path)) {}

    bool poll();
    const std::optional<JoinRequest>& request() const { return request_; }

private:
    std::wstring path_;
    bool exists_ = false;
    unsigned long long writeTime_ = 0;
    std::optional<JoinRequest> request_;
};

}  // namespace join_file
