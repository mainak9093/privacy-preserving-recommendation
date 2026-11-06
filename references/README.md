# References

The two papers OblivRec builds on, and reading by topic. [`INDEX.md`](INDEX.md) lists every work cited, with links. The
papers themselves are not redistributed here.

## The two base papers

### Nudge

**Nudge: A Private Recommendations Engine.** Alexandra Henzinger, Emma Dauterman, Henry Corrigan-Gibbs, Dan Boneh. *35th
USENIX Security Symposium*, 2026.
[Paper page](https://www.usenix.org/conference/usenixsecurity26/presentation/henzinger) ·
[Full version](https://eprint.iacr.org/2026/179) ·
[Reference implementation](https://github.com/NudgeArtifact/private-recs)

Private matrix factorisation at scale. Three servers, 2-of-3 replicated secret sharing, semi-honest, tolerating one
compromise. It replaces gradient descent with power iteration cast as a matrix-vector program, and only truncation and
normalisation need much interaction, both built on function secret sharing. It trains on Netflix (0.5M users) in 50 minutes on
three 192-core machines, with nDCG@20 of 0.29 against 0.31 for non-private neural recommenders.

**What we take from it:** the training core. Its stated non-goal (§3.1) is where OblivRec begins:

> *"Nudge's goal is to map user ratings into personalized recommendations; it relies on other means (e.g., Apple's private
> relay, Tor, or cryptographic private information retrieval) to let users fetch data items in a private way."*

### Pirsona

**You May Also Like... Privacy: Recommendation Systems Meet PIR.** Adithya Vadapalli, Fattaneh Bayatbabolghani, Ryan Henry.
*Proceedings on Privacy Enhancing Technologies* 2021(4):30-53.
[Paper page](https://petsymposium.org/popets/2021/popets-2021-0059.php)

A content-delivery system where users fetch records by multi-server PIR, and the servers extract secret-shared consumption
histories directly from the incoming queries, then periodically run a four-party Boolean matrix factorisation to refresh the
model.

**What we take from it:** the end-to-end shape, private delivery, and the loop that turns delivery queries into the next
round's training data.

## Reading by topic

### Function secret sharing and private delivery

- Boyle, Gilboa, Ishai. *Function Secret Sharing.* EUROCRYPT 2015. The tree-based DPF.
- Boyle, Gilboa, Ishai. *Function Secret Sharing: Improvements and Extensions.* CCS 2016.
- Hafiz, Henry. *A Bit More Than a Bit Is More Than a Bit Better.* PoPETs 2019. The PIR scheme Pirsona uses.
- Henzinger et al. *One Server for the Price of Two: Simple and Fast Single-Server PIR.* USENIX Security 2023.
- Menon, Wu. *Spiral.* IEEE S&P 2022.
- Vadapalli, Storrier, Henry. *Sabre: Sender-Anonymous Messaging with Fast Audits.* IEEE S&P 2022. The audit that would close
  our remaining malicious-client gap.

### Three-party computation and non-linear protocols

- Evans, Kolesnikov, Rosulek. *A Pragmatic Introduction to Secure Multi-Party Computation.* Chapter 3.
- Araki, Furukawa, Lindell, Nof, Ohara. *High-Throughput Semi-Honest Secure Three-Party Computation with an Honest Majority.* CCS
  2016. The replicated-sharing multiplication.
- Boyle et al. *Function Secret Sharing for Mixed-Mode and Fixed-Point Secure Computation.* EUROCRYPT 2021. The comparison gate.
- Lindell. *How to Simulate It.* The simulation technique behind our security sketch.
- Escudero et al. *Improved Primitives for MPC over Mixed Arithmetic-Binary Circuits.* CRYPTO 2020. Truncation on shared data.
- Storrier, Vadapalli, Lyons, Henry. *Grotto.* CCS 2023. Fast DPF-based comparison.

### Matrix factorisation

- Koren, Bell, Volinsky. *Matrix Factorization Techniques for Recommender Systems.* IEEE Computer 2009.
- Nikolaenko et al. *Privacy-Preserving Matrix Factorization.* CCS 2013. The garbled-circuit approach.

### Evaluation and security

- Dwork, Roth. *The Algorithmic Foundations of Differential Privacy.*
- Dwork, Talwar, Thakurta, Zhang. *Analyze Gauss.* STOC 2014. The noise calibration for Nudge's differential privacy.
- Narayanan, Shmatikov. *Robust De-anonymization of Large Sparse Datasets.* IEEE S&P 2008. Why ratings data is identifying.
- Vadapalli, Henry, Goldberg. *Duoram.* USENIX Security 2023, and Vadapalli et al. *PRAC.* PoPETs 2024. How this community
  reports round complexity and wide-area results.
