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



namespace msc
{
template<class T>
class vector{

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
  static constexpr std::size_t PAGE_SIZE = 4096; // sysconf(_SC_PAGESIZE);
  static constexpr std::size_t COPY_LIMIT = PAGE_SIZE * 128;
  private:
  static constexpr std::size_t max_capacity_ = (1ull<<40);


  static constexpr std::size_t roundup(std::size_t x, std::size_t a) {return (x+a-1) / a * a;}
  static constexpr std::size_t rounddown(std::size_t x, std::size_t a) {return x / a * a;}


  /**
   * The standard's exposition only synth-three-way, used by operator<=>.
   * An element type that has <=> is ordered by it and keeps its own category;
   * one that only has < is ordered by two calls to it, which can only ever
   * justify a weak_ordering. Without this a legacy type with nothing but
   * operator< would not be comparable at all.
   */
  static constexpr auto synth_three_way =
    []<class U>(const U& a, const U& b){
      if constexpr (std::three_way_comparable<U>) return a <=> b;
      else {
        if(a < b) return std::weak_ordering::less;
        if(b < a) return std::weak_ordering::greater;
        return std::weak_ordering::equivalent;
      }
    };


  /**
   * Byte size of a mapping holding n elements, rounded up to whole pages.
   * Never returns 0: mmap and mremap reject zero length mappings with EINVAL,
   * so an empty vector still owns one page and data() stays a valid pointer.
   */
  static constexpr std::size_t capacity_bytes(std::size_t n) {
    std::size_t bytes = roundup(n * sizeof(T), PAGE_SIZE);
    return bytes == 0 ? PAGE_SIZE : bytes;
  }


  /**
   * Grows the vector by the factor size_multiplier.
   *
   * The factor applies to bytes, so the result has to be converted back to an
   * element count, and that division truncates. Once sizeof(T) reaches a
   * quarter of the current capacity the truncation swallows the entire
   * increment and the call becomes a no op, leaving the caller to construct
   * into a page that was never mapped. Flooring the target one page above the
   * current mapping keeps every call moving; it only binds while the vector is
   * small, so the growth factor is unchanged once 25% exceeds a page.
   */
  void grow(){
    std::size_t target = static_cast<std::size_t>(static_cast<double>(capacity_) * size_multiplier);
    if(target < capacity_ + PAGE_SIZE) target = capacity_ + PAGE_SIZE;
    grow(roundup(target, sizeof(T)) / sizeof(T));
  }


  /**
   * Every path that can enlarge the vector ends up here, so this is the one
   * place the max_size() limit has to be enforced. Beyond conforming to what
   * std::vector promises, the check is what keeps capacity_bytes() safe: it
   * multiplies by sizeof(T), and that product can only overflow for a count
   * this rejects.
   */
  void grow(std::size_t new_cap){
    if(new_cap > max_size()) throw std::length_error("vector::grow: size exceeds max_size()");

    std::size_t new_capacity = capacity_bytes(new_cap);
    if(capacity_ >= new_capacity) return;

    int success = mprotect(static_cast<void*>(data_), new_capacity, PROT_READ | PROT_WRITE);
    if (success) throw std::bad_alloc{};
    capacity_ = new_capacity;
    }


  /**
   * The element count this vector would reach by adding n more, or a throw if
   * that exceeds max_size(). Written as a subtraction because size_ + n is
   * exactly the sum that could wrap, which would turn an impossible request
   * into a small and apparently valid one.
   */
  std::size_t checked_total(std::size_t n) const {
    if(n > max_size() - size_) throw std::length_error("vector: size exceeds max_size()");
    return size_ + n;
  }


    /**
   * Shrinks the capacity of vector to size
   */
  void shrink(){
    shrink(size_);
  }


  /**
   * Shrinks the size of the vector to new size, cuts of elements if size > new_size to size
   */
  void shrink(std::size_t new_size){
    std::size_t new_capacity = capacity_bytes(new_size);
    if(new_capacity == capacity_) return;

    int success = mprotect(data_+new_capacity, capacity_ - new_capacity, PROT_NONE);
    if(success) throw std::bad_alloc{};
    success = madvise(data_+new_capacity, capacity_-new_capacity, MADV_DONTNEED);
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
  void move(std::byte* old_pos, std::size_t old_size,
            std::byte* new_pos, std::size_t new_size){
    void* p = mremap(static_cast<void*>(old_pos), old_size, new_size,
                     MREMAP_MAYMOVE | MREMAP_FIXED, static_cast<void*>(new_pos));
    if (p == MAP_FAILED) throw std::bad_alloc{};
    if(p != new_pos) throw std::runtime_error {"Moving the vector failed"};
  }


  void init(){
     void* p = mmap(nullptr, max_capacity_,
                    PROT_NONE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                    -1, 0);
    if(p == MAP_FAILED) throw std::bad_alloc{};
    data_ = static_cast<std::byte*>(p);
    int success = mprotect(static_cast<void*>(data_), capacity_, PROT_READ | PROT_WRITE);
    if(success) throw std::bad_alloc{}; 
  }


  /**
   * Runs every element's destructor and empties the vector without touching the
   * mapping. clear() releases the pages on top of this; assign() must not, since
   * it refills immediately and dropping the pages would only fault them back in.
   */
  void destroy_all() noexcept {
    std::destroy_n(data(), size_);
    size_ = 0;
  }


  /**
   * Opens a gap of n uninitialized slots at index idx and hands the first of
   * them to construct, which has to fill exactly n. The elements from idx
   * onwards are relocated bitwise, as everywhere else in this class.
   *
   * size_ only grows once construct has succeeded, so if it throws the gap is
   * closed again and the vector is left exactly as it was.
   */
  template<class F>
  T* insert_n(std::size_t idx, std::size_t n, F&& construct){
    if(n == 0) return data() + idx;
    grow(checked_total(n));

    std::byte* from = data_ + idx * sizeof(T);
    const std::size_t tail_bytes = (size_ - idx) * sizeof(T);
    std::memmove(from + n * sizeof(T), from, tail_bytes);

    try {
      construct(data() + idx);
    } catch(...) {
      std::memmove(from, from + n * sizeof(T), tail_bytes);
      throw;
    }
    size_ += n;
    return data() + idx;
  }


  inline void splice_copy(vector<T>& other){
    std::memcpy(data_ + (size_*sizeof(T)), other.data_, other.size_*sizeof(T));
    size_ += other.size_;
    //other is <= PAGE_SIZE no need to remap
    memset(other.data_, 0, other.size_ * sizeof(T));
    other.size_ = 0;
  }

  inline void splice_move(vector<T>& other){
    constexpr std::size_t stride = std::lcm(sizeof(T), PAGE_SIZE);
    const std::size_t size_in_bytes  = size_ * sizeof(T);
    const std::size_t other_used_cap = capacity_bytes(other.size_);
    const std::size_t pagebound_size = rounddown(size_in_bytes, stride);
    const std::size_t keep  = pagebound_size / sizeof(T);
    const std::size_t spill = size_ - keep;

    if(spill == 0){
      // we already end on a boundary, so nothing of ours is in the way
      move(other.data_, other.capacity_, data_ + size_in_bytes, other_used_cap);
    } else if(spill < other.size_){ // copy end of A since its cheaper than copying B
      const std::size_t spill_bytes = spill * sizeof(T);
      auto buf = std::make_unique_for_overwrite<std::byte[]>(spill_bytes);
      std::memcpy(buf.get(), data_ + pagebound_size, spill_bytes);
      move(other.data_, other.capacity_, data_ + pagebound_size, other_used_cap);
      std::memcpy(data_ + pagebound_size + other.size_ * sizeof(T), buf.get(), spill_bytes);
    } else {
      std::memcpy(data_ + size_in_bytes, other.data_, other.size_ * sizeof(T));
    }
    size_ += other.size_;

    munmap(other.data_, max_capacity_);
    other.capacity_ = PAGE_SIZE;
    other.size_ = 0;
    other.data_ = nullptr;
    other.init();
  }


  /**
   * How a single source is going to be consumed by splice_all.
   *
   * body is the leading run of bytes that sits on a stride boundary and can be
   * handed to mremap; tail is whatever is left over and has to be copied. A
   * source small enough for COPY_LIMIT, or shorter than one whole stride, is
   * all tail: the syscall would cost more than the copy it saves.
   */
  struct source_split {
    std::size_t body;
    std::size_t tail;
  };

  static constexpr source_split  split_source(std::size_t bytes){
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
  void splice_all(std::span<vector* const> sources){
    constexpr std::size_t stride = std::lcm(sizeof(T), PAGE_SIZE);

    const std::size_t size_bytes = size_ * sizeof(T);
    const std::size_t first_body     = rounddown(size_bytes, stride);
    const std::size_t first_spill    = size_bytes - first_body;

    // ---- measure, before anything is touched ----
    // alongside the sizes this costs both strategies in bytes copied, because
    // batching is not always the cheaper one. mremap carries a source's tail
    // along for free when the whole mapping moves, which is what the pairwise
    // form does; batching gives that up to keep the bodies adjacent, and pays
    // for it once per source. Which way that goes depends entirely on how close
    // the sources are to a stride boundary, so it is measured rather than assumed
    std::size_t added         = 0;
    std::size_t body_total    = 0;

    for(vector* s : sources){
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
    if(body_total && first_spill){
      std::memmove(data_ + tail_cursor, data_ + first_body, first_spill);
      tail_cursor += first_spill;
    }

    for(vector* s : sources){
      if(s == nullptr || s == this || s->size_ == 0) continue;
      const source_split sp = split_source(s->size_ * sizeof(T));

      if(sp.body){
        // only the body is handed to mremap, so the tail stays put in the
        // source's mapping and can be read afterwards. passing the whole
        // mapping would have the kernel drop everything past the new size
        void* p = mremap(static_cast<void*>(s->data_), sp.body, sp.body,
                         MREMAP_MAYMOVE | MREMAP_FIXED,
                         static_cast<void*>(data_ + body_cursor));
        if(p == MAP_FAILED) throw std::bad_alloc{};
        body_cursor += sp.body;
      }

      // one copy, straight to the place this tail keeps
      if(sp.tail){
        std::memcpy(data_ + tail_cursor, s->data_ + sp.body, sp.tail);
        tail_cursor += sp.tail;
      }

      if(sp.body){
        // the prefix was moved out from under it; this releases what is left,
        // including the reservation beyond the source's live capacity
        munmap(s->data_ + sp.body, max_capacity_ - sp.body);
        s->data_     = nullptr;
        s->size_     = 0;
        s->capacity_ = PAGE_SIZE;
        s->init();
      } else {
        // the bytes now live here, so the source's storage holds no object
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
  using value_type = T;
  using size_type  = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference       = T&;
  using const_reference = const T&;
  using pointer         = T*;
  using const_pointer   = const T*;
  using iterator   = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  vector() : capacity_(PAGE_SIZE){
    init();
  }

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
    try{
      std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_),other.size_, data());
    } catch(...){
      munmap(data_, max_capacity_);
      throw;
    }
    size_ = other.size_;
  }
  vector(vector&& other) noexcept : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
    other.size_     = 0;
    other.capacity_ = PAGE_SIZE;
    other.init();
  }

  vector& operator=(const vector& other){
    if(this == &other) return *this;

    // assigning to a moved from husk is allowed, but it owns no mapping yet
    if(!data_){
      capacity_ = PAGE_SIZE;
      init();
    }

    std::destroy_n(data(), size_);
    size_ = 0;

    // grow() counts elements and only ever grows, so a bigger capacity stays
    grow(other.size_);
    std::uninitialized_copy_n(reinterpret_cast<const T*>(other.data_),
                              other.size_, data());
    size_ = other.size_;
    return *this;
  }

  vector& operator=(vector&& other) noexcept {
    if(this == &other) return *this;

    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);

    data_     = other.data_;
    size_     = other.size_;
    capacity_ = other.capacity_;

    other.size_     = 0;
    other.capacity_ = PAGE_SIZE;
    other.init();
    return *this;
  }

  vector& operator=(std::initializer_list<T> il){
    assign(il);
    return *this;
  }

  ~vector(){
    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);
  }

  /**
   * Replaces the contents. grow() only ever enlarges, so assigning something
   * smaller keeps the capacity that was already there, as std::vector does.
   */
  void assign(size_type count, const T& value){
    // grow first: it is the step that can throw, and it only ever enlarges the
    // mapping without touching an element, so a request that is refused leaves
    // the vector exactly as it was rather than emptied
    grow(count);
    destroy_all();
    std::uninitialized_fill_n(data(), count, value);
    size_ = count;
  }

  template<std::input_iterator It>
  void assign(It first, It last){
    assign_range(std::ranges::subrange(first, last));
  }

  void assign(std::initializer_list<T> il){ assign(il.begin(), il.end()); }

  template<std::ranges::input_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
  void assign_range(R&& rg){
    // where the length is knowable, take the bounds check and the mapping
    // before destroying anything, so an oversized range is refused with the
    // vector intact. a single pass range cannot be measured without consuming
    // it, so there the old contents are gone before the append can fail
    if constexpr (std::ranges::forward_range<R> || std::ranges::sized_range<R>){
      grow(static_cast<size_type>(std::ranges::distance(rg)));
    }
    destroy_all();
    append_range(std::forward<R>(rg));
  }

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Element access
//----------------------------------------------------------------------------------------------------------------------------------------------------
  reference at(size_type i){
    if (i >= size_) throw std::out_of_range("vector::at");
    return data()[i];
  }
  const_reference at(size_type i) const {
    if (i >= size_) throw std::out_of_range("vector::at");
    return data()[i];
  }
  reference       operator[](size_type i)       { return data()[i]; }
  const_reference operator[](size_type i) const { return data()[i]; }
  reference       front()       { return data()[0]; }
  const_reference front() const { return data()[0]; }
  reference       back()        { return data()[size_ - 1]; }
  const_reference back()  const { return data()[size_ - 1]; }
  pointer         data()        { return reinterpret_cast<pointer>(data_); }
  const_pointer   data()  const { return reinterpret_cast<const_pointer>(data_); }
//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Iterators
//----------------------------------------------------------------------------------------------------------------------------------------------------
  iterator       begin()       { return data(); }
  const_iterator begin() const { return data(); }
  iterator       end()         { return data()+size_; }
  const_iterator end()   const { return data()+size_; }
  const_iterator cbegin() const { return begin(); }
  const_iterator cend()   const { return end(); }

  reverse_iterator       rbegin()        { return reverse_iterator(end()); }
  const_reverse_iterator rbegin()  const { return const_reverse_iterator(end()); }
  reverse_iterator       rend()          { return reverse_iterator(begin()); }
  const_reverse_iterator rend()    const { return const_reverse_iterator(begin()); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  const_reverse_iterator crend()   const { return rend(); }

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Capacity
//----------------------------------------------------------------------------------------------------------------------------------------------------
  bool empty() const {return size_ == 0 ? true : false;}
  size_type size() const { return size_; }
  constexpr size_type max_size() const {return max_capacity_ / sizeof(T);}
  void reserve(size_type new_cap){
    grow(new_cap);
  }
  size_type capacity() const {return capacity_ / sizeof(T);}
  void shrink_to_fit(){shrink();}
//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Modifiers
//----------------------------------------------------------------------------------------------------------------------------------------------------
  void clear (){
    destroy_all();
    int success = madvise(data_, capacity_, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    // success = mprotect(data_, capacity_, PROT_NONE);
    //  if(success) throw std::bad_alloc{};
    //  success = mprotect(data_, PAGE_SIZE, PROT_READ | PROT_WRITE);
    //  if(success) throw std::bad_alloc{};
    size_ = 0;
    // capacity_ = PAGE_SIZE;
  }


  iterator insert(const_iterator pos, const T& value){
    return insert_n(pos - cbegin(), 1, [&](T* p){ ::new (p) T(value); });
  }


  iterator insert(const_iterator pos, T&& value){
    return insert_n(pos - cbegin(), 1, [&](T* p){ ::new (p) T(std::move(value)); });
  }


  iterator insert(const_iterator pos, size_type count, const T& value){
    return insert_n(pos - cbegin(), count,
                    [&](T* p){ std::uninitialized_fill_n(p, count, value); });
  }


  template<std::forward_iterator It>
  iterator insert(const_iterator pos, It first, It last){
    const auto count = static_cast<size_type>(std::distance(first, last));
    return insert_n(pos - cbegin(), count,
                    [&](T* p){ std::uninitialized_copy(first, last, p); });
  }

  iterator insert(const_iterator pos, std::initializer_list<T> il){
    return insert(pos, il.begin(), il.end());
  }

  template<class R>
    requires std::ranges::forward_range<R> || std::ranges::sized_range<R>
  iterator insert_range(const_iterator pos, R&& rg){
    const auto count = static_cast<size_type>(std::ranges::distance(rg));
    return insert_n(pos - cbegin(), count, [&](T* p){
      std::uninitialized_copy_n(std::ranges::begin(rg), count, p);
    });
  }

  template<class... Args>
  iterator emplace(const_iterator pos, Args&&... args){
    return insert_n(pos - cbegin(), 1,
                    [&](T* p){ ::new (p) T(std::forward<Args>(args)...); });
  }

  /**
   * Removes [first, last) and closes the gap, returning an iterator to the
   * element that took first's place. Destructors are noexcept, so nothing here
   * can fail part way.
   */
  iterator erase(const_iterator first, const_iterator last){
    const auto idx = static_cast<size_type>(first - cbegin());
    const auto n   = static_cast<size_type>(last - first);
    // the position is const, so the destruction and the returned iterator both
    // go through data() rather than through the parameter
    if(n == 0) return data() + idx;

    std::destroy_n(data() + idx, n);
    std::byte* to = data_ + idx * sizeof(T);
    std::memmove(to, to + n * sizeof(T), (size_ - idx - n) * sizeof(T));
    size_ -= n;
    return data() + idx;
  }

  iterator erase(const_iterator pos){
    return erase(pos, pos + 1);
  }

  /**
   * Both overloads defer to emplace_back, which constructs into the slot.
   * Assigning instead would run operator= on storage no object lives in yet,
   * which is undefined and crashes outright for any type whose assignment
   * reads its own state before overwriting it.
   *
   * push_back(const T&) copies, so it needs a copy constructor. It is a non
   * template member, so it is only instantiated where it is called: a move only
   * T is fine as long as callers stay on the rvalue overload, and asking for the
   * copy is a compile error at the call site rather than anything at runtime.
   *
   * Passing an element of this same vector is safe, which std::vector cannot
   * promise. Growing is an mprotect over a mapping reserved up front, so the
   * storage never moves and val is still live when the copy runs; a reallocating
   * vector would have destroyed it first. This holds for the growth path only,
   * splice() relocates with mremap and insert()/erase() shift with memmove.
   */
  void push_back(const T& val){ emplace_back(val); }
  void push_back(T&& val){ emplace_back(std::move(val)); }
    
  template <class... Args>
  T& emplace_back(Args&&... a) {
    if ((size_+1)*sizeof(T) > capacity_) grow();
    T* slot = data() + size_;
    ::new (slot) T(std::forward<Args>(a)...);   // <-- construction happens HERE
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
  void append_range(R&& rg){
    if constexpr (std::ranges::forward_range<R> || std::ranges::sized_range<R>){
      const auto n = static_cast<size_type>(std::ranges::distance(rg));
      if(n == 0) return;
      grow(checked_total(n));
      // size_ is only committed once every element is built, so a throw part
      // way leaves the vector exactly as it was
      std::uninitialized_copy_n(std::ranges::begin(rg), n, data() + size_);
      size_ += n;
    } else {
      for(auto&& e : rg) emplace_back(std::forward<decltype(e)>(e));
    }
  }

  void pop_back(){ --size_; std::destroy_at(data() + size_); }

  /**
   * Both overloads leave the capacity alone when they shrink. Releasing the
   * pages is shrink_to_fit's job, and the private shrink() that does it only
   * looks like this operation.
   */
  void resize(size_type count){
    if(count < size_){
      std::destroy_n(data() + count, size_ - count);
      size_ = count;
    } else if(count > size_){
      grow(count);
      std::uninitialized_value_construct_n(data() + size_, count - size_);
      size_ = count;
    }
  }

  void resize(size_type count, const T& value){
    if(count < size_){
      std::destroy_n(data() + count, size_ - count);
      size_ = count;
    } else if(count > size_){
      grow(count);
      std::uninitialized_fill_n(data() + size_, count - size_, value);
      size_ = count;
    }
  }

  /**
   * Every vector owns an independent mapping, so this exchanges three scalars
   * and never touches an element. There is no allocator to propagate, which is
   * what makes it unconditionally noexcept.
   */
  void swap(vector& other) noexcept {
    std::swap(data_,     other.data_);
    std::swap(size_,     other.size_);
    std::swap(capacity_, other.capacity_);
  }

  friend void swap(vector& a, vector& b) noexcept { a.swap(b); }

  /**
   * Merges two vectors into one.
   * ORDER IS NOT PRESERVED. Some elements may move to the end of the vector.
   */
  void splice(vector<T>&& other){
    if(this == &other || other.size_ == 0) return;
    
    grow(checked_total(other.size_));
    
    if(other.size_ * sizeof(T) <= COPY_LIMIT){
      splice_copy(other);
    }else{
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
    requires (sizeof...(Vs) > 0)
  void splice(Vs&&... others){
    vector* ptrs[] = { (&others)... };
    splice_all(std::span<vector* const>{ptrs, sizeof...(Vs)});
  }

  /**
   * The same, for a number of sources only known at run time. Takes any range
   * of vectors, for instance a std::vector<msc::vector<T>> of partial results.
   */
  template<std::ranges::input_range R>
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, vector>
  void splice_range(R&& sources){
    std::size_t n = 0;
    for(auto&& s : sources){ (void)s; ++n; }
    if(n == 0) return;

    auto ptrs = std::make_unique_for_overwrite<vector*[]>(n);
    std::size_t i = 0;
    for(auto&& s : sources) ptrs[i++] = std::addressof(s);

    splice_all(std::span<vector* const>{ptrs.get(), n});
  }

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Comparison
//----------------------------------------------------------------------------------------------------------------------------------------------------

  /**
   * Hidden friends, as the standard declares both for std::vector. == is never
   * synthesized from <=>, so the two are independent and both are needed.
   *
   * The constraints keep std::equality_comparable<vector<T>> and
   * std::three_way_comparable<vector<T>> honest: an element type that cannot be
   * compared makes the operator drop out of overload resolution instead of
   * answering "yes" and then hard erroring inside std::equal.
   */
  friend bool operator==(const vector& lhs, const vector& rhs)
    requires std::equality_comparable<T>
  {
    // the four iterator form compares the lengths first for random access
    // iterators, so an unequal size never reaches the element comparisons
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
  }

  friend auto operator<=>(const vector& lhs, const vector& rhs)
    requires std::three_way_comparable<T>
          || requires(const T& a, const T& b){ { a < b } -> std::convertible_to<bool>; }
  {
    return std::lexicographical_compare_three_way(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), synth_three_way);
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
typename vector<T>::size_type erase(vector<T>& c, const U& value){
  auto it = std::remove(c.begin(), c.end(), value);
  const auto removed = static_cast<typename vector<T>::size_type>(c.end() - it);
  c.erase(it, c.end());
  return removed;
}

template<class T, class Pred>
typename vector<T>::size_type erase_if(vector<T>& c, Pred pred){
  auto it = std::remove_if(c.begin(), c.end(), pred);
  const auto removed = static_cast<typename vector<T>::size_type>(c.end() - it);
  c.erase(it, c.end());
  return removed;
}


} // namespace msc