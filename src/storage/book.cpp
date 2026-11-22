#include "storage/book.h"

#include <algorithm>
#include <map>

#include "core/text.h"

namespace fp {

namespace {

const char* kSchemaV1 = R"SQL(
CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE accounts(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  type INTEGER NOT NULL DEFAULT 0,
  opening INTEGER NOT NULL DEFAULT 0,
  color INTEGER NOT NULL DEFAULT 0,
  icon INTEGER NOT NULL DEFAULT 0,
  archived INTEGER NOT NULL DEFAULT 0,
  sort INTEGER NOT NULL DEFAULT 0);
CREATE TABLE categories(
  id INTEGER PRIMARY KEY,
  type INTEGER NOT NULL,
  parent_id INTEGER REFERENCES categories(id),
  name TEXT NOT NULL,
  icon INTEGER NOT NULL DEFAULT 0,
  color INTEGER NOT NULL DEFAULT 0,
  archived INTEGER NOT NULL DEFAULT 0,
  sort INTEGER NOT NULL DEFAULT 0);
CREATE TABLE transactions(
  id INTEGER PRIMARY KEY,
  kind INTEGER NOT NULL,
  date INTEGER NOT NULL,
  payee TEXT NOT NULL DEFAULT '',
  note TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  deleted_at INTEGER);
CREATE TABLE lines(
  id INTEGER PRIMARY KEY,
  tx_id INTEGER NOT NULL REFERENCES transactions(id) ON DELETE CASCADE,
  account_id INTEGER NOT NULL REFERENCES accounts(id),
  category_id INTEGER REFERENCES categories(id),
  amount INTEGER NOT NULL);
CREATE INDEX ix_tx_date ON transactions(deleted_at, date);
CREATE INDEX ix_lines_tx ON lines(tx_id);
CREATE INDEX ix_lines_account ON lines(account_id);
CREATE INDEX ix_lines_category ON lines(category_id);
)SQL";

constexpr const char* kTxSelect =
    "SELECT t.id, t.kind, t.date, t.payee, t.note, t.deleted_at, l.id, l.account_id, "
    "IFNULL(l.category_id,0), l.amount FROM transactions t JOIN lines l ON l.tx_id = t.id ";

std::vector<Transaction> readTx(Stmt& s) {
    std::vector<Transaction> out;
    while (s.step()) {
        i64 id = s.i(0);
        if (out.empty() || out.back().id != id) {
            Transaction t;
            t.id = id;
            t.kind = static_cast<TxKind>(s.i(1));
            t.date = Date::fromKey((int)s.i(2));
            t.payee = s.text(3);
            t.note = s.text(4);
            t.deletedAt = s.i(5);
            out.push_back(std::move(t));
        }
        out.back().lines.push_back({s.i(6), s.i(7), s.i(8), s.i(9)});
    }
    return out;
}

}  // namespace

bool BookDb::createSchema() {
    if (!db_.exec(kSchemaV1)) return false;
    db_.setUserVersion(kBookSchemaVersion);
    return true;
}

bool BookDb::migrate() {
    int v = db_.userVersion();
    if (v > kBookSchemaVersion) return false;
    // Future migrations: if (v < 2) { ... v = 2; } each inside a transaction.
    return true;
}

Status BookDb::create(const std::wstring& path, const u8* key, const BookMeta& m, const std::vector<SeedCategory>& seed,
                      const std::wstring& defaultAccountName) {
    Status st = db_.open(path, key, true);
    if (!st) return st;
    Tx tx(db_);
    if (!createSchema()) return Status::fail(Ec::Io, db_.lastError());
    setMeta("uuid", fromUtf8(m.uuid));
    setMeta("name", m.name);
    setMeta("kind", std::to_wstring((int)m.kind));
    setMeta("currency", fromUtf8(m.currency));
    setMeta("created_at", std::to_wstring(unixNow()));
    int sort = 0;
    for (auto& c : seed) {
        Category cat;
        cat.type = c.type;
        cat.name = c.name;
        cat.icon = c.icon;
        cat.color = c.color;
        cat.sort = sort++;
        saveCategory(cat);
    }
    Account a;
    a.name = defaultAccountName;
    a.type = AccountType::Cash;
    a.icon = Icon::Wallet;
    saveAccount(a);
    if (!tx.commit()) return Status::fail(Ec::Io, db_.lastError());
    return {};
}

Status BookDb::open(const std::wstring& path, const u8* key) {
    Status st = db_.open(path, key, false);
    if (!st) return st;
    if (db_.userVersion() > kBookSchemaVersion) { db_.close(); return Status::fail(Ec::TooNew); }
    if (!migrate()) { db_.close(); return Status::fail(Ec::Corrupt, "migration"); }
    Stmt q = db_.prep("PRAGMA quick_check");
    if (!q.step() || q.utf8(0) != "ok") { db_.close(); return Status::fail(Ec::Corrupt, "quick_check"); }
    return {};
}

void BookDb::setMeta(const std::string& key, const std::wstring& value) {
    db_.prep("INSERT INTO meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value")
        .bind(1, key).bind(2, value).run();
}

std::wstring BookDb::meta(const std::string& key) {
    Stmt s = db_.prep("SELECT value FROM meta WHERE key=?");
    s.bind(1, key);
    return s.step() ? s.text(0) : std::wstring();
}

// ---------------------------------------------------------------- accounts

std::vector<Account> BookDb::accounts(bool includeArchived) {
    std::vector<Account> out;
    Stmt s = db_.prep(
        "SELECT a.id, a.name, a.type, a.opening, a.color, a.icon, a.archived, a.sort, "
        "a.opening + IFNULL((SELECT SUM(l.amount) FROM lines l JOIN transactions t ON t.id = l.tx_id "
        "  WHERE l.account_id = a.id AND t.deleted_at IS NULL), 0) "
        "FROM accounts a WHERE a.archived = 0 OR ?1 ORDER BY a.archived, a.sort, a.id");
    s.bind(1, includeArchived ? 1 : 0);
    while (s.step()) {
        Account a;
        a.id = s.i(0);
        a.name = s.text(1);
        a.type = static_cast<AccountType>(s.i(2));
        a.opening = s.i(3);
        a.color = (int)s.i(4);
        a.icon = static_cast<Icon>(s.i(5));
        a.archived = s.i(6) != 0;
        a.sort = (int)s.i(7);
        a.balance = s.i(8);
        out.push_back(std::move(a));
    }
    return out;
}

i64 BookDb::saveAccount(const Account& a) {
    if (a.id) {
        db_.prep("UPDATE accounts SET name=?,type=?,opening=?,color=?,icon=?,archived=?,sort=? WHERE id=?")
            .bind(1, a.name).bind(2, (int)a.type).bind(3, a.opening).bind(4, a.color).bind(5, (int)a.icon)
            .bind(6, a.archived ? 1 : 0).bind(7, a.sort).bind(8, a.id).run();
        return a.id;
    }
    Stmt s = db_.prep("INSERT INTO accounts(name,type,opening,color,icon,archived,sort) VALUES(?,?,?,?,?,?,"
                      "IFNULL((SELECT MAX(sort)+1 FROM accounts),0))");
    s.bind(1, a.name).bind(2, (int)a.type).bind(3, a.opening).bind(4, a.color).bind(5, (int)a.icon)
        .bind(6, a.archived ? 1 : 0);
    return s.run() ? db_.lastId() : 0;
}

i64 BookDb::totalBalance() {
    Stmt s = db_.prep(
        "SELECT IFNULL((SELECT SUM(opening) FROM accounts WHERE archived=0),0) + "
        "IFNULL((SELECT SUM(l.amount) FROM lines l JOIN transactions t ON t.id=l.tx_id "
        " JOIN accounts a ON a.id=l.account_id WHERE t.deleted_at IS NULL AND a.archived=0),0)");
    return s.step() ? s.i(0) : 0;
}

// ---------------------------------------------------------------- categories

std::vector<Category> BookDb::categories(bool includeArchived) {
    std::vector<Category> out;
    Stmt s = db_.prep("SELECT id,type,IFNULL(parent_id,0),name,icon,color,archived,sort FROM categories "
                      "WHERE archived=0 OR ?1 ORDER BY type, sort, id");
    s.bind(1, includeArchived ? 1 : 0);
    while (s.step()) {
        Category c;
        c.id = s.i(0);
        c.type = static_cast<CatType>(s.i(1));
        c.parent = s.i(2);
        c.name = s.text(3);
        c.icon = static_cast<Icon>(s.i(4));
        c.color = (int)s.i(5);
        c.archived = s.i(6) != 0;
        c.sort = (int)s.i(7);
        out.push_back(std::move(c));
    }
    return out;
}

i64 BookDb::saveCategory(const Category& c) {
    if (c.id) {
        Stmt s = db_.prep("UPDATE categories SET type=?,parent_id=?,name=?,icon=?,color=?,archived=?,sort=? WHERE id=?");
        s.bind(1, (int)c.type);
        if (c.parent) s.bind(2, c.parent); else s.bindNull(2);
        s.bind(3, c.name).bind(4, (int)c.icon).bind(5, c.color).bind(6, c.archived ? 1 : 0).bind(7, c.sort).bind(8, c.id);
        s.run();
        // children follow the parent's type
        db_.prep("UPDATE categories SET type=? WHERE parent_id=?").bind(1, (int)c.type).bind(2, c.id).run();
        return c.id;
    }
    Stmt s = db_.prep("INSERT INTO categories(type,parent_id,name,icon,color,archived,sort) VALUES(?,?,?,?,?,?,"
                      "IFNULL((SELECT MAX(sort)+1 FROM categories),0))");
    s.bind(1, (int)c.type);
    if (c.parent) s.bind(2, c.parent); else s.bindNull(2);
    s.bind(3, c.name).bind(4, (int)c.icon).bind(5, c.color).bind(6, c.archived ? 1 : 0);
    return s.run() ? db_.lastId() : 0;
}

bool BookDb::mergeCategory(i64 from, i64 to) {
    if (from == to) return false;
    Tx tx(db_);
    db_.prep("UPDATE lines SET category_id=? WHERE category_id=?").bind(1, to).bind(2, from).run();
    db_.prep("UPDATE categories SET parent_id=? WHERE parent_id=?").bind(1, to).bind(2, from).run();
    db_.prep("DELETE FROM categories WHERE id=?").bind(1, from).run();
    return tx.commit();
}

int BookDb::categoryUseCount(i64 id) {
    Stmt s = db_.prep("SELECT COUNT(*) FROM lines WHERE category_id=?");
    s.bind(1, id);
    return s.step() ? (int)s.i(0) : 0;
}

// ---------------------------------------------------------------- transactions

i64 BookDb::saveTransaction(const Transaction& t) {
    Tx tx(db_);
    i64 now = unixNow();
    i64 id = t.id;
    if (id) {
        db_.prep("UPDATE transactions SET kind=?,date=?,payee=?,note=?,updated_at=? WHERE id=?")
            .bind(1, (int)t.kind).bind(2, t.date.key()).bind(3, t.payee).bind(4, t.note).bind(5, now).bind(6, id).run();
        db_.prep("DELETE FROM lines WHERE tx_id=?").bind(1, id).run();
    } else {
        Stmt s = db_.prep("INSERT INTO transactions(kind,date,payee,note,created_at,updated_at) VALUES(?,?,?,?,?,?)");
        s.bind(1, (int)t.kind).bind(2, t.date.key()).bind(3, t.payee).bind(4, t.note).bind(5, now).bind(6, now);
        if (!s.run()) return 0;
        id = db_.lastId();
    }
    Stmt ins = db_.prep("INSERT INTO lines(tx_id,account_id,category_id,amount) VALUES(?,?,?,?)");
    for (auto& l : t.lines) {
        ins.reset();
        ins.bind(1, id).bind(2, l.account);
        if (l.category) ins.bind(3, l.category); else ins.bindNull(3);
        ins.bind(4, l.amount);
        if (!ins.run()) return 0;
    }
    return tx.commit() ? id : 0;
}

bool BookDb::softDelete(i64 id) {
    return db_.prep("UPDATE transactions SET deleted_at=? WHERE id=?").bind(1, unixNow()).bind(2, id).run();
}

bool BookDb::restore(i64 id) {
    return db_.prep("UPDATE transactions SET deleted_at=NULL WHERE id=?").bind(1, id).run();
}

bool BookDb::hardDelete(i64 id) {
    return db_.prep("DELETE FROM transactions WHERE id=?").bind(1, id).run();
}

bool BookDb::getTransaction(i64 id, Transaction& out) {
    Stmt s = db_.prep(std::string(kTxSelect) + "WHERE t.id=? ORDER BY l.id");
    s.bind(1, id);
    auto v = readTx(s);
    if (v.empty()) return false;
    out = std::move(v.front());
    return true;
}

std::vector<Transaction> BookDb::transactions(const Period& p, int limit) {
    std::string sql = std::string(kTxSelect) +
                      "WHERE t.deleted_at IS NULL AND t.date >= ? AND t.date < ? ORDER BY t.date DESC, t.id DESC, l.id";
    Stmt s = db_.prep(sql);
    s.bind(1, p.start.key()).bind(2, p.end.key());
    auto v = readTx(s);
    if (limit > 0 && (int)v.size() > limit) v.resize(limit);
    return v;
}

std::vector<Transaction> BookDb::recent(int limit) {
    Stmt s = db_.prep(std::string(kTxSelect) +
                      "WHERE t.id IN (SELECT id FROM transactions WHERE deleted_at IS NULL ORDER BY date DESC, id DESC "
                      "LIMIT ?) ORDER BY t.date DESC, t.id DESC, l.id");
    s.bind(1, limit);
    return readTx(s);
}

std::vector<Transaction> BookDb::trash() {
    Stmt s = db_.prep(std::string(kTxSelect) + "WHERE t.deleted_at IS NOT NULL ORDER BY t.deleted_at DESC, t.id, l.id");
    return readTx(s);
}

void BookDb::purgeTrash(int olderThanDays) {
    db_.prep("DELETE FROM transactions WHERE deleted_at IS NOT NULL AND deleted_at < ?")
        .bind(1, unixNow() - (i64)olderThanDays * 86400).run();
}

int BookDb::transactionCount() {
    Stmt s = db_.prep("SELECT COUNT(*) FROM transactions WHERE deleted_at IS NULL");
    return s.step() ? (int)s.i(0) : 0;
}

Date BookDb::firstDate() {
    Stmt s = db_.prep("SELECT MIN(date) FROM transactions WHERE deleted_at IS NULL");
    if (s.step() && !s.isNull(0)) return Date::fromKey((int)s.i(0));
    return Date::today();
}

// ---------------------------------------------------------------- aggregates

Totals BookDb::totals(const Period& p) {
    Totals t;
    Stmt s = db_.prep("SELECT t.kind, SUM(l.amount) FROM transactions t JOIN lines l ON l.tx_id=t.id "
                      "WHERE t.deleted_at IS NULL AND t.date>=? AND t.date<? AND t.kind IN (0,1) GROUP BY t.kind");
    s.bind(1, p.start.key()).bind(2, p.end.key());
    while (s.step()) {
        if (s.i(0) == (int)TxKind::Income) t.income = s.i(1);
        else t.expense = -s.i(1);
    }
    return t;
}

std::vector<CategorySum> BookDb::byCategory(const Period& p, CatType type) {
    std::vector<CategorySum> out;
    Stmt s = db_.prep(
        "SELECT IFNULL(c.parent_id, c.id) AS top, SUM(l.amount), COUNT(DISTINCT t.id) FROM transactions t "
        "JOIN lines l ON l.tx_id=t.id JOIN categories c ON c.id=l.category_id "
        "WHERE t.deleted_at IS NULL AND t.date>=? AND t.date<? AND t.kind=? GROUP BY top");
    s.bind(1, p.start.key()).bind(2, p.end.key()).bind(3, (int)(type == CatType::Income ? TxKind::Income : TxKind::Expense));
    while (s.step()) {
        i64 a = s.i(1);
        out.push_back({s.i(0), a < 0 ? -a : a, (int)s.i(2)});
    }
    std::sort(out.begin(), out.end(), [](const CategorySum& a, const CategorySum& b) { return a.amount > b.amount; });
    return out;
}

std::vector<Bucket> BookDb::series(const Period& p, BucketSize size) {
    std::vector<Bucket> out;
    Date start = p.start, end = p.end;
    if (p.kind == PeriodKind::All) {
        start = firstDate();
        start = {start.y, start.m, 1};
        Date t = Date::today();
        end = Date{t.y, t.m, 1}.addMonths(1);
    }
    if (size == BucketSize::Day) {
        for (Date d = start; d < end; d = d.addDays(1)) out.push_back({d, 0, 0});
    } else {
        for (Date d{start.y, start.m, 1}; d < end; d = d.addMonths(1)) out.push_back({d, 0, 0});
    }
    if (out.empty()) return out;
    Stmt s = db_.prep("SELECT t.date, t.kind, SUM(l.amount) FROM transactions t JOIN lines l ON l.tx_id=t.id "
                      "WHERE t.deleted_at IS NULL AND t.date>=? AND t.date<? AND t.kind IN (0,1) GROUP BY t.date, t.kind");
    s.bind(1, start.key()).bind(2, end.key());
    while (s.step()) {
        Date d = Date::fromKey((int)s.i(0));
        size_t idx;
        if (size == BucketSize::Day) idx = (size_t)(d.days() - out.front().start.days());
        else idx = (size_t)((d.y - out.front().start.y) * 12 + (d.m - out.front().start.m));
        if (idx >= out.size()) continue;
        i64 a = s.i(2);
        if (s.i(1) == (int)TxKind::Income) out[idx].income += a;
        else out[idx].expense += -a;
    }
    return out;
}

std::vector<std::wstring> BookDb::payees(const std::wstring& prefix, int limit) {
    std::vector<std::wstring> out;
    Stmt s = db_.prep("SELECT payee, COUNT(*) n FROM transactions WHERE payee<>'' AND deleted_at IS NULL "
                      "GROUP BY payee ORDER BY n DESC, MAX(date) DESC LIMIT 200");
    while (s.step() && (int)out.size() < limit) {
        std::wstring p = s.text(0);
        if (prefix.empty() || (p.size() >= prefix.size() && lower(p.substr(0, prefix.size())) == lower(prefix)))
            out.push_back(std::move(p));
    }
    return out;
}

i64 BookDb::lastCategoryForPayee(const std::wstring& payee, TxKind kind) {
    Stmt s = db_.prep("SELECT IFNULL(l.category_id,0) FROM transactions t JOIN lines l ON l.tx_id=t.id "
                      "WHERE t.payee=? AND t.kind=? AND t.deleted_at IS NULL ORDER BY t.date DESC, t.id DESC LIMIT 1");
    s.bind(1, payee).bind(2, (int)kind);
    return s.step() ? s.i(0) : 0;
}

}  // namespace fp
