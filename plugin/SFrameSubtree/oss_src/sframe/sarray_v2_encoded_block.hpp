/**
 * Copyright (C) 2015 Dato, Inc.
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the LICENSE file for details.
 */
#ifndef GRAPHLAB_SFRAME_ENCODED_BLOCK_HPP
#define GRAPHLAB_SFRAME_ENCODED_BLOCK_HPP
#include <boost/coroutine/coroutine.hpp>
#include <boost/circular_buffer.hpp>
#include <vector>
#include <memory>
#include <flexible_type/flexible_type.hpp>
#include <sframe/sarray_v2_block_types.hpp>
namespace graphlab {
namespace v2_block_impl {

class encoded_block_range;
/**
 * This class provides accessors into a typed v2
 * sarray<flexible_type> encoded column block. It maintains the
 * block in a compressed state, and stream decodes it.
 *
 * The encoded block object is copyable, move constructable, 
 * copy assignable, move assignable. Copies are cheap and free 
 * as it only needs to copy a single shared pointer.
 */
class encoded_block {
 public:
  /// Default constructor. Does nothing.
  encoded_block();

  /// Default Copy constructor
  encoded_block(const encoded_block&) = default;
  /// Default Move constructor
  encoded_block(encoded_block&&) = default;
  /// Default Copy assignment
  encoded_block& operator=(const encoded_block&) = default;
  /// Default Move assignment 
  encoded_block& operator=(encoded_block&&) = default;

  /// block constructor from data contents; simply calls init().
  encoded_block(block_info info, std::vector<char>&& data) {
    init(info, std::move(data));
  }


  /// block constructor from data contents; simply calls init().
  encoded_block(block_info info, std::shared_ptr<std::vector<char> > data) {
    init(info, data);
  }

  /** 
   * Initializes this block to point to new data.
   *
   * Existing ranges are NOT invalidated.
   * They will continue to point to what they used to point to.
   * \param info The block information structure
   * \param data The binary data
   */
  void init(block_info info, std::vector<char>&& data);


  /** 
   * Initializes this block to point to new data.
   *
   * Existing ranges are NOT invalidated.
   * They will continue to point to what they used to point to.
   * \param info The block information structure
   * \param data The binary data
   */
  void init(block_info info, std::shared_ptr<std::vector<char> > data);

  /**
   * Returns an accessor to the contents of the block.
   *
   * The range is *not* concurrent. But independent ranges can be accessed
   * in parallel safely.
   */
  encoded_block_range get_range();

  /**
   * Release the block object. All acquired ranges are stil valid.
   */
  void release();

  inline size_t size() const {
    return m_size;
  }

  const block_info get_block_info() const {
    return m_block.m_block_info;
  }

  std::shared_ptr<std::vector<char> > get_block_data() const {
    return m_block.m_data;
  }

  friend class encoded_block_range;

 private:


  struct block {
    /// The block information. Needed for the decode.
    block_info m_block_info;
    /// The actual block data.
    std::shared_ptr<std::vector<char> > m_data;
  };

  block m_block;
  size_t m_size = 0;
}; // class encoded_block


/**
 * The range returned by \ref encoded_block::get_range().
 * It provides begin() and end() functions which allows it to be used in:
 *
 * \code
 * for (const flexible_type& i : block.get_range()) {
 *   ...
 * }
 * \endcode
 *
 * The encoded_block_range provides a one pass reader to the data.
 *
 * The encoded_block_range holds its own pointers to the data and hence
 * is not invalidated by destruction or reassignment of the originating
 * encoded_block object.
 *
 * The range is *not* concurrent.
 */
class encoded_block_range {
 private:
  /**************************************************************************/
  /*                                                                        */
  /*    Buffers and shared datastructures between this and the coroutine    */
  /*                                                                        */
  /**************************************************************************/
  struct coro_shared_data {
    size_t m_skip = 0;
    flexible_type* m_write_target = nullptr;
    size_t m_write_target_numel = 0;
    bool terminate = false;
  };
 public:
  encoded_block_range() = default;
  explicit encoded_block_range(const encoded_block& block);
  encoded_block_range(encoded_block_range&&) = default;

  encoded_block_range& operator=(const encoded_block_range&) = delete;
  encoded_block_range& operator=(encoded_block_range&&) = default;

  /**
   * Releases the range object and all internal handles.
   * All iterators are invalidated.
   */
  void release();

  /**
   * Decodes the next num_elem elements into the decode_target.
   * Returns the number of elements read, i.e.
   * while(num_elem) {
   *   (*decode_target) = next_value;
   *   ++decode_target;
   *   --num_elem;
   * }
   */
  size_t decode_to(flexible_type* decode_target, size_t num_elem);

  /**
   * Skips some number of elements
   */
  void skip(size_t n);

  ~encoded_block_range();

 private:

  /**************************************************************************/
  /*                                                                        */
  /*                       The data I am reading from                       */
  /*                                                                        */
  /**************************************************************************/
  encoded_block::block m_block;

  /**************************************************************************/
  /*                                                                        */
  /*                          The coroutine types                           */
  /*                                                                        */
  /**************************************************************************/
  typedef boost::coroutines::coroutine<void>::pull_type coroutine_type;
  coroutine_type source;

  void coroutine_launch(); 
  void call_source();
  std::shared_ptr<coro_shared_data> m_shared;

  /**************************************************************************/
  /*                                                                        */
  /*                          Iterator management                           */
  /*                                                                        */
  /**************************************************************************/

  /// True if there is no more data
  bool coroutine_started = false;

  /// Fills up to numel elements starting from the pointer at write_target
  size_t fill_buffer(flexible_type* write_target, size_t numel);

}; // encoded_block_range



} // v2_block_impl
} // namespace graphlab
#endif
