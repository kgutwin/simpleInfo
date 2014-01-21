#include "pebble.h"

Window *window;
TextLayer *text_date_layer;
TextLayer *text_time_layer;
Layer *line_layer;

GBitmap *wx_image;
BitmapLayer *wx_image_layer;
TextLayer *text_temp_layer;

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
	int frame = tick_time->tm_sec % 10;
	
	wx_image->bounds.origin.x = frame * 40;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "Bounds: %d %d %d %d", wx_image->bounds.origin.x, wx_image->bounds.origin.y, 
	//		wx_image->bounds.size.w, wx_image->bounds.size.h);
	bitmap_layer_set_bitmap(wx_image_layer, wx_image);
	Layer *wx_image_layer_layer = bitmap_layer_get_layer(wx_image_layer);
	layer_mark_dirty(wx_image_layer_layer);
	
	if (tick_time->tm_sec <= 1) {
		handle_minute_tick(tick_time, units_changed);
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

	wx_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WX_SNOW);
	wx_image_layer = bitmap_layer_create(GRect(8, 18, 40, 40));
	wx_image->bounds.origin.x = 0;
	wx_image->bounds.size.w = 40;
	//APP_LOG(APP_LOG_LEVEL_DEBUG, "Bounds: %d %d %d %d", wx_image->bounds.origin.x, wx_image->bounds.origin.y, 
	//		wx_image->bounds.size.w, wx_image->bounds.size.h);
	bitmap_layer_set_bitmap(wx_image_layer, wx_image);
	bitmap_layer_set_alignment(wx_image_layer, GAlignCenter);
	layer_add_child(window_layer, bitmap_layer_get_layer(wx_image_layer));
	
	text_temp_layer = text_layer_create(GRect(58, 18, 144-58, 40));
	text_layer_set_text_color(text_temp_layer, GColorWhite);
	text_layer_set_background_color(text_temp_layer, GColorClear);
	text_layer_set_font(text_temp_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
	text_layer_set_text(text_temp_layer, "31°");
	layer_add_child(window_layer, text_layer_get_layer(text_temp_layer));
	
	tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);

	handle_minute_tick(NULL, YEAR_UNIT);
}


int main(void) {
	handle_init();

	app_event_loop();
  
	handle_deinit();
}
