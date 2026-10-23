#include "gui_guider.h"
#include "gg_utils.h"

/* Runtime instance used by the GUI Guider generated screen code. */
gg_ui_t guider_ui;

void gg_ui_init(void)
{
    setup_ui(&guider_ui);
}
