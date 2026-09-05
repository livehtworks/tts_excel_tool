#include "ui/MainFrame.h"

#include "ui/UiString.h"

#include <wx/app.h>
#include <wx/msgdlg.h>

#include <exception>
#include <string>

class AdayoApp final : public wxApp {
public:
    bool OnInit() override {
        try {
            auto* frame = new adayo::ui::MainFrame();
            frame->Show(true);
            return true;
        } catch (const std::exception& ex) {
            wxMessageBox(adayo::ui::WxUtf8(std::string("启动失败：") + ex.what()),
                adayo::ui::WxUtf8("Adayo语料测试"),
                wxOK | wxICON_ERROR);
            return false;
        } catch (...) {
            wxMessageBox(adayo::ui::WxUtf8("启动失败：未知错误"),
                adayo::ui::WxUtf8("Adayo语料测试"),
                wxOK | wxICON_ERROR);
            return false;
        }
    }
};

wxIMPLEMENT_APP(AdayoApp);
