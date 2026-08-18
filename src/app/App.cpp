#include "ui/MainFrame.h"

#include <wx/app.h>

class AdayoApp final : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new adayo::ui::MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(AdayoApp);
