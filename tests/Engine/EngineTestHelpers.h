#pragma once

#include "EngineTestPeer.h"

#include <engine/KickSnareHatEngine.h>

#include <vector>

namespace ksh::test
{

inline void clearAll (KickSnareHatEngine& engine)
{
    EngineTestPeer::clearAll (engine);
}

struct EngineFixture
{
    std::vector<double> randomValues;
    std::vector<std::string> statuses;
    std::vector<MidiNoteEvent> notes;
    KickSnareHatEngine engine { EngineCallbacks {} };

    explicit EngineFixture (std::vector<double> values = {})
        : randomValues (std::move (values))
    {
        rebind();
    }

    void setRandomValues (std::vector<double> values)
    {
        randomValues = std::move (values);
        randomIndex = 0;
        EngineTestPeer::setRandomValues (engine, randomValues);
    }

    void clearAll()
    {
        ksh::test::clearAll (engine);
        statuses.clear();
        notes.clear();
        randomIndex = 0;
    }

private:
    mutable size_t randomIndex = 0;

    void rebind()
    {
        EngineCallbacks callbacks;
        callbacks.rng = [this] { return nextRandom(); };
        callbacks.emitStatus = [this] (const std::string& message)
        {
            statuses.push_back (message);
        };
        callbacks.emitNote = [this] (const MidiNoteEvent& note)
        {
            notes.push_back (note);
        };

        engine = KickSnareHatEngine { callbacks };
        EngineTestPeer::setRandomValues (engine, randomValues);
    }

    double nextRandom() const
    {
        if (randomValues.empty())
            return 0.0;

        const auto value = randomValues[randomIndex % randomValues.size()];
        ++randomIndex;
        return value;
    }
};

} // namespace ksh::test
