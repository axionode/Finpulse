#pragma once
// ADR-0009: one .fpbackup file with every book (raw encrypted files + keys) and all settings.
#include <string>
#include <vector>

#include "services/library.h"

namespace fp {

enum class BackupMode : u8 { Plain = 0, Password = 1, Dpapi = 2 };

struct BackupBookInfo {
    BookRecord rec;
    bool hasKey = false;      // DEK included (book can be re-keyed / PIN reset)
    size_t fileSize = 0;
};

struct BackupPreview {
    BackupMode mode = BackupMode::Plain;
    i64 created = 0;
    std::string appVersion;
    std::vector<BackupBookInfo> books;
};

// pins: PINs entered for locked books (book id -> PIN); books without key are exported with their PIN wrap only.
Status exportBackup(Library& lib, const std::wstring& path, BackupMode mode, const std::wstring& password,
                    const std::vector<std::pair<i64, std::wstring>>& pins);

// Reads header to know whether a password is needed.
Result<BackupMode> backupMode(const std::wstring& path);
Result<BackupPreview> readBackupPreview(const std::wstring& path, const std::wstring& password);

enum class ImportMode { Add, Replace };
Status importBackup(Library& lib, const std::wstring& path, const std::wstring& password, ImportMode mode,
                    bool resetPins, int* importedBooks);

// Automatic backup on exit into Backups\, DPAPI-protected, keeps the newest 14 files.
Status autoBackup(Library& lib);

}  // namespace fp
