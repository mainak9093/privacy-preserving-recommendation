# References

The two papers the instructor recommended for this topic, plus the supporting reading each
workstream needs. **Everyone reads the two primary papers in full before writing code**
(Phase 0 task 0.5) — the whole design is a composition of them.

---

## Primary — recommended by the instructor

### [PIRSONA]
**You May Also Like… Privacy: Recommendation Systems Meet PIR**
Adithya Vadapalli, Fattaneh Bayatbabolghani, Ryan Henry.
*Proceedings on Privacy Enhancing Technologies* 2021(4):30–53. DOI 10.2478/popets-2021-0059.
<https://petsymposium.org/popets/2021/popets-2021-0059.php>
→ [`Recommendation System PIR.pdf`](Recommendation%20System%20PIR.pdf)

**This is the instructor's own paper.** Content-distribution system where users fetch records via
Hafiz–Henry computationally 1-private multiserver PIR. The servers extract secret-shared
consumption histories *directly from the incoming PIR queries*, then periodically run a bespoke
**4PC Boolean matrix factorization** to refresh the collaborative-filtering model.

What we take from it: **the end-to-end shape.** Private delivery, and the loop that turns delivery
queries into the next round's training data (ARCHITECTURE §7.3).

Read especially: §1.1 system overview, §2.1 Hafiz–Henry PIR and the (2,2)-DPF construction,
§2.2 collaborative filtering.

### [NUDGE]
**Nudge: A Private Recommendations Engine**
Alexandra Henzinger, Emma Dauterman, Henry Corrigan-Gibbs, Dan Boneh.
*35th USENIX Security Symposium*, August 12–14 2026, Baltimore MD. ISBN 978-1-939133-58-8.
<https://www.usenix.org/conference/usenixsecurity26/presentation/henzinger>
→ [`Nudge A Private Recommendations Engine.pdf`](Nudge%20A%20Private%20Recommendations%20Engine.pdf)

Private matrix factorization at scale. Three servers, 2-of-3 replicated secret sharing,
semi-honest, tolerates compromise of one. Replaces gradient descent with **power iteration** cast
as a *matrix-vector program*: matrix–vector steps are non-interactive, and only truncation and
normalization need interaction — both built on function secret sharing. Netflix (0.5M users) in
50 min on 3×192-core; nDCG@20 = 0.29 against 0.31 for non-private neural recommenders.

What we take from it: **the training core.**

Read especially: §3 system design and threat model, §4.1–4.3 matrix-vector programs and the
non-linear protocols, §5 power iteration, §9 differential privacy.

**Read its non-goals carefully (§3.1) — they define our contribution:**
> *"Nudge's goal is to map user ratings into personalized recommendations; it relies on other means
> (e.g., Apple's private relay, Tor, or cryptographic private information retrieval) to let users
> fetch data items in a private way."*

---

## Supporting reading, by workstream

### W1 — FSS core and private delivery
- Boyle, Gilboa, Ishai. *Function Secret Sharing.* EUROCRYPT 2015. — the GGM-tree DPF. **Essential.**
- Boyle, Gilboa, Ishai. *Function Secret Sharing: Improvements and Extensions.* CCS 2016.
- Hafiz, Henry. *A Bit More Than a Bit Is More Than a Bit Better.* PoPETs 2019(4). — the PIR scheme [PIRSONA] uses.
- Henzinger et al. *One Server for the Price of Two: Simple and Fast Single-Server PIR (SimplePIR).* USENIX Security 2023.
- Menon, Wu. *Spiral.* IEEE S&P 2022.
- Vadapalli, Storrier, Henry. *Sabre: Sender-Anonymous Messaging with Fast Audits.* IEEE S&P 2022. — for the malicious-client DPF audit (D9.4).

### W2 — 3PC substrate and non-linear protocols
- Evans, Kolesnikov, Rosulek. *A Pragmatic Introduction to Secure Multi-Party Computation.* (free) — ch. 3.
- Araki, Furukawa, Lindell, Nof, Ohara. *High-Throughput Semi-Honest Secure Three-Party Computation with an Honest Majority.* CCS 2016. — replicated 2-of-3 sharing, the PRF model.
- Lindell. *How to Simulate It.* — you will be asked this in the viva.
- Escudero et al. — truncation on secret-shared data; the low-order carry correction.
- Storrier, Vadapalli, Lyons, Henry. *Grotto.* CCS 2023. — fast DPF-based comparison over `Z_{2^n}`.

### W3 — Factorization and serving
- Nikolaenko et al. *Privacy-Preserving Matrix Factorization.* CCS 2013. — the garbled-circuit approach [NUDGE] beats by four orders of magnitude.
- Koren, Bell, Volinsky. *Matrix Factorization Techniques for Recommender Systems.* IEEE Computer 2009.
- Classical power iteration / subspace iteration — any numerical linear algebra text.

### W4 — Evaluation and security analysis
- [NUDGE] §3.1 (non-goals) and §9 (differential privacy).
- Dwork, Roth. *The Algorithmic Foundations of Differential Privacy.*
- Sasy, Vadapalli, Goldberg. *PRAC.* PoPETs 2024. — for how this community reports round complexity and WAN results.
- Vadapalli, Henry, Goldberg. *Duoram.* USENIX Security 2023. — same.

---

## Notes

- PDFs here are open-access ([PIRSONA] is CC BY-NC-ND via PoPETs; [NUDGE] is USENIX open access)
  and are kept in the repo so the team reads the same version. Cite the papers, not this folder.
- One-page structured notes per paper go in [`../docs/survey/notes/`](../docs/survey/notes/)
  (Phase 1 task 1.1).
