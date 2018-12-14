#ifndef slic3r_Channel_hpp_
#define slic3r_Channel_hpp_

#include <deque>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <boost/optional.hpp>


namespace Slic3r {


template<class T> class Channel
{
private:
    using UniqueLock = std::unique_lock<std::mutex>;
    using Queue = std::deque<T>;
public:
    class Guard
    {
    public:
        Guard(UniqueLock lock, const Queue &queue) : m_lock(std::move(lock)), m_queue(queue) {}
        Guard(const Guard &other) = delete;
        Guard(Guard &&other) = delete;
        ~Guard() {}

        // Access trampolines
        size_t size() const noexcept { return m_queue.size(); }
        bool empty() const noexcept { return m_queue.empty(); }
        typename Queue::const_iterator begin() const noexcept { return m_queue.begin(); }
        typename Queue::const_iterator end() const noexcept { return m_queue.end(); }
        typename Queue::const_reference operator[](size_t i) const { return m_queue[i]; }

        Guard& operator=(const Guard &other) = delete;
        Guard& operator=(Guard &&other) = delete;
    private:
        UniqueLock m_lock;
        const Queue &m_queue;
    };


    Channel() {}
    ~Channel() {}

    void push(const T& item, bool silent = false)
    {
        {
            UniqueLock lock(m_mutex);
            m_queue.push_back(item);
        }
        if (! silent) { m_condition.notify_one(); }
    }

    void push(T &&item, bool silent = false)
    {
        {
            UniqueLock lock(m_mutex);
            m_queue.push_back(std::forward(item));
        }
        if (! silent) { m_condition.notify_one(); }
    }

    T pop()
    {
        UniqueLock lock(m_mutex);
        m_condition.wait(lock, [this]() { return !m_queue.empty(); });
        auto item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    boost::optional<T> try_pop()
    {
        UniqueLock lock(m_mutex);
        if (m_queue.empty()) {
            return boost::none;
        } else {
            auto item = std::move(m_queue.front());
            m_queue.pop();
            return item;
        }
    }

    // Unlocked observers
    // Thread unsafe! Keep in mind you need to re-verify the result after acquiring lock!
    size_t size() const noexcept { return m_queue.size(); }
    bool empty() const noexcept { return m_queue.empty(); }

    Guard read() const
    {
        return Guard(UniqueLock(m_mutex), m_queue);
    }

private:
    Queue m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condition;
};


} // namespace Slic3r

#endif // slic3r_Channel_hpp_
