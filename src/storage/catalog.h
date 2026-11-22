#pragma once
// catalog.db: app settings and the list of books with their wrapped keys (ADR-0005, ADR-0007).
#include <string>
#include <vector>

#include "core/model.h"
#include "crypto/crypto.h"
#include "storage/sqlite.h"

namespace fp {

enum class KeyMode : int { Dpapi = 1, Pin = 2 };

struct BookRecord {
    i64 id = 0;
    std::string uuid;
    std::wstring name;
    BookKind kind = BookKind::Personal;
    std::string currency = "CZK";
    int color = 0;
    Icon icon = Icon::Wallet;
    int sort = 0;
    bool archived = false;
    i64 createdAt = 0;

    KeyMode keyMode = KeyMode::Dpapi;
    Bytes wrappedKey;          // DPAPI blob, or AES-GCM wrap under Argon2id(PIN)
    Bytes kdfSalt;
    crypto::KdfParams kdf;
    int failCount = 0;
    i64 lockUntil = 0;         // unix seconds

    bool hasPin() const { return keyMode == KeyMode::Pin; }
};

class Catalog {
public:
    Status open(const std::wstring& path);
    void close() { db_.close(); }

    std::vector<BookRecord> books();
    bool getBook(i64 id, BookRecord& out);
    i64 insertBook(const BookRecord& b);
    bool updateBookInfo(const BookRecord& b);    // name, kind, currency, color, icon, sort, archived
    bool updateBookKey(const BookRecord& b);     // key mode, wrapped key, kdf
    bool updatePinState(i64 id, int failCount, i64 lockUntil);
    bool removeBook(i64 id);

    std::wstring setting(const std::string& key, const std::wstring& def = {});
    int settingInt(const std::string& key, int def);
    void setSetting(const std::string& key, const std::wstring& value);
    void setSettingInt(const std::string& key, int v) { setSetting(key, std::to_wstring(v)); }
    std::vector<std::pair<std::string, std::wstring>> allSettings();

private:
    Db db_;
};

}  // namespace fp
