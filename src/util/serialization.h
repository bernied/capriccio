
/**
 * Serialization routines
 **/


typedef int (*serialization_func)(void *struct, char *buf, int len);
typedef int (*deserialization_func)(void **return, char *buf, int len);
typedef int (*serialized_length_func)(void *struct);



typedef struct {
  serialization_func serialize;
  deserialization_func deserialize;
  serialized_length_func serialized_length;
  
} serialization_functions_t;




typedef struct {
  int id;
} serializer_id_t;


typedef void serializable_t;
