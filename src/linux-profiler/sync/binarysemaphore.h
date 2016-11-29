#ifndef _BINARY_SEMAPHORE_H_
#define _BINARY_SEMAPHORE_H_

#include <mutex>
#include <condition_variable>

template <typename Mutex, typename CondVar>
class basic_binary_semaphore {
public:
    basic_binary_semaphore();

    explicit basic_binary_semaphore(bool init);

    void notify();

    void wait();

    bool try_wait();

private:
    Mutex   m_mutex;
    CondVar m_cv;
    bool    m_val;
};

using binary_semaphore =
    basic_binary_semaphore<std::mutex, std::condition_variable>;

template <typename Mutex, typename CondVar>
basic_binary_semaphore<Mutex, CondVar>::basic_binary_semaphore()
    : m_val(false)
{}

template <typename Mutex, typename CondVar>
basic_binary_semaphore<Mutex, CondVar>::basic_binary_semaphore(bool init)
    : m_val(init)
{}

template <typename Mutex, typename CondVar>
void basic_binary_semaphore<Mutex, CondVar>::notify()
{
    std::lock_guard<Mutex> lock(m_mutex);
    m_val = true;;
    m_cv.notify_one();
}

template <typename Mutex, typename CondVar>
void basic_binary_semaphore<Mutex, CondVar>::wait()
{
    std::unique_lock<Mutex> lock(m_mutex);
    while (!m_val)
    {
        m_cv.wait(lock);
    }
    m_val = false;
}

template <typename Mutex, typename CondVar>
bool basic_binary_semaphore<Mutex, CondVar>::try_wait()
{
    std::lock_guard<Mutex> lock(m_mutex);
    if (m_val)
    {
        m_val = false;
        return true;
    }
    return false;
}

#endif // _BINARY_SEMAPHORE_H_
