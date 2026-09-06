// ==========================================================================
//  csprng.cpp -- OS-backed cryptographic randomness. See csprng.hpp for why.
// ==========================================================================
#include "oblivrec/csprng.hpp"

#include <windows.h>
#include <bcrypt.h>

#include <cstdio>
#include <cstdlib>

namespace oblivrec {

void RandomBytes(std::uint8_t* out, std::size_t n) {
  if (n == 0) return;

  // BCryptGenRandom takes a ULONG length. Chunk rather than assume the caller
  // never asks for more than 4 GB, since a silent truncation here would leave
  // part of a key as uninitialised stack.
  std::size_t done = 0;
  while (done < n) {
    const std::size_t remaining = n - done;
    const ULONG chunk = static_cast<ULONG>(
        remaining > 0x10000000u ? 0x10000000u : remaining);
    const NTSTATUS st = BCryptGenRandom(
        nullptr, out + done, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (st != 0) {
      // Abort rather than degrade. A weaker fallback would make a broken
      // security guarantee indistinguishable from a working one.
      std::fprintf(stderr,
                   "FATAL: BCryptGenRandom failed (NTSTATUS 0x%08lx). "
                   "Refusing to generate key material from a weaker source.\n",
                   static_cast<unsigned long>(st));
      std::abort();
    }
    done += chunk;
  }
}

}  // namespace oblivrec
