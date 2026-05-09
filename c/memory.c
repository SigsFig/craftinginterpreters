//> Chunks of Bytecode memory-c
#include <stdlib.h>

//> Garbage Collection memory-include-compiler
#include "compiler.h"
//< Garbage Collection memory-include-compiler
#include "memory.h"
//> Strings memory-include-vm
#include "vm.h"
//< Strings memory-include-vm
//> Garbage Collection debug-log-includes

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif
//< Garbage Collection debug-log-includes
//> Garbage Collection heap-grow-factor

#define GC_HEAP_GROW_FACTOR 2
//< Garbage Collection heap-grow-factor

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
//> Garbage Collection updated-bytes-allocated
  vm.bytesAllocated += newSize - oldSize;
//< Garbage Collection updated-bytes-allocated
//> Garbage Collection call-collect
  if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC
    collectGarbage();
#endif
//> collect-on-next

    if (vm.bytesAllocated > vm.nextGC) {
      collectGarbage();
    }
//< collect-on-next
  }

//< Garbage Collection call-collect
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
//> out-of-memory
  if (result == NULL) exit(1);
//< out-of-memory
  return result;
}
void retainObject(Obj* object) {
  if (object == NULL) return;
  object->refCount++;
}

void retainValue(Value value) {
  if (IS_OBJ(value)) retainObject(AS_OBJ(value));
}

void releaseObject(Obj* object) {
  if (object == NULL) return;
  if (object->refCount > 0) object->refCount--;
}

void releaseValue(Value value) {
  if (IS_OBJ(value)) releaseObject(AS_OBJ(value));
}

static void releaseChildren(Obj* object) {
  switch (objType(object)) {
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      releaseValue(bound->receiver);
      releaseObject((Obj*)bound->method);
      break;
    }
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      releaseObject((Obj*)klass->name);
      for (int i = 0; i < klass->methods.capacity; i++) {
        Entry* entry = &klass->methods.entries[i];
        if (!IS_EMPTY(entry->key)) releaseValue(entry->value);
      }
      break;
    }
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      releaseObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        releaseObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      releaseObject((Obj*)function->name);
      for (int i = 0; i < function->chunk.constants.count; i++) {
        releaseValue(function->chunk.constants.values[i]);
      }
      break;
    }
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      releaseObject((Obj*)instance->klass);
      for (int i = 0; i < instance->fields.capacity; i++) {
        Entry* entry = &instance->fields.entries[i];
        if (!IS_EMPTY(entry->key)) {
          releaseValue(entry->key);
          releaseValue(entry->value);
        }
      }
      break;
    }
    case OBJ_UPVALUE: {
      ObjUpvalue* upvalue = (ObjUpvalue*)object;
      if (upvalue->location == &upvalue->closed) {
        releaseValue(upvalue->closed);
      }
      break;
    }
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

//> Garbage Collection mark-object
void markObject(Obj* object) {
  if (object == NULL) return;
//> check-is-marked
  if (isMarked(object) == vm.markValue) return;

//< check-is-marked
//> log-mark-object
#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

//< log-mark-object
  setIsMarked(object, vm.markValue);
//> add-to-gray-stack

  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = (Obj**)realloc(vm.grayStack,
                                  sizeof(Obj*) * vm.grayCapacity);
//> exit-gray-stack

    if (vm.grayStack == NULL) exit(1);
//< exit-gray-stack
  }

  vm.grayStack[vm.grayCount++] = object;
//< add-to-gray-stack
}
//< Garbage Collection mark-object
//> Garbage Collection mark-value
void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}
//< Garbage Collection mark-value
//> Garbage Collection mark-array
static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}
//< Garbage Collection mark-array
//> Garbage Collection blacken-object
static void blackenObject(Obj* object) {
//> log-blacken-object
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

//< log-blacken-object
  switch (objType(object)) {
//> Methods and Initializers blacken-bound-method
    case OBJ_BOUND_METHOD: {
      ObjBoundMethod* bound = (ObjBoundMethod*)object;
      markValue(bound->receiver);
      markObject((Obj*)bound->method);
      break;
    }
//< Methods and Initializers blacken-bound-method
//> Classes and Instances blacken-class
    case OBJ_CLASS: {
      ObjClass* klass = (ObjClass*)object;
      markObject((Obj*)klass->name);
//> Methods and Initializers mark-methods
      markTable(&klass->methods);
//< Methods and Initializers mark-methods
      break;
    }
//< Classes and Instances blacken-class
//> blacken-closure
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
//< blacken-closure
//> blacken-function
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }
//< blacken-function
//> Classes and Instances blacken-instance
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      markObject((Obj*)instance->klass);
      markTable(&instance->fields);
      break;
    }
//< Classes and Instances blacken-instance
//> blacken-upvalue
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
//< blacken-upvalue
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}
//< Garbage Collection blacken-object
//> Strings free-object
static void freeObject(Obj* object) {
//> Garbage Collection log-free-object
#ifdef DEBUG_LOG_GC
  printf("%p free type %d\n", (void*)object, objType(object));
#endif

//< Garbage Collection log-free-object
  switch (objType(object)) {
//> Methods and Initializers free-bound-method
    case OBJ_BOUND_METHOD:
      FREE(ObjBoundMethod, object);
      break;
//< Methods and Initializers free-bound-method
//> Classes and Instances free-class
    case OBJ_CLASS: {
//> Methods and Initializers free-methods
      ObjClass* klass = (ObjClass*)object;
      freeTable(&klass->methods);
//< Methods and Initializers free-methods
      FREE(ObjClass, object);
      break;
    } // [braces]
//< Classes and Instances free-class
//> Closures free-closure
    case OBJ_CLOSURE: {
//> free-upvalues
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues,
                 closure->upvalueCount);
//< free-upvalues
      FREE(ObjClosure, object);
      break;
    }
//< Closures free-closure
//> Calls and Functions free-function
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
//< Calls and Functions free-function
//> Classes and Instances free-instance
    case OBJ_INSTANCE: {
      ObjInstance* instance = (ObjInstance*)object;
      freeTable(&instance->fields);
      FREE(ObjInstance, object);
      break;
    }
//< Classes and Instances free-instance
//> Calls and Functions free-native
    case OBJ_NATIVE:
      FREE(ObjNative, object);
      break;
//< Calls and Functions free-native
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      if (string->ownsChars) { // <--
        FREE_ARRAY(char, (char*)string->chars, string->length + 1);
      }
      FREE(ObjString, object);
      break;
    }
//> Closures free-upvalue
    case OBJ_UPVALUE:
      FREE(ObjUpvalue, object);
      break;
//< Closures free-upvalue
  }
}
//< Strings free-object
//> Garbage Collection mark-roots
static void markRoots() {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }
//> mark-closures

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }
//< mark-closures
//> mark-open-upvalues

  for (ObjUpvalue* upvalue = vm.openUpvalues;
       upvalue != NULL;
       upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }
//< mark-open-upvalues
//> mark-globals

  markTable(&vm.globals);
//< mark-globals
//> call-mark-compiler-roots
  markCompilerRoots();
//< call-mark-compiler-roots
//> Methods and Initializers mark-init-string
  markObject((Obj*)vm.initString);
//< Methods and Initializers mark-init-string
}
//< Garbage Collection mark-roots
//> Garbage Collection trace-references
static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}
//< Garbage Collection trace-references
//> Garbage Collection sweep
static void sweep() {
  Obj* object = vm.objects;
  while (object != NULL) {
    if (isMarked(object) != vm.markValue) {
      releaseChildren(object);
    }
    object = objNext(object);
  }

  Obj* previous = NULL;
  object = vm.objects;
  while (object != NULL) {
    if (isMarked(object) == vm.markValue) {
      previous = object;
      object = objNext(object);
    } else {
      Obj* unreached = object;
      object = objNext(object);
      if (previous != NULL) {
        setObjNext(previous, object);
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}
//< Garbage Collection sweep
//> Garbage Collection collect-garbage
void collectGarbage() {
//> log-before-collect
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
//> log-before-size
  size_t before = vm.bytesAllocated;
//< log-before-size
#endif
//< log-before-collect
//> call-mark-roots

  markRoots();
//< call-mark-roots
//> call-trace-references
  traceReferences();
//< call-trace-references
//> sweep-strings
  tableRemoveWhite(&vm.strings);
//< sweep-strings
//> call-sweep
  sweep();
//< call-sweep
//> update-next-gc

  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
  vm.markValue = !vm.markValue;
//< update-next-gc
//> log-after-collect

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
//> log-collected-amount
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
//< log-collected-amount
#endif
//< log-after-collect
}
//< Garbage Collection collect-garbage
//> Strings free-objects
void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = objNext(object);
    freeObject(object);
    object = next;
  }
//> Garbage Collection free-gray-stack

  free(vm.grayStack);
//< Garbage Collection free-gray-stack
}
//< Strings free-objects
