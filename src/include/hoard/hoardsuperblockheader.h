// -*- C++ -*-

/*

  The Hoard Multiprocessor Memory Allocator
  www.hoard.org

  Author: Emery Berger, http://www.emeryberger.com

  Copyright (c) 1998-2018 Emery Berger

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef HOARD_HOARDSUPERBLOCKHEADER_H
#define HOARD_HOARDSUPERBLOCKHEADER_H

#include <stdio.h>


#if defined(_WIN32)
#pragma warning( push )
#pragma warning( disable: 4355 ) // this used in base member initializer list
#endif

#include "heaplayers.h"

#include <rdma/rdma_verbs.h>

#include <cstdlib>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#endif

namespace Hoard {

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType,
	    template <class LockType_,
		      int SuperblockSize_,
		      typename HeapType_>
	    class Header_>
  class HoardSuperblock;

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeader;

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeaderHelper {
  public:

    enum { Alignment = 16 };

  public:

    typedef HoardSuperblock<LockType, SuperblockSize, HeapType, HoardSuperblockHeader> BlockType;

    HoardSuperblockHeaderHelper (size_t sz, size_t bufferSize, char * start)
      : _magicNumber (MAGIC_NUMBER ^ (size_t) this),
	_objectSize (sz),
	_objectSizeIsPowerOfTwo (!(sz & (sz - 1)) && sz),
	_totalObjects ((unsigned int) (bufferSize / sz - bufferSize / sz * sz)),
	_owner (nullptr),
	_prev (nullptr),
	_next (nullptr),
	_reapableObjects (_totalObjects),
	_objectsFree (_totalObjects),
	_start (start),
	_position (start),
	_rdmaMr (nullptr),
  _bufferSize (bufferSize)
    {
      assert ((HL::align<Alignment>((size_t) start) == (size_t) start));
      assert (_objectSize >= Alignment);
      assert ((_totalObjects == 1) || (_objectSize % Alignment == 0));
    }

    virtual ~HoardSuperblockHeaderHelper() {
      clear();
      if (_rdmaMr != nullptr)
      {
        ibv_dereg_mr(_rdmaMr);
      }
    }

    inline void * malloc() {
      assert (isValid());
      void * ptr = reapAlloc();
      assert ((ptr == nullptr) || ((size_t) ptr % Alignment == 0));
      if (!ptr) {
        ptr = freeListAlloc();
        assert ((ptr == nullptr) || ((size_t) ptr % Alignment == 0));
      }
      if (ptr != nullptr) {
        assert (getSize(ptr) >= _objectSize);
        assert ((size_t) ptr % Alignment == 0);
        uint8_t * const refCount = getRefCount(ptr);
        *refCount = 1;
      }
      return ptr;
    }

    inline void free (void * ptr) {
      unpin(ptr);
    }

    void clear() {
      assert (isValid());
      // Clear out the freelist.
      _freeList.clear();
      // All the objects are now free.
      _objectsFree = _totalObjects;
      _reapableObjects = _totalObjects;
      _position = (char *) (HL::align<Alignment>((size_t) _start));
    }

    /// @brief Returns the actual start of the object.
    INLINE void * normalize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      void * p;

      // Optimization note: the modulo operation (%) is *really* slow on
      // some architectures (notably x86-64). To reduce its overhead, we
      // optimize for the case when the size request is a power of two,
      // which is often enough to make a difference.

      if (_objectSizeIsPowerOfTwo) {
	p = (void *) ((size_t) ptr - (offset & (_objectSize - 1)));
      } else {
	p = (void *) ((size_t) ptr - (offset % _objectSize));
      }
      return p;
    }


    size_t getSize (void * ptr) const {
      assert (isValid());
      auto offset = (size_t) ptr - (size_t) _start;
      size_t newSize;
      if (_objectSizeIsPowerOfTwo) {
	newSize = _objectSize - (offset & (_objectSize - 1));
      } else {
	newSize = _objectSize - (offset % _objectSize);
      }
      return newSize;
    }

    size_t getObjectSize() const {
      return _objectSize;
    }

    unsigned int getTotalObjects() const {
      return _totalObjects;
    }

    unsigned int getObjectsFree() const {
      return _objectsFree;
    }

    HeapType * getOwner() const {
      return _owner;
    }

    void setOwner (HeapType * o) {
      _owner = o;
    }

    bool isValid() const {
      return (_magicNumber == (MAGIC_NUMBER ^ (size_t) this));
    }

    BlockType * getNext() const {
      return _next;
    }

    BlockType* getPrev() const {
      return _prev;
    }

    void setNext (BlockType* n) {
      _next = n;
    }

    void setPrev (BlockType* p) {
      _prev = p;
    }

    void lock() {
      _theLock.lock();
    }

    void unlock() {
      _theLock.unlock();
    }

    inline ibv_mr * getRdmaMr(struct ibv_pd *pd)
    {
      assert (isValid());
      if (_rdmaMr == nullptr)
      {
        assert(pd != nullptr);
        //fprintf(stderr, "Registering RDMA buffer on demand...\n");
        _rdmaMr = ibv_reg_mr(pd,
            (void *)_start,
            _totalObjects * _objectSize,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
        //fprintf(stderr, "Registered.\n");
      }

      assert(_rdmaMr != nullptr);
      return _rdmaMr;
    }

    inline void pin(void *ptr) {
      uint8_t * const refCount = getRefCount(ptr);
      if (0xff == *pinCount) {
        fprintf(stderr, "REFERENCE COUNT OVERFLOW");
      }

      ++(*pinCount);
    }

    inline void unpin(void *ptr) {
      uint8_t * const refCount = getRefCount(ptr);
      if (0 == *pinCount) {
        fprintf(stderr, "REFERENCE COUNT UNDERFLOW");
      }

      --(*pinCount);
      if (0 == *pinCount) {
        _freeList.insert (reinterpret_cast<FreeSLList::Entry *>(ptr));
        _objectsFree++;
        if (_objectsFree == _totalObjects) {
          clear();
        }
      }
    }

  private:

    MALLOC_FUNCTION INLINE void * reapAlloc() {
      assert (isValid());
      assert (_position);
      // Reap mode.
      if (_reapableObjects > 0) {
	auto * ptr = _position;
	_position = ptr + _objectSize;
	_reapableObjects--;
	_objectsFree--;
	assert ((size_t) ptr % Alignment == 0);
	return ptr;
      } else {
	return nullptr;
      }
    }

    MALLOC_FUNCTION INLINE void * freeListAlloc() {
      assert (isValid());
      // Freelist mode.
      auto * ptr = reinterpret_cast<char *>(_freeList.get());
      if (ptr) {
	assert (_objectsFree >= 1);
	_objectsFree--;
      }
      return ptr;
    }

    inline uint8_t * getRefCount(void *ptr) {
      assert ((size_t) ptr % Alignment == 0);
      assert (isValid());

      size_t idx = (ptr - start) / _objectSize;
      return reinterpret_cast<uint8_t *>(_start) + _bufferSize - 1 - idx;
    }

    enum { MAGIC_NUMBER = 0xcafed00d };

    /// A magic number used to verify validity of this header.
    const size_t _magicNumber;

    /// The object size.
    const size_t _objectSize;

    /// True iff size is a power of two.
    const bool _objectSizeIsPowerOfTwo;

    /// Total objects in the superblock.
    const unsigned int _totalObjects;

    /// The lock.
    LockType _theLock;

    /// The owner of this superblock.
    HeapType * _owner;

    /// The preceding superblock in a linked list.
    BlockType* _prev;

    /// The succeeding superblock in a linked list.
    BlockType* _next;

    /// The number of objects available to be 'reap'ed.
    unsigned int _reapableObjects;

    /// The number of objects available for (re)use.
    unsigned int _objectsFree;

    /// The start of reap allocation.
    const char * _start;

    /// The cursor into the buffer following the header.
    char * _position;

    struct ibv_mr *_rdmaMr;

    uint32_t _bufferSize;

    /// The list of freed objects.
    FreeSLList _freeList;
  };

  // A helper class that pads the header to the desired alignment.

  template <class LockType,
	    int SuperblockSize,
	    typename HeapType>
  class HoardSuperblockHeader :
    public HoardSuperblockHeaderHelper<LockType, SuperblockSize, HeapType> {
  public:


    HoardSuperblockHeader (size_t sz, size_t bufferSize)
      : HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> (sz, bufferSize, (char *) (this + 1))
    {
      static_assert(sizeof(HoardSuperblockHeader) % Parent::Alignment == 0,
		    "Superblock header size must be a multiple of the parent's alignment.");
    }

  private:

    //    typedef Header_<LockType, SuperblockSize, HeapType> Header;
    typedef HoardSuperblockHeaderHelper<LockType,SuperblockSize,HeapType> Parent;
    char _dummy[Parent::Alignment - (sizeof(Parent) % Parent::Alignment)];
  };

}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#pragma warning( pop )
#endif

#endif
