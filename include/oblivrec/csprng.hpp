// ==========================================================================
//  csprng.hpp -- cryptographically secure randomness for key generation.
//
//  WHY THIS FILE EXISTS.
//
//  The first version of Gen seeded a std::mt19937_64 and used its output
//  directly as DPF seed material. That was a real bug, not a stylistic one.
//
//  The DPF's initial seeds ARE the secret. Everything else in a key (the
//  correction words) is public-looking by construction, and security rests
//  entirely on an adversary being unable to guess or reconstruct the seeds. A
//  Mersenne Twister is fully reconstructible from 624 consecutive 32-bit
//  outputs, so an adversary who saw enough key material could recompute the
//  whole GGM tree and recover alpha. That defeats the single property the
//  whole project exists to provide.
//
//  It also only had about 64 bits of seed entropy, against the 128-bit
//  security parameter the design claims.
//
//  THE FIX. Draw key material from the operating system CSPRNG, via the
//  Windows CNG interface BCryptGenRandom with BCRYPT_USE_SYSTEM_PREFERRED_RNG.
//  bcrypt is a Windows system library, in the same category as ws2_32, so this
//  does not introduce a third-party dependency.
//
//  FAILURE POLICY: abort, never degrade. If the OS cannot supply randomness we
//  terminate rather than silently falling back to something weaker. A quiet
//  fallback is precisely how this class of bug stays invisible, and a crash is
//  a far better outcome than a recommender that looks private and is not.
// ==========================================================================
#ifndef OBLIVREC_CSPRNG_HPP
#define OBLIVREC_CSPRNG_HPP

#include <cstddef>
#include <cstdint>

namespace oblivrec {

// Fill out[0..n) with cryptographically secure random bytes.
// Terminates the process if the OS RNG fails.
void RandomBytes(std::uint8_t* out, std::size_t n);

}  // namespace oblivrec
#endif  // OBLIVREC_CSPRNG_HPP
