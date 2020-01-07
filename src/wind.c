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

#include "nk.h"
#include "macro.h"

#define CIRC_RAD 5

// oss settings
#define AFMT AFMT_S16_NE
#define CHNLS 1
#define RATE 48000
#define OSS_DEVNAME "/dev/dsp"
#define MICBUF_SAMPLES 2048

float micbuf[MICBUF_SAMPLES];


enum windtypes {
	WIND_MENU,

	// generators
	WIND_GEN_SIN,
	WIND_GEN_MIC,

	// filters
	WIND_FFT,
	WIND_REV_FFT,
	WIND_TEE,

	// plot
	WIND_PLOT,
};

struct connector {
	int samples;
	int bufsize;
	float *buf;
};

struct node {
	enum windtypes type;
	char *name;

	int x, y, h, w;
	int icon, ocon;

	struct connector **inp; // array pointers to input connectors
	struct connector **out; // array pointers to output connectors

	int processed;

	union {
		// sine settings
		struct {
			float step;
			int sine_samples;
		};

		// mic settings;
		struct {
			float gain;
			int mic_samples;
		};

		// plot settings
		struct {
			float minval, maxval;
		};
	};
};

struct links {
	struct node *from, *to;
	int fcon, tcon;
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


static struct links*
find_link(struct node *from, struct node *to, int fcon, int tcon)
{
	struct links *link;

	list_foreach (links, link) {
		if ((from == NULL || from == link->from) &&
		    (to   == NULL || to   == link->to  ) &&
		    (fcon == -1   || fcon == link->fcon) &&
		    (tcon == -1   || tcon == link->tcon))
			break;
	}

	return link;
}

static int
fft(float *rex, float *imx, int n)
{
	int i, ip, j, k;

	if (n <= 0 || rex == NULL || imx == NULL)
		return -1;

	for (i = 1, j = n/2; i < n - 1; ++i, j += k) {
		if (i < j) {
			SWAP(rex[i], rex[j]);
			SWAP(imx[i], imx[j]);
		}

		for (k = n/2; k <= j; j -= k, k /= 2)
			;
	}

	for (i = 1; i <= log2(n); i++) {				//Each stage
		int le;
		float si, sr, ti, tr, ui, ur;

		le = pow(2, i - 1);
		sr = cos(NK_PI / le);
		si = -sin(NK_PI / le);
		ur = 1;
		ui = 0;

		for (j = 1; j <= le; j++) {				//Each SUB DFT
			for (k = j - 1; k < n; k += 2*le) {		//Each butterfly
				ip = k + le;				//Butterfly
				tr = rex[ip] * ur - imx[ip] * ui;
				ti = rex[ip] * ui + imx[ip] * ur;

				rex[ip] = rex[k] - tr;
				imx[ip] = imx[k] - ti;

				rex[k] = rex[k] + tr;
				imx[k] = imx[k] + ti;
			}

			tr = ur;
			ur  = tr * sr - ui * si;
			ui = tr * si + ui * sr;
		}
	}

	return 0;
}

static int
rev_fft(float *rex, float *imx, int n)
{
	int i;

	fft(rex, imx, n);

	for (i = 0; i < n; ++i) {
		rex[i] /= n/2;
		imx[i] /= n/2;
	}

	return 0;
}

static void
gensin(struct node *node)
{
	int i;
	int samples = node->out[0]->samples;

	for (i = 0; i < samples; ++i)
		node->out[0]->buf[i] = sin(2.0f*NK_PI*i*node->step/samples);
}

static void
set_micbuf(void)
{
	int i;
	int samples = MICBUF_SAMPLES;
	int16_t buf[samples];

	read(oss_fd, buf, samples * sizeof (int16_t));

	for (i = 0; i < samples; ++i)
		micbuf[i] = (float)buf[i]/INT16_MAX;
}

static void
genmic(struct node *node)
{
	int i;
	int samples = node->out[0]->samples;

	for (i = 0; i < samples; ++i)
		node->out[0]->buf[i] = (float)node->gain * micbuf[i];
}

static void
tee_proc(struct node *node)
{
	size_t size;

	if (node->inp[0] == NULL)
		return;

	size = node->out[0]->samples * sizeof (float);

	memcpy(node->out[0]->buf, node->inp[0]->buf, size);
	memcpy(node->out[1]->buf, node->inp[0]->buf, size);
}

static void
fft_proc(struct node *node)
{
	int samples;
	size_t size;
	float *rex, *imx;

	if (node->inp[0] == NULL)
		return;

	samples = 2 * node->out[0]->samples;
	size = samples * sizeof (float);

	rex = alloca(size);
	imx = alloca(size);

	memcpy(rex, node->inp[0]->buf, size);
	memset(imx, 0, size);

	fft(rex, imx, samples);

	memcpy(node->out[0]->buf, rex, size/2);
	memcpy(node->out[1]->buf, imx, size/2);
}

static void
rev_fft_proc(struct node *node)
{
	size_t size;
	int samples;
	float *buf;

	if (node->inp[0] == NULL || node->inp[1] == NULL)
		return;

	samples = node->out[0]->samples;
	size = samples * sizeof (float);
	buf = alloca(size);

	memcpy(node->out[0]->buf, node->inp[0]->buf, size/2);
	memset(node->out[0]->buf + samples/2, 0, size/2);
	memcpy(buf, node->inp[1]->buf, size/2);
	memset(buf + samples/2, 0, size/2);

	rev_fft(buf, node->out[0]->buf, samples);
}

static void
signal_proc_rec(struct node *node)
{
	int i;
	struct links *link;
	int *samples;
	int bufsize;
	float *buf;
	int conn_count;

	if (node == NULL || node->processed)
		return;

	switch (node->type) {
		case WIND_MENU:
			return;

		case WIND_PLOT:
			link = find_link(NULL, node, -1, 0);
			if (link != NULL)
				return;

			signal_proc_rec(link->from);

			return;

		case WIND_FFT:
			conn_count = 2;
			samples = alloca(2 * sizeof (int));

			samples[0] = node->out[0]->samples;

			link = find_link(NULL, node, -1, 0);
			if (link != NULL) {
				signal_proc_rec(link->from);
				samples[0] = node->inp[0]->samples;
				samples[0] = pow(2, (int)log2(samples[0]))/2;
			}

			samples[1] = samples[0];

			break;

		case WIND_REV_FFT:
			conn_count = 1;
			samples = alloca(sizeof (int));

			samples[0] = node->out[0]->samples;

			link = find_link(NULL, node, -1, 0);
			if (link != NULL) {
				signal_proc_rec(link->from);
				samples[0] = node->inp[0]->samples*2;
			}

			link = find_link(NULL, node, -1, 1);
			if (link != NULL) {
				signal_proc_rec(link->from);
				samples[0] = MIN(samples[0], node->inp[1]->samples*2);
			}

			break;

		case WIND_TEE:
			conn_count = 2;
			samples = alloca(2 * sizeof (int));

			samples[0] = node->out[0]->samples;

			link = find_link(NULL, node, -1, 0);
			if (link != NULL) {
				signal_proc_rec(link->from);
				samples[0] = node->inp[0]->samples;
			}

			samples[1] = samples[0];

			break;

		case WIND_GEN_MIC:
			conn_count = 1;
			samples = alloca(sizeof (int));

			samples[0] = samples[1] = node->mic_samples;

			break;

		case WIND_GEN_SIN:
			conn_count = 1;
			samples = alloca(sizeof (int));

			samples[0] = samples[1] = node->sine_samples;

			break;
	}

	for (i = 0; i < conn_count; ++i) {
		node->out[i]->samples = samples[i];
		bufsize = node->out[i]->bufsize;
		buf = node->out[i]->buf;

		if (bufsize < samples[i]) {
			bufsize = samples[i];
			buf = xrealloc(buf, bufsize * sizeof (float));

			node->out[i]->bufsize = bufsize;
			node->out[i]->buf = buf;
		}
	}

	if (node->type == WIND_FFT)
		fft_proc(node);
	else if (node->type == WIND_REV_FFT)
		rev_fft_proc(node);
	else if (node->type == WIND_TEE)
		tee_proc(node);
	else if (node->type == WIND_GEN_MIC)
		genmic(node);
	else if (node->type == WIND_GEN_SIN)
		gensin(node);

	node->processed = 1;
}

static void
signal_proc(void)
{
	struct links *link;
	struct node *node;

	list_foreach(wind_nodes, node)
		node->processed = 0;

	list_foreach(links, link)
		if (link->to->type == WIND_PLOT)
			signal_proc_rec(link->from);
}

int
wind_init(void)
{
	int afmt, chnls, rate;
	char *devname = OSS_DEVNAME;

	wind_nodes = list_new(wind_nodes);

	wind_nodes->type = WIND_MENU;
	wind_nodes->name = "menu";

	wind_nodes->x = 0;
	wind_nodes->y = 0;
	wind_nodes->h = 400;
	wind_nodes->w = 250;

	wind_nodes->icon = 0;
	wind_nodes->ocon = 0;
	wind_nodes->inp = NULL;
	wind_nodes->out = NULL;

	nodecount = 1;

	// oss init
	oss_fd = open(devname, O_RDWR);
	if (oss_fd == -1)
		return 0;

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

static void
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
		node->sine_samples = 512;

		node->out = xmalloc(sizeof (struct connector*));
		node->out[0] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));

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
		node->mic_samples = 512;

		node->inp = NULL;
		node->out = xmalloc(sizeof (struct connector*));
		node->out[0] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));

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

		node->inp = xmalloc(sizeof (struct connector*));
		node->inp[0] = NULL;
		node->out = xmalloc(2 * sizeof (struct connector*));
		node->out[0] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));
		node->out[1] = xmalloc(sizeof (struct connector));
		memset(node->out[1], 0, sizeof (struct connector));

		++nodecount;
	}
	if (nk_button_label(ctx, "FFT")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_FFT;
		node->name = "FFT";

		node->x = node->y = 0;
		node->h = 150;
		node->w = 150;
		node->icon = 1;
		node->ocon = 2;

		node->inp = xmalloc(sizeof (struct connector*));
		node->inp[0] = NULL;
		node->out = xmalloc(2 * sizeof (struct connector*));
		node->out[0] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));
		node->out[1] = xmalloc(sizeof (struct connector));
		memset(node->out[1], 0, sizeof (struct connector));

		++nodecount;
	}
	if (nk_button_label(ctx, "Reverse FFT")) {
		node = list_alloc_at_end(wind_nodes);

		node->type = WIND_REV_FFT;
		node->name = "Reverse FFT";

		node->x = node->y = 0;
		node->h = 150;
		node->w = 150;
		node->icon = 2;
		node->ocon = 1;

		node->inp = xmalloc(2 * sizeof (struct connector*));
		node->inp[0] = NULL;

		node->out = xmalloc(sizeof (struct connector*));
		node->out[0] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));
		node->out[1] = xmalloc(sizeof (struct connector));
		memset(node->out[0], 0, sizeof (struct connector));

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

		node->inp = xmalloc(sizeof (struct connector*));
		node->inp[0] = NULL;

		node->maxval =  1.0;
		node->minval = -1.0;

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

	sprintf(text, "Samples: %d", node->sine_samples);
	nk_label(ctx, text, NK_TEXT_LEFT);
	nk_slider_int(ctx, 0, &node->sine_samples, 4096, 16);
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
	int samples;

	nk_layout_row_dynamic(ctx, 100, 1);
	if (node->inp[0] != NULL) {
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
		    nk_rgb(0xFF,0,0), nk_rgb(0,0,0), node->inp[0]->samples,
		    node->minval, node->maxval)) {
			float *buf = node->inp[0]->buf;

			for (i = 0; i < node->inp[0]->samples; ++i) {
				if (buf[i] >= node->maxval)
					nk_chart_push_slot(ctx, node->maxval, 0);
				else if (buf[i] <= node->minval)
					nk_chart_push_slot(ctx, node->minval, 0);
				else
					nk_chart_push_slot(ctx, buf[i], 0);
			}
		}

		samples = node->inp[0]->samples;
	}
	else {
		nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
		    nk_rgb(0xFF,0,0), nk_rgb(0,0,0), 0, -1, 1);

		samples = 0;
	}

	nk_layout_row_dynamic(ctx, 15, 2);
	sprintf(text, "Max value: %.2f", node->maxval);
	nk_label(ctx, text, NK_TEXT_LEFT);
	sprintf(text, "Min value: %.2f", node->minval);
	nk_label(ctx, text, NK_TEXT_LEFT);
	sprintf(text, "Samples: %d", samples);
	nk_label(ctx, text, NK_TEXT_LEFT);
	nk_slider_float(ctx, 1, &node->maxval, 40, 1);
	node->minval = -node->maxval;
}

int
wind_draw(struct nk_context *ctx)
{
	int i;
	struct node *node;
	struct nk_command_buffer *canvas;
	struct nk_rect total_space;
	size_t flags = NK_WINDOW_MOVABLE | NK_WINDOW_NO_SCROLLBAR |
	    NK_WINDOW_BORDER | NK_WINDOW_TITLE;// | NK_WINDOW_CLOSABLE;
	struct links *link;

	set_micbuf();

	canvas = nk_window_get_canvas(ctx);
	total_space = nk_window_get_content_region(ctx);
	nk_layout_space_begin(ctx, NK_STATIC, total_space.h, nodecount);

	list_foreach (wind_nodes, node) {
		struct nk_rect bounds;
		float space;
		struct nk_rect circle;
		struct nk_panel *panel;

		nk_layout_space_push(ctx, nk_rect(node->x, node->y, node->w, node->h));

		if (!nk_group_begin(ctx, node->name, flags))
			continue;

		panel = nk_window_get_panel(ctx);

		switch (node->type) {
		case WIND_MENU:
			menu_content(ctx, node);
			break;

		case WIND_TEE:
		case WIND_FFT:
		case WIND_REV_FFT:
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
				list_foreach (links, link) {
					if (link->from == node && link->fcon == i) {
						link->to->inp[link->tcon] = NULL;
						links = list_free(link);
						break;
					}
				}

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
						links = list_free(link);

				node->inp[i] = linking.node->out[linking.slot];

				link = list_alloc_at_end(links);
				links = list_get_head(link);
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
