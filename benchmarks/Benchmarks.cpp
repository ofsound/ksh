TEST_CASE ("Boot performance")
{
    BENCHMARK_ADVANCED ("Processor constructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::storage_for<PluginProcessor>> storage (size_t (meter.runs()));
        meter.measure ([&] (int i) { storage[(size_t) i].construct(); });
    };

    BENCHMARK_ADVANCED ("Processor destructor")
    (Catch::Benchmark::Chronometer meter)
    {
        std::vector<Catch::Benchmark::destructable_object<PluginProcessor>> storage (size_t (meter.runs()));
        for (auto& s : storage)
            s.construct();
        meter.measure ([&] (int i) { storage[(size_t) i].destruct(); });
    };

    BENCHMARK_ADVANCED ("Editor open and close")
    (Catch::Benchmark::Chronometer meter)
    {
        PluginProcessor plugin;

        // due to complex construction logic of the editor, let's measure open/close together
        meter.measure ([&] (int /* i */) {
            auto editor = plugin.createEditorAndMakeActive();
            plugin.editorBeingDeleted (editor);
            delete editor;
            return plugin.getActiveEditor();
        });
    };
}

TEST_CASE ("Native editor performance", "[native-ui]")
{
    PluginProcessor processor;
    (void) processor.dispatchUiEngineCommand ("source_steps", { 1, 32 });
    for (int ch = 0; ch < 8; ++ch)
        for (int step = 0; step < 32; ++step)
            (void) processor.dispatchUiEngineCommand ("cell", { 1, ch + 1, step + 1, 1, 100, 85, 4, 5, 2 });
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<ksh::ui::NativeEditor*> (editor->getChildComponent (0));
    REQUIRE (view != nullptr);
    BENCHMARK ("Dense 32-step full native paint")
    {
        return editor->createComponentSnapshot (editor->getLocalBounds());
    };
    BENCHMARK ("Eight-channel flash strip repaint")
    {
        return editor->createComponentSnapshot ({ 366, 212, 54, 464 });
    };
    BENCHMARK ("Native idle state poll")
    {
        return view->controller.poll();
    };
    BENCHMARK ("32-cell edit and publish")
    {
        auto& controller = view->controller;
        controller.begin ("Benchmark edit");
        for (int step = 0; step < 32; ++step)
        {
            auto cell = controller.cell ({ 0, step });
            cell.velocity = cell.velocity == 100 ? 99 : 100;
            controller.editCell ({ 0, step }, cell);
        }
        controller.commit();
        return controller.cell ({ 0, 0 }).velocity;
    };
}
