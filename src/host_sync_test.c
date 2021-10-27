/*
 * Copyright (C) 2021 Robin Gareus <robin@gareus.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define HST_URI "http://gareus.org/oss/lv2/host_sync_test"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lv2/lv2plug.in/ns/ext/time/time.h"
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

typedef struct {
	LV2_URID atom_Object;
	LV2_URID midi_MidiEvent;
	LV2_URID atom_Float;
	LV2_URID atom_Int;
	LV2_URID atom_Long;
	LV2_URID time_Position;
	LV2_URID time_bar;
	LV2_URID time_barBeat;
	LV2_URID time_beatUnit;
	LV2_URID time_beatsPerBar;
	LV2_URID time_beatsPerMinute;
	LV2_URID time_speed;
	LV2_URID time_frame;
} HSTURIs;

typedef struct {
	/* ports */
	const LV2_Atom_Sequence* midi_in;
	LV2_Atom_Sequence*       midi_out;

	float const* audio_in;
	float*       audio_out;

	/* LV2 Output */
	LV2_Log_Log*   log;
	LV2_Log_Logger logger;

	/* LV2 Features and URIs */
	LV2_URID_Map*        map;
	LV2_Atom_Forge       forge;
	LV2_Atom_Forge_Frame frame;
	HSTURIs              uris;

	/* Host Time */
	bool    host_info;
	float   host_bpm;
	double  host_bbt;
	float   host_speed;
	int     host_div;
	int64_t host_pos;

	/* settings, config */
	double sample_rate;

	/* state */
	int64_t sample_at_cycle_start;
	double  bbt_at_cycle_start;
	int32_t hold;
	int32_t trigger_delay;
	float   trigger;

} HostSyncTest;

/* ****************************************************************************
 * Helper functions
 */

static void
map_uris (LV2_URID_Map* map, HSTURIs* uris)
{
	uris->atom_Object         = map->map (map->handle, LV2_ATOM__Object);
	uris->midi_MidiEvent      = map->map (map->handle, LV2_MIDI__MidiEvent);
	uris->time_Position       = map->map (map->handle, LV2_TIME__Position);
	uris->atom_Long           = map->map (map->handle, LV2_ATOM__Long);
	uris->atom_Int            = map->map (map->handle, LV2_ATOM__Int);
	uris->atom_Float          = map->map (map->handle, LV2_ATOM__Float);
	uris->time_bar            = map->map (map->handle, LV2_TIME__bar);
	uris->time_barBeat        = map->map (map->handle, LV2_TIME__barBeat);
	uris->time_beatUnit       = map->map (map->handle, LV2_TIME__beatUnit);
	uris->time_beatsPerBar    = map->map (map->handle, LV2_TIME__beatsPerBar);
	uris->time_beatsPerMinute = map->map (map->handle, LV2_TIME__beatsPerMinute);
	uris->time_speed          = map->map (map->handle, LV2_TIME__speed);
	uris->time_frame          = map->map (map->handle, LV2_TIME__frame);
}

/**
 * parse LV2 LV2_TIME__Position message,
 * require music-time and sample-time to be provided by the host
 */
static bool
update_position (HostSyncTest* self, const LV2_Atom_Object* obj)
{
	const HSTURIs* uris = &self->uris;

	LV2_Atom* bar   = NULL;
	LV2_Atom* beat  = NULL;
	LV2_Atom* bunit = NULL;
	LV2_Atom* bpb   = NULL;
	LV2_Atom* bpm   = NULL;
	LV2_Atom* speed = NULL;
	LV2_Atom* frame = NULL;

	lv2_atom_object_get (
	    obj,
	    uris->time_bar, &bar,
	    uris->time_barBeat, &beat,
	    uris->time_beatUnit, &bunit,
	    uris->time_beatsPerBar, &bpb,
	    uris->time_beatsPerMinute, &bpm,
	    uris->time_speed, &speed,
	    uris->time_frame, &frame,
	    NULL);

	/* clang-format off */
	if (   bpm   && bpm->type   == uris->atom_Float
	    && bpb   && bpb->type   == uris->atom_Float
	    && bar   && bar->type   == uris->atom_Long
	    && beat  && beat->type  == uris->atom_Float
	    && bunit && bunit->type == uris->atom_Int
	    && speed && speed->type == uris->atom_Float
	    && frame && frame->type == uris->atom_Long)
	/* clang-format on */
	{
		float   _bpb  = ((LV2_Atom_Float*)bpb)->body;
		int64_t _bar  = ((LV2_Atom_Long*)bar)->body;
		float   _beat = ((LV2_Atom_Float*)beat)->body;

		self->host_div   = ((LV2_Atom_Int*)bunit)->body;
		self->host_bpm   = ((LV2_Atom_Float*)bpm)->body;
		self->host_speed = ((LV2_Atom_Float*)speed)->body;
		self->host_pos   = ((LV2_Atom_Long*)frame)->body;

		self->host_bbt = _bar * _bpb + _beat; // * host_div / 4.0 // TODO map host metrum

		self->host_info = (self->host_pos >= 0);
	} else {
		self->host_info = false;
	}
	return self->host_info;
}

/**
 * enqueue a MIDI message to the output port
 */
static void
forge_midimessage (HostSyncTest*        self,
                   uint32_t             ts,
                   const uint8_t* const buffer,
                   uint32_t             size)
{
	LV2_Atom midiatom;
	midiatom.type = self->uris.midi_MidiEvent;
	midiatom.size = size;

	if (0 == lv2_atom_forge_frame_time (&self->forge, ts)) return;
	if (0 == lv2_atom_forge_raw (&self->forge, &midiatom, sizeof (LV2_Atom))) return;
	if (0 == lv2_atom_forge_raw (&self->forge, buffer, size)) return;
	lv2_atom_forge_pad (&self->forge, sizeof (LV2_Atom) + size);
}

/* ****************************************************************************/

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	HostSyncTest* self = (HostSyncTest*)calloc (1, sizeof (HostSyncTest));

	for (int i = 0; features[i]; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			self->map = (LV2_URID_Map*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_LOG__log)) {
			self->log = (LV2_Log_Log*)features[i]->data;
		}
	}

	/* Initialise logger (if map is unavailable, will fallback to printf) */
	lv2_log_logger_init (&self->logger, self->map, self->log);

	if (!self->map) {
		lv2_log_error (&self->logger, "HostSyncTest.lv2: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	lv2_atom_forge_init (&self->forge, self->map);
	map_uris (self->map, &self->uris);

	self->host_info             = false;
	self->host_bpm              = 0.0;
	self->host_bbt              = 0.0;
	self->host_speed            = 1.0;
	self->host_div              = 4;
	self->host_pos              = 0;
	self->sample_rate           = rate;
	self->sample_at_cycle_start = 0;
	self->bbt_at_cycle_start    = 0;
	self->hold                  = 0;
	self->trigger_delay         = rate * .25;
	self->trigger               = 1.f;

	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	HostSyncTest* self = (HostSyncTest*)instance;

	switch (port) {
		case 0:
			self->midi_in = (const LV2_Atom_Sequence*)data;
			break;
		case 1:
			self->midi_out = (LV2_Atom_Sequence*)data;
			break;
		case 2:
			self->audio_in = (const float*)data;
			break;
		case 3:
			self->audio_out = (float*)data;
			break;
		default:
			break;
	}
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	HostSyncTest* self = (HostSyncTest*)instance;

	/* localize host-time variables */
	int64_t  sample_at_cycle_start = self->sample_at_cycle_start;
	double   bbt_at_cycle_start    = self->bbt_at_cycle_start;
	double   beats_per_samples     = self->host_bpm * self->host_speed / (60.0 * self->sample_rate);
	uint32_t cycle_offset          = 0;

	/* forward audio */
	if (self->audio_out != self->audio_in) {
		memcpy (self->audio_out, self->audio_in, sizeof (float) * n_samples);
	}

	/* prepare MIDI output */
	const uint32_t capacity = self->midi_out->atom.size;
	lv2_atom_forge_set_buffer (&self->forge, (uint8_t*)self->midi_out, capacity);
	lv2_atom_forge_sequence_head (&self->forge, &self->frame, 0);

	/* process MIDI and host-time events */
	LV2_ATOM_SEQUENCE_FOREACH (self->midi_in, ev)
	{
		if (ev->body.type == self->uris.midi_MidiEvent) {
			const uint8_t* const data = (const uint8_t*)(ev + 1);
			const uint32_t       when = ev->time.frames - cycle_offset;
#if 1 // skip MIDI panic messages
			if (ev->body.size == 3 && 0xb == (data[0] >> 4) && (data[1] == 0x7b || data[1] == 0x79 || data[1] == 0x40)) {
				continue;
			}
#endif
			printf ("@%6ld | %4.2f MIDI ev[%d]:",
			        sample_at_cycle_start + when,
			        bbt_at_cycle_start + when * beats_per_samples,
			        ev->body.size);
			for (int i = 0; i < ev->body.size; ++i) {
				printf (" %02x", data[i]);
			}
			printf ("\n");
		} else if (ev->body.type == self->uris.atom_Object) {
			const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
			if (obj->body.otype == self->uris.time_Position) {
				if (update_position (self, obj)) {
#if 0
					printf ("HOST POS %ld -> %ld\n", sample_at_cycle_start, self->host_pos);
#endif
					sample_at_cycle_start = self->host_pos;
					bbt_at_cycle_start    = self->host_bbt;
					beats_per_samples     = self->host_bpm * self->host_speed / (60.0 * self->sample_rate);
					cycle_offset          = ev->time.frames;
				}
			}
		}
	}

	/* Forge Note-off event */
	int32_t hold = self->hold;
	if (hold >= 0 && hold < n_samples) {
		uint8_t ev[3] = { 0x80, 0x40, 0x00 };
		forge_midimessage (self, hold, ev, 3);
	}

	/* Find rising edge in audio signal, print abs position
	 * and send a note-on message
	 */
	float trigger = self->trigger;
	for (uint32_t i = 0; i < n_samples; ++i) {
		const float v = self->audio_out[i];
		--hold;
		if (trigger <= 0 && v > 0) {
			if (hold < 0) {
				const uint32_t when = i - cycle_offset;
				printf ("@%6ld | %4.2f Rising Edge [%.2f] -> %.2f\n",
				        sample_at_cycle_start + when,
				        bbt_at_cycle_start + when * beats_per_samples,
				        trigger, v);

				/* ignore any rising edges, for `trigger_delay` samples,
				 * queue note-off message after `trigger_delay`
				 */
				hold = self->trigger_delay;

				/* send note on */
				uint8_t ev[3] = { 0x90, 0x40, 0x7f };
				forge_midimessage (self, i, ev, 3);

				/* just in case a host runs with a huge (> 250ms) block-size */
				if (hold < n_samples - i) {
					ev[0] = 0x80;
					ev[2] = 0x00;
					forge_midimessage (self, hold + i, ev, 3);
				}
			}
		}
		trigger = v;
	}

	/* copy back state variables */
	self->trigger = trigger;
	self->hold    = hold >= 0 ? hold : -1;

	/* keep track of time */
	const int distance_samples = (int)floorf (self->host_speed * (float)(n_samples - cycle_offset));
#if 0
	printf ("Now: %ld speed = %f END: %ld\n", sample_at_cycle_start, self->host_speed, sample_at_cycle_start + distance_samples);
#endif
	self->bbt_at_cycle_start    = bbt_at_cycle_start + distance_samples * beats_per_samples;
	self->sample_at_cycle_start = sample_at_cycle_start + distance_samples;
}

static void
cleanup (LV2_Handle instance)
{
	free (instance);
}

static const LV2_Descriptor descriptor = {
	HST_URI,
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	NULL
};

/* clang-format off */
#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
# define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
# define LV2_SYMBOL_EXPORT __attribute__ ((visibility ("default")))
#endif
/* clang-format on */
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
		case 0:
			return &descriptor;
		default:
			return NULL;
	}
}
