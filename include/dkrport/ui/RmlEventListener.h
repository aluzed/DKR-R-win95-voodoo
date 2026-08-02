#pragma once

#include <RmlUi/Core/EventListener.h>

#include <functional>
#include <string>

namespace dkrport {

class RmlEventListener final : public Rml::EventListener {
  public:
    using Callback = std::function<void(const std::string&)>;

    RmlEventListener(std::string action, Callback callback);
    void ProcessEvent(Rml::Event& event) override;

  private:
    std::string m_action;
    Callback m_callback;
};

} // namespace dkrport
