#pragma once

#include "adapters/audio/MiniaudioPlayer.h"
#include "services/ModelRegistry.h"
#include "services/PlaybackService.h"
#include "services/TtsService.h"

#include <wx/panel.h>

namespace adayo::ui {
class TtsPanel final : public wxPanel {
public:
    explicit TtsPanel(wxWindow* parent);
private:
    ModelRegistry model_registry_;
    ModelRegistryScanResult model_scan_;
    TtsService tts_service_;
    MiniaudioPlayer audio_player_;
    PlaybackService playback_service_;
};
} // namespace adayo::ui
