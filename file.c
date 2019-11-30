#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <alloca.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "macro.h"

#define CIRC_RAD 5

// oss settings
#define AFMT AFMT_S16_NE
#define CHNLS 1
#define RATE 48000
#define OSS_DEVNAME "/dev/dsp"


enum windtypes {
	WIND_MENU,
	WIND_GEN_SIN,
	WIND_GEN_MIC,
	WIND_TEE,
	WIND_PLOT,
};

struct node {
	enum windtypes type;
	char *name;

	int x, y, h, w;
	int icon, ocon;

	// signal settings
	int samples;
	int bufsize;
	float *buf;

	union {
		// sine settings
		struct {
			float step;
		};

		// mic settings;
		struct {
			float gain;
		};

		// plot settings
		struct {
			int minval, maxval;
		};
	};

	struct list_node _list;
};

struct links {
	struct node *from, *to;
	int fcon, tcon;

	struct list_node _list;
};

struct linking {
	struct node *node;
	int slot;
};


static int nodecount;
static struct node *wind_nodes;
static struct linking linking;
static struct links *links;

static int oss_fd;


void
gensin(struct node *node)
{
	int i;

	if (node->bufsize < node->samples) {
		node->bufsize = node->samples;
		node->buf = realloc(node->buf, node->bufsize * sizeof (float));
	}

	for (i = 0; i < node->samples; ++i)
		node->buf[i] = sin(2.0*NK_PI*i*node->step/node->samples);
}

void
genmic(struct node *node)
{
	int i;
	size_t size;
	int16_t *buf;

	size = node->samples * sizeof (int16_t);
	buf = alloca(size);

	if (node->bufsize < node->samples) {
		node->bufsize = node->samples;
		node->buf = realloc(node->buf, node->bufsize * sizeof (float));
	}

	read(oss_fd, buf, size);
	for (i = 0; i < node->samples; ++i)
		node->buf[i] = (float)buf[i]/INT16_MAX;
}

// TODO
void
signal_proc(void)
{
	struct node *node;
	struct links *link;

	list_foreach (wind_nodes, node) {
		if (node->type == WIND_GEN_SIN)
			gensin(node);
		else if (node->type == WIND_GEN_MIC)
			genmic(node);
	}

	list_foreach (links, link) {
		if (link->to->bufsize < link->from->samples) {
			link->to->bufsize = link->from->samples;
			link->to->buf = xrealloc(link->to->buf, link->to->bufsize * sizeof (float));
		}

		memcpy(link->to->buf, link->from->buf, link->from->samples * sizeof (float));
		link->to->samples = link->from->samples;
	}
}

static int
node_init(void)
{
	int afmt, chnls, rate;
	char *devname = OSS_DEVNAME;

	wind_nodes = list_init(wind_nodes);

	wind_nodes->type = WIND_MENU;
	wind_nodes->name = "menu";

	wind_nodes->x = wind_nodes->y = 0;
	wind_nodes->h = 400;
	wind_nodes->w = 250;
	wind_nodes->icon = wind_nodes->ocon = 0;

	wind_nodes->samples = wind_nodes->bufsize = 0;
	wind_nodes->buf = NULL;

	nodecount = 1;

	// oss init
	oss_fd = SYSCALL(0, open, devname, O_RDWR);
	afmt = AFMT;
	chnls = CHNLS;
	rate = RATE;

	rate = SYSCALL(1, ioctl, oss_fd, SNDCTL_DSP_SPEED, &rate);
	chnls = SYSCALL(1, ioctl, oss_fd, SNDCTL_DSP_CHANNELS, &chnls);
	afmt = SYSCALL(1, ioctl, oss_fd, SNDCTL_DSP_SETFMT, &afmt);

	if (rate != 0 && rate < RATE) {
		fprintf(stderr, "Device doesn't support rate.\n");
		return 1;
	}
	if (chnls != 0 && chnls < CHNLS) {
		fprintf(stderr, "Device doesn't support %d channel(s).\n", chnls);
		return 1;
	}
	if (afmt != 0 && afmt < AFMT) {
		fprintf(stderr, "Device doesn't support format.\n");
		return 1;
	}

	return 0;
}

void
menu_content(struct nk_context *ctx, struct node *node)
{
	nk_layout_row_dynamic(ctx, 25, 1);
	nk_label(ctx, "New windows:", NK_TEXT_LEFT);

	if (nk_button_label(ctx, "Sine wave generator")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_GEN_SIN;
		node->name = "Sine wave generator";

		node->x = node->y = 0;
		node->h = 250;
		node->w = 250;
		node->icon = 0;
		node->ocon = 1;

		node->samples = 512;
		node->bufsize = 0;
		node->buf = NULL;

		node->step = 1;

		++nodecount;
	}
	if (nk_button_label(ctx, "Microphone signal")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_GEN_MIC;
		node->name = "Microphone signal";

		node->x = node->y = 0;
		node->h = 250;
		node->w = 250;
		node->icon = 0;
		node->ocon = 1;

		node->samples = 1024;
		node->bufsize = 0;
		node->buf = NULL;

		node->gain = 1.0;

		++nodecount;
	}
	if (nk_button_label(ctx, "Tee")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_TEE;
		node->name = "Tee";

		node->x = node->y = 0;
		node->h = 150;
		node->w = 150;
		node->icon = 1;
		node->ocon = 2;

		node->samples = node->bufsize = 0;
		node->buf = NULL;

		++nodecount;
	}
	if (nk_button_label(ctx, "Plot")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_PLOT;
		node->name = "Plot";

		node->x = node->y = 0;
		node->h = 250;
		node->w = 400;
		node->icon = 1;
		node->ocon = 0;

		node->samples = node->bufsize = 0;
		node->buf = NULL;
		node->maxval =  1;
		node->minval = -1;

		++nodecount;
	}
}

static void
gensin_content(struct nk_context *ctx, struct node *node)
{
	char text[512];

	nk_layout_row_dynamic(ctx, 15, 2);
	sprintf(text, "Frequency: %.2f", node->step);
	nk_label(ctx, text, NK_TEXT_LEFT);
	nk_slider_float(ctx, 0, &node->step, 10, 0.01);
	sprintf(text, "Samples: %d", node->samples);
	nk_label(ctx, text, NK_TEXT_LEFT);
	nk_slider_int(ctx, 0, &node->samples, 4096, 16);
}

static void
genmic_content(struct nk_context *ctx, struct node *node)
{
	char text[512];

	nk_layout_row_dynamic(ctx, 15, 2);
	sprintf(text, "Gain: %.2f", node->gain);
	nk_label(ctx, text, NK_TEXT_LEFT);
	nk_slider_float(ctx, 0, &node->gain, 5, 0.01);
}

static void
plot_content(struct nk_context *ctx, struct node *node)
{
	int i;
	char text[512];

	nk_layout_row_dynamic(ctx, 100, 1);
	if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
	    nk_rgb(0xFF,0,0), nk_rgb(0,0,0), node->samples,
	    node->minval, node->maxval)) {
		for (i = 0; i < node->samples; ++i)
			nk_chart_push_slot(ctx, node->buf[i], 0);
	}

	nk_layout_row_dynamic(ctx, 15, 2);
	sprintf(text, "Max value: %d", node->maxval);
	nk_label(ctx, text, NK_TEXT_LEFT);
	sprintf(text, "Min value: %d", node->minval);
	nk_label(ctx, text, NK_TEXT_LEFT);
	sprintf(text, "Samples: %d", node->samples);
	nk_label(ctx, text, NK_TEXT_LEFT);
}

static int
node_editor(struct nk_context *ctx)
{
	int i;
	struct node *node;
	struct nk_command_buffer *canvas;
	struct nk_rect total_space;
	size_t flags = NK_WINDOW_CLOSABLE | NK_WINDOW_MOVABLE |
	    NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BORDER | NK_WINDOW_TITLE;
	struct links *link;

	canvas = nk_window_get_canvas(ctx);
	total_space = nk_window_get_content_region(ctx);
	nk_layout_space_begin(ctx, NK_STATIC, total_space.h, nodecount);

	list_foreach (wind_nodes, node) {
		struct nk_rect bounds;
		struct nk_panel *panel;
		float space;
		struct nk_rect circle;

		nk_layout_space_push(ctx, nk_rect(node->x, node->y, node->w, node->h));

		if (nk_group_begin(ctx, node->name, flags)) {
			panel = nk_window_get_panel(ctx);

			switch (node->type) {
			case WIND_MENU:
				menu_content(ctx, node);
				break;

			case WIND_TEE:
				break;

			case WIND_GEN_MIC:
				genmic_content(ctx, node);
				break;

			case WIND_GEN_SIN:
				gensin_content(ctx, node);
				break;

			case WIND_PLOT:
				plot_content(ctx, node);
				break;
			}

			nk_group_end(ctx);
		}

		bounds = nk_layout_space_rect_to_local(ctx, panel->bounds);
		node->x = bounds.x;
		node->y = bounds.y;


		// output connector
		space = (float)panel->bounds.h / (node->ocon+1);

		for (i = 0; i < node->ocon; ++i) {
			circle.w = circle.h = 2*CIRC_RAD;
			circle.x = panel->bounds.x + panel->bounds.w - CIRC_RAD;
			circle.y = panel->bounds.y + space * (float)(i+1);
			nk_fill_circle(canvas, circle, nk_rgb(0x7F, 0x7F, 0x7F));

			// start linking process
			if (nk_input_has_mouse_click_down_in_rect(&ctx->input,
			    NK_BUTTON_LEFT, circle, 1)) {
				list_foreach (links, link)
					if (link->from == node && link->fcon == i)
						links = list_delete(link);

				linking.node = node;
				linking.slot = i;
			}

			// draw link while mouse pressed
			if (linking.node == node && linking.slot == i) {
				struct nk_vec2 p0, p1;

				p0 = nk_vec2(circle.x + 4, circle.y + 4);
				p1 = ctx->input.mouse.pos;

				nk_stroke_curve(canvas, p0.x, p0.y, p0.x + 50.0,
				    p0.y, p1.x - 50.0, p1.y, p1.x, p1.y, 1.0,
				    nk_rgb(0x7F, 0x7F, 0x7F));
			}
		}

		// input connector
		space = (float)panel->bounds.h / (node->icon+1);

		for (i = 0; i < node->icon; ++i) {
			circle.w = circle.h = 2*CIRC_RAD;
			circle.x = panel->bounds.x - CIRC_RAD;
			circle.y = panel->bounds.y + space * (float)(i+1);
			nk_fill_circle(canvas, circle, nk_rgb(0x7F, 0x7F, 0x7F));

			// end linking process
			if (nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT) &&
			    nk_input_is_mouse_hovering_rect(&ctx->input, circle) &&
			    linking.node != node) {
				list_foreach (links, link)
					if (link->to == node && link->tcon == i)
						links = list_delete(link);

				link = list_alloc_at_end(links);
				link->from = linking.node;
				link->fcon = linking.slot;
				link->to = node;
				link->tcon = i;

				links = (links == NULL) ? link : links;

				linking.node = NULL;
				linking.slot = 0;
			}
		}
	}

	if (linking.node && nk_input_is_mouse_released(&ctx->input, NK_BUTTON_LEFT))
		linking.node = NULL;

	// draw each link
	list_foreach (links, link) {
		float spacei, spaceo;
		size_t x, y;
		struct nk_vec2 p0, p1;

		spaceo = (float) link->from->h / (link->from->ocon + 1);
		x = link->from->x + link->from->w;
		y = CIRC_RAD + link->from->y + (float)spaceo * (link->fcon + 1);
		p0 = nk_layout_space_to_screen(ctx, nk_vec2(x, y));

		spacei = (float) link->to->h / (link->to->icon + 1);
		x = link->to->x;
		y = CIRC_RAD + link->to->y + (float)spacei * (link->tcon + 1);
		p1 = nk_layout_space_to_screen(ctx, nk_vec2(x, y));
		nk_stroke_curve(canvas, p0.x, p0.y, p0.x + 50.0, p0.y,
		    p1.x - 50.0, p1.y, p1.x, p1.y, 1.0, nk_rgb(0x7F, 0x7F, 0x7F));
	}
	nk_layout_space_end(ctx);

	signal_proc();

	return 0;
}
