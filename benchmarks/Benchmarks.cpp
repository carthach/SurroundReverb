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

TEST_CASE ("Reverb performance")
{
    // One second of audio per run, so the mean time reads directly as a fraction of realtime
    const auto benchmarkLayout = [] (const char* name, int numChannels) {
        ReverbEngine engine;
        engine.prepare (48000.0, ReverbEngine::Layout::matched (numChannels));

        juce::Random random (1);
        juce::AudioBuffer<float> noise (numChannels, 512);

        for (int channel = 0; channel < numChannels; ++channel)
            for (int sample = 0; sample < noise.getNumSamples(); ++sample)
                noise.setSample (channel, sample, random.nextFloat() - 0.5f);

        juce::AudioBuffer<float> buffer (numChannels, 512);

        BENCHMARK (name)
        {
            for (int block = 0; block < 48000 / 512; ++block)
            {
                buffer.makeCopyOf (noise, true);
                engine.process (buffer);
            }

            return buffer.getSample (0, 0);
        };
    };

    benchmarkLayout ("1 second of stereo at 48 kHz", 2);
    benchmarkLayout ("1 second of 5.1 at 48 kHz", 6);
    benchmarkLayout ("1 second of 7.1.4 at 48 kHz", 12);
}
