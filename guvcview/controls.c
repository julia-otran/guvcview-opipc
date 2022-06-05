#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include "json.h"
#include "gviewv4l2core.h"

#define CTRL_FILE "/var/www/guvcview/ctrl.json"

void write_file(v4l2_dev_t *my_vd) {
	struct json_object *json;
	struct json_object *ctrl_json;

	v4l2_ctrl_t *ctrl = v4l2core_get_control_list(my_vd);
	json = json_object_new_array();

	int32_t value;

	while (ctrl != NULL) {
		ctrl_json = json_object_new_object();
		json_object_object_add(ctrl_json, "ctrlName", json_object_new_string(ctrl->name));
		json_object_object_add(ctrl_json, "ctrlValue", json_object_new_int(ctrl->value));
		json_object_object_add(ctrl_json, "ctrlMax", json_object_new_int(ctrl->control.maximum));
		json_object_object_add(ctrl_json, "ctrlMin", json_object_new_int(ctrl->control.minimum));
		json_object_object_add(ctrl_json, "ctrlStep", json_object_new_int(ctrl->control.step));
		json_object_object_add(ctrl_json, "ctrlDefault", json_object_new_int(ctrl->control.default_value));

		json_object_array_add(json, ctrl_json);

		ctrl = ctrl->next;

	}

	FILE *out = fopen(CTRL_FILE, "wb");

	if (out != NULL) {
		fputs(json_object_get_string(json), out);
		fclose(out);
	}

	json_object_put(json);
}

int set_control(v4l2_dev_t *my_vd, const char *name, int32_t value) {
	v4l2_ctrl_t *ctrl = v4l2core_get_control_list(my_vd);
	int32_t prevValue;
	int changed = 0;

	while (ctrl != NULL) {
		if (strcmp(ctrl->name, name) == 0) {
			prevValue = ctrl->value;

			if (prevValue != value) {
				printf("[CONTROL] Will update %s to %i\n", ctrl->name, value);
				ctrl->value = value;
				int result = v4l2core_set_control_value_by_id(my_vd, ctrl->control.id);

				if (result != 0) {
					printf("Set control value returned %i", result);
				}

				changed = 1;
			}
		}

		ctrl = ctrl->next;
	}

	return changed;
}

int read_controls(v4l2_dev_t *my_vd) {
	FILE *in = fopen(CTRL_FILE, "r");

	fseek(in, 0L, SEEK_END);
	uint64_t size = ftell(in) + 1;

	if (size > 500000) {
		printf("Controls file too large. Skipping.\n");
		fclose(in);
		return;
	}

	fseek(in, 0L, SEEK_SET);

	char *buffer = malloc(size);

	fgets(buffer, size, in);

	fclose(in);

	buffer[size - 1] = 0;

	json_object *json = json_tokener_parse(buffer);

	size_t ctrls_length = json_object_array_length(json);
	size_t i = 0;

	json_object *ctrl;
	char *name;
	int32_t value;
	int changed = 0;

	for (i = 0; i < ctrls_length; i++) {
		ctrl = json_object_array_get_idx(json, i);
		name = json_object_get_string(json_object_object_get(ctrl, "ctrlName"));
		value = json_object_get_int(json_object_object_get(ctrl, "ctrlValue"));
		if (set_control(my_vd, (const char*)name, value)) {
			changed = 1;
		}
	}

	json_object_put(json);
	free(buffer);

	return changed;

}

void update_controls(v4l2_dev_t *my_vd) {
  if (access(CTRL_FILE, F_OK) == 0) {
	int changed = read_controls(my_vd);

	if (changed > 0 || v4l2core_check_control_events(my_vd) > 0) {
  		write_file(my_vd);
	}
  } else {
	v4l2core_set_control_defaults(my_vd);
  	write_file(my_vd);
  }
}

