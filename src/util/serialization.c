

#include "serialization.h"



/**
 * Look up the serialization functions for the given ID 
 **/
serialization_functions_t* find_serializtion_functions(serializer_id_t *id) 
{
  // FIXME: use a hashtable 

  // FIXME: use CIL to auto-register things
  return NULL;
}



/**
 * Find the length of the data, if it were to be serialized
 **/
int serialized_length(serializable_t *data)
{
  serialization_functions_t *funcs;

  // find the serialization functions
  funcs = find_serialization_functions( (serializer_id_t*)data );
  assert( funcs != NULL );

  // deserialize
  return funcs->serialize(data, buf, len);
}


/**
 * Serialize an object into the given buffer.  Return an error if we
 * would need to write more than len bytes.
 **/
int serialize(serializable_t *data, char *buf, int len)
{
  serialization_functions_t *funcs;

  // find the serialization functions
  funcs = find_serialization_functions( (serializer_id_t*)data );
  assert( funcs != NULL );

  // deserialize
  return funcs->serialize(data, buf, len);
}


/**
 * Deserialize from the given buffer
 **/
int deserialize(void **ret, char *buf, int len)
{
  serialization_functions_t *funcs;

  // determine the type of the object to deserialize
  id = *( (serializer_id_t*) buf );

  // find the serialization functions
  funcs = find_serialization_functions( (serlializer_id_t*)buf );
  assert( funcs != NULL );

  // deserialize
  return funcs->deserialize(ret, buf, len);
}


