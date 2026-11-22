#pragma once
// Owns the catalog, resolves the data folder and manages book files, keys and PINs (ADR-0005, ADR-0007).
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "services/i18n.h"
#include "storage/book.h"
#include "storage/catalog.h"

namespace fp {

struct DataLocation {
    std::wstring dir;         // ...\Finpulse Data
    bool portable = true;     // next to the exe
};

DataLocation resolveDataLocation();
std::wstring exeDirectory();
std::wstring exePath();
bool ensureDir(const std::wstring& dir);
bool writeFileAtomic(const std::wstring& path, const Bytes& data);
bool readFile(const std::wstring& path, Bytes& out);

struct NewBook {
    std::wstring name;
    BookKind kind = BookKind::Personal;
    std::string currency = "CZK";
    int color = 0;
    Icon icon = Icon::Wallet;
};

// A book that is currently decrypted and open.
struct OpenedBook {
    BookRecord rec;
    BookDb db;
    crypto::Key key{};
    OpenedBook() { crypto::lockMemory(key.data(), key.size()); }
    ~OpenedBook() { db.close(); wipe(key.data(), key.size()); crypto::unlockMemory(key.data(), key.size()); }
};

class Library {
public:
    Status init();
    const DataLocation& location() const { return loc_; }
    Catalog& catalog() { return catalog_; }
    std::wstring booksDir() const { return loc_.dir + L"\\books"; }
    std::wstring backupsDir() const { return loc_.dir + L"\\Backups"; }
    std::wstring bookPath(const std::string& uuid) const { return booksDir() + L"\\" + fromUtf8Str(uuid) + L".fpbook"; }

    std::vector<BookRecord> books() { return catalog_.books(); }
    Result<i64> createBook(const NewBook& nb);
    Status deleteBook(i64 id);
    Status updateBookInfo(const BookRecord& r);

    // Opening. For PIN books pin must be 4 digits; for others pass empty.
    Status open(i64 id, const std::wstring& pin);
    void closeCurrent() { current_.reset(); }
    OpenedBook* current() { return current_.get(); }

    // PIN management on the currently open book.
    Status setPin(const std::wstring& pin);
    Status removePin();
    int throttleSeconds(const BookRecord& r) const;

    // Reads the DEK of any book: DPAPI books directly, PIN books with pin, the open book from memory.
    Result<crypto::Key> bookKey(const BookRecord& r, const std::wstring& pin);
    Status keyFromPin(const BookRecord& r, const std::wstring& pin, crypto::Key& out);

    // Balance shown on the start screen for books without PIN.
    std::optional<i64> peekBalance(const BookRecord& r, int* txCount);

    static bool validPin(const std::wstring& pin);
    static Bytes keyAad(const std::string& uuid);
    static std::wstring fromUtf8Str(const std::string& s);
    static std::string newUuid();

private:
    DataLocation loc_;
    Catalog catalog_;
    std::unique_ptr<OpenedBook> current_;
};

}  // namespace fp
