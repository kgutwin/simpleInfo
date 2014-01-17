#include "pebble.h"

Window *window;
TextLayer *text_date_layer;
TextLayer *text_time_layer;
Layer *line_layer;

GBitmap *wx_image;
BitmapLayer *wx_image_layer;

void line_layer_update_callback(Layer *layer, GContext* ctx) {
	graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed) {
	// Need to be static because they're used by the system later.
	static char time_text[] = "00:00";
	static char date_text[] = "Xxxxxxxxx 00";

	char *time_format;

	// TODO: Only update the date when it's changed.
	strftime(date_text, sizeof(date_text), "%B %e", tick_time);
	text_layer_set_text(text_date_layer, date_text);

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
	
	Layer *wx_image_layer_layer = bitmap_layer_get_layer(wx_image_layer);
	GRect bounds = layer_get_bounds(wx_image_layer_layer);
	bounds.origin.x = frame * -40;
	// this seems to be necessary in order to keep the window the right
	// size. However, it stops displaying the image midway through the
	// animation...
	bounds.size.w = 40 - (bounds.origin.x * 2);
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Bounds: %d %d %d %d", bounds.origin.x, bounds.origin.y, bounds.size.w, bounds.size.h);
	layer_set_bounds(wx_image_layer_layer, bounds);
	
	// need to figure out how to trigger this less often
	//if (tick_time->tm_sec == 0) {
		handle_minute_tick(tick_time, units_changed);
	//}
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

	wx_image = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WX_SUN);
	wx_image_layer = bitmap_layer_create(GRect(8, 8, 400, 40));
	bitmap_layer_set_bitmap(wx_image_layer, wx_image);
	bitmap_layer_set_alignment(wx_image_layer, GAlignTopLeft);
	layer_add_child(window_layer, bitmap_layer_get_layer(wx_image_layer));
	
	//tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
	tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
	// TODO: Update display here to avoid blank display on launch?
}


int main(void) {
	handle_init();

	app_event_loop();
  
	handle_deinit();
}
