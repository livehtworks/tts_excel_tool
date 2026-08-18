#include "adapters/tts/MossNanoTtsEngine.h"

#include <cassert>
#include <stdexcept>
#include <string>

using namespace adayo;

namespace {

void AssertMossPortBlockedMessage(const std::string& message) {
    assert(message.find("MOSS Nano ONNX C++ adapter") != std::string::npos);
    assert(message.find("禁止启用") != std::string::npos);
}

void TestMossAdapterRemainsBlockedUntilOfficialParityExists() {
    MossNanoTtsEngine engine;
    assert(engine.Id() == "moss-nano-onnx");
    assert(!engine.IsLoaded());

    TtsModelConfig config;
    bool load_threw = false;
    try {
        engine.Load(config);
    } catch (const std::runtime_error& ex) {
        load_threw = true;
        AssertMossPortBlockedMessage(ex.what());
    }
    assert(load_threw);
    assert(!engine.IsLoaded());

    TtsRequest request;
    request.text = "hello";
    bool synthesize_threw = false;
    try {
        (void)engine.Synthesize(request);
    } catch (const std::runtime_error& ex) {
        synthesize_threw = true;
        AssertMossPortBlockedMessage(ex.what());
    }
    assert(synthesize_threw);
}

} // namespace

int main() {
    TestMossAdapterRemainsBlockedUntilOfficialParityExists();
    return 0;
}
