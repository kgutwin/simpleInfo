#include "pebble.h"

///// DISABLE ALL DEBUGGING MESSAGES
///// COMMENT THESE OUT FOR DEBUGGING
#undef APP_LOG
#define APP_LOG(level, fmt, args...)  ;
/////

Window *window;
TextLayer *text_date_layer;
TextLayer *text_time_layer;
Layer *line_layer;

GBitmap *wx_image;
BitmapLayer *wx_image_layer;
uint8_t wx_image_layer_animate;
TextLayer *text_temp_layer;
char text_temp_layer_value[8];

GBitmap *cal_image;
BitmapLayer *cal_image_layer;
TextLayer *text_cal_info_layer;
char text_cal_info_layer_value[64];

uint8_t snooze_ticks_remain;
uint8_t snoozing;

static AppSync sync;
#define SYNC_BUFFER_SIZE 512
static uint8_t sync_buffer[SYNC_BUFFER_SIZE];
	
enum messages_e {
	MSG_WXCURTEMP = 11,
	MSG_WXCURICON = 12,
	MSG_WXCURALERTS = 13,
	MSG_WXGET = 14,
	
	MSG_CALCURTEXT = 21,
	MSG_CALCURICON = 22,
	MSG_CALCURSTART = 23,
	MSG_CALGET = 24,
};

void request_update(uint8_t wx, uint8_t cal);

uint8_t wx_image_map[] = {
	RESOURCE_ID_IMAGE_WX_UNKNOWN,
	RESOURCE_ID_IMAGE_WX_CLEAR_DAY,
	RESOURCE_ID_IMAGE_WX_CLEAR_NIGHT,
	RESOURCE_ID_IMAGE_WX_RAIN,
	RESOURCE_ID_IMAGE_WX_SNOW,
	RESOURCE_ID_IMAGE_WX_SLEET,
	RESOURCE_ID_IMAGE_WX_WIND,
	RESOURCE_ID_IMAGE_WX_FOG,
	RESOURCE_ID_IMAGE_WX_CLOUDY,
	RESOURCE_ID_IMAGE_WX_PARTLY_CLOUDY_DAY,
	RESOURCE_ID_IMAGE_WX_PARTLY_CLOUDY_NIGHT
};

uint8_t cal_image_map[] = {
	RESOURCE_ID_IMAGE_CAL_MEETING,
	RESOURCE_ID_IMAGE_CAL_LYNC
};

// Originally a boring line drawing function, this now draws two rectangles. 
// The first is a single pixel high but spans the width of the screen. The
// second is two pixels high and the width is defined by the battery charge.
void line_layer_update_callback(Layer *layer, GContext* ctx) {
	graphics_context_set_fill_color(ctx, GColorWhite);
	GRect bounds = layer_get_bounds(layer);
	BatteryChargeState bat = battery_state_service_peek();
	// draw half height bar
	bounds.size.h = 1;
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);
	// draw full height bar
	bounds.size.h = 2;
	bounds.size.w = (140 * bat.charge_percent) / 100;
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

// Update time and date text layers, and request data feed updates if time
// cycles require.
void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
	if (tick_time == NULL) {
		time_t now = time(NULL);
		tick_time = localtime(&now);
	}
	
	// Need to be static because they're used by the system later.
	static char time_text[] = "00:00";
	static char date_text[] = "Xxxxxxxxx 00";

	char *time_format;

	if ((tick_time->tm_hour == 0 && 
		 tick_time->tm_min == 0 && 
		 tick_time->tm_sec <= 5) ||
		units_changed == YEAR_UNIT) {
		strftime(date_text, sizeof(date_text), "%B %e", tick_time);
		text_layer_set_text(text_date_layer, date_text);
	}

	if (clock_is_24h_style()) {
		time_format = "%R";
	} else {
		time_format = "%I:%M";
	}
	
	strftime(time_text, sizeof(time_text), time_format, tick_time);

	// Kludge to handle lack of non-padded hour format string
	// for twelve hour clock.
	if (!clock_is_24h_style() && (time_text[0] == '0')) {
		memmove(time_text, &time_text[1], sizeof(time_text) - 1);
	}

	text_layer_set_text(text_time_layer, time_text);

	if (snoozing) {
		// request calendar and weather updates hourly
		if (tick_time->tm_min == 0) {
			request_update(true, true);
		}
	} else {
		// request calendar updates on the 15 minute mark
		if ((tick_time->tm_min % 15) == 0) {
			request_update(false, true);
		}
		// request weather updates on the two minute mark
		if ((tick_time->tm_min % 2) == 0) {
			if (layer_get_hidden(bitmap_layer_get_layer(cal_image_layer)))
				request_update(true, false);
		}
	}
}

// Used in handle_second_tick, for determining when to trigger handle_minute_tick()
static int last_minute = -1;

// Perform icon animations (if needed), trigger handle_minute_tick() when
// appropriate, and transition in and out of snooze.
// Note that during snoozing, even though the period changes to per-minute,
// the timer service still calls this function, so that snooze transitions
// are still handled.
void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
	if (wx_image_layer_animate) {
		wx_image->bounds.origin.x = (tick_time->tm_sec % 10) * 40;
		bitmap_layer_set_bitmap(wx_image_layer, wx_image);
		Layer *wx_image_layer_layer = bitmap_layer_get_layer(wx_image_layer);
		layer_mark_dirty(wx_image_layer_layer);
	}
	
	if (tick_time->tm_min + (tick_time->tm_hour * 60) != last_minute) {
		last_minute = tick_time->tm_min + (tick_time->tm_hour * 60);
		handle_minute_tick(tick_time, units_changed);
	}
	
	// handle snooze
	if (snooze_ticks_remain == 0) {
		// Snooze transitions that happen long after we're out of snooze ticks.
		if (tick_time->tm_min == 0 && tick_time->tm_sec <= 1) {
			if (tick_time->tm_hour == 0) {
				// Transition to snooze after midnight.
				snoozing = true;
				tick_timer_service_unsubscribe();
				tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
			} else if (tick_time->tm_hour == 6) {
				// Transition out of snooze at 6:00 AM.
				snoozing = false;
				tick_timer_service_unsubscribe();
				tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
			}
		}
	} else if (snooze_ticks_remain == 1) {
		// If we're between midnight and 6AM and we're just about to run out of
		// snooze ticks, then go ahead and transition immediately to snooze.
		if (tick_time->tm_hour < 6) {
			snoozing = true;
			tick_timer_service_unsubscribe();
			tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
		}
		snooze_ticks_remain--;
	} else {
		// Tick tick tick...
		snooze_ticks_remain--;
	}
}

// Called on app clean shutdown, frees all known allocated memory.
void handle_deinit(void) {
	tick_timer_service_unsubscribe();
	gbitmap_destroy(wx_image);
	bitmap_layer_destroy(wx_image_layer);
	text_layer_destroy(text_temp_layer);
	text_layer_destroy(text_date_layer);
	text_layer_destroy(text_time_layer);
	gbitmap_destroy(cal_image);
	bitmap_layer_destroy(cal_image_layer);
	text_layer_destroy(text_cal_info_layer);
	layer_destroy(line_layer);
	window_destroy(window);
}

// Window and layer setup.
void handle_init(void) {
	window = window_create();
	window_stack_push(window, true /* Animated */);
	window_set_background_color(window, GColorBlack);

	Layer *window_layer = window_get_root_layer(window);

	// Date string (January 29)
	text_date_layer = text_layer_create(GRect(8, 68, 144-8, 168-68));
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	text_layer_set_font(text_date_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
	layer_add_child(window_layer, text_layer_get_layer(text_date_layer));

	// Time string (10:49)
	text_time_layer = text_layer_create(GRect(7, 92, 144-7, 168-92));
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
	text_layer_set_font(text_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
	layer_add_child(window_layer, text_layer_get_layer(text_time_layer));

	// Line separator/battery meter
	GRect line_frame = GRect(8, 97, 139, 2);
	line_layer = layer_create(line_frame);
	layer_set_update_proc(line_layer, line_layer_update_callback);
	layer_add_child(window_layer, line_layer);

	// Weather icon
	wx_image_layer = bitmap_layer_create(GRect(8, 18, 40, 40));
	bitmap_layer_set_alignment(wx_image_layer, GAlignCenter);
	layer_add_child(window_layer, bitmap_layer_get_layer(wx_image_layer));
	
	// Weather temperature text
	text_temp_layer = text_layer_create(GRect(56, 18, 144-56, 40));
	text_layer_set_text_color(text_temp_layer, GColorWhite);
	text_layer_set_background_color(text_temp_layer, GColorClear);
	text_layer_set_font(text_temp_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
	layer_add_child(window_layer, text_layer_get_layer(text_temp_layer));
	
	// Calendar icon (starts hidden)
	cal_image_layer = bitmap_layer_create(GRect(8, 18, 40, 40));
	bitmap_layer_set_alignment(cal_image_layer, GAlignCenter);
	layer_set_hidden(bitmap_layer_get_layer(cal_image_layer), true);
	layer_add_child(window_layer, bitmap_layer_get_layer(cal_image_layer));
	
	// Calendar text (starts empty)
	text_cal_info_layer = text_layer_create(GRect(58, 4, 300, 60));
	text_layer_set_text_color(text_cal_info_layer, GColorWhite);
	text_layer_set_background_color(text_cal_info_layer, GColorClear);
	text_layer_set_font(text_cal_info_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_add_child(window_layer, text_layer_get_layer(text_cal_info_layer));
	
	// Preset snooze settings (tick 30 times before any snooze is initiated)
	// and subscribe to the timer service
	snooze_ticks_remain = 30;
	snoozing = false;
	tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);

	// Draw the time immediately
	handle_minute_tick(NULL, YEAR_UNIT);
}

// Send a message to the javascript code, either requesting a weather or a 
// calendar update.
void request_update(uint8_t wx, uint8_t cal) {
	Tuplet wx_tuple = TupletInteger(MSG_WXGET, 1);
	Tuplet cal_tuple = TupletInteger(MSG_CALGET, 1);
        
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);

	if (iter == NULL) return;

	if (wx) dict_write_tuplet(iter, &wx_tuple);
	if (cal) dict_write_tuplet(iter, &cal_tuple);
	dict_write_end(iter);

	app_message_outbox_send();
}

// Compare two arbitrary tuples, returning zero if equal, and either positive
// or negative 1 depending on if the first is "greater" than the second.
#define THREE_COMP(x,y) if((x)>(y))return 1;if((x)<(y))return -1;
int8_t tuple_compare(const Tuple* a, const Tuple* b) {
	THREE_COMP(a->key, b->key)
	THREE_COMP(a->length, b->length)
	THREE_COMP(a->type, b->type)
	switch (a->type) {
		case TUPLE_CSTRING:
			return strcmp(a->value->cstring, b->value->cstring);
		case TUPLE_BYTE_ARRAY:
			return memcmp(a->value->data, b->value->data, a->length);
		case TUPLE_INT:
			if (a->length == 4) {
				THREE_COMP(a->value->int32, b->value->int32)
			} else if (a->length == 2) {
				THREE_COMP(a->value->int16, b->value->int16)
			} else if (a->length == 1) {
				THREE_COMP(a->value->int8, b->value->int8)
			}
			break;
		case TUPLE_UINT:
			if (a->length == 4) {
				THREE_COMP(a->value->uint32, b->value->uint32)
			} else if (a->length == 2) {
				THREE_COMP(a->value->uint16, b->value->uint16)
			} else if (a->length == 1) {
				THREE_COMP(a->value->uint8, b->value->uint8)
			}
			break;
	}
	return 0;
}

// Send a log message containing the content of the tuple.
void tuple_log(const Tuple* t) {
	switch (t->type) {
		case TUPLE_CSTRING:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu cstring:'%s'", t->key, t->value->cstring);
			break;
		case TUPLE_BYTE_ARRAY:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu bytearray len:%d", t->key, t->length);
			break;
		case TUPLE_INT:
			if (t->length == 4) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int32:%ld", t->key, t->value->int32); }
			if (t->length == 2) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int16:%d", t->key, t->value->int16); }
			if (t->length == 1) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int8:%d", t->key, t->value->int8); }
			break;
		case TUPLE_UINT:
			if (t->length == 4) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int32:%lu", t->key, t->value->uint32); }
			if (t->length == 2) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int16:%u", t->key, t->value->uint16); }
			if (t->length == 1) { APP_LOG(APP_LOG_LEVEL_DEBUG, "key:%lu int8:%u", t->key, t->value->uint8); }
			break;
	}
}

void sync_buffer_check() {
	// doesn't really work :(
	DictionaryIterator *iter = NULL;
	
	Tuple *t = dict_read_begin_from_buffer(iter, sync_buffer, sizeof(sync_buffer));
	if (t == NULL) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "dict_read_begin_from_buffer failed!");
	} else {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "sync_buffer: %lu of %u", dict_size(iter), sizeof(sync_buffer));
	}
}

// This is run both on initial AppSync startup as well as any time a message
// is received with new tuple data.
static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
	// Frequently this function is called when the tuple value hasn't really
	// changed. We skip this situation (if this isn't initialization time)
	// so that we properly transition in and out of calendar mode.
	if (old_tuple != NULL && tuple_compare(new_tuple, old_tuple) == 0)
		return;
	
	tuple_log(new_tuple);
	
	switch (key) {
		case MSG_WXCURICON: // Weather icon update
			if (wx_image) gbitmap_destroy(wx_image);
			wx_image = gbitmap_create_with_resource(wx_image_map[new_tuple->value->uint8]);
			wx_image->bounds.origin.x = 0;
			wx_image->bounds.size.w = 40;
			bitmap_layer_set_bitmap(wx_image_layer, wx_image);
			wx_image_layer_animate = (new_tuple->value->uint8 > 0);	
			break;
		
		case MSG_WXCURTEMP: // Weather temperature update
			strcpy(text_temp_layer_value, new_tuple->value->cstring);
			text_layer_set_text(text_temp_layer, text_temp_layer_value);
			break;
		
		case MSG_WXCURALERTS: // Weather alerts count update
			// currently ignored
			break;
		
		case MSG_CALCURICON: // Calendar icon update
			if (new_tuple->value->int8 >= 0) {
				// positive values mean icon is set
				APP_LOG(APP_LOG_LEVEL_DEBUG, "enabling calendar icon");
				if (cal_image) gbitmap_destroy(cal_image);
				cal_image = gbitmap_create_with_resource(cal_image_map[new_tuple->value->int8]);
				bitmap_layer_set_bitmap(cal_image_layer, cal_image);
				// hide the weather icon and show the calendar icon
				layer_set_hidden(bitmap_layer_get_layer(cal_image_layer), false);
				layer_set_hidden(bitmap_layer_get_layer(wx_image_layer), true);
			} else {
				// negative values means icon is disabled
				layer_set_hidden(bitmap_layer_get_layer(cal_image_layer), true);
				layer_set_hidden(bitmap_layer_get_layer(wx_image_layer), false);
			}
			break;
		
		case MSG_CALCURTEXT: // Calendar text update
			strcpy(text_cal_info_layer_value, new_tuple->value->cstring);
			text_layer_set_text(text_cal_info_layer, text_cal_info_layer_value);
			if (strcmp("", new_tuple->value->cstring) == 0) {
				// if there's text to be seen, hide the weather temperature layer
				layer_set_hidden(text_layer_get_layer(text_cal_info_layer), true);
				layer_set_hidden(text_layer_get_layer(text_temp_layer), false);
			} else {
				layer_set_hidden(text_layer_get_layer(text_cal_info_layer), false);
				layer_set_hidden(text_layer_get_layer(text_temp_layer), true);
			}
			break;
		
		case MSG_CALCURSTART: // Calendar start time update
			// currently ignored
			break;
	}
}

// Called in case of problems with AppMessage.
static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}

void request_initial(void* nothing) {
	request_update(true, true);
}

// Set up app sync, defining initial values for data parameters
void app_message_init() {
	Tuplet initial_values[] = {
		TupletInteger(MSG_WXCURICON, 0),
		TupletCString(MSG_WXCURTEMP, " --°"),
		TupletInteger(MSG_WXCURALERTS, 0),
		TupletInteger(MSG_CALCURICON, -1),
		TupletCString(MSG_CALCURTEXT, ""),
		TupletInteger(MSG_CALCURSTART, 0)
	};
	
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values), 
		sync_tuple_changed_callback, sync_error_callback, NULL);
	// init buffers
	app_message_open(sizeof(sync_buffer), sizeof(sync_buffer));
	// send initial weather request
	app_timer_register(1000, request_initial, NULL);
	//request_update(true, true);
}

// Tear down app sync
void app_message_deinit() {
	app_sync_deinit(&sync);
}

// Main entry point.
int main(void) {
	handle_init();
	app_message_init();

	app_event_loop();
  
	app_message_deinit();
	handle_deinit();
}
