#include "services/library.h"

#include <windows.h>
#include <shlobj.h>

#include "core/text.h"

namespace fp {

// ---------------------------------------------------------------- files and folders

std::wstring exePath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return std::wstring(buf, n);
}

std::wstring exeDirectory() {
    std::wstring p = exePath();
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"." : p.substr(0, k);
}

bool ensureDir(const std::wstring& dir) {
    if (CreateDirectoryW(dir.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool canWriteIn(const std::wstring& dir) {
    if (!ensureDir(dir)) return false;
    std::wstring probe = dir + L"\\.write-test";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

DataLocation resolveDataLocation() {
    DataLocation loc;
    std::wstring portable = exeDirectory() + L"\\Finpulse Data";
    if (canWriteIn(portable)) {
        loc.dir = portable;
        loc.portable = true;
        return loc;
    }
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        loc.dir = std::wstring(local) + L"\\Finpulse";
        CoTaskMemFree(local);
    } else {
        loc.dir = portable;
    }
    ensureDir(loc.dir);
    loc.portable = false;
    return loc;
}

bool writeFileAtomic(const std::wstring& path, const Bytes& data) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t off = 0;
    bool ok = true;
    while (off < data.size() && ok) {
        DWORD chunk = (DWORD)std::min<size_t>(data.size() - off, 1 << 24), done = 0;
        ok = WriteFile(h, data.data() + off, chunk, &done, nullptr) && done == chunk;
        off += done;
    }
    ok = ok && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool readFile(const std::wstring& path, Bytes& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    size_t off = 0;
    bool ok = true;
    while (off < out.size() && ok) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - off, 1 << 24), done = 0;
        ok = ReadFile(h, out.data() + off, chunk, &done, nullptr) && done == chunk;
        off += done;
    }
    CloseHandle(h);
    return ok;
}

// ---------------------------------------------------------------- helpers

std::wstring Library::fromUtf8Str(const std::string& s) { return fromUtf8(s); }

std::string Library::newUuid() {
    u8 b[16];
    crypto::randomBytes(b, 16);
    b[6] = (b[6] & 0x0F) | 0x40;
    b[8] = (b[8] & 0x3F) | 0x80;
    std::string h = crypto::toHex(b, 16);
    return h.substr(0, 8) + "-" + h.substr(8, 4) + "-" + h.substr(12, 4) + "-" + h.substr(16, 4) + "-" + h.substr(20);
}

Bytes Library::keyAad(const std::string& /*uuid*/) {
    // Not bound to the uuid on purpose: an imported copy gets a new uuid and must keep its PIN.
    std::string s = "finpulse-book-key-v1";
    return Bytes(s.begin(), s.end());
}

bool Library::validPin(const std::wstring& pin) {
    if (pin.size() != 4) return false;
    for (wchar_t c : pin)
        if (c < L'0' || c > L'9') return false;
    return true;
}

int Library::throttleSeconds(const BookRecord& r) const {
    i64 left = r.lockUntil - unixNow();
    return left > 0 ? (int)left : 0;
}

// ---------------------------------------------------------------- lifecycle

Status Library::init() {
    loc_ = resolveDataLocation();
    if (!ensureDir(loc_.dir) || !ensureDir(booksDir())) return Status::fail(Ec::Io, "data dir");
    ensureDir(backupsDir());
    return catalog_.open(loc_.dir + L"\\catalog.db");
}

Result<i64> Library::createBook(const NewBook& nb) {
    BookRecord r;
    r.uuid = newUuid();
    r.name = nb.name;
    r.kind = nb.kind;
    r.currency = nb.currency;
    r.color = nb.color;
    r.icon = nb.icon;
    r.createdAt = unixNow();
    r.keyMode = KeyMode::Dpapi;

    crypto::Key dek = crypto::randomKey();
    Bytes raw(dek.begin(), dek.end());
    if (!crypto::dpapiProtect(raw, r.wrappedKey)) { wipe(raw.data(), raw.size()); return Result<i64>::fail(Ec::Crypto, "dpapi"); }
    wipe(raw.data(), raw.size());

    BookMeta meta{r.uuid, r.name, r.kind, r.currency};
    BookDb db;
    Status st = db.create(bookPath(r.uuid), dek.data(), meta, defaultCategories(nb.kind), tr(S::DefaultAccountName));
    db.close();
    wipe(dek.data(), dek.size());
    if (!st) { DeleteFileW(bookPath(r.uuid).c_str()); return Result<i64>::fail(st.code, st.detail); }
    i64 id = catalog_.insertBook(r);
    if (!id) return Result<i64>::fail(Ec::Io, "catalog insert");
    return Result<i64>::of(id);
}

Status Library::deleteBook(i64 id) {
    BookRecord r;
    if (!catalog_.getBook(id, r)) return Status::fail(Ec::NotFound);
    if (current_ && current_->rec.id == id) current_.reset();
    std::wstring p = bookPath(r.uuid);
    DeleteFileW(p.c_str());
    DeleteFileW((p + L"-wal").c_str());
    DeleteFileW((p + L"-shm").c_str());
    return catalog_.removeBook(id) ? Status{} : Status::fail(Ec::Io);
}

Status Library::updateBookInfo(const BookRecord& r) {
    if (!catalog_.updateBookInfo(r)) return Status::fail(Ec::Io);
    if (current_ && current_->rec.id == r.id) {
        current_->rec.name = r.name;
        current_->rec.kind = r.kind;
        current_->rec.color = r.color;
        current_->rec.icon = r.icon;
        current_->rec.archived = r.archived;
        current_->db.setMeta("name", r.name);
        current_->db.setMeta("kind", std::to_wstring((int)r.kind));
    }
    return {};
}

Status Library::keyFromPin(const BookRecord& r, const std::wstring& pin, crypto::Key& out) {
    if (!validPin(pin)) return Status::fail(Ec::WrongPin);
    std::string p = toUtf8(pin);
    crypto::Key kek{};
    crypto::lockMemory(kek.data(), kek.size());
    bool ok = crypto::argon2id(p.data(), p.size(), r.kdfSalt.data(), r.kdfSalt.size(), r.kdf, kek);
    wipe(p.data(), p.size());
    bool unwrapped = ok && crypto::unwrapKey(kek, r.wrappedKey, keyAad(r.uuid), out);
    wipe(kek.data(), kek.size());
    crypto::unlockMemory(kek.data(), kek.size());
    if (!ok) return Status::fail(Ec::Crypto, "argon2");
    return unwrapped ? Status{} : Status::fail(Ec::WrongPin);
}

Result<crypto::Key> Library::bookKey(const BookRecord& r, const std::wstring& pin) {
    if (current_ && current_->rec.id == r.id) return Result<crypto::Key>::of(current_->key);
    crypto::Key k{};
    if (r.keyMode == KeyMode::Dpapi) {
        Bytes raw;
        if (!crypto::dpapiUnprotect(r.wrappedKey, raw) || raw.size() != k.size()) {
            wipe(raw.data(), raw.size());
            return Result<crypto::Key>::fail(Ec::Crypto, "dpapi");
        }
        std::copy(raw.begin(), raw.end(), k.begin());
        wipe(raw.data(), raw.size());
        return Result<crypto::Key>::of(k);
    }
    Status st = keyFromPin(r, pin, k);
    if (!st) return Result<crypto::Key>::fail(st.code, st.detail);
    return Result<crypto::Key>::of(k);
}

Status Library::open(i64 id, const std::wstring& pin) {
    BookRecord r;
    if (!catalog_.getBook(id, r)) return Status::fail(Ec::NotFound);
    if (r.hasPin()) {
        if (throttleSeconds(r) > 0) return Status::fail(Ec::Throttled);
        crypto::Key k{};
        Status st = keyFromPin(r, pin, k);
        if (st.code == Ec::WrongPin) {
            // ADR-0007: 5 wrong -> 30 s, then every 3 more: 2 min, then 5 min.
            int fails = r.failCount + 1;
            i64 until = 0;
            if (fails >= 5) {
                int extra = fails - 5;
                int secs = extra == 0 ? 30 : (extra % 3 != 0 ? 0 : (extra / 3 == 1 ? 120 : 300));
                if (secs) until = unixNow() + secs;
            }
            catalog_.updatePinState(id, fails, until);
            return until ? Status::fail(Ec::Throttled) : st;
        }
        if (!st) return st;
        catalog_.updatePinState(id, 0, 0);
        auto ob = std::make_unique<OpenedBook>();
        ob->rec = r;
        ob->key = k;
        wipe(k.data(), k.size());
        Status os = ob->db.open(bookPath(r.uuid), ob->key.data());
        if (!os) return os;
        ob->db.purgeTrash(30);
        current_ = std::move(ob);
        return {};
    }
    auto k = bookKey(r, L"");
    if (!k) return k.status;
    auto ob = std::make_unique<OpenedBook>();
    ob->rec = r;
    ob->key = k.value;
    wipe(k.value.data(), k.value.size());
    Status os = ob->db.open(bookPath(r.uuid), ob->key.data());
    if (!os) return os;
    ob->db.purgeTrash(30);
    current_ = std::move(ob);
    return {};
}

Status Library::setPin(const std::wstring& pin) {
    if (!current_) return Status::fail(Ec::NotFound);
    if (!validPin(pin)) return Status::fail(Ec::Invalid);
    BookRecord& r = current_->rec;
    Bytes salt(crypto::kSaltSize);
    crypto::randomBytes(salt.data(), salt.size());
    crypto::KdfParams kp;
    std::string p = toUtf8(pin);
    crypto::Key kek{};
    bool ok = crypto::argon2id(p.data(), p.size(), salt.data(), salt.size(), kp, kek);
    wipe(p.data(), p.size());
    if (!ok) return Status::fail(Ec::Crypto, "argon2");
    Bytes wrapped = crypto::wrapKey(kek, current_->key, keyAad(r.uuid));
    wipe(kek.data(), kek.size());
    if (wrapped.empty()) return Status::fail(Ec::Crypto, "wrap");
    BookRecord upd = r;
    upd.keyMode = KeyMode::Pin;
    upd.wrappedKey = wrapped;
    upd.kdfSalt = salt;
    upd.kdf = kp;
    if (!catalog_.updateBookKey(upd)) return Status::fail(Ec::Io);
    r = upd;
    return {};
}

Status Library::removePin() {
    if (!current_) return Status::fail(Ec::NotFound);
    Bytes raw(current_->key.begin(), current_->key.end()), blob;
    bool ok = crypto::dpapiProtect(raw, blob);
    wipe(raw.data(), raw.size());
    if (!ok) return Status::fail(Ec::Crypto, "dpapi");
    BookRecord upd = current_->rec;
    upd.keyMode = KeyMode::Dpapi;
    upd.wrappedKey = blob;
    upd.kdfSalt.clear();
    upd.kdf = {0, 0, 0};
    if (!catalog_.updateBookKey(upd)) return Status::fail(Ec::Io);
    current_->rec = upd;
    return {};
}

std::optional<i64> Library::peekBalance(const BookRecord& r, int* txCount) {
    if (r.hasPin()) return std::nullopt;
    if (current_ && current_->rec.id == r.id) {
        if (txCount) *txCount = current_->db.transactionCount();
        return current_->db.totalBalance();
    }
    auto k = bookKey(r, L"");
    if (!k) return std::nullopt;
    BookDb db;
    Status st = db.open(bookPath(r.uuid), k.value.data());
    wipe(k.value.data(), k.value.size());
    if (!st) return std::nullopt;
    i64 b = db.totalBalance();
    if (txCount) *txCount = db.transactionCount();
    db.close();
    return b;
}

}  // namespace fp
