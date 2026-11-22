#pragma once
// Thin RAII wrapper over SQLite3 Multiple Ciphers.
#include <string>
#include <string_view>

#include "core/base.h"

struct sqlite3;
struct sqlite3_stmt;

namespace fp {

class Stmt {
public:
    Stmt() = default;
    Stmt(sqlite3* db, std::string_view sql);
    ~Stmt();
    Stmt(Stmt&& o) noexcept : s_(o.s_) { o.s_ = nullptr; }
    Stmt& operator=(Stmt&& o) noexcept;
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool valid() const { return s_ != nullptr; }
    Stmt& bind(int i, i64 v);
    Stmt& bind(int i, int v) { return bind(i, (i64)v); }
    Stmt& bind(int i, double v);
    Stmt& bind(int i, std::wstring_view v);
    Stmt& bind(int i, std::string_view v);
    Stmt& bind(int i, const char* v) { return bind(i, std::string_view(v)); }
    Stmt& bind(int i, const wchar_t* v) { return bind(i, std::wstring_view(v)); }
    Stmt& bindBlob(int i, const void* p, size_t n);
    Stmt& bindNull(int i);

    bool step();   // true while a row is available
    bool run();    // executes to completion, true on success
    void reset();

    int columns() const;
    int type(int c) const;  // SQLITE_INTEGER etc.
    bool isNull(int c) const;
    i64 i(int c) const;
    double d(int c) const;
    std::wstring text(int c) const;
    std::string utf8(int c) const;
    Bytes blob(int c) const;
    std::string name(int c) const;

private:
    sqlite3_stmt* s_ = nullptr;
    int rc_ = 0;
};

class Db {
public:
    Db() = default;
    ~Db() { close(); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // key == nullptr: plain database (catalog). Otherwise 32-byte key, ChaCha20-Poly1305 (ADR-0007).
    Status open(const std::wstring& path, const u8* key, bool create);
    void close();
    bool isOpen() const { return h_ != nullptr; }

    bool exec(std::string_view sql);
    Stmt prep(std::string_view sql) { return Stmt(h_, sql); }
    i64 lastId() const;
    int changes() const;
    std::string lastError() const;
    int userVersion();
    void setUserVersion(int v);

    bool begin() { return exec("BEGIN IMMEDIATE"); }
    bool commit() { return exec("COMMIT"); }
    void rollback() { exec("ROLLBACK"); }

    sqlite3* raw() const { return h_; }

private:
    sqlite3* h_ = nullptr;
};

// Rolls back automatically unless commit() was called.
class Tx {
public:
    explicit Tx(Db& db) : db_(db) { ok_ = db_.begin(); }
    ~Tx() { if (ok_ && !done_) db_.rollback(); }
    bool ok() const { return ok_; }
    bool commit() { done_ = true; return ok_ && db_.commit(); }
private:
    Db& db_;
    bool ok_ = false, done_ = false;
};

}  // namespace fp
