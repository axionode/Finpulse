#pragma once
// One encrypted SQLite file per book (ADR-0005).
#include <string>
#include <vector>

#include "core/model.h"
#include "storage/sqlite.h"

namespace fp {

struct BookMeta {
    std::string uuid;
    std::wstring name;
    BookKind kind = BookKind::Personal;
    std::string currency = "CZK";
};

struct SeedCategory {
    CatType type;
    std::wstring name;
    Icon icon;
    int color;
};

constexpr int kBookSchemaVersion = 1;

class BookDb {
public:
    Status create(const std::wstring& path, const u8* key, const BookMeta& meta,
                  const std::vector<SeedCategory>& seed, const std::wstring& defaultAccountName);
    Status open(const std::wstring& path, const u8* key);
    void close() { db_.close(); }
    bool isOpen() const { return db_.isOpen(); }
    Db& db() { return db_; }

    // schema: used by create() and by backup import
    bool createSchema();
    bool migrate();

    // meta (copy of the catalog entry, makes the file self-describing)
    void setMeta(const std::string& key, const std::wstring& value);
    std::wstring meta(const std::string& key);

    // accounts
    std::vector<Account> accounts(bool includeArchived = false);
    i64 saveAccount(const Account& a);
    i64 totalBalance();

    // categories
    std::vector<Category> categories(bool includeArchived = true);
    i64 saveCategory(const Category& c);
    bool mergeCategory(i64 from, i64 to);
    int categoryUseCount(i64 id);

    // transactions
    i64 saveTransaction(const Transaction& t);  // insert (id == 0) or replace
    bool softDelete(i64 id);
    bool restore(i64 id);
    bool hardDelete(i64 id);
    bool getTransaction(i64 id, Transaction& out);
    std::vector<Transaction> transactions(const Period& p, int limit = 0);
    std::vector<Transaction> recent(int limit);
    std::vector<Transaction> trash();
    void purgeTrash(int olderThanDays);
    int transactionCount();
    Date firstDate();

    // aggregates
    Totals totals(const Period& p);
    std::vector<CategorySum> byCategory(const Period& p, CatType type);
    std::vector<Bucket> series(const Period& p, BucketSize size);
    std::vector<std::wstring> payees(const std::wstring& prefix, int limit);
    i64 lastCategoryForPayee(const std::wstring& payee, TxKind kind);

private:
    Db db_;
};

}  // namespace fp
