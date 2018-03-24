#include "RaiiObjectPool.hpp"
#include "gtest/gtest.h"
#include <queue> 

namespace // anonymous
{

class ObjectWithRefCount
{
public:
    ObjectWithRefCount()            { ++_instances; }
    virtual ~ObjectWithRefCount()   { --_instances; }
    static size_t _instances;
};

size_t ObjectWithRefCount::_instances = 0;

using Pool = RaiiObjectPool<ObjectWithRefCount>;

Pool::ObjectFactory factory =
    [](){ return new ObjectWithRefCount(); };

TEST(RaiiObjectPool, Constructor)
{
    auto pool = Pool::create(factory);
    EXPECT_EQ(pool->limit(),                  Pool::Unlimited);
    EXPECT_EQ(pool->allocated(),              0);
    EXPECT_EQ(pool->pooled(),                 0);
    EXPECT_EQ(ObjectWithRefCount::_instances, 0);
}

TEST(RaiiObjectPool, ConstructorWithOptionalLimit)
{
    auto pool = Pool::create(factory, 4);
    EXPECT_EQ(pool->limit(),                  4);
    EXPECT_EQ(pool->allocated(),              0);
    EXPECT_EQ(pool->pooled(),                 0);
    EXPECT_EQ(ObjectWithRefCount::_instances, 0);
}

TEST(RaiiObjectPool, AcquireAndReleaseCycle)
{
    size_t N = 4;
    auto pool = Pool::create(factory);

    std::queue<Pool::UniquePtrToObject> objects;
    
    for (size_t i = 1; i <= N; ++i)
    {
        auto obj = pool->acquire();
        EXPECT_TRUE(obj);
        objects.push(std::move(obj));
        EXPECT_EQ(pool->allocated(),              i);
        EXPECT_EQ(pool->pooled(),                 0);
        EXPECT_EQ(ObjectWithRefCount::_instances, i);
    }
    
    for (size_t i = 1; i <= N; ++i)
    {
        objects.pop();
        EXPECT_EQ(pool->allocated(),              N);
        EXPECT_EQ(pool->pooled(),                 i);
        EXPECT_EQ(ObjectWithRefCount::_instances, N);
    }

    for (size_t i = 1; i <= N; ++i)
    {
        auto obj = pool->acquire();
        EXPECT_TRUE(obj);
        objects.push(std::move(obj));
        EXPECT_EQ(pool->allocated(),              N);
        EXPECT_EQ(pool->pooled(),                 N-i);
        EXPECT_EQ(ObjectWithRefCount::_instances, N);
    }
    
    for (size_t i = 1; i <= N; ++i)
    {
        objects.pop();
        EXPECT_EQ(pool->allocated(),              N);
        EXPECT_EQ(pool->pooled(),                 i);
        EXPECT_EQ(ObjectWithRefCount::_instances, N);
    }

    pool.reset();
    EXPECT_EQ(ObjectWithRefCount::_instances, 0);
}

TEST(RaiiObjectPool, AcquireDeletePoolRelease)
{
    size_t N = 4;
    auto pool = Pool::create(factory);

    std::queue<Pool::UniquePtrToObject> objects;
    
    for (size_t i = 1; i <= N; ++i)
    {
        auto obj = pool->acquire();
        EXPECT_TRUE(obj);
        objects.push(std::move(obj));
        EXPECT_EQ(pool->allocated(),              i);
        EXPECT_EQ(pool->pooled(),                 0);
        EXPECT_EQ(ObjectWithRefCount::_instances, i);
    }
    
    pool.reset();
    EXPECT_EQ(ObjectWithRefCount::_instances, N);
    
    for (size_t i = 1; i <= N; ++i)
    {
        objects.pop();
        EXPECT_EQ(ObjectWithRefCount::_instances, N-i);
    }
    
    EXPECT_EQ(ObjectWithRefCount::_instances, 0);
}

TEST(RaiiObjectPool, AcquirePastLimit)
{
    size_t limit = 3;
    auto pool = Pool::create(factory, limit);

    // obtain objects up to the limit
    std::queue<Pool::UniquePtrToObject> objects;
    for (size_t i = 0; i < limit; ++i)
    {
        objects.push(pool->acquire());
    }
    
    // now try to grab one more past the limit
    auto obj = pool->acquire();
    EXPECT_FALSE(obj);
    EXPECT_EQ(pool->allocated(),              limit);
    EXPECT_EQ(pool->pooled(),                 0);
    EXPECT_EQ(ObjectWithRefCount::_instances, limit);
}

} // namespace anonymous
