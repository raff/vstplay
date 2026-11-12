#include <juce_audio_utils/juce_audio_utils.h>

using namespace juce;

// Window that hosts a plugin's own editor.
class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioProcessorEditor* editor, const String& name, std::function<void()> onClose)
        : DocumentWindow (name, Colours::darkgrey, DocumentWindow::closeButton),
          closed (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { closed(); }

private:
    std::function<void()> closed;
};

class MainComponent : public Component
{
public:
    MainComponent()
        : keyboard (keyboardState, MidiKeyboardComponent::horizontalKeyboard)
    {
        formatManager.addDefaultFormats();
        loadKnownPlugins();

        deviceManager.initialise (0, 2, nullptr, true);
        // Route the plugin through a graph so every bus it declares gets a real buffer.
        graph.setPlayConfigDetails (0, 2, 44100.0, 512);
        using IOProcessor = AudioProcessorGraph::AudioGraphIOProcessor;
        outputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::audioOutputNode));
        midiInputNode = graph.addNode (std::make_unique<IOProcessor> (IOProcessor::midiInputNode));
        player.setProcessor (&graph);

        deviceManager.addAudioCallback (&player);
        for (auto& in : MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled (in.identifier, true);
        deviceManager.addMidiInputDeviceCallback ({}, &player);
        keyboardState.addListener (&player.getMidiMessageCollector());

        addAndMakeVisible (scanButton);
        addAndMakeVisible (formatBox);
        addAndMakeVisible (pluginBox);
        addAndMakeVisible (showEditorButton);
        addAndMakeVisible (status);
        addAndMakeVisible (keyboard);

        formatBox.addItem ("All", 1);
        formatBox.addItem ("VST3", 2);
        formatBox.addItem ("AU", 3);
        formatBox.setSelectedId (1, dontSendNotification);
        formatBox.onChange = [this] { refreshPluginBox(); };

        scanButton.onClick = [this] { scanPlugins(); };
        showEditorButton.onClick = [this] { showEditor(); };
        pluginBox.onChange = [this] { loadSelectedPlugin(); };

        status.setJustificationType (Justification::centredLeft);
        refreshPluginBox();
        setSize (700, 180);
    }

    ~MainComponent() override
    {
        closeEditor();
        keyboardState.removeListener (&player.getMidiMessageCollector());
        deviceManager.removeMidiInputDeviceCallback ({}, &player);
        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        graph.clear();
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto top = area.removeFromTop (28);
        scanButton.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (8);
        formatBox.setBounds (top.removeFromLeft (80));
        top.removeFromLeft (8);
        showEditorButton.setBounds (top.removeFromRight (100));
        top.removeFromRight (8);
        pluginBox.setBounds (top);
        area.removeFromTop (8);
        status.setBounds (area.removeFromTop (24));
        area.removeFromTop (8);
        keyboard.setBounds (area);
    }

private:
    File knownPluginsFile() const
    {
        return File::getSpecialLocation (File::userApplicationDataDirectory)
                   .getChildFile ("VstPlay/plugins.xml");
    }

    void loadKnownPlugins()
    {
        if (auto xml = parseXML (knownPluginsFile()))
            knownPlugins.recreateFromXml (*xml);
    }

    void saveKnownPlugins()
    {
        knownPluginsFile().getParentDirectory().createDirectory();
        if (auto xml = knownPlugins.createXml())
            xml->writeTo (knownPluginsFile());
    }

    // Scans synchronously on the message thread; fine for personal use, but a
    // misbehaving plugin can take the whole app down while it is being scanned.
    void scanPlugins()
    {
        status.setText ("Scanning...", dontSendNotification);
        for (auto* format : formatManager.getFormats())
        {
            PluginDirectoryScanner scanner (knownPlugins, *format,
                                            format->getDefaultLocationsToSearch(),
                                            true, {});
            String name;
            while (scanner.scanNextFile (true, name)) {}
        }
        saveKnownPlugins();
        refreshPluginBox();
        status.setText ("Found " + String (knownPlugins.getNumTypes()) + " plugins",
                        dontSendNotification);
    }

    void refreshPluginBox()
    {
        pluginBox.clear (dontSendNotification);
        types.clear();
        auto wanted = formatBox.getSelectedId();
        for (auto& t : knownPlugins.getTypes())
            if (wanted == 1
                || (wanted == 2 && t.pluginFormatName == "VST3")
                || (wanted == 3 && t.pluginFormatName == "AudioUnit"))
                types.add (t);
        for (int i = 0; i < types.size(); ++i)
            pluginBox.addItem (types[i].name + " (" + types[i].pluginFormatName + ")", i + 1);
        pluginBox.setTextWhenNothingSelected ("Select a plugin");
    }

    void loadSelectedPlugin()
    {
        auto index = pluginBox.getSelectedItemIndex();
        if (! isPositiveAndBelow (index, types.size()))
            return;

        unloadPlugin();

        auto desc = types[index];
        auto setup = deviceManager.getAudioDeviceSetup();
        status.setText ("Loading " + desc.name + "...", dontSendNotification);

        formatManager.createPluginInstanceAsync (
            desc, setup.sampleRate, setup.bufferSize > 0 ? setup.bufferSize : 512,
            [safe = SafePointer<MainComponent> (this)] (std::unique_ptr<AudioPluginInstance> p,
                                                        const String& error)
            {
                if (safe == nullptr)
                    return;
                if (p == nullptr)
                {
                    safe->status.setText ("Failed: " + error, dontSendNotification);
                    return;
                }
                safe->attachPlugin (std::move (p));
            });
    }

    void attachPlugin (std::unique_ptr<AudioPluginInstance> plugin)
    {
        auto numOuts = plugin->getTotalNumOutputChannels();
        pluginNode = graph.addNode (std::move (plugin));

        // Feed the plugin's first (up to) two output channels to the stereo output.
        for (int ch = 0; ch < 2 && numOuts > 0; ++ch)
            graph.addConnection ({ { pluginNode->nodeID, jmin (ch, numOuts - 1) },
                                   { outputNode->nodeID, ch } });

        graph.addConnection ({ { midiInputNode->nodeID, AudioProcessorGraph::midiChannelIndex },
                               { pluginNode->nodeID,    AudioProcessorGraph::midiChannelIndex } });

        status.setText ("Loaded " + pluginNode->getProcessor()->getName(), dontSendNotification);
        showEditor();
    }

    void unloadPlugin()
    {
        closeEditor();
        if (pluginNode != nullptr)
        {
            graph.removeNode (pluginNode->nodeID);
            pluginNode = nullptr;
        }
    }

    void showEditor()
    {
        if (pluginNode == nullptr || ! pluginNode->getProcessor()->hasEditor())
            return;
        auto* instance = pluginNode->getProcessor();
        if (editorWindow != nullptr)
        {
            editorWindow->toFront (true);
            return;
        }
        editorWindow = std::make_unique<PluginWindow> (
            instance->createEditorIfNeeded(), instance->getName(),
            [this] { MessageManager::callAsync ([safe = SafePointer<MainComponent> (this)]
                                                { if (safe != nullptr) safe->closeEditor(); }); });
    }

    void closeEditor() { editorWindow.reset(); }

    AudioPluginFormatManager formatManager;
    KnownPluginList knownPlugins;
    Array<PluginDescription> types;
    AudioDeviceManager deviceManager;
    AudioProcessorGraph graph;
    AudioProcessorGraph::Node::Ptr pluginNode, outputNode, midiInputNode;
    AudioProcessorPlayer player;
    MidiKeyboardState keyboardState;

    std::unique_ptr<PluginWindow> editorWindow;

    TextButton scanButton { "Scan" }, showEditorButton { "Show editor" };
    ComboBox pluginBox, formatBox;
    Label status;
    MidiKeyboardComponent keyboard;
};

class MainWindow : public DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("VstPlay", Colours::darkgrey, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent(), true);
        setResizable (true, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class VstPlayApp : public JUCEApplication
{
public:
    const String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }

    void initialise (const String&) override { window = std::make_unique<MainWindow>(); }
    void shutdown() override                 { window.reset(); }

private:
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (VstPlayApp)
