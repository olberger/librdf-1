/* -*- Mode: c; c-basic-offset: 2 -*-
 *
 * rdf_storage.c - RDF Storage Implementation
 *
 * $Id$
 *
 * Copyright (C) 2000 David Beckett - http://purl.org/net/dajobe/
 * Institute for Learning and Research Technology, University of Bristol.
 *
 *    This package is Free Software available under either of two licenses
 *    (see FAQS.html to see why):
 * 
 * 1. The GNU Lesser General Public License (LGPL)
 * 
 *    See http://www.gnu.org/copyleft/lesser.html or COPYING.LIB for the
 *    full license text.
 *      _________________________________________________________________
 * 
 *      Copyright (C) 2000 David Beckett, Institute for Learning and
 *      Research Technology, University of Bristol. All Rights Reserved.
 * 
 *      This library is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU Lesser General Public License
 *      as published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 * 
 *      This library is distributed in the hope that it will be useful, but
 *      WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *      Lesser General Public License for more details.
 * 
 *      You should have received a copy of the GNU Lesser General Public
 *      License along with this library; if not, write to the Free Software
 *      Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *      USA
 *      _________________________________________________________________
 * 
 *    NOTE - under Term 3 of the LGPL, you may choose to license the entire
 *    library under the GPL. See COPYING for the full license text.
 * 
 * 2. The Mozilla Public License
 * 
 *    See http://www.mozilla.org/MPL/MPL-1.1.html or MPL.html for the full
 *    license text.
 * 
 *    Under MPL section 13. I declare that all of the Covered Code is
 *    Multiple Licensed:
 *      _________________________________________________________________
 * 
 *      The contents of this file are subject to the Mozilla Public License
 *      version 1.1 (the "License"); you may not use this file except in
 *      compliance with the License. You may obtain a copy of the License
 *      at http://www.mozilla.org/MPL/
 * 
 *      Software distributed under the License is distributed on an "AS IS"
 *      basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 *      the License for the specific language governing rights and
 *      limitations under the License.
 * 
 *      The Initial Developer of the Original Code is David Beckett.
 *      Portions created by David Beckett are Copyright (C) 2000 David
 *      Beckett, Institute for Learning and Research Technology, University
 *      of Bristol. All Rights Reserved.
 * 
 *      Alternatively, the contents of this file may be used under the
 *      terms of the GNU Lesser General Public License, in which case the
 *      provisions of the LGPL License are applicable instead of those
 *      above. If you wish to allow use of your version of this file only
 *      under the terms of the LGPL License and not to allow others to use
 *      your version of this file under the MPL, indicate your decision by
 *      deleting the provisions above and replace them with the notice and
 *      other provisions required by the LGPL License. If you do not delete
 *      the provisions above, a recipient may use your version of this file
 *      under either the MPL or the LGPL License.
 */


#include <rdf_config.h>

#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>

#define LIBRDF_INTERNAL 1
#include <librdf.h>

#include <rdf_storage.h>
#include <rdf_storage_hashes.h>
#include <rdf_storage_list.h>


/* prototypes for helper functions */
static void librdf_delete_storage_factories(void);


/* prototypes for functions implementing get_sources, arcs, targets
 * librdf_iterator via conversion from a librdf_stream of librdf_statement
 */
static int librdf_storage_stream_to_node_iterator_have_elements(void* iterator);
static void* librdf_storage_stream_to_node_iterator_get_next(void* iterator);
static void librdf_storage_stream_to_node_iterator_finished(void* iterator);


/**
 * librdf_init_storage - Initialise the librdf_storage module
 * 
 * Initialises and registers all
 * compiled storage modules.  Must be called before using any of the storage
 * factory functions such as librdf_get_storage_factory()
 **/
void
librdf_init_storage(void) 
{
  /* Always have storage list, hashes implementations available */
  librdf_init_storage_hashes();
  librdf_init_storage_list();
}


/**
 * librdf_finish_storage - Terminate the librdf_storage module
 **/
void
librdf_finish_storage(void) 
{
  librdf_delete_storage_factories();
}


/* statics */

/* list of storage factories */
static librdf_storage_factory* storages;


/* helper functions */


/*
 * librdf_delete_storage_factories - helper function to delete all the registered storage factories
 */
static void
librdf_delete_storage_factories(void)
{
  librdf_storage_factory *factory, *next;
  
  for(factory=storages; factory; factory=next) {
    next=factory->next;
    LIBRDF_FREE(librdf_storage_factory, factory->name);
    LIBRDF_FREE(librdf_storage_factory, factory);
  }
}


/* class methods */

/**
 * librdf_storage_register_factory - Register a storage factory
 * @name: the storage factory name
 * @factory: pointer to function to call to register the factory
 * 
 **/
void
librdf_storage_register_factory(const char *name,
				void (*factory) (librdf_storage_factory*)) 
{
  librdf_storage_factory *storage, *h;
  char *name_copy;
  
#if defined(LIBRDF_DEBUG) && LIBRDF_DEBUG > 1
  LIBRDF_DEBUG2(librdf_storage_register_factory,
                "Received registration for storage %s\n", name);
#endif
  
  storage=(librdf_storage_factory*)LIBRDF_CALLOC(librdf_storage_factory, 1,
                                                 sizeof(librdf_storage_factory));
  if(!storage)
    LIBRDF_FATAL1(librdf_storage_register_factory, "Out of memory\n");
  
  name_copy=(char*)LIBRDF_CALLOC(cstring, strlen(name)+1, 1);
  if(!name_copy) {
    LIBRDF_FREE(librdf_storage, storage);
    LIBRDF_FATAL1(librdf_storage_register_factory, "Out of memory\n");
  }
  strcpy(name_copy, name);
  storage->name=name_copy;
        
  for(h = storages; h; h = h->next ) {
    if(!strcmp(h->name, name_copy)) {
      LIBRDF_FATAL2(librdf_storage_register_factory,
                    "storage %s already registered\n", h->name);
    }
  }
  
  /* Call the storage registration function on the new object */
  (*factory)(storage);
  
#if defined(LIBRDF_DEBUG) && LIBRDF_DEBUG > 1
  LIBRDF_DEBUG3(librdf_storage_register_factory, "%s has context size %d\n",
                name, storage->context_length);
#endif
  
  storage->next = storages;
  storages = storage;
}


/**
 * librdf_get_storage_factory - Get a storage factory by name
 * @name: the factory name or NULL for the default factory
 * 
 * Return value: the factory object or NULL if there is no such factory
 **/
librdf_storage_factory*
librdf_get_storage_factory (const char *name) 
{
  librdf_storage_factory *factory;

  /* return 1st storage if no particular one wanted - why? */
  if(!name) {
    factory=storages;
    if(!factory) {
      LIBRDF_DEBUG1(librdf_get_storage_factory, 
                    "No (default) storages registered\n");
      return NULL;
    }
  } else {
    for(factory=storages; factory; factory=factory->next) {
      if(!strcmp(factory->name, name)) {
        break;
      }
    }
    /* else FACTORY name not found */
    if(!factory) {
      LIBRDF_DEBUG2(librdf_get_storage_factory,
                    "No storage with name %s found\n",
                    name);
      return NULL;
    }
  }
        
  return factory;
}



/**
 * librdf_new_storage - Constructor - create a new librdf_storage object
 * @storage_name: the storage factory name
 * @name: an identifier for the storage
 * @options_string: options to initialise storage
 *
 * The options are encoded as described in librdf_hash_from_string()
 * and can be NULL if none are required.
 *
 * Return value: a new &librdf_storage object or NULL on failure
 */
librdf_storage*
librdf_new_storage (char *storage_name, char *name, 
                    char *options_string) {
  librdf_storage_factory* factory;
  librdf_hash* options_hash;
  
  factory=librdf_get_storage_factory(storage_name);
  if(!factory)
    return NULL;

  options_hash=librdf_new_hash(NULL);
  if(!options_hash)
    return NULL;

  if(librdf_hash_open(options_hash, NULL, 0, 1, 1, NULL)) {
    librdf_free_hash(options_hash);
    return NULL;
  }
  
  if(librdf_hash_from_string(options_hash, options_string)) {
    librdf_free_hash(options_hash);
    return NULL;
  }

  return librdf_new_storage_from_factory(factory, name, options_hash);
}


/**
 * librdf_new_storage_from_storage - Copy constructor - create a new librdf_storage object from an existing one
 * @storage: the existing storage &librdf_storage to use
 *
 * Should create a new storage in the same context as the existing one
 * as appropriate for the storage.  For example, in a RDBMS storage
 * it would be a new database, or in on disk it would be a new
 * set of files.  This will mean automatically generating
 * a new identifier for the storage, maybe based on the existing
 * storage identifier.
 *
 * Return value: a new &librdf_storage object or NULL on failure
 */
librdf_storage*
librdf_new_storage_from_storage(librdf_storage* old_storage) 
{
  librdf_storage* new_storage;

  /* FIXME: fail if clone is not supported by this storage (factory) */
  if(!old_storage->factory->clone) {
    LIBRDF_FATAL2(librdf_new_storage_from_storage, "clone not implemented for factory type %s", old_storage->factory->name);
    return NULL;
  }

  new_storage=(librdf_storage*)LIBRDF_CALLOC(librdf_storage, 1,
                                             sizeof(librdf_storage));
  if(!new_storage)
    return NULL;
  
  new_storage->context=(char*)LIBRDF_CALLOC(librdf_storage_context, 1,
                                            old_storage->factory->context_length);
  if(!new_storage->context) {
    librdf_free_storage(new_storage);
    return NULL;
  }
  
  if(old_storage->factory->clone(new_storage, old_storage)) {
    librdf_free_storage(new_storage);
    return NULL;

  }

  /* do this now so librdf_free_storage won't call new factory on
   * partially copied storage 
   */
  new_storage->factory=old_storage->factory;
  
  return new_storage;
}


/**
 * librdf_new_storage_from_factory - Constructor - create a new librdf_storage object
 * @factory: the factory to use to construct the storage
 * @name: name to use for storage
 * @options: &librdf_hash of options to initialise storage
 *
 * If the options are present, they become owned by the storage
 * and should no longer be used.
 *
 * Return value: a new &librdf_storage object or NULL on failure
 */
librdf_storage*
librdf_new_storage_from_factory (librdf_storage_factory* factory,
                                 char *name,
                                 librdf_hash* options) {
  librdf_storage* storage;

  if(!factory) {
    LIBRDF_DEBUG1(librdf_new_storage, "No factory given\n");
    librdf_free_hash(options);
    return NULL;
  }
  
  storage=(librdf_storage*)LIBRDF_CALLOC(librdf_storage, 1,
                                         sizeof(librdf_storage));
  if(!storage) {
    librdf_free_hash(options);
    return NULL;
  }
  
  
  storage->context=(char*)LIBRDF_CALLOC(librdf_storage_context, 1,
                                        factory->context_length);
  if(!storage->context) {
    librdf_free_hash(options);
    librdf_free_storage(storage);
    return NULL;
  }
  
  storage->factory=factory;
  
  if(factory->init(storage, name, options)) {
    librdf_free_hash(options);
    librdf_free_storage(storage);
    return NULL;
  }
  
  return storage;
}


/**
 * librdf_free_storage - Destructor - destroy a librdf_storage object
 * @storage: &librdf_storage object
 * 
 **/
void
librdf_free_storage (librdf_storage* storage) 
{
  if(storage->factory)
    storage->factory->terminate(storage);

  if(storage->context)
    LIBRDF_FREE(librdf_storage_context, storage->context);
  LIBRDF_FREE(librdf_storage, storage);
}


/* methods */

/**
 * librdf_storage_open - Start a model / storage association
 * @storage: &librdf_storage object
 * @model: model stored
 *
 * This is ended with librdf_storage_close()
 * 
 * Return value: non 0 on failure
 **/
int
librdf_storage_open(librdf_storage* storage, librdf_model* model) 
{
  return storage->factory->open(storage, model);
}


/**
 * librdf_storage_close - End a model / storage association
 * @storage: &librdf_storage object
 * 
 * Return value: non 0 on failure
 **/
int
librdf_storage_close(librdf_storage* storage)
{
  return storage->factory->close(storage);
}


/**
 * librdf_storage_size - Get the number of statements stored
 * @storage: &librdf_storage object
 * 
 * Return value: The number of statements
 **/
int
librdf_storage_size(librdf_storage* storage) 
{
  return storage->factory->size(storage);
}


/**
 * librdf_storage_add_statement - Add a statement to a storage
 * @storage: &librdf_storage object
 * @statement: &librdf_statement statement to add
 * 
 * Return value: non 0 on failure
 **/
int
librdf_storage_add_statement(librdf_storage* storage,
                             librdf_statement* statement) 
{
  return storage->factory->add_statement(storage, statement);
}


/**
 * librdf_storage_add_statements - Add a stream of statements to the storage
 * @storage: &librdf_storage object
 * @statement_stream: &librdf_stream of statements
 * 
 * Return value: non 0 on failure
 **/
int
librdf_storage_add_statements(librdf_storage* storage,
                              librdf_stream* statement_stream) 
{
  return storage->factory->add_statements(storage, statement_stream);
}


/**
 * librdf_storage_remove_statement - Remove a statement from the storage
 * @storage: &librdf_storage object
 * @statement: &librdf_statement statement to remove
 * 
 * Return value: non 0 on failure
 **/
int
librdf_storage_remove_statement(librdf_storage* storage, 
                                librdf_statement* statement) 
{
  return storage->factory->remove_statement(storage, statement);
}


/**
 * librdf_storage_contains_statement - Test if a given statement is present in the storage
 * @storage: &librdf_storage object
 * @statement: &librdf_statement statement to check
 *
 * Return value: non 0 if the storage contains the statement
 **/
int
librdf_storage_contains_statement(librdf_storage* storage,
                                  librdf_statement* statement) 
{
  return storage->factory->contains_statement(storage, statement);
}


/**
 * librdf_storage_serialise - Serialise the storage as a librdf_stream of statemetns
 * @storage: &librdf_storage object
 * 
 * Return value: &librdf_stream of statements or NULL on failure
 **/
librdf_stream*
librdf_storage_serialise(librdf_storage* storage) 
{
  return storage->factory->serialise(storage);
}


/**
 * librdf_storage_find_statements - search the storage for matching statements
 * @storage: &librdf_storage object
 * @statement: &librdf_statement partial statement to find
 * 
 * Searches the storage for a (partial) statement - see librdf_statement_match() - and return a stream matching &librdf_statement.
 * 
 * Return value:  &librdf_stream of matching statements (may be empty) or NULL on failure
 **/
librdf_stream*
librdf_storage_find_statements(librdf_storage* storage,
                               librdf_statement* statement) 
{
  return storage->factory->find_statements(storage, statement);
}


typedef struct {
  librdf_stream *stream;
  librdf_statement *partial_statement;
#define LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_SOURCES 0
#define LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_ARCS 1
#define LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_TARGETS 2
  int want;
} librdf_storage_stream_to_node_iterator_context;


static int
librdf_storage_stream_to_node_iterator_have_elements(void* iterator)
{
  librdf_storage_stream_to_node_iterator_context* context=(librdf_storage_stream_to_node_iterator_context*)iterator;

  return !librdf_stream_end(context->stream);
}


static void*
librdf_storage_stream_to_node_iterator_get_next(void* iterator) 
{
  librdf_storage_stream_to_node_iterator_context* context=(librdf_storage_stream_to_node_iterator_context*)iterator;
  librdf_statement* statement;
  librdf_node* node;
  
  statement=librdf_stream_next(context->stream);
  if(!statement)
    return NULL;

  switch(context->want) {
    case LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_SOURCES: /* SOURCES (subjects) */
      node=librdf_statement_get_subject(statement);
      librdf_statement_set_subject(statement, NULL);
      break;
      
    case LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_ARCS: /* ARCS (predicates) */
      node=librdf_statement_get_predicate(statement);
      librdf_statement_set_predicate(statement, NULL);
      break;
      
    case LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_TARGETS: /* TARGETS (objects) */
      node=librdf_statement_get_object(statement);
      librdf_statement_set_object(statement, NULL);
      break;
      
    default: /* error */
    abort();
  }
  
  librdf_free_statement(statement);

  return (void*)node;
}


static void
librdf_storage_stream_to_node_iterator_finished(void* iterator) 
{
  librdf_storage_stream_to_node_iterator_context* context=(librdf_storage_stream_to_node_iterator_context*)iterator;
  librdf_statement *partial_statement=context->partial_statement;

  /* make sure librdf_free_statement() doesn't free anything here */
  if(partial_statement) {
    librdf_statement_set_subject(partial_statement, NULL);
    librdf_statement_set_predicate(partial_statement, NULL);
    librdf_statement_set_object(partial_statement, NULL);

    librdf_free_statement(partial_statement);
  }

  if(context->stream)
    librdf_free_stream(context->stream);

  LIBRDF_FREE(librdf_storage_stream_to_node_iterator_context, context);
}


/**
 * librdf_storage_get_sources - return the sources (subjects) of arc in an RDF graph given arc (predicate) and target (object)
 * @storage: &librdf_storage object
 * @arc: &librdf_node arc
 * @target: &librdf_node target
 * 
 * Searches the storage for arcs matching the given arc and target
 * and returns a list of the source &librdf_node objects as an iterator
 * 
 * Return value:  &librdf_iterator of &librdf_node objects (may be empty) or NULL on failure
 **/
librdf_iterator*
librdf_storage_get_sources(librdf_storage *storage,
                           librdf_node *arc, librdf_node *target) 
{
  librdf_statement *partial_statement;
  librdf_stream *stream;
  librdf_storage_stream_to_node_iterator_context* context;
  librdf_iterator *iterator;
  
  if (storage->factory->find_sources)
    return storage->factory->find_sources(storage, arc, target);

  partial_statement=librdf_new_statement();
  if(!partial_statement)
    return NULL;
  
  context=(librdf_storage_stream_to_node_iterator_context*)LIBRDF_CALLOC(librdf_storage_stream_to_node_iterator_context, 1, sizeof(librdf_storage_stream_to_node_iterator_context));
  if(!context) {
    librdf_free_statement(partial_statement);
    return NULL;
  }
  
  /*  librdf_statement_set_subject(partial_statement, NULL); */
  librdf_statement_set_predicate(partial_statement, arc);
  librdf_statement_set_object(partial_statement, target);

  stream=storage->factory->find_statements(storage, partial_statement);
  if(!stream) {
    librdf_storage_stream_to_node_iterator_finished(context);
    return NULL;
  }
  
  /* initialise context */
  context->partial_statement=partial_statement;
  context->stream=stream;
  context->want=LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_SOURCES;
  
  iterator=librdf_new_iterator((void*)context,
                               librdf_storage_stream_to_node_iterator_have_elements,
                               librdf_storage_stream_to_node_iterator_get_next,
                               librdf_storage_stream_to_node_iterator_finished);
  if(!iterator)
    librdf_storage_stream_to_node_iterator_finished(context);

  return iterator;
}


/**
 * librdf_storage_get_arcs - return the arcs (predicates) of an arc in an RDF graph given source (subject) and target (object)
 * @storage: &librdf_storage object
 * @source: &librdf_node source
 * @target: &librdf_node target
 * 
 * Searches the storage for arcs matching the given source and target
 * and returns a list of the arc &librdf_node objects as an iterator
 * 
 * Return value:  &librdf_iterator of &librdf_node objects (may be empty) or NULL on failure
 **/
librdf_iterator*
librdf_storage_get_arcs(librdf_storage *storage,
                        librdf_node *source, librdf_node *target) 
{
  librdf_statement *partial_statement;
  librdf_stream *stream;
  librdf_storage_stream_to_node_iterator_context* context;
  librdf_iterator *iterator;
  
  if (storage->factory->find_arcs)
    return storage->factory->find_arcs(storage, source, target);

  partial_statement=librdf_new_statement();
  if(!partial_statement)
    return NULL;
  
  context=(librdf_storage_stream_to_node_iterator_context*)LIBRDF_CALLOC(librdf_storage_stream_to_node_iterator_context, 1, sizeof(librdf_storage_stream_to_node_iterator_context));
  if(!context) {
    librdf_free_statement(partial_statement);
    return NULL;
  }
  
  librdf_statement_set_subject(partial_statement, source);
  /* librdf_statement_set_predicate(partial_statement, NULL); */
  librdf_statement_set_object(partial_statement, target);

  stream=storage->factory->find_statements(storage, partial_statement);
  if(!stream) {
    librdf_storage_stream_to_node_iterator_finished(context);
    return NULL;
  }
  
  /* initialise context */
  context->partial_statement=partial_statement;
  context->stream=stream;
  context->want=LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_ARCS;
  
  iterator=librdf_new_iterator((void*)context,
                               librdf_storage_stream_to_node_iterator_have_elements,
                               librdf_storage_stream_to_node_iterator_get_next,
                               librdf_storage_stream_to_node_iterator_finished);
  if(!iterator)
    librdf_storage_stream_to_node_iterator_finished(context);

  return iterator;
}


/**
 * librdf_storage_get_targets - return the targets (objects) of an arc in an RDF graph given source (subject) and arc (predicate)
 * @storage: &librdf_storage object
 * @source: &librdf_node source
 * @arc: &librdf_node arc
 * 
 * Searches the storage for targets matching the given source and arc
 * and returns a list of the source &librdf_node objects as an iterator
 * 
 * Return value:  &librdf_iterator of &librdf_node objects (may be empty) or NULL on failure
 **/
librdf_iterator*
librdf_storage_get_targets(librdf_storage *storage,
                           librdf_node *source, librdf_node *arc) 
{
  librdf_statement *partial_statement;
  librdf_stream *stream;
  librdf_storage_stream_to_node_iterator_context* context;
  librdf_iterator *iterator;
  
  if (storage->factory->find_targets)
    return storage->factory->find_targets(storage, source, arc);

  partial_statement=librdf_new_statement();
  if(!partial_statement)
    return NULL;
  
  context=(librdf_storage_stream_to_node_iterator_context*)LIBRDF_CALLOC(librdf_storage_stream_to_node_iterator_context, 1, sizeof(librdf_storage_stream_to_node_iterator_context));
  if(!context) {
    librdf_free_statement(partial_statement);
    return NULL;
  }
  
  librdf_statement_set_subject(partial_statement, source);
  librdf_statement_set_predicate(partial_statement, arc);
  /* librdf_statement_set_object(partial_statement, NULL); */

  stream=storage->factory->find_statements(storage, partial_statement);
  if(!stream) {
    librdf_storage_stream_to_node_iterator_finished(context);
    return NULL;
  }
  
  /* initialise context */
  context->partial_statement=partial_statement;
  context->stream=stream;
  context->want=LIBRDF_STREAM_TO_NODE_ITERATOR_WANT_TARGETS;
  
  iterator=librdf_new_iterator((void*)context,
                               librdf_storage_stream_to_node_iterator_have_elements,
                               librdf_storage_stream_to_node_iterator_get_next,
                               librdf_storage_stream_to_node_iterator_finished);
  if(!iterator)
    librdf_storage_stream_to_node_iterator_finished(context);

  return iterator;
}





#ifdef STANDALONE

/* one more prototype */
int main(int argc, char *argv[]);


int
main(int argc, char *argv[]) 
{
  librdf_storage* storage;
  char *program=argv[0];
  
  /* initialise hash, model and storage modules */
  librdf_init_hash();
  librdf_init_storage();
  librdf_init_model();
  
  fprintf(stdout, "%s: Creating storage\n", program);
  storage=librdf_new_storage(NULL, "test", NULL);
  if(!storage) {
    fprintf(stderr, "%s: Failed to create new storage\n", program);
    return(1);
  }

  
  fprintf(stdout, "%s: Opening storage\n", program);
  if(librdf_storage_open(storage, NULL)) {
    fprintf(stderr, "%s: Failed to open storage\n", program);
    return(1);
  }


  /* Can do nothing here since need model and storage working */

  fprintf(stdout, "%s: Closing storage\n", program);
  librdf_storage_close(storage);

  fprintf(stdout, "%s: Freeing storage\n", program);
  librdf_free_storage(storage);
  

  /* finish model and storage modules */
  librdf_finish_model();
  librdf_finish_storage();
  librdf_finish_hash();
  
  
#ifdef LIBRDF_MEMORY_DEBUG 
  librdf_memory_report(stderr);
#endif
  
  /* keep gcc -Wall happy */
  return(0);
}

#endif
