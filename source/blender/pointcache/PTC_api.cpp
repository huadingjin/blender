/*
 * Copyright 2013, Blender Foundation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "PTC_api.h"

#ifdef WITH_ALEMBIC

#include "util/util_error_handler.h"

#include "reader.h"
#include "writer.h"
#include "export.h"

extern "C" {
#include "BLI_math.h"

#include "DNA_modifier_types.h"
#include "DNA_pointcache_types.h"

#include "BKE_modifier.h"
#include "BKE_report.h"

#include "RNA_access.h"
}

using namespace PTC;

void PTC_error_handler_std(void)
{
	ErrorHandler::clear_default_handler();
}

void PTC_error_handler_callback(PTCErrorCallback cb, void *userdata)
{
	ErrorHandler::set_default_handler(new CallbackErrorHandler(cb, userdata));
}

static ReportType report_type_from_error_level(PTCErrorLevel level)
{
	switch (level) {
		case PTC_ERROR_INFO:        return RPT_INFO;
		case PTC_ERROR_WARNING:     return RPT_WARNING;
		case PTC_ERROR_CRITICAL:    return RPT_ERROR;
	}
	return RPT_ERROR;
}

static void error_handler_reports_cb(void *vreports, PTCErrorLevel level, const char *message)
{
	ReportList *reports = (ReportList *)vreports;
	
	BKE_report(reports, report_type_from_error_level(level), message);
}

void PTC_error_handler_reports(struct ReportList *reports)
{
	ErrorHandler::set_default_handler(new CallbackErrorHandler(error_handler_reports_cb, reports));
}

static void error_handler_modifier_cb(void *vmd, PTCErrorLevel UNUSED(level), const char *message)
{
	ModifierData *md = (ModifierData *)vmd;
	
	modifier_setError(md, "%s", message);
}

void PTC_error_handler_modifier(struct ModifierData *md)
{
	ErrorHandler::set_default_handler(new CallbackErrorHandler(error_handler_modifier_cb, md));
}


void PTC_validate(PointCache *cache, int framenr)
{
	if (cache) {
		cache->state.simframe = framenr;
	}
}

void PTC_invalidate(PointCache *cache)
{
	if (cache) {
		cache->state.simframe = 0;
		cache->state.last_exact = min_ii(cache->startframe, 0);
	}
}


void PTC_writer_free(PTCWriter *_writer)
{
	PTC::Writer *writer = (PTC::Writer *)_writer;
	delete writer;
}

void PTC_write_sample(struct PTCWriter *_writer)
{
	PTC::Writer *writer = (PTC::Writer *)_writer;
	writer->write_sample();
}

void PTC_bake(struct Main *bmain, struct Scene *scene, struct EvaluationContext *evalctx, struct PTCWriter *_writer, int start_frame, int end_frame,
              short *stop, short *do_update, float *progress)
{
	PTC::Writer *writer = (PTC::Writer *)_writer;
	PTC::Exporter exporter(bmain, scene, evalctx, stop, do_update, progress);
	exporter.bake(writer, start_frame, end_frame);
}


void PTC_reader_free(PTCReader *_reader)
{
	PTC::Reader *reader = (PTC::Reader *)_reader;
	delete reader;
}

void PTC_reader_get_frame_range(PTCReader *_reader, int *start_frame, int *end_frame)
{
	PTC::Reader *reader = (PTC::Reader *)_reader;
	int sfra, efra;
	reader->get_frame_range(sfra, efra);
	if (start_frame) *start_frame = sfra;
	if (end_frame) *end_frame = efra;
}

PTCReadSampleResult PTC_read_sample(PTCReader *_reader, float frame)
{
	PTC::Reader *reader = (PTC::Reader *)_reader;
	return reader->read_sample(frame);
}

PTCReadSampleResult PTC_test_sample(PTCReader *_reader, float frame)
{
	PTC::Reader *reader = (PTC::Reader *)_reader;
	return reader->test_sample(frame);
}

/* get writer/reader from RNA type */
PTCWriter *PTC_writer_from_rna(Scene *scene, PointerRNA *ptr)
{
	if (RNA_struct_is_a(ptr->type, &RNA_ParticleSystem)) {
		Object *ob = (Object *)ptr->id.data;
		ParticleSystem *psys = (ParticleSystem *)ptr->data;
		return PTC_writer_particles(scene, ob, psys);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_ClothModifier)) {
		Object *ob = (Object *)ptr->id.data;
		ClothModifierData *clmd = (ClothModifierData *)ptr->data;
		return PTC_writer_cloth(scene, ob, clmd);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_SoftBodySettings)) {
		Object *ob = (Object *)ptr->id.data;
		SoftBody *softbody = (SoftBody *)ptr->data;
		return PTC_writer_softbody(scene, ob, softbody);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_RigidBodyWorld)) {
		BLI_assert((Scene *)ptr->id.data == scene);
		RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;
		return PTC_writer_rigidbody(scene, rbw);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_SmokeDomainSettings)) {
		Object *ob = (Object *)ptr->id.data;
		SmokeDomainSettings *domain = (SmokeDomainSettings *)ptr->data;
		return PTC_writer_smoke(scene, ob, domain);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_DynamicPaintSurface)) {
		Object *ob = (Object *)ptr->id.data;
		DynamicPaintSurface *surface = (DynamicPaintSurface *)ptr->data;
		return PTC_writer_dynamicpaint(scene, ob, surface);
	}
#if 0 /* modifier uses internal writer during scene update */
	if (RNA_struct_is_a(ptr->type, &RNA_PointCacheModifier)) {
		Object *ob = (Object *)ptr->id.data;
		PointCacheModifierData *pcmd = (PointCacheModifierData *)ptr->data;
		return PTC_writer_point_cache(scene, ob, pcmd);
	}
#endif
	return NULL;
}

PTCReader *PTC_reader_from_rna(Scene *scene, PointerRNA *ptr)
{
	if (RNA_struct_is_a(ptr->type, &RNA_ParticleSystem)) {
		Object *ob = (Object *)ptr->id.data;
		ParticleSystem *psys = (ParticleSystem *)ptr->data;
		return PTC_reader_particles(scene, ob, psys);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_ClothModifier)) {
		Object *ob = (Object *)ptr->id.data;
		ClothModifierData *clmd = (ClothModifierData *)ptr->data;
		return PTC_reader_cloth(scene, ob, clmd);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_SoftBodySettings)) {
		Object *ob = (Object *)ptr->id.data;
		SoftBody *softbody = (SoftBody *)ptr->data;
		return PTC_reader_softbody(scene, ob, softbody);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_RigidBodyWorld)) {
		BLI_assert((Scene *)ptr->id.data == scene);
		RigidBodyWorld *rbw = (RigidBodyWorld *)ptr->data;
		return PTC_reader_rigidbody(scene, rbw);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_SmokeDomainSettings)) {
		Object *ob = (Object *)ptr->id.data;
		SmokeDomainSettings *domain = (SmokeDomainSettings *)ptr->data;
		return PTC_reader_smoke(scene, ob, domain);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_DynamicPaintSurface)) {
		Object *ob = (Object *)ptr->id.data;
		DynamicPaintSurface *surface = (DynamicPaintSurface *)ptr->data;
		return PTC_reader_dynamicpaint(scene, ob, surface);
	}
	if (RNA_struct_is_a(ptr->type, &RNA_PointCacheModifier)) {
		Object *ob = (Object *)ptr->id.data;
		PointCacheModifierData *pcmd = (PointCacheModifierData *)ptr->data;
		return PTC_reader_point_cache(scene, ob, pcmd);
	}
	return NULL;
}

#else

void PTC_writer_free(PTCWriter *_writer)
{
}

void PTC_write(struct PTCWriter *_writer)
{
}

#endif
