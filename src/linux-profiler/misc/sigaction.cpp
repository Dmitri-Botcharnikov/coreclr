#include <utility>
#include <system_error>
#include <exception>
#include <stdexcept>

#include <string.h>
#include <assert.h>
#include <errno.h>

#include "sigaction.h"

// static
void SigAction::SwapActions(
    int signum,
    const struct sigaction &newAction,
    const struct sigaction &oldAction,
    bool doFirstCheck)
{
    struct sigaction curAction;

    if (doFirstCheck)
    {
        if (sigaction(signum, nullptr, &curAction))
        {
            throw std::system_error(errno, std::system_category(),
                "SigAction::SwapActions(): sigaction()");
        }

        if (curAction.sa_handler   != oldAction.sa_handler ||
            curAction.sa_sigaction != oldAction.sa_sigaction)
        {
            throw std::runtime_error(
                "SigAction::SwapActions(): Signal handler was changed");
        }
    }

    if (sigaction(signum, &newAction, &curAction)) {
        throw std::system_error(errno, std::system_category(),
            "SigAction::SwapActions(): sigaction()");
    }
    // NOTE: double-check of old action to avoid some race conditions.
    if (curAction.sa_handler   != oldAction.sa_handler ||
        curAction.sa_sigaction != oldAction.sa_sigaction)
    {
        sigaction(signum, &curAction, nullptr);
        throw std::runtime_error(
            "SigAction::SwapActions(): Signal handler was changed");
    }
}

SigAction::SigAction()
    : m_haveAction(false)
{}

SigAction::SigAction(int signum, const struct sigaction &action)
    : m_haveAction(true)
    , m_signum(signum)
    , m_action(action)
{
    if (sigaction(signum, nullptr, &m_oldAction))
    {
        throw std::system_error(errno, std::system_category(),
            "SigAction::SigAction(): sigaction()");
    }

    if (m_oldAction.sa_handler != SIG_DFL &&
        m_oldAction.sa_handler != SIG_IGN)
    {
        throw std::runtime_error(
            "SigAction::SigAction(): Signal handler was changed");
    }

    SigAction::SwapActions(m_signum, m_action, m_oldAction, false);
}

SigAction::SigAction(SigAction &&other) noexcept
    : m_haveAction(false)
{
    *this = std::move(other);
}

SigAction::~SigAction()
{
    try
    {
        this->Release();
    }
    catch (...) {}
}

SigAction &SigAction::operator=(SigAction &&other)
{
    assert(this != &other);
    this->Release();

    m_haveAction = other.m_haveAction;
    m_signum     = other.m_signum;
    m_action     = other.m_action;
    m_oldAction  = other.m_oldAction;

    other.m_haveAction = false;
    return *this;
}

SigAction::operator bool() const noexcept
{
    return m_haveAction;
}

void SigAction::Release()
{
    if (!m_haveAction)
    {
        return;
    }

    m_haveAction = false;
    SigAction::SwapActions(m_signum, m_oldAction, m_action);
}
