#include <vector>
#include <map>
#include <algorithm>

class IPtr;

template<typename T>
struct Destroyer
{
	static void Destroy(void* obj)
	{ 
		(*(T*)(obj)).~T();
	}
};

class MemoryManager
{
public:
	struct ObjectInfo
	{
		void*              object;
		size_t             size;
		bool               mark;
		const char*        source;
		int                line;
		std::vector<IPtr*> pointers;

		void(*destroy)(void*) = nullptr;
	};

private:
	static MemoryManager         instance;
	std::map<void*, ObjectInfo*> objects;
	std::vector<IPtr*>           pointers;
	size_t                       allocatedBytes = 0;
	bool                         currentMark = true;

public:
	static void CollectGarbage();

protected:
	void MarkObject(ObjectInfo* info);

	friend void* operator new(size_t size, const char* source, int line);
	friend void operator delete(void* object, const char* source, int line);
	friend void operator delete(void* object);

	template<typename T>
	friend class Ptr;
};

MemoryManager MemoryManager::instance;

class IPtr
{
protected:
	void*                      object;
	MemoryManager::ObjectInfo* info;

public:
	virtual ~IPtr() {}
	virtual bool IsRoot() const = 0;

protected:
	void MarkInvalid()
	{
		object = nullptr;
		info = nullptr;
	}

	friend void operator delete(void* object);
	friend class MemoryManager;
};

template<typename T>
class Ptr: public IPtr
{
public:
	Ptr()
	{
		object = nullptr;
		info = nullptr;

		MemoryManager::instance.pointers.push_back(this);
	}

	Ptr(T* object)
	{
		this->object = object;

		auto fnd = MemoryManager::instance.objects.find(object);
		if (fnd != MemoryManager::instance.objects.end())
		{
			info = fnd->second;
			info->pointers.push_back(this);

			if (!info->destroy)
				info->destroy = &Destroyer<T>::Destroy;
		}

		MemoryManager::instance.pointers.push_back(this);
	}

	Ptr(const Ptr<T>& other)
	{
		object = other.object;
		info = other.info;

		if (info)
			info->pointers.push_back(this);

		MemoryManager::instance.pointers.push_back(this);
	}

	~Ptr()
	{
		if (info)
		{
			auto fnd = std::find(info->pointers.begin(), info->pointers.end(), this);
			if (fnd != info->pointers.end())
				info->pointers.erase(fnd);
		}

		auto fnd = std::find(MemoryManager::instance.pointers.begin(), MemoryManager::instance.pointers.end(), this);
		if (fnd != MemoryManager::instance.pointers.end())
			MemoryManager::instance.pointers.erase(fnd);
	}

	T* Get() const
	{
		return (T*)object;
	}

	bool IsValid() const
	{
		return object != nullptr;
	}

	bool IsRoot() const
	{
		return false;
	}

	operator bool()
	{
		return object != nullptr;
	}

	operator T*()
	{
		return (T*)object;
	}

	T* operator->()
	{
		return (T*)object;
	}

	T& operator*()
	{
		return *(T*)object;
	}

	const T& operator*() const
	{
		return *(T*)object;
	}

	Ptr<T>& operator=(const Ptr<T>& other)
	{
		if (info)
		{
			auto fnd = std::find(info->pointers.begin(), info->pointers.end(), this);
			if (fnd != info->pointers.end())
				info->pointers.erase(fnd);
		}

		object = other.object;
		info = other.info;

		if (info)
			info->pointers.push_back(this);

		return *this;
	}

	Ptr<T>& operator=(T* other)
	{
		if (info)
		{
			auto fnd = std::find(info->pointers.begin(), info->pointers.end(), this);
			if (fnd != info->pointers.end())
				info->pointers.erase(fnd);
		}

		object = other; 
		
		auto fnd = MemoryManager::instance.objects.find(object);
		if (fnd != MemoryManager::instance.objects.end())
		{
			info = fnd->second;
			info->pointers.push_back(this);

			if (!info->destroy)
				info->destroy = &Destroyer<T>::Destroy;
		}
		else info = nullptr;

		return *this;
	}
};

template<typename T>
class RootPtr: public Ptr<T>
{
public:
	RootPtr():Ptr() {}

	RootPtr(T* object):Ptr(object) {}

	RootPtr(const Ptr<T>& other):Ptr(other) {}

	bool IsRoot() const
	{
		return true;
	}

	operator bool()
	{
		return object != nullptr;
	}

	operator T*()
	{
		return (T*)object;
	}

	T* operator->()
	{
		return (T*)object;
	}

	T& operator*()
	{
		return *(T*)object;
	}

	const T& operator*() const
	{
		return *(T*)object;
	}

	RootPtr<T>& operator=(const Ptr<T>& other)
	{
		Ptr<T>::operator=(other);
		return *this;
	}

	RootPtr<T>& operator=(T* other)
	{
		Ptr<T>::operator=(other);
		return *this;
	}
};

void* operator new(size_t size, const char* source, int line)
{
	void* res = malloc(size);

	MemoryManager::ObjectInfo* objInfo = new MemoryManager::ObjectInfo();
	objInfo->object = res;
	objInfo->size = size;
	objInfo->mark = MemoryManager::instance.currentMark;
	objInfo->source = source;
	objInfo->line = line;

	MemoryManager::instance.objects[res] = objInfo;
	MemoryManager::instance.allocatedBytes += size;

	return res;
}

void operator delete(void* data, const char* source, int line)
{
	delete data;
}

void operator delete(void* data)
{
	auto fnd = MemoryManager::instance.objects.find(data);

	if (fnd != MemoryManager::instance.objects.end())
	{
		MemoryManager::instance.allocatedBytes -= fnd->second->size;

		for (auto ptr : fnd->second->pointers)
			ptr->MarkInvalid();

		delete fnd->second;
		MemoryManager::instance.objects.erase(fnd);
	}

	free(data);
}

void MemoryManager::CollectGarbage()
{
	instance.currentMark = !instance.currentMark;

	for (auto ptr : instance.pointers)
	{
		if (ptr->IsRoot())
		{
			if (ptr->info)
				instance.MarkObject(ptr->info);
		}
	}

	std::vector< std::map<void*, ObjectInfo*>::iterator > freeObjects;
	for (auto obj = instance.objects.begin(); obj != instance.objects.end(); ++obj)
	{
		if (obj->second->mark != instance.currentMark)
			freeObjects.push_back(obj);
	}

	for (auto obj : freeObjects)
	{
		instance.allocatedBytes -= obj->second->size;

		obj->second->destroy(obj->first);
		free(obj->first);

		for (auto ptr : obj->second->pointers)
			ptr->MarkInvalid();

		delete obj->second;
		instance.objects.erase(obj);
	}
}

void MemoryManager::MarkObject(ObjectInfo* info)
{
	info->mark = MemoryManager::instance.currentMark;

	char* left = (char*)info->object;
	char* right = left + info->size;

	for (auto ptr : instance.pointers)
	{
		char* cptr = (char*)ptr;
		if (cptr >= left && cptr < right)
		{
			if (ptr->info && ptr->info->mark != MemoryManager::instance.currentMark)
				MarkObject(ptr->info);
		}
	}
}

#define mnew new(__FILE__, __LINE__)

struct B;
struct C;
struct D;

struct A
{
	Ptr<B> pb;
	Ptr<C> pc;

	A() { printf("A()\n"); }
	~A() { printf("~A()\n"); }
};

struct B
{
	Ptr<C> pc;

	B() { printf("B()\n"); }
	~B() { printf("~B()\n"); }
};

struct C
{
	Ptr<D> pd;

	C() { printf("C()\n"); }
	~C() { printf("~C()\n"); }
};

struct D
{
	Ptr<C> pc;

	D() { printf("D()\n"); }
	~D() { printf("~D()\n"); }
};

int main()
{
	RootPtr<A> pa = mnew A;

	pa->pb = mnew B;
	pa->pc = mnew C;

	pa->pc->pd = mnew D;
	pa->pc->pd->pc = pa->pc;

	pa->pc = nullptr;

	MemoryManager::CollectGarbage();

    return 0;
}
