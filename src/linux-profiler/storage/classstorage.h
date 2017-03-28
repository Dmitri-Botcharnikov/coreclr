#ifndef _CLASS_STORAGE_H_
#define _CLASS_STORAGE_H_

#include "mappedstorage.h"
#include "classinfo.h"

class ClassStorage : public MappedStorage<ClassID, ClassInfo>
{};

#endif // _CLASS_STORAGE_H_
