#include "pebble.h"

Window *window;
TextLayer *text_date_layer;
TextLayer *text_time_layer;
Layer *line_layer;

GBitmap *wx_image;
BitmapLayer *wx_image_layer;
uint8_t wx_image_layer_animate;
TextLayer *text_temp_layer;

uint8_t snooze_ticks_remain;

static AppSync sync;
static uint8_t sync_buffer[64];

AppTimer *resync_timer;
#define RESYNC_TIMEOUT_MS (10 * 60 * 1000)

enum {
	MSG_WXCURTEMP = 1,
	MSG_WXCURICON = 2,
	MSG_WXCURALERTS = 3,
	MSG_WXGET = 4,
};

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
}

void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
	if (wx_image_layer_animate) {
		wx_image->bounds.origin.x = (tick_time->tm_sec % 10) * 40;
		bitmap_layer_set_bitmap(wx_image_layer, wx_image);
		Layer *wx_image_layer_layer = bitmap_layer_get_layer(wx_image_layer);
		layer_mark_dirty(wx_image_layer_layer);
	}
	
	if (tick_time->tm_sec <= 1) {
		handle_minute_tick(tick_time, units_changed);
	}
	
	// handle snooze
	if (snooze_ticks_remain == 0) {
		if (tick_time->tm_min == 0 && tick_time->tm_sec <= 1) {
			if (tick_time->tm_hour == 0) {
				tick_timer_service_unsubscribe();
				tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
			} else if (tick_time->tm_hour == 6) {
				tick_timer_service_unsubscribe();
				tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
			}
		}
	} else if (snooze_ticks_remain == 1) {
		if (tick_time->tm_hour < 6) {
			tick_timer_service_unsubscribe();
			tick_timer_service_subscribe(MINUTE_UNIT, handle_second_tick);
		}
		snooze_ticks_remain--;
	} else {
		snooze_ticks_remain--;
	}
}

void handle_deinit(void) {
	tick_timer_service_unsubscribe();
	gbitmap_destroy(wx_image);
	bitmap_layer_destroy(wx_image_layer);
	text_layer_destroy(text_date_layer);
	text_layer_destroy(text_time_layer);
	layer_destroy(line_layer);
	window_destroy(window);
}

void handle_init(void) {
	window = window_create();
	window_stack_push(window, true /* Animated */);
	window_set_background_color(window, GColorBlack);

	Layer *window_layer = window_get_root_layer(window);

	text_date_layer = text_layer_create(GRect(8, 68, 144-8, 168-68));
	text_layer_set_text_color(text_date_layer, GColorWhite);
	text_layer_set_background_color(text_date_layer, GColorClear);
	text_layer_set_font(text_date_layer, fonts_get_system_font(FONT_KEY_ROBOTO_CONDENSED_21));
	layer_add_child(window_layer, text_layer_get_layer(text_date_layer));

	text_time_layer = text_layer_create(GRect(7, 92, 144-7, 168-92));
	text_layer_set_text_color(text_time_layer, GColorWhite);
	text_layer_set_background_color(text_time_layer, GColorClear);
	text_layer_set_font(text_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
	layer_add_child(window_layer, text_layer_get_layer(text_time_layer));

	GRect line_frame = GRect(8, 97, 139, 2);
	line_layer = layer_create(line_frame);
	layer_set_update_proc(line_layer, line_layer_update_callback);
	layer_add_child(window_layer, line_layer);

	wx_image_layer = bitmap_layer_create(GRect(8, 18, 40, 40));
	bitmap_layer_set_alignment(wx_image_layer, GAlignCenter);
	layer_add_child(window_layer, bitmap_layer_get_layer(wx_image_layer));
	
	text_temp_layer = text_layer_create(GRect(58, 18, 144-58, 40));
	text_layer_set_text_color(text_temp_layer, GColorWhite);
	text_layer_set_background_color(text_temp_layer, GColorClear);
	text_layer_set_font(text_temp_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
	layer_add_child(window_layer, text_layer_get_layer(text_temp_layer));
	
	snooze_ticks_remain = 30;
	tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);

	handle_minute_tick(NULL, YEAR_UNIT);
}

void request_wx_update() {
	Tuplet command_tuple = TupletInteger(MSG_WXGET, 1);
        
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);

	if (iter == NULL) return;

	dict_write_tuplet(iter, &command_tuple);
	dict_write_end(iter);

	app_message_outbox_send();
}

void resync_timer_handler(void *data) {
	request_wx_update();
}

static void sync_tuple_changed_callback(const uint32_t key, const Tuple* new_tuple, const Tuple* old_tuple, void* context) {
	app_timer_reschedule(resync_timer, RESYNC_TIMEOUT_MS);
	switch (key) {
		case MSG_WXCURICON:
			if (wx_image) gbitmap_destroy(wx_image);
			wx_image = gbitmap_create_with_resource(wx_image_map[new_tuple->value->uint8]);
			wx_image->bounds.origin.x = 0;
			wx_image->bounds.size.w = 40;
			bitmap_layer_set_bitmap(wx_image_layer, wx_image);
			wx_image_layer_animate = (new_tuple->value->uint8 > 0);	
			break;
		
		case MSG_WXCURTEMP:
			text_layer_set_text(text_temp_layer, new_tuple->value->cstring);
			break;
	}
}

static void sync_error_callback(DictionaryResult dict_error, AppMessageResult app_message_error, void *context) {
	APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Sync Error: %d", app_message_error);
}


void app_message_init() {
	Tuplet initial_values[] = {
		TupletInteger(MSG_WXCURICON, 0),
		TupletCString(MSG_WXCURTEMP, "--°"),
		TupletInteger(MSG_WXCURALERTS, 0)
	};
	
	app_sync_init(&sync, sync_buffer, sizeof(sync_buffer), initial_values, ARRAY_LENGTH(initial_values), 
		sync_tuple_changed_callback, sync_error_callback, NULL);
	// init buffers
	app_message_open(64, 64);
	// send initial weather request
	request_wx_update();
	resync_timer = app_timer_register(RESYNC_TIMEOUT_MS, resync_timer_handler, NULL);
}

void app_message_deinit() {
	app_sync_deinit(&sync);
}

int main(void) {
	handle_init();
	app_message_init();

	app_event_loop();
  
	app_message_deinit();
	handle_deinit();
}
