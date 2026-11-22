#include "storage/sqlite.h"

#include "core/text.h"
#include "sqlite3mc_amalgamation.h"

namespace fp {

Stmt::Stmt(sqlite3* db, std::string_view sql) {
    if (db) sqlite3_prepare_v3(db, sql.data(), (int)sql.size(), 0, &s_, nullptr);
}

Stmt::~Stmt() {
    if (s_) sqlite3_finalize(s_);
}

Stmt& Stmt::operator=(Stmt&& o) noexcept {
    if (this != &o) {
        if (s_) sqlite3_finalize(s_);
        s_ = o.s_;
        o.s_ = nullptr;
    }
    return *this;
}

Stmt& Stmt::bind(int i, i64 v) { if (s_) sqlite3_bind_int64(s_, i, v); return *this; }
Stmt& Stmt::bind(int i, double v) { if (s_) sqlite3_bind_double(s_, i, v); return *this; }
Stmt& Stmt::bind(int i, std::wstring_view v) { return bind(i, std::string_view(toUtf8(v))); }
Stmt& Stmt::bind(int i, std::string_view v) {
    if (s_) sqlite3_bind_text(s_, i, v.data(), (int)v.size(), SQLITE_TRANSIENT);
    return *this;
}
Stmt& Stmt::bindBlob(int i, const void* p, size_t n) {
    if (s_) sqlite3_bind_blob(s_, i, p ? p : "", (int)n, SQLITE_TRANSIENT);
    return *this;
}
Stmt& Stmt::bindNull(int i) { if (s_) sqlite3_bind_null(s_, i); return *this; }

bool Stmt::step() {
    if (!s_) return false;
    rc_ = sqlite3_step(s_);
    return rc_ == SQLITE_ROW;
}

bool Stmt::run() {
    if (!s_) return false;
    do { rc_ = sqlite3_step(s_); } while (rc_ == SQLITE_ROW);
    return rc_ == SQLITE_DONE;
}

void Stmt::reset() {
    if (s_) { sqlite3_reset(s_); sqlite3_clear_bindings(s_); }
}

int Stmt::columns() const { return s_ ? sqlite3_column_count(s_) : 0; }
int Stmt::type(int c) const { return s_ ? sqlite3_column_type(s_, c) : SQLITE_NULL; }
bool Stmt::isNull(int c) const { return type(c) == SQLITE_NULL; }
i64 Stmt::i(int c) const { return s_ ? sqlite3_column_int64(s_, c) : 0; }
double Stmt::d(int c) const { return s_ ? sqlite3_column_double(s_, c) : 0; }
std::string Stmt::utf8(int c) const {
    if (!s_) return {};
    auto* t = reinterpret_cast<const char*>(sqlite3_column_text(s_, c));
    return t ? std::string(t, sqlite3_column_bytes(s_, c)) : std::string();
}
std::wstring Stmt::text(int c) const { return fromUtf8(utf8(c)); }
Bytes Stmt::blob(int c) const {
    if (!s_) return {};
    auto* p = static_cast<const u8*>(sqlite3_column_blob(s_, c));
    int n = sqlite3_column_bytes(s_, c);
    return p ? Bytes(p, p + n) : Bytes();
}
std::string Stmt::name(int c) const {
    const char* n = s_ ? sqlite3_column_name(s_, c) : nullptr;
    return n ? n : "";
}

Status Db::open(const std::wstring& path, const u8* key, bool create) {
    close();
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | (create ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(toUtf8(path).c_str(), &h_, flags, nullptr) != SQLITE_OK) {
        std::string e = lastError();
        close();
        return Status::fail(Ec::Io, e);
    }
    if (key) {
        // The 32-byte key is random (DEK), so the cipher's own KDF adds nothing: keep it at 1 iteration
        // for instant open. The value must stay identical between runs (it is not stored in the file).
        sqlite3mc_config(h_, "cipher", sqlite3mc_cipher_index("chacha20"));
        sqlite3mc_config_cipher(h_, "chacha20", "kdf_iter", 1);
        if (sqlite3_key(h_, key, 32) != SQLITE_OK) { close(); return Status::fail(Ec::Crypto, "key"); }
    }
    // Wrong key surfaces on first read as SQLITE_NOTADB.
    if (!exec("PRAGMA foreign_keys=ON") || !exec("SELECT count(*) FROM sqlite_schema")) {
        std::string e = lastError();
        close();
        return Status::fail(key ? Ec::Corrupt : Ec::Io, e);
    }
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=FULL");
    exec("PRAGMA secure_delete=ON");
    exec("PRAGMA temp_store=MEMORY");
    return {};
}

void Db::close() {
    if (h_) {
        sqlite3_close_v2(h_);
        h_ = nullptr;
    }
}

bool Db::exec(std::string_view sql) {
    if (!h_) return false;
    std::string s(sql);
    return sqlite3_exec(h_, s.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

i64 Db::lastId() const { return h_ ? sqlite3_last_insert_rowid(h_) : 0; }
int Db::changes() const { return h_ ? sqlite3_changes(h_) : 0; }
std::string Db::lastError() const { return h_ ? sqlite3_errmsg(h_) : "not open"; }

int Db::userVersion() {
    Stmt s = prep("PRAGMA user_version");
    return s.step() ? (int)s.i(0) : 0;
}

void Db::setUserVersion(int v) { exec("PRAGMA user_version=" + std::to_string(v)); }

}  // namespace fp
