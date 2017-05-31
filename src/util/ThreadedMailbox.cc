//
//  ThreadedMailbox.cc
//  blip_cpp
//
//  Created by Jens Alfke on 4/13/17.
//  Copyright © 2017 Couchbase. All rights reserved.
//

#include "ThreadedMailbox.hh"
#include "Actor.hh"
#include "Timer.hh"
#include "Logging.hh"
#include "Channel.cc"       // Brings in the definitions of the template methods

using namespace std;

namespace litecore { namespace actor {


    LogDomain ActorLog("Actor");


#pragma mark - SCHEDULER:

    
    static Scheduler* sScheduler;


    Scheduler* Scheduler::sharedScheduler() {
        if (!sScheduler) {
            sScheduler = new Scheduler;
            sScheduler->start();
        }
        return sScheduler;
    }


    void Scheduler::start() {
        if (!_started.test_and_set()) {
            if (_numThreads == 0) {
                _numThreads = thread::hardware_concurrency();
                if (_numThreads == 0)
                    _numThreads = 2;
            }
            LogTo(ActorLog, "Starting Scheduler<%p> with %u threads", this, _numThreads);
            for (unsigned id = 1; id <= _numThreads; id++)
                _threadPool.emplace_back([this,id]{task(id);});
        }
    }
    

    void Scheduler::stop() {
        LogTo(ActorLog, "Stopping Scheduler<%p>...", this);
        _queue.close();
        for (auto &t : _threadPool) {
            t.join();
        }
        LogTo(ActorLog, "Scheduler<%p> has stopped", this);
        _started.clear();
    }


    void Scheduler::task(unsigned taskID) {
        LogToAt(ActorLog, Verbose, "   task %d starting", taskID);
#ifndef _MSC_VER
        {
            char name[100];
            sprintf(name, "LiteCore Scheduler #%u", taskID);
#ifdef __APPLE__
            pthread_setname_np(name);
#else
            pthread_setname_np(pthread_self(), name);
#endif
        }
#endif
        ThreadedMailbox *mailbox;
        while ((mailbox = _queue.pop()) != nullptr) {
            LogToAt(ActorLog, Verbose, "   task %d calling Actor<%p>", taskID, mailbox);
            mailbox->performNextMessage();
            mailbox = nullptr;
        }
        LogTo(ActorLog, "   task %d finished", taskID);
    }


    void Scheduler::schedule(ThreadedMailbox *mbox) {
        sScheduler->_queue.push(mbox);
    }


    // Explicitly instantiate the Channel specializations we need; this corresponds to the
    // "extern template..." declarations at the bottom of Actor.hh
    template class Channel<ThreadedMailbox*>;
    template class Channel<std::function<void()>>;


#pragma mark - MAILBOX PROXY


    /*  The only purpose of this class is to handle the situation where an enqueueAfter triggers
        after its target Actor has been deleted. It has a weak reference to a mailbox (which is
        cleared by the mailbox's destructor.) The proxy is retained by the Timer's lambda, so
        it can safely be called when the timer fires; it will tell its mailbox to enqueue the
        function, unless the mailbox has been deleted. */
    class MailboxProxy : public RefCounted {
    public:
        MailboxProxy(ThreadedMailbox *m)
        :_mailbox(m)
        { }

        void detach() {
            _mailbox = nullptr;
        }

        void enqueue(function<void()> f) {
            ThreadedMailbox* mb = _mailbox;
            if (mb)
                mb->enqueue(f);
        }

    private:
        virtual ~MailboxProxy() =default;
        atomic<ThreadedMailbox*> _mailbox;
    };


#pragma mark - MAILBOX:


    ThreadedMailbox::ThreadedMailbox(Actor *a, const std::string &name)
    :_actor(a)
    ,_name(name)
    {
        Scheduler::sharedScheduler()->start();
    }


    ThreadedMailbox::~ThreadedMailbox() {
        if (_proxy)
            _proxy->detach();
    }


    void ThreadedMailbox::enqueue(std::function<void()> f) {
        retain(_actor);
        if (push(f))
            reschedule();
    }


    void ThreadedMailbox::enqueueAfter(delay_t delay, std::function<void()> f) {
        if (delay <= delay_t::zero())
            return enqueue(f);

        Retained<MailboxProxy> proxy;
        {
            std::lock_guard<mutex> lock(_mutex);
            proxy = _proxy;
            if (!proxy)
                proxy = _proxy = new MailboxProxy(this);
        }

        auto timer = new Timer([proxy, f]{ proxy->enqueue(f); });
        timer->autoDelete();
        timer->fireAfter(chrono::duration_cast<Timer::duration>(delay));
    }


    void ThreadedMailbox::reschedule() {
        Scheduler::schedule(this);
    }


    void ThreadedMailbox::performNextMessage() {
        LogTo(ActorLog, "%s performNextMessage", _actor->actorName().c_str());
#if DEBUG
        assert(++_active == 1);     // Fail-safe check to detect 'impossible' re-entrant call
#endif
        try {
            auto &fn = front();
            fn();
        } catch (...) {
            Warn("EXCEPTION thrown from actor method");
        }
        _actor->afterEvent();
        
#if DEBUG
        assert(--_active == 0);
#endif

        bool empty;
        pop(empty);
        if (!empty)
            reschedule();
        release(_actor);
    }

} }