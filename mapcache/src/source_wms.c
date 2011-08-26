/*
 *  Copyright 2010 Thomas Bonfort
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "geocache.h"
#include "ezxml.h"
#include <apr_tables.h>
#include <apr_strings.h>

/**
 * \private \memberof geocache_source_wms
 * \sa geocache_source::render_map()
 */
void _geocache_source_wms_render_map(geocache_context *ctx, geocache_map *map) {
    geocache_source_wms *wms = (geocache_source_wms*)map->tileset->source;
    apr_table_t *params = apr_table_clone(ctx->pool,wms->wms_default_params);
    apr_table_setn(params,"BBOX",apr_psprintf(ctx->pool,"%f,%f,%f,%f",
             map->extent[0],map->extent[1],map->extent[2],map->extent[3]));
    apr_table_setn(params,"WIDTH",apr_psprintf(ctx->pool,"%d",map->width));
    apr_table_setn(params,"HEIGHT",apr_psprintf(ctx->pool,"%d",map->height));
    apr_table_setn(params,"FORMAT","image/png");
    apr_table_setn(params,"SRS",map->grid_link->grid->srs);
 
    apr_table_overlap(params,wms->getmap_params,0);
    if(map->dimensions && !apr_is_empty_table(map->dimensions)) {
       const apr_array_header_t *elts = apr_table_elts(map->dimensions);
       int i;
       for(i=0;i<elts->nelts;i++) {
          apr_table_entry_t entry = APR_ARRAY_IDX(elts,i,apr_table_entry_t);
          apr_table_setn(params,entry.key,entry.val);
       }
 
    }      
    map->data = geocache_buffer_create(30000,ctx->pool);
    geocache_http_do_request_with_params(ctx,wms->http,params,map->data,NULL);
    GC_CHECK_ERROR(ctx);
 
    if(!geocache_imageio_is_valid_format(ctx,map->data)) {
       char *returned_data = apr_pstrndup(ctx->pool,(char*)map->data->buf,map->data->size);
       ctx->set_error(ctx, 502, "wms request for tileset %s returned an unsupported format:\n%s",
             map->tileset->name, returned_data);
    }
}

void _geocache_source_wms_query(geocache_context *ctx, geocache_feature_info *fi) {
    geocache_map *map = (geocache_map*)fi;
    geocache_source_wms *wms = (geocache_source_wms*)map->tileset->source;
    
    apr_table_t *params = apr_table_clone(ctx->pool,wms->wms_default_params);
    apr_table_overlap(params,wms->getmap_params,0);
    apr_table_setn(params,"BBOX",apr_psprintf(ctx->pool,"%f,%f,%f,%f",
             map->extent[0],map->extent[1],map->extent[2],map->extent[3]));
    apr_table_setn(params,"REQUEST","GetFeatureInfo");
    apr_table_setn(params,"WIDTH",apr_psprintf(ctx->pool,"%d",map->width));
    apr_table_setn(params,"HEIGHT",apr_psprintf(ctx->pool,"%d",map->height));
    apr_table_setn(params,"SRS",map->grid_link->grid->srs);
    apr_table_setn(params,"X",apr_psprintf(ctx->pool,"%d",fi->i));
    apr_table_setn(params,"Y",apr_psprintf(ctx->pool,"%d",fi->j));
    apr_table_setn(params,"INFO_FORMAT",fi->format);
 
    apr_table_overlap(params,wms->getfeatureinfo_params,0);
    if(map->dimensions && !apr_is_empty_table(map->dimensions)) {
       const apr_array_header_t *elts = apr_table_elts(map->dimensions);
       int i;
       for(i=0;i<elts->nelts;i++) {
          apr_table_entry_t entry = APR_ARRAY_IDX(elts,i,apr_table_entry_t);
          apr_table_setn(params,entry.key,entry.val);
       }
 
    }      

    map->data = geocache_buffer_create(30000,ctx->pool);
    geocache_http_do_request_with_params(ctx,wms->http,params,map->data,NULL);
    GC_CHECK_ERROR(ctx);
   
}

/**
 * \private \memberof geocache_source_wms
 * \sa geocache_source::configuration_parse()
 */
void _geocache_source_wms_configuration_parse_json(geocache_context *ctx, cJSON *props, geocache_source *source) {
   cJSON *tmp;
   geocache_source_wms *src = (geocache_source_wms*)source;
   tmp = cJSON_GetObjectItem(props,"http");
   if(!tmp) {
      ctx->set_error(ctx,400,"wms source %s has no http object",source->name);
      return;
   }
   src->http = geocache_http_configuration_parse_json(ctx,tmp);
   GC_CHECK_ERROR(ctx);
   
   tmp = cJSON_GetObjectItem(props,"getmap");
   if(!tmp) {
      ctx->set_error(ctx,400,"wms source %s has no getmap object",source->name);
      return;
   }
   tmp = cJSON_GetObjectItem(tmp,"params");
   parse_keyvalues(ctx,tmp,src->getmap_params);
   
   tmp = cJSON_GetObjectItem(props,"getfeatureinfo");
   if(tmp) {
      cJSON *info_formats = cJSON_GetObjectItem(tmp,"info_formats");
      int n;
      if(!info_formats || ! (n = cJSON_GetArraySize(info_formats))) {
         ctx->set_error(ctx,400,"wms source %s getfeatureinfo has no info_formats",source->name);
         return;
      }
      source->info_formats = apr_array_make(ctx->pool,n,sizeof(char*));
      while(n--) {
         cJSON *ift = cJSON_GetArrayItem(info_formats,n);
         if(!ift->valuestring) {
            ctx->set_error(ctx,400,"failed to parse info_format for source %s",source->name);
            return;
         }
         APR_ARRAY_PUSH(source->info_formats,char*) = apr_pstrdup(ctx->pool,ift->valuestring);
      }
      tmp = cJSON_GetObjectItem(tmp,"params");
      parse_keyvalues(ctx,tmp,src->getfeatureinfo_params);
   }



}
/**
 * \private \memberof geocache_source_wms
 * \sa geocache_source::configuration_parse()
 */
void _geocache_source_wms_configuration_parse_xml(geocache_context *ctx, ezxml_t node, geocache_source *source) {
   ezxml_t cur_node;
   geocache_source_wms *src = (geocache_source_wms*)source;


   if ((cur_node = ezxml_child(node,"getmap")) != NULL){
      ezxml_t gm_node;
      if ((gm_node = ezxml_child(cur_node,"params")) != NULL) {
         for(gm_node = gm_node->child; gm_node; gm_node = gm_node->sibling) {
            apr_table_set(src->getmap_params, gm_node->name, gm_node->txt);
         }
      } else {
         ctx->set_error(ctx,400,"wms source %s <getmap> has no <params> block (should contain at least <LAYERS> child)",source->name);
         return;
      }
   } else {
      ctx->set_error(ctx,400,"wms source %s has no <getmap> block",source->name);
      return;
   }
   if ((cur_node = ezxml_child(node,"getfeatureinfo")) != NULL){
      ezxml_t fi_node;
      if ((fi_node = ezxml_child(cur_node,"info_formats")) != NULL) {
         source->info_formats = apr_array_make(ctx->pool,3,sizeof(char*));
         char *iformats = apr_pstrdup(ctx->pool,fi_node->txt);
         char *key,*last;
         for (key = apr_strtok(iformats, "," , &last); key != NULL;
               key = apr_strtok(NULL, ",", &last)) {
            APR_ARRAY_PUSH(source->info_formats,char*) = key;
         }
      } else {
         ctx->set_error(ctx,400,"wms source %s <getfeatureinfo> has no <info_formats> tag",source->name);
         return;
      }
      if ((fi_node = ezxml_child(cur_node,"params")) != NULL) {
         for(fi_node = fi_node->child; fi_node; fi_node = fi_node->sibling) {
            apr_table_set(src->getfeatureinfo_params, fi_node->name, fi_node->txt);
         }
      } else {
         ctx->set_error(ctx,400,"wms source %s <getfeatureinfo> has no <params> block (should contain at least <QUERY_LAYERS> child)",source->name);
         return;
      }
   }
   if ((cur_node = ezxml_child(node,"http")) != NULL) {
      src->http = geocache_http_configuration_parse_xml(ctx,cur_node);
   }
}

/**
 * \private \memberof geocache_source_wms
 * \sa geocache_source::configuration_check()
 */
void _geocache_source_wms_configuration_check(geocache_context *ctx, geocache_source *source) {
   geocache_source_wms *src = (geocache_source_wms*)source;
   /* check all required parameters are configured */
   if(!src->http) {
      ctx->set_error(ctx, 400, "wms source %s has no <http> request configured",source->name);
   }
   if(!apr_table_get(src->getmap_params,"LAYERS")) {
      ctx->set_error(ctx, 400, "wms source %s has no LAYERS", source->name);
   }
   if(source->info_formats) {
      if(!apr_table_get(src->getfeatureinfo_params,"QUERY_LAYERS")) {
         ctx->set_error(ctx, 400, "wms source %s has no QUERY_LAYERS", source->name);
      }
   }
}

geocache_source* geocache_source_wms_create(geocache_context *ctx) {
   geocache_source_wms *source = apr_pcalloc(ctx->pool, sizeof(geocache_source_wms));
   if(!source) {
      ctx->set_error(ctx, 500, "failed to allocate wms source");
      return NULL;
   }
   geocache_source_init(ctx, &(source->source));
   source->source.type = GEOCACHE_SOURCE_WMS;
   source->source.render_map = _geocache_source_wms_render_map;
   source->source.configuration_check = _geocache_source_wms_configuration_check;
   source->source.configuration_parse_xml = _geocache_source_wms_configuration_parse_xml;
   source->source.configuration_parse_json = _geocache_source_wms_configuration_parse_json;
   source->source.query_info = _geocache_source_wms_query;
   source->wms_default_params = apr_table_make(ctx->pool,4);;
   source->getmap_params = apr_table_make(ctx->pool,4);
   source->getfeatureinfo_params = apr_table_make(ctx->pool,4);
   apr_table_add(source->wms_default_params,"VERSION","1.1.1");
   apr_table_add(source->wms_default_params,"REQUEST","GetMap");
   apr_table_add(source->wms_default_params,"SERVICE","WMS");
   apr_table_add(source->wms_default_params,"STYLES","");
   return (geocache_source*)source;
}



/* vim: ai ts=3 sts=3 et sw=3
*/
