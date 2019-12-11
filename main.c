#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <time.h>

#include <allegro5/allegro.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_ALLEGRO5_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_allegro5.h"
#include "style.c"
#include "wind.c"


#define UNUSED(a) (void)a
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))
#define LEN(a) (sizeof(a)/sizeof(a)[0])

#define WINDOW_WIDTH 1200
#define WINDOW_HEIGHT 800
#define FONTFILE "fonts/Roboto-Regular.ttf"

int
main(void)
{
	ALLEGRO_DISPLAY *display = NULL;
	ALLEGRO_EVENT_QUEUE *event_queue = NULL;
	NkAllegro5Font *font;
	struct nk_context *ctx;

	node_init();

	if (!al_init()) {
		fprintf(stderr, "failed to initialize allegro5!\n");
		exit(1);
	}

	al_install_mouse();
	al_set_mouse_wheel_precision(150);
	al_install_keyboard();

	al_set_new_display_flags(ALLEGRO_FULLSCREEN_WINDOW|ALLEGRO_OPENGL);
	al_set_new_display_option(ALLEGRO_SAMPLE_BUFFERS, 1, ALLEGRO_SUGGEST);
	al_set_new_display_option(ALLEGRO_SAMPLES, 8, ALLEGRO_SUGGEST);
	display = al_create_display(WINDOW_WIDTH, WINDOW_HEIGHT);
	if (!display) {
		fprintf(stderr, "failed to create display!\n");
		exit(1);
	}

	event_queue = al_create_event_queue();
	if (!event_queue) {
		fprintf(stderr, "failed to create event_queue!\n");
		al_destroy_display(display);
		exit(1);
	}

	al_register_event_source(event_queue, al_get_display_event_source(display));
	al_register_event_source(event_queue, al_get_mouse_event_source());
	al_register_event_source(event_queue, al_get_keyboard_event_source());

	font = nk_allegro5_font_create_from_file(FONTFILE, 12, 0);
	assert(font && "create font from file error");

	ctx = nk_allegro5_init(font, display, WINDOW_WIDTH, WINDOW_HEIGHT);

	set_style(ctx, THEME_DARK);

	while(1) {
		ALLEGRO_EVENT ev;
		ALLEGRO_TIMEOUT timeout;
		ALLEGRO_MONITOR_INFO info;
		bool get_event;

		al_init_timeout(&timeout, 0.0);
		get_event = al_wait_for_event_until(event_queue, &ev, &timeout);

		if (get_event && ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE)
			break;

		nk_input_begin(ctx);
		for (; get_event; get_event = al_get_next_event(event_queue, &ev))
			nk_allegro5_handle_event(&ev);
		nk_input_end(ctx);

		al_get_monitor_info(0, &info);

		if (nk_begin(ctx, "Hack window", nk_rect(0, 0, info.x2-info.x1,
		    info.y2-info.y1), NK_WINDOW_NO_SCROLLBAR))
			node_editor(ctx);
		nk_end(ctx);

		/* Draw */
		al_clear_to_color(al_map_rgb(19, 43, 81));
		nk_allegro5_render();
		al_flip_display();
	}

	nk_allegro5_font_del(font);
	nk_allegro5_shutdown();
	al_destroy_display(display);
	al_destroy_event_queue(event_queue);

	return 0;
}
