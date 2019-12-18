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

//PIALIC: comment out iverb
//#include "ndspi.h"
//#include <ndtestutil.h>
//#include <rdma/rdma_verbs.h>

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
	_totalObjects ((unsigned int) (bufferSize / sz)),
	_owner (nullptr),
	_prev (nullptr),
	_next (nullptr),
	_reapableObjects (_totalObjects),
	_objectsFree (_totalObjects),
	_start (start),
	_position (start),
    _memory_region(nullptr),
    _destroy_memory_region(nullptr)
    {
      assert ((HL::align<Alignment>((size_t) start) == (size_t) start));
      assert (_objectSize >= Alignment);
      assert ((_totalObjects == 1) || (_objectSize % Alignment == 0));
    }

    virtual ~HoardSuperblockHeaderHelper() {
      clear();
      if (_memory_region && _destroy_memory_region)
      {
          (*_destroy_memory_region)(_memory_region);
          _memory_region = 0;
          _destroy_memory_region = 0;
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
      }
      return ptr;
    }

    inline void free (void * ptr) {
      assert ((size_t) ptr % Alignment == 0);
      assert (isValid());
      _freeList.insert (reinterpret_cast<FreeSLList::Entry *>(ptr));
      _objectsFree++;
      if (_objectsFree == _totalObjects) {
	clear();
      }
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

	inline void getRdmaMr(void* ptr, size_t size, void (*register_callback)(void*, size_t))
    {
      assert (isValid());
      //if (!_isRegisteredMr)
      //{
      //    (*register_callback)((void*)_start, _totalObjects * _objectSize);
      //    _isRegisteredMr = true;
      //}

      //assert(_isRegisteredMr);
    }

    /**
        Create a memory region
        Register the memory if necessary
        Return a pointer to the memory region
    **/
    inline void* get_memory_region(
        void* ptr, // pointer to subblock (not used)
        size_t size, // size of subblock (not used)
        void* (*create_memory_region)(void* block_start, size_t block_size),
        void(*destroy_memory_region)(void* memory_region)
    )
    {
        //fprintf(stderr, "%s(%p, %zu, %p, %p)\n", __FUNCTION__, ptr, size, create_memory_region, destroy_memory_region);
        if (!_memory_region)
        {
            if (_destroy_memory_region)
                fprintf(stderr, "_destroy_memory_region is already set to %p\n", _destroy_memory_region);
            _memory_region = (*create_memory_region)((void*)_start, _totalObjects * _objectSize);
            _destroy_memory_region = destroy_memory_region;
        }
        //fprintf(stderr, "%s -> %p\n", __FUNCTION__, _memory_region);
        return _memory_region;
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
	
	//PIALIC:
	

    //struct ibv_mr *_rdmaMr;
	//void* _rdmaMr;
    void* _memory_region;
    void(*_destroy_memory_region)(void* memory_region);

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
