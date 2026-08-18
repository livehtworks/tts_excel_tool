#include "adapters/tts/MossNanoTtsEngine.h"

#include "TestCheck.h"

#include <stdexcept>
#include <string>

using namespace adayo;

namespace {

void AssertMossPortBlockedMessage(const std::string& message) {
    REQUIRE(message.find("MOSS Nano ONNX C++ adapter") != std::string::npos);
    REQUIRE(message.find("禁止启用") != std::string::npos);
}

void TestMossAdapterRemainsBlockedUntilOfficialParityExists() {
    MossNanoTtsEngine engine;
    REQUIRE(engine.Id() == "moss-nano-onnx");
    REQUIRE(!engine.IsLoaded());

    TtsModelConfig config;
    bool load_threw = false;
    try {
        engine.Load(config);
    } catch (const std::runtime_error& ex) {
        load_threw = true;
        AssertMossPortBlockedMessage(ex.what());
    }
    REQUIRE(load_threw);
    REQUIRE(!engine.IsLoaded());

    TtsRequest request;
    request.text = "hello";
    bool synthesize_threw = false;
    try {
        (void)engine.Synthesize(request);
    } catch (const std::runtime_error& ex) {
        synthesize_threw = true;
        AssertMossPortBlockedMessage(ex.what());
    }
    REQUIRE(synthesize_threw);
}

} // namespace

int main() {
    return test::RunTestMain("adayo_p8_moss_blocked_tests", [] {
        TestMossAdapterRemainsBlockedUntilOfficialParityExists();
    });
}
