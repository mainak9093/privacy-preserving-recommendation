// ==========================================================================
//  mvp.cpp -- see mvp.hpp, including the correction to the design draft's
//  round-count claim.
// ==========================================================================
#include "oblivrec/mvp.hpp"

#include <sstream>

namespace oblivrec {

template <typename Ring>
void MatVecProgram<Ring>::Push(const SharedMatrix<Ring>& M,
                               std::unique_ptr<NonLinear<Ring>> f) {
  Stage st;
  st.shared = true;
  st.shared_m = M;
  st.rows = M.rows;
  st.cols = M.cols;
  st.f = std::move(f);
  stages_.push_back(std::move(st));
}

template <typename Ring>
void MatVecProgram<Ring>::PushPublic(std::vector<Ring> M, std::uint32_t rows,
                                     std::uint32_t cols,
                                     std::unique_ptr<NonLinear<Ring>> f) {
  if (M.size() != std::size_t(rows) * cols) {
    throw std::invalid_argument("PushPublic: matrix size disagrees with shape");
  }
  Stage st;
  st.shared = false;
  st.public_m = std::move(M);
  st.rows = rows;
  st.cols = cols;
  st.f = std::move(f);
  stages_.push_back(std::move(st));
}

template <typename Ring>
std::uint32_t MatVecProgram<Ring>::DeclaredRounds() const {
  std::uint32_t r = 0;
  for (const auto& st : stages_) {
    // A shared matrix product is itself one round; a public one is free.
    if (st.shared) ++r;
    r += st.f->DeclaredRounds();
  }
  return r;
}

template <typename Ring>
SharedVec<Ring> MatVecProgram<Ring>::Run(Mpc3<Ring>& s,
                                         const SharedVec<Ring>& v) const {
  SharedVec<Ring> cur = v;
  for (const auto& st : stages_) {
    if (st.cols != cur.size()) {
      throw std::invalid_argument("MatVecProgram: stage expects " +
                                  std::to_string(st.cols) +
                                  " inputs, got " + std::to_string(cur.size()));
    }
    cur = st.shared
              ? s.MatVec(st.shared_m, cur)
              : Mpc3<Ring>::MatVecPublic(
                    Span<const Ring>(st.public_m.data(), st.public_m.size()),
                    st.rows, st.cols, cur);
    cur = st.f->Apply(s, cur);
  }
  return cur;
}

template <typename Ring>
std::string MatVecProgram<Ring>::Describe() const {
  std::ostringstream os;
  for (std::size_t i = 0; i < stages_.size(); ++i) {
    const auto& st = stages_[i];
    os << (i ? " -> " : "") << (st.shared ? "[shared " : "[public ") << st.rows
       << "x" << st.cols << "]";
    if (st.f->DeclaredRounds() > 0 || std::string(st.f->Name()) != "noop") {
      os << " -> " << st.f->Name();
    }
  }
  os << "  (" << DeclaredRounds() << " rounds)";
  return os.str();
}

template class MatVecProgram<u64>;
template class MatVecProgram<u128>;
template class NoOp<u64>;
template class NoOp<u128>;

}  // namespace oblivrec
