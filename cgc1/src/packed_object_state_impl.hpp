#pragma once
#include <atomic>
#include <iostream>
#include "slab_allocator.hpp"
namespace cgc1
{
  namespace details
  {
    CGC1_OPT_INLINE packed_object_state_t *get_state(void *v)
    {
      uintptr_t vi = reinterpret_cast<uintptr_t>(v);
      size_t mask = ::std::numeric_limits<size_t>::max() << (__builtin_ffsll(c_packed_object_block_size) - 1);

      vi &= mask;
      vi += slab_allocator_t::cs_alignment;
      packed_object_state_t *state = reinterpret_cast<packed_object_state_t *>(vi);
      return state;
    }
    CGC1_ALWAYS_INLINE auto packed_object_state_t::declared_entry_size() const noexcept -> size_t
    {
      return m_info.m_data_entry_sz;
    }
    CGC1_ALWAYS_INLINE auto packed_object_state_t::real_entry_size() const noexcept -> size_t
    {
      return cs_object_alignment > declared_entry_size() ? cs_object_alignment : declared_entry_size();
    }
    CGC1_ALWAYS_INLINE auto packed_object_state_t::header_size() const noexcept -> size_t
    {
      return m_info.m_header_size;
    }
    CGC1_OPT_INLINE void packed_object_state_t::initialize() noexcept
    {
      for (size_t i = 0; i < num_blocks(); ++i)
        free_bits()[i].fill(::std::numeric_limits<uint64_t>::max());
    }
    CGC1_OPT_INLINE void packed_object_state_t::clear_mark_bits() noexcept
    {
      for (size_t i = 0; i < num_blocks(); ++i)
        mark_bits()[i].fill(0);
    }

    CGC1_OPT_INLINE auto packed_object_state_t::all_free() const noexcept -> bool
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto &&it = free_bits()[i];
        if (!it.all_set())
          return false;
      }
      return true;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::any_free() const noexcept -> bool
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto &&it = free_bits()[i];
        if (it.any_set())
          return true;
      }
      return false;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::none_free() const noexcept -> bool
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto &&it = free_bits()[i];
        if (it.any_set())
          return false;
      }
      return true;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::first_free() const noexcept -> size_t
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto ret = free_bits()[i].first_set();
        if (ret != ::std::numeric_limits<size_t>::max())
          return i * bits_array_type::size_in_bits() + ret;
      }
      return ::std::numeric_limits<size_t>::max();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::any_marked() const noexcept -> bool
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto &&it = mark_bits()[i];
        if (it.any_set())
          return true;
      }
      return false;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::none_marked() const noexcept -> bool
    {
      for (size_t i = 0; i < num_blocks(); ++i) {
        auto &&it = mark_bits()[i];
        if (it.any_set())
          return false;
      }
      return true;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::free_popcount() const noexcept -> size_t
    {
      return ::std::accumulate(free_bits(), free_bits() + num_blocks(), static_cast<size_t>(0),
                               [](size_t b, auto &&x) { return x.popcount() + b; });
    }
    CGC1_OPT_INLINE void packed_object_state_t::set_free(size_t i, bool val) noexcept
    {
      auto pos = i / bits_array_type::size_in_bits();
      auto sub_pos = i - (pos * bits_array_type::size_in_bits());
      free_bits()[pos].set_bit(sub_pos, val);
    }
    CGC1_OPT_INLINE void packed_object_state_t::set_marked(size_t i) noexcept
    {
      auto pos = i / bits_array_type::size_in_bits();
      auto sub_pos = i - (pos * bits_array_type::size_in_bits());
      mark_bits()[pos].set_bit_atomic(sub_pos, true, ::std::memory_order_relaxed);
    }
    CGC1_OPT_INLINE auto packed_object_state_t::is_free(size_t i) const noexcept -> bool
    {
      auto pos = i / bits_array_type::size_in_bits();
      auto sub_pos = i - (pos * bits_array_type::size_in_bits());
      return free_bits()[pos].get_bit(sub_pos);
    }
    CGC1_OPT_INLINE auto packed_object_state_t::is_marked(size_t i) const noexcept -> bool
    {
      auto pos = i / bits_array_type::size_in_bits();
      auto sub_pos = i - (pos * bits_array_type::size_in_bits());
      return mark_bits()[pos].get_bit(sub_pos);
    }
    CGC1_OPT_INLINE auto packed_object_state_t::size() const noexcept -> size_t
    {
      return m_info.m_size;
    }
    CGC1_OPT_INLINE void packed_object_state_t::_compute_size() noexcept
    {
      auto blocks = m_info.m_num_blocks;
      auto unaligned = sizeof(*this) + sizeof(bits_array_type) * blocks * cs_bits_array_multiple;
      m_info.m_header_size = align(unaligned, cs_header_alignment);

      auto hdr_sz = header_size();
      auto data_entry_sz = declared_entry_size();
      auto data_sz = blocks * data_entry_sz * bits_array_type::size_in_bits() - hdr_sz - 32;
      auto num_data = data_sz / (real_entry_size());
      m_info.m_size = num_data;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::size_bytes() const noexcept -> size_t
    {
      return size() * real_entry_size();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::total_size_bytes() const noexcept -> size_t
    {
      return size_bytes() + header_size();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::begin() noexcept -> uint8_t *
    {
      return reinterpret_cast<uint8_t *>(this) + header_size();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::end() noexcept -> uint8_t *
    {
      return begin() + size_bytes();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::begin() const noexcept -> const uint8_t *
    {
      return reinterpret_cast<const uint8_t *>(this) + header_size();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::end() const noexcept -> const uint8_t *
    {
      return begin() + size_bytes();
    }

    CGC1_OPT_INLINE void *packed_object_state_t::allocate() noexcept
    {
    RESTART:
      auto i = first_free();
      if (i >= size())
        return nullptr;
      if (i == ::std::numeric_limits<size_t>::max())
        return nullptr;
      // guarentee the memory address exists somewhere that is visible to gc
      volatile auto memory_address = begin() + real_entry_size() * i;
      set_free(i, false);
      size_t retries = 0;
      // this awful code is because for a conservative gc
      // we could set free before memory_address is live.
      // this can go wrong because we could mark while it is still free.
      if (unlikely(first_free() == i)) {
        if (retries < 15) {
          goto RESTART;
        } else
          abort();
      }
      assert(memory_address);
      return memory_address;
    }
    CGC1_OPT_INLINE bool packed_object_state_t::deallocate(void *vv) noexcept
    {
      auto v = reinterpret_cast<uint8_t *>(vv);
      if (v < begin() || v > end())
        return false;
      size_t byte_diff = static_cast<size_t>(v - begin());
      if (unlikely(byte_diff % real_entry_size()))
        return false;
      auto i = byte_diff / real_entry_size();
      set_free(i, true);
      return true;
    }

    CGC1_OPT_INLINE void packed_object_state_t::free_unmarked() noexcept
    {
      for (size_t i = 0; i < num_blocks(); ++i)
        free_bits()[i] |= ~mark_bits()[i];
    }
    CGC1_OPT_INLINE auto packed_object_state_t::num_blocks() const noexcept -> size_t
    {
      return m_info.m_num_blocks;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::free_bits() noexcept -> bits_array_type *
    {
      return unsafe_cast<bits_array_type>(this + 1);
    }
    CGC1_OPT_INLINE auto packed_object_state_t::free_bits() const noexcept -> const bits_array_type *
    {
      return unsafe_cast<bits_array_type>(this + 1);
    }

    CGC1_OPT_INLINE auto packed_object_state_t::mark_bits() noexcept -> bits_array_type *
    {
      return free_bits() + num_blocks();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::mark_bits() const noexcept -> const bits_array_type *
    {
      return free_bits() + num_blocks();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::get_index(void *v) const noexcept -> size_t
    {
      auto diff = reinterpret_cast<const uint8_t *>(v) - begin();
      if (diff < 0) {
        assert(0);
        return ::std::numeric_limits<size_t>::max();
      }
      return static_cast<size_t>(diff) / real_entry_size();
    }
    CGC1_OPT_INLINE auto packed_object_state_t::get_object(size_t i) noexcept -> void *
    {
      return reinterpret_cast<void *>(begin() + i * real_entry_size());
    }
    CGC1_OPT_INLINE auto packed_object_state_t::has_valid_magic_numbers() const noexcept -> bool
    {
      return m_info.m_padding[2] == cs_magic_number_0 && m_info.m_padding[3] == cs_magic_number_1;
    }
    CGC1_OPT_INLINE auto packed_object_state_t::addr_in_header(void *v) const noexcept -> bool
    {
      return begin() > v;
    }
  }
}
