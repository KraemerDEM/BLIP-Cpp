//
// GCDMailbox.cc
//
// Copyright (c) 2017 Couchbase, Inc All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "GCDMailbox.hh"
#include "Actor.hh"
#include "Logging.hh"
#include <algorithm>

using namespace std;

namespace litecore { namespace actor {

    extern LogDomain ActorLog;

#if ACTORS_TRACK_STATS
#define beginLatency()  fleece::Stopwatch st
#define endLatency()    _maxLatency = max(_maxLatency, (double)st.elapsed())
#define beginBusy()     _busy.start()
#define endBusy()       _busy.stop()
#else
#define beginLatency()  ({})
#define endLatency()    ({})
#define beginBusy()     ({})
#define endBusy()       ({})
#endif

    
    static char kQueueMailboxSpecificKey;

    GCDMailbox::GCDMailbox(Actor *a, const std::string &name, Scheduler *s)
    :_actor(a)
    {
        dispatch_queue_attr_t attr = DISPATCH_QUEUE_SERIAL;
        attr = dispatch_queue_attr_make_with_qos_class(attr, QOS_CLASS_UTILITY, 0);
        if (__builtin_available(iOS 10.0, macOS 10.12, tvos 10.0, watchos 3.0, *)) {
            attr = dispatch_queue_attr_make_with_autorelease_frequency(attr,
                                                        DISPATCH_AUTORELEASE_FREQUENCY_NEVER);
        }
        _queue = dispatch_queue_create((name.empty() ? nullptr : name.c_str()), attr);
        dispatch_queue_set_specific(_queue, &kQueueMailboxSpecificKey, this, nullptr);
    }

    GCDMailbox::~GCDMailbox() {
        dispatch_release(_queue);
    }


    std::string GCDMailbox::name() const {
        return dispatch_queue_get_label(_queue);
    }


    GCDMailbox* GCDMailbox::currentMailbox() {
        return (GCDMailbox*) dispatch_get_specific(&kQueueMailboxSpecificKey);
    }

    
    Actor* GCDMailbox::currentActor() {
        auto mailbox = currentMailbox();
        return mailbox ? mailbox->_actor : nullptr;
    }


    static void safelyCall(void (^block)()) {
        try {
            block();
        } catch (const std::exception &x) {
            Warn("Caught exception in Actor: %s", x.what());
        } catch (...) {
            Warn("Caught unknown exception in Actor");
        }
    }

    
    void GCDMailbox::enqueue(void (^block)()) {
        beginLatency();
        ++_eventCount;
        retain(_actor);
        auto wrappedBlock = ^{
            endLatency();
            beginBusy();
            safelyCall(block);
            afterEvent();
        };
        dispatch_async(_queue, wrappedBlock);
    }


    void GCDMailbox::enqueueAfter(delay_t delay, void (^block)()) {
        beginLatency();
        ++_eventCount;
        retain(_actor);
        auto wrappedBlock = ^{
            endLatency();
            beginBusy();
            safelyCall(block);
            afterEvent();
        };
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
        if (ns > 0)
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, ns), _queue, wrappedBlock);
        else
            dispatch_async(_queue, wrappedBlock);
    }


    void GCDMailbox::afterEvent() {
        _actor->afterEvent();
        endBusy();
#if ACTORS_TRACK_STATS
        if (_eventCount > _maxEventCount) {
            _maxEventCount = _eventCount;
        }
#endif
        --_eventCount;
        release(_actor);
    }


    void GCDMailbox::logStats() {
#if ACTORS_TRACK_STATS
        LogTo(ActorLog, "Max queue depth of %s was %d; max latency was %s; busy %s (%.1f%%)",
              _actor->actorName().c_str(), _maxEventCount,
              fleece::Stopwatch::formatTime(_maxLatency).c_str(),
              fleece::Stopwatch::formatTime(_busy.elapsed()).c_str(),
              (_busy.elapsed() / _createdAt.elapsed())*100.0);
#endif
    }

} }
