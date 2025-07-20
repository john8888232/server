#ifndef SHARDED_MUTEX_H
#define SHARDED_MUTEX_H

#include <shared_mutex>
#include <array>
#include <string>
#include <functional>

//分片锁实现，用于减少高并发场景下的锁争用
//分片锁通过将单一互斥量拆分为多个独立的互斥量，根据键值哈希分配不同的锁，

template<size_t NumLocks = 16>
class ShardedMutex {
private:
    mutable std::array<std::shared_mutex, NumLocks> mutexes_;

public:
    //获取与某个键关联的锁
    std::shared_mutex& getMutexForKey(const std::string& key) const {
        // 简单的哈希函数，将键映射到锁数组中的一个位置
        size_t hash = std::hash<std::string>{}(key) % NumLocks;
        return const_cast<std::shared_mutex&>(mutexes_[hash]);
    }
    
    //根据整数键获取锁
    std::shared_mutex& getMutexForKey(size_t key) const {
        return const_cast<std::shared_mutex&>(mutexes_[key % NumLocks]);
    }
    
    //锁定所有锁（用于需要完全同步的操作）
    void lockAll() {
        for (auto& mutex : mutexes_) {
            mutex.lock();
        }
    }
    
    //共享锁定所有锁（用于需要完全只读同步的操作）
    void lockSharedAll() const {
        for (auto& mutex : const_cast<std::array<std::shared_mutex, NumLocks>&>(mutexes_)) {
            mutex.lock_shared();
        }
    }
    
    //解锁所有锁
    void unlockAll() {
        for (auto& mutex : mutexes_) {
            mutex.unlock();
        }
    }
    
    //解锁所有共享锁
    void unlockSharedAll() const {
        for (auto& mutex : const_cast<std::array<std::shared_mutex, NumLocks>&>(mutexes_)) {
            mutex.unlock_shared();
        }
    }
    
    //获取锁的总数
    static constexpr size_t size() {
        return NumLocks;
    }
};

#endif // SHARDED_MUTEX_H 