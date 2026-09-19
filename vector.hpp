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
   * Grows the vector by the factor size_multiplier
   */
  void grow(){
    std::size_t new_capacity = static_cast<std::size_t>(static_cast<double>(capacity_) * size_multiplier);
    grow(new_capacity / sizeof(T));
  }


  void grow(std::size_t new_cap){
    std::size_t new_capacity = capacity_bytes(new_cap);
    if(capacity_ >= new_capacity) return;

    int success = mprotect(static_cast<void*>(data_), new_capacity, PROT_READ | PROT_WRITE);
    if (success) throw std::bad_alloc{};
    capacity_ = new_capacity;
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
    grow(size_ + n);

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

  explicit vector(size_type size){
    capacity_ = capacity_bytes(size);
    init();
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

  ~vector(){
    std::destroy_n(data(), size_);
    if(data_) munmap(data_, max_capacity_);
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
    if((new_cap*sizeof(T)) > capacity_)
      grow(new_cap);
  }
  size_type capacity() const {return capacity_ / sizeof(T);}
  void shrink_to_fit(){shrink();}
//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Modifiers
//----------------------------------------------------------------------------------------------------------------------------------------------------
  void clear (){
    std::destroy_n(data(), size_);
    int success = madvise(data_, capacity_, MADV_DONTNEED);
    if(success) throw std::bad_alloc{};
    // success = mprotect(data_, capacity_, PROT_NONE);
    //  if(success) throw std::bad_alloc{};
    //  success = mprotect(data_, PAGE_SIZE, PROT_READ | PROT_WRITE);
    //  if(success) throw std::bad_alloc{};
    size_ = 0;
    // capacity_ = PAGE_SIZE;
  }
  /**
   * Inserts before pos and returns an iterator to the first element inserted.
   * Order is preserved: everything from pos onwards moves up, which makes these
   * linear in the number of elements after pos. splice() stays the one
   * operation in this class that is allowed to reorder.
   *
   * Growing uses mprotect and never relocates the mapping, so pos survives the
   * growth; it is the shifting that moves elements, not the reallocation.
   */
  iterator insert(iterator pos, const T& value){
    return insert_n(pos - begin(), 1, [&](T* p){ ::new (p) T(value); });
  }

  iterator insert(iterator pos, T&& value){
    return insert_n(pos - begin(), 1, [&](T* p){ ::new (p) T(std::move(value)); });
  }

  iterator insert(iterator pos, size_type count, const T& value){
    return insert_n(pos - begin(), count,
                    [&](T* p){ std::uninitialized_fill_n(p, count, value); });
  }

  template<std::forward_iterator It>
  iterator insert(iterator pos, It first, It last){
    const auto count = static_cast<size_type>(std::distance(first, last));
    return insert_n(pos - begin(), count,
                    [&](T* p){ std::uninitialized_copy(first, last, p); });
  }

  iterator insert(iterator pos, std::initializer_list<T> il){
    return insert(pos, il.begin(), il.end());
  }

  /**
   * Copies every element of rg in before pos. The range has to be sized or
   * multi pass so the count is known before the gap is opened. To move the
   * elements out of rg instead of copying them, pass std::views::as_rvalue(rg).
   */
  template<class R>
    requires std::ranges::forward_range<R> || std::ranges::sized_range<R>
  iterator insert_range(iterator pos, R&& rg){
    const auto count = static_cast<size_type>(std::ranges::distance(rg));
    return insert_n(pos - begin(), count, [&](T* p){
      std::uninitialized_copy_n(std::ranges::begin(rg), count, p);
    });
  }

  template<class... Args>
  iterator emplace(iterator pos, Args&&... args){
    return insert_n(pos - begin(), 1,
                    [&](T* p){ ::new (p) T(std::forward<Args>(args)...); });
  }

  /**
   * Removes [first, last) and closes the gap, returning an iterator to the
   * element that took first's place. Destructors are noexcept, so nothing here
   * can fail part way.
   */
  iterator erase(iterator first, iterator last){
    const auto idx = static_cast<size_type>(first - begin());
    const auto n   = static_cast<size_type>(last - first);
    if(n == 0) return first;

    std::destroy_n(first, n);
    std::byte* to = data_ + idx * sizeof(T);
    std::memmove(to, to + n * sizeof(T), (size_ - idx - n) * sizeof(T));
    size_ -= n;
    return data() + idx;
  }

  iterator erase(iterator pos){
    return erase(pos, pos + 1);
  }
  void push_back(const T& val){
    if((size_+1)*sizeof(T) > capacity_) grow();
    data()[size_] = val;
    size_++;
  }
    
  template <class... Args>
  T& emplace_back(Args&&... a) {
    if ((size_+1)*sizeof(T) > capacity_) grow();
    T* slot = data() + size_;
    ::new (slot) T(std::forward<Args>(a)...);   // <-- construction happens HERE
    ++size_;
    return *slot;
  }

  void append_range();
  void pop_back(){ --size_; std::destroy_at(data() + size_); }
  void resize();
  void swap();

  /**
   * Merges two vectors into one.
   * ORDER IS NOT PRESERVED. Some elements may move to the end of the vector.
   */
  void splice(vector<T>&& other){
    if(this == &other || other.size_ == 0) return;
    
    grow(size_ + other.size_);
    
    if(other.size_ * sizeof(T) <= COPY_LIMIT){
      splice_copy(other);
    }else{
      splice_move(other);
    }
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



} // namespace msc