#include "crypto/crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>

#include <cstring>

#include "argon2.h"

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace fp::crypto {

namespace {

BCRYPT_ALG_HANDLE aesAlg() {
    static BCRYPT_ALG_HANDLE h = [] {
        BCRYPT_ALG_HANDLE a = nullptr;
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&a, BCRYPT_AES_ALGORITHM, nullptr, 0))) return (BCRYPT_ALG_HANDLE) nullptr;
        if (!NT_SUCCESS(BCryptSetProperty(a, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0))) {
            BCryptCloseAlgorithmProvider(a, 0);
            return (BCRYPT_ALG_HANDLE) nullptr;
        }
        return a;
    }();
    return h;
}

BCRYPT_ALG_HANDLE shaAlg() {
    static BCRYPT_ALG_HANDLE h = [] {
        BCRYPT_ALG_HANDLE a = nullptr;
        if (!NT_SUCCESS(BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return (BCRYPT_ALG_HANDLE) nullptr;
        return a;
    }();
    return h;
}

bool gcm(bool encrypt, const Key& key, const u8* nonce, const u8* aad, size_t aadLen, const u8* in, size_t len,
         u8* out, u8* tag) {
    BCRYPT_ALG_HANDLE alg = aesAlg();
    if (!alg) return false;
    BCRYPT_KEY_HANDLE hk = nullptr;
    if (!NT_SUCCESS(BCryptGenerateSymmetricKey(alg, &hk, nullptr, 0, (PUCHAR)key.data(), (ULONG)key.size(), 0)))
        return false;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = (PUCHAR)nonce;
    info.cbNonce = (ULONG)kNonceSize;
    info.pbAuthData = (PUCHAR)aad;
    info.cbAuthData = (ULONG)aadLen;
    info.pbTag = tag;
    info.cbTag = (ULONG)kTagSize;
    ULONG done = 0;
    // CNG rejects null buffers for zero-length data; use a dummy byte.
    u8 dummy = 0;
    PUCHAR pin = len ? (PUCHAR)in : &dummy;
    PUCHAR pout = len ? (PUCHAR)out : &dummy;
    NTSTATUS st = encrypt ? BCryptEncrypt(hk, pin, (ULONG)len, &info, nullptr, 0, pout, (ULONG)len, &done, 0)
                          : BCryptDecrypt(hk, pin, (ULONG)len, &info, nullptr, 0, pout, (ULONG)len, &done, 0);
    BCryptDestroyKey(hk);
    return NT_SUCCESS(st) && done == len;
}

}  // namespace

bool randomBytes(void* out, size_t n) {
    return NT_SUCCESS(BCryptGenRandom(nullptr, (PUCHAR)out, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

Key randomKey() {
    Key k{};
    randomBytes(k.data(), k.size());
    return k;
}

Sha256::Sha256() {
    BCRYPT_HASH_HANDLE hh = nullptr;
    if (shaAlg() && NT_SUCCESS(BCryptCreateHash(shaAlg(), &hh, obj_, sizeof(obj_), nullptr, 0, 0))) h_ = hh;
}

Sha256::~Sha256() {
    if (h_) BCryptDestroyHash((BCRYPT_HASH_HANDLE)h_);
}

void Sha256::update(const void* p, size_t n) {
    if (h_ && n) BCryptHashData((BCRYPT_HASH_HANDLE)h_, (PUCHAR)p, (ULONG)n, 0);
}

std::array<u8, 32> Sha256::finish() {
    std::array<u8, 32> out{};
    if (h_) BCryptFinishHash((BCRYPT_HASH_HANDLE)h_, out.data(), 32, 0);
    return out;
}

bool aesGcmEncrypt(const Key& key, const u8 nonce[kNonceSize], const u8* aad, size_t aadLen, const u8* pt, size_t len,
                   u8* ct, u8 tag[kTagSize]) {
    return gcm(true, key, nonce, aad, aadLen, pt, len, ct, tag);
}

bool aesGcmDecrypt(const Key& key, const u8 nonce[kNonceSize], const u8* aad, size_t aadLen, const u8* ct, size_t len,
                   const u8 tag[kTagSize], u8* pt) {
    return gcm(false, key, nonce, aad, aadLen, ct, len, pt, const_cast<u8*>(tag));
}

Bytes wrapKey(const Key& kek, const Key& dek, const Bytes& aad) {
    Bytes blob(kNonceSize + kKeySize + kTagSize);
    randomBytes(blob.data(), kNonceSize);
    if (!aesGcmEncrypt(kek, blob.data(), aad.data(), aad.size(), dek.data(), kKeySize, blob.data() + kNonceSize,
                       blob.data() + kNonceSize + kKeySize))
        return {};
    return blob;
}

bool unwrapKey(const Key& kek, const Bytes& blob, const Bytes& aad, Key& dek) {
    if (blob.size() != kNonceSize + kKeySize + kTagSize) return false;
    return aesGcmDecrypt(kek, blob.data(), aad.data(), aad.size(), blob.data() + kNonceSize, kKeySize,
                         blob.data() + kNonceSize + kKeySize, dek.data());
}

bool dpapiProtect(const Bytes& in, Bytes& out) {
    DATA_BLOB src{(DWORD)in.size(), (BYTE*)in.data()}, dst{};
    if (!CryptProtectData(&src, L"Finpulse", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) return false;
    out.assign(dst.pbData, dst.pbData + dst.cbData);
    LocalFree(dst.pbData);
    return true;
}

bool dpapiUnprotect(const Bytes& in, Bytes& out) {
    DATA_BLOB src{(DWORD)in.size(), (BYTE*)in.data()}, dst{};
    if (!CryptUnprotectData(&src, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &dst)) return false;
    out.assign(dst.pbData, dst.pbData + dst.cbData);
    SecureZeroMemory(dst.pbData, dst.cbData);
    LocalFree(dst.pbData);
    return true;
}

bool argon2id(const void* secret, size_t len, const u8* salt, size_t saltLen, const KdfParams& p, Key& out) {
    int rc = argon2id_hash_raw(p.passes, p.memKiB, p.lanes, secret, len, salt, saltLen, out.data(), out.size());
    return rc == ARGON2_OK;
}

void lockMemory(void* p, size_t n) { VirtualLock(p, n); }
void unlockMemory(void* p, size_t n) { VirtualUnlock(p, n); }

std::string toHex(const u8* p, size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) { s += hx[p[i] >> 4]; s += hx[p[i] & 15]; }
    return s;
}

}  // namespace fp::crypto
