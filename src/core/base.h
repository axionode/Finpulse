#pragma once
// Common primitives shared by every layer. No Windows headers here (ADR-0012: core is platform-free).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace fp {

using i64 = std::int64_t;
using u8 = std::uint8_t;
using u32 = std::uint32_t;
using Bytes = std::vector<u8>;

// Error codes that the UI maps to localized messages.
enum class Ec {
    Ok = 0,
    Io,           // file could not be read/written
    Corrupt,      // data damaged or tampered
    WrongPin,     // PIN does not unwrap the key
    Throttled,    // too many wrong PINs, wait
    WrongPassword,// backup password is wrong
    TooNew,       // file was written by a newer Finpulse
    NotFound,
    Invalid,      // validation failed
    Crypto,       // OS crypto failure
};

struct Status {
    Ec code = Ec::Ok;
    std::string detail;  // English diagnostic, never shown with secrets
    bool ok() const { return code == Ec::Ok; }
    explicit operator bool() const { return ok(); }
    static Status fail(Ec c, std::string d = {}) { return {c, std::move(d)}; }
};

template <class T> struct Result {
    T value{};
    Status status;
    bool ok() const { return status.ok(); }
    explicit operator bool() const { return ok(); }
    static Result fail(Ec c, std::string d = {}) { Result r; r.status = Status::fail(c, std::move(d)); return r; }
    static Result of(T v) { Result r; r.value = std::move(v); return r; }
};

// Wipes memory in a way the optimizer cannot remove.
inline void wipe(void* p, size_t n) {
    volatile u8* v = static_cast<volatile u8*>(p);
    while (n--) *v++ = 0;
}

}  // namespace fp
