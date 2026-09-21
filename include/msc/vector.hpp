// msc::vector - https://github.com/M-Scheinost/vector
// SPDX-FileCopyrightText: 2026 Manuel Scheinost
// SPDX-License-Identifier: MIT

/*

Copyright (c) 2026 Manuel Scheinost

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#pragma once

#include <sys/mman.h>
#include <type_traits>
#include <memory>
#include <new>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <ranges>
#include <iterator>
#include <initializer_list>
#include <compare>
#include <concepts>
#include <span>
#include <utility>


namespace msc
{
template<class T>
class vector {

private:
  std::byte* data_ = nullptr;
  // in number of T elements
  std::size_t size_ = 0;
  // in bytes
  std::size_t capacity_ = 0;

  // used to grow / shrink the vector;
  static constexpr double size_multiplier = 1.25;
  // get the systems page size (4KiB for amd64, may vary with ARM)
public:
  static constexpr std::size_t PAGE_SIZE  = 4096; // sysconf(_SC_PAGESIZE);
  static constexpr std::size_t COPY_LIMIT = PAGE_SIZE * 128;

private:
  static constexpr std::size_t max_capacity_ = (1ull << 40);


  static constexpr std::size_t roundup(std::size_t x, std::size_t a) { return (x + a - 1) / a * a; }
  static constexpr std::size_t rounddown(std::size_t x, std::size_t a) { return x / a * a; }


  static constexpr auto synth_three_way = []<class U>(const U& a, const U& b) {
    if constexpr(std::three_way_comparable<U>)
      return a <=> b;
    else {
      if(a < b) return std::weak_ordering::less;
      if(b < a) return std::weak_ordering::greater;
      return std::weak_ordering::equivalent;
    }
  };


  static constexpr std::size_t capacity_bytes(std::size_t n) {
    std::size_t bytes = roundup(n * sizeof(T), PAGE_SIZE);
    return bytes == 0 ? PAGE_SIZE : bytes;
  }


  void grow() {
    std::size_t target = static_cast<std::size_t>(static_cast<double>(capacity_) * size_multiplier);
    if(target < capacity_ + PAGE_SIZE) target = capacity_ + PAGE_SIZE;
    grow(roundup(target, sizeof(T)) / sizeof(T));
  }


  void grow(std::size_t new_cap) {
    if(new_cap > max_size()) throw std::length_error("vector::grow: size exceeds max_size()");

    // a moved from vector owns no mapping. It reserves one the first time it
    // has to hold something, which is what keeps the move itself free of any
    // syscall and so genuinely noexcept
    if(!data_) {
      map_reservation();
      capacity_ = 0;
    }

    std::size_t new_capacity = capacity_bytes(new_cap);
    if(capacity_ >= new_capacity) return;

    int success = mprotect(static_cast<void*>(data_), new_capacity, PROT_READ | PROT_WRITE);
    if(success) throw std::bad_alloc{};
    capacity_ = new_capacity;
  }


  std::size_t checked_total(std::size_t n) const {
    if(n > max_size() - size_) throw std::length_error("vector: size exceeds max_size()");
    return size_ + n;
  }

  void shrink() { shrink(size_); }


  void shrink(std::size_t new_size) {
    // without a mapping there is no capacity to hand back
    if(!data_) return;
    std::size_t new_capacity = capacity_bytes(new_size);
    if(new_capacity == capacity_) return;

    int success = mprotect(data_ + new_capacity, capacity_ - new_capacity, PROT_NONE);
    if(success) throw std::bad_alloc{};
    success = madvise(data_ + new_capacity, capacity_ - new_capacity, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    capacity_ = new_capacity;
    if(size_ > new_size) size_ = new_size;
  }


  /**
   * Relocates a mapping onto new_pos. All sizes are in bytes. old_size has to
   * be the whole source mapping, otherwise the part beyond it stays mapped and
   * is leaked; new_size may be smaller, the kernel unmaps the excess.
   * new_pos must be page aligned, MREMAP_FIXED rejects anything else.
   */
  void move(std::byte* old_pos, std::size_t old_size, std::byte* new_pos, std::size_t new_size) {
    void* p = mremap(static_cast<void*>(old_pos), old_size, new_size, MREMAP_MAYMOVE | MREMAP_FIXED,
                     static_cast<void*>(new_pos));
    if(p == MAP_FAILED) throw std::bad_alloc{};
    if(p != new_pos) throw std::runtime_error{"Moving the vector failed"};
  }


  /**
   * Reserves the address range without committing any of it. Kept apart from
   * init() so grow() can call it for a vector that owns no mapping yet.
   */
  void map_reservation() {
    void* p =
        mmap(nullptr, max_capacity_, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if(p == MAP_FAILED) throw std::bad_alloc{};
    data_ = static_cast<std::byte*>(p);
  }


  void init() {
    map_reservation();
    int success = mprotect(static_cast<void*>(data_), capacity_, PROT_READ | PROT_WRITE);
    if(success) throw std::bad_alloc{};
  }


  void destroy_all() noexcept {
    std::destroy_n(data(), size_);
    size_ = 0;
  }


  /**
   * Makes room for n new elements at index idx and has construct build them;
   * construct fills exactly n uninitialized slots from the pointer it is given.
   * grow() never relocates the storage, so anything construct reads out of this
   * vector is still where it was when the insert began.
   *
   * Trivially copyable elements are moved as bytes: the tail is shifted up
   * with memmove and construct builds straight into the gap. The shift would
   * carry along a value that refers into this vector, which is why the value
   * overloads pass a copy. If construct throws, the tail is shifted back and
   * the vector is left exactly as it was.
   *
   * Anything else has to move through its own move operations, as std::vector
   * does: copying the bytes of a std::string or a std::list leaves them
   * pointing at their old address. So construct builds the new elements at the
   * end instead, where a failure disturbs nothing and arguments that refer into
   * this vector are still intact, and they are then rotated into place. The
   * rotation only ever moves between live objects, so even a move that throws
   * part way leaves every slot holding a valid element.
   */
  template<class F>
  T* insert_n(std::size_t idx, std::size_t n, F&& construct) {
    if(n == 0) return data() + idx;
    grow(checked_total(n));

    if constexpr(std::is_trivially_copyable_v<T>) {
      std::byte* from              = data_ + idx * sizeof(T);
      const std::size_t tail_bytes = (size_ - idx) * sizeof(T);
      std::memmove(from + n * sizeof(T), from, tail_bytes);

      try {
        construct(data() + idx);
      } catch(...) {
        std::memmove(from, from + n * sizeof(T), tail_bytes);
        throw;
      }
      size_ += n;
    } else {
      T* const first = data() + idx;
      T* const mid   = data() + size_;
      construct(mid);
      // from here on every slot below size_ holds a live object, so however
      // the rotation ends the destructor sees exactly what exists
      size_ += n;
      rotate_into_place(first, mid, mid + n);
    }
    return data() + idx;
  }


  /**
   * Moves [mid, last) in front of [first, mid). A single element, by far the
   * common case, goes through one temporary and one move_backward, which moves
   * every element once; std::rotate would swap them, at three moves apiece.
   */
  static void rotate_into_place(T* first, T* mid, T* last) {
    if(first == mid) return;
    if(last - mid == 1) {
      T tmp(std::move(*mid));
      std::move_backward(first, mid, last);
      *first = std::move(tmp);
    } else {
      std::rotate(first, mid, last);
    }
  }


  inline void splice_copy(vector<T>& other) {
    std::memcpy(data_ + (size_ * sizeof(T)), other.data_, other.size_ * sizeof(T));
    size_ += other.size_;
    //other is <= PAGE_SIZE no need to remap
    memset(other.data_, 0, other.size_ * sizeof(T));
    other.size_ = 0;
  }

  inline void splice_move(vector<T>& other) {
    constexpr std::size_t stride     = std::lcm(sizeof(T), PAGE_SIZE);
    const std::size_t size_in_bytes  = size_ * sizeof(T);
    const std::size_t other_used_cap = capacity_bytes(other.size_);
    const std::size_t pagebound_size = rounddown(size_in_bytes, stride);
    const std::size_t keep           = pagebound_size / sizeof(T);
    const std::size_t spill          = size_ - keep;

    if(spill == 0) {
      // we already end on a boundary, so nothing of ours is in the way
      move(other.data_, other.capacity_, data_ + size_in_bytes, other_used_cap);
    } else if(spill < other.size_) { // copy end of A since its cheaper than copying B
      const std::size_t spill_bytes = spill * sizeof(T);
      auto buf                      = std::make_unique_for_overwrite<std::byte[]>(spill_bytes);
      std::memcpy(buf.get(), data_ + pagebound_size, spill_bytes);
      move(other.data_, other.capacity_, data_ + pagebound_size, other_used_cap);
      std::memcpy(data_ + pagebound_size + other.size_ * sizeof(T), buf.get(), spill_bytes);
    } else {
      std::memcpy(data_ + size_in_bytes, other.data_, other.size_ * sizeof(T));
    }
    size_ += other.size_;

    // the source is left owning nothing, exactly like a moved from vector. It
    // used to get a fresh reservation here, but if that mmap failed the source
    // was stranded with no mapping and a capacity still claiming one page, so
    // its next push_back wrote through a null pointer
    munmap(other.data_, max_capacity_);
    other.data_     = nullptr;
    other.size_     = 0;
    other.capacity_ = 0;
  }


  struct source_split {
    std::size_t body;
    std::size_t tail;
  };

  static constexpr source_split split_source(std::size_t bytes) {
    constexpr std::size_t stride = std::lcm(sizeof(T), PAGE_SIZE);
    if(bytes <= COPY_LIMIT) return {0, bytes};
    const std::size_t body = rounddown(bytes, stride);
    return {body, bytes - body};
  }


  /**
   * Merges many vectors in one pass.
   *
   * Splicing k sources one at a time keeps re-handling this vector's unaligned
   * spill: every step lifts it into a scratch buffer, mremaps the source over
   * where it sat, and writes it back, so those bytes are copied twice per
   * source. Doing it once for all k sources needs a single split: every source
   * contributes a stride aligned body that mremap can relocate for free, and a
   * leftover tail that has to be copied.
   *
   * The final position of every tail is known before anything moves, because
   * the bodies' total size is known. So each tail goes straight there, copied
   * exactly once, with no staging buffer in between. Only this vector's own
   * spill has to move before the first mremap lands on it, and it too goes
   * directly to its final place.
   *
   * The layout that falls out is
   *     [ this' body ][ body 1 ]..[ body k ][ this' spill ][ tail 1 ]..[ tail k ]
   * so, as with splice(), ORDER IS NOT PRESERVED. When nothing can be mapped
   * the sources are simply appended and the order does survive.
   *
   * The span is walked three times, so it has to be stable across the call, and
   * naming the same vector twice or naming this one is not allowed.
   */
  void splice_all(std::span<vector* const> sources) {
    constexpr std::size_t stride = std::lcm(sizeof(T), PAGE_SIZE);

    const std::size_t size_bytes  = size_ * sizeof(T);
    const std::size_t first_body  = rounddown(size_bytes, stride);
    const std::size_t first_spill = size_bytes - first_body;

    std::size_t added      = 0;
    std::size_t body_total = 0;

    for(vector* s : sources) {
      if(s == nullptr || s == this || s->size_ == 0) continue;
      if(s->size_ > max_size() - size_ - added)
        throw std::length_error("vector::splice_all: size exceeds max_size()");
      added += s->size_;

      const std::size_t bytes = s->size_ * sizeof(T);
      const source_split sp   = split_source(bytes);
      body_total += sp.body;
    }
    if(added == 0) return;

    // can throw, and nothing has been modified yet
    grow(size_ + added);

    // where the bodies start, and where everything copied lands behind them
    std::size_t body_cursor = body_total ? first_body : size_bytes;
    std::size_t tail_cursor = body_cursor + body_total;

    // this vector's spill is the one run that the first mremap would land on,
    // so it moves first. memmove because the two can overlap when the bodies
    // together are shorter than the spill itself
    if(body_total && first_spill) {
      std::memmove(data_ + tail_cursor, data_ + first_body, first_spill);
      tail_cursor += first_spill;
    }

    for(vector* s : sources) {
      if(s == nullptr || s == this || s->size_ == 0) continue;
      const source_split sp = split_source(s->size_ * sizeof(T));

      if(sp.body) {
        // only the body is handed to mremap, so the tail stays put in the
        // source's mapping and can be read afterwards. passing the whole
        // mapping would have the kernel drop everything past the new size
        void* p = mremap(static_cast<void*>(s->data_), sp.body, sp.body,
                         MREMAP_MAYMOVE | MREMAP_FIXED, static_cast<void*>(data_ + body_cursor));
        if(p == MAP_FAILED) throw std::bad_alloc{};
        body_cursor += sp.body;
      }

      if(sp.tail) {
        std::memcpy(data_ + tail_cursor, s->data_ + sp.body, sp.tail);
        tail_cursor += sp.tail;
      }

      if(sp.body) {
        munmap(s->data_ + sp.body, max_capacity_ - sp.body);
        // left owning nothing, as splice() and a move leave their source
        s->data_     = nullptr;
        s->size_     = 0;
        s->capacity_ = 0;
      } else {
        std::memset(s->data_, 0, s->size_ * sizeof(T));
        s->size_ = 0;
      }
    }

    size_ += added;
  }

public:
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Member types
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  using value_type             = T;
  using size_type              = std::size_t;
  using difference_type        = std::ptrdiff_t;
  using reference              = T&;
  using const_reference        = const T&;
  using pointer                = T*;
  using const_pointer          = const T*;
  using iterator               = T*;
  using const_iterator         = const T*;
  using reverse_iterator       = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  vector() : capacity_(PAGE_SIZE) { init(); }

  explicit vector(size_type count) : vector() {
    grow(count);
    std::uninitialized_value_construct_n(data(), count);
    size_ = count;
  }

  vector(size_type count, const T& value) : vector() {
    grow(count);
    std::uninitialized_fill_n(data(), count, value);
    size_ = count;
  }

  template<std::input_iterator It>
  vector(It first, It last) : vector() {
    append_range(std::ranges::subrange(first, last));
  }

  vector(std::initializer_list<T> il) : vector(il.begin(), il.end()) {}

  template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
  vector(std::from_range_t, R&& rg) : vector() {
    append_range(std::forward<R>(rg));
  }

  vector(const vector& other) : capacity_(other.capacity_) {
    init();
    try {
      std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_), other.size_, data());
    } catch(...) {
      munmap(data_, max_capacity_);
      throw;
    }
    size_ = other.size_;
  }

  vector(vector&& other) noexcept
      : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
        capacity_(std::exchange(other.capacity_, 0)) {}

  vector& operator=(const vector& other) {
    if(this == &other) return *this;

    // assigning to a moved from husk is allowed, but it owns no mapping yet
    if(!data_) {
      capacity_ = PAGE_SIZE;
      init();
    }

    std::destroy_n(data(), size_);
    size_ = 0;

    grow(other.size_);
    std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_), other.size_, data());
    size_ = other.size_;
    return *this;
  }

  vector& operator=(vector&& other) noexcept {
    if(this == &other) return *this;

    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);

    data_     = std::exchange(other.data_, nullptr);
    size_     = std::exchange(other.size_, 0);
    capacity_ = std::exchange(other.capacity_, 0);
    return *this;
  }

  vector& operator=(std::initializer_list<T> il) {
    assign(il);
    return *this;
  }

  ~vector() {
    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);
  }

  /**
   * Replaces the contents. grow() only ever enlarges, so assigning something
   * smaller keeps the capacity that was already there, as std::vector does.
   */
  void assign(size_type count, const T& value) {
    grow(count);
    destroy_all();
    std::uninitialized_fill_n(data(), count, value);
    size_ = count;
  }

  template<std::input_iterator It>
  void assign(It first, It last) {
    assign_range(std::ranges::subrange(first, last));
  }

  void assign(std::initializer_list<T> il) { assign(il.begin(), il.end()); }

  template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
  void assign_range(R&& rg) {
    // where the length is knowable, take the bounds check and the mapping
    // before destroying anything, so an oversized range is refused with the
    // vector intact. a single pass range cannot be measured without consuming
    // it, so there the old contents are gone before the append can fail
    if constexpr(std::ranges::forward_range<R> || std::ranges::sized_range<R>) {
      grow(static_cast<size_type>(std::ranges::distance(rg)));
    }
    destroy_all();
    append_range(std::forward<R>(rg));
  }

  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Element access
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  reference at(size_type i) {
    if(i >= size_) throw std::out_of_range("vector::at");
    return data()[i];
  }
  const_reference at(size_type i) const {
    if(i >= size_) throw std::out_of_range("vector::at");
    return data()[i];
  }
  reference operator[](size_type i) { return data()[i]; }
  const_reference operator[](size_type i) const { return data()[i]; }
  reference front() { return data()[0]; }
  const_reference front() const { return data()[0]; }
  reference back() { return data()[size_ - 1]; }
  const_reference back() const { return data()[size_ - 1]; }
  pointer data() { return reinterpret_cast<pointer>(data_); }
  const_pointer data() const { return reinterpret_cast<const_pointer>(data_); }
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Iterators
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  iterator begin() { return data(); }
  const_iterator begin() const { return data(); }
  iterator end() { return data() + size_; }
  const_iterator end() const { return data() + size_; }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend() const { return rend(); }

  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Capacity
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  bool empty() const { return size_ == 0 ? true : false; }
  size_type size() const { return size_; }
  constexpr size_type max_size() const { return max_capacity_ / sizeof(T); }
  void reserve(size_type new_cap) { grow(new_cap); }
  size_type capacity() const { return capacity_ / sizeof(T); }
  void shrink_to_fit() { shrink(); }
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Modifiers
  //----------------------------------------------------------------------------------------------------------------------------------------------------
  void clear() {
    destroy_all();
    // a vector without a mapping has no pages to release
    if(!data_) return;
    int success = madvise(data_, capacity_, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    size_ = 0;
  }


  iterator insert(const_iterator pos, const T& value) { return emplace(pos, value); }


  iterator insert(const_iterator pos, T&& value) { return emplace(pos, std::move(value)); }


  /**
   * value may be an element of this vector itself. On the memmove path the
   * shift would move it before it is read, so a copy goes in instead; the
   * other path builds the new elements before anything moves.
   */
  iterator insert(const_iterator pos, size_type count, const T& value) {
    if constexpr(std::is_trivially_copyable_v<T>) {
      const T copy(value);
      return insert_n(pos - cbegin(), count,
                      [&](T* p) { std::uninitialized_fill_n(p, count, copy); });
    } else {
      return insert_n(pos - cbegin(), count,
                      [&](T* p) { std::uninitialized_fill_n(p, count, value); });
    }
  }


  template<std::forward_iterator It>
  iterator insert(const_iterator pos, It first, It last) {
    const auto count = static_cast<size_type>(std::distance(first, last));
    return insert_n(pos - cbegin(), count, [&](T* p) { std::uninitialized_copy(first, last, p); });
  }

  iterator insert(const_iterator pos, std::initializer_list<T> il) {
    return insert(pos, il.begin(), il.end());
  }

  template<class R>
    requires std::ranges::forward_range<R> || std::ranges::sized_range<R>
  iterator insert_range(const_iterator pos, R&& rg) {
    const auto count = static_cast<size_type>(std::ranges::distance(rg));
    return insert_n(pos - cbegin(), count,
                    [&](T* p) { std::uninitialized_copy_n(std::ranges::begin(rg), count, p); });
  }

  /**
   * The arguments may refer into this vector, as in v.emplace(v.begin(), v[2]).
   * On the memmove path the element is therefore built before the shift and
   * copied into the gap afterwards, which for a trivially copyable type costs
   * no more than a memcpy. The other path builds it in place at the end,
   * before anything has moved.
   */
  template<class... Args>
  iterator emplace(const_iterator pos, Args&&... args) {
    if constexpr(std::is_trivially_copyable_v<T>) {
      // moved rather than copied into the gap, so a trivially copyable type
      // whose copy constructor is deleted still works
      T value(std::forward<Args>(args)...);
      return insert_n(pos - cbegin(), 1, [&](T* p) { ::new(p) T(std::move(value)); });
    } else {
      return insert_n(pos - cbegin(), 1, [&](T* p) { ::new(p) T(std::forward<Args>(args)...); });
    }
  }

  /**
   * Removes [first, last) and closes the gap, returning an iterator to the
   * element that took first's place.
   *
   * Trivially copyable elements have nothing to destroy and the bytes simply
   * close the gap, so nothing here can fail. Anything else is shifted down with
   * its own move assignment and the moved from objects left at the end are
   * destroyed, as std::vector does; if an assignment throws, every slot still
   * holds a valid element.
   */
  iterator erase(const_iterator first, const_iterator last) {
    const auto idx = static_cast<size_type>(first - cbegin());
    const auto n   = static_cast<size_type>(last - first);
    // the position is const, so the shift and the returned iterator both go
    // through data() rather than through the parameter
    if(n == 0) return data() + idx;

    if constexpr(std::is_trivially_copyable_v<T>) {
      std::byte* to = data_ + idx * sizeof(T);
      std::memmove(to, to + n * sizeof(T), (size_ - idx - n) * sizeof(T));
    } else {
      T* const gap = data() + idx;
      std::move(gap + n, data() + size_, gap);
      std::destroy(data() + size_ - n, data() + size_);
    }
    size_ -= n;
    return data() + idx;
  }

  iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

  void push_back(const T& val) { emplace_back(val); }
  void push_back(T&& val) { emplace_back(std::move(val)); }

  template<class... Args>
  T& emplace_back(Args&&... a) {
    if((size_ + 1) * sizeof(T) > capacity_) grow();
    T* slot = data() + size_;
    ::new(slot) T(std::forward<Args>(a)...); // <-- construction happens HERE
    ++size_;
    return *slot;
  }

  /**
   * Appends every element of rg.
   *
   * Unlike insert_range this accepts a single pass range too. insert_n has to
   * know the count before it opens the gap, but appending opens no gap: with
   * nothing after the insertion point the elements can simply be pushed one at
   * a time. Where the count is known the bulk path still takes it, so a sized
   * or multi pass range costs one grow and one uninitialized_copy_n.
   */
  template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
  void append_range(R&& rg) {
    if constexpr(std::ranges::forward_range<R> || std::ranges::sized_range<R>) {
      const auto n = static_cast<size_type>(std::ranges::distance(rg));
      if(n == 0) return;
      grow(checked_total(n));
      // size_ is only committed once every element is built, so a throw part
      // way leaves the vector exactly as it was
      std::uninitialized_copy_n(std::ranges::begin(rg), n, data() + size_);
      size_ += n;
    } else {
      for(auto&& e : rg)
        emplace_back(std::forward<decltype(e)>(e));
    }
  }

  void pop_back() {
    --size_;
    std::destroy_at(data() + size_);
  }

  void resize(size_type count) {
    if(count < size_) {
      std::destroy_n(data() + count, size_ - count);
      size_ = count;
    } else if(count > size_) {
      grow(count);
      std::uninitialized_value_construct_n(data() + size_, count - size_);
      size_ = count;
    }
  }

  void resize(size_type count, const T& value) {
    if(count < size_) {
      std::destroy_n(data() + count, size_ - count);
      size_ = count;
    } else if(count > size_) {
      grow(count);
      std::uninitialized_fill_n(data() + size_, count - size_, value);
      size_ = count;
    }
  }

  void swap(vector& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(size_, other.size_);
    std::swap(capacity_, other.capacity_);
  }

  friend void swap(vector& a, vector& b) noexcept { a.swap(b); }

  /**
   * Merges two vectors into one.
   * ORDER IS NOT PRESERVED. Some elements may move to the end of the vector.
   */
  void splice(vector<T>&& other) {
    if(this == &other || other.size_ == 0) return;

    grow(checked_total(other.size_));

    if(other.size_ * sizeof(T) <= COPY_LIMIT) {
      splice_copy(other);
    } else {
      splice_move(other);
    }
  }

  /**
   * Merges any number of vectors in at once.
   * ORDER IS NOT PRESERVED, as with the two vector form.
   *
   * Prefer this over splicing one at a time: the pairwise form re-copies this
   * vector's unaligned tail on every step, while this copies each unmappable
   * byte exactly once no matter how many sources there are.
   *
   * The sources have to be distinct from each other and from this vector.
   */
  template<std::same_as<vector>... Vs>
    requires(sizeof...(Vs) > 0)
  void splice(Vs&&... others) {
    vector* ptrs[] = {(&others)...};
    splice_all(std::span<vector* const>{ptrs, sizeof...(Vs)});
  }

  /**
   * The same, for a number of sources only known at run time. Takes any range
   * of vectors, for instance a std::vector<msc::vector<T>> of partial results.
   */
  template<std::ranges::input_range R>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, vector>
  void splice_range(R&& sources) {
    std::size_t n = 0;
    for(auto&& s : sources) {
      (void)s;
      ++n;
    }
    if(n == 0) return;

    auto ptrs     = std::make_unique_for_overwrite<vector*[]>(n);
    std::size_t i = 0;
    for(auto&& s : sources)
      ptrs[i++] = std::addressof(s);

    splice_all(std::span<vector* const>{ptrs.get(), n});
  }

  //----------------------------------------------------------------------------------------------------------------------------------------------------
  //    Comparison
  //----------------------------------------------------------------------------------------------------------------------------------------------------

  friend bool operator==(const vector& lhs, const vector& rhs)
    requires std::equality_comparable<T>
  {
    // the four iterator form compares the lengths first for random access
    // iterators, so an unequal size never reaches the element comparisons
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  friend auto operator<=>(const vector& lhs, const vector& rhs)
    requires std::three_way_comparable<T> || requires(const T& a, const T& b) {
      { a < b } -> std::convertible_to<bool>;
    }
  {
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                                                  synth_three_way);
  }
};


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Deduction guides
//----------------------------------------------------------------------------------------------------------------------------------------------------

template<std::input_iterator It>
vector(It, It) -> vector<typename std::iterator_traits<It>::value_type>;

template<std::ranges::input_range R>
vector(std::from_range_t, R&&) -> vector<std::ranges::range_value_t<R>>;


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Non member erase
//----------------------------------------------------------------------------------------------------------------------------------------------------

/**
 * The free erase / erase_if the standard added in C++20, which collapse the
 * remove-erase idiom into one call and return how many elements went. They live
 * in namespace msc and are found by argument dependent lookup, so an unqualified
 * erase(v, 3) resolves to them without a using declaration.
 */
template<class T, class U = T>
typename vector<T>::size_type erase(vector<T>& c, const U& value) {
  auto it            = std::remove(c.begin(), c.end(), value);
  const auto removed = static_cast<typename vector<T>::size_type>(c.end() - it);
  c.erase(it, c.end());
  return removed;
}

template<class T, class Pred>
typename vector<T>::size_type erase_if(vector<T>& c, Pred pred) {
  auto it            = std::remove_if(c.begin(), c.end(), pred);
  const auto removed = static_cast<typename vector<T>::size_type>(c.end() - it);
  c.erase(it, c.end());
  return removed;
}


} // namespace msc