#include "app.hpp"

#include "app_identity.h"
#include "pages/assistant/assistant_view.hpp"

#include <memory>

namespace {

struct AppState {
    std::unique_ptr<AssistantViewModel> assistant_view_model;
    std::unique_ptr<AssistantView> assistant_view;
};

AppState app_state;

void app_open(const cm0_app_context_t *context)
{
    app_state.assistant_view_model = std::make_unique<AssistantViewModel>();
    app_state.assistant_view       = std::make_unique<AssistantView>(*app_state.assistant_view_model);
    app_state.assistant_view->create(context->root, LILYGO_UI_TEMPLATE_APP_NAME);
}

void app_close()
{
    if (app_state.assistant_view) app_state.assistant_view->destroy();
    app_state.assistant_view.reset();
    app_state.assistant_view_model.reset();
}

const cm0_app_descriptor_t app_descriptor{
    CM0_APP_API_VERSION, sizeof(cm0_app_descriptor_t), LILYGO_UI_TEMPLATE_APP_ID, app_open, app_close,
};

}  // namespace

extern "C" const cm0_app_descriptor_t *cm0_app_get_descriptor()
{
    return &app_descriptor;
}
