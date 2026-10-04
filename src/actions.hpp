// actions.hpp -- carrying out the items the user ticked.
//
// Every file is checked before it is used: its size and SHA-256 against what
// the publisher published, or its Authenticode signature against the
// publisher's name. A file that fails is not used. Every file this program
// puts on disk is recorded, and every file it replaces is copied aside first,
// so "Undo last installation" can put things back the way they were.
#pragma once

#include "planner.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace sdi {

struct ActionContext {
    std::function<void(const std::wstring&)> log;
    std::function<void(uint64_t got, uint64_t total)> progress;
    // Asked before a step that needs the user's attention (Zadig). Empty: yes.
    std::function<bool(const std::wstring& title, const std::wstring& text)> confirm;
    const std::atomic<bool>* cancel = nullptr;
    std::wstring payloadDir;  // payload\ beside this program: the offline package
    std::wstring cacheDir;    // what was fetched, kept for next time
    std::wstring backupsDir;  // backups and undo information, one folder per run
};

// The folders above, with a log that also goes to log.txt in the data folder.
ActionContext defaultContext();

bool verifyPackageFile(const Package& p, const std::wstring& path, std::wstring& err);
// A checked copy on disk: payload\ first (when the package may be there), then
// the cache, otherwise fetched from the publisher's own address.
bool obtainPackage(const Package& p, ActionContext& c, std::wstring& path, std::wstring& err);
bool unpackArchive(const std::wstring& archive, ActionContext& c, std::wstring& dir, std::wstring& err);
std::wstring findRtlDll(const std::wstring& unpackedDir, Arch a);
// Only when the package is already on disk -- never fetches anything.
RtlReference rtlReference(ActionContext& c);

class Transaction {
public:
    explicit Transaction(std::wstring backupsDir) : root_(std::move(backupsDir)) {}
    // Puts `from` at `to`. An existing `to` is copied into this run's backup folder first.
    // `changed` is false when `to` already was exactly that file.
    bool install(const std::wstring& from, const std::wstring& to, bool& changed, std::wstring& err);
    size_t changes() const { return changes_; }
    const std::wstring& dir() const { return dir_; }

private:
    bool open(std::wstring& err);
    std::wstring root_, dir_, manifest_;
    size_t changes_ = 0;
};

bool hasUndo(const std::wstring& backupsDir);
// Puts back what the most recent run changed. A file changed since then is left alone.
bool undoLatest(const std::wstring& backupsDir, const std::function<void(const std::wstring&)>& log,
                std::wstring& err);

struct RunResult {
    int done = 0, failed = 0, skipped = 0;
    std::wstring backupDir;
};
RunResult runPlan(const std::vector<PlanItem>& items, ActionContext& c);

std::wstring zadigInstructions();

}  // namespace sdi
