#pragma once

#include "core/domain/Types.h"

#include <string>

namespace adayo {

class ITtsEngine {
public:
    virtual ~ITtsEngine() = default;
    virtual std::string Id() const = 0;
    virtual std::string RuntimeIdentity() const { return Id(); }
    virtual bool IsLoaded() const noexcept = 0;
    virtual void Load(const TtsModelConfig& config) = 0;
    virtual void Unload() noexcept = 0;
    virtual AudioBuffer Synthesize(const TtsRequest& request) = 0;
};

} // namespace adayo
