#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/fmt/bundled/xchar.h>
#include <mutex>
#include "LogPanel.hpp"

wxDEFINE_EVENT(Spdlog_NewMsg, wxThreadEvent);

template <typename Mutex>
class LogPanelSink : public spdlog::sinks::base_sink<Mutex>
{
public:
    typedef std::function<void(const wxString&)> Callback;
    LogPanelSink(Callback fn)
    {
        this->fn = fn;
    }

protected:
    virtual void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);

        wxString data = wxString::FromUTF8(formatted.data(), formatted.size());
        fn(data);
    }

    virtual void flush_() override
    {
    }

private:
    Callback fn;
};

struct LogPanel::Data
{
    Data(LogPanel* owner);
    void OnSpdlogCallback(const wxString& msg);
    void OnSpdlog(const wxThreadEvent& evt);

    LogPanel*   owner;
    wxTextCtrl* logTextCtrl;
};

LogPanel::Data::Data(LogPanel* owner)
{
    this->owner = owner;

    spdlog::set_default_logger(spdlog::synchronous_factory::create<LogPanelSink<std::mutex>>(
        "wxLogger", std::bind(&Data::OnSpdlogCallback, this, std::placeholders::_1)));

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    logTextCtrl = new wxTextCtrl(owner, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxTE_MULTILINE | wxTE_READONLY);
    sizer->Add(logTextCtrl, 1, wxEXPAND);
    owner->SetSizer(sizer);

    owner->Bind(Spdlog_NewMsg, &Data::OnSpdlog, this);
}

void LogPanel::Data::OnSpdlogCallback(const wxString& msg)
{
    wxThreadEvent* e = new wxThreadEvent(Spdlog_NewMsg);
    e->SetString(msg);
    owner->QueueEvent(e);
}

void LogPanel::Data::OnSpdlog(const wxThreadEvent& evt)
{
    logTextCtrl->AppendText(evt.GetString());
}

LogPanel::LogPanel(wxWindow* parent) : wxPanel(parent)
{
    m_data = new Data(this);
}

LogPanel::~LogPanel()
{
    delete m_data;
}
