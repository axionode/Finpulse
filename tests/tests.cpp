// Minimal test runner (ADR-0016). Runs on Windows or under Wine.
#ifdef _WIN32
#include <windows.h>
#endif

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/date.h"
#include "core/money.h"
#include "core/text.h"
#ifdef _WIN32
#include "crypto/crypto.h"
#include "services/backup.h"
#include "services/i18n.h"
#include "services/library.h"
#include "storage/book.h"
#endif

using namespace fp;

static int g_fail = 0, g_checks = 0;
static FILE* g_log = nullptr;
#include <cstdarg>
static void logf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    std::vprintf(fmt, a);
    va_end(a);
    if (g_log) { va_start(a, fmt); std::vfprintf(g_log, fmt, a); va_end(a); std::fflush(g_log); }
}
static std::vector<std::pair<const char*, std::function<void()>>>& registry() {
    static std::vector<std::pair<const char*, std::function<void()>>> r;
    return r;
}
struct Reg { Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); } };
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(...) do { ++g_checks; if (!(__VA_ARGS__)) { ++g_fail; logf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); } } while (0)

TEST(dates) {
    CHECK(Date{1970, 1, 1}.days() == 0);
    CHECK(Date::fromDays(Date{2026, 11, 22}.days()) == (Date{2026, 11, 22}));
    CHECK((Date{2026, 11, 22}.weekday()) == 6);  // Sunday
    CHECK((Date{2024, 1, 31}.addMonths(1)) == (Date{2024, 2, 29}));
    Period w = makePeriod(PeriodKind::Week, {2026, 11, 22}, 0);
    CHECK(w.start == (Date{2026, 11, 16}) && w.end == (Date{2026, 11, 23}));
    Period m = shiftPeriod(makePeriod(PeriodKind::Month, {2026, 1, 15}, 0), -1, 0);
    CHECK(m.start == (Date{2025, 12, 1}) && m.end == (Date{2026, 1, 1}));
    Period q = makePeriod(PeriodKind::Quarter, {2026, 11, 22}, 0);
    CHECK(q.start == (Date{2026, 10, 1}) && q.end == (Date{2027, 1, 1}));
}

TEST(money) {
    auto e = [](const wchar_t* s, int dec = 2) { auto r = evalAmount(s, dec); return r ? r.value : -999999; };
    CHECK(e(L"120") == 12000);
    CHECK(e(L"12,5") == 1250);
    CHECK(e(L"12.50") == 1250);
    CHECK(e(L"1 200,50") == 120050);
    CHECK(e(L"1,200.50") == 120050);
    CHECK(e(L"120+35") == 15500);
    CHECK(e(L"900/3") == 30000);
    CHECK(e(L"10/3") == 333);
    CHECK(e(L"20/3") == 667);
    CHECK(e(L"(10+5)*2") == 3000);
    CHECK(e(L"0.1+0.2") == 30);
    CHECK(e(L"-5") == -500);
    CHECK(e(L"100", 0) == 100);
    CHECK(!evalAmount(L"", 2));
    CHECK(!evalAmount(L"5/0", 2));
    CHECK(!evalAmount(L"abc", 2));
    NumberFormat nf;
    CHECK(formatAmount(123456789, currencyByCode("CZK"), nf) == L"1 234 567,89 Kč");
    CHECK(formatAmount(-1250, currencyByCode("USD"), nf) == L"-$12,50");
    CHECK(formatAmount(1250, currencyByCode("CZK"), nf, false) == L"12,50");
    CHECK(formatPlain(120050, 2, L',') == L"1200,50");
    CHECK(formatPlain(120000, 2, L',') == L"1200");
}

TEST(text) {
    CHECK(fromUtf8(toUtf8(L"Привет, Kč € 𝄞")) == L"Привет, Kč € 𝄞");
    CHECK(containsCI(L"Продукты Albert", L"albert"));
    CHECK(containsCI(L"Продукты", L"ПРОД"));
    CHECK(!containsCI(L"Кафе", L"бар"));
}

#ifdef _WIN32
TEST(crypto_primitives) {
    crypto::Key k = crypto::randomKey(), dek = crypto::randomKey(), out{};
    Bytes aad{1, 2, 3};
    Bytes blob = crypto::wrapKey(k, dek, aad);
    CHECK(blob.size() == 60);
    CHECK(crypto::unwrapKey(k, blob, aad, out) && out == dek);
    blob[20] ^= 1;
    CHECK(!crypto::unwrapKey(k, blob, aad, out));
    Bytes plain{9, 8, 7}, prot, back;
    CHECK(crypto::dpapiProtect(plain, prot) && crypto::dpapiUnprotect(prot, back) && back == plain);
    // Argon2id RFC 9106-style sanity: deterministic and salt-sensitive
    crypto::Key a{}, b{}, c{};
    u8 s1[16] = {1}, s2[16] = {2};
    crypto::KdfParams kp{1024, 2, 1};
    CHECK(crypto::argon2id("1234", 4, s1, 16, kp, a) && crypto::argon2id("1234", 4, s1, 16, kp, b) && a == b);
    CHECK(crypto::argon2id("1234", 4, s2, 16, kp, c) && !(a == c));
    crypto::Sha256 h;
    h.update("abc", 3);
    auto d = h.finish();
    CHECK(crypto::toHex(d.data(), 4) == "ba7816bf");
}

static std::wstring tmpDir() {
    wchar_t buf[MAX_PATH];
    GetTempPathW(MAX_PATH, buf);
    std::wstring d = std::wstring(buf) + L"fptest-" + std::to_wstring(GetTickCount());
    ensureDir(d);
    return d;
}

TEST(book_storage) {
    std::wstring dir = tmpDir();
    crypto::Key key = crypto::randomKey();
    BookDb db;
    BookMeta meta{"u-1", L"Тест", BookKind::Personal, "CZK"};
    Status st = db.create(dir + L"\\b.fpbook", key.data(), meta, defaultCategories(BookKind::Personal), L"Наличные");
    CHECK(st.ok());
    auto accs = db.accounts();
    CHECK(accs.size() == 1);
    auto cats = db.categories();
    CHECK(cats.size() > 10);
    i64 cash = accs[0].id;
    Account card;
    card.name = L"Card";
    card.type = AccountType::Card;
    card.opening = 100000;
    i64 cardId = db.saveAccount(card);
    CHECK(cardId > 0);

    i64 food = 0, salary = 0;
    for (auto& c : cats) {
        if (c.type == CatType::Expense && !food) food = c.id;
        if (c.type == CatType::Income && !salary) salary = c.id;
    }
    Transaction t1;
    t1.kind = TxKind::Expense;
    t1.date = {2026, 11, 20};
    t1.payee = L"Albert";
    t1.lines = {{0, cash, food, -25000}};
    i64 id1 = db.saveTransaction(t1);
    Transaction t2;
    t2.kind = TxKind::Income;
    t2.date = {2026, 11, 5};
    t2.lines = {{0, cardId, salary, 5000000}};
    db.saveTransaction(t2);
    Transaction t3;
    t3.kind = TxKind::Transfer;
    t3.date = {2026, 11, 21};
    t3.lines = {{0, cardId, 0, -100000}, {0, cash, 0, 100000}};
    db.saveTransaction(t3);

    Period nov = makePeriod(PeriodKind::Month, {2026, 11, 1}, 0);
    Totals tt = db.totals(nov);
    CHECK(tt.income == 5000000 && tt.expense == 25000);
    CHECK(db.totalBalance() == 100000 + 5000000 - 25000);
    auto byCat = db.byCategory(nov, CatType::Expense);
    CHECK(byCat.size() == 1 && byCat[0].amount == 25000);
    auto list = db.transactions(nov);
    CHECK(list.size() == 3 && list[0].kind == TxKind::Transfer && list[0].absAmount() == 100000);
    auto ser = db.series(nov, BucketSize::Day);
    CHECK(ser.size() == 30 && ser[19].expense == 25000 && ser[4].income == 5000000);
    CHECK(db.softDelete(id1) && db.totals(nov).expense == 0 && db.trash().size() == 1);
    CHECK(db.restore(id1) && db.totals(nov).expense == 25000);
    CHECK(db.payees(L"al", 5).size() == 1);
    CHECK(db.lastCategoryForPayee(L"Albert", TxKind::Expense) == food);
    db.close();

    BookDb wrong;
    crypto::Key other = crypto::randomKey();
    CHECK(!wrong.open(dir + L"\\b.fpbook", other.data()).ok());
    BookDb again;
    CHECK(again.open(dir + L"\\b.fpbook", key.data()).ok());
    CHECK(again.transactionCount() == 3);
    again.close();
    // file must not contain plaintext
    Bytes raw;
    readFile(dir + L"\\b.fpbook", raw);
    std::string s(raw.begin(), raw.end());
    CHECK(s.find("Albert") == std::string::npos && s.find("SQLite format") == std::string::npos);
}

TEST(library_pin_and_backup) {
    Library lib;
    CHECK(lib.init().ok());
    for (auto& b : lib.books()) lib.deleteBook(b.id);
    NewBook nb;
    nb.name = L"Личное";
    nb.currency = "EUR";
    auto r = lib.createBook(nb);
    CHECK(r.ok());
    i64 id = r.value;
    CHECK(lib.open(id, L"").ok());
    auto* ob = lib.current();
    CHECK(ob != nullptr);
    Transaction t;
    t.kind = TxKind::Expense;
    t.date = Date::today();
    t.payee = L"Billa";
    t.lines = {{0, ob->db.accounts()[0].id, ob->db.categories()[0].id, -1990}};
    CHECK(ob->db.saveTransaction(t) > 0);
    CHECK(lib.setPin(L"4321").ok());
    lib.closeCurrent();
    CHECK(lib.open(id, L"0000").code == Ec::WrongPin);
    CHECK(lib.open(id, L"4321").ok());
    lib.closeCurrent();

    NewBook nb2;
    nb2.name = L"Firma";
    nb2.kind = BookKind::Business;
    auto r2 = lib.createBook(nb2);
    CHECK(r2.ok());

    std::wstring dir = tmpDir();
    // export with PIN provided, password protected
    CHECK(exportBackup(lib, dir + L"\\all.fpbackup", BackupMode::Password, L"secret", {{id, L"4321"}}).ok());
    CHECK(backupMode(dir + L"\\all.fpbackup").value == BackupMode::Password);
    CHECK(readBackupPreview(dir + L"\\all.fpbackup", L"bad").status.code == Ec::WrongPassword);
    auto pv = readBackupPreview(dir + L"\\all.fpbackup", L"secret");
    CHECK(pv.ok() && pv.value.books.size() == 2 && pv.value.books[0].hasKey);
    // plain export without PIN: locked book exported without its key
    CHECK(exportBackup(lib, dir + L"\\plain.fpbackup", BackupMode::Plain, L"", {}).ok());
    auto pv2 = readBackupPreview(dir + L"\\plain.fpbackup", L"");
    CHECK(pv2.ok() && pv2.value.books.size() == 2 && !pv2.value.books[0].hasKey && pv2.value.books[1].hasKey);

    // replace everything, reset PINs
    int n = 0;
    CHECK(importBackup(lib, dir + L"\\all.fpbackup", L"secret", ImportMode::Replace, true, &n).ok());
    CHECK(n == 2);
    auto books = lib.books();
    CHECK(books.size() == 2 && !books[0].hasPin());
    CHECK(lib.open(books[0].id, L"").ok());
    CHECK(lib.current()->db.transactionCount() == 1);
    lib.closeCurrent();
    // add the plain one: PIN kept, uuids renamed
    CHECK(importBackup(lib, dir + L"\\plain.fpbackup", L"", ImportMode::Add, false, &n).ok());
    books = lib.books();
    CHECK(books.size() == 4);
    int pinned = 0;
    for (auto& b : books) pinned += b.hasPin();
    CHECK(pinned == 1);
    for (auto& b : books)
        if (b.hasPin()) CHECK(lib.open(b.id, L"4321").ok());
    lib.closeCurrent();
    CHECK(autoBackup(lib).ok());
    for (auto& b : lib.books()) lib.deleteBook(b.id);
}

#endif  // _WIN32

#ifdef _WIN32
int wmain() {
    // results go next to the exe so they can be read after a double-click run
    std::wstring out = exeDirectory() + L"\\test-results.txt";
    g_log = _wfopen(out.c_str(), L"w");
    setLang(Lang::Ru);
#else
int main() {
#endif
    for (auto& [name, fn] : registry()) {
        int before = g_fail;
        fn();
        logf("%s %s\n", g_fail == before ? "ok  " : "FAIL", name);
    }
    logf("\n%d checks, %d failed\n", g_checks, g_fail);
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
