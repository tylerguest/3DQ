/*
  ==============================================================================

    3DQ editor: hosts the web GUI (ui/) inside a juce::WebBrowserComponent.

    Band sync:
      - GUI calls native setBands([{ id, freq, gainDb, q, pos, muted, range, threshold, type, slope }, ...])
        type is "peak" | "lowcut" | "highcut", slope in dB/oct (cuts) whenever
        it changes; id is the parameter slot (0..maxBands-1), pos is degrees.
      - GUI calls native getBands() on load.
      - When the host changes band parameters (automation, preset load, undo),
        the editor emits a "bandsChanged" event carrying the same array.

    Audio response: every timer tick the editor analyses the processor's recent
    output and emits a "spectrum" event { ll, rr, lr } (see StereoSpectrum.h),
    plus each band slot's live dynamic gain and detector level { gains, levels }.

    The production GUI (ui/build) is embedded as BinaryData and served through
    the resource provider. To develop with live reload, run `npm run dev` in ui/
    and launch the plugin with THREEDQ_DEV_SERVER=1 in the environment; the
    editor then loads http://localhost:3000/ instead.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::String devServerAddress { "http://localhost:3000/" };

const char* getMimeForExtension (const juce::String& extension)
{
    static const std::unordered_map<juce::String, const char*> mimeMap =
    {
        { { "html" }, "text/html" },
        { { "js" },   "text/javascript" },
        { { "css" },  "text/css" },
        { { "json" }, "application/json" },
        { { "svg" },  "image/svg+xml" },
        { { "png" },  "image/png" },
        { { "woff2" }, "font/woff2" }
    };

    if (const auto it = mimeMap.find (extension.toLowerCase()); it != mimeMap.end())
        return it->second;

    return "application/octet-stream";
}

const std::array<const char*, 3> typeNames { "peak", "lowcut", "highcut" };

int typeIndexFromName (const juce::String& name)
{
    for (size_t i = 0; i < typeNames.size(); ++i)
        if (name == typeNames[i])
            return (int) i;
    return 0;
}

int slopeIndexFromDb (int slope)
{
    // Nearest available slope.
    int best = 0;
    for (size_t i = 0; i < threedq::cutSlopes.size(); ++i)
        if (std::abs (threedq::cutSlopes[i] - slope) < std::abs (threedq::cutSlopes[(size_t) best] - slope))
            best = (int) i;
    return best;
}

bool useDevServer()
{
    return juce::SystemStats::getEnvironmentVariable ("THREEDQ_DEV_SERVER", {}).isNotEmpty();
}
}

//==============================================================================
_3DQAudioProcessorEditor::_3DQAudioProcessorEditor (_3DQAudioProcessor& p)
    : AudioProcessorEditor (&p),
      audioProcessor (p),
      webView (juce::WebBrowserComponent::Options{}
                   .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                   .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                                .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
                   .withNativeIntegrationEnabled()
                   .withNativeFunction ("setBands", [this] (auto& args, auto complete)
                                        {
                                            if (! args.isEmpty())
                                                applyBandsFromGui (args[0]);
                                            complete ({});
                                        })
                   .withNativeFunction ("getBands", [this] (auto&, auto complete)
                                        {
                                            complete (getBandsForGui());
                                        })
                   .withResourceProvider ([] (const auto& url) { return getResource (url); },
                                          juce::URL { devServerAddress }.getOrigin()))
{
    addAndMakeVisible (webView);

    webView.goToURL (useDevServer() ? devServerAddress
                                    : juce::WebBrowserComponent::getResourceProviderRoot());

    for (int i = 0; i < threedq::maxBands; ++i)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (i);
        for (const auto& id : { ids.enabled, ids.freq, ids.gain, ids.q, ids.pos, ids.mute, ids.range, ids.threshold, ids.type, ids.slope })
            audioProcessor.state.addParameterListener (id, this);
    }

    startTimerHz (60);

    setResizable (true, true);
    setSize (800, 500);
}

_3DQAudioProcessorEditor::~_3DQAudioProcessorEditor()
{
    for (int i = 0; i < threedq::maxBands; ++i)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (i);
        for (const auto& id : { ids.enabled, ids.freq, ids.gain, ids.q, ids.pos, ids.mute, ids.range, ids.threshold, ids.type, ids.slope })
            audioProcessor.state.removeParameterListener (id, this);
    }
}

//==============================================================================
void _3DQAudioProcessorEditor::applyBandsFromGui (const juce::var& bands)
{
    if (! bands.isArray())
        return;

    applyingGuiChange = true;
    const juce::ScopeGuard resetFlag { [this] { applyingGuiChange = false; } };

    std::array<const juce::var*, threedq::maxBands> bySlot {};
    for (const auto& b : *bands.getArray())
    {
        const int slot = (int) b["id"];
        if (slot >= 0 && slot < threedq::maxBands)
            bySlot[(size_t) slot] = &b;
    }

    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* param = audioProcessor.state.getParameter (id))
        {
            const auto normalised = param->convertTo0to1 (value);
            if (std::abs (param->getValue() - normalised) > 1.0e-6f)
                param->setValueNotifyingHost (normalised);
        }
    };

    for (int slot = 0; slot < threedq::maxBands; ++slot)
    {
        const auto ids = _3DQAudioProcessor::getBandParamIDs (slot);

        if (const auto* b = bySlot[(size_t) slot])
        {
            set (ids.freq, (float) (double) (*b)["freq"]);
            set (ids.gain, (float) (double) (*b)["gainDb"]);
            set (ids.q,    (float) (double) (*b)["q"]);
            set (ids.pos,  (float) (double) (*b)["pos"]);
            set (ids.mute, (bool) (*b)["muted"] ? 1.0f : 0.0f);
            set (ids.range, (float) (double) (*b)["range"]);
            set (ids.threshold, (float) (double) (*b)["threshold"]);
            set (ids.type, (float) typeIndexFromName ((*b)["type"].toString()));
            set (ids.slope, (float) slopeIndexFromDb ((int) (*b)["slope"]));
            set (ids.enabled, 1.0f);
        }
        else
        {
            set (ids.enabled, 0.0f);
            set (ids.mute, 0.0f);
            set (ids.range, 0.0f);
            set (ids.type, 0.0f);
        }
    }
}

juce::var _3DQAudioProcessorEditor::getBandsForGui() const
{
    juce::Array<juce::var> result;

    for (int slot = 0; slot < threedq::maxBands; ++slot)
    {
        const auto p = audioProcessor.getBandParams (slot);
        if (! p.enabled)
            continue;

        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty ("id", slot);
        obj->setProperty ("freq", p.freq);
        obj->setProperty ("gainDb", p.gainDb);
        obj->setProperty ("q", p.q);
        obj->setProperty ("pos", p.posDeg);
        obj->setProperty ("muted", audioProcessor.isBandMuted (slot));
        obj->setProperty ("range", p.rangeDb);
        obj->setProperty ("threshold", p.thresholdDb);
        obj->setProperty ("type", typeNames[(size_t) p.type]);
        obj->setProperty ("slope", p.slopeDbPerOct);
        result.add (juce::var (obj.release()));
    }

    return result;
}

void _3DQAudioProcessorEditor::parameterChanged (const juce::String&, float)
{
    // May arrive on the audio thread (host automation); just flag it.
    if (! applyingGuiChange)
        hostChangedBands = true;
}

void _3DQAudioProcessorEditor::timerCallback()
{
    if (hostChangedBands.exchange (false))
        webView.emitEventIfBrowserIsVisible ("bandsChanged", getBandsForGui());

    sendSpectrum();
}

void _3DQAudioProcessorEditor::sendSpectrum()
{
    audioProcessor.copyRecentOutput (recentLeft.data(), recentRight.data(), StereoSpectrum::fftSize);
    const auto result = spectrum.analyse (recentLeft.data(), recentRight.data(), audioProcessor.getAnalysisSampleRate());

    auto toVar = [] (const auto& values)
    {
        juce::Array<juce::var> array;
        array.ensureStorageAllocated ((int) values.size());
        for (auto v : values)
            array.add (v);
        return juce::var (array);
    };

    auto obj = std::make_unique<juce::DynamicObject>();
    obj->setProperty ("ll", toVar (result.ll));
    obj->setProperty ("rr", toVar (result.rr));
    obj->setProperty ("lr", toVar (result.lr));

    // Live dynamics: each slot's current total gain and detector level.
    std::array<float, threedq::maxBands> gains {}, levels {};
    for (int i = 0; i < threedq::maxBands; ++i)
    {
        gains[(size_t) i]  = audioProcessor.getLiveGainDb (i);
        levels[(size_t) i] = audioProcessor.getLiveLevelDb (i);
    }
    obj->setProperty ("gains", toVar (gains));
    obj->setProperty ("levels", toVar (levels));
    webView.emitEventIfBrowserIsVisible ("spectrum", juce::var (obj.release()));
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource> _3DQAudioProcessorEditor::getResource (const juce::String& url)
{
    const auto path = url.upToFirstOccurrenceOf ("?", false, false).fromFirstOccurrenceOf ("/", false, false);
    const auto fileName = path.isEmpty() ? juce::String { "index.html" } : path;

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (fileName != BinaryData::originalFilenames[i])
            continue;

        int size = 0;
        const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);

        if (data == nullptr)
            return std::nullopt;

        std::vector<std::byte> bytes ((size_t) size);
        std::memcpy (bytes.data(), data, (size_t) size);

        return juce::WebBrowserComponent::Resource { std::move (bytes),
                                                     getMimeForExtension (fileName.fromLastOccurrenceOf (".", false, false)) };
    }

    return std::nullopt;
}

void _3DQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111110));
}

void _3DQAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}
