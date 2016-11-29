#ifndef _SIG_ACTION_H_
#define _SIG_ACTION_H_

#include <signal.h>

class SigAction
{
private:
    static void SwapActions(
        int signum,
        const struct sigaction &newAction,
        const struct sigaction &oldAction,
        bool doFirstCheck = true);

public:
    SigAction();

    SigAction(int signum, const struct sigaction &action);

    SigAction(const SigAction &) = delete;

    SigAction(SigAction &&other) noexcept;

    ~SigAction();

    SigAction &operator=(const SigAction&) = delete;

    SigAction &operator=(SigAction &&other);

    explicit operator bool() const noexcept;

    void Release();

private:
    bool m_haveAction;
    int  m_signum;
    struct sigaction m_action;
    struct sigaction m_oldAction;
};

#endif // _SIG_ACTION_H_
