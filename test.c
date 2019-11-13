#include <stdlib.h>
#include <math.h>

#define SIG_CS 4096
#define SIG_MAXVAL (+5)
#define SIG_MINVAL (-5)
#define SIG_CNTVAL 1000

#define sqr(x) ((double)(x)*(x))
#define SWAP(a, b) do {typeof (a) __tmp = a; a = b; b = __tmp;} while (0)

enum sig_types {
	NOISE,
	SIN,
	COS,
	SQR,
	SOME,
};

enum conv_types {
	CONV_HPF,	// high_pass filter
	CONV_LPF,	// low pass filter
	CONV_FD,	// first difference
};

enum fur_types {
	IM,
	RE,
	MAG,
	PHASE,
};


#define CONV_LPF_CS 41
static float conv_lpf[CONV_LPF_CS] = {
0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
-0.1, -0.1, -0.1, -0.1, 0.8,  -0.1, -0.1, -0.1, -0.1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define CONV_HPF_CS 41
static float conv_hpf[CONV_HPF_CS] = {
0,    0,    0,    0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
0.12, 0.12, 0.12, 0.12, 0.12, 0.12, 0.12, 0.12, 0.12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define CONV_FD_CS 2
static float conv_fd[CONV_FD_CS] = {1, -1};


static float signal[SIG_CS];
static int graph[SIG_CNTVAL];

static enum conv_types convtype = CONV_LPF;
static enum sig_types sigtype = NOISE;
static float sig_x;
static float sig_mult = 3.0;
static float sig_sin_step = 0.001;
static float sig_cos_step = 0.001;
static float sig_sqr_step = 0.001;
static int   sig_sqr_harm = 1;
static int rand_count = 1;
static int graph_scale = 60;
int fur_scale = SIG_CNTVAL;

static int stop;
static int wind_graph = 1;
static int wind_fourier;
static enum fur_types sigtype_fourier = MAG;

static void
setsig(void) {
	int i, j;
	double di, dj;

	if (stop)
		return;

	switch (sigtype) {
	case SIN:
		for (i = di = 0; i < SIG_CS; ++i, di += sig_sin_step)
			signal[i] = sin(di);
		break;

	case COS:
		for (i = di = 0; i < SIG_CS; ++i, di += sig_cos_step)
			signal[i] = cos(di);
		break;

	case SQR:
		memset(signal, 0, SIG_CS * sizeof (float));

		for (i = 0; i < sig_sqr_harm; ++i)
			for (j = di = 0; j < SIG_CS; ++j, di += sig_sqr_step)
				signal[j] += 4/((2*i+1)*NK_PI) * sin((2*i+1)*di);
		break;

	case SOME:
		for (i = di = dj = 0; i < SIG_CS; ++i, di += 0.1, dj += 0.01)
			signal[i] = (cos(di) + sin(1.5*di) + sin(dj))/2;
		break;

	case NOISE:
		for (i = 0; i < SIG_CS; ++i)
			for (j = signal[i] = 0; j < rand_count; ++j) {
				signal[i] += (rand() % 10000)/5000.0 - 1;
				signal[i] /= 2;
			}
		break;
	}

	for (i = 0; i < SIG_CS; ++i)
		signal[i] = signal[i] * sig_mult + sig_x;
}

static void
dft(float *signal, float *re, float *im)
{
	int i, j;

	memset(re, 0, (SIG_CS/2 + 1) * sizeof (int));
	memset(im, 0, (SIG_CS/2 + 1) * sizeof (int));

	for (i = 0; i <= SIG_CS/2; ++i) {
		for (j = 0; j < SIG_CS; ++j) {
			re[i] += signal[j] * sin(2.0*i*j*NK_PI/SIG_CS);
			im[i] += signal[j] * cos(2.0*i*j*NK_PI/SIG_CS);
		}

		im[i] *= -1;
	}
}

static int
fft(float *rex, float *imx, int n)
{
	int i, ip, j, k, m;
	int le;
	float si, sr, ti, tr, ui, ur;

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

	m = log2(n);

	for (k = 1; k <= m; k++) {					//Each stage
		le = pow(2, k - 1);
		sr = cos(M_PI / le);
		si = -sin(M_PI / le);
		ur = 1;
		ui = 0;

		for (j = 1; j <= le; j++) {				//Each SUB DFT
			for (i = j - 1; i < n; i += 2*le) {		//Each butterfly
				ip = i + le;				//Butterfly
				tr = rex[ip] * ur - imx[ip] * ui;
				ti = rex[ip] * ui + imx[ip] * ur;

				rex[ip] = rex[i] - tr;
				imx[ip] = imx[i] - ti;

				rex[i] = rex[i] + tr;
				imx[i] = imx[i] + ti;
			}

			tr = ur;
			ur  = tr * sr - ui * si;
			ui = tr * si + ui * sr;
		}
	}

	return 0;
}

int
fft_rev(float *rex, float *imx, int n)
{
	int i;

	for (i = 0; i < n; ++i)
		imx[i] = -imx[i];

	fft(rex, imx, n);

	return 0;
}

static void
test(struct nk_context *ctx)
{
	int i, j;
	char str[128];
	static nk_flags window_flags = 0;

	window_flags |= NK_WINDOW_TITLE;
	window_flags |= NK_WINDOW_BORDER;
	window_flags |= NK_WINDOW_MOVABLE;
	window_flags |= NK_WINDOW_SCALABLE;

	if (nk_begin(ctx, "signal", nk_rect(0, 0, 700, 500), window_flags)) {
		float *conv;
		int conv_cs;

		switch (convtype) {
		case CONV_HPF:
			conv = conv_hpf;
			conv_cs = CONV_HPF_CS;
			break;

		case CONV_LPF:
			conv = conv_lpf;
			conv_cs = CONV_LPF_CS;
			break;

		case CONV_FD:
			conv = conv_fd;
			conv_cs = CONV_FD_CS;
			break;
		}

		setsig();

		nk_layout_row_dynamic(ctx, 400, 1);
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
		    nk_rgb(255,0,0), nk_rgb(150,0,0), SIG_CS, SIG_MINVAL, SIG_MAXVAL)) {
			float sig[SIG_CS];

			nk_chart_add_slot_colored(ctx, NK_CHART_LINES_NO_RECT,
			    nk_rgb(0,0,255), nk_rgb(0,0,150), SIG_CS, SIG_MINVAL, SIG_MAXVAL);

			// signal
			for (i = 0; i < SIG_CS; ++i) {
				if (signal[i] <= SIG_MINVAL)
					nk_chart_push_slot(ctx, SIG_MINVAL, 0);
				else if (signal[i] >= SIG_MAXVAL)
					nk_chart_push_slot(ctx, SIG_MAXVAL, 0);
				else
					nk_chart_push_slot(ctx, (float)signal[i], 0);
			}

			// convolution
			for (i = 0; i <= SIG_CS - conv_cs; ++i) {
				float n;

				for (j = n = 0; j < conv_cs; ++j)
					n += (float)signal[i + j] * conv[conv_cs - j - 1];
				sig[i + conv_cs] = n;
			}
			for (i = 0; i < SIG_CS; ++i) {
				if (i < conv_cs || i > SIG_CS - conv_cs)
					nk_chart_push_slot(ctx, 0, 1);
				else if (sig[i] <= SIG_MINVAL)
					nk_chart_push_slot(ctx, SIG_MINVAL, 1);
				else if (sig[i] >= SIG_MAXVAL)
					nk_chart_push_slot(ctx, SIG_MAXVAL, 1);
				else
					nk_chart_push_slot(ctx, (float)sig[i], 1);
			}
		}
	}
	nk_end(ctx);

	if (wind_fourier && nk_begin(ctx, "", nk_rect(0, 500, 400, 250), window_flags)) {
		float buf[SIG_CS/2 + 1];
		float re[SIG_CS/2 + 1];
		float im[SIG_CS/2 + 1];
		float signal_re[SIG_CS];
		float signal_im[SIG_CS];

		memcpy(signal_re, signal, SIG_CS * sizeof (float));
		memset(signal_im, 0, SIG_CS * sizeof (float));
		fft(signal_re, signal_im, SIG_CS);
		memcpy(re, signal_re, (SIG_CS/2 + 1) * sizeof (float));
		memcpy(im, signal_im, (SIG_CS/2 + 1) * sizeof (float));

		if (sigtype_fourier == IM)
			memcpy(buf, im, SIG_CS/2 * sizeof (float));
		else if (sigtype_fourier == RE)
			memcpy(buf, re, SIG_CS/2 * sizeof (float));
		else if (sigtype_fourier == MAG) {
			for (i = 0; i <= SIG_CS/2; ++i)
				buf[i] = sqrt(sqr(re[i]) + sqr(im[i]));
		}
		else if (sigtype_fourier = PHASE) {
			for (i = 0; i <= SIG_CS/2; ++i) {
				if (re[i])
					buf[i] = atan(im[i]/re[i]);
				else
					if (im[i] > 0)
						buf[i] = NK_PI/2.0;
					else
						buf[i] = -NK_PI/2.0;
			}
		}

		nk_layout_row_dynamic(ctx, 200, 1);
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
		    nk_rgb(255,0,0), nk_rgb(150,0,0), SIG_CS/2 + 1, -fur_scale, fur_scale)) {
			for (i = 0; i <= SIG_CS/2; ++i) {
				if (buf[i] >= fur_scale)
					nk_chart_push_slot(ctx, (float)fur_scale, 0);
				else if (buf[i] <= -fur_scale)
					nk_chart_push_slot(ctx, (float)-fur_scale, 0);
				else
					nk_chart_push_slot(ctx, (float)buf[i], 0);
			}
		}
		nk_chart_end(ctx);

		nk_layout_row_dynamic(ctx, 20, 2);
		if (nk_option_label(ctx, "Re", sigtype_fourier == RE)) {
			sigtype_fourier = RE;
			fur_scale = SIG_CNTVAL;
		}
		if (nk_option_label(ctx, "Im", sigtype_fourier == IM)) {
			sigtype_fourier = IM;
			fur_scale = SIG_CNTVAL;
		}
		if (nk_option_label(ctx, "Mag", sigtype_fourier == MAG)) {
			sigtype_fourier = MAG;
			fur_scale = SIG_CNTVAL;
		}
		if (nk_option_label(ctx, "Phase", sigtype_fourier == PHASE)) {
			sigtype_fourier = PHASE;
			fur_scale = NK_PI;
		}
	}
	if (wind_fourier)
		nk_end(ctx);

	if (wind_graph && nk_begin(ctx, "graph", nk_rect(700, 0, 400, 250), window_flags)) {
		memset(graph, 0, SIG_CNTVAL * sizeof (int));

		nk_layout_row_dynamic(ctx, 200, 1);
		if (nk_chart_begin_colored(ctx, NK_CHART_LINES_NO_RECT,
		    nk_rgb(255,0,0), nk_rgb(150,0,0), SIG_CNTVAL, 0.0, (float)SIG_CS/graph_scale)) {
			for (i = 0; i < SIG_CS; ++i) {
				if (signal[i] <= SIG_MINVAL) {
					++graph[0];
					continue;
				}
				else if (signal[i] >= SIG_MAXVAL) {
					++graph[SIG_CNTVAL - 1];
					continue;
				}

				for (j = 0; j < SIG_CNTVAL; ++j) {
					float tmp;
					tmp = j*((float)(SIG_MAXVAL - SIG_MINVAL)/SIG_CNTVAL);

					if (tmp >= signal[i] - SIG_MINVAL) {
						++graph[j];
						break;
					}
				}
			}

			for (i = SIG_CNTVAL - 1; i >= 0; --i) {
				if (graph[i] >= SIG_CS/graph_scale)
					nk_chart_push_slot(ctx, (float)SIG_CS/graph_scale, 0);
				else
					nk_chart_push_slot(ctx, (float)graph[i], 0);
			}
		}
		nk_chart_end(ctx);
	}
	if (wind_graph)
		nk_end(ctx);

	if (nk_begin(ctx, "menu", nk_rect(700, 250, 400, 250), window_flags)) {
		nk_layout_row_dynamic(ctx, 20, 2);
		nk_label(ctx, "Окна:", NK_TEXT_LEFT);
		nk_layout_row_dynamic(ctx, 20, 2);
		nk_checkbox_label(ctx, "Частотная область", &wind_fourier);
		nk_checkbox_label(ctx, "Граф", &wind_graph);

		nk_layout_row_dynamic(ctx, 20, 1);
		nk_label(ctx, "Сигналы:", NK_TEXT_LEFT);
		nk_layout_row_dynamic(ctx, 20, 2);
		if (nk_option_label(ctx, "sin", sigtype == SIN))
			sigtype = SIN;
		if (nk_option_label(ctx, "noise", sigtype == NOISE))
			sigtype = NOISE;
		if (nk_option_label(ctx, "cos", sigtype == COS))
			sigtype = COS;
		if (nk_option_label(ctx, "sqr", sigtype == SQR))
			sigtype = SQR;
		if (nk_option_label(ctx, "some", sigtype == SOME))
			sigtype = SOME;

		nk_layout_row_dynamic(ctx, 20, 1);
		nk_label(ctx, "Свёртки:", NK_TEXT_LEFT);
		nk_layout_row_dynamic(ctx, 20, 2);
		if (nk_option_label(ctx, "ФВЧ", convtype == CONV_HPF))
			convtype = CONV_HPF;
		if (nk_option_label(ctx, "ФНЧ", convtype == CONV_LPF))
			convtype = CONV_LPF;
		if (nk_option_label(ctx, "Первая разность", convtype == CONV_FD))
			convtype = CONV_FD;

		nk_layout_row_dynamic(ctx, 20, 2);
		nk_label(ctx, "Среднее значение:", NK_TEXT_LEFT);
		nk_slider_float(ctx, SIG_MINVAL, &sig_x, SIG_MAXVAL, 0.01);
		nk_label(ctx, "Усиление:", NK_TEXT_LEFT);
		nk_slider_float(ctx, 0, &sig_mult, 10, 0.01);
		nk_label(ctx, "Масштаб графа:", NK_TEXT_LEFT);
		nk_slider_int(ctx, 1, &graph_scale, 100, 1);
		nk_checkbox_label(ctx, "Stop", &stop);
	}
	nk_end(ctx);

	if (nk_begin(ctx, "settings", nk_rect(700, 500, 400, 250), window_flags)) {
		if (sigtype == SIN || sigtype == COS) {
			float *step;

			step = (sigtype == SIN) ? &sig_sin_step : &sig_cos_step;

			nk_layout_row_dynamic(ctx, 20, 2);
			nk_label(ctx, "Частота:", NK_TEXT_LEFT);
			nk_slider_float(ctx, 0.001, step, 0.5, 0.001);
		}
		else if (sigtype == SQR) {
			nk_layout_row_dynamic(ctx, 20, 2);
			nk_label(ctx, "Частота:", NK_TEXT_LEFT);
			nk_slider_float(ctx, 0.001, &sig_sqr_step, 0.5, 0.001);
			nk_label(ctx, "Кол-во гармоник:", NK_TEXT_LEFT);
			nk_slider_int(ctx, 1, &sig_sqr_harm, 1000, 10);
		}
		else if (sigtype == NOISE) {
			nk_layout_row_dynamic(ctx, 20, 2);
			nk_label(ctx, "Rand count:", NK_TEXT_LEFT);
			nk_slider_int(ctx, 1, &rand_count, 12, 1);
		}
	}
	nk_end(ctx);
}
