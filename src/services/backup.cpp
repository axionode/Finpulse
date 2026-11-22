#include "services/backup.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <map>

#include "core/text.h"

namespace fp {

namespace {

constexpr u8 kMagic[4] = {'F', 'P', 'B', 'K'};
constexpr uint16_t kFormat = 1;
constexpr size_t kChunk = 1 << 20;
constexpr const char* kAppVersion = "0.1.0";

enum RecType : uint16_t { RManifest = 1, RSettings = 2, RBook = 3, RBookFile = 4, RBookWal = 5, REnd = 0xFFFF };

// ---------------------------------------------------------------- byte helpers
struct W {
    Bytes b;
    void u8_(u8 v) { b.push_back(v); }
    void u16_(uint16_t v) { for (int i = 0; i < 2; ++i) b.push_back(u8(v >> (8 * i))); }
    void u32_(u32 v) { for (int i = 0; i < 4; ++i) b.push_back(u8(v >> (8 * i))); }
    void u64_(uint64_t v) { for (int i = 0; i < 8; ++i) b.push_back(u8(v >> (8 * i))); }
    void raw(const void* p, size_t n) { auto* c = static_cast<const u8*>(p); b.insert(b.end(), c, c + n); }
};

struct R {
    const Bytes& b;
    size_t i = 0;
    bool bad = false;
    bool need(size_t n) { if (i + n > b.size()) { bad = true; return false; } return true; }
    u8 u8_() { return need(1) ? b[i++] : 0; }
    uint16_t u16_() { uint16_t v = 0; if (need(2)) for (int k = 0; k < 2; ++k) v |= uint16_t(b[i++]) << (8 * k); return v; }
    u32 u32_() { u32 v = 0; if (need(4)) for (int k = 0; k < 4; ++k) v |= u32(b[i++]) << (8 * k); return v; }
    uint64_t u64_() { uint64_t v = 0; if (need(8)) for (int k = 0; k < 8; ++k) v |= uint64_t(b[i++]) << (8 * k); return v; }
    Bytes take(size_t n) { if (!need(n)) return {}; Bytes o(b.begin() + i, b.begin() + i + n); i += n; return o; }
};

void be32(u8* p, u32 v) { p[0] = u8(v >> 24); p[1] = u8(v >> 16); p[2] = u8(v >> 8); p[3] = u8(v); }

std::string hex(const Bytes& b) { return crypto::toHex(b.data(), b.size()); }
Bytes unhex(const std::string& s) {
    Bytes o;
    auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1); };
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        int a = nib(s[i]), b = nib(s[i + 1]);
        if (a < 0 || b < 0) return {};
        o.push_back(u8(a * 16 + b));
    }
    return o;
}

std::string esc(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

std::string unesc(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            o += n == 'n' ? '\n' : (n == 'r' ? '\r' : n);
        } else {
            o += v[i];
        }
    }
    return o;
}

using KV = std::map<std::string, std::string>;

Bytes kvBytes(const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string s;
    for (auto& [k, v] : kv) s += k + "=" + esc(v) + "\n";
    return Bytes(s.begin(), s.end());
}

KV parseKv(const Bytes& b) {
    KV m;
    std::string s(b.begin(), b.end());
    size_t pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        if (nl == std::string::npos) nl = s.size();
        std::string line = s.substr(pos, nl - pos);
        size_t eq = line.find('=');
        if (eq != std::string::npos) m[line.substr(0, eq)] = unesc(line.substr(eq + 1));
        pos = nl + 1;
    }
    return m;
}

long long num(const KV& m, const char* k) {
    auto it = m.find(k);
    return it == m.end() ? 0 : std::strtoll(it->second.c_str(), nullptr, 10);
}
std::string str(const KV& m, const char* k) {
    auto it = m.find(k);
    return it == m.end() ? std::string() : it->second;
}

void addRecord(W& w, uint16_t type, const std::string& name, const Bytes& data) {
    w.u16_(type);
    w.u32_((u32)name.size());
    w.raw(name.data(), name.size());
    w.u64_(data.size());
    w.raw(data.data(), data.size());
}

// Snapshot of a book file. With the key we checkpoint WAL into the main file first.
bool snapshotBook(Library& lib, const BookRecord& r, const crypto::Key* key, Bytes& file, Bytes& wal) {
    std::wstring path = lib.bookPath(r.uuid);
    OpenedBook* cur = lib.current();
    if (cur && cur->rec.id == r.id) {
        cur->db.db().exec("PRAGMA wal_checkpoint(TRUNCATE)");
    } else if (key) {
        BookDb db;
        if (db.open(path, key->data())) {
            db.db().exec("PRAGMA wal_checkpoint(TRUNCATE)");
            db.close();
        }
    }
    if (!readFile(path, file)) return false;
    Bytes w;
    if (readFile(path + L"-wal", w) && !w.empty()) wal = std::move(w);
    return true;
}

// ---------------------------------------------------------------- build / seal / open
Bytes buildPlain(Library& lib, const std::vector<std::pair<i64, std::wstring>>& pins, bool includeAllKnownKeys) {
    W w;
    auto books = lib.books();
    addRecord(w, RManifest, "manifest",
              kvBytes({{"format", std::to_string(kFormat)}, {"app", kAppVersion}, {"created", std::to_string(unixNow())},
                       {"books", std::to_string(books.size())}}));
    std::vector<std::pair<std::string, std::string>> settings;
    for (auto& [k, v] : lib.catalog().allSettings()) settings.emplace_back(k, toUtf8(v));
    addRecord(w, RSettings, "settings", kvBytes(settings));

    for (auto& r : books) {
        crypto::Key key{};
        bool haveKey = false;
        std::wstring pin;
        for (auto& [id, p] : pins) if (id == r.id) pin = p;
        if (!r.hasPin() || (lib.current() && lib.current()->rec.id == r.id) || !pin.empty() || includeAllKnownKeys) {
            if (!r.hasPin() || !pin.empty() || (lib.current() && lib.current()->rec.id == r.id)) {
                auto k = lib.bookKey(r, pin);
                if (k) { key = k.value; haveKey = true; wipe(k.value.data(), k.value.size()); }
            }
        }
        std::vector<std::pair<std::string, std::string>> kv = {
            {"uuid", r.uuid}, {"name", toUtf8(r.name)}, {"kind", std::to_string((int)r.kind)}, {"currency", r.currency},
            {"color", std::to_string(r.color)}, {"icon", std::to_string((int)r.icon)}, {"sort", std::to_string(r.sort)},
            {"archived", r.archived ? "1" : "0"}, {"created", std::to_string(r.createdAt)},
            {"key_mode", std::to_string((int)r.keyMode)}, {"wrapped", hex(r.wrappedKey)}, {"salt", hex(r.kdfSalt)},
            {"kdf_m", std::to_string(r.kdf.memKiB)}, {"kdf_t", std::to_string(r.kdf.passes)},
            {"kdf_p", std::to_string(r.kdf.lanes)}};
        if (haveKey) kv.emplace_back("dek", crypto::toHex(key.data(), key.size()));
        Bytes file, wal;
        bool snap = snapshotBook(lib, r, haveKey ? &key : nullptr, file, wal);
        wipe(key.data(), key.size());
        if (!snap) continue;
        Bytes rec = kvBytes(kv);
        addRecord(w, RBook, r.uuid, rec);
        wipe(rec.data(), rec.size());
        addRecord(w, RBookFile, r.uuid, file);
        if (!wal.empty()) addRecord(w, RBookWal, r.uuid, wal);
    }
    crypto::Sha256 h;
    h.update(w.b.data(), w.b.size());
    auto digest = h.finish();
    addRecord(w, REnd, "end", Bytes(digest.begin(), digest.end()));
    return std::move(w.b);
}

bool seal(const Bytes& plain, BackupMode mode, const std::wstring& password, Bytes& out) {
    W hdr;
    hdr.raw(kMagic, 4);
    hdr.u16_(kFormat);
    hdr.u8_((u8)mode);
    if (mode == BackupMode::Plain) {
        out = hdr.b;
        out.insert(out.end(), plain.begin(), plain.end());
        return true;
    }
    crypto::Key key{};
    if (mode == BackupMode::Password) {
        u8 salt[crypto::kSaltSize];
        crypto::randomBytes(salt, sizeof(salt));
        crypto::KdfParams kp{64 * 1024, 3, 2};
        std::string p = toUtf8(password);
        bool ok = crypto::argon2id(p.data(), p.size(), salt, sizeof(salt), kp, key);
        wipe(p.data(), p.size());
        if (!ok) return false;
        hdr.raw(salt, sizeof(salt));
        hdr.u32_(kp.memKiB);
        hdr.u32_(kp.passes);
        hdr.u32_(kp.lanes);
    } else {
        key = crypto::randomKey();
        Bytes raw(key.begin(), key.end()), blob;
        bool ok = crypto::dpapiProtect(raw, blob);
        wipe(raw.data(), raw.size());
        if (!ok) return false;
        hdr.u32_((u32)blob.size());
        hdr.raw(blob.data(), blob.size());
    }
    u8 prefix[8];
    crypto::randomBytes(prefix, 8);
    hdr.raw(prefix, 8);
    out = hdr.b;
    size_t nChunks = plain.empty() ? 1 : (plain.size() + kChunk - 1) / kChunk;
    Bytes ct;
    for (size_t c = 0; c < nChunks; ++c) {
        size_t off = c * kChunk, len = std::min(kChunk, plain.size() - std::min(off, plain.size()));
        u8 last = c + 1 == nChunks ? 1 : 0;
        u8 nonce[12];
        std::memcpy(nonce, prefix, 8);
        be32(nonce + 8, (u32)c);
        Bytes aad = hdr.b;
        aad.resize(aad.size() + 5);
        be32(aad.data() + hdr.b.size(), (u32)c);
        aad.back() = last;
        ct.resize(len);
        u8 tag[16];
        if (!crypto::aesGcmEncrypt(key, nonce, aad.data(), aad.size(), plain.data() + off, len, ct.data(), tag)) {
            wipe(key.data(), key.size());
            return false;
        }
        out.push_back(last);
        for (int k = 0; k < 4; ++k) out.push_back(u8(len >> (8 * k)));
        out.insert(out.end(), ct.begin(), ct.end());
        out.insert(out.end(), tag, tag + 16);
    }
    wipe(key.data(), key.size());
    return true;
}

Status unseal(const Bytes& file, const std::wstring& password, Bytes& plain, BackupMode* modeOut) {
    R r{file};
    Bytes magic = r.take(4);
    if (r.bad || std::memcmp(magic.data(), kMagic, 4) != 0) return Status::fail(Ec::Corrupt, "magic");
    uint16_t fmt = r.u16_();
    if (fmt > kFormat) return Status::fail(Ec::TooNew);
    BackupMode mode = (BackupMode)r.u8_();
    if (modeOut) *modeOut = mode;
    if (mode == BackupMode::Plain) {
        plain.assign(file.begin() + r.i, file.end());
        return {};
    }
    crypto::Key key{};
    if (mode == BackupMode::Password) {
        Bytes salt = r.take(crypto::kSaltSize);
        crypto::KdfParams kp;
        kp.memKiB = r.u32_();
        kp.passes = r.u32_();
        kp.lanes = r.u32_();
        if (r.bad || kp.memKiB > 1024 * 1024 || kp.passes > 16 || kp.lanes > 16 || kp.memKiB < 8 * kp.lanes)
            return Status::fail(Ec::Corrupt, "kdf");
        std::string p = toUtf8(password);
        bool ok = crypto::argon2id(p.data(), p.size(), salt.data(), salt.size(), kp, key);
        wipe(p.data(), p.size());
        if (!ok) return Status::fail(Ec::Crypto, "argon2");
    } else if (mode == BackupMode::Dpapi) {
        u32 n = r.u32_();
        Bytes blob = r.take(n), raw;
        if (r.bad || !crypto::dpapiUnprotect(blob, raw) || raw.size() != key.size()) return Status::fail(Ec::WrongPassword, "dpapi");
        std::copy(raw.begin(), raw.end(), key.begin());
        wipe(raw.data(), raw.size());
    } else {
        return Status::fail(Ec::Corrupt, "mode");
    }
    Bytes prefix = r.take(8);
    if (r.bad) return Status::fail(Ec::Corrupt, "header");
    Bytes hdr(file.begin(), file.begin() + r.i);
    plain.clear();
    bool sawLast = false;
    for (u32 c = 0; r.i < file.size(); ++c) {
        u8 last = r.u8_();
        u32 len = r.u32_();
        if (r.bad || len > kChunk) { wipe(key.data(), key.size()); return Status::fail(Ec::Corrupt, "chunk"); }
        Bytes ct = r.take(len), tag = r.take(16);
        if (r.bad) { wipe(key.data(), key.size()); return Status::fail(Ec::Corrupt, "truncated"); }
        u8 nonce[12];
        std::memcpy(nonce, prefix.data(), 8);
        be32(nonce + 8, c);
        Bytes aad = hdr;
        aad.resize(aad.size() + 5);
        be32(aad.data() + hdr.size(), c);
        aad.back() = last;
        size_t off = plain.size();
        plain.resize(off + len);
        if (!crypto::aesGcmDecrypt(key, nonce, aad.data(), aad.size(), ct.data(), len, tag.data(), plain.data() + off)) {
            wipe(key.data(), key.size());
            wipe(plain.data(), plain.size());
            plain.clear();
            return Status::fail(c == 0 && mode == BackupMode::Password ? Ec::WrongPassword : Ec::Corrupt, "gcm");
        }
        if (last) { sawLast = true; break; }
    }
    wipe(key.data(), key.size());
    if (!sawLast || r.i != file.size()) return Status::fail(Ec::Corrupt, "incomplete");
    return {};
}

struct Record {
    uint16_t type;
    std::string name;
    Bytes data;
};

Status parseRecords(const Bytes& plain, std::vector<Record>& out) {
    R r{plain};
    size_t lastStart = 0;
    while (r.i < plain.size()) {
        size_t start = r.i;
        Record rec;
        rec.type = r.u16_();
        u32 nl = r.u32_();
        Bytes nm = r.take(nl);
        uint64_t dl = r.u64_();
        if (r.bad || dl > plain.size()) return Status::fail(Ec::Corrupt, "record");
        rec.name.assign(nm.begin(), nm.end());
        rec.data = r.take((size_t)dl);
        if (r.bad) return Status::fail(Ec::Corrupt, "record data");
        if (rec.type == REnd) {
            crypto::Sha256 h;
            h.update(plain.data(), start);
            auto d = h.finish();
            if (rec.data.size() != 32 || std::memcmp(d.data(), rec.data.data(), 32) != 0) return Status::fail(Ec::Corrupt, "hash");
            lastStart = start;
            out.push_back(std::move(rec));
            return r.i == plain.size() ? Status{} : Status::fail(Ec::Corrupt, "trailing");
        }
        out.push_back(std::move(rec));
    }
    (void)lastStart;
    return Status::fail(Ec::Corrupt, "no end record");
}

BookRecord recordFromKv(const KV& m) {
    BookRecord b;
    b.uuid = str(m, "uuid");
    b.name = fromUtf8(str(m, "name"));
    b.kind = (BookKind)num(m, "kind");
    b.currency = str(m, "currency");
    b.color = (int)num(m, "color");
    b.icon = (Icon)num(m, "icon");
    b.sort = (int)num(m, "sort");
    b.archived = num(m, "archived") != 0;
    b.createdAt = num(m, "created");
    b.keyMode = (KeyMode)num(m, "key_mode");
    b.wrappedKey = unhex(str(m, "wrapped"));
    b.kdfSalt = unhex(str(m, "salt"));
    b.kdf.memKiB = (u32)num(m, "kdf_m");
    b.kdf.passes = (u32)num(m, "kdf_t");
    b.kdf.lanes = (u32)num(m, "kdf_p");
    return b;
}

Status loadPlain(const std::wstring& path, const std::wstring& password, std::vector<Record>& recs, BackupMode* mode) {
    Bytes file, plain;
    if (!readFile(path, file)) return Status::fail(Ec::Io, "read");
    Status st = unseal(file, password, plain, mode);
    if (!st) return st;
    st = parseRecords(plain, recs);
    wipe(plain.data(), plain.size());
    return st;
}

bool writeSealed(const std::wstring& path, Bytes& plain, BackupMode mode, const std::wstring& password) {
    Bytes out;
    bool ok = seal(plain, mode, password, out);
    wipe(plain.data(), plain.size());
    return ok && writeFileAtomic(path, out);
}

}  // namespace

// ---------------------------------------------------------------- public API

Status exportBackup(Library& lib, const std::wstring& path, BackupMode mode, const std::wstring& password,
                    const std::vector<std::pair<i64, std::wstring>>& pins) {
    Bytes plain = buildPlain(lib, pins, false);
    return writeSealed(path, plain, mode, password) ? Status{} : Status::fail(Ec::Io, "write");
}

Result<BackupMode> backupMode(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return Result<BackupMode>::fail(Ec::Io);
    u8 hdr[7] = {};
    DWORD got = 0;
    ReadFile(h, hdr, 7, &got, nullptr);
    CloseHandle(h);
    if (got < 7 || std::memcmp(hdr, kMagic, 4) != 0) return Result<BackupMode>::fail(Ec::Corrupt);
    if ((hdr[4] | (hdr[5] << 8)) > kFormat) return Result<BackupMode>::fail(Ec::TooNew);
    return Result<BackupMode>::of((BackupMode)hdr[6]);
}

Result<BackupPreview> readBackupPreview(const std::wstring& path, const std::wstring& password) {
    std::vector<Record> recs;
    BackupPreview pv;
    Status st = loadPlain(path, password, recs, &pv.mode);
    if (!st) return Result<BackupPreview>::fail(st.code, st.detail);
    for (auto& r : recs) {
        if (r.type == RManifest) {
            KV m = parseKv(r.data);
            pv.created = num(m, "created");
            pv.appVersion = str(m, "app");
            if (num(m, "format") > kFormat) return Result<BackupPreview>::fail(Ec::TooNew);
        } else if (r.type == RBook) {
            KV m = parseKv(r.data);
            BackupBookInfo bi;
            bi.rec = recordFromKv(m);
            bi.hasKey = m.count("dek") != 0;
            pv.books.push_back(std::move(bi));
            wipe(r.data.data(), r.data.size());
        } else if (r.type == RBookFile) {
            for (auto& b : pv.books) if (b.rec.uuid == r.name) b.fileSize = r.data.size();
        }
    }
    return Result<BackupPreview>::of(std::move(pv));
}

Status importBackup(Library& lib, const std::wstring& path, const std::wstring& password, ImportMode mode,
                    bool resetPins, int* importedBooks) {
    std::vector<Record> recs;
    Status st = loadPlain(path, password, recs, nullptr);
    if (!st) return st;
    if (importedBooks) *importedBooks = 0;

    if (mode == ImportMode::Replace) {
        Status ab = autoBackup(lib);
        if (!ab) return ab;  // never destroy data without a safety copy
        lib.closeCurrent();
        for (auto& b : lib.books()) lib.deleteBook(b.id);
        for (auto& r : recs) {
            if (r.type != RSettings) continue;
            for (auto& [k, v] : parseKv(r.data)) lib.catalog().setSetting(k, fromUtf8(v));
        }
    }

    std::map<std::string, Bytes> files, wals;
    for (auto& r : recs) {
        if (r.type == RBookFile) files[r.name] = std::move(r.data);
        else if (r.type == RBookWal) wals[r.name] = std::move(r.data);
    }
    auto existing = lib.books();
    for (auto& r : recs) {
        if (r.type != RBook) continue;
        KV m = parseKv(r.data);
        wipe(r.data.data(), r.data.size());
        BookRecord b = recordFromKv(m);
        std::string origUuid = b.uuid;
        auto fit = files.find(origUuid);
        if (fit == files.end()) continue;
        Bytes dekRaw = unhex(str(m, "dek"));
        if (m.count("dek")) { wipe(m["dek"].data(), m["dek"].size()); }
        bool hasKey = dekRaw.size() == crypto::kKeySize;
        for (auto& e : existing)
            if (e.uuid == b.uuid) { b.uuid = Library::newUuid(); break; }

        if (hasKey && (resetPins || b.keyMode == KeyMode::Dpapi)) {
            Bytes blob;
            if (!crypto::dpapiProtect(dekRaw, blob)) { wipe(dekRaw.data(), dekRaw.size()); return Status::fail(Ec::Crypto); }
            b.keyMode = KeyMode::Dpapi;
            b.wrappedKey = blob;
            b.kdfSalt.clear();
            b.kdf = {0, 0, 0};
        }
        // A DPAPI book without key can only be restored on the same Windows account; verify below.
        std::wstring p = lib.bookPath(b.uuid);
        if (!writeFileAtomic(p, fit->second)) { wipe(dekRaw.data(), dekRaw.size()); return Status::fail(Ec::Io); }
        auto wit = wals.find(origUuid);
        if (wit != wals.end()) writeFileAtomic(p + L"-wal", wit->second);
        else DeleteFileW((p + L"-wal").c_str());

        if (hasKey) {
            BookDb test;
            Status ts = test.open(p, dekRaw.data());
            test.close();
            if (!ts) { wipe(dekRaw.data(), dekRaw.size()); DeleteFileW(p.c_str()); return Status::fail(Ec::Corrupt, "book verify"); }
        } else if (b.keyMode == KeyMode::Dpapi) {
            Bytes raw;
            if (!crypto::dpapiUnprotect(b.wrappedKey, raw)) { DeleteFileW(p.c_str()); continue; }
            wipe(raw.data(), raw.size());
        }
        wipe(dekRaw.data(), dekRaw.size());
        b.failCount = 0;
        b.lockUntil = 0;
        i64 id = lib.catalog().insertBook(b);
        if (!id) return Status::fail(Ec::Io, "catalog");
        b.id = id;
        lib.catalog().updateBookInfo(b);
        existing.push_back(b);
        if (importedBooks) ++*importedBooks;
    }
    return {};
}

Status autoBackup(Library& lib) {
    if (lib.books().empty()) return {};
    ensureDir(lib.backupsDir());
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"Finpulse-auto-%04d%02d%02d-%02d%02d%02d.fpbackup", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    Bytes plain = buildPlain(lib, {}, false);
    if (!writeSealed(lib.backupsDir() + L"\\" + name, plain, BackupMode::Dpapi, L"")) return Status::fail(Ec::Io, "auto");
    // rotation: keep newest 14
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((lib.backupsDir() + L"\\Finpulse-auto-*.fpbackup").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do { found.push_back(fd.cFileName); } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(found.begin(), found.end());
    for (size_t i = 0; i + 14 < found.size(); ++i) DeleteFileW((lib.backupsDir() + L"\\" + found[i]).c_str());
    return {};
}

}  // namespace fp
