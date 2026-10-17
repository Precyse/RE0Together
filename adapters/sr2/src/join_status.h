#pragma once
#include <string>

// What force join tells the launcher: one line `state=<s> detail=<d>` in <game>\coop\sr2_status.txt, rewritten when it
// changes. The launcher passes it on to the other lobby members.
namespace join_status {

struct Status {
    std::string state;
    std::string detail;  // may be empty

    bool operator==(const Status&) const = default;
};

inline constexpr const wchar_t* kStatusFileName = L"sr2_status.txt";

// The file's text for a status, without the line end.
inline std::string statusLine(const Status& status) {
    std::string line = "state=" + status.state;
    if (!status.detail.empty()) line += " detail=" + status.detail;
    return line;
}

// Chooses the file (<coop dir>\sr2_status.txt) and whether each change is also logged. Call once, before publish.
void open(bool logChanges);

// Thread-safe. Writes the file (temp file, then rename) when `status` differs from the last one written.
void publish(const Status& status);

// Thread-safe. The next publish writes even an unchanged status: the launcher removes the file when it starts a session.
void writeNext();

}  // namespace join_status
