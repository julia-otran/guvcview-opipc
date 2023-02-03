#include "gviewv4l2core.h"

#define CEC_MODE_PREVENT_REPLY		0x100

void init_cec_controls();
void stop_cec_controls();
int poll_cec_events(v4l2_dev_t *my_vd);

