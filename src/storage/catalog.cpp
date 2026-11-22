#include "storage/catalog.h"

#include <cstdlib>

namespace fp {

namespace {
const char* kCatalogSchema = R"SQL(
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS books(
  id INTEGER PRIMARY KEY,
  uuid TEXT NOT NULL UNIQUE,
  name TEXT NOT NULL,
  kind INTEGER NOT NULL DEFAULT 0,
  currency TEXT NOT NULL,
  color INTEGER NOT NULL DEFAULT 0,
  icon INTEGER NOT NULL DEFAULT 0,
  sort INTEGER NOT NULL DEFAULT 0,
  archived INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  key_mode INTEGER NOT NULL,
  wrapped_key BLOB NOT NULL,
  kdf_salt BLOB,
  kdf_m INTEGER NOT NULL DEFAULT 0,
  kdf_t INTEGER NOT NULL DEFAULT 0,
  kdf_p INTEGER NOT NULL DEFAULT 0,
  fail_count INTEGER NOT NULL DEFAULT 0,
  lock_until INTEGER NOT NULL DEFAULT 0);
)SQL";
}

Status Catalog::open(const std::wstring& path) {
    Status st = db_.open(path, nullptr, true);
    if (!st) return st;
    if (!db_.exec(kCatalogSchema)) return Status::fail(Ec::Io, db_.lastError());
    if (db_.userVersion() == 0) db_.setUserVersion(1);
    return {};
}

std::vector<BookRecord> Catalog::books() {
    std::vector<BookRecord> out;
    Stmt s = db_.prep("SELECT id,uuid,name,kind,currency,color,icon,sort,archived,created_at,key_mode,wrapped_key,"
                      "kdf_salt,kdf_m,kdf_t,kdf_p,fail_count,lock_until FROM books ORDER BY archived, sort, id");
    while (s.step()) {
        BookRecord b;
        b.id = s.i(0);
        b.uuid = s.utf8(1);
        b.name = s.text(2);
        b.kind = static_cast<BookKind>(s.i(3));
        b.currency = s.utf8(4);
        b.color = (int)s.i(5);
        b.icon = static_cast<Icon>(s.i(6));
        b.sort = (int)s.i(7);
        b.archived = s.i(8) != 0;
        b.createdAt = s.i(9);
        b.keyMode = static_cast<KeyMode>(s.i(10));
        b.wrappedKey = s.blob(11);
        b.kdfSalt = s.blob(12);
        b.kdf.memKiB = (u32)s.i(13);
        b.kdf.passes = (u32)s.i(14);
        b.kdf.lanes = (u32)s.i(15);
        b.failCount = (int)s.i(16);
        b.lockUntil = s.i(17);
        out.push_back(std::move(b));
    }
    return out;
}

bool Catalog::getBook(i64 id, BookRecord& out) {
    for (auto& b : books())
        if (b.id == id) { out = b; return true; }
    return false;
}

i64 Catalog::insertBook(const BookRecord& b) {
    Stmt s = db_.prep("INSERT INTO books(uuid,name,kind,currency,color,icon,sort,archived,created_at,key_mode,wrapped_key,"
                      "kdf_salt,kdf_m,kdf_t,kdf_p) VALUES(?,?,?,?,?,?,IFNULL((SELECT MAX(sort)+1 FROM books),0),?,?,?,?,?,?,?,?)");
    s.bind(1, b.uuid).bind(2, b.name).bind(3, (int)b.kind).bind(4, b.currency).bind(5, b.color).bind(6, (int)b.icon)
        .bind(7, b.archived ? 1 : 0).bind(8, b.createdAt).bind(9, (int)b.keyMode)
        .bindBlob(10, b.wrappedKey.data(), b.wrappedKey.size()).bindBlob(11, b.kdfSalt.data(), b.kdfSalt.size())
        .bind(12, (i64)b.kdf.memKiB).bind(13, (i64)b.kdf.passes).bind(14, (i64)b.kdf.lanes);
    return s.run() ? db_.lastId() : 0;
}

bool Catalog::updateBookInfo(const BookRecord& b) {
    return db_.prep("UPDATE books SET name=?,kind=?,currency=?,color=?,icon=?,sort=?,archived=? WHERE id=?")
        .bind(1, b.name).bind(2, (int)b.kind).bind(3, b.currency).bind(4, b.color).bind(5, (int)b.icon)
        .bind(6, b.sort).bind(7, b.archived ? 1 : 0).bind(8, b.id).run();
}

bool Catalog::updateBookKey(const BookRecord& b) {
    return db_.prep("UPDATE books SET key_mode=?,wrapped_key=?,kdf_salt=?,kdf_m=?,kdf_t=?,kdf_p=?,fail_count=0,lock_until=0 "
                    "WHERE id=?")
        .bind(1, (int)b.keyMode).bindBlob(2, b.wrappedKey.data(), b.wrappedKey.size())
        .bindBlob(3, b.kdfSalt.data(), b.kdfSalt.size()).bind(4, (i64)b.kdf.memKiB).bind(5, (i64)b.kdf.passes)
        .bind(6, (i64)b.kdf.lanes).bind(7, b.id).run();
}

bool Catalog::updatePinState(i64 id, int failCount, i64 lockUntil) {
    return db_.prep("UPDATE books SET fail_count=?, lock_until=? WHERE id=?").bind(1, failCount).bind(2, lockUntil).bind(3, id).run();
}

bool Catalog::removeBook(i64 id) { return db_.prep("DELETE FROM books WHERE id=?").bind(1, id).run(); }

std::wstring Catalog::setting(const std::string& key, const std::wstring& def) {
    Stmt s = db_.prep("SELECT value FROM settings WHERE key=?");
    s.bind(1, key);
    return s.step() ? s.text(0) : def;
}

int Catalog::settingInt(const std::string& key, int def) {
    std::wstring v = setting(key);
    if (v.empty()) return def;
    return (int)std::wcstol(v.c_str(), nullptr, 10);
}

void Catalog::setSetting(const std::string& key, const std::wstring& value) {
    db_.prep("INSERT INTO settings(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value")
        .bind(1, key).bind(2, value).run();
}

std::vector<std::pair<std::string, std::wstring>> Catalog::allSettings() {
    std::vector<std::pair<std::string, std::wstring>> out;
    Stmt s = db_.prep("SELECT key, value FROM settings ORDER BY key");
    while (s.step()) out.emplace_back(s.utf8(0), s.text(1));
    return out;
}

}  // namespace fp
