/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rdf_query_virtuoso.c - RDF Query with Virtuoso
 *
 * Copyright (C) 2008, Openlink Software http://www.openlinksw.com/
 * Copyright (C) 2010, Dave Beckett http://www.dajobe.org/
 *
 * Based in part on rasqal_result_formats.c and rasqal_sparql_xml.c
 * (see NOTICE later in code)
 *
 * This package is Free Software and part of Redland http://librdf.org/
 *
 * It is licensed under the following three licenses as alternatives:
 *   1. GNU Lesser General Public License(LGPL) V2.1 or any newer version
 *   2. GNU General Public License(GPL) V2 or any newer version
 *   3. Apache License, V2.0 or any newer version
 *
 * You may not use this file except in compliance with at least one of
 * the above three licenses.
 *
 * See LICENSE.html or LICENSE.txt at the top of this package for the
 * complete terms and further detail along with the license texts for
 * the licenses in COPYING.LIB, COPYING and LICENSE-2.0.txt respectively.
 *
 */

#ifdef HAVE_CONFIG_H
#include <rdf_config.h>
#endif

#ifdef WIN32
#include <win32_rdf_config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h> /* for abort() as used in errors */
#endif
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#include <ctype.h>

#include <redland.h>

#include <sql.h>
#include <sqlext.h>

/*#define VIRTUOSO_STORAGE_DEBUG 1 */

#include <rdf_storage_virtuoso_internal.h>


static int librdf_query_virtuoso_results_next(librdf_query_results *query_results);

static const char *strexpect(const char *keyword, const char *source)
{
  while(isspace(*source))
    source++;

  while(*keyword &&(toupper(*keyword) == toupper(*source))) {
    keyword++;
    source++;
  }

  if(*keyword)
    return NULL;

  if(*source == '\0')
    return source;

  if(isspace(*source)) {
    while(isspace(*source))
      source++;
    return source;
  }

  return NULL;
}


static int
rdf_virtuoso_ODBC_Errors(const char *where, librdf_world *world,
                         librdf_storage_virtuoso_connection* handle)
{
  SQLCHAR buf[512];
  SQLCHAR sqlstate[15];

  while(SQLError(handle->henv, handle->hdbc, handle->hstmt, sqlstate, NULL,
                 buf, sizeof(buf), NULL) == SQL_SUCCESS) {
#ifdef VIRTUOSO_STORAGE_DEBUG
    fprintf(stderr, "%s ||%s, SQLSTATE=%s\n", where, buf, sqlstate);
#endif
    librdf_log(world, 0, LIBRDF_LOG_ERROR, LIBRDF_FROM_QUERY, NULL,
               "Virtuoso %s failed [%s] %s", where, sqlstate, buf);
  }

  while(SQLError(handle->henv, handle->hdbc, SQL_NULL_HSTMT, sqlstate, NULL,
                 buf, sizeof(buf), NULL) == SQL_SUCCESS) {
#ifdef VIRTUOSO_STORAGE_DEBUG
    fprintf(stderr, "%s ||%s, SQLSTATE=%s\n", where, buf, sqlstate);
#endif
    librdf_log(world, 0, LIBRDF_LOG_ERROR, LIBRDF_FROM_QUERY, NULL,
               "Virtuoso %s failed [%s] %s", where, sqlstate, buf);
  }

  while(SQLError(handle->henv, SQL_NULL_HDBC, SQL_NULL_HSTMT, sqlstate, NULL,
                 buf, sizeof(buf), NULL) == SQL_SUCCESS) {
#ifdef VIRTUOSO_STORAGE_DEBUG
    fprintf(stderr, "%s ||%s, SQLSTATE=%s\n", where, buf, sqlstate);
#endif
    librdf_log(world, 0, LIBRDF_LOG_ERROR, LIBRDF_FROM_QUERY, NULL,
               "Virtuoso %s failed [%s] %s", where, sqlstate, buf);
  }

  return -1;
}



/* functions implementing query api */


static void
virtuoso_free_result(librdf_query* query)
{
  librdf_query_virtuoso_context *context;

  short i;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "virtuoso_free_result \n");
#endif
  if(context->colNames) {
    for(i = 0; i < context->numCols; i++) {
      if(context->colNames[i])
        LIBRDF_FREE(cstring, (char *)context->colNames[i]);
    }
    LIBRDF_FREE(cstrings, (char **)context->colNames);
  }
  context->colNames = NULL;

  if(context->colValues) {
    for(i = 0; i < context->numCols; i++) {
      if(context->colValues[i]) {
        librdf_free_node(context->colValues[i]);
      }
    }
    LIBRDF_FREE(librdf_node*, context->colValues);
  }
  context->colValues = NULL;
}


static int
librdf_query_virtuoso_init(librdf_query* query, const char *name,
                           librdf_uri* uri, const unsigned char* query_string,
                           librdf_uri *base_uri)
{
  librdf_query_virtuoso_context *context;
  int len;
  unsigned char *query_string_copy;
  char *seps={(char *)" \t\n\r\f"};
  char *token;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_init \n");
#endif
  context->query = query;
  context->language=context->query->factory->name;
  context->offset = 0;
  context->colNames = NULL;
  context->colValues = 0;
  context->numCols = 0;
  context->failed = 0;
  context->eof = 1;
  context->row_count = 0;
  context->result_type = VQUERY_RESULTS_UNKNOWN;

  context->results = NULL; /* FIXME */

  len = strlen((const char*)query_string);
  query_string_copy = (unsigned char*)LIBRDF_MALLOC(cstring, len+1);
  if(!query_string_copy)
    return 1;
  strcpy((char*)query_string_copy, (const char*)query_string);

  token = strtok((char*)query_string_copy, seps);

  while(token != NULL) {
    if(strexpect("SELECT", (const char*)token)) {
      context->result_type = VQUERY_RESULTS_BINDINGS;
      context->results = NULL; /* FIXME */
      break;
    } else if(strexpect("ASK", (const char*)token)) {
      context->result_type = VQUERY_RESULTS_BOOLEAN;
      break;
    } else if(strexpect("CONSTRUCT", (const char*)token)) {
      context->result_type = VQUERY_RESULTS_GRAPH | VQUERY_RESULTS_BINDINGS;
      break;
    } else if(strexpect("DESCRIBE", (const char*)token)) {
      context->result_type = VQUERY_RESULTS_GRAPH | VQUERY_RESULTS_BINDINGS;
      break;
    }
    token = strtok(NULL, seps);
  }

  strcpy((char*)query_string_copy, (const char*)query_string);
  context->query_string = query_string_copy;
  if(base_uri)
    context->uri=librdf_new_uri_from_uri(base_uri);

  return 0;
}


static void
librdf_query_virtuoso_terminate(librdf_query* query)
{
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_terminate \n");
#endif
  if(context->results)
    rasqal_free_query_results(context->results);
  
  virtuoso_free_result(query);
  SQLCloseCursor(context->vc->hstmt);

  if(context->query_string)
    LIBRDF_FREE(cstring, context->query_string);

  if(context->uri)
    librdf_free_uri(context->uri);

  if(context->vc)
    context->vc->v_release_connection(context->storage, context->vc);

  if(context->storage)
    librdf_storage_remove_reference(context->storage);
}


static librdf_query_results*
librdf_query_virtuoso_execute(librdf_query* query, librdf_model* model)
{
  librdf_query_virtuoso_context *context;
  librdf_query_results* results;
  int rc = 0;
  SQLUSMALLINT icol;
  char pref[]="sparql define output:format '_JAVA_' ";
  char *cmd = NULL;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_execute \n");
#endif
  context->model=model;
  context->numCols = 0;
  context->failed = 0;
  context->eof = 1;
  context->row_count = 0;
  context->limit= -1;
  context->offset= -1;
  virtuoso_free_result(query);
  SQLCloseCursor(context->vc->hstmt);

  if(!(cmd = (char*)LIBRDF_MALLOC(cstring, strlen(pref)+
                                  strlen((char *)context->query_string) +1))) {
      goto error;
    }
  strcpy(cmd, pref);
  strcat(cmd, (char *)context->query_string);

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "SQL>>%s\n", cmd);
#endif
  rc = SQLExecDirect(context->vc->hstmt, (SQLCHAR *)cmd, SQL_NTS);
  if(!SQL_SUCCEEDED(rc)) {
    context->result_type = VQUERY_RESULTS_SYNTAX;
    rdf_virtuoso_ODBC_Errors("SQLExecDirect()", context->storage->world,
                             context->vc);
    goto error;
  }

  rc = SQLNumResultCols(context->vc->hstmt, &context->numCols);
  if(!SQL_SUCCEEDED(rc)) {
    rdf_virtuoso_ODBC_Errors("SQLNumResultCols()", context->storage->world,
                             context->vc);
    goto error;
  }

  if(context->numCols > 0) {
    context->colNames  = (char**)LIBRDF_CALLOC(cstrings, context->numCols+1,
                                               sizeof(char*));
    if(!context->colNames) {
      goto error;
    }

    context->colValues  = (librdf_node **)LIBRDF_CALLOC(librdf_node**,
                                                        context->numCols+1,
                                                        sizeof(librdf_node*));
    if(!context->colValues) {
      goto error;
    }

    for(icol = 1; icol<= context->numCols; icol++) {
      SQLSMALLINT namelen;
      SQLCHAR name[255];

      rc = SQLColAttributes(context->vc->hstmt, icol, SQL_COLUMN_LABEL, name,
                            sizeof(name), &namelen, NULL);
      if(!SQL_SUCCEEDED(rc)) {
        rdf_virtuoso_ODBC_Errors("SQLColAttributes()", context->storage->world,
                                 context->vc);
        goto error;
      }

      context->colNames[icol-1]  = (char*)LIBRDF_CALLOC(cstring, 1, namelen + 1);
      if(!context->colNames[icol-1])
        goto error;

      strcpy(context->colNames[icol-1], (char *)name);
    }

    context->colNames[context->numCols] = NULL;
    context->result_type |= VQUERY_RESULTS_BINDINGS;
    context->eof = 0;
  }

  context->results = rasqal_new_query_results(query->world->rasqal_world_ptr, NULL, RASQAL_QUERY_RESULTS_BINDINGS, NULL /* vars_table */);

  results = (librdf_query_results*)LIBRDF_MALLOC(librdf_query_results, 
                                                 sizeof(*results));
  if(!results) {
    SQLCloseCursor(context->vc->hstmt);
  } else {
    results->query = query;
  }

  rc = librdf_query_virtuoso_results_next(results);
  if(rc == 2) /* ERROR */
    goto error;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_execute OK\n");
#endif
  if(cmd)
    LIBRDF_FREE(cstring, (char *)cmd);

  return results;

error:
  if(cmd)
    LIBRDF_FREE(cstring, (char *)cmd);
  context->failed = 1;
  virtuoso_free_result(query);
  return NULL;
}


static int
librdf_query_virtuoso_get_limit(librdf_query* query)
{
  librdf_query_virtuoso_context *context;
  context = (librdf_query_virtuoso_context*)query->context;
  return context->limit;
}


static int
librdf_query_virtuoso_set_limit(librdf_query* query, int limit)
{
  librdf_query_virtuoso_context *context;
  context = (librdf_query_virtuoso_context*)query->context;

  context->limit = limit;
  return 0;
}


static int
librdf_query_virtuoso_get_offset(librdf_query* query)
{
  librdf_query_virtuoso_context *context;
  context = (librdf_query_virtuoso_context*)query->context;

  return context->offset;
}


static int
librdf_query_virtuoso_set_offset(librdf_query* query, int offset)
{
  librdf_query_virtuoso_context *context;
  context = (librdf_query_virtuoso_context*)query->context;

  context->offset = offset;
  return 0;
}


/**
 * librdf_query_results_get_count:
 * @query_results: #librdf_query_results query results
 *
 * Get number of bindings so far.
 *
 * Return value: number of bindings found so far
 **/
static int
librdf_query_virtuoso_results_get_count(librdf_query_results *query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_count \n");
#endif
  if(!query)
    return -1;

  context  = (librdf_query_virtuoso_context*)query->context;

  if(context->failed || context->numCols <= 0)
    return -1;

  if(context->numCols <= 0)
    return -1;

  return context->row_count;
}


/**
 * librdf_query_results_next:
 * @query_results: #librdf_query_results query results
 *
 * Move to the next result.
 *
 * Return value: non-0 if failed or results exhausted
 **/
static int
librdf_query_virtuoso_results_next(librdf_query_results *query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  int rc;
  short col;
  short numCols=context->numCols;
  librdf_node *node;
  char *data;
  int is_null;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_next \n");
#endif
  if(context->failed || context->eof)
    return 1;

  for(col = 0; col < numCols; col++)
    if(context->colValues[col]) {
      librdf_free_node(context->colValues[col]);
      context->colValues[col] = NULL;
    }

  rc = SQLFetch(context->vc->hstmt);
  if(rc == SQL_NO_DATA_FOUND) {
    context->eof = 1;
    return 1;
  } else if(rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
    rdf_virtuoso_ODBC_Errors((char *)"SQLFetch", context->storage->world,
                             context->vc);
    return 2;
  }

  for(col = 1; col <= context->numCols; col++) {
    data = context->vc->v_GetDataCHAR(context->storage->world, context->vc,
                                    col, &is_null);
    if(!data && !is_null)
      return 2;

    if(!data || is_null) {
      node = NULL;
    } else {
      node = context->vc->v_rdf2node(context->storage, context->vc, col, data);
      LIBRDF_FREE(cstring, (char *) data);
      if(!node)
	return 2;
    }
    context->colValues[col-1]=node;
  }

  context->row_count++;
  return 0;
}


/**
 * librdf_query_results_finished:
 * @query_results: #librdf_query_results query results
 *
 * Find out if binding results are exhausted.
 *
 * Return value: non-0 if results are finished or query failed
 **/
static int
librdf_query_virtuoso_results_finished(librdf_query_results *query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_finished \n");
#endif
  if(context->failed || context->eof)
    return 1;

  return 0;
}


/**
 * librdf_query_results_get_bindings:
 * @query_results: #librdf_query_results query results
 * @names: pointer to an array of binding names(or NULL)
 * @values: pointer to an array of binding value #librdf_node(or NULL)
 *
 * Get all binding names, values for current result.
 *
 * If names is not NULL, it is set to the address of a shared array
 * of names of the bindings(an output parameter).  These names
 * are shared and must not be freed by the caller
 *
 * If values is not NULL, it is used as an array to store pointers
 * to the librdf_node* of the results.  These nodes must be freed
 * by the caller.  The size of the array is determined by the
 * number of names of bindings, returned by
 * librdf_query_get_bindings_count dynamically or
 * will be known in advanced if hard-coded into the query string.
 *
 * Example
 *
 * const char **names = NULL;
 * librdf_node* values[10];
 *
 * if(librdf_query_results_get_bindings(results, &amp;names, values))
 *   ...
 *
 * Return value: non-0 if the assignment failed
 **/
static int
librdf_query_virtuoso_results_get_bindings(librdf_query_results *query_results, const char ***names, librdf_node **values)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  short col;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_bindings \n");
#endif
  if(context->failed || context->numCols <= 0)
    return 1;

  if(names)
    *names  = (const char**)context->colNames;

  if(values && !context->eof) {

    for(col = 0; col < context->numCols; col++) {
      values[col]=context->colValues[col];
      context->colValues[col] = NULL;
    }
  }

 return 0;
}


/**
 * librdf_query_results_get_binding_value:
 * @query_results: #librdf_query_results query results
 * @offset: offset of binding name into array of known names
 *
 * Get one binding value for the current result.
 *
 * Return value: a new #librdf_node binding value or NULL on failure
 **/
static librdf_node*
librdf_query_virtuoso_results_get_binding_value(librdf_query_results *query_results, int offset)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  librdf_node *retVal;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_binding_value \n");
#endif
  if(context->failed || context->numCols <= 0)
    return NULL;

  if(offset < 0 || offset > context->numCols-1 || !context->colValues)
    return NULL;

  retVal=context->colValues[offset];
  context->colValues[offset] = NULL;
  return retVal;
}


/**
 * librdf_query_results_get_binding_name:
 * @query_results: #librdf_query_results query results
 * @offset: offset of binding name into array of known names
 *
 * Get binding name for the current result.
 *
 * Return value: a pointer to a shared copy of the binding name or NULL on failure
 **/
static const char*
librdf_query_virtuoso_results_get_binding_name(librdf_query_results *query_results, int offset)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_binding_name \n");
#endif
  if(context->failed || context->numCols <= 0)
    return NULL;

  if(offset < 0 || offset > context->numCols-1 || !context->colNames)
    return NULL;

  return context->colNames[offset];
}


/**
 * librdf_query_results_get_binding_value_by_name:
 * @query_results: #librdf_query_results query results
 * @name: variable name
 *
 * Get one binding value for a given name in the current result.
 *
 * Return value: a new #librdf_node binding value or NULL on failure
 **/
static librdf_node*
librdf_query_virtuoso_results_get_binding_value_by_name(librdf_query_results *query_results, const char *name)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  short col;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_binding_value_by_name \n");
#endif
  if(context->failed || context->numCols <= 0)
    return NULL;

  if(!context->colNames || !context->colValues)
    return NULL;

  for(col = 0; col < context->numCols; col++) {
    if(!strcmp((const char*)name, (const char*)context->colNames[col])) {
      return context->colValues[col];
      break;
    }
  }

  return NULL;
}


/**
 * librdf_query_results_get_bindings_count:
 * @query_results: #librdf_query_results query results
 *
 * Get the number of bound variables in the result.
 *
 * Return value: <0 if failed or results exhausted
 **/
static int
librdf_query_virtuoso_results_get_bindings_count(librdf_query_results *query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_bindings_count \n");
#endif
  if(context->failed || context->numCols <= 0)
    return -1;

  if(!context->colNames || !context->colValues)
    return -1;

  return context->numCols;
}


/**
 * librdf_free_query_results:
 * @query_results: #librdf_query_results object
 *
 * Destructor - destroy a #librdf_query_results object.
 *
 **/
static void
librdf_query_virtuoso_free_results(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_free_results \n");
#endif
  if(!context->failed && context->numCols) {
    SQLCloseCursor(context->vc->hstmt);
  }

  virtuoso_free_result(query);
  context->eof = 1;
  context->numCols = 0;
  context->row_count = 0;
  context->result_type = VQUERY_RESULTS_UNKNOWN;
}


/**
 * librdf_query_results_is_bindings:
 * @query_results: #librdf_query_results object
 *
 * Test if librdf_query_results is variable bindings format.
 *
 * Return value: non-0 if true
 **/
static int
librdf_query_virtuoso_results_is_bindings(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_is_bindings \n");
#endif
  if(context->numCols <= 0)
    return 0;

  return(context->result_type & VQUERY_RESULTS_BINDINGS);
}


/**
 * librdf_query_results_is_boolean:
 * @query_results: #librdf_query_results object
 *
 * Test if librdf_query_results is boolean format.
 *
 * If this function returns true, the result can be retrieved by
 * librdf_query_results_get_boolean().
 *
 * Return value: non-0 if true
 **/
static int
librdf_query_virtuoso_results_is_boolean(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_is_boolean \n");
#endif
  if(context->numCols <= 0)
    return 0;

  return(context->result_type & VQUERY_RESULTS_BOOLEAN);
}


/**
 * librdf_query_results_is_graph:
 * @query_results: #librdf_query_results object
 *
 * Test if librdf_query_results is RDF graph format.
 *
 * Return value: non-0 if true
 **/
static int
librdf_query_virtuoso_results_is_graph(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  
  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_is_graph \n");
#endif
  if(context->numCols <= 0)
    return 0;

  return(context->result_type & VQUERY_RESULTS_GRAPH);
}


/**
 * librdf_query_results_is_syntax:
 * @query_results: #librdf_query_results object
 *
 * Test if librdf_query_results is a syntax.
 *
 * If this function returns true, the ONLY result available
 * from this query is a syntax that can be serialized using
 * one of the #query_result_formatter class methods or with
 * librdf_query_results_to_counted_string(), librdf_query_results_to_string(),
 * librdf_query_results_to_file_handle() or librdf_query_results_to_file()
 *
 * Return value: non-0 if true
 **/
static int
librdf_query_virtuoso_results_is_syntax(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_is_syntax \n");
#endif
  if(context->numCols <= 0)
    return 0;

  return(context->result_type & VQUERY_RESULTS_SYNTAX);
}


/**
 * librdf_query_results_get_boolean:
 * @query_results: #librdf_query_results query_results
 *
 * Get boolean query result.
 *
 * The return value is only meaningful if this is a boolean
 * query result - see #librdf_query_results_is_boolean
 *
 * Return value: boolean query result - >0 is true, 0 is false, <0 on error or finished
 */
static int
librdf_query_virtuoso_results_get_boolean(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  int rc;
  int data;
  int is_null;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_get_boolean \n");
#endif
  if(context->failed || context->numCols <= 0)
    return -1;

  rc = SQLFetch(context->vc->hstmt);
  if(rc == SQL_NO_DATA_FOUND) {
    context->eof = 1;
    return 0;
  } else if(rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
    rdf_virtuoso_ODBC_Errors((char *)"SQLFetch", context->storage->world,
                             context->vc);
    return -1;
  }

  rc = context->vc->v_GetDataINT(context->storage->world, context->vc, 1,
                                 &is_null, &data);

  context->eof = 1;

  if(rc == -1)
    return -1;

  return data;
}


typedef struct {
  librdf_query *query;

  librdf_query_virtuoso_context* qcontext; /* query context */

  librdf_statement* statement; /* current statement */
  librdf_node *graph;
  int finished;
  short numCols;
} librdf_query_virtuoso_stream_context;


static int
librdf_query_virtuoso_query_results_end_of_stream(void* context)
{
  librdf_query_virtuoso_stream_context* scontext = (librdf_query_virtuoso_stream_context*)context;

  return scontext->finished;
}


static int
librdf_query_virtuoso_query_results_update_statement(void* context)
{
  librdf_query_virtuoso_stream_context* scontext;
  librdf_query_virtuoso_context *qcontext;
  librdf_world* world;
  librdf_node* node;
  short colNum;
  char *data;
  int is_null;

  scontext = (librdf_query_virtuoso_stream_context*)context;
  qcontext = scontext->qcontext;
  world = scontext->query->world;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_query_results_update_statement \n");
#endif
  scontext->statement=librdf_new_statement(world);
  if(!scontext->statement)
    return 1;

  if(scontext->graph) {
    librdf_free_node(scontext->graph);
    scontext->graph = NULL;
  }

  if(!(qcontext->result_type & VQUERY_RESULTS_GRAPH))
    goto fail;

  colNum = 1;
  if(colNum > scontext->numCols)
    goto fail;

  if(scontext->numCols > 3) {
    data = qcontext->vc->v_GetDataCHAR(world, qcontext->vc, colNum, &is_null);
    if(!data || is_null)
      goto fail;
    node = qcontext->vc->v_rdf2node(qcontext->storage, qcontext->vc, colNum, data);
    LIBRDF_FREE(cstring, (char *)data);
    if(!node)
      goto fail;
    scontext->graph=node;
    colNum++;
  }

  data = qcontext->vc->v_GetDataCHAR(world, qcontext->vc, colNum, &is_null);
  if(!data || is_null)
    goto fail;
  node = qcontext->vc->v_rdf2node(qcontext->storage, qcontext->vc, colNum, data);
  LIBRDF_FREE(cstring, (char *)data);
  if(!node)
    goto fail;

  librdf_statement_set_subject(scontext->statement, node);
  colNum++;

  if(colNum > scontext->numCols)
    goto fail;

  data = qcontext->vc->v_GetDataCHAR(world, qcontext->vc, colNum, &is_null);
  if(!data || is_null)
    goto fail;
  node = qcontext->vc->v_rdf2node(qcontext->storage, qcontext->vc, colNum, data);
  LIBRDF_FREE(cstring, (char *)data);
  if(!node)
    goto fail;

  librdf_statement_set_predicate(scontext->statement, node);
  colNum++;

  if(colNum > scontext->numCols)
    goto fail;

  data = qcontext->vc->v_GetDataCHAR(world, qcontext->vc, colNum, &is_null);
  if(!data || is_null)
    goto fail;
  node = qcontext->vc->v_rdf2node(qcontext->storage, qcontext->vc, colNum, data);
  LIBRDF_FREE(cstring, (char *)data);
  if(!node)
    goto fail;

  librdf_statement_set_object(scontext->statement, node);
  return 0; /* success */

fail:
  librdf_free_statement(scontext->statement);
  scontext->statement = NULL;
  return 1;
}


static int
librdf_query_virtuoso_query_results_next_statement(void* context)
{
  librdf_query_virtuoso_stream_context* scontext;
  librdf_query_virtuoso_context *qcontext;
  librdf_world* world;
  int rc;

  scontext = (librdf_query_virtuoso_stream_context*)context;
  qcontext = scontext->qcontext;
  world =scontext->query->world;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_query_results_next_statement \n");
#endif
  if(scontext->finished)
    return 1;

  if(scontext->statement) {
    librdf_free_statement(scontext->statement);
    scontext->statement = NULL;
  }

  rc = SQLFetch(qcontext->vc->hstmt);
  if(rc == SQL_NO_DATA_FOUND) {
    scontext->finished = 1;
  } else if(rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
    rdf_virtuoso_ODBC_Errors((char *)"SQLFetch", world, qcontext->vc);
    scontext->finished = 1;
  }

  if(!scontext->finished)
    librdf_query_virtuoso_query_results_update_statement(scontext);

  return scontext->finished;
}


static void*
librdf_query_virtuoso_query_results_get_statement(void* context, int flags)
{
  librdf_query_virtuoso_stream_context* scontext;

  scontext = (librdf_query_virtuoso_stream_context*)context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_query_results_get_statement \n");
#endif
  switch(flags) {
    case LIBRDF_ITERATOR_GET_METHOD_GET_OBJECT:
      return scontext->statement;

    case LIBRDF_ITERATOR_GET_METHOD_GET_CONTEXT:
      return scontext->graph;

    default:
      librdf_log(scontext->query->world, 0, LIBRDF_LOG_ERROR,
                 LIBRDF_FROM_QUERY, NULL,
                 "Unknown iterator method flag %d", flags);
      return NULL;
  }
}


static void
librdf_query_virtuoso_query_results_finished(void* context)
{
  librdf_query_virtuoso_stream_context* scontext;

  scontext = (librdf_query_virtuoso_stream_context*)context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_query_results_finished \n");
#endif
  if(scontext) {
    if(scontext->statement)
      librdf_free_statement(scontext->statement);

    if(scontext->graph)
      librdf_free_node(scontext->graph);

    LIBRDF_FREE(librdf_query_virtuoso_stream_context, scontext);
  }
}


/**
 * librdf_query_results_as_stream:
 * @query_results: #librdf_query_results query_results
 *
 * Get a query result as an RDF graph in #librdf_stream form
 *
 * The return value is only meaningful if this is an RDF graph
 * query result - see #librdf_query_results_is_graph
 *
 * Return value: a new #librdf_stream result or NULL on error
 */
static librdf_stream*
librdf_query_virtuoso_results_as_stream(librdf_query_results* query_results)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;
  librdf_query_virtuoso_stream_context* scontext;
  librdf_stream *stream;
  int col;

  context = (librdf_query_virtuoso_context*)query->context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_as_stream \n");
#endif
  if(context->failed || context->numCols < 3 || context->eof)
    return NULL;

  if((context->result_type & VQUERY_RESULTS_GRAPH) == 0)
    return NULL;

  scontext = (librdf_query_virtuoso_stream_context*)LIBRDF_CALLOC(librdf_query_virtuoso_stream_context, 1, sizeof(*scontext));
  if(!scontext)
    return NULL;

  scontext->query = query;
  scontext->qcontext=context;
  scontext->numCols=context->numCols;
  scontext->statement= librdf_new_statement(query->world);
  if(!scontext->statement)
    return NULL;

  col = 0;
  if(scontext->numCols > 3) {
    scontext->graph=context->colValues[col];
    context->colValues[col] = NULL;
    col++;
  }

  librdf_statement_set_subject(scontext->statement, context->colValues[col]);
  context->colValues[col] = NULL;
  col++;
  if(col > scontext->numCols)
    goto fail;

  librdf_statement_set_predicate(scontext->statement, context->colValues[col]);
  context->colValues[col] = NULL;
  col++;
  if(col > scontext->numCols)
    goto fail;

  librdf_statement_set_object(scontext->statement, context->colValues[col]);
  context->colValues[col] = NULL;
  col++;
  if(col > scontext->numCols)
    goto fail;

  stream=librdf_new_stream(query->world,
                          (void*)scontext,
                           &librdf_query_virtuoso_query_results_end_of_stream,
                           &librdf_query_virtuoso_query_results_next_statement,
                           &librdf_query_virtuoso_query_results_get_statement,
                           &librdf_query_virtuoso_query_results_finished);
  if(!stream) {
    librdf_query_virtuoso_query_results_finished((void*)scontext);
    return NULL;
  }

  return stream;

fail:
  librdf_free_statement(scontext->statement);
  scontext->statement = NULL;
  librdf_query_virtuoso_query_results_finished((void*)scontext);
  return NULL;
}



/**
 * librdf_query_results_formatter_write:
 * @iostr: #raptor_iostream to write the query to
 * @formatter: #librdf_query_results_formatter object
 * @results: #librdf_query_results query results format
 * @base_uri: #librdf_uri base URI of the output format
 *
 * Write the query results using the given formatter to an iostream
 *
 * See librdf_query_results_formats_enumerate() to get the
 * list of syntax URIs and their description.
 *
 * Return value: non-0 on failure
 **/
static int
librdf_query_virtuoso_results_formatter_write(raptor_iostream *iostr,
                                              librdf_query_results_formatter* qrf,
                                              librdf_query_results* query_results, 
                                              librdf_uri *base_uri)
{
  librdf_query *query = query_results->query;
  librdf_query_virtuoso_context *context;

#ifdef VIRTUOSO_STORAGE_DEBUG
  fprintf(stderr, "librdf_query_virtuoso_results_formatter_write \n");
#endif

  context = (librdf_query_virtuoso_context*)query->context;

  return rasqal_query_results_formatter_write(iostr, qrf->formatter,
                                              context->results, 
                                              (raptor_uri*)base_uri);
}


/* local function to register list query functions */


static void
librdf_query_virtuoso_register_factory(librdf_query_factory *factory)
{
  factory->context_length			= sizeof(librdf_query_virtuoso_context);

  factory->init					= librdf_query_virtuoso_init;
  factory->terminate				= librdf_query_virtuoso_terminate;
  factory->execute				= librdf_query_virtuoso_execute;
  factory->get_limit				= librdf_query_virtuoso_get_limit;
  factory->set_limit				= librdf_query_virtuoso_set_limit;
  factory->get_offset				= librdf_query_virtuoso_get_offset;
  factory->set_offset				= librdf_query_virtuoso_set_offset;

  factory->results_get_count			= librdf_query_virtuoso_results_get_count;
  factory->results_next				= librdf_query_virtuoso_results_next;
  factory->results_finished			= librdf_query_virtuoso_results_finished;
  factory->results_get_bindings			= librdf_query_virtuoso_results_get_bindings;
  factory->results_get_binding_value		= librdf_query_virtuoso_results_get_binding_value;
  factory->results_get_binding_name		= librdf_query_virtuoso_results_get_binding_name;
  factory->results_get_binding_value_by_name	= librdf_query_virtuoso_results_get_binding_value_by_name;

  factory->results_get_bindings_count		= librdf_query_virtuoso_results_get_bindings_count;
  factory->free_results				= librdf_query_virtuoso_free_results;
  factory->results_is_bindings			= librdf_query_virtuoso_results_is_bindings;
  factory->results_is_boolean			= librdf_query_virtuoso_results_is_boolean;
  factory->results_is_graph			= librdf_query_virtuoso_results_is_graph;
  factory->results_is_syntax			= librdf_query_virtuoso_results_is_syntax;
  factory->results_get_boolean			= librdf_query_virtuoso_results_get_boolean;
  factory->results_as_stream			= librdf_query_virtuoso_results_as_stream;

  factory->results_formatter_write		= librdf_query_virtuoso_results_formatter_write;
}


void
librdf_init_query_virtuoso(librdf_world *world)
{
  librdf_query_register_factory(world, "vsparql",
                                (const unsigned char*)"http://www.w3.org/TR/rdf-vsparql-query/",
                                &librdf_query_virtuoso_register_factory);
}
