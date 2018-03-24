#ifndef RAIIOBJECTPOOL_H
#define RAIIOBJECTPOOL_H

#include <memory>
#include <functional>
#include <mutex>
#include <stack>

///@brief A thread-safe RAII-style object pool returning unique pointers
///@warning this class requires the user provide a factory function that
///         constructs objects of type T using the standard new allocator
///@note this class must be constructed as a std::shared_ptr using
///      RaiiObjectPool::create() due to the use of std::enabled_shared_from_this
///@tparam T represents the type of the objects to be pooled
template <typename T>
class RaiiObjectPool : public std::enable_shared_from_this<RaiiObjectPool<T>>
{
public:

    ///@brief helper class to return pointers back to the pool
    ///       or delete the pointer if the pool no longer exists
    class ReturnToPoolDeleter;

    ///@brief convenient name for a std::unique_ptr to a pooled object
    ///       (with custom deleter)
    using UniquePtrToObject = std::unique_ptr<T, ReturnToPoolDeleter>;

    ///@brief convenient name for a factory function to create new objects of type T
    ///@warning the factory function must construct objects of type T 
    ///         using the standard new allocator
    using ObjectFactory = std::function<T* ()>;

    ///@brief convenient name for a std::shared_ptr to this type of pool 
    using SharedPtr = std::shared_ptr<RaiiObjectPool<T>>;

    ///@brief constant to represent an unlimited number of objects
    enum : size_t { Unlimited = 0 };

    ///@brief creates the object pool
    ///@param factory a function that constructs an object of type T
    ///@param limit (optional) a limit to the number of objects to construct
    ///       (default is no limit)
    ///@warning the factory function must construct objects of type T 
    ///         using the standard new allocator
    static SharedPtr create(ObjectFactory factory, size_t limit = Unlimited);
    
    ///@brief destructor
    virtual ~RaiiObjectPool();

    ///@brief obtain a currently unused object of type T from the pool
    ///       or create a new one if there are none available
    ///@return a unique pointer to an object of type T
    ///@return an empty unique pointer if the instantiation limit has been reached
    UniquePtrToObject acquire();

    ///@brief returns the configured limit to the number of objects
    ///       of type T that can be created
    ///@note a value of 0 represents Unlimited 
    size_t limit() const;

    ///@brief returns the total number of objects the pool has created
    ///       via the factory provided in the constructor
    size_t allocated() const;

    ///@brief returns the current number of unused objects that have been
    ///       created but subsequently released and returned to the pool
    size_t pooled() const;

protected:

    ///@brief constructor that requires a factory function and a limit
    ///@note this constructor is protected to ensure users call the static
    ///      RaiiObjectPool::create method that returns a shared pointer
    ///@param factory a function that constructs an object of type T
    ///@param limit a limit to the number of objects to construct
    ///@warning the factory function must construct objects of type T 
    ///         using the standard new allocator
    explicit RaiiObjectPool(ObjectFactory factory, size_t limit);

    ///@brief places the unique pointer to an object of type T onto the pool
    ///@param ptr unique pointer to an object of type T
    void add(UniquePtrToObject ptr);

    ///@brief the limit to the number of objects of type T objects created by the pool
    size_t _limit;
    
    ///@brief the current number of objects of type T created by the pool
    size_t _allocated;

    ///@brief a factory function to create an object of type T
    ObjectFactory _factory;

    ///@brief allows mutually exclusive access to member data
    mutable std::mutex _mutex;

    ///@brief container for the released objects of type T to be reused
    std::stack<UniquePtrToObject> _poolContainer;

public:
    ///@brief helper class to return pointers back to the pool
    ///       or delete the pointer if the pool no longer exists
    class ReturnToPoolDeleter
    {
    public:
        ///@brief constructor with optional weak pointer back to the originating pool
        ///@param ptrToOwnerPool weak pointer to the pool that constructed the object
        explicit ReturnToPoolDeleter(std::weak_ptr<RaiiObjectPool<T>> ptrToOwnerPool = {});
        ///@brief function called to delete the object
        ///@param ptrToReleasedObject raw pointer to constructed object
        void operator()(T* ptrToReleasedObject);
        ///@brief instructs the deleter object to disassociate from the originating pool
        void detachFromPool();
    private:
        ///@brief a weak pointer back to the pool that constructed the object
        std::weak_ptr<RaiiObjectPool<T>> _ptrToOwnerPool;
    };

};

// *******************************************
//      Implementation for RaiiObjectPool
// *******************************************

template <typename T>
typename RaiiObjectPool<T>::SharedPtr RaiiObjectPool<T>::create(ObjectFactory factory, size_t limit)
{
    return SharedPtr(new RaiiObjectPool<T>(factory, limit));
}

template <typename T>
RaiiObjectPool<T>::RaiiObjectPool(ObjectFactory factory, size_t limit)
    : _limit(limit)
    , _allocated(0)
    , _factory(factory)
    , _mutex()
    , _poolContainer()
    
{
}

template <typename T>
RaiiObjectPool<T>::~RaiiObjectPool()
{
    std::unique_lock<std::mutex> lock(_mutex);
    while (!_poolContainer.empty())
    {
        _poolContainer.top().get_deleter().detachFromPool();
        _poolContainer.pop();
    }
}

template <typename T>
typename RaiiObjectPool<T>::UniquePtrToObject RaiiObjectPool<T>::acquire()
{
    std::unique_lock<std::mutex> lock(_mutex);
    if (!_poolContainer.empty())
    {
        // grab a recycled object of type T from the pool
        auto returnPointer = std::move(_poolContainer.top());
        _poolContainer.pop();
        return returnPointer;
    }
    else if (_limit == Unlimited || _allocated < _limit)
    {
        // there are no objects of type T available in the pool
        // and we have not reached the instantiation limit so
        // use the factory function to create and return a new object of type T
        ++_allocated;
        return UniquePtrToObject{_factory(), ReturnToPoolDeleter(this->shared_from_this())};
    }
    else
    {
        // pool is empty and we have reached our instantiation limit so
        // return an empty pointer 
        return UniquePtrToObject{};
    }
}

template <typename T>
void RaiiObjectPool<T>::add(RaiiObjectPool::UniquePtrToObject ptr)
{
    std::unique_lock<std::mutex> lock(_mutex);
    _poolContainer.push(std::move(ptr));
}

template <typename T>
size_t RaiiObjectPool<T>::limit() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _limit;
}

template <typename T>
size_t RaiiObjectPool<T>::allocated() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _allocated;
}

template <typename T>
size_t RaiiObjectPool<T>::pooled() const
{
    std::unique_lock<std::mutex> lock(_mutex);
    return _poolContainer.size();
}

// ************************************************
//      Implementation for ReturnToPoolDeleter
// ************************************************

template <typename T>
RaiiObjectPool<T>::ReturnToPoolDeleter::ReturnToPoolDeleter(std::weak_ptr<RaiiObjectPool<T>> ptrToOwnerPool)
    : _ptrToOwnerPool(ptrToOwnerPool) { }

template <typename T>
void RaiiObjectPool<T>::ReturnToPoolDeleter::operator()(T* ptrToReleasedObject)
{
    if (auto pool = _ptrToOwnerPool.lock())
    {
        // the originating pool still exists so attempt to return the Object back to the pool
        try
        {
            pool->add(UniquePtrToObject{ptrToReleasedObject, ReturnToPoolDeleter(pool)});
            return;
        }
        catch(const std::bad_alloc&) { }
    }
    // the originating pool is gone or cannot be added back so delete the object
    delete ptrToReleasedObject;
}

template <typename T>
void RaiiObjectPool<T>::ReturnToPoolDeleter::detachFromPool()
{
    _ptrToOwnerPool.reset();
}

#endif // RAIIOBJECTPOOL_H
