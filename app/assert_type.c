#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "app.h"

bool assert_type(Value *val, enum Type type, char *msg) {
  if (val == NULL) {
    fprintf(stderr, "%s", msg);
    fprintf(stderr, "Expected value got NULL\n");
    return false;
  }

  if (val->type != type) {
    fprintf(stderr, "%s", msg);
    fprintf(stderr, "Assertion failed. Expected %d, got %d\n", type, val->type);
    return false;
  }
  return true;
}
