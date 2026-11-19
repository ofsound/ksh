#include <PluginEditor.h>
#include <ui/KshDrumComputerImport.h>

#include <catch2/catch_test_macros.hpp>
#include <span>

namespace
{
    void writeArray (juce::MemoryOutputStream& file, int id, std::span<const float> values)
    {
        file.writeInt (id);
        file.writeInt (static_cast<int> (values.size()) + 2);
        file.writeInt (0xab4130);
        file.writeInt (static_cast<int> (values.size()));
        for (const auto value : values)
            file.writeFloat (value);
    }

    void writeControl (juce::MemoryOutputStream& file, int id, float value)
    {
        file.writeInt (id);
        file.writeInt (1);
        file.writeFloat (value);
    }

    juce::MemoryBlock makePreset (float bpm = 117.5f, bool mixedRates = false)
    {
        std::array<float, 16384> bank {};
        std::array<float, 1024> lengths {};
        lengths.fill (15.0f);
        lengths[0] = 11.0f;
        lengths[15 * 8 + 1] = 8.0f;
        constexpr std::array<float, 8> notes { 43, 51, 36, 40, 42, 44, 46, 48 };
        bank[3] = 0.85f;          // Pattern 1, track 1, step 4.
        bank[128 + 3] = 0.30f;   // Every third visit.
        bank[256 + 3] = 0.43f;   // Four evenly spaced hits.
        bank[384 + 3] = -0.25f;  // Negative offset, rounded to KSH nudge.
        bank[15 * 1024 + 16 + 7] = 0.5f;
        bank[15 * 1024 + 256 + 16 + 7] = 1.0f; // Eight-hit roll in slot 16.

        juce::MemoryOutputStream records;
        writeArray (records, 244, bank);
        writeArray (records, 844, lengths);
        writeArray (records, 158, notes);
        writeControl (records, 245, mixedRates ? 0.0f : 0.35f);
        writeControl (records, 246, mixedRates ? 0.0f : 0.5f);
        writeControl (records, 248, mixedRates ? 6.0f : 8.0f);
        records.writeInt (-1);
        records.writeInt (mixedRates ? 0 : -3);

        juce::MemoryOutputStream file;
        file.writeInt (static_cast<int> ((36 + records.getDataSize()) / 4));
        file.writeInt (9);
        file.writeFloat (bpm);
        for (int i = 0; i < 5; ++i)
            file.writeInt (0);
        file.writeInt (100518);
        file.write (records.getData(), records.getDataSize());
        return file.getMemoryBlock();
    }

    void saveFixture (const juce::File& file, const juce::MemoryBlock& data)
    {
        REQUIRE (file.replaceWithData (data.getData(), data.getSize()));
    }
}

TEST_CASE ("DrumComputer presets preserve sparse slots, split dynamics and groove controls", "[drumcomputer-import]")
{
    const juce::TemporaryFile source (".sbb");
    saveFixture (source.getFile(), makePreset());
    const auto import = ksh::ui::importDrumComputerPreset (source.getFile(), "Pattern bank.sbb", 120.0);
    REQUIRE (import.error.isEmpty());
    CHECK (import.bpm == 117.5);
    CHECK (import.populatedPatterns == 2);
    CHECK_FALSE (import.approximateTiming);

    auto engine = std::make_unique<ksh::KickSnareHatEngine>();
    REQUIRE (engine->deserializeForPersistence (nlohmann::json::parse (import.projectData)));
    const auto document = engine->documentState();
    const auto& settings = document.settings;
    CHECK (settings.tempo == 117.5);
    CHECK (settings.swing == 35);
    CHECK (settings.swingSubdivisionIndex == 0);
    CHECK (settings.timingHumanize == 13);
    CHECK (settings.channels[1].note == 51);
    CHECK (settings.sourceSettings[0].loopRanges[0].loopLength == 12);
    CHECK (settings.sourceSettings[15].loopRanges[1].loopLength == 9);
    CHECK_FALSE (document.sources[1][0][3].enabled);

    const auto& hit = document.sources[0][0][3];
    CHECK (hit.enabled);
    CHECK (hit.cycle == 3);
    CHECK (hit.cycleMask == 0b100);
    CHECK (hit.roll == 4);
    CHECK (hit.span == 1);
    CHECK (hit.nudge == -23);
    CHECK (hit.splitVelocity (0) == 107);
    CHECK (hit.splitVelocity (1) == 75);
    CHECK (hit.splitVelocity (3) == 75);
    CHECK (document.sources[15][1][7].enabled);
    CHECK (document.sources[15][1][7].roll == 8);
}

TEST_CASE ("A slower DrumComputer row keeps its roll duration on a shared KSH grid", "[drumcomputer-import]")
{
    const juce::TemporaryFile source (".sbb");
    saveFixture (source.getFile(), makePreset (0.0f, true));
    const auto import = ksh::ui::importDrumComputerPreset (source.getFile(), "Mixed.sbb", 139.0);
    REQUIRE (import.error.isEmpty());
    CHECK_FALSE (import.bpm.has_value());
    const auto project = nlohmann::json::parse (import.projectData);
    CHECK (project.at ("tempo") == 139.0);
    CHECK (project.at ("stepCount") == 32);
    CHECK (project.at ("rate") == "16n");
    CHECK (project.at ("sourceSettings")[0][2][0][1] == 24);
    REQUIRE (project.at ("cellSpans").size() == 1);
    CHECK (project.at ("cellSpans")[0] == nlohmann::json { 0, 0, 6, 2 });
}

TEST_CASE ("Dropping a DrumComputer preset opens a new unsaved KSH project", "[drumcomputer-import][native-ui]")
{
    const juce::TemporaryFile source (".sbb");
    saveFixture (source.getFile(), makePreset());
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Standalone);
    PluginProcessor processor;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_Undefined);
    processor.setStandaloneTempoBpm (132.0);
    processor.markDocumentClean();
    PluginEditor editor (processor);
    const auto epoch = processor.getUiBridge().getDocumentEpoch();
    const juce::StringArray files { source.getFile().getFullPathName() };
    REQUIRE (editor.isInterestedInFileDrag (files));
    CHECK_FALSE (editor.isInterestedInFileDrag ({ files[0], files[0] }));
    editor.filesDropped (files, 400, 200);

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (processor.getUiBridge().getDocumentEpoch() == epoch && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

    REQUIRE (processor.getUiBridge().getDocumentEpoch() != epoch);
    CHECK (processor.getProjectName() == source.getFile().getFileNameWithoutExtension());
    CHECK (processor.getStandaloneTempoBpm() == 117.5);
    CHECK (processor.isDocumentModified());
    CHECK (processor.getProjectFile() == juce::File());

    auto damaged = makePreset();
    damaged.setSize (damaged.getSize() - 12);
    saveFixture (source.getFile(), damaged);
    const auto invalid = ksh::ui::importDrumComputerPreset (source.getFile(), source.getFile().getFileName(), 120.0);
    CHECK (invalid.error.isNotEmpty());
    CHECK (invalid.projectData.empty());
}
