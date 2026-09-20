#include "gtest/gtest.h"
#include "./vector.hpp"



#include <vector>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <concepts>
#include <type_traits>
#include <compare>
#include <ranges>
#include <limits>
#include <memory>

struct A {
  int a;
  double b;
  bool c;
  char d;
  uint64_t e;

  A(size_t i) : a(static_cast<int>(i)) , b(static_cast<double>(i)), c(static_cast<bool>(i)) , d(static_cast<char>(i)), e(i) {}
  bool operator==(const A& other) const {return a==other.a && b==other.b && c==other.c && d == other.d && e == other.e;}
};

/**
 * Non-trivially constructible and destructible, unlike A, so that the vector
 * has to run a real constructor and destructor for every element instead of
 * getting away with raw bytes. Every instance is counted, which lets a test
 * check the two are balanced and that nothing is constructed twice or leaked.
 *
 * The payload lives on the heap and the object holds no pointer into itself,
 * so an instance stays valid when its bytes are moved to another address.
 * A std::string member would not survive that: a short string points into its
 * own storage, which and splice() would leave dangling.
 */
struct B {
  static inline size_t live        = 0;
  static inline size_t constructed = 0;
  static inline size_t destroyed   = 0;

  static void reset(){ live = 0; constructed = 0; destroyed = 0; }

  int* payload;

  explicit B(size_t i) : payload(new int(static_cast<int>(i))){
    ++live; ++constructed;
  }
  B(const B& other) : payload(new int(*other.payload)){
    ++live; ++constructed;
  }
  B& operator=(const B& other){
    *payload = *other.payload;
    return *this;
  }
  ~B(){
    delete payload;
    payload = nullptr;
    --live; ++destroyed;
  }

  int value() const { return *payload; }
  bool operator==(const B& other) const { return *payload == *other.payload; }
};

static_assert(!std::is_trivially_default_constructible_v<B>);
static_assert(!std::is_trivially_destructible_v<B>);
static_assert(!std::is_trivially_copyable_v<B>);
// A is the opposite on every count, which is what makes the pair useful
static_assert(std::is_trivially_destructible_v<A>);
static_assert(std::is_trivially_copyable_v<A>);


namespace {

constexpr size_t TEST_SIZE = 1024;

// taken straight from the vector rather than copied, so the tests cannot drift
// away from the implementation. neither value depends on T, so any
// instantiation names the same constant
constexpr size_t PAGE_SIZE  = msc::vector<int>::PAGE_SIZE;

// the threshold in splice(): a source of at most this many bytes is copied onto
// the end of the destination, a larger one is relocated with mremap
constexpr size_t COPY_LIMIT = msc::vector<int>::COPY_LIMIT;

// largest element count that still takes the copy path, and the smallest that
// takes the move path. asked per T, so they keep working if the limit ever
// becomes type dependent
template<class T> constexpr size_t copy_path_count(){ return msc::vector<T>::COPY_LIMIT / sizeof(T); }
template<class T> constexpr size_t move_path_count(){ return msc::vector<T>::COPY_LIMIT / sizeof(T) + 1; }

/**
 * Appends n elements, element i holding the value i.
 * Takes the vector by reference so it fills the caller's vector in place rather
 * than a copy of it.
 */
template<class T>
void fill(msc::vector<T>& v, size_t n){
  for(size_t i = 0; i < n; ++i){
    v.emplace_back(i);
  }
}

/**
 * Appends elements until the vector reallocates, so that accessors can be
 * checked against storage that has moved.
 */
template<class T>
void grow_once(msc::vector<T>& v){
  size_t old_capacity = v.capacity();
  while(v.capacity() == old_capacity){
    v.emplace_back(0);
  }
}

} // namespace


TEST(vector, init)
{

  msc::vector<int> a {};

  size_t size = 1024;
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(a.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    ASSERT_EQ(i, a[i]);
  }

  size <<= 1;
  msc::vector<int> b {size};
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(b.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    ASSERT_EQ(i, b[i]);
  }

  msc::vector<A> c (size);
  ASSERT_EQ(c.size(), 0);
  ASSERT_GE(c.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(b.emplace_back(i));
  }
  for(size_t i = 0; i < size; ++i){
    A test {i};
    ASSERT_EQ(test, b[i]);
  }

}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Element access
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, data)
{
  msc::vector<int> a {};
  // storage is mapped up front, so data() is never null
  ASSERT_NE(a.data(), nullptr);

  fill(a, TEST_SIZE);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a.data()[i], static_cast<int>(i));
  }

  a.data()[0] = 99;
  ASSERT_EQ(a.data()[0], 99);
  a.data()[0] = 0;

  for(size_t j = 0; j < 5; ++j){
    grow_once(a);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a.data()[i], static_cast<int>(i));
    }
  }
}

// operator[]
TEST(vector, subscript)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a[i], a.data() + i);
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    a[i] = static_cast<int>(i * 2);
  }
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i * 2));
  }

  grow_once(a);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i * 2));
  }

  msc::vector<A> b {};
  fill(b, TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    A test {i};
    ASSERT_EQ(b[i], test);
    ASSERT_EQ(&b[i], b.data() + i);
  }
  b[0] = A{42};
  ASSERT_EQ(b[0], A{42});
}


TEST(vector, at)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  int zw;
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_NO_THROW(zw = a.at(i));
    ASSERT_EQ(zw, static_cast<int>(i));
  }

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a.at(i), &a[i]);
  }

  a.at(0) = 99;
  ASSERT_EQ(a.at(0), 99);

  ASSERT_THROW(a.at(TEST_SIZE), std::out_of_range);
  ASSERT_THROW(a.at(-1), std::out_of_range);

  msc::vector<int> b {};
  b.reserve(TEST_SIZE);
  ASSERT_THROW(b.at(0), std::out_of_range);
}


TEST(vector, front)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  ASSERT_EQ(a.front(), 0);
  ASSERT_EQ(&a.front(), a.data());

  a.front() = 99;
  ASSERT_EQ(a.front(), 99);
  ASSERT_EQ(a[0], 99);

  a.emplace_back(7);
  ASSERT_EQ(a.front(), 99);

  grow_once(a);
  ASSERT_EQ(a.front(), 99);
  ASSERT_EQ(&a.front(), a.data());
}


TEST(vector, back)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  ASSERT_EQ(a.back(), static_cast<int>(TEST_SIZE - 1));
  ASSERT_EQ(&a.back(), a.data() + a.size() - 1);

  a.back() = 99;
  ASSERT_EQ(a.back(), 99);
  ASSERT_EQ(a[a.size() - 1], 99);

  a.emplace_back(7);
  ASSERT_EQ(a.back(), 7);
  a.pop_back();
  ASSERT_EQ(a.back(), 99);

  grow_once(a);
  ASSERT_EQ(&a.back(), a.data() + a.size() - 1);

  msc::vector<int> b {};
  b.emplace_back(5);
  ASSERT_EQ(&b.front(), &b.back());
}

//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Iterators
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, begin)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.begin(), a.end());

  fill(a, TEST_SIZE);

  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(*a.begin(), 0);
  ASSERT_EQ(&*a.begin(), &a.front());

  *a.begin() = 99;
  ASSERT_EQ(a[0], 99);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(&a.begin()[i], &a[i]);
  }

  grow_once(a);
  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(*a.begin(), 99);

  a.clear();
  ASSERT_EQ(a.begin(), a.data());
  ASSERT_EQ(a.begin(), a.end());
}


TEST(vector, end)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.end(), a.begin());

  fill(a, TEST_SIZE);

  ASSERT_EQ(a.end(), a.data() + a.size());
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(a.size()));
  ASSERT_EQ(&*(a.end() - 1), &a.back());
  ASSERT_EQ(*(a.end() - 1), static_cast<int>(TEST_SIZE - 1));

  a.emplace_back(7);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE + 1));
  ASSERT_EQ(*(a.end() - 1), 7);
  a.pop_back();
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE));

  grow_once(a);
  ASSERT_EQ(a.end(), a.data() + a.size());

  a.clear();
  ASSERT_EQ(a.end(), a.begin());

  msc::vector<int> b {};
  b.reserve(TEST_SIZE * 4);
  fill(b, TEST_SIZE);
  ASSERT_EQ(b.end() - b.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE));
  ASSERT_LT(b.end(), b.data() + b.capacity());
}


TEST(vector, iteration)
{
  msc::vector<int> a {};

  using iterator = decltype(a.begin());
  static_assert(std::contiguous_iterator<iterator>);
  static_assert(std::same_as<std::iter_value_t<iterator>, int>);

  fill(a, TEST_SIZE);

  size_t i = 0;
  for(int& x : a){
    ASSERT_EQ(x, static_cast<int>(i));
    ASSERT_EQ(&x, &a[i]);
    ++i;
  }
  ASSERT_EQ(i, TEST_SIZE);

  for(int& x : a){
    x *= 2;
  }
  for(size_t j = 0; j < TEST_SIZE; ++j){
    ASSERT_EQ(a[j], static_cast<int>(j * 2));
  }

  ASSERT_EQ(std::accumulate(a.begin(), a.end(), 0LL),
            static_cast<long long>(TEST_SIZE - 1) * static_cast<long long>(TEST_SIZE));
  ASSERT_EQ(std::find(a.begin(), a.end(), 42), a.begin() + 21);
  ASSERT_EQ(std::find(a.begin(), a.end(), -1), a.end());
  ASSERT_TRUE(std::is_sorted(a.begin(), a.end()));

  std::vector<int> copy(a.begin(), a.end());
  ASSERT_EQ(copy.size(), TEST_SIZE);
  ASSERT_TRUE(std::equal(a.begin(), a.end(), copy.begin(), copy.end()));

  // mutating algorithms rearrange the storage in place
  std::reverse(a.begin(), a.end());
  ASSERT_EQ(a.front(), static_cast<int>((TEST_SIZE - 1) * 2));
  ASSERT_EQ(a.back(), 0);
  std::sort(a.begin(), a.end());
  ASSERT_TRUE(std::equal(a.begin(), a.end(), copy.begin(), copy.end()));

  msc::vector<A> b {};
  fill(b, TEST_SIZE);
  size_t k = 0;
  for(const A& x : b){
    ASSERT_EQ(x, A{k});
    ASSERT_EQ(&x, &b[k]);
    ++k;
  }
  ASSERT_EQ(k, TEST_SIZE);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Const access
//----------------------------------------------------------------------------------------------------------------------------------------------------

// the member aliases are the container's public contract, so pin them down
// rather than only checking that the accessors are const qualified. asserting
// both halves means a change to one without the other cannot slip through
using V = msc::vector<int>;
static_assert(std::same_as<V::value_type, int>);
static_assert(std::same_as<V::reference, int&>);
static_assert(std::same_as<V::const_reference, const int&>);
static_assert(std::same_as<V::pointer, int*>);
static_assert(std::same_as<V::const_pointer, const int*>);
static_assert(std::same_as<V::const_iterator, const int*>);

// only reachable once begin() and end() have const overloads
static_assert(std::ranges::range<const V>);
static_assert(std::ranges::contiguous_range<const V>);
static_assert(std::ranges::sized_range<const V>);


TEST(vector, const_element_access)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);
  const msc::vector<int>& c = a;

  static_assert(std::same_as<decltype(c[0]), V::const_reference>);
  static_assert(std::same_as<decltype(c.at(0)), V::const_reference>);
  static_assert(std::same_as<decltype(c.front()), V::const_reference>);
  static_assert(std::same_as<decltype(c.back()), V::const_reference>);
  static_assert(std::same_as<decltype(c.data()), V::const_pointer>);
  // the non-const overloads must still win on a non-const object
  static_assert(std::same_as<decltype(a[0]), V::reference>);
  static_assert(std::same_as<decltype(a.data()), V::pointer>);

  ASSERT_EQ(c.size(), TEST_SIZE);
  ASSERT_FALSE(c.empty());
  ASSERT_EQ(c.front(), 0);
  ASSERT_EQ(c.back(), static_cast<int>(TEST_SIZE - 1));

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(c[i], static_cast<int>(i));
    ASSERT_EQ(c.at(i), static_cast<int>(i));
    ASSERT_EQ(c.data()[i], static_cast<int>(i));
    // the const views must name the same storage, not a copy of it
    ASSERT_EQ(&c[i], &a[i]);
  }

  ASSERT_EQ(&c.front(), c.data());
  ASSERT_EQ(&c.back(), c.data() + c.size() - 1);

  ASSERT_THROW(c.at(c.size()), std::out_of_range);
  ASSERT_THROW(c.at(TEST_SIZE * 2), std::out_of_range);
  ASSERT_NO_THROW(c.at(TEST_SIZE - 1));

  const msc::vector<int> empty_vec {};
  ASSERT_TRUE(empty_vec.empty());
  ASSERT_EQ(empty_vec.size(), 0u);
  ASSERT_NE(empty_vec.data(), nullptr);
  ASSERT_THROW(empty_vec.at(0), std::out_of_range);
}


TEST(vector, const_iteration)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);
  const msc::vector<int>& c = a;

  static_assert(std::same_as<decltype(c.begin()), V::const_iterator>);
  static_assert(std::same_as<decltype(c.cbegin()), V::const_iterator>);
  static_assert(std::same_as<decltype(a.begin()), V::iterator>);

  ASSERT_EQ(c.begin(), c.data());
  ASSERT_EQ(c.end() - c.begin(), static_cast<std::ptrdiff_t>(c.size()));
  ASSERT_EQ(c.cbegin(), c.begin());
  ASSERT_EQ(c.cend(), c.end());
  // the const iterators address the same elements as the mutable ones
  ASSERT_EQ(c.begin(), a.begin());

  size_t i = 0;
  for(const int& x : c){
    ASSERT_EQ(x, static_cast<int>(i));
    ASSERT_EQ(&x, &a[i]);
    ++i;
  }
  ASSERT_EQ(i, TEST_SIZE);

  ASSERT_TRUE(std::is_sorted(c.begin(), c.end()));
  ASSERT_EQ(std::find(c.cbegin(), c.cend(), 42), c.cbegin() + 42);
  ASSERT_EQ(std::accumulate(c.begin(), c.end(), 0LL),
            static_cast<long long>(TEST_SIZE - 1) * static_cast<long long>(TEST_SIZE) / 2);

  const msc::vector<int> empty_vec {};
  ASSERT_EQ(empty_vec.begin(), empty_vec.end());
  ASSERT_EQ(empty_vec.rbegin(), empty_vec.rend());
}


TEST(vector, reverse_iteration)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);
  const msc::vector<int>& c = a;

  static_assert(std::same_as<decltype(a.rbegin()), V::reverse_iterator>);
  static_assert(std::same_as<decltype(c.rbegin()), V::const_reverse_iterator>);
  static_assert(std::same_as<decltype(c.crbegin()), V::const_reverse_iterator>);

  ASSERT_EQ(a.rend() - a.rbegin(), static_cast<std::ptrdiff_t>(a.size()));
  ASSERT_EQ(*a.rbegin(), static_cast<int>(TEST_SIZE - 1));
  ASSERT_EQ(&*a.rbegin(), &a.back());
  ASSERT_EQ(&*(a.rend() - 1), &a.front());

  // reverse order has to be the forward order read backwards
  size_t i = TEST_SIZE;
  for(auto it = c.rbegin(); it != c.rend(); ++it){
    --i;
    ASSERT_EQ(*it, static_cast<int>(i));
    ASSERT_EQ(&*it, &a[i]);
  }
  ASSERT_EQ(i, 0u);

  i = TEST_SIZE;
  for(auto it = c.crbegin(); it != c.crend(); ++it){
    --i;
    ASSERT_EQ(*it, static_cast<int>(i));
  }
  ASSERT_EQ(i, 0u);

  // the mutable reverse iterator writes through to the storage
  *a.rbegin() = 99;
  ASSERT_EQ(a.back(), 99);
  std::reverse(a.rbegin(), a.rend());
  ASSERT_EQ(a.front(), 99);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Capacity
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, empty)
{
  msc::vector<int> a {};
  ASSERT_TRUE(a.empty());

  a.emplace_back(1);
  ASSERT_FALSE(a.empty());

  a.pop_back();
  ASSERT_TRUE(a.empty());

  fill(a, TEST_SIZE);
  ASSERT_FALSE(a.empty());

  a.clear();
  ASSERT_TRUE(a.empty());

  msc::vector<int> b {};
  b.reserve(TEST_SIZE);
  ASSERT_TRUE(b.empty());

  msc::vector<int> c (TEST_SIZE);
  ASSERT_TRUE(c.empty());

  ASSERT_EQ(a.empty(), a.size() == 0);
  ASSERT_EQ(a.empty(), a.begin() == a.end());
}


TEST(vector, size)
{
  msc::vector<int> a {};
  ASSERT_EQ(a.size(), 0);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    a.emplace_back(i);
    ASSERT_EQ(a.size(), i + 1);
  }

  for(size_t i = TEST_SIZE; i > 0; --i){
    ASSERT_EQ(a.size(), i);
    a.pop_back();
  }
  ASSERT_EQ(a.size(), 0);

  fill(a, TEST_SIZE);
  a.push_back(7);
  ASSERT_EQ(a.size(), TEST_SIZE + 1);

  ASSERT_EQ(a.size(), static_cast<size_t>(a.end() - a.begin()));

  a.clear();
  ASSERT_EQ(a.size(), 0);

  msc::vector<int> b {};
  fill(b, TEST_SIZE);
  grow_once(b);
  size_t grown = b.size();
  b.reserve(b.capacity() * 4);
  ASSERT_EQ(b.size(), grown);
}


TEST(vector, max_size)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.max_size(), (1ull << 40) / sizeof(int));

  size_t before = a.max_size();
  fill(a, TEST_SIZE);
  ASSERT_EQ(a.max_size(), before);
  a.clear();
  ASSERT_EQ(a.max_size(), before);

  ASSERT_GT(a.max_size(), a.size());
  ASSERT_GT(a.max_size(), a.capacity());

  static_assert(sizeof(A) > sizeof(int));
  msc::vector<A> b {};
  ASSERT_EQ(b.max_size(), (1ull << 40) / sizeof(A));
  ASSERT_LT(b.max_size(), a.max_size());
}


TEST(vector, capacity)
{
  msc::vector<int> a {};

  ASSERT_EQ(a.capacity(), PAGE_SIZE / sizeof(int));

  size_t last = a.capacity();
  for(size_t i = 0; i < TEST_SIZE * 8; ++i){
    a.emplace_back(i);
    ASSERT_GE(a.capacity(), a.size());
    ASSERT_GE(a.capacity(), last);
    last = a.capacity();
  }

  ASSERT_EQ((a.capacity() * sizeof(int)) % PAGE_SIZE, 0);

  size_t grown = a.capacity();
  a.pop_back();
  ASSERT_EQ(a.capacity(), grown);
  a.clear();
  ASSERT_EQ(a.capacity(), grown);

  msc::vector<int> b (TEST_SIZE);
  ASSERT_GE(b.capacity(), TEST_SIZE);
  ASSERT_EQ(b.size(), 0);

  msc::vector<int> d (0);
  ASSERT_EQ(d.capacity(), PAGE_SIZE / sizeof(int));
  ASSERT_EQ(d.size(), 0);
  ASSERT_NE(d.data(), nullptr);

  msc::vector<A> c {};
  ASSERT_EQ(c.capacity(), PAGE_SIZE / sizeof(A));
  ASSERT_LT(c.capacity(), a.capacity());
}


TEST(vector, shrink_to_fit)
{
  msc::vector<int> a {};
  a.reserve(TEST_SIZE * 16);
  fill(a, TEST_SIZE);

  size_t grown = a.capacity();
  ASSERT_GT(grown, a.size());

  a.shrink_to_fit();

  ASSERT_LT(a.capacity(), grown);
  ASSERT_GE(a.capacity(), a.size());
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  a.emplace_back(7);
  ASSERT_EQ(a.back(), 7);
  ASSERT_EQ(a.size(), TEST_SIZE + 1);

  a.shrink_to_fit();
  size_t fitted = a.capacity();
  a.shrink_to_fit();
  ASSERT_EQ(a.capacity(), fitted);

  msc::vector<int> b {};
  b.reserve(TEST_SIZE * 16);
  fill(b, TEST_SIZE);
  b.clear();
  ASSERT_NO_THROW(b.shrink_to_fit());
  ASSERT_EQ(b.size(), 0);
  ASSERT_TRUE(b.empty());

  msc::vector<int> c {};
  ASSERT_NO_THROW(c.shrink_to_fit());
  ASSERT_EQ(c.size(), 0);

  ASSERT_EQ(c.capacity(), PAGE_SIZE / sizeof(int));
  ASSERT_NE(c.data(), nullptr);
  ASSERT_EQ(c.begin(), c.end());
  c.emplace_back(7);
  ASSERT_EQ(c.back(), 7);
}


TEST(vector, reserve)
{
  msc::vector<int> a {};
  size_t size = 1024;
  a.reserve(size);
  ASSERT_EQ(a.size(), 0);
  ASSERT_GE(a.capacity(), size);

  size <<= 1;
  a.reserve(size);
  ASSERT_EQ(a.size(), 0);
  ASSERT_GE(a.capacity(), size);
  for(size_t i = 0; i < size; i++){
    ASSERT_NO_THROW(a.emplace_back(i));
  }
  ASSERT_EQ(a.size(), size);
  ASSERT_GE(a.capacity(), size);

  size >>= 1;
  a.reserve(size);
  ASSERT_EQ(a.size(), (size << 1));
  ASSERT_GE(a.capacity(), size);


  msc::vector<A> b (size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);

  size <<= 1;
  b.reserve(size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);

  size >>= 1;
  b.reserve(size);
  ASSERT_EQ(b.size(), 0);
  ASSERT_GE(b.capacity(), size);
}


namespace {

/**
 * An element large enough that the implicit growth factor has to be handled in
 * element counts rather than bytes. Filled with a byte pattern derived from the
 * index so a relocation that loses or duplicates an element is visible.
 */
template<size_t N>
struct Pad {
  char pad[N];
  explicit Pad(size_t i){ std::memset(pad, static_cast<int>(i & 0xFF), N); }
  bool operator==(const Pad& o) const { return std::memcmp(pad, o.pad, N) == 0; }
};

/**
 * Grows a default constructed vector one element at a time. The growth factor
 * is applied to a byte capacity and converted back to an element count, and
 * that division truncates: once sizeof(T) reaches a quarter of the current
 * capacity the increment rounds away to nothing and the vector hands out a slot
 * in a page it never mapped. A default constructed vector starts at one page,
 * so anything from PAGE_SIZE/2 up used to fault on the first growth.
 */
template<size_t N>
void check_growth(){
  constexpr size_t count = 24;
  msc::vector<Pad<N>> v;

  for(size_t i = 0; i < count; ++i){
    v.emplace_back(i);
    ASSERT_GE(v.capacity(), v.size()) << "sizeof(T) " << N << " at " << i;
  }
  ASSERT_EQ(v.size(), count);
  for(size_t i = 0; i < count; ++i){
    ASSERT_EQ(v[i], Pad<N>{i}) << "sizeof(T) " << N << " at " << i;
  }

  // push_back reaches the same growth path
  msc::vector<Pad<N>> w;
  for(size_t i = 0; i < count; ++i){
    Pad<N> e{i};
    w.push_back(e);
  }
  ASSERT_EQ(w.size(), count);
  for(size_t i = 0; i < count; ++i){
    ASSERT_EQ(w[i], Pad<N>{i}) << "sizeof(T) " << N << " at " << i;
  }
}

} // namespace


TEST(vector, growth_large_elements)
{
  // straddles PAGE_SIZE/2, where the truncation starts swallowing the increment
  check_growth<64>();
  check_growth<512>();
  check_growth<1024>();
  check_growth<PAGE_SIZE / 2>();
  check_growth<PAGE_SIZE>();
  check_growth<PAGE_SIZE * 2>();
}


TEST(vector, growth_is_monotonic)
{
  // every grow() has to enlarge the mapping. a call that returns without
  // changing the capacity leaves the next element without storage
  msc::vector<Pad<PAGE_SIZE>> v;
  size_t last = v.capacity();
  for(size_t i = 0; i < 16; ++i){
    v.emplace_back(i);
    ASSERT_GE(v.capacity(), v.size());
    ASSERT_GE(v.capacity(), last);
    last = v.capacity();
  }
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Modifiers
//----------------------------------------------------------------------------------------------------------------------------------------------------

TEST(vector, splice_copy_path)
{
  // a source of at most COPY_LIMIT is memcpy'd onto the end of the
  // destination, so this path is an ordered append: both runs stay intact and
  // in their original sequence
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, TEST_SIZE);
  fill(b, TEST_SIZE);
  ASSERT_LE(b.size() * sizeof(int), COPY_LIMIT);

  a.splice(std::move(b));

  ASSERT_EQ(a.size(), TEST_SIZE * 2);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(TEST_SIZE * 2));
  ASSERT_GE(a.capacity(), TEST_SIZE * 2);

  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[TEST_SIZE + i], static_cast<int>(i));
  }

  ASSERT_EQ(a.front(), 0);
  ASSERT_EQ(a.back(), static_cast<int>(TEST_SIZE - 1));
  size_t seen = 0;
  for(int& x : a){
    ASSERT_EQ(x, static_cast<int>(seen % TEST_SIZE));
    ++seen;
  }
  ASSERT_EQ(seen, TEST_SIZE * 2);
}


TEST(vector, splice_move_path)
{
  // a source above the threshold is relocated instead of copied. the
  // destination ends exactly on a page boundary here, so nothing of it is
  // displaced and the result still reads back in order
  const size_t big = move_path_count<int>();
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, TEST_SIZE);
  fill(b, big);
  ASSERT_GT(b.size() * sizeof(int), COPY_LIMIT);
  ASSERT_EQ((a.size() * sizeof(int)) % PAGE_SIZE, 0);

  a.splice(std::move(b));

  ASSERT_EQ(a.size(), TEST_SIZE + big);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }
  for(size_t i = 0; i < big; ++i){
    ASSERT_EQ(a[TEST_SIZE + i], static_cast<int>(i));
  }
}


TEST(vector, splice_move_path_displaces_tail)
{
  // three ints do not reach one stride, so the landing offset rounds down to 0:
  // the source takes the front of the mapping and the destination's own three
  // elements are parked in a buffer and put back behind it. only the move path
  // reorders, which is why the source has to be over the threshold here
  const size_t big = move_path_count<int>();
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, 3);
  fill(b, big);
  ASSERT_NE((a.size() * sizeof(int)) % PAGE_SIZE, 0);

  a.splice(std::move(b));

  ASSERT_EQ(a.size(), big + 3);
  ASSERT_EQ(a.end() - a.begin(), static_cast<std::ptrdiff_t>(big + 3));
  for(size_t i = 0; i < big; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }
  for(size_t i = 0; i < 3; ++i){
    ASSERT_EQ(a[big + i], static_cast<int>(i));
  }
}


TEST(vector, splice_threshold_boundary)
{
  // a source of exactly COPY_LIMIT still copies, one element more moves.
  // both have to end up holding the same elements
  for(size_t n : {copy_path_count<int>(), move_path_count<int>()}){
    msc::vector<int> a {};
    msc::vector<int> b {};
    fill(a, TEST_SIZE);
    fill(b, n);

    a.splice(std::move(b));
    ASSERT_EQ(a.size(), TEST_SIZE + n) << "n " << n;

    std::vector<int> got(a.begin(), a.end());
    std::vector<int> want;
    for(size_t i = 0; i < TEST_SIZE; ++i) want.push_back(static_cast<int>(i));
    for(size_t i = 0; i < n; ++i)         want.push_back(static_cast<int>(i));
    std::sort(got.begin(), got.end());
    std::sort(want.begin(), want.end());
    ASSERT_EQ(got, want) << "n " << n;
  }
}


TEST(vector, splice_leaves_source_empty_and_usable)
{
  // the copy path keeps the source's mapping and the move path gives it a
  // fresh one, but either way it has to come back as a valid empty vector
  for(size_t n : {TEST_SIZE, move_path_count<int>()}){
    msc::vector<int> a {};
    msc::vector<int> b {};
    fill(a, TEST_SIZE);
    fill(b, n);

    a.splice(std::move(b));

    ASSERT_EQ(b.size(), 0u)        << "n " << n;
    ASSERT_TRUE(b.empty())         << "n " << n;
    ASSERT_NE(b.data(), nullptr)   << "n " << n;
    ASSERT_EQ(b.begin(), b.end())  << "n " << n;

    fill(b, 10);
    ASSERT_EQ(b.size(), 10u) << "n " << n;
    for(size_t i = 0; i < 10; ++i){
      ASSERT_EQ(b[i], static_cast<int>(i)) << "n " << n;
    }
  }
}


TEST(vector, splice_non_trivial_relocates_without_copying)
{
  // both paths move an element's bytes rather than constructing a copy, so no
  // constructor and no destructor may run for an element that is spliced over
  for(size_t n : {size_t{64}, move_path_count<B>()}){
    B::reset();
    {
      msc::vector<B> a {};
      msc::vector<B> b {};
      fill(a, 40);
      fill(b, n);
      const size_t live_before  = B::live;
      const size_t built_before = B::constructed;

      a.splice(std::move(b));

      ASSERT_EQ(a.size(), 40 + n)              << "n " << n;
      ASSERT_EQ(B::live, live_before)          << "n " << n;
      ASSERT_EQ(B::constructed, built_before)  << "n " << n;
      ASSERT_EQ(B::destroyed, 0u)              << "n " << n;

      std::vector<int> got;
      for(B& x : a) got.push_back(x.value());
      std::vector<int> want;
      for(size_t i = 0; i < 40; ++i) want.push_back(static_cast<int>(i));
      for(size_t i = 0; i < n; ++i)  want.push_back(static_cast<int>(i));
      std::sort(got.begin(), got.end());
      std::sort(want.begin(), want.end());
      ASSERT_EQ(got, want) << "n " << n;
    }
    // every element is destroyed exactly once, by whichever vector owns it
    ASSERT_EQ(B::live, 0u) << "n " << n;
    ASSERT_EQ(B::destroyed, B::constructed) << "n " << n;
  }
}


TEST(vector, splice_reordering_keeps_every_element)
{
  // whatever path splice takes, the result has to hold exactly the elements of
  // both inputs. checked as a multiset, because the move path does not leave
  // the displaced elements in place
  for(size_t source : {TEST_SIZE * 4, move_path_count<int>()}){
    for(size_t prefix : {size_t{1}, size_t{2}, PAGE_SIZE/sizeof(int) - 1,
                         PAGE_SIZE/sizeof(int), PAGE_SIZE/sizeof(int) + 1,
                         TEST_SIZE + 7}){
      msc::vector<int> a {};
      msc::vector<int> b {};
      fill(a, prefix);
      fill(b, source);

      a.splice(std::move(b));
      ASSERT_EQ(a.size(), prefix + source) << "prefix " << prefix << " source " << source;

      std::vector<int> got(a.begin(), a.end());
      std::vector<int> want;
      for(size_t i = 0; i < prefix; ++i) want.push_back(static_cast<int>(i));
      for(size_t i = 0; i < source; ++i) want.push_back(static_cast<int>(i));
      std::sort(got.begin(), got.end());
      std::sort(want.begin(), want.end());
      ASSERT_EQ(got, want) << "prefix " << prefix << " source " << source;
    }
  }
}


TEST(vector, splice_stride_exceeds_page)
{
  // sizeof(C) is 24, which does not divide the page size, so a landing offset
  // has to be a multiple of lcm(4096, 24) == 12288 rather than of 4096. any
  // page boundary that is not also an element boundary would slice an element
  struct C {
    uint64_t a, b, c;
    explicit C(size_t i) : a(i), b(i*2), c(i*3) {}
    bool operator==(const C& o) const {return a==o.a && b==o.b && c==o.c;}
  };
  static_assert(sizeof(C) == 24);
  static_assert(PAGE_SIZE % sizeof(C) != 0);

  const size_t stride_elems = std::lcm(sizeof(C), PAGE_SIZE) / sizeof(C);
  ASSERT_EQ(stride_elems, 512u);

  // one source under the copy threshold and one over it, so both paths run
  for(size_t source : {stride_elems * 4, move_path_count<C>()}){
    for(size_t prefix : {size_t{1}, stride_elems - 1, stride_elems,
                         stride_elems + 3, stride_elems * 2 + 100}){
      msc::vector<C> a {};
      msc::vector<C> b {};
      fill(a, prefix);
      fill(b, source);

      a.splice(std::move(b));
      ASSERT_EQ(a.size(), prefix + source) << "prefix " << prefix << " source " << source;

      // every element still reads back as a whole C, which is what a landing
      // offset in the middle of an element would destroy
      std::vector<uint64_t> got;
      for(C& x : a){
        ASSERT_EQ(x.b, x.a * 2) << "prefix " << prefix << " source " << source;
        ASSERT_EQ(x.c, x.a * 3) << "prefix " << prefix << " source " << source;
        got.push_back(x.a);
      }
      std::vector<uint64_t> want;
      for(size_t i = 0; i < prefix; ++i) want.push_back(i);
      for(size_t i = 0; i < source; ++i) want.push_back(i);
      std::sort(got.begin(), got.end());
      std::sort(want.begin(), want.end());
      ASSERT_EQ(got, want) << "prefix " << prefix << " source " << source;
    }
  }
}


TEST(vector, splice_edge_cases)
{
  // an empty source leaves the destination untouched
  msc::vector<int> a {};
  msc::vector<int> empty {};
  fill(a, TEST_SIZE);
  a.splice(std::move(empty));
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  // splicing a vector into itself is a no op rather than a corruption
  a.splice(std::move(a));
  ASSERT_EQ(a.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(a[i], static_cast<int>(i));
  }

  // an empty destination takes everything, appending at offset 0
  msc::vector<int> c {};
  msc::vector<int> d {};
  fill(d, TEST_SIZE);
  c.splice(std::move(d));
  ASSERT_EQ(c.size(), TEST_SIZE);
  for(size_t i = 0; i < TEST_SIZE; ++i){
    ASSERT_EQ(c[i], static_cast<int>(i));
  }

  // a class type splices the same way. sizeof(A) divides the page size, so
  // filling one page worth keeps the append page aligned
  const size_t per_page = PAGE_SIZE / sizeof(A);
  msc::vector<A> e {};
  msc::vector<A> f {};
  fill(e, per_page);
  fill(f, 64);
  e.splice(std::move(f));
  ASSERT_EQ(e.size(), per_page + 64);
  for(size_t i = 0; i < per_page; ++i){
    ASSERT_EQ(e[i], A{i});
  }
  for(size_t i = 0; i < 64; ++i){
    ASSERT_EQ(e[per_page + i], A{i});
  }
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Object lifetime, with a type the vector cannot treat as raw bytes
//----------------------------------------------------------------------------------------------------------------------------------------------------

namespace {

/**
 * Separates a copy from a move, which B cannot: B has no move constructor, so
 * it cannot show which push_back overload was picked.
 */
struct Tracked {
  static inline size_t copies = 0;
  static inline size_t moves  = 0;
  static void reset(){ copies = 0; moves = 0; }

  int v;
  explicit Tracked(int i) : v(i) {}
  Tracked(const Tracked& o) : v(o.v) { ++copies; }
  Tracked(Tracked&& o) noexcept : v(o.v) { ++moves; }
};

} // namespace


TEST(vector, push_back_non_trivial)
{
  // push_back has to construct into the slot. assigning instead would run
  // operator= over storage holding no object, and B's assignment writes through
  // a payload pointer that is still whatever the fresh mapping contained
  B::reset();
  {
    msc::vector<B> a {};
    const size_t n = 200;

    for(size_t i = 0; i < n; ++i){
      B src{i};
      a.push_back(src);
    }

    ASSERT_EQ(a.size(), n);
    // one construction for each source and one for each element copied from it
    ASSERT_EQ(B::constructed, n * 2);
    ASSERT_EQ(B::live, n);
    for(size_t i = 0; i < n; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }

    // and it has to keep working once the storage has grown
    grow_once(a);
    for(size_t i = 0; i < n; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, push_back_moves_rvalue)
{
  Tracked::reset();
  msc::vector<Tracked> a {};

  Tracked lvalue{1};
  a.push_back(lvalue);
  ASSERT_EQ(Tracked::copies, 1u);
  ASSERT_EQ(Tracked::moves, 0u);

  a.push_back(Tracked{2});
  ASSERT_EQ(Tracked::copies, 1u);
  ASSERT_EQ(Tracked::moves, 1u);

  a.push_back(std::move(lvalue));
  ASSERT_EQ(Tracked::copies, 1u);
  ASSERT_EQ(Tracked::moves, 2u);

  ASSERT_EQ(a.size(), 3u);
  ASSERT_EQ(a[0].v, 1);
  ASSERT_EQ(a[1].v, 2);
  ASSERT_EQ(a[2].v, 1);
}


TEST(vector, push_back_self_reference)
{
  // growing is an mprotect over a mapping reserved up front, so the storage
  // never moves and a reference into the vector survives it. std::vector cannot
  // promise this: reallocating would destroy the referenced element before the
  // copy runs. the loop crosses at least one growth boundary on purpose
  msc::vector<int> a {};
  a.emplace_back(7);

  const int* base = a.data();
  const size_t start = a.capacity();
  while(a.capacity() == start){
    a.push_back(a[0]);
  }
  // and once more now that the capacity has already changed
  a.push_back(a[0]);
  a.push_back(a.back());

  ASSERT_GT(a.capacity(), start);
  // the mechanism that makes it safe: the mapping stayed where it was
  ASSERT_EQ(a.data(), base);
  ASSERT_GT(a.size(), 1u);
  for(size_t i = 0; i < a.size(); ++i){
    ASSERT_EQ(a[i], 7) << "at " << i;
  }

  // the same through the non trivial type, where a stale reference would mean
  // copying from a destroyed payload rather than reading a stale int
  B::reset();
  {
    msc::vector<B> b {};
    b.emplace_back(42);

    const size_t b_start = b.capacity();
    while(b.capacity() == b_start){
      b.push_back(b[0]);
    }
    b.push_back(b[0]);

    ASSERT_GT(b.capacity(), b_start);
    for(size_t i = 0; i < b.size(); ++i){
      ASSERT_EQ(b[i].value(), 42) << "at " << i;
    }
    ASSERT_EQ(B::live, b.size());
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, push_back_move_only_element)
{
  // push_back(const T&) is a non template member, so it is only instantiated
  // where it is called. a T with no copy constructor is therefore fine as long
  // as callers stay on the rvalue overload, exactly as with std::vector
  static_assert(!std::copy_constructible<std::unique_ptr<int>>);
  static_assert(std::move_constructible<std::unique_ptr<int>>);

  msc::vector<std::unique_ptr<int>> v {};
  v.emplace_back(new int(1));
  v.push_back(std::make_unique<int>(2));

  auto p = std::make_unique<int>(3);
  v.push_back(std::move(p));
  ASSERT_EQ(p, nullptr);

  ASSERT_EQ(v.size(), 3u);
  ASSERT_EQ(*v[0], 1);
  ASSERT_EQ(*v[1], 2);
  ASSERT_EQ(*v[2], 3);

  // and it keeps working across a growth
  for(int i = 0; i < 4096; ++i){
    v.push_back(std::make_unique<int>(i));
  }
  ASSERT_EQ(v.size(), 4099u);
  ASSERT_EQ(*v[0], 1);
  ASSERT_EQ(*v.back(), 4095);
}


TEST(vector, non_trivial_lifetime)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);

    // emplace_back constructs in place, exactly once per element
    ASSERT_EQ(B::constructed, TEST_SIZE);
    ASSERT_EQ(B::destroyed, 0u);
    ASSERT_EQ(B::live, TEST_SIZE);

    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }

    // growing extends the mapping in place, so the elements already there are
    // neither relocated nor destroyed and no copy constructor runs for them
    size_t before = B::constructed;
    grow_once(a);
    ASSERT_EQ(B::destroyed, 0u);
    ASSERT_EQ(B::constructed - before, a.size() - TEST_SIZE);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }
  }
  // leaving the scope runs one destructor per live element, and no more
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_clear)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);

    a.clear();

    // clear() destroys what it drops rather than just forgetting the count
    ASSERT_EQ(B::live, 0u);
    ASSERT_EQ(B::destroyed, TEST_SIZE);
    ASSERT_EQ(a.size(), 0u);
    ASSERT_TRUE(a.empty());

    // and the storage stays usable afterwards
    fill(a, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);
    for(size_t i = 0; i < TEST_SIZE; ++i){
      ASSERT_EQ(a[i].value(), static_cast<int>(i));
    }
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_pop_back)
{
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, TEST_SIZE);

    // pop_back destroys the element it drops, it does not just forget the count
    for(size_t i = 0; i < TEST_SIZE; ++i){
      a.pop_back();
      ASSERT_EQ(B::live, TEST_SIZE - i - 1);
    }
    ASSERT_EQ(a.size(), 0u);
    ASSERT_TRUE(a.empty());
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


TEST(vector, non_trivial_reserve)
{
  B::reset();
  {
    msc::vector<B> a {};
    a.reserve(TEST_SIZE * 4);

    // reserve only maps storage, it must not construct anything into it
    ASSERT_EQ(B::constructed, 0u);
    ASSERT_EQ(a.size(), 0u);
    ASSERT_GE(a.capacity(), TEST_SIZE * 4);

    fill(a, TEST_SIZE);
    ASSERT_EQ(B::constructed, TEST_SIZE);
    ASSERT_EQ(B::live, TEST_SIZE);
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    insert / emplace / erase
//----------------------------------------------------------------------------------------------------------------------------------------------------

namespace {

// expected contents after inserting `inserted` at index `at` into 0..n-1
std::vector<int> spliced_in(size_t n, size_t at, const std::vector<int>& inserted){
  std::vector<int> want;
  for(size_t i = 0; i < at; ++i) want.push_back(static_cast<int>(i));
  want.insert(want.end(), inserted.begin(), inserted.end());
  for(size_t i = at; i < n; ++i) want.push_back(static_cast<int>(i));
  return want;
}

} // namespace


TEST(vector, insert_single)
{
  // front, middle and end, since each exercises a different shift length
  for(size_t at : {size_t{0}, TEST_SIZE / 2, TEST_SIZE}){
    msc::vector<int> a {};
    fill(a, TEST_SIZE);

    auto it = a.insert(a.begin() + at, 99);

    ASSERT_EQ(a.size(), TEST_SIZE + 1)            << "at " << at;
    // the returned iterator points at the element just inserted
    ASSERT_EQ(it, a.begin() + at)                 << "at " << at;
    ASSERT_EQ(*it, 99)                            << "at " << at;
    ASSERT_EQ(std::vector<int>(a.begin(), a.end()),
              spliced_in(TEST_SIZE, at, {99}))    << "at " << at;
  }
}


TEST(vector, insert_count)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  auto it = a.insert(a.begin() + 10, 5, 77);

  ASSERT_EQ(a.size(), TEST_SIZE + 5);
  ASSERT_EQ(it, a.begin() + 10);
  ASSERT_EQ(std::vector<int>(a.begin(), a.end()),
            spliced_in(TEST_SIZE, 10, {77, 77, 77, 77, 77}));

  // a count of zero changes nothing and still returns pos
  auto none = a.insert(a.begin() + 3, 0, 1);
  ASSERT_EQ(a.size(), TEST_SIZE + 5);
  ASSERT_EQ(none, a.begin() + 3);
}


TEST(vector, insert_iterator_pair_and_init_list)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);
  const std::vector<int> src {51, 52, 53};

  auto it = a.insert(a.begin() + 7, src.begin(), src.end());
  ASSERT_EQ(it, a.begin() + 7);
  ASSERT_EQ(std::vector<int>(a.begin(), a.end()), spliced_in(TEST_SIZE, 7, src));

  msc::vector<int> b {};
  fill(b, TEST_SIZE);
  auto it2 = b.insert(b.begin() + 7, {51, 52, 53});
  ASSERT_EQ(it2, b.begin() + 7);
  ASSERT_EQ(std::vector<int>(b.begin(), b.end()), spliced_in(TEST_SIZE, 7, src));
}


TEST(vector, insert_range)
{
  const std::vector<int> src {61, 62, 63, 64};

  msc::vector<int> a {};
  fill(a, TEST_SIZE);
  auto it = a.insert_range(a.begin() + 20, src);
  ASSERT_EQ(it, a.begin() + 20);
  ASSERT_EQ(std::vector<int>(a.begin(), a.end()), spliced_in(TEST_SIZE, 20, src));

  // another msc::vector is a range too
  msc::vector<int> b {};
  msc::vector<int> from {};
  fill(b, TEST_SIZE);
  for(int v : src) from.emplace_back(v);
  auto it2 = b.insert_range(b.begin() + 20, from);
  ASSERT_EQ(it2, b.begin() + 20);
  ASSERT_EQ(std::vector<int>(b.begin(), b.end()), spliced_in(TEST_SIZE, 20, src));
  // the source is only read from
  ASSERT_EQ(from.size(), src.size());
}


TEST(vector, emplace_constructs_in_place)
{
  // a type whose constructor takes more than one argument, which is the whole
  // point of emplace over insert
  struct P {
    int x; double y;
    P(int x, double y) : x(x), y(y) {}
    bool operator==(const P& o) const { return x == o.x && y == o.y; }
  };

  msc::vector<P> a {};
  for(int i = 0; i < 10; ++i) a.emplace_back(i, i * 0.5);

  auto it = a.emplace(a.begin() + 4, 42, 1.5);

  ASSERT_EQ(a.size(), 11u);
  ASSERT_EQ(it, a.begin() + 4);
  ASSERT_EQ(*it, (P{42, 1.5}));
  for(int i = 0; i < 4; ++i)  ASSERT_EQ(a[i], (P{i, i * 0.5}));
  for(int i = 4; i < 10; ++i) ASSERT_EQ(a[i + 1], (P{i, i * 0.5}));
}


TEST(vector, insert_crosses_capacity)
{
  msc::vector<int> a {};
  fill(a, a.capacity());               // exactly full, the next insert must grow
  const size_t before = a.size();
  ASSERT_EQ(a.capacity(), before);

  a.insert(a.begin(), 5, -1);

  ASSERT_EQ(a.size(), before + 5);
  ASSERT_GT(a.capacity(), before);
  for(size_t i = 0; i < 5; ++i)      ASSERT_EQ(a[i], -1);
  for(size_t i = 0; i < before; ++i) ASSERT_EQ(a[5 + i], static_cast<int>(i));
}


TEST(vector, erase_single_and_range)
{
  msc::vector<int> a {};
  fill(a, TEST_SIZE);

  auto it = a.erase(a.begin() + 10);
  ASSERT_EQ(a.size(), TEST_SIZE - 1);
  // returns the element that moved into the hole
  ASSERT_EQ(it, a.begin() + 10);
  ASSERT_EQ(*it, 11);
  for(size_t i = 0; i < 10; ++i)             ASSERT_EQ(a[i], static_cast<int>(i));
  for(size_t i = 10; i < TEST_SIZE - 1; ++i) ASSERT_EQ(a[i], static_cast<int>(i + 1));

  msc::vector<int> b {};
  fill(b, TEST_SIZE);
  auto it2 = b.erase(b.begin() + 5, b.begin() + 15);
  ASSERT_EQ(b.size(), TEST_SIZE - 10);
  ASSERT_EQ(it2, b.begin() + 5);
  for(size_t i = 0; i < 5; ++i)              ASSERT_EQ(b[i], static_cast<int>(i));
  for(size_t i = 5; i < TEST_SIZE - 10; ++i) ASSERT_EQ(b[i], static_cast<int>(i + 10));

  // an empty range is a no op, and erasing everything empties the vector
  auto same = b.erase(b.begin() + 2, b.begin() + 2);
  ASSERT_EQ(b.size(), TEST_SIZE - 10);
  ASSERT_EQ(same, b.begin() + 2);

  auto e = b.erase(b.begin(), b.end());
  ASSERT_EQ(b.size(), 0u);
  ASSERT_TRUE(b.empty());
  ASSERT_EQ(e, b.begin());
  ASSERT_EQ(b.begin(), b.end());

  // erasing the last element is the same as pop_back
  msc::vector<int> c {};
  fill(c, 4);
  c.erase(c.end() - 1);
  ASSERT_EQ(c.size(), 3u);
  ASSERT_EQ(c.back(), 2);
}


TEST(vector, insert_erase_non_trivial)
{
  // every inserted element is constructed exactly once and every erased one
  // destroyed exactly once, which is what a wrong shift length would break
  B::reset();
  {
    msc::vector<B> a {};
    fill(a, 100);
    ASSERT_EQ(B::live, 100u);

    a.emplace(a.begin() + 10, 999);
    ASSERT_EQ(a.size(), 101u);
    ASSERT_EQ(B::live, 101u);
    ASSERT_EQ(a[10].value(), 999);
    ASSERT_EQ(a[11].value(), 10);
    ASSERT_EQ(B::destroyed, 0u);

    a.erase(a.begin() + 10);
    ASSERT_EQ(a.size(), 100u);
    ASSERT_EQ(B::live, 100u);
    ASSERT_EQ(B::destroyed, 1u);
    ASSERT_EQ(a[10].value(), 10);

    a.erase(a.begin() + 5, a.begin() + 15);
    ASSERT_EQ(a.size(), 90u);
    ASSERT_EQ(B::live, 90u);
    ASSERT_EQ(B::destroyed, 11u);
    for(size_t i = 0; i < 5; ++i)  ASSERT_EQ(a[i].value(), static_cast<int>(i));
    for(size_t i = 5; i < 90; ++i) ASSERT_EQ(a[i].value(), static_cast<int>(i + 10));
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}


//----------------------------------------------------------------------------------------------------------------------------------------------------
//    Comparison
//----------------------------------------------------------------------------------------------------------------------------------------------------

namespace {

/**
 * Orderable by < alone, with no operator<=>. This is the case synth-three-way
 * exists for: two calls to < can only justify a weak_ordering, never a strong
 * one, so vector<LessOnly> must compare weakly even though its elements are a
 * plain int underneath.
 */
struct LessOnly {
  int v;
  bool operator==(const LessOnly& o) const { return v == o.v; }
  bool operator<(const LessOnly& o)  const { return v <  o.v; }
};

static_assert(!std::three_way_comparable<LessOnly>);

} // namespace

// A defines operator== and nothing else, so the vector must be equality
// comparable while <=> drops out of overload resolution rather than hard
// erroring inside lexicographical_compare_three_way
static_assert(std::equality_comparable<msc::vector<A>>);
static_assert(!std::three_way_comparable<msc::vector<A>>);
static_assert(std::equality_comparable<msc::vector<int>>);
static_assert(std::three_way_comparable<msc::vector<int>>);

// the category is taken from the element type, not fixed by the container
static_assert(std::same_as<std::compare_three_way_result_t<msc::vector<int>>,
                           std::strong_ordering>);
static_assert(std::same_as<std::compare_three_way_result_t<msc::vector<double>>,
                           std::partial_ordering>);
static_assert(std::same_as<std::compare_three_way_result_t<msc::vector<LessOnly>>,
                           std::weak_ordering>);


TEST(vector, comparison_equality)
{
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, TEST_SIZE);
  fill(b, TEST_SIZE);

  ASSERT_TRUE(a == a);
  ASSERT_TRUE(a == b);
  ASSERT_FALSE(a != b);

  // a copy compares equal to its source
  msc::vector<int> copy = a;
  ASSERT_TRUE(copy == a);

  // same length, one element apart
  b[TEST_SIZE / 2] = -1;
  ASSERT_FALSE(a == b);
  ASSERT_TRUE(a != b);

  // differing length, common prefix
  msc::vector<int> shorter {};
  fill(shorter, TEST_SIZE - 1);
  ASSERT_FALSE(a == shorter);
  ASSERT_TRUE(a != shorter);

  // empty compares equal only to empty
  msc::vector<int> e1 {};
  msc::vector<int> e2 {};
  ASSERT_TRUE(e1 == e2);
  ASSERT_FALSE(e1 == a);
  ASSERT_TRUE(e1 != a);

  // capacity must not take part: same contents, storage of very different size
  msc::vector<int> roomy {};
  roomy.reserve(TEST_SIZE * 16);
  fill(roomy, TEST_SIZE);
  ASSERT_NE(roomy.capacity(), a.capacity());
  ASSERT_TRUE(roomy == a);
}


TEST(vector, comparison_ordering)
{
  msc::vector<int> a {};
  msc::vector<int> b {};
  fill(a, TEST_SIZE);
  fill(b, TEST_SIZE);

  ASSERT_TRUE((a <=> b) == std::strong_ordering::equal);
  ASSERT_FALSE(a < b);
  ASSERT_FALSE(a > b);
  ASSERT_TRUE(a <= b);
  ASSERT_TRUE(a >= b);

  // lexicographic: the first differing element decides, wherever it sits
  b[TEST_SIZE / 2] = static_cast<int>(TEST_SIZE) * 10;
  ASSERT_TRUE(a < b);
  ASSERT_TRUE(b > a);
  ASSERT_TRUE((a <=> b) == std::strong_ordering::less);

  // a difference early outweighs everything after it
  msc::vector<int> c {};
  fill(c, TEST_SIZE);
  c[0] = -1;
  ASSERT_TRUE(c < a);

  // a prefix is less than the sequence that extends it, which a comparison
  // that looked at size first would get backwards
  msc::vector<int> prefix {};
  fill(prefix, TEST_SIZE - 1);
  ASSERT_TRUE(prefix < a);
  ASSERT_TRUE(a > prefix);
  ASSERT_TRUE((prefix <=> a) == std::strong_ordering::less);

  // ...but a shorter sequence still wins on a larger leading element
  msc::vector<int> big_short {};
  big_short.emplace_back(static_cast<int>(TEST_SIZE) * 100);
  ASSERT_TRUE(big_short > a);

  // empty is less than anything non empty and equal to another empty
  msc::vector<int> e {};
  ASSERT_TRUE(e < a);
  ASSERT_TRUE((e <=> msc::vector<int>{}) == std::strong_ordering::equal);
}


TEST(vector, comparison_synth_three_way)
{
  // LessOnly has no <=>, so ordering has to go through the synthesized path
  msc::vector<LessOnly> a {};
  msc::vector<LessOnly> b {};
  for(int i = 0; i < 8; ++i){
    a.emplace_back(i);
    b.emplace_back(i);
  }

  static_assert(std::same_as<decltype(a <=> b), std::weak_ordering>);

  ASSERT_TRUE((a <=> b) == std::weak_ordering::equivalent);
  ASSERT_TRUE(a == b);

  b[4] = LessOnly{100};
  ASSERT_TRUE((a <=> b) == std::weak_ordering::less);
  ASSERT_TRUE(a < b);
  ASSERT_TRUE(b > a);
  ASSERT_FALSE(a == b);

  msc::vector<LessOnly> prefix {};
  for(int i = 0; i < 7; ++i) prefix.emplace_back(i);
  ASSERT_TRUE(prefix < a);
}


TEST(vector, comparison_partial_ordering)
{
  // double is only partially ordered, and the container has to pass that
  // through rather than flattening it to strong_ordering
  msc::vector<double> a {};
  msc::vector<double> b {};
  a.emplace_back(1.0);
  b.emplace_back(std::numeric_limits<double>::quiet_NaN());

  static_assert(std::same_as<decltype(a <=> b), std::partial_ordering>);

  ASSERT_TRUE((a <=> b) == std::partial_ordering::unordered);
  ASSERT_FALSE(a < b);
  ASSERT_FALSE(a > b);
  ASSERT_FALSE(a == b);
}


TEST(vector, comparison_non_trivial)
{
  // comparison reads the elements and must neither construct nor destroy any
  B::reset();
  {
    msc::vector<B> a {};
    msc::vector<B> b {};
    fill(a, 64);
    fill(b, 64);

    const size_t built_before     = B::constructed;
    const size_t destroyed_before = B::destroyed;

    const msc::vector<B>& ca = a;
    const msc::vector<B>& cb = b;
    ASSERT_TRUE(ca == cb);

    ASSERT_EQ(B::constructed, built_before);
    ASSERT_EQ(B::destroyed, destroyed_before);
    ASSERT_EQ(B::live, 128u);

    b[32] = B{999};
    ASSERT_FALSE(ca == cb);

    msc::vector<B> shorter {};
    fill(shorter, 63);
    ASSERT_FALSE(ca == shorter);
  }
  ASSERT_EQ(B::live, 0u);
  ASSERT_EQ(B::destroyed, B::constructed);
}
