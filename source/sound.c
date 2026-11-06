/* Short chime for mentions and private messages (ndsp). Without the DSP
 * firmware (sdmc:/3ds/dspfirm.cdc) the app just stays silent. */
#include <3ds.h>
#include <math.h>
#include <string.h>

#include "app.h"

#define RATE    22050
#define SAMPLES (RATE * 22 / 100)     /* 0.22 s */

static int       ok;
static s16      *pcm;
static ndspWaveBuf wb;

void sound_init(void)
{
	if (R_FAILED(ndspInit()))
		return;
	pcm = linearAlloc(SAMPLES * sizeof(s16));
	if (!pcm) {
		ndspExit();
		return;
	}
	/* two soft notes: E6 then A6, with a fade */
	for (int i = 0; i < SAMPLES; i++) {
		float t = (float)i / RATE;
		float f = i < SAMPLES / 2 ? 1318.5f : 1760.0f;
		int k = i % (SAMPLES / 2);
		float env = 1.0f - (float)k / (SAMPLES / 2);
		env *= env;
		float atk = k < 200 ? k / 200.0f : 1.0f;
		pcm[i] = (s16)(sinf(2 * (float)M_PI * f * t) * 9000 * env * atk);
	}
	DSP_FlushDataCache(pcm, SAMPLES * sizeof(s16));
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = { 0.8f, 0.8f };
	ndspChnSetMix(0, mix);
	ok = 1;
}

void sound_beep(void)
{
	if (!ok)
		return;
	ndspChnWaveBufClear(0);
	memset(&wb, 0, sizeof(wb));
	wb.data_vaddr = pcm;
	wb.nsamples = SAMPLES;
	ndspChnWaveBufAdd(0, &wb);
}

void sound_exit(void)
{
	if (!ok)
		return;
	ndspChnWaveBufClear(0);
	ndspExit();
	linearFree(pcm);
	ok = 0;
}
