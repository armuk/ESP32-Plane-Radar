#pragma once

/**
 * Aircraft tag placement that avoids overlaps (upstream #9). Header-only and
 * free of drawing code so the native unit tests can run it.
 *
 * Tags are placed one at a time, nearest aircraft first. Each tries a few
 * spots around its symbol (beside it toward the centre, then raised,
 * lowered and on the other side); the first spot that hits no earlier tag
 * and no aircraft symbol wins. If none fits, the caller retries with a
 * one-line tag (callsign / route only) and finally draws no tag.
 */

#include <cstddef>
#include <cstdint>

namespace ui::radar {

struct LabelRect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
};

inline bool rectsOverlap(const LabelRect& a, const LabelRect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
         b.y < a.y + a.h;
}

/** Tag spots tried for each aircraft (see candidateRect). */
constexpr int kLabelCandidateCount = 6;

/**
 * Candidate spot `index` for a w x h tag beside a symbol at (x, y).
 * `offset` is the gap from the symbol centre to the tag's near edge.
 * Spots 0-2: preferred side, centred / raised / lowered; 3-5: other side.
 * Each is kept on screen (0..screen_size-1).
 */
inline LabelRect candidateRect(int index, int x, int y, int w, int h,
                               int offset, bool prefer_right,
                               int screen_size) {
  const bool right = (index < 3) == prefer_right;
  int left = right ? x + offset : x - offset - w;
  int top = y - h / 2;
  switch (index % 3) {
    case 1:
      top = y - h - offset / 2;  // raised: bottom edge just above the symbol
      break;
    case 2:
      top = y + offset / 2;  // lowered: top edge just below the symbol
      break;
    default:
      break;
  }
  const int max_left = screen_size - w - 1;
  const int max_top = screen_size - h - 1;
  left = left < 1 ? 1 : (left > max_left ? max_left : left);
  top = top < 1 ? 1 : (top > max_top ? max_top : top);
  LabelRect r;
  r.x = static_cast<int16_t>(left);
  r.y = static_cast<int16_t>(top);
  r.w = static_cast<int16_t>(w);
  r.h = static_cast<int16_t>(h);
  return r;
}

/** Fixed-size list of areas already taken (tags and aircraft symbols). */
template <size_t kCapacity>
class LabelOccupancy {
 public:
  void clear() { count_ = 0; }

  bool add(const LabelRect& r) {
    if (count_ >= kCapacity) {
      return false;
    }
    rects_[count_++] = r;
    return true;
  }

  bool collides(const LabelRect& r, int skip = -1) const {
    for (size_t i = 0; i < count_; ++i) {
      if (static_cast<int>(i) != skip && rectsOverlap(r, rects_[i])) {
        return true;
      }
    }
    return false;
  }

  size_t count() const { return count_; }

 private:
  LabelRect rects_[kCapacity];
  size_t count_ = 0;
};

/**
 * First free spot for a w x h tag, or false. `own_symbol` is the index of
 * this aircraft's own symbol in `taken` (ignored so a tag may touch it).
 */
template <size_t kCapacity>
bool findFreeLabelSpot(const LabelOccupancy<kCapacity>& taken, int x, int y,
                       int w, int h, int offset, bool prefer_right,
                       int screen_size, int own_symbol, LabelRect* out) {
  for (int i = 0; i < kLabelCandidateCount; ++i) {
    const LabelRect r =
        candidateRect(i, x, y, w, h, offset, prefer_right, screen_size);
    if (!taken.collides(r, own_symbol)) {
      *out = r;
      return true;
    }
  }
  return false;
}

}  // namespace ui::radar
