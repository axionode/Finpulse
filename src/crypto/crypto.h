#pragma once
// ADR-0007: only OS crypto (CNG, DPAPI) + reference Argon2id. No home-made primitives.
#include <array>
#include <cstddef>

#include "core/base.h"

namespace fp::crypto {

constexpr size_t kKeySize = 32;
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kSaltSize = 16;

using Key = std::array<u8, kKeySize>;

bool randomBytes(void* out, size_t n);
Key randomKey();

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void update(const void* p, size_t n);
    std::array<u8, 32> finish();
private:
    void* h_ = nullptr;
    u8 obj_[512];
};

// AES-256-GCM. ct has the same length as pt.
bool aesGcmEncrypt(const Key& key, const u8 nonce[kNonceSize], const u8* aad, size_t aadLen,
                   const u8* pt, size_t len, u8* ct, u8 tag[kTagSize]);
bool aesGcmDecrypt(const Key& key, const u8 nonce[kNonceSize], const u8* aad, size_t aadLen,
                   const u8* ct, size_t len, const u8 tag[kTagSize], u8* pt);

// Key wrapping: blob = nonce | ciphertext(32) | tag
Bytes wrapKey(const Key& kek, const Key& dek, const Bytes& aad);
bool unwrapKey(const Key& kek, const Bytes& blob, const Bytes& aad, Key& dek);

// DPAPI, current Windows user.
bool dpapiProtect(const Bytes& in, Bytes& out);
bool dpapiUnprotect(const Bytes& in, Bytes& out);

struct KdfParams {
    u32 memKiB = 64 * 1024;  // ADR-0007: light params for a 4-digit PIN (~150 ms)
    u32 passes = 2;
    u32 lanes = 2;
};
bool argon2id(const void* secret, size_t len, const u8* salt, size_t saltLen, const KdfParams& p, Key& out);

void lockMemory(void* p, size_t n);  // keep secrets out of the page file
void unlockMemory(void* p, size_t n);

std::string toHex(const u8* p, size_t n);

}  // namespace fp::crypto
