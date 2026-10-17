// System.Collections(.Generic) subset with .NET-compatible ordering semantics.
#pragma once
#include "core.h"
#include <algorithm>
#include <functional>

namespace System { namespace Collections {

namespace Generic {

template<class T> struct IEqualityComparer : Interface
{
    virtual bool Equals(T a, T b) = 0;
    virtual int32_t GetHashCode(T a) = 0;
};
template<class T> struct ICollection : IEnumerable<T> {};
template<class T> struct IList : ICollection<T> {};

template<class K, class V> struct KeyValuePair
{
    K Key{};
    V Value{};
    KeyValuePair() {}
    KeyValuePair(const K& k, const V& v) : Key(k), Value(v) {}
    K get_Key() const { return Key; }
    V get_Value() const { return Value; }
};

template<class T> struct List;
template<class T> struct List_Enumerator
{
    List<T>* list = nullptr;
    int32_t index = 0;
    T current{};
    bool MoveNext();
    T get_Current() const { return current; }
    void Dispose() {}
};

template<class T> struct ListEnumObj final : Object, IEnumerator_<T>
{
    List<T>* list; int32_t index = -1;
    explicit ListEnumObj(List<T>* l) : list(l) {}
    bool MoveNext() override;
    T get_Current() override;
};

template<class T> struct List final : Object, IList<T>
{
    ::System::Array<T>* items = nullptr;
    int32_t size = 0;
    int32_t version = 0;

    List() { items = ::System::Array<T>::New(0); }
    explicit List(int32_t capacity) { items = ::System::Array<T>::New(capacity < 0 ? 0 : capacity); }
    template<class E, std::enable_if_t<std::is_class_v<E>, int> = 0> explicit List(E* src) { items = ::System::Array<T>::New(0); AddRange(src); }

    void EnsureCapacity(int32_t n)
    {
        if (items->length >= n) return;
        int32_t cap = items->length == 0 ? 4 : items->length * 2;
        if (cap < n) cap = n;
        auto* ni = ::System::Array<T>::New(cap);
        for (int32_t i = 0; i < size; i++) ni->data[i] = items->data[i];
        items = ni;
    }
    int32_t get_Count() const { return size; }
    int32_t get_Capacity() const { return items->length; }
    T& operator[](int32_t i) { if ((uint32_t)i >= (uint32_t)size) ::sys::throw_index(); return items->data[i]; }
    T get_Item(int32_t i) { if ((uint32_t)i >= (uint32_t)size) ::sys::throw_index(); return items->data[i]; }
    void set_Item(int32_t i, const T& v) { if ((uint32_t)i >= (uint32_t)size) ::sys::throw_index(); items->data[i] = v; version++; }
    void Add(const T& v) { if (size == items->length) EnsureCapacity(size + 1); items->data[size++] = v; version++; }
    template<class E> void AddRange(E* src);
    void Clear() { for (int32_t i = 0; i < size; i++) items->data[i] = T{}; size = 0; version++; }
    bool Contains(const T& v) { return IndexOf(v) >= 0; }
    int32_t IndexOf(const T& v) { for (int32_t i = 0; i < size; i++) if (::sys::eq<T>(items->data[i], v)) return i; return -1; }
    int32_t LastIndexOf(const T& v) { for (int32_t i = size - 1; i >= 0; i--) if (::sys::eq<T>(items->data[i], v)) return i; return -1; }
    void Insert(int32_t idx, const T& v)
    {
        if ((uint32_t)idx > (uint32_t)size) ::sys::throw_index();
        if (size == items->length) EnsureCapacity(size + 1);
        for (int32_t i = size; i > idx; i--) items->data[i] = items->data[i - 1];
        items->data[idx] = v; size++; version++;
    }
    bool Remove(const T& v) { int32_t i = IndexOf(v); if (i < 0) return false; RemoveAt(i); return true; }
    void RemoveAt(int32_t idx)
    {
        if ((uint32_t)idx >= (uint32_t)size) ::sys::throw_index();
        size--;
        for (int32_t i = idx; i < size; i++) items->data[i] = items->data[i + 1];
        items->data[size] = T{}; version++;
    }
    void RemoveRange(int32_t idx, int32_t count) { for (int32_t i = 0; i < count; i++) RemoveAt(idx); }
    template<class P> int32_t RemoveAll(P* pred)
    {
        int32_t j = 0;
        for (int32_t i = 0; i < size; i++)
        {
            T v = items->data[i];
            if (!pred->Invoke(v)) items->data[j++] = v;
        }
        int32_t removed = size - j;
        for (int32_t i = j; i < size; i++) items->data[i] = T{};
        size = j; version++;
        return removed;
    }
    void Reverse() { std::reverse(items->data, items->data + size); version++; }
    void Sort() { std::stable_sort(items->data, items->data + size, [](const T& a, const T& b) { return ::sys::CompareTo(a, b) < 0; }); version++; }
    template<class C> void Sort(C* comparison)
    {
        std::stable_sort(items->data, items->data + size, [comparison](const T& a, const T& b) { return comparison->Invoke(a, b) < 0; });
        version++;
    }
    ::System::Array<T>* ToArray() { auto* a = ::System::Array<T>::New(size); for (int32_t i = 0; i < size; i++) a->data[i] = items->data[i]; return a; }
    template<class P> T Find(P* pred) { for (int32_t i = 0; i < size; i++) if (pred->Invoke(items->data[i])) return items->data[i]; return T{}; }
    template<class P> int32_t FindIndex(P* pred) { for (int32_t i = 0; i < size; i++) if (pred->Invoke(items->data[i])) return i; return -1; }
    template<class P> List* FindAll(P* pred) { auto* l = ::sys::gcnew<List>(); for (int32_t i = 0; i < size; i++) if (pred->Invoke(items->data[i])) l->Add(items->data[i]); return l; }
    template<class P> bool Exists(P* pred) { return FindIndex(pred) >= 0; }
    template<class A> void ForEach(A* act) { for (int32_t i = 0; i < size; i++) act->Invoke(items->data[i]); }
    List_Enumerator<T> GetEnumerator() { List_Enumerator<T> e; e.list = this; return e; }
    IEnumerator_<T>* GetEnumerator__I() override { return ::sys::gcnew<ListEnumObj<T>>(this); }
    List* GetRange(int32_t idx, int32_t count) { auto* l = ::sys::gcnew<List>(count); for (int32_t i = 0; i < count; i++) l->Add((*this)[idx + i]); return l; }
};

template<class T> bool List_Enumerator<T>::MoveNext()
{
    if (index < list->size) { current = list->items->data[index++]; return true; }
    index = list->size + 1; current = T{}; return false;
}
template<class T> bool ListEnumObj<T>::MoveNext() { index++; return index < list->size; }
template<class T> T ListEnumObj<T>::get_Current() { return list->items->data[index]; }


// -------------------------------------------------- Dictionary (.NET algorithm, insertion ordered)
template<class K> struct DefaultComparer
{
    IEqualityComparer<K>* custom = nullptr;
    bool eq(const K& a, const K& b) const { return custom ? custom->Equals(a, b) : ::sys::eq<K>(a, b); }
    int32_t hash(const K& a) const { return (custom ? custom->GetHashCode(a) : ::sys::hash<K>(a)) & 0x7FFFFFFF; }
};

template<class K, class V> struct Dictionary;
template<class K, class V> struct Dictionary_KeyCollection final : Object, IEnumerable<K>
{
    Dictionary<K, V>* dict = nullptr;
    explicit Dictionary_KeyCollection(Dictionary<K, V>* d) : dict(d) {}
    int32_t get_Count();
    IEnumerator_<K>* GetEnumerator__I() override;
};
template<class K, class V> struct Dictionary_ValueCollection final : Object, IEnumerable<V>
{
    Dictionary<K, V>* dict = nullptr;
    explicit Dictionary_ValueCollection(Dictionary<K, V>* d) : dict(d) {}
    int32_t get_Count();
    IEnumerator_<V>* GetEnumerator__I() override;
};

template<class K, class V> struct DictEntry { int32_t hashCode; int32_t next; K key; V value; };

template<class K, class V> struct Dictionary final : Object, IEnumerable<KeyValuePair<K, V>>
{
    ::System::Array<int32_t>* buckets = nullptr;
    ::System::Array<DictEntry<K, V>>* entries = nullptr;
    int32_t count = 0;
    int32_t freeList = -1;
    int32_t freeCount = 0;
    DefaultComparer<K> cmp;

    Dictionary() {}
    explicit Dictionary(int32_t capacity) { if (capacity > 0) Init(capacity); }
    explicit Dictionary(IEqualityComparer<K>* c) { cmp.custom = c; }
    template<class C, std::enable_if_t<std::is_base_of_v<IEqualityComparer<K>, C>, int> = 0> explicit Dictionary(C* c) { cmp.custom = c; }
    template<class D, std::enable_if_t<std::is_same_v<D, Dictionary>, int> = 0> explicit Dictionary(D* other) { for (int32_t i = 0; i < other->count; i++) if (other->entries->data[i].hashCode >= 0) Add(other->entries->data[i].key, other->entries->data[i].value); }

    static int32_t NextPrime(int32_t n)
    {
        static const int32_t primes[] = { 3, 7, 11, 17, 23, 29, 37, 47, 59, 71, 89, 107, 131, 163, 197, 239, 293, 353, 431, 521, 631, 761, 919, 1103, 1327, 1597, 1931, 2333, 2801, 3371, 4049, 4861, 5839, 7013, 8419, 10103, 12143, 14591, 17519, 21023, 25229, 30293, 36353, 43627, 52361, 62851, 75431, 90523, 108631, 130363, 156437, 187751, 225307, 270371, 324449, 389357, 467237, 560689, 672827, 807403, 968897 };
        for (int32_t p : primes) if (p >= n) return p;
        int32_t c = n | 1;
        for (;; c += 2) { bool prime = true; for (int32_t d = 3; d * d <= c; d += 2) if (c % d == 0) { prime = false; break; } if (prime) return c; }
    }
    void Init(int32_t cap)
    {
        int32_t size = NextPrime(cap);
        buckets = ::System::Array<int32_t>::New(size);
        for (int32_t i = 0; i < size; i++) buckets->data[i] = -1;
        entries = ::System::Array<DictEntry<K, V>>::New(size);
        freeList = -1;
    }
    void Resize()
    {
        int32_t newSize = NextPrime(count * 2);
        auto* nb = ::System::Array<int32_t>::New(newSize);
        for (int32_t i = 0; i < newSize; i++) nb->data[i] = -1;
        auto* ne = ::System::Array<DictEntry<K, V>>::New(newSize);
        for (int32_t i = 0; i < count; i++) ne->data[i] = entries->data[i];
        for (int32_t i = 0; i < count; i++)
            if (ne->data[i].hashCode >= 0)
            {
                int32_t b = ne->data[i].hashCode % newSize;
                ne->data[i].next = nb->data[b];
                nb->data[b] = i;
            }
        buckets = nb; entries = ne;
    }
    int32_t FindEntry(const K& key)
    {
        if (!buckets) return -1;
        int32_t h = cmp.hash(key);
        for (int32_t i = buckets->data[h % buckets->length]; i >= 0; i = entries->data[i].next)
            if (entries->data[i].hashCode == h && cmp.eq(entries->data[i].key, key)) return i;
        return -1;
    }
    void Insert(const K& key, const V& value, bool add)
    {
        if (!buckets) Init(0);
        int32_t h = cmp.hash(key);
        int32_t target = h % buckets->length;
        for (int32_t i = buckets->data[target]; i >= 0; i = entries->data[i].next)
            if (entries->data[i].hashCode == h && cmp.eq(entries->data[i].key, key))
            {
                if (add) ::sys::throw_msg("ArgumentException", "An item with the same key has already been added.");
                entries->data[i].value = value;
                return;
            }
        int32_t index;
        if (freeCount > 0) { index = freeList; freeList = entries->data[index].next; freeCount--; }
        else
        {
            if (count == entries->length) { Resize(); target = h % buckets->length; }
            index = count++;
        }
        auto& e = entries->data[index];
        e.hashCode = h; e.next = buckets->data[target]; e.key = key; e.value = value;
        buckets->data[target] = index;
    }
    void Add(const K& k, const V& v) { Insert(k, v, true); }
    bool ContainsKey(const K& k) { return FindEntry(k) >= 0; }
    bool ContainsValue(const V& v) { for (int32_t i = 0; i < count; i++) if (entries->data[i].hashCode >= 0 && ::sys::eq<V>(entries->data[i].value, v)) return true; return false; }
    V get_Item(const K& k)
    {
        int32_t i = FindEntry(k);
        if (i < 0) ::sys::throw_msg("KeyNotFoundException", "The given key was not present in the dictionary.");
        return entries->data[i].value;
    }
    void set_Item(const K& k, const V& v) { Insert(k, v, false); }
    bool TryGetValue(const K& k, V& out)
    {
        int32_t i = FindEntry(k);
        if (i >= 0) { out = entries->data[i].value; return true; }
        out = V{}; return false;
    }
    bool Remove(const K& key)
    {
        if (!buckets) return false;
        int32_t h = cmp.hash(key);
        int32_t b = h % buckets->length;
        int32_t last = -1;
        for (int32_t i = buckets->data[b]; i >= 0; last = i, i = entries->data[i].next)
            if (entries->data[i].hashCode == h && cmp.eq(entries->data[i].key, key))
            {
                if (last < 0) buckets->data[b] = entries->data[i].next;
                else entries->data[last].next = entries->data[i].next;
                entries->data[i].hashCode = -1;
                entries->data[i].next = freeList;
                entries->data[i].key = K{};
                entries->data[i].value = V{};
                freeList = i; freeCount++;
                return true;
            }
        return false;
    }
    void Clear()
    {
        if (count > 0)
        {
            for (int32_t i = 0; i < buckets->length; i++) buckets->data[i] = -1;
            for (int32_t i = 0; i < count; i++) entries->data[i] = DictEntry<K, V>{};
            freeList = -1; count = 0; freeCount = 0;
        }
    }
    int32_t get_Count() const { return count - freeCount; }
    Dictionary_KeyCollection<K, V>* get_Keys() { return ::sys::gcnew<Dictionary_KeyCollection<K, V>>(this); }
    Dictionary_ValueCollection<K, V>* get_Values() { return ::sys::gcnew<Dictionary_ValueCollection<K, V>>(this); }

    struct EnumObj final : Object, IEnumerator_<KeyValuePair<K, V>>
    {
        Dictionary* d; int32_t index = 0; KeyValuePair<K, V> cur;
        explicit EnumObj(Dictionary* dd) : d(dd) {}
        bool MoveNext() override
        {
            while (index < d->count)
            {
                auto& e = d->entries->data[index++];
                if (e.hashCode >= 0) { cur = KeyValuePair<K, V>(e.key, e.value); return true; }
            }
            return false;
        }
        KeyValuePair<K, V> get_Current() override { return cur; }
    };
    IEnumerator_<KeyValuePair<K, V>>* GetEnumerator__I() override { return ::sys::gcnew<EnumObj>(this); }
};

template<class K, class V> int32_t Dictionary_KeyCollection<K, V>::get_Count() { return dict->get_Count(); }
template<class K, class V> int32_t Dictionary_ValueCollection<K, V>::get_Count() { return dict->get_Count(); }
template<class K, class V> struct KeyEnumObj final : Object, IEnumerator_<K>
{
    Dictionary<K, V>* d; int32_t index = 0; K cur{};
    explicit KeyEnumObj(Dictionary<K, V>* dd) : d(dd) {}
    bool MoveNext() override { while (index < d->count) { auto& e = d->entries->data[index++]; if (e.hashCode >= 0) { cur = e.key; return true; } } return false; }
    K get_Current() override { return cur; }
};
template<class K, class V> struct ValEnumObj final : Object, IEnumerator_<V>
{
    Dictionary<K, V>* d; int32_t index = 0; V cur{};
    explicit ValEnumObj(Dictionary<K, V>* dd) : d(dd) {}
    bool MoveNext() override { while (index < d->count) { auto& e = d->entries->data[index++]; if (e.hashCode >= 0) { cur = e.value; return true; } } return false; }
    V get_Current() override { return cur; }
};
template<class K, class V> IEnumerator_<K>* Dictionary_KeyCollection<K, V>::GetEnumerator__I() { return ::sys::gcnew<KeyEnumObj<K, V>>(dict); }
template<class K, class V> IEnumerator_<V>* Dictionary_ValueCollection<K, V>::GetEnumerator__I() { return ::sys::gcnew<ValEnumObj<K, V>>(dict); }

// -------------------------------------------------- HashSet
template<class T> struct HashSet final : Object, IEnumerable<T>
{
    Dictionary<T, bool>* d;
    HashSet() { d = ::sys::gcnew<Dictionary<T, bool>>(); }
    template<class E, std::enable_if_t<std::is_class_v<E>, int> = 0> explicit HashSet(E* src) { d = ::sys::gcnew<Dictionary<T, bool>>(); UnionWith(src); }
    bool Add(const T& v) { if (d->ContainsKey(v)) return false; d->Add(v, true); return true; }
    bool Remove(const T& v) { return d->Remove(v); }
    bool Contains(const T& v) { return d->ContainsKey(v); }
    void Clear() { d->Clear(); }
    int32_t get_Count() { return d->get_Count(); }
    template<class E> void UnionWith(E* src);
    struct EnumObj final : Object, IEnumerator_<T>
    {
        Dictionary<T, bool>* d; int32_t index = 0; T cur{};
        explicit EnumObj(Dictionary<T, bool>* dd) : d(dd) {}
        bool MoveNext() override { while (index < d->count) { auto& e = d->entries->data[index++]; if (e.hashCode >= 0) { cur = e.key; return true; } } return false; }
        T get_Current() override { return cur; }
    };
    IEnumerator_<T>* GetEnumerator__I() override { return ::sys::gcnew<EnumObj>(d); }
};

// -------------------------------------------------- Stack / Queue
template<class T> struct Stack final : Object, IEnumerable<T>
{
    List<T>* l;
    Stack() { l = ::sys::gcnew<List<T>>(); }
    explicit Stack(int32_t cap) { l = ::sys::gcnew<List<T>>(cap); }
    void Push(const T& v) { l->Add(v); }
    T Pop() { if (l->size == 0) ::sys::throw_msg("InvalidOperationException", "Stack empty."); T v = l->items->data[l->size - 1]; l->RemoveAt(l->size - 1); return v; }
    T Peek() { if (l->size == 0) ::sys::throw_msg("InvalidOperationException", "Stack empty."); return l->items->data[l->size - 1]; }
    int32_t get_Count() { return l->size; }
    void Clear() { l->Clear(); }
    bool Contains(const T& v) { return l->Contains(v); }
    struct EnumObj final : Object, IEnumerator_<T>
    {
        List<T>* l; int32_t index;
        explicit EnumObj(List<T>* ll) : l(ll), index(ll->size) {}
        bool MoveNext() override { index--; return index >= 0; }
        T get_Current() override { return l->items->data[index]; }
    };
    IEnumerator_<T>* GetEnumerator__I() override { return ::sys::gcnew<EnumObj>(l); }
};

template<class T> struct Queue final : Object, IEnumerable<T>
{
    List<T>* l;
    Queue() { l = ::sys::gcnew<List<T>>(); }
    void Enqueue(const T& v) { l->Add(v); }
    T Dequeue() { if (l->size == 0) ::sys::throw_msg("InvalidOperationException", "Queue empty."); T v = l->items->data[0]; l->RemoveAt(0); return v; }
    T Peek() { if (l->size == 0) ::sys::throw_msg("InvalidOperationException", "Queue empty."); return l->items->data[0]; }
    int32_t get_Count() { return l->size; }
    void Clear() { l->Clear(); }
    bool Contains(const T& v) { return l->Contains(v); }
    IEnumerator_<T>* GetEnumerator__I() override { return l->GetEnumerator__I(); }
};

}}} // namespace System::Collections::Generic

namespace sys {

// ---------------------------------------------------------------- foreach support
template<class T> struct IndexRange
{
    T* data; int32_t n;
    T* begin() const { return data; }
    T* end() const { return data + n; }
};

// live list iteration (tolerates growth like an index loop)
template<class T> struct ListRange
{
    ::System::Collections::Generic::List<T>* l;
    struct It
    {
        ::System::Collections::Generic::List<T>* l; int32_t i;
        bool operator!=(const It& o) const { return i < l->size; }
        void operator++() { i++; }
        T operator*() const { return l->items->data[i]; }
    };
    It begin() const { if (!l) throw_null(); return { l, 0 }; }
    It end() const { return { l, 0 }; }
};

template<class T> struct EnumRange
{
    ::System::Collections::Generic::IEnumerator_<T>* e;
    struct It
    {
        ::System::Collections::Generic::IEnumerator_<T>* e; bool done;
        bool operator!=(const It&) const { return !done; }
        void operator++() { done = !e->MoveNext(); }
        T operator*() const { return e->get_Current(); }
    };
    It begin() const { return { e, !e->MoveNext() }; }
    It end() const { return { e, true }; }
};

struct ObjEnumRange
{
    ::System::Collections::IEnumerator* e;
    struct It
    {
        ::System::Collections::IEnumerator* e; bool done;
        bool operator!=(const It&) const { return !done; }
        void operator++() { done = !e->MoveNext(); }
        ::System::Object* operator*() const { return e->get_Current(); }
    };
    It begin() const { return { e, !e->MoveNext() }; }
    It end() const { return { e, true }; }
};

template<class T> ListRange<T> each(::System::Collections::Generic::List<T>* l) { return { l }; }
template<class T> IndexRange<T> each(::System::Array<T>* a) { if (!a) throw_null(); return { a->data, a->length }; }
template<class T> IndexRange<T> each(::System::Array2<T>* a) { if (!a) throw_null(); return { a->data, a->length }; }
inline IndexRange<const char16_t> each(::System::String* s) { if (!s) throw_null(); return { s->data, s->length }; }
template<class T, std::enable_if_t<std::is_base_of_v<::System::Collections::IEnumerable, T> && !std::is_base_of_v<::System::Collections::Generic::IEnumerable<::System::Object*>, T>, int> = 0>
ObjEnumRange each(T* e) { if (!e) throw_null(); return { e->GetEnumerator() }; }
inline ObjEnumRange each(::System::Collections::IEnumerator* e) { return { e }; }

template<class T> struct enum_elem;
template<class T> struct enum_elem<::System::Collections::Generic::IEnumerable<T>> { typedef T type; };
template<class T> auto enum_elem_of(::System::Collections::Generic::IEnumerable<T>*) -> T;

template<class C, class E = decltype(enum_elem_of(std::declval<C*>())), std::enable_if_t<!std::is_base_of_v<::System::Collections::IEnumerable, C>, int> = 0>
EnumRange<E> each(C* c)
{
    if (!c) throw_null();
    ::System::Collections::Generic::IEnumerable<E>* ie = c;
    return { ie->GetEnumerator__I() };
}

} // namespace sys

namespace System { namespace Collections { namespace Generic {
template<class T> template<class E> void List<T>::AddRange(E* src)
{
    if (!src) ::sys::throw_null();
    for (auto x : ::sys::each(src)) Add(x);
}
template<class T> template<class E> void HashSet<T>::UnionWith(E* src)
{
    if (!src) ::sys::throw_null();
    for (auto x : ::sys::each(src)) Add(x);
}
}}}

template<class T> System::String* System::String::Join(String* sep, Collections::Generic::IEnumerable<T>* enumerable)
{
    String* r = nullptr;
    for (auto x : ::sys::each(enumerable))
    {
        auto s = ::sys::ToStr(x);
        r = r ? Concat(Concat(r, sep), s) : s;
    }
    return r ? r : Empty();
}
