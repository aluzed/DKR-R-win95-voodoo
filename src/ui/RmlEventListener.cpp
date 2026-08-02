#include "dkrport/ui/RmlEventListener.h"

#include <RmlUi/Core/Event.h>

#include <utility>

namespace dkrport {

RmlEventListener::RmlEventListener(std::string action, Callback callback)
    : m_action(std::move(action)), m_callback(std::move(callback)) {}

void RmlEventListener::ProcessEvent(Rml::Event& event) {
    if (event.GetType() == "click" && m_callback) m_callback(m_action);
}

} // namespace dkrport
