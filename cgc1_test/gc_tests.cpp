#include "../cgc1/src/internal_declarations.hpp"
#include <cgc1/cgc1.hpp>
#include "bandit.hpp"
#include <cgc1/posix_slab.hpp>
#include <cgc1/posix.hpp>
#include <cgc1/aligned_allocator.hpp>
#include "../cgc1/src/slab_allocator.hpp"
#include <thread>
#include <chrono>
#include <string.h>
#include <signal.h>
#include "../cgc1/src/allocator_block.hpp"
#include "../cgc1/src/allocator.hpp"
#include "../cgc1/src/internal_allocator.hpp"
#include "../cgc1/src/global_kernel_state.hpp"
#include "../cgc1/src/internal_stream.hpp"
static ::std::vector<void *> locations;
static ::std::mutex debug_mutex;
using namespace bandit;
namespace cgc1
{
  template <size_t bytes = 5000>
  static _NoInline_ void clean_stack()
  {
    int array[bytes];
    // this nukes all registers and forces spills.
    __asm__ __volatile__(
        "xorl %%eax, %%eax\n"
        "xorl %%ebx, %%ebx\n"
        "xorl %%ecx, %%ecx\n"
        "xorl %%edx, %%edx\n"
        "xorl %%esi, %%esi\n"
        "xorl %%edi, %%edi\n"
        "xorl %%r8d, %%r8d\n"
        "xorl %%r9d, %%r9d\n"
        "xorl %%r10d, %%r10d\n"
        "xorl %%r11d, %%r11d\n"
        "xorl %%r12d, %%r12d\n"
        "xorl %%r13d, %%r13d\n"
        "xorl %%r14d, %%r15d\n"
        "xorl %%r15d, %%r15d\n"
        "pxor %%xmm0, %%xmm0\n"
        "pxor %%xmm1, %%xmm1\n"
        "pxor %%xmm2, %%xmm2\n"
        "pxor %%xmm3, %%xmm3\n"
        "pxor %%xmm4, %%xmm4\n"
        "pxor %%xmm5, %%xmm5\n"
        "pxor %%xmm6, %%xmm6\n"
        "pxor %%xmm7, %%xmm7\n"
        "pxor %%xmm8, %%xmm8\n"
        "pxor %%xmm9, %%xmm9\n"
        "pxor %%xmm10, %%xmm10\n"
        "pxor %%xmm11, %%xmm11\n"
        "pxor %%xmm12, %%xmm12\n"
        "pxor %%xmm13, %%xmm13\n"
        "pxor %%xmm14, %%xmm14\n"
        "pxor %%xmm15, %%xmm15\n"
        :
        :
        : "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi", "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15", "%xmm0",
          "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13",
          "%xmm14", "%xmm15");
    // zero the stack.
    cgc1::secure_zero(array, bytes);
  }
}
/**
 * \brief Setup for root test.
 * This must be a separate funciton to make sure the compiler does not hide pointers somewhere.
 **/
static _NoInline_ void root_test__setup(void *&memory, size_t &old_memory)
{
  memory = cgc1::cgc_malloc(50);
  // hide a pointer away for comparison testing.
  old_memory = cgc1::hide_pointer(memory);
  cgc1::cgc_add_root(&memory);
  AssertThat(cgc1::cgc_size(memory), Equals(static_cast<size_t>(64)));
  AssertThat(cgc1::cgc_is_cgc(memory), IsTrue());
  AssertThat(cgc1::cgc_is_cgc(nullptr), IsFalse());
}
/**
 * \brief Test root functionality.
 **/
static void root_test()
{
  void *memory;
  size_t old_memory;
  // setup a root.
  root_test__setup(memory, old_memory);
  // force collection
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  // verify that nothing was collected.
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect, HasLength(0));
  // remove the root.
  cgc1::cgc_remove_root(&memory);
  // make sure that the we zero the memory so the pointer doesn't linger.
  cgc1::secure_zero_pointer(memory);
  auto num_collections = cgc1::debug::num_gc_collections();
  // force collection.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  // now we should collect.
  AssertThat(last_collect.size(), Equals(static_cast<size_t>(1)));
  // verify it collected the correct address.
  AssertThat(last_collect[0] == cgc1::unhide_pointer(old_memory), IsTrue());
  // verify that we did perform a collection.
  AssertThat(cgc1::debug::num_gc_collections(), Equals(num_collections + 1));
}
/**
 * \brief Setup for internal pointer test.
 **/
static _NoInline_ void internal_pointer_test__setup(void *&memory, size_t &old_memory)
{
  memory = cgc1::cgc_malloc(50);
  uint8_t *&umemory = cgc1::unsafe_reference_cast<uint8_t *>(memory);
  old_memory = cgc1::hide_pointer(memory);
  umemory += 1;
  cgc1::cgc_add_root(&memory);
}
/**
 * \brief This tests pointers pointing to the inside of an object as opposed to the start.
 **/
static void internal_pointer_test()
{
  void *memory;
  size_t old_memory;
  // setup a buffer to point into.
  internal_pointer_test__setup(memory, old_memory);
  // force collection.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  // it should stick around because it has a root.
  AssertThat(last_collect, HasLength(0));
  // remove the root.
  cgc1::cgc_remove_root(&memory);
  // make sure that we zero the memory so the pointer doesn't linger.
  cgc1::secure_zero_pointer(memory);
  // force collection.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  // now we should collect.
  AssertThat(last_collect, HasLength(1));
  // verify it collected the correct address.
  AssertThat(last_collect[0] == cgc1::unhide_pointer(old_memory), IsTrue());
}
/**
 * \brief Setup for atomic object test.
 **/
static _NoInline_ void atomic_test__setup(void *&memory, size_t &old_memory)
{
  memory = cgc1::cgc_malloc(50);
  cgc1::cgc_add_root(&memory);
  void *memory2 = cgc1::cgc_malloc(50);
  *reinterpret_cast<void **>(memory) = memory2;
  old_memory = cgc1::hide_pointer(memory2);
  cgc1::secure_zero_pointer(memory2);
}
/**
 * \brief Test atomic object functionality.
 **/
static void atomic_test()
{
  void *memory;
  size_t old_memory;
  atomic_test__setup(memory, old_memory);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect.size(), Equals(static_cast<size_t>(0)));
  cgc1::cgc_set_atomic(memory, true);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect, HasLength(1));
  AssertThat(last_collect[0] == cgc1::unhide_pointer(old_memory), IsTrue());
  cgc1::cgc_remove_root(&memory);
  cgc1::secure_zero_pointer(memory);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  AssertThat(last_collect, HasLength(1));
  // test bad parameters
  cgc1::cgc_set_atomic(nullptr, true);
  cgc1::cgc_set_atomic(&old_memory, true);
}
static _NoInline_ void finalizer_test__setup(std::atomic<bool> &finalized, size_t &old_memory)
{
  void *memory = cgc1::cgc_malloc(50);
  old_memory = cgc1::hide_pointer(memory);
  finalized = false;
  cgc1::cgc_register_finalizer(memory, [&finalized](void *) { finalized = true; });
  cgc1::secure_zero_pointer(memory);
}
static void finalizer_test()
{
  size_t old_memory;
  std::atomic<bool> finalized;
  finalizer_test__setup(finalized, old_memory);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect.size(), Equals(static_cast<size_t>(1)));
  AssertThat(last_collect[0] == cgc1::unhide_pointer(old_memory), IsTrue());
  AssertThat(static_cast<bool>(finalized), IsTrue());
  // test bad parameters
  cgc1::cgc_register_finalizer(nullptr, [&finalized](void *) { finalized = true; });
  AssertThat(cgc1::cgc_start(nullptr) == nullptr, IsTrue());
  AssertThat(cgc1::cgc_start(&old_memory) == nullptr, IsTrue());
  cgc1::cgc_register_finalizer(&old_memory, [&finalized](void *) { finalized = true; });
}
static _NoInline_ void uncollectable_test__setup(size_t &old_memory)
{
  void *memory = cgc1::cgc_malloc(50);
  old_memory = cgc1::hide_pointer(memory);
  cgc1::cgc_set_uncollectable(memory, true);
  cgc1::secure_zero_pointer(memory);
}
static _NoInline_ void uncollectable_test__cleanup(size_t &old_memory)
{
  cgc1::cgc_set_uncollectable(cgc1::unhide_pointer(old_memory), false);
}
static void uncollectable_test()
{
  size_t old_memory;
  uncollectable_test__setup(old_memory);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect.size(), Equals(static_cast<size_t>(0)));
  uncollectable_test__cleanup(old_memory);
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(last_collect.size(), Equals(static_cast<size_t>(1)));
  AssertThat(last_collect[0] == cgc1::unhide_pointer(old_memory), IsTrue());
  // test bad parameters
  cgc1::cgc_set_uncollectable(nullptr, true);
  cgc1::cgc_set_uncollectable(&old_memory, true);
}
static void linked_list_test()
{
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  std::atomic<bool> keep_going{true};
  auto test_thread = [&keep_going]() {
    CGC1_INITIALIZE_THREAD();
    void **foo = reinterpret_cast<void **>(cgc1::cgc_malloc(100));
    {
      void **bar = foo;
      for (int i = 0; i < 3000; ++i) {
        {
          CGC1_CONCURRENCY_LOCK_GUARD(debug_mutex);
          locations.push_back(bar);
        }
        cgc1::secure_zero(bar, 100);
        *bar = cgc1::cgc_malloc(100);
        bar = reinterpret_cast<void **>(*bar);
      }
      {
        CGC1_CONCURRENCY_LOCK_GUARD(debug_mutex);
        locations.push_back(bar);
      }
    }
    while (keep_going) {
      ::std::stringstream ss;
      ss << foo << ::std::endl;
    }
    cgc1::cgc_unregister_thread();
  };
  ::std::thread t1(test_thread);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  for (int i = 0; i < 100; ++i) {
    cgc1::cgc_force_collect();
    cgc1::details::g_gks.wait_for_finalization();
    auto freed_last = cgc1::details::g_gks._d_freed_in_last_collection();
    assert(freed_last.empty());
    AssertThat(freed_last, HasLength(0));
  }
  keep_going = false;
  t1.join();
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  ::std::sort(locations.begin(), locations.end());
  ::std::sort(last_collect.begin(), last_collect.end());
  for (void *v : locations) {
    assert(::std::find(last_collect.begin(), last_collect.end(), v) != last_collect.end());
    AssertThat(::std::find(last_collect.begin(), last_collect.end(), v) != last_collect.end(), IsTrue());
  }
  locations.clear();
}
/**
 * \brief Try to create a race condition in the garbage collector.
 **/
static void race_condition_test()
{
  ::std::atomic<bool> keep_going{true};
  ::std::atomic<bool> finished_part1{true};
  // lambda for thread test.
  auto test_thread = [&keep_going, &finished_part1]() {
    cgc1::clean_stack();
    CGC1_INITIALIZE_THREAD();
    char *foo = reinterpret_cast<char *>(cgc1::cgc_malloc(100));
    cgc1::secure_zero(foo, 100);
    {
      CGC1_CONCURRENCY_LOCK_GUARD(debug_mutex);
      locations.push_back(foo);
    }
    finished_part1 = true;
    // the only point of this is to prevent highly aggressive compiler optimzations.
    while (keep_going) {
      ::std::stringstream ss;
      ss << foo << ::std::endl;
    };

    foo = nullptr;
    {
      CGC1_CONCURRENCY_LOCK_GUARD(debug_mutex);
      for (int i = 0; i < 5000; ++i) {
        foo = reinterpret_cast<char *>(cgc1::cgc_malloc(100));
        cgc1::secure_zero(foo, 100);
        locations.push_back(foo);
      }
    }
    cgc1::cgc_unregister_thread();
    cgc1::clean_stack();
  };
  ::std::thread t1(test_thread);
  ::std::thread t2(test_thread);
  // try to force a race condition by interrupting threads.
  // this is obviously stochastic.
  while (!finished_part1) {
    cgc1::cgc_force_collect();
    cgc1::details::g_gks.wait_for_finalization();
    auto freed_last = cgc1::details::g_gks._d_freed_in_last_collection();
    assert(freed_last.empty());
    AssertThat(freed_last, HasLength(0));
    // prevent test from hammering gc before threads are setup.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // wait for threads to finish.
  keep_going = false;
  t1.join();
  t2.join();
  // force collection.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  auto last_collect = cgc1::details::g_gks._d_freed_in_last_collection();
  // pointers might be in arbitrary order so sort them.
  ::std::sort(locations.begin(), locations.end());
  ::std::sort(last_collect.begin(), last_collect.end());
  // make sure all pointers have been collected.
  for (void *v : locations) {
    bool found = ::std::find(last_collect.begin(), last_collect.end(), v) != last_collect.end();
    assert(found);
    AssertThat(found, IsTrue());
  }
  // cleanup
  locations.clear();
}
static size_t expected_global_blocks(const size_t start, size_t taken, const size_t put_back)
{
  taken = ::std::min(start, taken);
  return start - taken + put_back;
}
static void return_to_global_test0()
{
  // get the global allocator
  auto &allocator = cgc1::details::g_gks.gc_allocator();
  // get the number of global blocks starting.
  const auto start_num_global_blocks = allocator.num_global_blocks();
  // this thread will create an object (which will create a thread local block)
  // then it will exit, returning the block to global.
  auto test_thread = []() {
    cgc1::clean_stack();
    CGC1_INITIALIZE_THREAD();
    cgc1::cgc_malloc(100);
    cgc1::cgc_unregister_thread();
    cgc1::clean_stack();
  };
  ::std::thread t1(test_thread);
  t1.join();
  // wait for thread to terminate.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  // make sure exactly one memory location was freed.
  auto freed_last = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(freed_last, HasLength(1));
  // make sure that exactly one global block was added.
  auto num_global_blocks = allocator.num_global_blocks();
  AssertThat(num_global_blocks, Equals(expected_global_blocks(start_num_global_blocks, 1, 1)));
}
static void return_to_global_test1()
{
  // get the global allocator
  auto &allocator = cgc1::details::g_gks.gc_allocator();
  // put block bounds here.
  uint8_t *begin = nullptr;
  uint8_t *end = nullptr;
  // thread local lambda.
  auto test_thread = [&allocator, &begin, &end]() {
    cgc1::clean_stack();
    CGC1_INITIALIZE_THREAD();
    // get thread local state.
    auto &tls = allocator.initialize_thread();
    const size_t size_to_alloc = 100;
    // get block set id.
    auto id = tls.find_block_set_id(size_to_alloc);
    auto &abs = tls.allocators()[id];
    // allocate some memory and immediately free it.
    cgc1::cgc_free(cgc1::cgc_malloc(size_to_alloc));
    // get the stats on the last block so that we can test to see if it is freed to global
    auto &lb = abs.last_block();
    begin = lb.begin();
    end = lb.end();
    cgc1::cgc_unregister_thread();
    cgc1::clean_stack();
  };
  ::std::thread t1(test_thread);
  t1.join();
  // wait for thread to terminate.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  // make sure nothing was freed (as it should have already been freed)
  auto freed_last = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(freed_last, HasLength(0));
  // check that the memory was returned to global free list.
  bool in_free = allocator.in_free_list(::std::make_pair(begin, end));
  AssertThat(in_free, IsTrue());
}
/*static void return_to_global_test2()
{
  // get the global allocator
  auto &allocator = cgc1::details::g_gks.gc_allocator();
  // get the number of global blocks starting.
  const auto start_num_global_blocks = allocator.num_global_blocks();
  ::std::atomic<bool> ready_for_test{false};
  ::std::atomic<bool> test_done{false};
  auto test_thread = [&allocator, &ready_for_test, &test_done]() {
    cgc1::clean_stack();
    CGC1_INITIALIZE_THREAD();
    // get thread local state.
    auto &tls = allocator.initialize_thread();
    // get block set id.
    auto id = tls.find_block_set_id(100);
    // get multiple for blocks.
    auto multiple = tls.get_allocator_multiple(id);
    auto &abs = tls.allocators()[id];
    tls.set_destroy_threshold(0);
    tls.set_minimum_local_blocks(1);
    auto min_sz = abs.allocator_min_size();
    auto max_sz = abs.allocator_max_size();
    ::std::cout << id << " " << multiple << " " << tls.get_allocator_block_size(id) << " " << min_sz << " " << max_sz << "\n";
    ::std::cout << abs.size() << ::std::endl;
    ::std::vector<void *> ptrs;
    ::std::cout << "start\n";
    while (abs.size() != 2) {
      ptrs.push_back(cgc1::cgc_malloc(max_sz));
    }
    ::std::cout << "end\n";
    ::std::cout << abs.size() << ::std::endl;
    //        cgc1::cgc_free(ptrs.back());
    tls.destroy(ptrs.back());
    ptrs.pop_back();
    ::std::cout << abs.size() << ::std::endl;
    ::std::cout << "HERE4" << ::std::endl;
    ready_for_test = true;
    while (!test_done)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    //      ::std::cout << "HERE2" << ::std::endl;
    cgc1::cgc_unregister_thread();
    cgc1::clean_stack();
    ::std::cout << "HERE" << ::std::endl;
  };
  ::std::thread t1(test_thread);
  while (!ready_for_test)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  ::std::cout << "HERE3" << ::std::endl;
  ::std::cout << "GLOBAL BLOCKS " << allocator.num_global_blocks() << " " << expected_global_blocks(start_num_global_blocks, 2, 1)
              << ::std::endl;
  assert(allocator.num_global_blocks() == expected_global_blocks(start_num_global_blocks, 2, 1));
  AssertThat(allocator.num_global_blocks(), Equals(expected_global_blocks(start_num_global_blocks, 2, 1)));
  ::std::cout << "HERE5" << ::std::endl;
  test_done = true;
  t1.join();
  // wait for thread to terminate.
  cgc1::cgc_force_collect();
  cgc1::details::g_gks.wait_for_finalization();
  // make sure exactly one memory location was freed.
  auto freed_last = cgc1::details::g_gks._d_freed_in_last_collection();
  AssertThat(freed_last, HasLength(1));
  // make sure we now have one more global block.
  auto num_global_blocks = allocator.num_global_blocks();
  AssertThat(num_global_blocks, Equals(expected_global_blocks(start_num_global_blocks, 2, 2)));
  ::std::cout << "HERE7" << ::std::endl;
}
*/
/**
 * \brief Test various APIs.
**/
static void api_tests()
{
  // test heap size api call.
  AssertThat(cgc1::cgc_heap_size(), Is().GreaterThan(static_cast<size_t>(0)));
  // test heap free api call.
  AssertThat(cgc1::cgc_heap_free(), Is().GreaterThan(static_cast<size_t>(0)));
  // try disabling gc.
  cgc1::cgc_disable();
  AssertThat(cgc1::cgc_is_enabled(), Is().False());
  // try enabling gc.
  cgc1::cgc_enable();
  AssertThat(cgc1::cgc_is_enabled(), Is().True());
}
void gc_bandit_tests()
{
  describe("GC", []() {
    it("return_to_global_test0", []() {
      cgc1::clean_stack();
      return_to_global_test0();
      cgc1::clean_stack();
    });

    it("return_to_global_test1", []() {
      cgc1::clean_stack();
      return_to_global_test1();
      cgc1::clean_stack();
    });
    /*        it("return_to_global_test2", []() {
      cgc1::clean_stack();
      return_to_global_test2();
      cgc1::clean_stack();
      });*/
    for (size_t i = 0; i < 10; ++i) {
      it("race condition", []() {
        cgc1::clean_stack();
        race_condition_test();
        cgc1::clean_stack();
      });
    }
    it("linked list test", []() {
      cgc1::clean_stack();
      linked_list_test();
      cgc1::clean_stack();
    });
    it("root", []() {
      cgc1::clean_stack();
      root_test();
      cgc1::clean_stack();
    });
    it("internal pointer", []() {
      cgc1::clean_stack();
      internal_pointer_test();
      cgc1::clean_stack();
    });
    it("finalizers", []() {
      cgc1::clean_stack();
      finalizer_test();
      cgc1::clean_stack();
    });
    it("atomic", []() {
      cgc1::clean_stack();
      atomic_test();
      cgc1::clean_stack();
    });
    it("uncollectable", []() {
      cgc1::clean_stack();
      uncollectable_test();
      cgc1::clean_stack();
    });
    it("api_tests", []() {
      cgc1::clean_stack();
      api_tests();
      cgc1::clean_stack();
    });
  });
}
