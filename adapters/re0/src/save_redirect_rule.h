#pragma once

// Which copy of a session save file the guest's game reads and writes (save_redirect.cpp, unit tested): the session
// folder's from before the launch, or the staged one the host sent while the game runs, which is newer. Pure.
namespace save_redirect_rule {

enum class Source { Steam, Session, Staged };

// `inSession`: coop\session holds the file (this game serves the host's session); `staged`: coop\session\staging does.
constexpr Source source(bool inSession, bool staged) {
    if (!inSession) return Source::Steam;
    return staged ? Source::Staged : Source::Session;
}

}  // namespace save_redirect_rule
